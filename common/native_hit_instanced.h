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
// the same instance-major order; the consumer's per-vertex clip + UV check still decides each hit
// (fail closed): bit-exact, except the wind rows' clip positions within wind_clip_tolerance below. Pure policy on plain numbers; tests/native_hit_instanced_test.cpp pins it.
#include <algorithm>
#include <cmath>
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
// Word 220 bits 8..9: the family (RT_HIT_INSTANCED consumers count the clip check per family, the
// "hit material instanced clip" line); bit 1: the clip positions are compared with the row's derived
// tolerance (words 224..255, wind_clip_tolerance) instead of bit for bit. The UV stays bit-exact.
inline constexpr uint32_t kRowTolerant = 2, kRowFamilyShift = 8, kRowToleranceWord = 224;
inline uint32_t family_flags(Family f) { return uint32_t(f) << kRowFamilyShift; }

// ---- Wind clip tolerance (the reason `clip` of the wind rows) ----
// Both wind VSs (1719e9d0 depth, 50fbfceb G-buffer; dumped GLSL) compute
//   _22 = precise(P * instance rows)                          (bit-identical in both)
//   o   = (normalize(Wvec + d) * |d| - d) * fade              (NOT precise: contraction may differ)
//   _23 = precise(_22 + o),  clip = precise(vec4(_23, 1) * cam._m2)
// with d = (_22 + cam) - (pivot + cam) (adds only: identical), the phase
//   phi_k = c_k * F + 0.5, F = 0.5 (h_x + h_y) + t * m1 * (s5 * 0.1) + 0.2 * m2 * col.y * col.w,
//   h = |2 fract(0.1 * abs_xz + 0.5) - 1|, abs_xz the pivot's ABSOLUTE world xz (pivot + cam),
// g = m5 (0.25 S (1 - 0.09 m5) + 0.09 m5) with S = (s0 + s1)(s2 + s3) of the smoothsteps of
// |2 fract(phi_k) - 1|, Wvec = (s1, 0, s3) g m3 (m2 col.y + col.x). Inputs: t = frame UBO 0:0 word 0,
// s1/s3/s5 = scene UBO 0:1 bytes 12/28/44, m0..m3 = material UBO 1:1, cam = camera 0:2 _m6 (byte 208),
// cam._m2 = camera bytes 128..191. With the same inputs (checked bitwise on the CPU, both draws) the two
// compilations differ only where a mul + add is fused in one and not the other. Each such site changes
// one rounding (<= u (|product| + |result|), u = 2^-24); propagated with first-order Lipschitz bounds:
//   P1 0.1 abs_xz + 0.5 (|abs| <= R = |cam|inf + |W|inf): u (0.2 R + 0.5) per h input, x2 slope, F gets 2x.
//   P2 c_k F + 0.5: u (2 c_k Fmax + 0.5), Fmax = 1 + |t| m1 |s5| 0.1 + 0.2 m2 (vertex colours in [0, 1]).
//   fract*2-1, 3-2x and the dot()s with 1 are exact (powers of two); smoothstep slope <= 1.5, so
//   ds_k <= 3 dphi_k; dS/4 <= 0.5 sum ds_k; dg = m5 |1 - 0.09 m5| dS/4; dWvec = amp dg, amp = max(|s1|,|s3|) m3 (m2 + 1).
//   P7 Wvec + d: u (2 Wmax + L); normalize/length/dot sites and P8 n L - d: 5 u L; |d(nL)| <= 2 |dY|_2
//   (|Y| >= L / 2 while |Wvec| <= L / 2 or |Wvec| >= 2 L; the near-cancelling band in between is the bound's
//   one exception). fade: dfade <= 12 u |m0| (length site + smoothstep's (x - 30) / 15), only while the
//   pivot is 30..45 m away (L <= sqrt3 |W|inf + 45 there); q * fade rounding 4 u fmax L.
//   L = |d| <= 2 sqrt3 R (first order: pivot and vertex share the absolute magnitude): the loosest term.
//   Then _23 adds 2 u |W|inf (two final roundings), clip adds 2 gamma4 (s_c |W|inf + t_c) for the precise
//   4-term dot (s_c = sum_k |m2[k][c]|, t_c = |m2[3][c]|), and d_clip_c <= s_c d_23 + that.
// If only t differs between the passes (the time-dependent term evaluated in each pass), F also differs
// by |dt| m1 |s5| 0.1 and the same chain carries it. Any other input difference: no tolerance (exact,
// fail closed). Result: tau_c = a_c + b_c |W|inf per vertex (|W| the BVH vertex), rounded up to float.
struct WindInputs {
  bool ok = false;
  float time = 0;              // 0:0 word 0
  float scene[3]{};            // 0:1 _m1, _m3, _m5 (bytes 12, 28, 44)
  float material[4]{};         // 1:1 _m0.._m3
  float camera[3]{};           // 0:2 _m6 (byte 208)
  float viewproj[16]{};        // 0:2 _m2 (bytes 128..191, row-major as GLSL reads it: clip_c = sum_k W_k m[k][c] + m[3][c])
};
inline constexpr uint32_t kSceneWindOffsets[3] = {12, 28, 44}, kCameraViewProjOffset = 128, kCameraPositionOffset = 208;
enum WindMatch : uint32_t { MatchUnread = 0, MatchEqual = 1, MatchTimeOnly = 2, MatchDiffer = 3 };
inline WindMatch wind_match(const WindInputs& a, const WindInputs& b) {
  if (!a.ok || !b.ok) return MatchUnread;
  if (std::memcmp(a.scene, b.scene, sizeof a.scene) || std::memcmp(a.material, b.material, sizeof a.material) ||
      std::memcmp(a.camera, b.camera, sizeof a.camera) || std::memcmp(a.viewproj, b.viewproj, sizeof a.viewproj))
    return MatchDiffer;
  return std::memcmp(&a.time, &b.time, 4) ? MatchTimeOnly : MatchEqual;
}
struct ClipTolerance { float a[4]{}, b[4]{}; };  // tau_c = a_c + b_c |W|inf (row words 224..239, 240..255)
// A float >= v (v finite, >= 0).
inline float round_up(double v) {
  float f = float(v);
  return double(f) < v ? std::nextafter(f, INFINITY) : f;
}
inline bool wind_clip_tolerance(const WindInputs& pre, const WindInputs& def, ClipTolerance& out) {
  out = {};
  const WindMatch match = wind_match(pre, def);
  if (match != MatchEqual && match != MatchTimeOnly) return false;
  struct Aff { double c, w; };  // c + w |W|inf
  auto add = [](Aff x, Aff y) { return Aff{x.c + y.c, x.w + y.w}; };
  auto mul = [](double k, Aff x) { return Aff{k * x.c, k * x.w}; };
  const double u = 0x1p-24, r3 = std::sqrt(3.0);
  const double cam = std::max({std::fabs(double(def.camera[0])), std::fabs(double(def.camera[1])), std::fabs(double(def.camera[2]))});
  const double m0 = std::fabs(double(def.material[0])), m1 = std::fabs(double(def.material[1])), m2 = std::fabs(double(def.material[2])),
               m3 = std::fabs(double(def.material[3]));
  const double s1 = std::fabs(double(def.scene[0])), s3 = std::fabs(double(def.scene[1])), s5 = std::fabs(double(def.scene[2]));
  const double kt = m1 * s5 * 0.1, tmax = std::max(std::fabs(double(pre.time)), std::fabs(double(def.time)));
  const double fmax_phase = 1.0 + tmax * kt + 0.2 * m2, dft = std::fabs(double(pre.time) - double(def.time)) * kt;
  const Aff R{cam, 1.0};
  const Aff d1 = add(mul(0.2 * u, R), Aff{0.5 * u, 0});              // P1, per h input
  const Aff dF = add(mul(2.0, d1), Aff{dft, 0});                     // 0.5 (2 d1 + 2 d1) + the time term
  const double c[4] = {0.17000000178813934, 0.37000000476837158, 1.9750000238418579, 0.79299998283386230};
  Aff ds{0, 0};
  for (double ck : c) ds = add(ds, mul(3.0, add(mul(ck, dF), Aff{u * (2.0 * ck * fmax_phase + 0.5), 0})));
  const Aff dS = mul(0.5, ds);
  const double gk = s5 * std::fabs(1.0 - 0.09 * s5), gmax = s5 * (std::fabs(1.0 - 0.09 * s5) + 0.09 * s5);
  const double amp = std::max(s1, s3) * m3 * (m2 + 1.0), wmax = amp * gmax;
  const Aff L = mul(2.0 * r3, R);
  const Aff dY = add(add(mul(amp * gk, dS), Aff{2.0 * u * wmax, 0}), mul(u, L));
  const Aff dq = add(mul(2.0 * r3, dY), mul(5.0 * u, L));
  const double fmax = std::max(1.0, std::fabs(1.0 - double(def.material[0]))), dfade = 12.0 * u * m0;
  const Aff Lf{45.0, r3};
  const Aff dO = add(add(mul(fmax, dq), mul(2.0 * dfade, Lf)), mul(4.0 * u * fmax, L));
  const Aff e23 = add(dO, Aff{0, 2.0 * u});
  const double g4 = 4.0 * u / (1.0 - 4.0 * u), margin = 1.0 + 8.0 * u;  // the shader's fp32 a + b w and |c - c'|
  for (uint32_t k = 0; k < 4; ++k) {
    double s = 0;
    for (uint32_t r = 0; r < 3; ++r) s += std::fabs(double(def.viewproj[4 * r + k]));
    const double t = std::fabs(double(def.viewproj[12 + k]));
    const double a = (s * e23.c + 2.0 * g4 * t) * margin, b = (s * e23.w + 2.0 * g4 * s) * margin;
    if (!std::isfinite(a) || !std::isfinite(b)) return false;
    out.a[k] = round_up(a);
    out.b[k] = round_up(b);
  }
  return true;
}
}  // namespace bg3::native_hit::instanced
