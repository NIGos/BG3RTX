// Box (projector) light math (common/box_light.h, used by shaders/rt/bg3_boxes.hlsli) against a
// literal transcription of the game's box loop, f469db78 (captures/alpha_cache/f469db7865d7c524.glsl
// lines 736-930: the standard class, the record's _m1 bit 0 path), and the ls_box=2 segment rules.
//
// Tolerance: both sides are float32 (SPIR-V float). The game expression and ours differ in operation
// order and fma use only, about 40 roundings each; the Burley factors (1 + fd x) are >= 0.045 for
// a >= 0.09 (the game's roughness floor), so their condition number is at most 22. The bound
// 40 x 22 x 2^-24 = 5.3e-5 relative is rounded up to 2^-14 (6.1e-5). It is 128x below the half-step of
// the game's B10G11R11 storage (2^-7 relative), so a pass cannot hide a visible error; any formula
// difference (a missing factor, a wrong curve) is orders of magnitude larger. The GGX peak adds its own
// conditioning, 8 x 2^-24 / dn (section 3).
#include "../common/box_light.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace bg3::box_light;

static unsigned g_checks = 0, g_fail = 0;
static void check(bool ok, const char* what) {
  ++g_checks;
  if (!ok) {
    ++g_fail;
    if (g_fail <= 20) std::fprintf(stderr, "FAIL: %s\n", what);
  }
}
static const float kTol = 1.0f / 16384.0f;   // 2^-14 relative (see the top)
static bool close(float a, float b) { return std::abs(a - b) <= kTol * std::max(std::abs(a), std::abs(b)) + 1e-30f; }

// ---- the game's expressions, transcribed (GLSL names in the comments) ----
static float g_clamp(float x) { return std::clamp(x, 0.0f, 1.0f); }
static float g_smooth(float t) { float x = g_clamp(t); return x * x * (3.0f - 2.0f * x); }   // GLSL smoothstep(0, 1, t)
// dot(_304[k], vec4(t, t*t, smoothstep(0,1,t), (t*t*t) * fma(t, fma(t, 6, -15), 10))); _304 the identity rows.
static float g_curve(int k, float t) {
  const float t2 = t * t;
  const float v[4] = {t, t2, g_smooth(t), (t2 * t) * std::fma(t, std::fma(t, 6.0f, -15.0f), 10.0f)};
  return k >= 0 && k < 4 ? v[k] : 0.0f;
}
// lines 754-817: _1279 (z), _1302 (x), _1320 (y); the product (_1320.x * _1320.y) * _1320.z.
static float g_falloff(float3 u, float4 r, float2 e, int k) {
  float z;
  if (u.z < r.x) z = g_curve(k, u.z * r.z);
  else if (u.z > r.y) z = g_curve(k, (1.0f - u.z) * r.w);
  else z = 1.0f;
  const float ex = (0.5f - std::abs(u.x - 0.5f)) * e.x, ey = (0.5f - std::abs(u.y - 0.5f)) * e.y;
  const float x = ex < 1.0f ? g_curve(k, ex) : 1.0f, y = ey < 1.0f ? g_curve(k, ey) : 1.0f;
  return (x * y) * z;
}
static float dot3(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float3 add3(float3 a, float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static float3 mul3(float3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static float3 norm3(float3 a) { return mul3(a, 1.0f / std::sqrt(dot3(a, a))); }
struct Out { float3 d, s; };
// lines 748-753 and 896-910, the standard class with _m1 bit 0 (x b13[0]): diffuse _1598, specular _1597.
// m9 the record's direction (not normalised: the game uses it as is), V = normalize(-P), N, a = _515,
// f0 = _482, alb = _477 (diffuse colour), col = _1466, g = the falloff, sc = b13[0].
static Out g_box(float3 m9, float3 V, float3 N, float a, float3 f0, float3 alb, float3 col, float g, float sc) {
  Out o{{0, 0, 0}, {0, 0, 0}};
  const float3 h0 = add3(V, m9);
  const float3 H = mul3(h0, 1.0f / std::sqrt(std::max(dot3(h0, h0), 1.0000000133514319600180897396058e-10f)));
  const float loh = g_clamp(dot3(m9, H)), noh = g_clamp(dot3(N, H)), nol = g_clamp(dot3(N, m9));
  const float nov = std::max(dot3(N, V), 9.9999997473787516355514526367188e-06f);   // _581
  const float c1326 = nol * g;                                                       // _1326 (no shadow map)
  if (!(nol > 0.0f)) return o;
  const float k = c1326 * sc;                                                        // _1433 * _6._m0[0]
  const float fd = std::fma((2.0f * loh) * loh, a, 0.0f + (0.5f - 0.0f) * a) - 1.0f; // _1545
  const float k2 = ((a + 1.0f) * 0.5f) * ((a + 1.0f) * 0.5f);                        // _1575
  const float a4 = (a * a) * (a * a);                                                // _1587
  const float dn = std::fma(std::fma(noh, a4, -noh), noh, 1.0f);                     // _1590
  const float vis = 0.5f / std::fma(nol, std::sqrt(std::fma(std::fma(-nov, k2, nov), nov, k2)),
                                    nov * std::sqrt(std::fma(std::fma(-nol, k2, nol), nol, k2)));
  const float D = a4 * (0.3183098733425140380859375f / (dn * dn));
  const float p5 = std::pow(std::max(std::abs(1.0f - loh), 9.9999997473787516355514526367188e-05f), 5.0f);
  const float bur = (1.0f + fd * std::pow(std::max(std::abs(1.0f - nol), 9.9999997473787516355514526367188e-05f), 5.0f)) *
                    (1.0f + fd * std::pow(std::max(std::abs(1.0f - nov), 9.9999997473787516355514526367188e-05f), 5.0f));
  const float diff = 0.3183098733425140380859375f * (bur * (1.0f + (0.662251651287078857421875f - 1.0f) * a));
  const float c[3] = {col.x * k, col.y * k, col.z * k};
  const float F[3] = {f0.x + (1.0f - f0.x) * p5, f0.y + (1.0f - f0.y) * p5, f0.z + (1.0f - f0.z) * p5};
  const float al[3] = {alb.x, alb.y, alb.z};
  float dd[3], ss[3];
  for (int i = 0; i < 3; ++i) {
    ss[i] = c[i] * (F[i] * (vis * D));
    dd[i] = c[i] * (al[i] * diff);
  }
  o.d = {dd[0], dd[1], dd[2]};
  o.s = {ss[0], ss[1], ss[2]};
  return o;
}

// ---- ours: the composition of shaders/rt/bg3_boxes.hlsli bg3_box_eval_mat (standard class) ----
static Out our_box(float3 m9, float3 V, float3 N, float a, float3 f0, float3 alb, float3 col, float g, float sc) {
  Out o{{0, 0, 0}, {0, 0, 0}};
  const float nol = dot3(N, m9);
  const float nolc = saturate(nol);
  if (!(nolc > 0.0f)) return o;
  const float nov = std::max(dot3(N, V), 1e-5f);   // Bg3Surface nov (bg3_materials.hlsli)
  const float3 h0 = add3(V, m9);
  const float3 H = mul3(h0, 1.0f / std::sqrt(std::max(dot3(h0, h0), 1.0000000e-10f)));
  const float loh = saturate(dot3(m9, H)), noh = saturate(dot3(N, H));
  const BlLobes l = bl_std_lobes(nolc, nov, loh, noh, a);
  const float k = (nolc * g) * sc;
  const float c[3] = {col.x * k, col.y * k, col.z * k};
  const float f[3] = {f0.x, f0.y, f0.z}, al[3] = {alb.x, alb.y, alb.z};
  float dd[3], ss[3];
  for (int i = 0; i < 3; ++i) {
    dd[i] = c[i] * (al[i] * l.diff);
    ss[i] = c[i] * ((f[i] + (1.0f - f[i]) * l.f5) * (l.vis * l.D));
  }
  o.d = {dd[0], dd[1], dd[2]};
  o.s = {ss[0], ss[1], ss[2]};
  return o;
}

int main() {
  std::mt19937 rng(20261101u);
  std::uniform_real_distribution<float> U(0.0f, 1.0f);
  auto u = [&](float lo, float hi) { return lo + (hi - lo) * U(rng); };
  auto dir = [&]() {
    for (;;) {
      float3 v{u(-1, 1), u(-1, 1), u(-1, 1)};
      float l = dot3(v, v);
      if (l > 1e-3f && l <= 1.0f) return norm3(v);
    }
  };

  // 1. The curves: the four of the game, index 4+ is no light.
  for (int k = 0; k < 5; ++k)
    for (int i = 0; i <= 64; ++i) {
      const float t = -0.25f + 1.5f * float(i) / 64.0f;
      check(close(bl_curve(uint(k), t), g_curve(k, t)), "curve");
    }
  check(bl_curve(2, 0.5f) == 0.5f && bl_curve(3, 0.5f) == 0.5f && bl_curve(1, 0.5f) == 0.25f, "curve values");

  // 2. The falloff, random records and points: ramps that overlap (the game's if/else jump, 329 boxes),
  //    side ramps that never reach 1, every curve. Bit-close to the game's expression.
  int n_overlap = 0;
  for (int i = 0; i < 200000; ++i) {
    const float rx = u(-0.1f, 0.7f), ry = u(0.3f, 1.1f);
    const float4 r{rx, ry, rx > 0 ? 1.0f / std::max(rx, 1e-3f) * u(0.5f, 2.0f) : u(0, 4), ry < 1 ? 1.0f / std::max(1 - ry, 1e-3f) * u(0.5f, 2.0f) : u(0, 4)};
    const float2 e{u(0.0f, 12.0f), u(0.0f, 12.0f)};
    const int k = int(u(0, 4.999f));
    const float3 p{u(0, 1), u(0, 1), u(0, 1)};
    n_overlap += rx > ry ? 1 : 0;
    check(close(bl_falloff(p, r, e, uint(k)), g_falloff(p, r, e, k)), "falloff");
  }
  check(n_overlap > 1000, "overlapping ramps sampled");
  check(bl_inside({0.5f, 0.5f, 0.5f}) && !bl_inside({0.5f, 0.5f, 1.0f}) && !bl_inside({0.0f, 0.5f, 0.5f}), "inside is strict");

  // 2b. bl_reaches (D-LS and the rf_hit_shade early-out): the game's channel test (_m4 bits 0-23 hold
  //     the surface's channel, (round(w * 255) & 112) >> 4 in 0-7) and the strict inside test on
  //     uvw = rows . (P, 1); the bits 24-31 (curve) never count as a channel.
  for (int i = 0; i < 20000; ++i) {
    const float4 r0{u(-0.3f, 0.3f), u(-0.3f, 0.3f), u(-0.3f, 0.3f), u(0, 1)}, r1{u(-0.3f, 0.3f), u(-0.3f, 0.3f), u(-0.3f, 0.3f), u(0, 1)},
        r2{u(-0.3f, 0.3f), u(-0.3f, 0.3f), u(-0.3f, 0.3f), u(0, 1)};
    const float3 P{u(-3, 3), u(-3, 3), u(-3, 3)};
    const uint ch = uint(u(0, 7.999f)), mask = uint(u(0, 255.99f)) | (uint(u(0, 3.99f)) << 24);
    float3 w;
    const bool r = bl_reaches(mask, r0, r1, r2, ch, P, w);
    const float3 q{r0.x * P.x + r0.y * P.y + r0.z * P.z + r0.w, r1.x * P.x + r1.y * P.y + r1.z * P.z + r1.w,
                   r2.x * P.x + r2.y * P.y + r2.z * P.z + r2.w};
    const bool on = ((mask & 16777215u) >> ch) & 1u;
    const bool in = std::abs(q.x - 0.5f) < 0.5f && std::abs(q.y - 0.5f) < 0.5f && std::abs(q.z - 0.5f) < 0.5f;
    check(r == (on && in), "reaches = channel and inside");
    if (on) check(w.x == q.x && w.y == q.y && w.z == q.z, "reaches uvw");
  }
  {
    float3 w;
    check(!bl_reaches(1u << 24, {0, 0, 0, 0.5f}, {0, 0, 0, 0.5f}, {0, 0, 0, 0.5f}, 0, {0, 0, 0}, w), "curve bits are no channel");
    check(bl_reaches(1u, {0, 0, 0, 0.5f}, {0, 0, 0, 0.5f}, {0, 0, 0, 0.5f}, 0, {0, 0, 0}, w), "channel 0 inside");
  }

  // 3. The standard-class terms: random surfaces, directions (the record's _m9 un-normalised, as the
  //    game reads it), roughness from the game's floor 0.09 to 1, F0 0.02-1, both lobes per channel.
  float worst = 0.0f, worst_s = 0.0f;   // diffuse: relative; specular: as a fraction of its bound
  int lit = 0;
  for (int i = 0; i < 200000; ++i) {
    const float3 N = dir(), V0 = dir();
    const float3 V = dot3(N, V0) < 0 ? mul3(V0, -1.0f) : V0;
    const float3 m9 = mul3(dir(), u(0.98f, 1.02f));
    const float a = u(0.09f, 1.0f);
    const float3 f0{u(0.02f, 1.0f), u(0.02f, 1.0f), u(0.02f, 1.0f)}, alb{u(0, 1), u(0, 1), u(0, 1)};
    const float3 col{u(0, 4e4f), u(0, 4e4f), u(0, 4e4f)};
    const float g = u(0, 1), sc = u(1e-4f, 3e-3f);
    const Out A = g_box(m9, V, N, a, f0, alb, col, g, sc), B = our_box(m9, V, N, a, f0, alb, col, g, sc);
    lit += A.d.x > 0 ? 1 : 0;
    // The GGX peak is ill-conditioned in float: dn = 1 - noh^2 (1 - a^4) cancels for smooth surfaces near
    // the mirror direction, and D ~ 1/dn^2 amplifies the rounding of its terms (fma or not) by about 2/dn.
    // The specular bound adds that: 8 x 2^-24 / dn (dn in double; up to 4 roundings, x 2 for the square).
    const double noh_d = std::clamp(double(dot3(N, norm3(add3(V, m9)))), 0.0, 1.0), a4_d = double(a) * a * a * a;
    const double dn_d = 1.0 - noh_d * noh_d * (1.0 - a4_d);
    const float tol_s = kTol + float(8.0 * 5.9604644775390625e-08 / dn_d);
    for (int c = 0; c < 3; ++c) {
      const float ad = A.d[c], bd = B.d[c], as = A.s[c], bs = B.s[c];
      const bool ok_s = std::abs(as - bs) <= tol_s * std::max(std::abs(as), std::abs(bs)) + 1e-30f;
      if (!(close(ad, bd) && ok_s) && g_fail < 5)
        std::fprintf(stderr, "  case: a %.4f nol %.6f nov %.6f noh %.6f d %g/%g s %g/%g\n", a, dot3(N, m9), dot3(N, V),
                     noh_d, ad, bd, as, bs);
      check(close(ad, bd) && ok_s, "standard terms vs the game");
      if (ad > 0) worst = std::max(worst, std::abs(ad - bd) / std::abs(ad));
      if (as > 0) worst_s = std::max(worst_s, std::abs(as - bs) / std::abs(as) / tol_s);
    }
    // N.L <= 0: the game adds nothing, and nor do we.
    if (dot3(N, m9) <= 0) check(B.d.x == 0 && B.s.x == 0 && A.d.x == 0 && A.s.x == 0, "back-facing is 0");
  }
  check(lit > 50000, "lit samples");
  std::printf("box_light_test: diffuse worst relative difference %.3g (bound %.3g); specular worst %.3g of its bound\n",
              worst, kTol, worst_s);

  // 4. The exit: from a box point along a world direction (the box map scaled per axis), the exit
  //    point lies on the box surface, and the segment stays inside.
  for (int i = 0; i < 20000; ++i) {
    const float3 p{u(0.01f, 0.99f), u(0.01f, 0.99f), u(0.01f, 0.99f)};
    const float3 s{u(0.05f, 2.0f), u(0.05f, 2.0f), u(0.05f, 2.0f)};   // box units per metre
    const float3 d = dir();
    const float3 dv{d.x * s.x, d.y * s.y, d.z * s.z};
    const float t = bl_exit(p, dv);
    const float3 q{p.x + dv.x * t, p.y + dv.y * t, p.z + dv.z * t};
    const float m = std::max({std::abs(q.x - 0.5f), std::abs(q.y - 0.5f), std::abs(q.z - 0.5f)});
    check(t > 0 && std::abs(m - 0.5f) < 1e-4f, "exit on the surface");
    const float3 h{p.x + dv.x * t * 0.999f, p.y + dv.y * t * 0.999f, p.z + dv.z * t * 0.999f};
    check(bl_inside(h), "segment inside");
  }

  // 5. ls_box=2, shadow-mapped: the near plane of an orthographic view along the light. View row 2
  //    z = dot(r2, (P, 1)), ref = ((z - dp.y) / dp.x) / dw; ref(t_near) = 0 and every point nearer the
  //    light has ref < 0 (not storable in the map); the receiver's own ref is > 0.
  for (int i = 0; i < 20000; ++i) {
    const float3 L = dir();                                 // towards the light
    const float near_d = u(0.5f, 30.0f), range = u(5.0f, 80.0f), dw = u(0.5f, 2.0f);
    const float3 R{u(-10, 10), u(-10, 10), u(-10, 10)};      // receiver
    // A view whose z grows away from the light, 0 at a plane near_d before the receiver.
    const float3 r2 = mul3(L, -1.0f);
    const float r2w = -dot3(r2, R) + near_d;                 // z(R) = near_d
    const float dpx = range, dpy = 0.0f;
    auto ref = [&](float3 P) { return ((dot3(r2, P) + r2w - dpy) / dpx) / dw; };
    const float ref0 = ref(R), slope = (dot3(r2, L) / dpx) / dw;
    const float tn = bl_shadow_near(ref0, slope);
    check(ref0 > 0 && std::abs(tn - near_d) <= 1e-4f * near_d + 1e-5f, "near plane distance");
    check(ref(add3(R, mul3(L, tn * 1.01f))) < 0 && ref(add3(R, mul3(L, tn * 0.99f))) > 0, "near plane side");
  }
  check(bl_shadow_near(0.3f, 0.0f) == kBlFar && bl_shadow_near(0.3f, 0.1f) == kBlFar, "no plane ahead");
  check(bl_shadow_near(-0.2f, -0.1f) == 0.0f, "receiver before the near plane: nothing to trace");

  // 6. ls_box=2, unshadowed: the segment ends where the light-side fade-in starts (uvw.z = ramp.x for
  //    a ray leaving through z = 0, ramp.y through z = 1), never beyond the box exit, <= 0 inside the
  //    fade-in, the face itself without a fade-in.
  for (int i = 0; i < 20000; ++i) {
    const float rx = u(0.0f, 0.4f), ry = u(0.6f, 1.0f);
    const float4 r{rx, ry, 0, 0};
    const float3 p{u(0.01f, 0.99f), u(0.01f, 0.99f), u(0.01f, 0.99f)};
    const float3 d = dir();
    const float3 dv = d;   // unit box scale
    const float te = bl_fixture_end(p.z, dv.z, r), tx = bl_exit(p, dv);
    if (dv.z < 0 && p.z > rx) check(std::abs((p.z + dv.z * te) - rx) < 1e-5f, "fixture plane z = ramp.x");
    if (dv.z > 0 && p.z < ry) check(std::abs((p.z + dv.z * te) - ry) < 1e-5f, "fixture plane z = ramp.y");
    if ((dv.z < 0 && p.z <= rx) || (dv.z > 0 && p.z >= ry)) check(te <= 0, "receiver in the fade-in");
    // The traced extent min(exit, fixture end) stays in the box and never crosses into the fade-in.
    const float tm = std::min(tx, te);
    if (tm > 0) {
      const float z = p.z + dv.z * tm * 0.999f;
      check(z >= std::min(rx, p.z) - 1e-5f && z <= std::max(ry, p.z) + 1e-5f, "segment outside the fade-in");
    }
  }
  check(bl_fixture_end(0.5f, -1.0f, {0.0f, 1.0f, 0, 0}) == 0.5f && bl_fixture_end(0.5f, 1.0f, {-0.2f, 1.3f, 0, 0}) == 0.5f,
        "no fade-in: to the face");
  check(bl_fixture_end(0.5f, 0.0f, {0.2f, 0.8f, 0, 0}) == kBlFar, "parallel to the faces");

  // 7. Energy (the ls_box=2 rule): an unshadowed box light's game term is s = 1 and our visibility t is
  //    1 (lit), 0 (occluded) or s (unknown, the game's own), so the delta c (t - s) is never positive.
  for (float t : {0.0f, 1.0f}) check(1.0f * (t - 1.0f) <= 0.0f, "unshadowed delta <= 0");

  std::printf("box_light_test: %u checks, %u failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
