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
#include <vector>
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
// ---- The live clip rejects (m4i-instanced-s14-fg2.1.093913): pairing and instance order ----
// The 2S VSs (106a0550 depth / 1e9145e4 G-buffer, dumped GLSL; precise throughout): W = P * rows with the
// per-draw translation t added to the rows' w, clip = vec4(W, 1) * _m2, clip.y negated; UV = input.
struct Inst2s { float rows[3][4]; };
static void vs_2s(const instanced::WindInputs& in, const Inst2s& inst, const float p[3], const float uv[2], unsigned char* out) {
  float w[3], clip[4];
  for (int k = 0; k < 3; ++k) {
    const float tw = inst.rows[k][3] + in.translation[k];
    w[k] = ((p[0] * inst.rows[k][0] + p[1] * inst.rows[k][1]) + p[2] * inst.rows[k][2]) + tw;
  }
  for (int c = 0; c < 4; ++c) clip[c] = ((w[0] * in.viewproj[c] + w[1] * in.viewproj[4 + c]) + w[2] * in.viewproj[8 + c]) + in.viewproj[12 + c];
  clip[1] = -clip[1];
  std::memset(out, 0, 64);
  std::memcpy(out, clip, 16);
  std::memcpy(out + 16, uv, 8);
  std::memcpy(out + 24, w, 12);  // stands in for the N/B/T words (offsets 24..60)
}
// One draw's XFB (stride 64): every instance's triangles in instance-major order, `order` the draw's
// instance stream (an index into `insts` per instance slot).
static std::vector<unsigned char> xfb_2s(const instanced::WindInputs& in, const std::vector<Inst2s>& insts, const std::vector<uint32_t>& order,
                                         const std::vector<float>& mesh, uint32_t vertices) {
  std::vector<unsigned char> out(size_t(order.size()) * vertices * 64);
  for (size_t s = 0; s < order.size(); ++s)
    for (uint32_t v = 0; v < vertices; ++v) {
      const float* m = mesh.data() + 5 * v;
      vs_2s(in, insts[order[s]], m, m + 3, out.data() + (s * vertices + v) * 64);
    }
  return out;
}
// The consumer's per-hit check on triangle `prim` (native_hit_material.hlsli hm_clip_instanced, 2S: no
// tolerance): 1 bit-exact, 3 UV differs, 6 clip differs; *rigid as its rigid-offset test with tau 0.
static uint32_t hit_check(const std::vector<unsigned char>& pre, const std::vector<unsigned char>& def, uint32_t prim, bool* rigid) {
  bool exact = true, uv = true;
  float d[3][4];
  for (uint32_t v = 0; v < 3; ++v) {
    const unsigned char* a = pre.data() + (prim * 3 + v) * 64;
    const unsigned char* b = def.data() + (prim * 3 + v) * 64;
    uv = uv && std::memcmp(a + 16, b + 16, 8) == 0;
    const bool same = std::memcmp(a, b, 16) == 0;
    exact = exact && same;
    float fa[4], fb[4];
    std::memcpy(fa, a, 16);
    std::memcpy(fb, b, 16);
    for (int c = 0; c < 4; ++c) d[v][c] = same ? 0.0f : fa[c] - fb[c];
  }
  bool r = !exact;
  for (int c = 0; c < 4; ++c) r = r && d[1][c] - d[0][c] == 0.0f && d[2][c] - d[0][c] == 0.0f;
  if (rigid) *rigid = r;
  return !uv ? 3u : exact ? 1u : 6u;
}
static void pairing_tests() {
  using namespace instanced;
  // A 2S mesh of 4 triangles (12 vertices: position + UV), 9 rotated / scaled foliage instances.
  const uint32_t vertices = 12, instances = 9, stride = 64, tris = vertices / 3 * instances;
  std::vector<float> mesh;
  for (uint32_t v = 0; v < vertices; ++v) {
    mesh.push_back(rnd(-0.6f, 0.6f)); mesh.push_back(rnd(0.0f, 1.8f)); mesh.push_back(rnd(-0.6f, 0.6f));
    mesh.push_back(rnd(0.0f, 1.0f)); mesh.push_back(rnd(0.0f, 1.0f));
  }
  std::vector<Inst2s> insts(instances);
  for (auto& in : insts) {
    const float a = rnd(0.0f, 6.2831853f), sc = rnd(0.8f, 1.3f), ca = std::cos(a) * sc, sa = std::sin(a) * sc;
    const float rows[3][4] = {{ca, 0, sa, rnd(-8.0f, 8.0f)}, {0, sc, 0, rnd(-0.5f, 0.5f)}, {-sa, 0, ca, rnd(-8.0f, 8.0f)}};
    std::memcpy(in.rows, rows, sizeof rows);
  }
  WindInputs frame = wind_inputs(1234.5f);
  frame.translation[0] = 96.0f; frame.translation[1] = -2.0f; frame.translation[2] = -40.0f;
  std::vector<uint32_t> depth_order(instances), gbuf_order;
  for (uint32_t i = 0; i < instances; ++i) depth_order[i] = i;
  gbuf_order = {0, 1, 2, 5, 3, 4, 6, 8, 7};  // the G-buffer pass uploads three instances in another order
  const auto pre = xfb_2s(frame, insts, depth_order, mesh, vertices);
  const auto def = xfb_2s(frame, insts, gbuf_order, mesh, vertices);
  // Reproduction: the live signature on every reordered instance, at one game frame (pair_check same).
  CHECK(pair_check(AlphaTest2s, frame, frame) == PairSame);
  uint32_t exact = 0, uv_differs = 0, differs = 0, rigid = 0;
  for (uint32_t prim = 0; prim < tris; ++prim) {
    bool r = false;
    const uint32_t o = hit_check(pre, def, prim, &r);
    exact += o == 1; uv_differs += o == 3; differs += o == 6; rigid += r;
  }
  std::printf("instance order: %u triangles, bit-exact %u, UV differs %u, differs %u, rigid offset %u (before the remap)\n", tris, exact, uv_differs, differs, rigid);
  CHECK(uv_differs == 0 && rigid == 0 && differs == 5 * (vertices / 3) && exact == tris - differs);
  // Fix: the remap gives every depth instance its G-buffer block, then every hit is bit-exact.
  uint32_t block = 0;
  CHECK(remap_ok(instances, tris, stride, block) && block == vertices * stride);
  CHECK(!remap_ok(1, tris / instances, stride, block) && !remap_ok(instances, tris + 1, stride, block) && !remap_ok(instances, tris, 20, block));
  CHECK(remap_ok(6, 6, stride, block) && block == 3 * stride);  // one triangle per instance: the minimum
  CHECK(!remap_ok(3, 1, stride, block) && !remap_ok(6, 3, stride, block) && block == 0);
  CHECK(remap_ok(instances, tris, stride, block));
  std::vector<unsigned char> fixed(def.size());
  uint64_t counts[kOrderCounters]{};
  remap_instances(pre.data(), def.data(), fixed.data(), instances, block, stride, counts);
  std::printf("remap counts: rows %llu same %llu permuted %llu unmatched %llu\n", (unsigned long long)counts[0], (unsigned long long)counts[1], (unsigned long long)counts[2], (unsigned long long)counts[3]);
  CHECK(counts[OrderRows] == 1 && counts[OrderSame] == 4 && counts[OrderPermuted] == 5 && counts[OrderUnmatched] == 0);
  for (uint32_t prim = 0; prim < tris; ++prim) CHECK(hit_check(pre, fixed, prim, nullptr) == 1);
  CHECK(std::memcmp(pre.data(), fixed.data(), pre.size()) == 0);
  // Same order: identity, nothing moves.
  uint64_t same[kOrderCounters]{};
  remap_instances(pre.data(), pre.data(), fixed.data(), instances, block, stride, same);
  CHECK(same[OrderSame] == instances && same[OrderPermuted] == 0 && same[OrderUnmatched] == 0);
  // Fail closed: a G-buffer draw with one other instance (culled / replaced) keeps that block, so the
  // hit check still rejects it; the matched instances move.
  std::vector<Inst2s> other = insts;
  other[4].rows[0][3] += 0.05f;
  const auto def_other = xfb_2s(frame, other, gbuf_order, mesh, vertices);
  uint64_t c2[kOrderCounters]{};
  remap_instances(pre.data(), def_other.data(), fixed.data(), instances, block, stride, c2);
  CHECK(c2[OrderUnmatched] == 1 && c2[OrderSame] + c2[OrderPermuted] == instances - 1);
  uint32_t rejected = 0;
  for (uint32_t prim = 0; prim < tris; ++prim) rejected += hit_check(pre, fixed, prim, nullptr) != 1;
  CHECK(rejected == vertices / 3);
  // Duplicated placement (two identical instances): either block is the same bytes.
  std::vector<Inst2s> dup = insts;dup[7] = dup[6];
  const auto pre_dup = xfb_2s(frame, dup, depth_order, mesh, vertices), def_dup = xfb_2s(frame, dup, gbuf_order, mesh, vertices);
  uint64_t c3[kOrderCounters]{};
  remap_instances(pre_dup.data(), def_dup.data(), fixed.data(), instances, block, stride, c3);
  CHECK(c3[OrderUnmatched] == 0 && std::memcmp(pre_dup.data(), fixed.data(), pre_dup.size()) == 0);
  // Pairing (the other cause): a G-buffer packet of another game frame joins through the frame-invariant
  // key. The frame time differs (one value per game frame): not mapped, whatever its geometry holds.
  WindInputs next = frame;next.time += 1.0f / 30.0f;
  CHECK(pair_check(AlphaTest2s, frame, next) == PairOtherFrame && pair_check(Wind, frame, next) == PairOtherFrame);
  WindInputs moved = frame;moved.viewproj[12] += 1e-4f;   // jitter / camera
  CHECK(pair_check(AlphaTest2s, frame, moved) == PairCamera && pair_check(Wind, frame, moved) == PairCamera);
  moved = frame;moved.camera[0] += 0.25f;
  CHECK(pair_check(AlphaTest2s, frame, moved) == PairCamera);
  WindInputs cell = frame;cell.translation[2] += 64.0f;   // the same mesh and instance count in another cell
  CHECK(pair_check(AlphaTest2s, frame, cell) == PairPlacement && pair_check(Wind, frame, cell) == PairSame);  // wind has no translation
  WindInputs gust = frame;gust.scene[2] += 0.01f;
  CHECK(pair_check(Wind, frame, gust) == PairWind && pair_check(AlphaTest2s, frame, gust) == PairSame);
  CHECK(pair_check(AlphaTest2s, frame, WindInputs{}) == PairUnread && pair_check(Wind, WindInputs{}, frame) == PairUnread && pair_check(None, frame, frame) == PairUnread);
  // Reproduction of that cause with the geometry: next frame's instance stream in another order -> the
  // same signature (UV equal, clip differs, not rigid); pair_check refuses the row before any hit.
  const auto def_next = xfb_2s(next, insts, gbuf_order, mesh, vertices);
  uint32_t next_differs = 0;
  for (uint32_t prim = 0; prim < tris; ++prim) next_differs += hit_check(pre, def_next, prim, nullptr) == 6;
  CHECK(next_differs == differs);
  CHECK(kPairOutcomes == sizeof kPairName / sizeof kPairName[0] && kRemapPermBase >= 16 * 3);
}
// ---- The third family, wind RGB 2S translucency (rt_hit_material_wind_rgb, startup) ----
// Off (the default) it does not exist for any lookup, so wind and 2S keep every decision they had.
static void wind_rgb_tests() {
  using namespace instanced;
  constexpr uint64_t kDepth = 0x633929ef4f08b8aeull, kVs = 0x735d5a19130262bdull, kFs = 0x838318119b2f43d8ull;
  // 1. Gate: key off -> None everywhere (depth, G-buffer, frame patch, recipe); on -> WindRgb.
  CHECK(depth_family(kDepth) == None && deferred_family(kVs, kFs) == None && !frame_vs(kVs) && recipe(kVs, kFs) == nullptr);
  CHECK(depth_family(kDepth, true) == WindRgb && deferred_family(kVs, kFs, true) == WindRgb && frame_vs(kVs, true) && !frame_vs(kDepth, true));
  CHECK(deferred_family(kVs, kStiFs, true) == None && deferred_family(kWindVs, kFs, true) == None && deferred_family(kDepth, kFs, true) == None);
  // The other families answer the same with the key on or off.
  for (bool on : {false, true}) {
    CHECK(depth_family(kWindDepth, on) == Wind && depth_family(k2sDepth, on) == AlphaTest2s && depth_family(kWindVs, on) == None);
    CHECK(deferred_family(kWindVs, kWindFs, on) == Wind && deferred_family(k2sVs, k2sFs, on) == AlphaTest2s);
    CHECK(frame_vs(kWindVs, on) && frame_vs(k2sVs, on) && !frame_vs(kStiVs, on));
    CHECK(recipe(kWindVs, kWindFs, on) == vt::recipe(kWindFs) && recipe(k2sVs, k2sFs, on) == vt::recipe(k2sFs));
  }
  CHECK(gate(WindRgb, false) == None && gate(Wind, false) == Wind && gate(AlphaTest2s, false) == AlphaTest2s);
  // 2. Recipe: a per-draw row, two-sided, reflectance 1:1 byte 20, staged into the gather's layout.
  const auto* r = recipe(kVs, kFs, true);
  CHECK(r && r == vt::recipe(kFs) && r->capture == vt::CaptureDraw && r->two_sided && r->reflectance_offset == 20);
  CHECK(row_flags(*r) == kRowTwoSided && family_flags(WindRgb) == 0x300 && (family_flags(WindRgb) & (kRowTolerant | kRowTwoSided)) == 0);
  uint32_t engine[10], material[8];
  for (uint32_t i = 0; i < 10; ++i) engine[i] = 0x1000u + i;
  for (uint32_t i = 0; i < 8; ++i) material[i] = 0x2000u + i;
  unsigned char s[kStageBytes];
  auto word = [&](uint32_t byte) { uint32_t v; std::memcpy(&v, s + byte, 4); return v; };
  CHECK(stage(*r, engine, 10, material, 6, s));
  for (uint32_t i = 0; i < 8; ++i) CHECK(word(64 + 4 * i) == 0x1000u + i);
  CHECK(word(100) == 0x1008u && word(104) == 0x1009u && word(kStagePhysical) == 0x2005u);   // index @32, channel @36, _m5
  CHECK(!stage(*r, engine, 10, material, 5, s) && pool::engine_bytes(*r) == 40 && vt::material_bytes(*r) == 24);
  // 3. Pairing: as wind (time, camera, scene and 1:1 _m0.._m3), plus the flutter amplitude 1:1 _m4.
  WindInputs frame{};frame.ok = true;frame.time = 12.5f;frame.scene[0] = .3f;frame.scene[1] = .1f;frame.scene[2] = 1.2f;
  for (int k = 0; k < 4; ++k) frame.material[k] = .25f * float(k + 1);
  frame.flutter = .4f;frame.camera[0] = 100.f;frame.viewproj[0] = 1.f;
  CHECK(pair_check(WindRgb, frame, frame) == PairSame);
  WindInputs f2 = frame;f2.flutter = .41f;
  CHECK(pair_check(WindRgb, frame, f2) == PairWind && pair_check(Wind, frame, f2) == PairSame);   // wind never reads _m4
  f2 = frame;f2.material[3] += .01f;
  CHECK(pair_check(WindRgb, frame, f2) == PairWind);
  f2 = frame;f2.time += 1.f / 30.f;
  CHECK(pair_check(WindRgb, frame, f2) == PairOtherFrame);
  f2 = frame;f2.viewproj[12] += 1e-4f;
  CHECK(pair_check(WindRgb, frame, f2) == PairCamera);
  f2 = frame;f2.translation[0] += 64.f;
  CHECK(pair_check(WindRgb, frame, f2) == PairSame);   // no per-draw translation in the wind VSs
  CHECK(pair_check(WindRgb, WindInputs{}, frame) == PairUnread);
  // 4. Sizes: one counter block per family, the family names, the remap counters below the permutations.
  CHECK(kFamilies == 5 && sizeof kFamilyName / sizeof kFamilyName[0] == kFamilies && kRemapPermBase >= 16 * kFamilies);
  CHECK(uint32_t(WindRgb) < kFamilies && uint32_t(WindRgb) <= 3u);   // word 220 bits 8..9
}
// ---- The fourth family, IP grass (rt_hit_material_ip_grass, startup) ----
static void ip_grass_tests() {
  using namespace instanced;
  constexpr uint64_t kDepth = 0x05d39f7537219e12ull, kVs = 0x105e47e448736d89ull, kFs = 0x35a18400f9654d9dull;
  // 1. Gate: key off -> None everywhere; on -> IpGrass; wind RGB independent of it.
  CHECK(depth_family(kDepth) == None && deferred_family(kVs, kFs) == None && !frame_vs(kVs) && recipe(kVs, kFs) == nullptr);
  CHECK(depth_family(kDepth, false, true) == IpGrass && deferred_family(kVs, kFs, false, true) == IpGrass && frame_vs(kVs, false, true));
  CHECK(!frame_vs(kDepth, true, true) && deferred_family(kVs, kStiFs, true, true) == None);
  for (bool rgb : {false, true}) for (bool grass : {false, true}) {
    CHECK(depth_family(kWindDepth, rgb, grass) == Wind && depth_family(k2sDepth, rgb, grass) == AlphaTest2s);
    CHECK(deferred_family(kWindVs, kWindFs, rgb, grass) == Wind && deferred_family(k2sVs, k2sFs, rgb, grass) == AlphaTest2s);
    CHECK(depth_family(0x633929ef4f08b8aeull, rgb, grass) == (rgb ? WindRgb : None));
  }
  // 2. Recipe: CaptureDraw, LayoutVt4 (VT layers at 1:4..1:7), engine UBO as the 2S family, reflectance at 1:1 byte 16;
  //    its frame patch puts N at location 2 (UV at 1, W at 0).
  const auto* r = recipe(kVs, kFs, false, true);
  CHECK(r && r->capture == vt::CaptureDraw && r->layout == vt::LayoutVt4 && !r->two_sided);
  CHECK(r->transform_offset == 0 && r->tileset_index_offset == 44 && r->channel_offset == 48 && r->reflectance_offset == 16);
  CHECK(vt::material_bytes(*r) == 20 && pool::pool_recipe(kVs, kFs) == nullptr);
  CHECK(frame_location(kVs) == 2 && frame_location(kWindVs) == 1 && frame_location(k2sVs) == 1);
  unsigned char staged[kStageBytes];uint32_t engine[16]{}, material[8]{};
  for (uint32_t i = 0; i < 16; ++i) engine[i] = 100 + i;
  material[4] = 0x3e800000u;   // 0.25 at byte 16
  CHECK(stage(*r, engine, 16, material, 8, staged));
  uint32_t idx, ch, refl;std::memcpy(&idx, staged + 100, 4);std::memcpy(&ch, staged + 104, 4);std::memcpy(&refl, staged + kStagePhysical, 4);
  CHECK(idx == 111 && ch == 112 && refl == 0x3e800000u);
  // 3. Pairing: the same certificate as wind (time, camera, scene and material words) plus the 2S translation.
  WindInputs a;a.ok = true;a.time = 3.f;a.scene[0] = .5f;a.material[1] = 2.f;a.camera[0] = 10.f;a.viewproj[0] = 1.f;a.translation[1] = 4.f;
  WindInputs b = a;
  CHECK(pair_check(IpGrass, a, b) == PairSame);
  b = a;b.translation[1] += 64.f;CHECK(pair_check(IpGrass, a, b) == PairPlacement);
  b = a;b.scene[0] += .01f;CHECK(pair_check(IpGrass, a, b) == PairWind);
  b = a;b.material[1] += .01f;CHECK(pair_check(IpGrass, a, b) == PairWind);
  b = a;b.time += 1.f / 30.f;CHECK(pair_check(IpGrass, a, b) == PairOtherFrame);
  b = a;b.camera[2] += .1f;CHECK(pair_check(IpGrass, a, b) == PairCamera);
  // 4. Word 220: the family needs bit 10; bits 8..9 of an IP grass row read 0 (a consumer without RT_HIT_IP_GRASS then
  //    checks the row with the plain bit-exact rule), and every older family reads the same under both masks.
  const uint32_t flags = row_flags(*r) | family_flags(IpGrass);
  CHECK(((flags >> 8) & 7u) == 4u && ((flags >> 8) & 3u) == 0u && (flags & 3u) == 0u);
  for (Family f : {Wind, AlphaTest2s, WindRgb}) CHECK(((family_flags(f) >> 8) & 7u) == ((family_flags(f) >> 8) & 3u));
  CHECK(kRemapPermBase == 80 && 16u * IpGrass + 16u <= kRemapPermBase);
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
  pairing_tests();
  wind_rgb_tests();
  ip_grass_tests();
  // 7. Stats window lines: the first query of each 120-frame window logs, even when frame % 120 == 0 had none.
  {
    uint64_t logged = UINT64_MAX;
    CHECK(stats_window_due(1801, logged) && logged == 15);   // frame 1800 rejected: 1801 still logs window 15
    CHECK(!stats_window_due(1802, logged) && !stats_window_due(1919, logged));
    CHECK(stats_window_due(1920, logged) && !stats_window_due(1920, logged));   // one line per window
    CHECK(stats_window_due(2100, logged) && logged == 17);   // a window with no query at all is skipped, not owed
    CHECK(!stats_window_due(2000, logged));                  // never a window twice (out-of-order slot frames)
    logged = 0; // reset-relative frame zero: never accept a straddling bucket
    CHECK(!stats_window_due(59, logged, 60));
    CHECK(stats_window_due(60, logged, 60));
    CHECK(!stats_window_due(61, logged, 60));
    CHECK(stats_window_due(121, logged, 60));

  }
  std::printf("native_hit_instanced_test: %u checks passed\n", checks);
  return 0;
}
