// bg3rtx_hairconv --all inputs without game data (docs/RTX_HAIR_ROADMAP.md 2f): a synthetic LSPK v18 package written
// here (stored and LZ4 block entries, an LSF with LZ4-frame sections, a DXT5 mask, a zstd entry), a patch package that
// overrides one entry, the hair catalogue built from it, a synthetic 64-bit GR2 with uncompressed sections (header,
// section table, relocations and the data-tree walk, no granny2.dll needed), Pillow-compatible DXT5 decode, and the
// granny-less path of --all (exit 3, a clear "unavailable" status naming granny2.dll).
#include "../tools/hairconv/catalogue.h"
#include "../tools/hairconv/granny.h"
#include "../tools/hairconv/hairconv.h"
#include "../tools/hairconv/pak.h"
#include "../tools/hairconv/pipeline.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace fs = std::filesystem;
using namespace hairconv;
using Bytes = std::vector<uint8_t>;

static void put32(Bytes& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
static void put16(Bytes& o, uint16_t v) { o.push_back(uint8_t(v)); o.push_back(uint8_t(v >> 8)); }
static void put64(Bytes& o, uint64_t v) { for (int i = 0; i < 8; ++i) o.push_back(uint8_t(v >> (8 * i))); }
static void putf(Bytes& o, float f) { uint32_t v; std::memcpy(&v, &f, 4); put32(o, v); }
static void set32(Bytes& o, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) o[at + i] = uint8_t(v >> (8 * i)); }

// A small greedy LZ4 block compressor (the format's rules: matches >= 4 bytes, the last 5 bytes literals, no match
// starting in the last 12 bytes), so the reader's match and overlap paths are exercised.
static Bytes lz4_compress(const Bytes& in) {
  Bytes out;
  const size_t n = in.size();
  std::map<uint32_t, size_t> last;
  size_t anchor = 0, i = 0;
  auto emit = [&](size_t lit_end, size_t match_len, size_t offset) {
    const size_t lit = lit_end - anchor;
    const size_t ml = match_len ? match_len - 4 : 0;
    out.push_back(uint8_t((std::min<size_t>(lit, 15) << 4) | (match_len ? std::min<size_t>(ml, 15) : 0)));
    if (lit >= 15) { size_t r = lit - 15; while (r >= 255) { out.push_back(255); r -= 255; } out.push_back(uint8_t(r)); }
    out.insert(out.end(), in.begin() + anchor, in.begin() + lit_end);
    if (!match_len) return;
    put16(out, uint16_t(offset));
    if (ml >= 15) { size_t r = ml - 15; while (r >= 255) { out.push_back(255); r -= 255; } out.push_back(uint8_t(r)); }
  };
  while (n >= 13 && i + 12 < n) {
    uint32_t key;
    std::memcpy(&key, &in[i], 4);
    const auto it = last.find(key);
    const size_t cand = it == last.end() ? SIZE_MAX : it->second;
    last[key] = i;
    if (cand != SIZE_MAX && i - cand <= 65535) {
      size_t len = 4;
      while (i + len + 5 < n && in[cand + len] == in[i + len]) ++len;
      emit(i, len, i - cand);
      i += len;
      anchor = i;
    } else {
      ++i;
    }
  }
  emit(n, 0, 0);
  return out;
}
static Bytes lz4_frame(const Bytes& in) {   // one linked compressed block + end mark, no checksums
  Bytes out;
  put32(out, 0x184D2204u);
  out.push_back(0x40);   // version 1, linked blocks, no checksums, no content size
  out.push_back(0x40);   // 64 KB blocks
  out.push_back(0);      // header checksum (not verified)
  const Bytes c = lz4_compress(in);
  put32(out, uint32_t(c.size()));
  out.insert(out.end(), c.begin(), c.end());
  put32(out, 0);
  return out;
}

// ---- LSF v7, format 2 (12-byte node and attribute records), sections LZ4 when `lz4`
struct LNode { std::string name; int parent; std::vector<std::pair<std::string, std::pair<uint32_t, Bytes>>> attrs; };
static std::pair<uint32_t, Bytes> fs_attr(const std::string& s) { Bytes b(s.begin(), s.end()); b.push_back(0); return {22, b}; }
static std::pair<uint32_t, Bytes> u8_attr(uint8_t v) { return {1, Bytes{v}}; }
static Bytes lsf(const std::vector<LNode>& nodes, bool lz4) {
  std::vector<std::string> names;
  auto handle = [&](const std::string& s) {
    for (size_t i = 0; i < names.size(); ++i) if (names[i] == s) return uint32_t(i);
    names.push_back(s);
    return uint32_t(names.size() - 1);
  };
  Bytes nsec, asec, vsec;
  for (size_t i = 0; i < nodes.size(); ++i) {
    const LNode& n = nodes[i];
    put32(nsec, handle(n.name));
    put32(nsec, n.attrs.empty() ? uint32_t(-1) : 0);
    put32(nsec, uint32_t(n.parent));
    for (const auto& [k, v] : n.attrs) {
      put32(asec, handle(k));
      put32(asec, v.first | uint32_t(v.second.size()) << 6);
      put32(asec, uint32_t(i));
      vsec.insert(vsec.end(), v.second.begin(), v.second.end());
    }
  }
  Bytes ssec;
  put32(ssec, 1);
  put16(ssec, uint16_t(names.size()));
  for (const std::string& s : names) { put16(ssec, uint16_t(s.size())); ssec.insert(ssec.end(), s.begin(), s.end()); }
  Bytes o{'L', 'S', 'O', 'F'};
  put32(o, 7);
  put64(o, 0x0004000000000000ull);
  const Bytes* secs[4] = {&ssec, &nsec, &asec, &vsec};
  Bytes packed[4];
  for (int k = 0; k < 4; ++k) packed[k] = lz4 ? (k == 0 ? lz4_compress(*secs[k]) : lz4_frame(*secs[k])) : Bytes();
  // (uncompressed, on disk) pairs: strings, keys, nodes, attributes, values.
  put32(o, uint32_t(ssec.size())); put32(o, lz4 ? uint32_t(packed[0].size()) : 0);
  put32(o, 0); put32(o, 0);
  for (int k = 1; k < 4; ++k) { put32(o, uint32_t(secs[k]->size())); put32(o, lz4 ? uint32_t(packed[k].size()) : 0); }
  o.push_back(lz4 ? 2 : 0); o.push_back(0); put16(o, 0); put32(o, 2);
  for (int k = 0; k < 4; ++k) { const Bytes& s = lz4 ? packed[k] : *secs[k]; o.insert(o.end(), s.begin(), s.end()); }
  return o;
}

// ---- LSPK v18
struct PEntry { std::string name; Bytes data; int method; };   // 0 stored, 2 LZ4 block, 3 "zstd" (bytes as given)
static void write_pak(const fs::path& p, const std::vector<PEntry>& entries, uint8_t priority) {
  Bytes o(40, 0);
  Bytes list;
  for (const PEntry& e : entries) {
    const Bytes body = e.method == 2 ? lz4_compress(e.data) : e.data;
    const uint64_t off = o.size();
    o.insert(o.end(), body.begin(), body.end());
    Bytes rec(256, 0);
    std::memcpy(rec.data(), e.name.data(), e.name.size());
    put32(rec, uint32_t(off)); put16(rec, uint16_t(off >> 32)); rec.push_back(0); rec.push_back(uint8_t(e.method));
    put32(rec, uint32_t(body.size())); put32(rec, e.method ? uint32_t(e.data.size()) : 0);
    list.insert(list.end(), rec.begin(), rec.end());
  }
  const uint64_t list_off = o.size();
  const Bytes packed = lz4_compress(list);
  put32(o, uint32_t(entries.size())); put32(o, uint32_t(packed.size()));
  o.insert(o.end(), packed.begin(), packed.end());
  std::memcpy(o.data(), "LSPK", 4);
  set32(o, 4, 18);
  for (int i = 0; i < 8; ++i) o[8 + i] = uint8_t(list_off >> (8 * i));
  set32(o, 16, uint32_t(o.size() - list_off));
  o[20] = 0; o[21] = priority; o[38] = 1;
  std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(o.data()), std::streamsize(o.size()));
}

static Bytes dxt5_4x4() {
  Bytes d(128, 0);
  std::memcpy(d.data(), "DDS ", 4);
  set32(d, 4, 124); set32(d, 8, 0x1007); set32(d, 12, 4); set32(d, 16, 4);
  set32(d, 76, 32); set32(d, 80, 4); std::memcpy(&d[84], "DXT5", 4);
  // alpha 200 / 100, every pixel index 2 -> (6 * 200 + 100) / 7 = 185; colour c0 = red 4 (565), c1 = 0, index 2.
  const uint8_t blk[16] = {200, 100, 0x92, 0x24, 0x49, 0x92, 0x24, 0x49, 0x00, 0x20, 0x00, 0x00, 0xAA, 0xAA, 0xAA, 0xAA};
  d.insert(d.end(), blk, blk + 16);
  return d;
}

// ---- a 64-bit GR2 with one uncompressed section: types, objects, relocations (LSLib's layout).
struct Gr2Builder {
  Bytes s;
  std::vector<std::pair<uint32_t, uint32_t>> rel;   // (pointer field offset, target offset)
  uint32_t str(const std::string& t) { const uint32_t at = uint32_t(s.size()); s.insert(s.end(), t.begin(), t.end()); s.push_back(0); return at; }
  void ptr(uint32_t target) { rel.push_back({uint32_t(s.size()), target}); put64(s, 0); }
  void nullptr_() { put64(s, 0); }
  // member: type, name, ref (or none), width
  void member(int32_t type, uint32_t name, int64_t ref, int32_t width) {
    put32(s, uint32_t(type)); ptr(name);
    if (ref >= 0) ptr(uint32_t(ref)); else nullptr_();
    put32(s, uint32_t(width)); put32(s, 0); put32(s, 0); put32(s, 0); put64(s, 0);
  }
  void end() { for (int i = 0; i < 44; ++i) s.push_back(0); }
};

static Bytes synthetic_gr2() {
  Gr2Builder b;
  const uint32_t sMeshes = b.str("Meshes"), sSkel = b.str("Skeletons"), sName = b.str("Name"), sPvd = b.str("PrimaryVertexData"),
                 sTopo = b.str("PrimaryTopology"), sBind = b.str("BoneBindings"), sVerts = b.str("Vertices"), sPos = b.str("Position"),
                 sUv = b.str("TextureCoordinates0"), sI16 = b.str("Indices16"), sInt = b.str("Int16"), sBone = b.str("BoneName"),
                 sBones = b.str("Bones"), sInv = b.str("InverseWorldTransform"), sMesh = b.str("HAIR_Test_Mesh"), sHead = b.str("Head_M");
  while (b.s.size() % 4) b.s.push_back(0);
  // Types are laid out first with forward references patched by fixed offsets: compute each block's size (44 per
  // member incl. the end marker).
  const uint32_t base = uint32_t(b.s.size());
  const uint32_t root_t = base, mesh_t = root_t + 3 * 44, vd_t = mesh_t + 5 * 44, vert_t = vd_t + 2 * 44, topo_t = vert_t + 3 * 44,
                 i16_t = topo_t + 2 * 44, bind_t = i16_t + 2 * 44, skel_t = bind_t + 2 * 44, bone_t = skel_t + 2 * 44,
                 objs = bone_t + 3 * 44;
  b.member(4, sMeshes, mesh_t, 0); b.member(4, sSkel, skel_t, 0); b.end();
  b.member(8, sName, -1, 0); b.member(2, sPvd, vd_t, 0); b.member(2, sTopo, topo_t, 0); b.member(3, sBind, bind_t, 0); b.end();
  b.member(7, sVerts, -1, 0); b.end();
  b.member(10, sPos, -1, 3); b.member(21, sUv, -1, 2); b.end();
  b.member(3, sI16, i16_t, 0); b.end();
  b.member(16, sInt, -1, 0); b.end();
  b.member(8, sBone, -1, 0); b.end();
  b.member(3, sBones, bone_t, 0); b.end();
  b.member(8, sName, -1, 0); b.member(10, sInv, -1, 16); b.end();
  CHECK(b.s.size() == objs);
  // Objects: root, mesh pointer array, mesh, vertex data, vertices, topology, indices, bindings, skeleton, bones.
  const uint32_t root = objs, mesh_arr = root + 24, mesh = mesh_arr + 8, vd = mesh + 36, verts = vd + 20, topo = verts + 3 * 16,
                 idx = topo + 12, binds = idx + 8, skel_arr = binds + 8, skel = skel_arr + 8, bones = skel + 12;
  put32(b.s, 1); b.ptr(mesh_arr); put32(b.s, 1); b.ptr(skel_arr);                 // root
  b.ptr(mesh);                                                                     // mesh_arr
  b.ptr(sMesh); b.ptr(vd); b.ptr(topo); put32(b.s, 1); b.ptr(binds);               // mesh
  b.ptr(vert_t); put32(b.s, 3); b.ptr(verts);                                      // vertex data
  const float pos[3][3] = {{0, 1.5f, 0.1f}, {0.02f, 1.5f, 0.1f}, {0, 1.4f, 0.1f}};
  const uint16_t uv[3][2] = {{0x0000, 0x0000}, {0x3800, 0x0000}, {0x0000, 0x3C00}};   // 0, 0.5, 1 as halves
  for (int v = 0; v < 3; ++v) { for (float f : pos[v]) putf(b.s, f); put16(b.s, uv[v][0]); put16(b.s, uv[v][1]); }
  put32(b.s, 3); b.ptr(idx);                                                       // topology
  put16(b.s, 2); put16(b.s, 0); put16(b.s, 1); put16(b.s, 0);                      // indices (+2 pad)
  b.ptr(sHead);                                                                    // bindings
  b.ptr(skel);                                                                     // skel_arr
  put32(b.s, 1); b.ptr(bones);                                                     // skeleton
  b.ptr(sHead);
  const float inv[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1.6f, 0, 1};
  for (float f : inv) putf(b.s, f);                                                // bone
  Bytes o;
  const uint8_t magic[16] = {0xE5, 0x2F, 0x4A, 0xE1, 0x6F, 0xC2, 0x8A, 0xEE, 0x1E, 0xD2, 0xB4, 0x4C, 0x90, 0xD7, 0x55, 0xAF};
  o.insert(o.end(), magic, magic + 16);
  put32(o, 104 + 44); put32(o, 0); put64(o, 0);
  const uint32_t data_at = 104 + 44, rel_at = data_at + uint32_t(b.s.size());
  put32(o, 7); put32(o, 0); put32(o, 0); put32(o, 72); put32(o, 1);
  put32(o, 0); put32(o, root_t); put32(o, 0); put32(o, root);
  for (int i = 0; i < 9; ++i) put32(o, 0);
  CHECK(o.size() == 104);
  const uint32_t sec[11] = {0, data_at, uint32_t(b.s.size()), uint32_t(b.s.size()), 4, 0, 0, rel_at, uint32_t(b.rel.size()),
                            rel_at + uint32_t(b.rel.size()) * 12, 0};
  for (uint32_t v : sec) put32(o, v);
  o.insert(o.end(), b.s.begin(), b.s.end());
  for (auto& [at, to] : b.rel) { put32(o, at); put32(o, 0); put32(o, to); }
  set32(o, 36, uint32_t(o.size()));
  return o;
}

int main() {
  const fs::path dir = fs::temp_directory_path() / ("hairconv_all_test_" + std::to_string(std::rand()));
  fs::remove_all(dir);
  fs::create_directories(dir / "Data");
  // ---- LZ4 round trips
  {
    Bytes in;
    for (int i = 0; i < 5000; ++i) in.push_back(uint8_t("hair strand atlas "[i % 18] + (i / 700)));
    for (int i = 0; i < 300; ++i) in.push_back('a');   // overlapping match (offset 1)
    Bytes out;
    CHECK(pak::lz4_block(lz4_compress(in).data(), lz4_compress(in).size(), out, in.size()) && out == in);
    const Bytes fr = lz4_frame(in);
    CHECK(pak::lz4_frame(fr.data(), fr.size(), out, in.size()) && out == in);
    CHECK(!pak::lz4_block(lz4_compress(in).data(), lz4_compress(in).size() - 3, out, in.size()));   // truncated
    std::string e;
    CHECK(!pak::decompress(pak::kZstd, in.data(), in.size(), in.size(), out, false, &e) && e.find("zstd") != std::string::npos);
  }
  // ---- synthetic packages
  const std::string gr2 = "Generated/Public/Test/Assets/Characters/Hair/HAIR_Test.GR2";
  const std::string dds = "Generated/Public/Test/Assets/Characters/Hair/HAIR_Test_MSKA.DDS";
  const std::vector<LNode> content = {
      {"VisualBank", -1, {}},
      {"Resource", 0, {{"ID", fs_attr("vis-1")}, {"SourceFile", fs_attr("Generated\\Public\\Test\\Assets\\Characters\\Hair\\HAIR_Test.GR2")}}},
      {"Objects", 1, {{"ObjectID", fs_attr("vis-1.HAIR_Test_Mesh.0")}, {"LOD", u8_attr(0)}, {"MaterialID", fs_attr("mat-1")}}},
      {"Objects", 1, {{"ObjectID", fs_attr("vis-1.HAIR_Test_Mesh_LOD2.0")}, {"LOD", u8_attr(2)}, {"MaterialID", fs_attr("mat-1")}}},
      {"MaterialBank", -1, {}},
      {"Resource", 4, {{"ID", fs_attr("mat-1")}, {"SourceFile", fs_attr("Public/Shared/Assets/Materials/Characters/CHAR_Hair.lsf")}}},
      {"Texture2DParameters", 5, {{"ParameterName", fs_attr("ID_Depth_Root_Alpha_MSKA")}, {"ID", fs_attr("tex-1")}}},
      {"TextureBank", -1, {}},
      {"Resource", 7, {{"ID", fs_attr("tex-1")}, {"SourceFile", fs_attr(dds)}}},
  };
  const std::vector<LNode> templates = {
      {"Templates", -1, {}},
      {"GameObjects", 0, {{"Name", fs_attr("S_Player_Test")}, {"CharacterVisualResourceID", fs_attr("cv-1")}}},
  };
  const std::vector<LNode> visuals = {
      {"CharacterVisualBank", -1, {}},
      {"Resource", 0, {{"ID", fs_attr("CV-1")}, {"Name", fs_attr("Test_CV")}}},
      {"Slots", 1, {{"Slot", fs_attr("Hair")}, {"VisualResource", fs_attr("VIS-1")}}},
  };
  const Bytes mask = dxt5_4x4(), gr2_bytes = synthetic_gr2();
  write_pak(dir / "Data" / "Shared.pak",
            {{"Public/Test/Content/Assets/Characters/[PAK]_Hair/_merged.lsf", lsf(content, true), 2},
             {"Public/Test/Content/[PAK]_CharacterVisuals/_merged.lsf", lsf(visuals, false), 0},
             {"Public/Test/RootTemplates/_merged.lsf", lsf(templates, false), 2},
             {gr2, gr2_bytes, 2},
             {dds, Bytes(64, 0), 0},   // replaced by the patch below
             {"Public/Test/zstd.bin", Bytes(16, 7), 3}},
            0);
  write_pak(dir / "Data" / "Patch1_Hotfix2.pak", {{dds, mask, 2}}, 0);
  write_pak(dir / "Data" / "Shared_1.pak", {}, 0);   // a numbered part file: never opened as a package
  pak::Set ps;
  std::string e;
  CHECK(ps.open((dir / "Data").wstring(), &e));
  CHECK(ps.packages().size() == 2 && ps.packages().back().file == "Patch1_Hotfix2.pak");   // patches load last
  Bytes got;
  CHECK(ps.read(dds, got, &e) && got == mask);                                           // the patch's copy wins
  CHECK(ps.read(gr2, got, &e) && got == gr2_bytes);
  CHECK(!ps.read("Public/Test/zstd.bin", got, &e) && e.find("zstd") != std::string::npos);
  CHECK(!ps.read("Public/Test/missing.lsf", got, &e));
  // ---- LSF: LZ4 frame sections and the plain layout
  pak::Resource r;
  CHECK(ps.read("Public/Test/Content/Assets/Characters/[PAK]_Hair/_merged.lsf", got, &e) && pak::parse_lsf(got.data(), got.size(), r, &e));
  CHECK(r.roots.size() == 3 && r.nodes[2].str("ObjectID") == "vis-1.HAIR_Test_Mesh.0" && r.nodes[3].get("LOD")->num == 2);
  // ---- catalogue: one LOD0 CHAR_Hair row (the LOD2 object is skipped), its mask, category from the templates
  std::vector<catalogue::Style> styles;
  CHECK(catalogue::styles(ps, styles, nullptr, &e));
  CHECK(styles.size() == 1 && styles[0].name == "HAIR_Test:HAIR_Test_Mesh" && styles[0].gr2 == gr2 && styles[0].mask == dds &&
        styles[0].lod == 0 && styles[0].visual == "vis-1");
  catalogue::classify(ps, styles, nullptr);
  CHECK(styles[0].category == "companion" && styles[0].tier == 1);   // S_Player_ template through its character visual
  const std::string cat = (dir / "cat.tsv").string();
  CHECK(catalogue::save(cat, ps.fingerprint(), styles));
  std::vector<catalogue::Style> again;
  CHECK(catalogue::load(cat, ps.fingerprint(), again) && again.size() == 1 && again[0].name == styles[0].name && again[0].tier == 1);
  CHECK(!catalogue::load(cat, ps.fingerprint() + "x", again));
  // ---- DXT5 decoded as Pillow does: 5-bit 4 -> 33 (bit replication, not 4 * 255 / 31 = 32), interpolation truncated
  Mask m;
  CHECK(decode_dds(mask.data(), mask.size(), m, &e) && m.w == 4 && m.h == 4);
  CHECK(m.at(0, 0, 0) == 22 && m.at(3, 3, 1) == 0 && m.at(1, 2, 3) == 185);
  // ---- GR2: header, relocations and the data-tree walk (uncompressed sections need no granny2.dll)
  granny::Library none;
  granny::File f;
  CHECK(none.read(gr2_bytes.data(), gr2_bytes.size(), f, &e));
  CHECK(f.meshes.size() == 1 && f.meshes[0].name == "HAIR_Test_Mesh" && f.meshes[0].indices == std::vector<uint32_t>({2, 0, 1}));
  CHECK(f.meshes[0].pos[3] == 0.02f && f.meshes[0].uv[2] == 0.5f && f.meshes[0].uv[5] == 1.0f && f.meshes[0].bindings.size() == 1);
  CHECK(f.bones.size() == 1 && f.bones[0].name == "Head_M" && f.bones[0].inverse_world[13] == -1.6f);
  Bytes bad = gr2_bytes;
  bad[104] = 4;   // a BitKnit section without granny2.dll: refused with the reason
  CHECK(!none.read(bad.data(), bad.size(), f, &e) && e.find("granny2.dll") != std::string::npos);
  bad = gr2_bytes;
  bad[0] ^= 1;
  CHECK(!none.read(bad.data(), bad.size(), f, &e));
  // ---- --all without granny2.dll: unavailable, exit 3, status names granny2.dll; a DLL that is not Granny likewise
  const std::string game = (dir / "Data").string(), out = (dir / "out").string(), rep = (dir / "rep").string();
  {
    const char* argv[] = {"--all", "--game", game.c_str(), "--out", out.c_str(), "--report-dir", rep.c_str()};
    CHECK(run_all(7, const_cast<char**>(argv)) == 3);
    const std::string s = read_text(fs::path(rep) / "preparation.json");
    CHECK(s.find("\"state\": \"unavailable\"") != std::string::npos && s.find("granny2.dll") != std::string::npos &&
          s.find("\"source\": \"game\"") != std::string::npos);
    CHECK(!fs::exists(out));   // nothing converted
  }
  {
    char sys[260] = {};
    GetSystemDirectoryA(sys, sizeof sys);
    const std::string not_granny = (fs::path(sys) / "version.dll").string();   // a real DLL without Granny's entry points
    const std::string missing = (dir / "nope" / "granny2.dll").string();
    for (const std::string& dll : {not_granny, missing}) {
      const char* argv[] = {"--all", "--game", game.c_str(), "--granny", dll.c_str(), "--out", out.c_str(), "--report-dir", rep.c_str()};
      CHECK(run_all(9, const_cast<char**>(argv)) == 3);
      const std::string s = read_text(fs::path(rep) / "preparation.json");
      CHECK(s.find("\"state\": \"unavailable\"") != std::string::npos && s.find("granny2.dll") != std::string::npos);
    }
  }
  fs::remove_all(dir);
  std::printf("hairconv_all_test: ok\n");
  return 0;
}
