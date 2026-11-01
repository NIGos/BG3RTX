#pragma once
// Offline frame replay pack (tools/frame_replay.md): everything ONE production compute pass reads in
// one frame, as files plus a line-based manifest, so tools/frame_replay can rebuild the TLAS, bind
// the same descriptors and dispatch the real shader without the game. Header-only, no Vulkan calls:
// the layer/core writer and the harness share the pass contract and the manifest grammar.
//
// manifest.txt, one directive per line ('#' comments; file names have no spaces, relative to the pack):
//   bg3rtx_replay_pack 1
//   pass gi_hit_direct                      a kPasses entry (its shader and descriptor table)
//   defines A=1;B=2                         the shader variant's -D pairs ('-' none)
//   frame N                                 Push::frame of the captured dispatch
//   dispatch X Y Z                          workgroup counts
//   push FILE                               the 256 push-constant bytes
//   buffer S B BYTES [FILE]                 descriptor range of BYTES; FILE fills it from 0 (rest zero)
//   range S B OFFSET FILE [FOFF BYTES]      a further slice at OFFSET (sparse: only what instances use):
//                                           FILE whole, or BYTES of it from byte FOFF (many slices, one file)
//   image S B E FORMAT W H LAYERS MIPS FILE VkFormat number; mip-major, each mip all layers, packed
//   omitted S B E                           an image left out over the budget: bound NaN, so a
//                                           material lookup that needs it fails as "sampling" (6)
//   sampler S B E MAG MIN MIPMODE AU AV AW ANISO MAXLOD   (Vk enums as numbers; ANISO 0 = off)
//   instances FILE COUNT                    VkAccelerationStructureInstanceKHR x COUNT, TLAS order
//                                           (= InstInfo order); accelerationStructureReference is an
//                                           opaque BLAS key, the geometry comes from InstInfo
//   output S B FORMAT W H [FILE]            a storage image the pass writes; FILE: the live result
//   note TEXT                               free text (scene, build, keys)
// The geometry is NOT duplicated: each TLAS instance's InstInfo (set 1 b7: base, meta, rec, tris)
// names its non-indexed triangle list in the opaque soup (b6, camera-relative) or the mesh pool
// (b8, object space, kInfoPool), stride meta & 255, exactly as the layer builds the BLAS
// (layer/bvhsplit.cpp tri_geometry); opaque unless kInfoAlpha. So the pack holds those buffers
// (sparse ranges) and the instance array as read at submit.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>
#include <sstream>

namespace bg3::replay {
inline constexpr int kVersion = 1;
// Target size of one pack (tools/frame_replay.md "Size"): the writer drops images over it (omitted)
// largest first; buffers ranges that instances reference are never dropped.
inline constexpr uint64_t kPackBudgetBytes = 1ull << 30;

enum Kind : uint32_t { kUbo, kSsbo, kSampled2D, kSampled2DArray, kStorage, kSampler, kTlas };
struct Slot {
  uint32_t set, binding;
  Kind kind;
  uint32_t count;           // descriptor array size
  uint32_t storage_format;  // VkFormat the shader declares (kStorage), 0 otherwise
  const char* name;
};
// VkFormat numbers used by the contracts (no vulkan.h dependency here).
inline constexpr uint32_t kR32Sfloat = 100, kR16G16Sfloat = 83, kR32G32Sfloat = 103, kRgba16Sfloat = 97,
                          kRgba32Sfloat = 109, kRgba32Uint = 107, kRgba8Unorm = 37;

// gi_hit_direct (shaders/gi/gi_trace_rt_hit_direct.hlsl, core/gi.cpp hit_gi_dispatch, base variant):
// set 0 = the GI push set (common/native_hit_gi_bindings.h, as the compiled SPIR-V uses it),
// set 1 = the material AlphaDSL (shaders/rt/rt_common.hlsli + native_hit_material.hlsli).
inline constexpr Slot kGiHitDirect[] = {
    {0, 0, kUbo, 1, 0, "cam"},          {0, 1, kUbo, 1, 0, "vp"},
    {0, 2, kSampled2D, 1, 0, "gNormal"}, {0, 5, kSampled2D, 1, 0, "gDepth"},
    {0, 8, kStorage, 1, kRgba16Sfloat, "giRaw"},        {0, 11, kStorage, 1, kRgba16Sfloat, "prevRadiance"},
    {0, 13, kStorage, 1, kR32Sfloat, "giRawDepth"},     {0, 17, kSampled2D, 1, 0, "gVelocity"},
    {0, 18, kTlas, 1, 0, "gScene"},      {0, 20, kSsbo, 1, 0, "rtStats"},
    {0, 21, kUbo, 1, 0, "lFlags"},       {0, 22, kSsbo, 1, 0, "lScale"},
    {0, 23, kSsbo, 1, 0, "lClusters"},   {0, 24, kSsbo, 1, 0, "lIndex"},
    {0, 25, kSsbo, 1, 0, "lLights"},     {0, 26, kUbo, 1, 0, "lConsts"},
    {0, 29, kUbo, 1, 0, "cam2"},         {0, 40, kStorage, 1, kRgba16Sfloat, "rtOrigin"},
    {0, 46, kStorage, 1, kRgba32Uint, "hdSlots"},
    {1, 0, kSsbo, 1, 0, "gAlphaRec"},    {1, 1, kSsbo, 1, 0, "gAlphaSoup"},
    {1, 2, kSampled2D, 4096, 0, "gAlphaTex"},           {1, 3, kSampled2DArray, 16, 0, "gAlphaCache"},
    {1, 4, kSampler, 1, 0, "gAlphaWrap"}, {1, 5, kSampler, 1, 0, "gAlphaClamp"},
    {1, 6, kSsbo, 1, 0, "gOpaqueSoup"},  {1, 7, kSsbo, 1, 0, "gInstInfo"},
    {1, 8, kSsbo, 1, 0, "gMeshPool"},    {1, 9, kSsbo, 1, 0, "gSceneStamp"},
    {1, 12, kSsbo, 1, 0, "gHitRecords"}, {1, 13, kSsbo, 1, 0, "gHitBasis"},
    {1, 14, kSampled2DArray, 256, 0, "gHitCache"},      {1, 15, kSampler, 64, 0, "gHitSampler"},
};
struct Pass {
  const char* name;
  const char* shader;   // relative to shaders/
  const Slot* slots;
  uint32_t slot_count, set_count, push_bytes;
};
inline constexpr Pass kPasses[] = {
    {"gi_hit_direct", "gi/gi_trace_rt_hit_direct.hlsl", kGiHitDirect, uint32_t(std::size(kGiHitDirect)), 2, 256},
};
inline const Pass* find_pass(const std::string& name) {
  for (const Pass& p : kPasses) if (name == p.name) return &p;
  return nullptr;
}
inline const Slot* find_slot(const Pass& p, uint32_t set, uint32_t binding) {
  for (uint32_t i = 0; i < p.slot_count; ++i) if (p.slots[i].set == set && p.slots[i].binding == binding) return p.slots + i;
  return nullptr;
}
// InstInfo meta bits (layer/bvhsplit.cpp, shaders/rt/rt_common.hlsli).
inline constexpr uint32_t kInfoPool = 1u << 8, kInfoLegacy = 1u << 9, kInfoAlpha = 1u << 10, kInfoHair = 1u << 12;

// Bytes of a W x H x LAYERS mip chain of `format`; 0 = a format the pack does not carry.
inline uint32_t block_bytes(uint32_t f, uint32_t& block) {
  block = 1;
  switch (f) {
    case 9: case 13: return 1;                                     // R8 UNORM / UINT
    case 16: case 70: case 74: case 76: return 2;                  // RG8 UNORM, R16 UNORM / UINT / SFLOAT
    case 37: case 38: case 41: case 43: case 44: case 50: case 58: case 64: case 77: case 81: case 83:
    case 98: case 99: case 100: case 122: case 123: return 4;      // RGBA8, RGB10A2, RG16, R32, B10G11R11, E5B9G9R9
    case 97: case 91: case 103: case 102: return 8;                // RGBA16F, RGBA16 UNORM, RG32F, RG32UI
    case 107: case 109: return 16;                                 // RGBA32UI, RGBA32F
    case 131: case 132: case 133: case 134: case 139: case 140: block = 4; return 8;    // BC1, BC4
    case 135: case 136: case 137: case 138: case 141: case 142: case 143: case 144:
    case 145: case 146: block = 4; return 16;                                         // BC2, BC3, BC5, BC6H, BC7
    default: return 0;
  }
}
inline uint64_t image_bytes(uint32_t f, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips) {
  uint32_t block;
  const uint32_t b = block_bytes(f, block);
  uint64_t n = 0;
  for (uint32_t m = 0; m < mips; ++m) {
    const uint32_t mw = w >> m ? w >> m : 1, mh = h >> m ? h >> m : 1;
    n += uint64_t((mw + block - 1) / block) * ((mh + block - 1) / block) * b * layers;
  }
  return n;
}

struct Buffer { uint32_t set, binding; uint64_t bytes; std::string file; };
struct Range { uint32_t set, binding; uint64_t offset; std::string file; uint64_t file_offset = 0, bytes = 0; };   // bytes 0: whole file
struct Image { uint32_t set, binding, element, format, w, h, layers, mips; std::string file; };
struct Omitted { uint32_t set, binding, element; };
struct Sampler { uint32_t set, binding, element, mag, min, mip, au, av, aw; float aniso, max_lod; };
struct Output { uint32_t set, binding, format, w, h; std::string reference; };
struct Manifest {
  int version = 0;
  std::string pass, defines, push, instances;
  uint64_t frame = 0;
  uint32_t groups[3] = {0, 0, 0}, instance_count = 0;
  std::vector<Buffer> buffers;
  std::vector<Range> ranges;
  std::vector<Image> images;
  std::vector<Omitted> omitted;
  std::vector<Sampler> samplers;
  std::vector<Output> outputs;
  std::vector<std::string> notes;
};

// Parses manifest text; false with `error` set on the first malformed line.
inline bool parse(const std::string& text, Manifest& m, std::string& error) {
  std::istringstream lines(text);
  std::string line;
  int number = 0;
  while (std::getline(lines, line)) {
    ++number;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream t(line);
    std::string k;
    t >> k;
    bool ok = true;
    if (k == "bg3rtx_replay_pack") ok = bool(t >> m.version);
    else if (k == "pass") ok = bool(t >> m.pass);
    else if (k == "defines") { ok = bool(t >> m.defines); if (m.defines == "-") m.defines.clear(); }
    else if (k == "frame") ok = bool(t >> m.frame);
    else if (k == "dispatch") ok = bool(t >> m.groups[0] >> m.groups[1] >> m.groups[2]);
    else if (k == "push") ok = bool(t >> m.push);
    else if (k == "instances") ok = bool(t >> m.instances >> m.instance_count);
    else if (k == "buffer") { Buffer b{}; ok = bool(t >> b.set >> b.binding >> b.bytes); t >> b.file; m.buffers.push_back(b); }
    else if (k == "range") {
      Range r{}; ok = bool(t >> r.set >> r.binding >> r.offset >> r.file);
      if (ok && (t >> r.file_offset)) ok = bool(t >> r.bytes) && r.bytes; else r.file_offset = 0;
      m.ranges.push_back(r);
    }
    else if (k == "image") {
      Image i{}; ok = bool(t >> i.set >> i.binding >> i.element >> i.format >> i.w >> i.h >> i.layers >> i.mips >> i.file);
      ok = ok && i.w && i.h && i.layers && i.mips && image_bytes(i.format, i.w, i.h, i.layers, i.mips);
      m.images.push_back(i);
    } else if (k == "omitted") { Omitted o{}; ok = bool(t >> o.set >> o.binding >> o.element); m.omitted.push_back(o); }
    else if (k == "sampler") {
      Sampler s{}; ok = bool(t >> s.set >> s.binding >> s.element >> s.mag >> s.min >> s.mip >> s.au >> s.av >> s.aw >> s.aniso >> s.max_lod);
      m.samplers.push_back(s);
    } else if (k == "output") { Output o{}; ok = bool(t >> o.set >> o.binding >> o.format >> o.w >> o.h); t >> o.reference; m.outputs.push_back(o); }
    else if (k == "note") { std::string rest; std::getline(t, rest); m.notes.push_back(rest); }
    else ok = false;
    if (!ok) { error = "manifest line " + std::to_string(number) + ": " + line; return false; }
  }
  if (m.version != kVersion) { error = "not a version " + std::to_string(kVersion) + " replay pack"; return false; }
  if (!find_pass(m.pass)) { error = "unknown pass '" + m.pass + "'"; return false; }
  if (m.push.empty() || !m.groups[0] || !m.groups[1] || !m.groups[2]) { error = "push or dispatch missing"; return false; }
  return true;
}
// ---- writer side (layer/replay.cpp, tools/frame_replay.cpp --synthesize) ----

// The manifest text of `m` (parse(serialize(m)) == m for every field the grammar carries).
inline std::string serialize(const Manifest& m) {
  std::ostringstream o;
  o.precision(9);
  o << "bg3rtx_replay_pack " << (m.version ? m.version : kVersion) << "\n";
  o << "pass " << m.pass << "\n";
  o << "defines " << (m.defines.empty() ? "-" : m.defines) << "\n";
  o << "frame " << m.frame << "\n";
  for (const auto& n : m.notes) o << "note" << (n.empty() || n[0] == ' ' ? "" : " ") << n << "\n";
  o << "dispatch " << m.groups[0] << " " << m.groups[1] << " " << m.groups[2] << "\n";
  o << "push " << m.push << "\n";
  for (const auto& b : m.buffers) o << "buffer " << b.set << " " << b.binding << " " << b.bytes << (b.file.empty() ? "" : " " + b.file) << "\n";
  for (const auto& r : m.ranges) {
    o << "range " << r.set << " " << r.binding << " " << r.offset << " " << r.file;
    if (r.bytes) o << " " << r.file_offset << " " << r.bytes;
    o << "\n";
  }
  for (const auto& i : m.images)
    o << "image " << i.set << " " << i.binding << " " << i.element << " " << i.format << " " << i.w << " " << i.h << " " << i.layers << " "
      << i.mips << " " << i.file << "\n";
  for (const auto& x : m.omitted) o << "omitted " << x.set << " " << x.binding << " " << x.element << "\n";
  for (const auto& s : m.samplers)
    o << "sampler " << s.set << " " << s.binding << " " << s.element << " " << s.mag << " " << s.min << " " << s.mip << " " << s.au << " "
      << s.av << " " << s.aw << " " << s.aniso << " " << s.max_lod << "\n";
  if (!m.instances.empty()) o << "instances " << m.instances << " " << m.instance_count << "\n";
  for (const auto& x : m.outputs)
    o << "output " << x.set << " " << x.binding << " " << x.format << " " << x.w << " " << x.h << (x.reference.empty() ? "" : " " + x.reference) << "\n";
  return o.str();
}

// A byte span of a buffer. merge_spans sorts and joins spans that overlap or are at most `gap`
// bytes apart (the gap is copied too: fewer, larger copies), so each byte is written once.
struct Span { uint64_t offset, bytes; };
inline void merge_spans(std::vector<Span>& v, uint64_t gap) {
  std::vector<Span> in;
  for (const Span& s : v) if (s.bytes) in.push_back(s);
  std::sort(in.begin(), in.end(), [](const Span& a, const Span& b) { return a.offset < b.offset; });
  v.clear();
  for (const Span& s : in) {
    if (!v.empty() && s.offset <= v.back().offset + v.back().bytes + gap) {
      const uint64_t end = std::max(v.back().offset + v.back().bytes, s.offset + s.bytes);
      v.back().bytes = end - v.back().offset;
    } else {
      v.push_back(s);
    }
  }
}
inline uint64_t span_bytes(const std::vector<Span>& v) { uint64_t n = 0; for (const Span& s : v) n += s.bytes; return n; }

// The spans of the soup (b6) and pool (b8) that a frame's InstInfo entries (16 B each: base, meta,
// rec, tris; layer/bvhsplit.cpp) name, as the BLAS builds read them: tris * 3 vertices of stride
// meta & 255 from base, opaque soup unless kInfoPool. Legacy soup, hair AABBs and empty entries name
// nothing (frame_replay leaves them out of the TLAS too).
inline void instinfo_spans(const uint32_t* info, uint32_t count, std::vector<Span>& soup, std::vector<Span>& pool) {
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t base = info[i * 4], meta = info[i * 4 + 1], tris = info[i * 4 + 3], stride = meta & 255u;
    if (!tris || stride < 12 || (meta & (kInfoLegacy | kInfoHair))) continue;
    (meta & kInfoPool ? pool : soup).push_back({base, uint64_t(tris) * 3 * stride});
  }
}

// The budget (tools/frame_replay.md "Size"): `fixed` bytes are never dropped (set 0, geometry,
// records); the droppable images go largest first until the total fits. Returns the indices
// dropped; `over` when even the fixed part does not fit (the pack is then not written).
inline std::vector<size_t> drop_over_budget(uint64_t fixed, const std::vector<uint64_t>& droppable, uint64_t budget, bool& over) {
  std::vector<size_t> order(droppable.size()), dropped;
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return droppable[a] > droppable[b]; });
  uint64_t total = fixed;
  for (uint64_t b : droppable) total += b;
  over = fixed > budget;
  for (size_t i : order) {
    if (total <= budget) break;
    total -= droppable[i];
    dropped.push_back(i);
  }
  std::sort(dropped.begin(), dropped.end());
  return dropped;
}
}  // namespace bg3::replay
