#pragma once
// Per-draw material rows for INSTANCED draws of two VT STI families whose VS is not class A, so
// their meshes are never pooled and their BVH geometry is the per-frame prepass XFB (all instances,
// each in world space): rt_hit_material_instanced (startup: the G-buffer VS frame patch; then live).
//   Wind      ATM_Wind_Vertex_RG_VT       depth VS 1719e9d0 (+ alpha FS 4bed5db2), G-buffer 50fbfceb/8e49cde8
//   2S alpha  Base_AlphaTest_2s_VT        depth VS 106a0550 (+ a VT alpha FS),     G-buffer 1e9145e4/923131c7
// Both depth passes are alpha-tested (shaders/rt/alpha_fs.txt: 4bed5db2 `vt ... p0=0 idx=32`), so the
// prepass side is the existing alpha record (note_prepass) and every hit already passed its cut.
// The depth and G-buffer VSs compute the same clip position per instance (dumped GLSL: same input
// locations, same arithmetic), so the prepass and G-buffer XFB of one draw hold the same vertices in
// the same instance-major order; the consumer's per-vertex clip + UV equality still decides each hit
// (fail closed). Pure policy on plain numbers; tests/native_hit_instanced_test.cpp pins it.
#include <cstdint>
#include <cstring>
#include "native_hit_join.h"
#include "native_hit_qualify.h"
#include "native_vt_surface.h"
namespace bg3::native_hit::instanced {
enum Family : uint32_t { None = 0, Wind = 1, AlphaTest2s = 2 };
struct Pair { uint64_t depth_vs, vs, fs; Family family; };
inline constexpr Pair kPairs[] = {
    {0x1719e9d0d14390f5ull, 0x50fbfceb06192473ull, 0x8e49cde81b680054ull, Wind},
    {0x106a055077bc19d6ull, 0x1e9145e4b621a5e0ull, 0x923131c7039f33daull, AlphaTest2s}};
inline constexpr const char* kFamilyName[] = {"none", "wind", "alpha-test 2S"};
// The family of a depth-prepass draw (by its VS: the depth FS is any alpha-table VT FS).
inline Family depth_family(uint64_t vs) {
  for (const auto& p : kPairs) if (p.depth_vs == vs) return p.family;
  return None;
}
// The family of a G-buffer draw (the exact audited pair).
inline Family deferred_family(uint64_t vs, uint64_t fs) {
  for (const auto& p : kPairs) if (p.vs == vs && p.fs == fs) return p.family;
  return None;
}
// The G-buffer VSs whose UV/N/B/T outputs (locations 0/1/2/3, as 2d2ac3c3) get the XFB frame patch.
inline bool frame_vs(uint64_t vs) {
  for (const auto& p : kPairs) if (p.vs == vs) return true;
  return false;
}
// The recipe of an instanced pair (native_vt_surface.h CaptureDraw rows).
inline const native_vt::Recipe* recipe(uint64_t vs, uint64_t fs) {
  const auto* r = native_vt::recipe(fs);
  return deferred_family(vs, fs) != None && r && r->vs == vs && r->capture == native_vt::CaptureDraw ? r : nullptr;
}
// Draw key shared by the depth and the G-buffer draw: the draw words (kind, count, instances, first,
// vertex offset) and the index binding of the ordinary key, the family; no vertex-buffer words (the two
// pipelines need not bind the same stream first) and no first instance (the instance stream is a
// per-pass upload). Two draws with one key in a pass are both declined (DrawJoin uniqueness), and a
// wrong pair still fails the per-vertex clip check at the hit.
inline constexpr uint64_t kKeyMagic = 0x4e484b32u;  // "NHK2"
inline DrawKey key(const DrawKey& ordinary, Family family) {
  DrawKey k{};
  for (size_t i = 0; i < 9; ++i) k.words[i] = ordinary.words[i];
  k.words[5] = 0;
  if (k.words[0] == 0) k.words[6] = k.words[7] = k.words[8] = 0;  // non-indexed: no index binding
  k.words[20] = kKeyMagic;
  k.words[21] = family;
  return k;
}
// Prepass alpha draw: all instances captured (vertices = count x instances = 3 x triangles).
inline bool prepass_geometry_ok(uint32_t vertices, uint32_t instances, uint32_t triangles, uint32_t stride) {
  return instances >= 1 && vertices == uint64_t(triangles) * 3 && stride >= kStaticMinStride;
}
// G-buffer draw: as deferred_qualify for an audited pair, any instance count.
inline DeferredQualify deferred_qualify(bool exact_pipeline, uint32_t kind, uint32_t instances, uint32_t stride) {
  if (!exact_pipeline || kind > 1 || !instances || stride < kStaticMinStride) return DeferredQualify::PipelineDeclined;
  return DeferredQualify::Accept;
}
// The gather (shaders/rt/hit_material_gather.hlsl) reads the 8995 engine layout: v[4] p0 @64, v[5] p1
// @80, words.y tile-set index @100, words.z channel @104, and the 1:1 x @0 of a separate binding. The
// STI engine bytes (p0 @transform, p1 @+16, index and channel at the recipe's offsets) and the 1:1
// reflectance are staged into that layout per material (512 B: engine at 0, 1:1 at 256), as the pool
// rows do. p0/p1 and the tile-set matrices are then checked against the depth draw's alpha record by
// the join (snapshot rows 48..207); the channel and the reflectance are the record-time bytes.
inline constexpr uint32_t kStageBytes = 512, kStagePhysical = 256;
inline bool stage(const native_vt::Recipe& r, const uint32_t* engine, uint32_t engine_words, const uint32_t* material,
                  uint32_t material_words, unsigned char (&out)[kStageBytes]) {
  std::memset(out, 0, sizeof out);
  const uint32_t t = r.transform_offset / 4u, index = r.tileset_index_offset / 4u, channel = r.channel_offset / 4u,
                 refl = r.reflectance_offset / 4u;
  if (r.reflectance != native_vt::ReflectanceUbo || r.layout != native_vt::LayoutBase || t + 8u > engine_words ||
      index >= engine_words || channel >= engine_words || refl >= material_words)
    return false;
  std::memcpy(out + 64, engine + t, 32);
  std::memcpy(out + 100, engine + index, 4);
  std::memcpy(out + 104, engine + channel, 4);
  std::memcpy(out + kStagePhysical, material + refl, 4);
  return true;
}
// Row word 220 of a per-draw row: flags, bit 0 = two-sided (the consumer scales the N column by
// nv_two_sided_sign at the hit). 0 on every other per-draw row.
inline constexpr uint32_t kRowFlagsWord = 220, kRowTwoSided = 1;
inline uint32_t row_flags(const native_vt::Recipe& r) { return r.two_sided ? kRowTwoSided : 0u; }
}  // namespace bg3::native_hit::instanced
