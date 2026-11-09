// GR2 through the player's granny2.dll (granny.h). The file's data tree is walked by its own member definitions,
// the layout LSLib's GR2Reader reads (64-bit files: packed members, 8-byte pointers).
#include "granny.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hairconv::granny {

namespace {
// granny_member_type (the values LSLib's MemberType uses).
enum : int32_t {
  kEnd = 0, kInline = 1, kReference = 2, kReferenceToArray = 3, kArrayOfReferences = 4, kVariantReference = 5,
  kReferenceToVariantArray = 7, kString = 8, kTransform = 9, kReal32 = 10, kInt8 = 11, kUInt8 = 12, kBinormalInt8 = 13,
  kNormalUInt8 = 14, kInt16 = 15, kUInt16 = 16, kBinormalInt16 = 17, kNormalUInt16 = 18, kInt32 = 19, kUInt32 = 20,
  kReal16 = 21, kEmptyReference = 22,
};
#pragma pack(push, 1)
struct MemberDef {   // granny_data_type_definition as stored in a 64-bit file (44 bytes)
  int32_t type;
  const char* name;
  const MemberDef* ref;
  int32_t width;
  int32_t extra[3];
  uint64_t ignored;
};
struct Variant { const MemberDef* type; const uint8_t* object; };
#pragma pack(pop)
static_assert(sizeof(MemberDef) == 44, "granny member definition layout");

template <class T> T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

struct Walker {
  std::string error;
  int depth = 0;

  size_t struct_size(const MemberDef* t) {
    if (!t) return 0;
    if (++depth > 32) { error = "GR2 type nesting too deep"; --depth; return 0; }
    size_t s = 0;
    for (int i = 0; t[i].type != kEnd; ++i) {
      if (i > 4096 || t[i].type < 0 || t[i].type > kEmptyReference) { error = "GR2 type definition not recognised"; break; }
      s += member_size(t[i]);
    }
    --depth;
    return s;
  }
  size_t member_size(const MemberDef& m) {
    size_t s = 0;
    switch (m.type) {
      case kInline: s = struct_size(m.ref); break;
      case kReference: case kString: case kEmptyReference: s = 8; break;
      case kReferenceToArray: case kArrayOfReferences: s = 12; break;
      case kVariantReference: s = 16; break;
      case kReferenceToVariantArray: s = 20; break;
      case kTransform: s = 68; break;
      case kReal32: case kInt32: case kUInt32: s = 4; break;
      case kInt8: case kUInt8: case kBinormalInt8: case kNormalUInt8: s = 1; break;
      case kInt16: case kUInt16: case kBinormalInt16: case kNormalUInt16: case kReal16: s = 2; break;
      default: s = 0;
    }
    return s * size_t(std::max(1, m.width));
  }
  // The member named `name` of an object of type t: its definition and data, or null.
  const uint8_t* field(const MemberDef* t, const uint8_t* obj, const char* name, const MemberDef** def) {
    if (!t || !obj) return nullptr;
    size_t at = 0;
    for (int i = 0; t[i].type != kEnd && i <= 4096; ++i) {
      if (t[i].name && std::strcmp(t[i].name, name) == 0) { *def = &t[i]; return obj + at; }
      at += member_size(t[i]);
    }
    return nullptr;
  }
  std::string string_at(const MemberDef* t, const uint8_t* obj, const char* name) {
    const MemberDef* d = nullptr;
    const uint8_t* p = field(t, obj, name, &d);
    if (!p || d->type != kString) return {};
    const char* s = rd<const char*>(p);
    return s ? std::string(s) : std::string();
  }
  const uint8_t* reference(const MemberDef* t, const uint8_t* obj, const char* name, const MemberDef** type) {
    const MemberDef* d = nullptr;
    const uint8_t* p = field(t, obj, name, &d);
    if (!p || d->type != kReference) return nullptr;
    *type = d->ref;
    return rd<const uint8_t*>(p);
  }
  // ReferenceToArray: (count, items of the member's type).
  const uint8_t* array(const MemberDef* t, const uint8_t* obj, const char* name, const MemberDef** type, int32_t* count) {
    const MemberDef* d = nullptr;
    const uint8_t* p = field(t, obj, name, &d);
    *count = 0;
    if (!p || d->type != kReferenceToArray) return nullptr;
    *type = d->ref;
    *count = rd<int32_t>(p);
    return rd<const uint8_t*>(p + 4);
  }
  // ArrayOfReferences: (count, pointers to objects of the member's type).
  const uint8_t* const* refs(const MemberDef* t, const uint8_t* obj, const char* name, const MemberDef** type, int32_t* count) {
    const MemberDef* d = nullptr;
    const uint8_t* p = field(t, obj, name, &d);
    *count = 0;
    if (!p || d->type != kArrayOfReferences) return nullptr;
    *type = d->ref;
    *count = rd<int32_t>(p);
    return rd<const uint8_t* const*>(p + 4);
  }
};

float half_to_float(uint16_t h) {
  const uint32_t s = uint32_t(h >> 15) << 31, e = (h >> 10) & 31, m = h & 1023;
  uint32_t bits;
  if (e == 0) {
    if (!m) bits = s;
    else {   // subnormal: exact as a float
      const float v = std::ldexp(float(m), -24);
      std::memcpy(&bits, &v, 4);
      bits |= s;
    }
  } else if (e == 31) bits = s | 0x7F800000u | (m << 13);
  else bits = s | ((e + 112) << 23) | (m << 13);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

// Reads `n` components of a vertex member as floats (Real32 or Real16; missing: zeros).
void components(const MemberDef* d, const uint8_t* p, int n, float* out) {
  for (int k = 0; k < n; ++k) out[k] = 0;
  if (!d || !p) return;
  const int w = std::min(n, std::max(1, d->width));
  for (int k = 0; k < w; ++k) {
    if (d->type == kReal32) out[k] = rd<float>(p + 4 * k);
    else if (d->type == kReal16) out[k] = half_to_float(rd<uint16_t>(p + 2 * k));
  }
}
void bytes4(const MemberDef* d, const uint8_t* p, uint8_t* out) {
  for (int k = 0; k < 4; ++k) out[k] = 0;
  if (!d || !p || (d->type != kUInt8 && d->type != kNormalUInt8 && d->type != kInt8)) return;
  for (int k = 0; k < std::min(4, std::max(1, d->width)); ++k) out[k] = p[k];
}

bool walk(const Variant& root, File& out, std::string* error) {
  Walker w;
  out = File{};
  const MemberDef* mesh_t = nullptr;
  int32_t nmesh = 0;
  const uint8_t* const* meshes = w.refs(root.type, root.object, "Meshes", &mesh_t, &nmesh);
  if (!w.error.empty() || nmesh < 0) { *error = w.error.empty() ? "GR2 has no mesh list" : w.error; return false; }
  for (int32_t mi = 0; mi < nmesh && meshes; ++mi) {
    const uint8_t* mo = meshes[mi];
    Mesh m;
    m.name = w.string_at(mesh_t, mo, "Name");
    const MemberDef *vd_t = nullptr, *topo_t = nullptr;
    const uint8_t* vd = w.reference(mesh_t, mo, "PrimaryVertexData", &vd_t);
    const uint8_t* topo = w.reference(mesh_t, mo, "PrimaryTopology", &topo_t);
    if (vd) {
      const MemberDef* d = nullptr;
      const uint8_t* p = w.field(vd_t, vd, "Vertices", &d);
      if (p && d->type == kReferenceToVariantArray) {
        const MemberDef* vt = rd<const MemberDef*>(p);
        const int32_t nv = rd<int32_t>(p + 8);
        const uint8_t* items = rd<const uint8_t*>(p + 12);
        const size_t stride = w.struct_size(vt);
        if (!w.error.empty() || (nv > 0 && (!items || !stride))) { *error = "GR2 vertex type not recognised"; return false; }
        const MemberDef *dp = nullptr, *du = nullptr, *dj = nullptr, *dw = nullptr;
        const uint8_t* p0 = nv > 0 ? items : nullptr;
        const uint8_t* fp = w.field(vt, p0, "Position", &dp);
        const uint8_t* fu = w.field(vt, p0, "TextureCoordinates0", &du);
        const uint8_t* fj = w.field(vt, p0, "BoneIndices", &dj);
        const uint8_t* fw = w.field(vt, p0, "BoneWeights", &dw);
        const size_t op = fp ? size_t(fp - p0) : 0, ou = fu ? size_t(fu - p0) : 0, oj = fj ? size_t(fj - p0) : 0,
                     ow = fw ? size_t(fw - p0) : 0;
        m.pos.resize(size_t(std::max(nv, 0)) * 3);
        m.uv.resize(size_t(std::max(nv, 0)) * 2);
        m.joints.resize(size_t(std::max(nv, 0)) * 4);
        m.weights.resize(size_t(std::max(nv, 0)) * 4);
        for (int32_t v = 0; v < nv; ++v) {
          const uint8_t* e = items + size_t(v) * stride;
          components(fp ? dp : nullptr, e + op, 3, &m.pos[size_t(v) * 3]);
          components(fu ? du : nullptr, e + ou, 2, &m.uv[size_t(v) * 2]);
          bytes4(fj ? dj : nullptr, e + oj, &m.joints[size_t(v) * 4]);
          bytes4(fw ? dw : nullptr, e + ow, &m.weights[size_t(v) * 4]);
        }
      }
    }
    if (topo) {
      // Indices (32-bit) when present and non-empty, else Indices16 (gr2_dump.ps1).
      for (const char* key : {"Indices", "Indices16"}) {
        const MemberDef* et = nullptr;
        int32_t n = 0;
        const uint8_t* items = w.array(topo_t, topo, key, &et, &n);
        if (!items || n <= 0 || !et) continue;
        const size_t stride = w.struct_size(et);
        const int32_t kind = et[0].type;
        if (!stride || (kind != kInt32 && kind != kUInt32 && kind != kInt16 && kind != kUInt16)) continue;
        m.indices.resize(size_t(n));
        for (int32_t i = 0; i < n; ++i) {
          const uint8_t* e = items + size_t(i) * stride;
          m.indices[size_t(i)] = kind == kInt32 || kind == kUInt32 ? rd<uint32_t>(e) : rd<uint16_t>(e);
        }
        break;
      }
    }
    const MemberDef* bt = nullptr;
    int32_t nb = 0;
    const uint8_t* binds = w.array(mesh_t, mo, "BoneBindings", &bt, &nb);
    const size_t bstride = w.struct_size(bt);
    for (int32_t b = 0; b < nb && binds && bstride; ++b) m.bindings.push_back(w.string_at(bt, binds + size_t(b) * bstride, "BoneName"));
    out.meshes.push_back(std::move(m));
  }
  const MemberDef* skel_t = nullptr;
  int32_t nskel = 0;
  const uint8_t* const* skels = w.refs(root.type, root.object, "Skeletons", &skel_t, &nskel);
  for (int32_t si = 0; si < nskel && skels; ++si) {
    const MemberDef* bt = nullptr;
    int32_t nb = 0;
    const uint8_t* bones = w.array(skel_t, skels[si], "Bones", &bt, &nb);
    const size_t stride = w.struct_size(bt);
    for (int32_t b = 0; b < nb && bones && stride; ++b) {
      const uint8_t* bo = bones + size_t(b) * stride;
      Bone bone{};
      bone.name = w.string_at(bt, bo, "Name");
      const MemberDef* d = nullptr;
      // BG3's bone type names it InverseWorldTransform; stock Granny's granny_bone InverseWorld4x4.
      const uint8_t* p = w.field(bt, bo, "InverseWorldTransform", &d);
      if (!p) p = w.field(bt, bo, "InverseWorld4x4", &d);
      if (p && d->type == kReal32 && d->width >= 16) std::memcpy(bone.inverse_world, p, 64);
      out.bones.push_back(std::move(bone));
    }
  }
  if (!w.error.empty()) { *error = w.error; return false; }
  return true;
}

// Structured exception guard: a GR2 whose tree does not follow the expected layout must fail one style, not the run.
bool walk_guarded(const Variant& root, File& out, std::string* error, bool* faulted) {
  __try {
    return walk(root, out, error);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *faulted = true;
    return false;
  }
}
// Granny's log (warnings and errors), kept for the error text of a failed read (calls are serialised by mu_).
std::string g_log;
struct LogCallback { void (*fn)(int, int, const char*, int, const char*, void*); void* user; };
void on_log(int type, int, const char*, int, const char* message, void*) {
  if (type >= 1 && message && g_log.size() < 2000) g_log += (g_log.empty() ? "" : "; ") + std::string(message);
}
}  // namespace

Library::~Library() {
  if (module_) FreeLibrary(static_cast<HMODULE>(module_));
}

bool Library::load(const std::wstring& dll, std::string* error) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    if (module_) FreeLibrary(static_cast<HMODULE>(module_));
    module_ = nullptr;
    return false;
  };
  if (dll.empty()) return fail("no granny2.dll given (--granny <path>)");
  if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) return fail("granny2.dll not found at the given path");
  // The exact file the player named; its own folder for any dependency, never the current directory.
  HMODULE h = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!h) return fail("granny2.dll could not be loaded (error " + std::to_string(GetLastError()) + "; a 64-bit granny2.dll is needed)");
  module_ = h;
  begin_ = reinterpret_cast<Begin>(GetProcAddress(h, "GrannyBeginFileDecompression"));
  incremental_ = reinterpret_cast<Incremental>(GetProcAddress(h, "GrannyDecompressIncremental"));
  end_ = reinterpret_cast<End>(GetProcAddress(h, "GrannyEndFileDecompression"));
  data_ = reinterpret_cast<Data>(GetProcAddress(h, "GrannyDecompressData"));
  auto ver = reinterpret_cast<const char* (*)()>(GetProcAddress(h, "GrannyGetVersionString"));
  if (!begin_ || !incremental_ || !end_ || !data_) return fail("the given DLL is not granny2.dll (Granny decompression entry points missing)");
  version_ = ver ? ver() : "unknown";
  if (auto set_log = reinterpret_cast<void (*)(const LogCallback*)>(GetProcAddress(h, "GrannySetLogCallback"))) {
    static const LogCallback cb{on_log, nullptr};
    set_log(&cb);
  }
  return true;
}

bool Library::decompress(uint32_t format, const uint8_t* src, uint32_t n, uint8_t* dst, uint32_t usize, uint32_t stop0,
                         uint32_t stop1) {
  if (format == 0) { if (n < usize) return false; std::memcpy(dst, src, usize); return true; }
  if (!module_) return false;   // compressed sections need the player's granny2.dll
  std::vector<uint8_t> in(src, src + n);   // granny reads past the end on some streams: padded copy
  in.resize(size_t(n) + 64, 0);
  if (format == 3 || format == 4) {
    // BitKnit: incremental decompression in 8 KB steps with a 16 KB work buffer (as LSLib's Granny2Compressor).
    std::vector<uint8_t> work(0x4000);
    void* state = begin_(int(format), false, int(usize), dst, int(work.size()), work.data());
    if (!state) return false;
    bool ok = true;
    for (uint32_t at = 0; at < n && ok; at += 0x2000) ok = incremental_(state, int(std::min<uint32_t>(0x2000, n - at)), in.data() + at);
    return end_(state) && ok;
  }
  return data_(int(format), false, int(n), in.data(), int(stop0), int(stop1), int(usize), dst);
}

bool Library::read(const uint8_t* d, size_t size, File& out, std::string* error) {
  std::lock_guard<std::mutex> lock(mu_);
  g_log.clear();
  auto fail = [&](const std::string& why) {
    *error = why + (g_log.empty() ? std::string() : " (granny: " + g_log + ")");
    return false;
  };
  // Without a loaded granny2.dll only uncompressed sections can be read (the CTest's synthetic GR2).
  // Magic block (32 bytes) then the v7 header (72 bytes) and the section table (LSLib GR2 Header / SectionHeader).
  static const uint8_t kLe64[16] = {0xE5, 0x2F, 0x4A, 0xE1, 0x6F, 0xC2, 0x8A, 0xEE, 0x1E, 0xD2, 0xB4, 0x4C, 0x90, 0xD7, 0x55, 0xAF};
  if (size < 104 || size > 0x7FFFFFFF) return fail("GR2 too small");
  if (std::memcmp(d, kLe64, 16) != 0) return fail("not a 64-bit little-endian GR2 (the layout BG3 uses)");
  if (rd<uint32_t>(d + 20) != 0) return fail("GR2 header format not supported");
  const uint32_t version = rd<uint32_t>(d + 32), sections_offset = rd<uint32_t>(d + 44), nsec = rd<uint32_t>(d + 48);
  if (version != 7) return fail("GR2 version " + std::to_string(version) + " not supported (7 only)");
  const uint32_t root_type[2] = {rd<uint32_t>(d + 52), rd<uint32_t>(d + 56)}, root_node[2] = {rd<uint32_t>(d + 60), rd<uint32_t>(d + 64)};
  const size_t table = 32 + size_t(sections_offset);
  if (nsec == 0 || nsec > 64 || table + size_t(nsec) * 44 > size) return fail("GR2 section table out of range");
  struct Sec { uint32_t compression, offset, csize, usize, align, stop0, stop1, reloc_offset, relocs, mix_offset, mixes; };
  std::vector<Sec> secs(nsec);
  std::vector<std::vector<uint8_t>> data(nsec);
  for (uint32_t s = 0; s < nsec; ++s) {
    std::memcpy(&secs[s], d + table + size_t(s) * 44, 44);
    const Sec& h = secs[s];
    if (h.usize > (256u << 20) || size_t(h.offset) + h.csize > size) return fail("GR2 section " + std::to_string(s) + " out of range");
    data[s].assign(size_t(h.usize) + 8, 0);
    if (h.usize && !decompress(h.compression, d + h.offset, h.csize, data[s].data(), h.usize, h.stop0, h.stop1))
      return fail("GR2 section " + std::to_string(s) + " did not decompress (format " + std::to_string(h.compression) + ")" +
                  (module_ ? "" : ": granny2.dll is needed"));
  }
  // Relocations: (offset in section, target section, target offset) -> a real pointer in our copy. With BitKnit
  // sections the table itself is BitKnit-compressed behind a u32 size (LSLib ReadSectionRelocations).
  for (uint32_t s = 0; s < nsec; ++s) {
    const Sec& h = secs[s];
    if (!h.relocs) continue;
    const size_t bytes = size_t(h.relocs) * 12;
    std::vector<uint8_t> rel(bytes + 8);
    if (h.compression == 4) {
      if (size_t(h.reloc_offset) + 4 > size) return fail("GR2 relocations out of range");
      const uint32_t cs = rd<uint32_t>(d + h.reloc_offset);
      if (size_t(h.reloc_offset) + 4 + cs > size || !decompress(4, d + h.reloc_offset + 4, cs, rel.data(), uint32_t(bytes), 0, 0))
        return fail("GR2 relocations did not decompress");
    } else {
      if (size_t(h.reloc_offset) + bytes > size) return fail("GR2 relocations out of range");
      std::memcpy(rel.data(), d + h.reloc_offset, bytes);
    }
    for (uint32_t r = 0; r < h.relocs; ++r) {
      const uint32_t at = rd<uint32_t>(&rel[r * 12]), ts = rd<uint32_t>(&rel[r * 12 + 4]), to = rd<uint32_t>(&rel[r * 12 + 8]);
      if (size_t(at) + 8 > h.usize || ts >= nsec || to > secs[ts].usize) return fail("GR2 relocation out of range");
      const uint8_t* target = data[ts].data() + to;
      std::memcpy(&data[s][at], &target, 8);
    }
  }
  if (root_type[0] >= nsec || root_node[0] >= nsec || root_type[1] >= secs[root_type[0]].usize ||
      root_node[1] >= secs[root_node[0]].usize)
    return fail("GR2 root out of range");
  Variant root{reinterpret_cast<const MemberDef*>(data[root_type[0]].data() + root_type[1]), data[root_node[0]].data() + root_node[1]};
  bool faulted = false;
  const bool ok = walk_guarded(root, out, error, &faulted);
  if (faulted) return fail("GR2 data tree does not have the expected layout");
  return ok;
}

std::vector<uint8_t> dump_bytes(const File& f) {
  std::vector<uint8_t> o;
  auto u32 = [&](uint32_t v) { const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); o.insert(o.end(), p, p + 4); };
  auto f32 = [&](float v) { const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); o.insert(o.end(), p, p + 4); };
  auto str = [&](const std::string& s) { u32(uint32_t(s.size())); o.insert(o.end(), s.begin(), s.end()); };
  o.insert(o.end(), {'G', 'R', '2', 'D'});
  u32(1);
  u32(uint32_t(f.meshes.size()));
  for (const Mesh& m : f.meshes) {
    const size_t nv = m.pos.size() / 3;
    str(m.name);
    u32(uint32_t(nv));
    u32(uint32_t(m.indices.size()));
    u32(uint32_t(m.bindings.size()));
    for (const std::string& b : m.bindings) str(b);
    for (size_t v = 0; v < nv; ++v) {
      for (int k = 0; k < 3; ++k) f32(m.pos[v * 3 + k]);
      for (int k = 0; k < 2; ++k) f32(m.uv[v * 2 + k]);
      for (int k = 0; k < 4; ++k) o.push_back(m.joints[v * 4 + k]);
      for (int k = 0; k < 4; ++k) o.push_back(m.weights[v * 4 + k]);
    }
    for (uint32_t i : m.indices) u32(i);
  }
  u32(uint32_t(f.bones.size()));
  for (const Bone& b : f.bones) {
    str(b.name);
    for (float x : b.inverse_world) f32(x);
  }
  return o;
}

}  // namespace hairconv::granny