#pragma once
// Per-draw material rows for INSTANCED draws of VT STI families (two, a third behind its own key) whose VS is not class A, so
// their meshes are never pooled and their BVH geometry is the per-frame prepass XFB (all instances,
// each in world space): rt_hit_material_instanced (startup: the G-buffer VS frame patch; then live).
//   Wind      ATM_Wind_Vertex_RG_VT       depth VS 1719e9d0 (+ alpha FS 4bed5db2), G-buffer 50fbfceb/8e49cde8
//   2S alpha  Base_AlphaTest_2s_VT        depth VS 106a0550 (+ a VT alpha FS),     G-buffer 1e9145e4/923131c7
//   Wind RGB  ATM_Wind_Vertex_RGB_AlphaTest_2S_Translucency_VT (rt_hit_material_wind_rgb, startup):
//             depth VS 633929ef (+ alpha FS 0d91554d),   G-buffer 735d5a19/83831811
// Both depth passes are alpha-tested (shaders/rt/alpha_fs.txt: 4bed5db2 `vt ... p0=0 idx=32`), so the
// prepass side is the existing alpha record (note_prepass) and every hit already passed its cut.
// The depth and G-buffer VSs compute the same clip position per instance (dumped GLSL: same input
// locations, same arithmetic), so the prepass and G-buffer XFB of one draw hold the same vertices per
// instance, instance-major, but not necessarily in the same instance order and only when both draws are
// of one game frame (pair_check, match_instance below); the consumer's per-vertex clip + UV check still
// decides each hit (fail closed): bit-exact, except the wind rows' clip positions within
// wind_clip_tolerance below. Pure policy on plain numbers; tests/native_hit_instanced_test.cpp pins it.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "native_hit_generated.h"
#include "native_hit_join.h"
#include "native_hit_qualify.h"
#include "native_vt_surface.h"
namespace bg3::native_hit::instanced {
// WindRgb (3): ATM_Wind_Vertex_RGB_AlphaTest_2S_Translucency_VT. Asset DB: its StaticInstancedDepth variant is
// 633929ef/0d91554d, its StaticInstancedDeferred variant 735d5a19/83831811. The dumped VSs have the same interface
// as the wind pair (inputs 0..6, outputs UV/N/B/T at 0..3; depth: UV, W) and textually the same position
// arithmetic (the RG wind of 1719e9d0/50fbfceb plus a vertical flutter term in 1:1 _m3/_m4 and vertex colour b,
// all inside the precise _26 = _25 + offset * fade). No clip tolerance is derived for the flutter term: the rows
// are checked bit for bit (the wind rows were 100% bit-exact live, m4i 2026-11-01), fail closed. Off unless
// rt_hit_material_wind_rgb (startup; gate() below), so the two other families are unchanged with it off.
// IpGrass (4): IP_Wind_GrassFlowers_AlphaTest_2S_VT / _PBR_VT (rt_hit_material_ip_grass, startup). Asset DB: depth
// 05d39f75/73849471, G-buffer 105e47e4/35a18400. Both VSs place the vertex with the same wind arithmetic (dumped GLSL: frame
// 0:0 _m0, scene 0:1 _m1/_m3/_m5, camera 0:2 _m2/_m6, the 1:0 translation _m1 @32 as 2S, 1:1 _m1/_m2, the 1:3 noise texture);
// its rows are checked bit for bit (no tolerance). Word 220 bits 8..10 hold the family: a consumer without RT_HIT_IP_GRASS
// reads bits 8..9 = 0 and checks the row bit for bit with the plain per-draw rule (fail closed, uncounted).
enum Family : uint32_t { None = 0, Wind = 1, AlphaTest2s = 2, WindRgb = 3, IpGrass = 4 };
inline constexpr uint32_t kFamilies = 5;   // per-family counter arrays (index 0 unused); word 220 bits 8..10 hold 0..4
struct Pair { uint64_t depth_vs, vs, fs; Family family; };
inline constexpr Pair kPairs[] = {
    {0x1719e9d0d14390f5ull, 0x50fbfceb06192473ull, 0x8e49cde81b680054ull, Wind},
    {0x106a055077bc19d6ull, 0x1e9145e4b621a5e0ull, 0x923131c7039f33daull, AlphaTest2s},
    {0x633929ef4f08b8aeull, 0x735d5a19130262bdull, 0x838318119b2f43d8ull, WindRgb},
    {0x05d39f7537219e12ull, 0x105e47e448736d89ull, 0x35a18400f9654d9dull, IpGrass}};
inline constexpr const char* kFamilyName[] = {"none", "wind", "alpha-test 2S", "wind RGB 2S", "IP grass"};
// A family the startup keys leave off is None: WindRgb needs rt_hit_material_wind_rgb, IpGrass rt_hit_material_ip_grass.
inline Family gate(Family f, bool wind_rgb, bool ip_grass = false) {
  return (f == WindRgb && !wind_rgb) || (f == IpGrass && !ip_grass) ? None : f;
}
// Generated rows (rt_hit_material_generated, common/native_hit_generated.h): instanced pairs proven offline to compute the
// same positions as an audited bit-exact family (same depth VS + the same G-buffer gl_Position statements, or depth and
// G-buffer VS equal to each other with the family's bindings), so they take that family's pairing certificate, key and
// remap; their rows are checked bit for bit at the hit like the family's. Only with `generated` (the key): with it off
// every lookup below is exactly the audited one. They are not gated by the wind RGB / IP grass keys: those gate the
// audited pairs of their families, while a generated row only borrows the family's certificate.
inline const generated::Row* generated_draw(uint64_t vs, uint64_t fs) {
  const auto* g = generated::row(vs, fs);
  return g && g->recipe.capture == native_vt::CaptureDraw && g->family > uint32_t(None) && g->family < kFamilies ? g : nullptr;
}
inline const generated::Row* generated_depth(uint64_t depth_vs) {
  for (uint32_t i = 0; i < generated::kRowCount; ++i) {
    const auto& g = generated::kRows[i];
    if (g.recipe.capture == native_vt::CaptureDraw && g.depth_vs == depth_vs && g.family > uint32_t(None) && g.family < kFamilies) return &g;
  }
  return nullptr;
}
inline const generated::Row* generated_vs(uint64_t vs) {
  for (uint32_t i = 0; i < generated::kRowCount; ++i)
    if (generated::kRows[i].recipe.capture == native_vt::CaptureDraw && generated::kRows[i].recipe.vs == vs) return &generated::kRows[i];
  return nullptr;
}
// The family of a depth-prepass draw (by its VS: the depth FS is any alpha-table VT FS).
inline Family depth_family(uint64_t vs, bool wind_rgb = false, bool ip_grass = false, bool generated = false) {
  for (const auto& p : kPairs) if (p.depth_vs == vs) {
    const Family f = gate(p.family, wind_rgb, ip_grass);
    // An audited depth VS whose family is off still pairs with that family's generated G-buffer pairs.
    if (f != None || !generated || !generated_depth(vs)) return f;
    return p.family;
  }
  if (const auto* g = generated ? generated_depth(vs) : nullptr) return Family(g->family);
  return None;
}
// The family of a G-buffer draw (the exact audited pair).
inline Family deferred_family(uint64_t vs, uint64_t fs, bool wind_rgb = false, bool ip_grass = false, bool generated = false) {
  for (const auto& p : kPairs) if (p.vs == vs && p.fs == fs) return gate(p.family, wind_rgb, ip_grass);
  if (const auto* g = generated ? generated_draw(vs, fs) : nullptr) return Family(g->family);
  return None;
}
// The G-buffer VSs whose UV/N/B/T outputs get the XFB frame patch (locations 0/1/2/3 as 2d2ac3c3; IpGrass 1/2/3/4:
// frame_location below). generated: the startup value of rt_hit_material_generated (the patch is made at pipeline creation).
inline bool frame_vs(uint64_t vs, bool wind_rgb = false, bool ip_grass = false, bool generated = false) {
  for (const auto& p : kPairs) if (p.vs == vs && gate(p.family, wind_rgb, ip_grass) != None) return true;
  return generated && generated_vs(vs);
}
// The N location of an instanced G-buffer VS (spirv_xfb.h native_vt_location; UV one below; the frame lands at 24/36/48).
// A generated VS: the location the generator read from its interface (0de1b186: 2, as 105e47e4).
inline uint32_t frame_location(uint64_t vs, bool generated = false) {
  if (vs == 0x105e47e448736d89ull) return 2u;
  const auto* g = generated ? generated_vs(vs) : nullptr;
  return g ? g->frame_location : 1u;
}
// The recipe of an instanced pair (native_vt_surface.h CaptureDraw rows; generated: common/native_hit_generated.h).
inline const native_vt::Recipe* recipe(uint64_t vs, uint64_t fs, bool wind_rgb = false, bool ip_grass = false, bool generated = false) {
  const auto* r = native_vt::recipe(fs);
  if (deferred_family(vs, fs, wind_rgb, ip_grass) != None && r && r->vs == vs && r->capture == native_vt::CaptureDraw) return r;
  const auto* g = generated && !r ? generated_draw(vs, fs) : nullptr;
  return g ? &g->recipe : nullptr;
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
// IP_Base_VT shares IP_AlphaTest's deferred pair but has no alpha record. This
// lookup is a recording-time hint, not a certificate: depth can record later.
// The caller supplies only current-frame sources; the final pairing certificate
// and GPU clip/UV checks remain mandatory. Other audited families are unchanged.
inline bool capture_source_ok(Family family, bool generated, const DrawJoin* source, const DrawKey& key) {
  if (!generated && family != IpGrass) return true;
  uint32_t record = UINT32_MAX;
  return source && source->unique_source(key, record);
}
// Generated 2S G-buffer and depth packets record in parallel. Capture speculatively
// when depth has not arrived; join_packets still rejects missing/duplicate sources.
inline bool capture_without_source_ok(Family family, bool generated) {
  return generated && family == AlphaTest2s;
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
  // ReflectanceConstant of a per-draw row (generated rows only, e.g. e4ade329 = 3da860be: RT2.y is 0.5 and there is no
  // 1:1 UBO): the gather's 1:1 float is staged as 0.5. No audited CaptureDraw recipe is constant and pool recipes are
  // never staged, so every audited staging is unchanged (a constant pool recipe is still refused).
  const bool constant = r.reflectance == native_vt::ReflectanceConstant && r.capture == native_vt::CaptureDraw;
  if ((r.reflectance != native_vt::ReflectanceUbo && !constant) || (r.layout != native_vt::LayoutBase && r.layout != native_vt::LayoutVt4) ||
      t + 8u > engine_words || index >= engine_words || channel >= engine_words || (!constant && refl >= material_words))
    return false;
  std::memcpy(out + 64, engine + t, 32);
  std::memcpy(out + 100, engine + index, 4);
  std::memcpy(out + 104, engine + channel, 4);
  const float half = 0.5f;
  std::memcpy(out + kStagePhysical, constant ? static_cast<const void*>(&half) : static_cast<const void*>(material + refl), 4);
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
// The VS inputs of one instanced draw, read from the game's mapped UBOs at record (both families: the
// pairing certificate below; wind: also the tolerance's inputs; 2S: also the per-draw translation).
struct WindInputs {
  bool ok = false;
  float time = 0;              // 0:0 word 0
  float scene[3]{};            // 0:1 _m1, _m3, _m5 (bytes 12, 28, 44); wind only
  float material[4]{};         // 1:1 _m0.._m3; wind only
  float flutter = 0;           // 1:1 _m4 (byte 16): the wind RGB VSs' flutter amplitude; WindRgb only
  float camera[3]{};           // 0:2 _m6 (byte 208)
  float viewproj[16]{};        // 0:2 _m2 (bytes 128..191, row-major as GLSL reads it: clip_c = sum_k W_k m[k][c] + m[3][c])
  float translation[3]{};      // 1:0 _m1 (bytes 32..43): added to the instance rows by 106a0550 / 1e9145e4; 2S only
};
inline constexpr uint32_t kSceneWindOffsets[3] = {12, 28, 44}, kCameraViewProjOffset = 128, kCameraPositionOffset = 208,
                          kTranslationOffset = 32;
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

// ---- Why the clip check rejected (live 2026-11-01, m4i-instanced-s14-fg2.1.093913) ----
// Rejects were not rounding: wind "within the derived tolerance 0, over x256", 2S "differs", UV differs 0,
// rigid 0, in bursts at a static camera (2.6%, 3.1%, 0%, then 0.4% of all lookups per window) and at the
// same per-window share for both families (24/27%, 10/16%, 2.2/2.4% of their checked hits), while the
// static per-draw rows (instances 1) had 0 clip rejects in the same frames. So whole frames held rows
// whose depth and G-buffer XFBs place the same mesh (UV bit-exact) with other instance positions. Two
// causes in the pairing, both fixed here; the per-hit clip + UV check stays bit-exact (fail closed).
//  1. The pair is from two game frames. The instanced key (draw words + index range) is the same every
//     frame, and the packet <-> source epoch is frame_index(), which counts presents: it moves in the
//     middle of a game frame's recording (the same run: "[hit material query reject] source epoch
//     current 4320 source 4319"), so a G-buffer packet of the previous game frame can carry the source's
//     index when the current one is recorded after the query. Certificate: both draws' CPU-read VS
//     inputs (pair_check): the frame time (0:0 word 0, one value per game frame), the camera _m2/_m6
//     and, per family, the 2S translation or the wind UBO words. Any difference: the row is not mapped.
//  2. The two passes hold the same instances in another order (per-pass instance uploads). The rows are
//     instance-major, so every reordered instance fails at the hit. rt/hit_material_instanced_remap.hlsl
//     maps each depth instance to the G-buffer instance with the same first triangle (match_instance)
//     and copies that block into the row; an instance without a match keeps its own block (the hit
//     check rejects it). match_instance / remap_instances are the CPU reference of that shader.
enum PairOutcome : uint32_t { PairSame = 0, PairUnread = 1, PairOtherFrame = 2, PairCamera = 3, PairPlacement = 4, PairWind = 5, kPairOutcomes = 6 };
inline constexpr const char* kPairName[] = {"same frame", "unread", "other frame (time)", "camera", "placement", "wind inputs"};
inline PairOutcome pair_check(Family family, const WindInputs& pre, const WindInputs& def) {
  if (family == None || !pre.ok || !def.ok) return PairUnread;
  if (std::memcmp(&pre.time, &def.time, 4)) return PairOtherFrame;
  if (std::memcmp(pre.viewproj, def.viewproj, sizeof pre.viewproj) || std::memcmp(pre.camera, def.camera, sizeof pre.camera)) return PairCamera;
  if ((family == AlphaTest2s || family == IpGrass) && std::memcmp(pre.translation, def.translation, sizeof pre.translation))
    return PairPlacement;
  if ((family == Wind || family == WindRgb || family == IpGrass) &&
      (std::memcmp(pre.scene, def.scene, sizeof pre.scene) || std::memcmp(pre.material, def.material, sizeof pre.material)))
    return PairWind;
  if (family == WindRgb && std::memcmp(&pre.flutter, &def.flutter, 4)) return PairWind;
  return PairSame;
}
// Instance blocks of a row: triangles x 3 vertices of `stride` bytes, instance-major; each block holds
// count (= 3 triangles / instances) vertices. The match key is the first triangle's clip (16 B) and UV
// (8 B) per vertex: an affine placement is fixed by three vertices, and the hit check certifies the rest.
inline constexpr uint32_t kMatchVertexBytes = 24, kMatchNone = 0xffffffffu;
inline bool remap_ok(uint32_t instances, uint32_t triangles, uint32_t stride, uint32_t& block) {
  block = 0;
  if (instances < 2 || stride < kMatchVertexBytes || stride % 4 || (uint64_t(triangles) * 3) % instances) return false;
  const uint64_t vertices = uint64_t(triangles) * 3 / instances, bytes = vertices * stride * instances;
  if (vertices < 3 || bytes > UINT32_MAX) return false;
  block = uint32_t(vertices * stride);
  return true;
}
// The G-buffer instance for depth instance i: the first j from i on (wrapping; the same order first).
inline uint32_t match_instance(const unsigned char* pre, const unsigned char* def, uint32_t instances, uint32_t block,
                               uint32_t stride, uint32_t i) {
  const unsigned char* a = pre + uint64_t(i) * block;
  for (uint32_t k = 0, j = i; k < instances; ++k, j = j + 1 == instances ? 0 : j + 1) {
    const unsigned char* b = def + uint64_t(j) * block;
    bool same = true;
    for (uint32_t v = 0; same && v < 3; ++v) same = std::memcmp(a + v * stride, b + v * stride, kMatchVertexBytes) == 0;
    if (same) return j;
  }
  return kMatchNone;
}
enum Order : uint32_t { OrderRows = 0, OrderSame = 1, OrderPermuted = 2, OrderUnmatched = 3, kOrderCounters = 4 };
// out = def with each depth instance's matched G-buffer block (its own block when unmatched); counts as
// the shader's per-family counters (rows, instances in the same order, permuted, unmatched).
inline void remap_instances(const unsigned char* pre, const unsigned char* def, unsigned char* out, uint32_t instances,
                            uint32_t block, uint32_t stride, uint64_t (&counts)[kOrderCounters]) {
  ++counts[OrderRows];
  for (uint32_t i = 0; i < instances; ++i) {
    const uint32_t j = match_instance(pre, def, instances, block, stride, i);
    ++counts[j == i ? OrderSame : j == kMatchNone ? OrderUnmatched : OrderPermuted];
    std::memcpy(out + uint64_t(i) * block, def + uint64_t(j == kMatchNone ? i : j) * block, block);
  }
}
// The remap buffer (host-visible, per query): 4 counters per family at 16 x family bytes, then the
// permutation words of each remapped row.
inline constexpr uint32_t kRemapPermBase = 16 * kFamilies;   // 80 with the fifth counter block (IpGrass)
static_assert(kRemapPermBase >= 16 * kFamilies, "remap counters (16 B per family) before the permutations");
// The layer's "[hit material instanced]" lines (rt_stats): the first query that reaches the log in each 120-frame
// window prints them, not only a query on frame % 120 == 0: that one query is often rejected (source epoch one frame
// behind) and the window then had no line (hig1 camp 2026-11-02: frames 1800 and 1920 rejected, 1 line in 3 windows).
inline constexpr uint64_t kStatsWindowFrames = 120;
inline bool stats_window_due(uint64_t frame, uint64_t& logged_window, uint64_t period = kStatsWindowFrames) {
  const uint64_t w = frame / (period ? period : kStatsWindowFrames);
  if (logged_window != UINT64_MAX && w <= logged_window) return false;
  logged_window = w;
  return true;
}
}  // namespace bg3::native_hit::instanced
