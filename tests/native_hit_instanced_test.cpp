// Pins the per-draw rows of instanced wind / two-sided alpha-test draws (common/native_hit_instanced.h,
// rt_hit_material_instanced): families, the shared draw key, geometry/qualify rules, the engine staging
// into the gather layout and the two-sided row flag.
#include "../common/native_hit_instanced.h"
#include "../common/native_hit_pool.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace bg3::native_hit;
namespace vt = bg3::native_vt;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static constexpr uint64_t kWindDepth = 0x1719e9d0d14390f5ull, kWindVs = 0x50fbfceb06192473ull, kWindFs = 0x8e49cde81b680054ull;
static constexpr uint64_t k2sDepth = 0x106a055077bc19d6ull, k2sVs = 0x1e9145e4b621a5e0ull, k2sFs = 0x923131c7039f33daull;
static constexpr uint64_t kStiVs = 0x2d2ac3c38992b7f0ull, kStiFs = 0x14678345c10aefbcull;
static DrawKey ordinary(uint32_t kind, uint32_t count, uint32_t instances, uint32_t first, int32_t vertex, uint32_t first_instance,
                        uint64_t ib, uint64_t ib_offset, uint64_t vb0) {
  DrawKey k;
  const uint64_t w[] = {kind, count, instances, first, uint64_t(uint32_t(vertex)), first_instance, ib, ib_offset, 1, vb0, 64, 48};
  for (size_t i = 0; i < sizeof w / sizeof w[0]; ++i) k.words[i] = w[i];
  return k;
}
// ---- Wind clip tolerance (native_hit_instanced.h wind_clip_tolerance) ----
// The wind VS of 1719e9d0 / 50fbfceb (dumped GLSL) in fp32, with every mul + add the driver may fuse
// fused or not per site (mask bit): two "compilations" of the same source on the same inputs.
enum Site : uint32_t { SP1 = 1, SP2 = 2, SY = 4, SLen = 8, SNorm = 16, SQ = 32, SFade = 64, SFadeLen = 128 };
struct WindVertex { float p[3], rows[3][4], col[4]; };
static float fr(float x) { return x - std::floor(x); }
static float dot3(const float* a, const float* b, bool fused) {
  return fused ? std::fma(a[2], b[2], std::fma(a[1], b[1], a[0] * b[0])) : (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
}
static void wind_vs(const instanced::WindInputs& in, const WindVertex& v, uint32_t mask, float w23[3], float clip[4], float* y_over_l = nullptr) {
  auto ma = [&](uint32_t site, float a, float b, float c) { return (mask & site) ? std::fma(a, b, c) : a * b + c; };
  const float* cam = in.camera;
  const float m0 = in.material[0], m1 = in.material[1], m2 = in.material[2], m3 = in.material[3];
  const float s1 = in.scene[0], s3 = in.scene[1], s5 = in.scene[2];
  float i90[3], p22[3];
  for (int k = 0; k < 3; ++k) i90[k] = v.rows[k][3] + cam[k];
  for (int k = 0; k < 3; ++k) p22[k] = ((v.p[0] * v.rows[k][0] + v.p[1] * v.rows[k][1]) + v.p[2] * v.rows[k][2]) + v.rows[k][3];  // precise
  const float h0 = std::fabs(fr(ma(SP1, i90[0], 0.100000001490116119384765625f, 0.5f)) * 2.0f - 1.0f);
  const float h1 = std::fabs(fr(ma(SP1, i90[2], 0.100000001490116119384765625f, 0.5f)) * 2.0f - 1.0f);
  const float inner = std::fma(in.time, m1 * (s5 * 0.100000001490116119384765625f), ((v.col[1] * m2) * v.col[3]) * 0.20000000298023223876953125f);
  const float F = std::fma(h0 + h1, 0.5f, inner);
  const float ck[4] = {0.17000000178813934326171875f, 0.37000000476837158203125f, 1.97500002384185791015625f, 0.7929999828338623046875f};
  float s[4];
  for (int k = 0; k < 4; ++k) { const float x = std::fabs(fr(ma(SP2, ck[k], F, 0.5f)) * 2.0f - 1.0f); s[k] = (x * x) * (3.0f - x * 2.0f); }
  const float S = ((s[0] + s[1]) * (s[2] + s[3])) * 0.25f;
  const float g = std::fma(S, std::fma(-s5, 0.089999996125698089599609375f, 1.0f), s5 * 0.089999996125698089599609375f) * s5;
  const float amp = m3 * std::fma(v.col[1], m2, v.col[0]);
  float d[3], Y[3];
  for (int k = 0; k < 3; ++k) d[k] = (p22[k] + cam[k]) - i90[k];
  const float wg[3] = {s1 * g, 0.0f * g, s3 * g};
  for (int k = 0; k < 3; ++k) Y[k] = ma(SY, wg[k], amp, d[k]);
  const float len = std::sqrt(dot3(d, d, mask & SLen)), inv = 1.0f / std::sqrt(dot3(Y, Y, mask & SNorm));
  if (y_over_l) *y_over_l = len > 0 ? 1.0f / (inv * len) : 0.0f;
  float rel[3];
  for (int k = 0; k < 3; ++k) rel[k] = i90[k] - cam[k];
  const float x = std::sqrt(dot3(rel, rel, mask & SFadeLen));
  float t = (mask & SFade) ? std::fma(x, 1.0f / 15.0f, -2.0f) : (x - 30.0f) / 15.0f;
  t = std::min(std::max(t, 0.0f), 1.0f);
  const float fade = std::fma(-(t * t * (3.0f - 2.0f * t)), m0, 1.0f);
  for (int k = 0; k < 3; ++k) w23[k] = p22[k] + ma(SQ, Y[k] * inv, len, -d[k]) * fade;  // precise add
  for (int c = 0; c < 4; ++c)
    clip[c] = ((w23[0] * in.viewproj[c] + w23[1] * in.viewproj[4 + c]) + w23[2] * in.viewproj[8 + c]) + in.viewproj[12 + c];
  clip[1] = -clip[1];
}
static uint32_t rng_state = 12345u;
static float rnd(float lo, float hi) { rng_state = rng_state * 1664525u + 1013904223u; return lo + (hi - lo) * float(rng_state >> 8) / 16777216.0f; }
static instanced::WindInputs wind_inputs(float time) {
  instanced::WindInputs in{};
  in.ok = true;
  in.time = time;
  in.scene[0] = 0.70710677f; in.scene[1] = -0.70710677f; in.scene[2] = 1.3f;
  in.material[0] = 0.5f; in.material[1] = 0.8f; in.material[2] = 0.6f; in.material[3] = 0.35f;
  in.camera[0] = 412.25f; in.camera[1] = 31.5f; in.camera[2] = -367.75f;
  // Perspective in the GLSL row-vector convention: rows k = 0..2 multiply W_k, row 3 the constant (jitter, near).
  const float f = 1.5f, aspect = 16.0f / 9.0f, cy = 0.8660254f, sy = 0.5f, cx = 0.9396926f, sx = 0.34202015f;
  const float R[3][3] = {{cy, 0, -sy}, {sy * sx, cx, cy * sx}, {sy * cx, -sx, cy * cx}};  // view rows (right, up, forward)
  for (int k = 0; k < 3; ++k) {
    in.viewproj[4 * k + 0] = f / aspect * R[0][k];
    in.viewproj[4 * k + 1] = f * R[1][k];
    in.viewproj[4 * k + 2] = 0.0f;
    in.viewproj[4 * k + 3] = R[2][k];
  }
  in.viewproj[12] = 3.1e-4f; in.viewproj[13] = -2.2e-4f; in.viewproj[14] = 0.1f; in.viewproj[15] = 0.0f;
  return in;
}
static WindVertex wind_vertex(float pivot_dist) {
  WindVertex v{};
  const float a = rnd(0.0f, 6.2831853f), sc = rnd(0.7f, 1.4f), ca = std::cos(a) * sc, sa = std::sin(a) * sc;
  const float px = rnd(-1.0f, 1.0f), pz = rnd(-1.0f, 1.0f), n = std::sqrt(px * px + pz * pz) + 1e-3f;
  const float r = rnd(2.0f, pivot_dist), py = rnd(-1.5f, 0.5f);
  const float rows[3][4] = {{ca, 0, sa, px / n * r}, {0, sc, 0, py}, {-sa, 0, ca, pz / n * r}};
  std::memcpy(v.rows, rows, sizeof rows);
  v.p[0] = rnd(-1.5f, 1.5f); v.p[1] = rnd(0.0f, 4.0f); v.p[2] = rnd(-1.5f, 1.5f);
  for (float& c : v.col) c = rnd(0.0f, 1.0f);
  return v;
}
static double worst_ratio = 0;  // max |c - c'| / tau seen by within()
static bool within(const instanced::ClipTolerance& tol, const float a[4], const float b[4], const float w23[3]) {
  const float w = std::max({std::fabs(w23[0]), std::fabs(w23[1]), std::fabs(w23[2])});
  bool ok = true;
  for (int c = 0; c < 4; ++c) {
    const float tau = tol.a[c] + tol.b[c] * w, e = std::fabs(a[c] - b[c]);
    worst_ratio = std::max(worst_ratio, double(e) / double(tau));
    ok = ok && e <= tau;
  }
  return ok;
}
static void wind_tests() {
  using namespace instanced;
  // Input match: unread, equal, time only, anything else differs.
  WindInputs a = wind_inputs(1234.5f), b = a, unread{};
  CHECK(wind_match(a, b) == MatchEqual && wind_match(a, unread) == MatchUnread && wind_match(unread, a) == MatchUnread);
  b.time += 1.0f / 60.0f;
  CHECK(wind_match(a, b) == MatchTimeOnly);
  WindInputs c = a;c.scene[2] = 1.31f;
  CHECK(wind_match(a, c) == MatchDiffer);
  c = a;c.viewproj[12] = 0.0f;
  CHECK(wind_match(a, c) == MatchDiffer);
  ClipTolerance tol{}, tol_dt{};
  CHECK(!wind_clip_tolerance(a, c, tol) && !wind_clip_tolerance(a, unread, tol));
  CHECK(wind_clip_tolerance(a, a, tol) && wind_clip_tolerance(a, b, tol_dt));
  // The test projection's z column is (0, 0, 0, 0.1): b_z = 0 (z does not depend on W), a_z > 0.
  for (int k = 0; k < 4; ++k) CHECK(tol.a[k] > 0 && (k == 2 ? tol.b[k] == 0 : tol.b[k] > 0) && std::isfinite(tol.a[k]) && tol_dt.a[k] >= tol.a[k] && tol_dt.b[k] >= tol.b[k]);
  CHECK(tol_dt.a[0] > tol.a[0] && tol_dt.a[3] > tol.a[3]);
  // Monotone in the camera's absolute magnitude (the wind phase works in absolute world xz).
  WindInputs far = a;far.camera[0] *= 4.0f;far.camera[2] *= 4.0f;ClipTolerance tol_far{};
  CHECK(wind_clip_tolerance(far, far, tol_far) && tol_far.a[0] > tol.a[0]);
  // Soundness: two compilations (fused / unfused sites) on the same inputs stay within tau, over random
  // instances within 60 m and the fade band; vertices in the bound's near-cancelling band are skipped.
  uint32_t differ = 0, skipped = 0, tested = 0, fails = 0;
  for (int i = 0; i < 20000; ++i) {
    const WindVertex v = wind_vertex(60.0f);
    const uint32_t m1 = (i & 1) ? 0u : rng_state & 255u, m2 = (i & 1) ? 255u : (rng_state >> 8) & 255u;
    float wa[3], wb[3], ca[4], cb[4], y_over_l = 0;
    wind_vs(a, v, m1, wa, ca, &y_over_l);
    wind_vs(a, v, m2, wb, cb);
    // The bound's one exception: |Y| < L / 2 (the wind vector nearly cancels the pivot offset).
    if (y_over_l < 0.5f) { ++skipped; continue; }
    ++tested;
    differ += std::memcmp(ca, cb, sizeof ca) != 0;
    if (!within(tol, ca, cb, wa)) ++fails;
  }
  std::printf("wind tolerance: %u vertices, %u with differing clip bits, %u outside tau (worst |dc| / tau %.3g), %u skipped (near-cancelling band); tau_x %.3g + %.3g |W|\n",
              tested, differ, fails, worst_ratio, skipped, double(tol.a[0]), double(tol.b[0]));
  CHECK(tested > 15000 && differ > 100 && fails == 0);
  worst_ratio = 0;
  // The time-dependent term evaluated in each pass (1/60 s apart) is within the time-only tolerance.
  uint32_t dt_fails = 0, dt_differ = 0;
  for (int i = 0; i < 5000; ++i) {
    const WindVertex v = wind_vertex(60.0f);
    float wa[3], wb[3], ca[4], cb[4];
    wind_vs(a, v, 0u, wa, ca);
    wind_vs(b, v, 255u, wb, cb);
    dt_differ += std::memcmp(ca, cb, sizeof ca) != 0;
    if (!within(tol_dt, ca, cb, wa)) ++dt_fails;
  }
  std::printf("wind tolerance, time 1/60 s apart: %u of 5000 vertices differ, %u outside tau (worst |dc| / tau %.3g); tau_x %.3g + %.3g |W|\n",
              dt_differ, dt_fails, worst_ratio, double(tol_dt.a[0]), double(tol_dt.b[0]));
  CHECK(dt_differ > 1000 && dt_fails == 0);
  // Fail closed: the same vertex of another instance 5 cm away (an instance order that differs between
  // the passes) is outside tau, also with the time-only tolerance; so is a different vertex 5 cm away.
  uint32_t caught = 0;
  for (int i = 0; i < 2000; ++i) {
    WindVertex v = wind_vertex(40.0f), other = v;
    const float ang = rnd(0.0f, 6.2831853f);
    other.rows[0][3] += 0.05f * std::cos(ang);
    other.rows[2][3] += 0.05f * std::sin(ang);
    float wa[3], wb[3], ca[4], cb[4];
    wind_vs(a, v, 0u, wa, ca);
    wind_vs(a, other, 0u, wb, cb);
    caught += !within(tol, ca, cb, wa) && !within(tol_dt, ca, cb, wa);
  }
  CHECK(caught == 2000);
}
int main() {
  using namespace instanced;
  // 1. Families: depth by VS only, G-buffer by the exact pair; the class A STI pair is none of them.
  CHECK(depth_family(kWindDepth) == Wind && depth_family(k2sDepth) == AlphaTest2s && depth_family(kWindVs) == None);
  CHECK(deferred_family(kWindVs, kWindFs) == Wind && deferred_family(k2sVs, k2sFs) == AlphaTest2s);
  CHECK(deferred_family(kWindVs, k2sFs) == None && deferred_family(kStiVs, kStiFs) == None && deferred_family(kWindDepth, kWindFs) == None);
  CHECK(frame_vs(kWindVs) && frame_vs(k2sVs) && !frame_vs(kWindDepth) && !frame_vs(kStiVs));
  // 2. Recipes: CaptureDraw rows, never pool pairs, still "no pool path" for the key-off census.
  const auto* w = recipe(kWindVs, kWindFs);const auto* a = recipe(k2sVs, k2sFs);
  CHECK(w && a && w->capture == vt::CaptureDraw && a->capture == vt::CaptureDraw);
  CHECK(recipe(kStiVs, kStiFs) == nullptr && recipe(kWindVs, kStiFs) == nullptr && recipe(k2sVs, kWindFs) == nullptr);
  CHECK(pool::pool_recipe(kWindVs, kWindFs) == nullptr && pool::pool_recipe(k2sVs, k2sFs) == nullptr);
  CHECK(pool::uncaptured_recipe(kWindVs, kWindFs) && pool::uncaptured_recipe(k2sVs, k2sFs) && !pool::uncaptured_recipe(kStiVs, kStiFs));
  // 3. Key: depth and G-buffer draws of one mesh join although they bind different vertex streams
  //    and first instances; count, instances, index range and family keep draws apart.
  const DrawKey depth = key(ordinary(1, 300, 12, 60, 7, 0, 0xabc, 4096, 0x111), Wind);
  const DrawKey gbuf = key(ordinary(1, 300, 12, 60, 7, 24, 0xabc, 4096, 0x222), Wind);
  CHECK(depth == gbuf && depth.words[20] == kKeyMagic && depth.words[21] == Wind && depth.words[9] == 0 && depth.words[5] == 0);
  CHECK(!(key(ordinary(1, 300, 13, 60, 7, 0, 0xabc, 4096, 0x111), Wind) == depth));   // instances
  CHECK(!(key(ordinary(1, 303, 12, 60, 7, 0, 0xabc, 4096, 0x111), Wind) == depth));   // count
  CHECK(!(key(ordinary(1, 300, 12, 63, 7, 0, 0xabc, 4096, 0x111), Wind) == depth));   // first index
  CHECK(!(key(ordinary(1, 300, 12, 60, 8, 0, 0xabc, 4096, 0x111), Wind) == depth));   // vertex offset
  CHECK(!(key(ordinary(1, 300, 12, 60, 7, 0, 0xabd, 4096, 0x111), Wind) == depth));   // index buffer
  CHECK(!(key(ordinary(1, 300, 12, 60, 7, 0, 0xabc, 4160, 0x111), Wind) == depth));   // index offset
  CHECK(!(key(ordinary(1, 300, 12, 60, 7, 0, 0xabc, 4096, 0x111), AlphaTest2s) == depth));
  CHECK(!(depth == ordinary(1, 300, 12, 60, 7, 0, 0xabc, 4096, 0x111)));             // never an ordinary key
  // Non-indexed: the stale index binding is not part of the key.
  CHECK(key(ordinary(0, 300, 4, 0, 0, 0, 0xabc, 4096, 1), Wind) == key(ordinary(0, 300, 4, 0, 0, 0, 0, 0, 2), Wind));
  // 4. Geometry: every instance's triangles (vertices = count x instances), the 64 B material stride.
  CHECK(prepass_geometry_ok(300 * 12, 12, 1200, 64) && prepass_geometry_ok(300, 1, 100, 64));
  CHECK(!prepass_geometry_ok(300 * 12, 12, 100, 64) && !prepass_geometry_ok(300 * 12, 12, 1200, 24) && !prepass_geometry_ok(0, 0, 0, 64));
  // The ordinary rule keeps declining instanced non-tileset prepass draws (unchanged).
  CHECK(!bg3::native_hit::prepass_geometry_ok(false, 300 * 12, 12, 1200, 64));
  CHECK(instanced::deferred_qualify(true, 1, 12, 64) == DeferredQualify::Accept && instanced::deferred_qualify(true, 0, 1, 64) == DeferredQualify::Accept);
  CHECK(instanced::deferred_qualify(false, 1, 12, 64) == DeferredQualify::PipelineDeclined);
  CHECK(instanced::deferred_qualify(true, 2, 12, 64) == DeferredQualify::PipelineDeclined);   // indirect
  CHECK(instanced::deferred_qualify(true, 1, 0, 64) == DeferredQualify::PipelineDeclined);
  CHECK(instanced::deferred_qualify(true, 1, 12, 24) == DeferredQualify::PipelineDeclined);
  CHECK(bg3::native_hit::deferred_qualify(true, false, true, 1, 12, 64) == DeferredQualify::PipelineDeclined);  // unchanged
  // 5. Staging into the gather's 8995 layout (hit_material_gather.hlsl Engine v[4]/v[5]/words.y/.z,
  //    Physical x at +256): wind engine {p0, p1, index @32, channel @36}, reflectance 1:1 byte 16;
  //    2S engine {p0, p1, offset @32, index @44, channel @48, flag @52}, reflectance byte 0.
  {
    uint32_t engine[13], material[8];
    for (uint32_t i = 0; i < 13; ++i) engine[i] = 0x1000u + i;
    for (uint32_t i = 0; i < 8; ++i) material[i] = 0x2000u + i;
    unsigned char s[kStageBytes];
    auto word = [&](uint32_t byte) { uint32_t v; std::memcpy(&v, s + byte, 4); return v; };
    CHECK(stage(*w, engine, 10, material, 5, s));
    for (uint32_t i = 0; i < 8; ++i) CHECK(word(64 + 4 * i) == 0x1000u + i);
    CHECK(word(100) == 0x1008u && word(104) == 0x1009u && word(96) == 0 && word(108) == 0 && word(kStagePhysical) == 0x2004u);
    CHECK(word(0) == 0 && word(60) == 0 && word(kStagePhysical + 4) == 0);
    CHECK(!stage(*w, engine, 9, material, 5, s) && !stage(*w, engine, 10, material, 4, s));   // short reads decline
    CHECK(stage(*a, engine, 13, material, 1, s));
    for (uint32_t i = 0; i < 8; ++i) CHECK(word(64 + 4 * i) == 0x1000u + i);
    CHECK(word(100) == 0x100bu && word(104) == 0x100cu && word(kStagePhysical) == 0x2000u);
    CHECK(!stage(*a, engine, 12, material, 1, s));
    // The engine reads cover engine_bytes (capture's minimum binding range).
    CHECK(pool::engine_bytes(*w) == 40 && pool::engine_bytes(*a) == 52 && vt::material_bytes(*w) == 20 && vt::material_bytes(*a) == 4);
    // Only the base layout with a UBO reflectance can be staged (other recipes keep their own paths).
    CHECK(!stage(*vt::recipe(0x3df678c09f13ea60ull), engine, 13, material, 8, s));
    CHECK(!stage(*vt::recipe(0x613ead9c442b9c8aull), engine, 13, material, 8, s));
  }
  // 6. Row flags (word 220): two-sided only for 923131c7.
  CHECK(row_flags(*a) == kRowTwoSided && row_flags(*w) == 0 && row_flags(*vt::recipe(kStiFs)) == 0 && kRowFlagsWord == 220);
  // Family bits 8..9 and the tolerance bit never overlap the two-sided bit; the tolerance words follow 220.
  CHECK(family_flags(Wind) == 0x100 && family_flags(AlphaTest2s) == 0x200 && family_flags(None) == 0);
  CHECK((kRowTolerant & kRowTwoSided) == 0 && (kRowTolerant & (3u << kRowFamilyShift)) == 0 && kRowToleranceWord == 224);
  wind_tests();
  std::printf("native_hit_instanced_test: %u checks passed\n", checks);
  return 0;
}
