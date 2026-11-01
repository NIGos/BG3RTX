// rf_hit_gi composition (common/hit_gi_compose.h, used by shaders/rt/native_hit_shade.hlsli NATIVE_HIT_GI)
// against a literal transcription of the screen's chain for the same point:
//   gi_ao_apply.hlsl    b6.x = lerp(SSAO, history .a, gi_ao) (the traced visibility replaces the SSAO)
//   f469db78 lighting   with m0.x > 0: ambient diffuse x b6, ambient specular x Lagarde SO(b6)
//                       (bg3_spec_occlusion), lights at the game's visibility x mix(1, b6, m0.y); D-LS's
//                       traced lights get their full value back with ao_direct=1 (else x the factor); the
//                       sun keeps the factor unless ao_direct=1 with its ray traced and its inputs bound
//                       (Push::ao_direct bits 2 and 3); m0.x <= 0: the game ignores b6
//   gi_composite.hlsl   + history .rgb x diffuse colour x gi_strength
// 1. A hit identical to a screen pixel, shaded from the screen's b6 and history there (rf_hit_gi=2, the
//    tier (a) check), composes exactly the screen's terms, for random inputs and every frame constant.
// 2. The per-ray rule hg_ray_weight is gi_trace_rt.hlsl's default path (gi_rt_fallback 0), transcribed:
//    the estimator at a hidden hit (rf_hit_gi=1) is the screen's estimator there.
// 3. Monte Carlo under a uniform infinite ceiling (height h, radiance L): the estimator's mean A and
//    bounce converge to the closed forms 1 - E[w] and L E[w], E[w] = 1 - c + c ln c, c = (h / R)^2.
#include "../common/hit_gi_compose.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace bg3::hit_gi;
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static bool near1(float x, float y, float tol) { return std::fabs(x - y) <= tol * (1.0f + std::fabs(y)); }
static bool near3(float3 a, float3 b, float tol) { return near1(a.x, b.x, tol) && near1(a.y, b.y, tol) && near1(a.z, b.z, tol); }

// ---- the screen, pass by pass (independent of the header) ----
struct ScreenTerms { float3 direct, sun, amb_d, amb_s, gi, total; };
static float3 mul(float3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static float3 add(float3 a, float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static ScreenTerms screen(const HgIn& in, float m0x, float m0y, uint32_t ao_direct, float strength, float gi_ao, float ssao,
                          float vis, float3 hist, float& b6) {
  b6 = ssao + (vis - ssao) * gi_ao;                       // gi_ao_apply.hlsl: ao.x = lerp(ao.x, vis, pc.ao)
  const float ao = m0x > 0.0f ? b6 : 1.0f;                // the lighting reads b6 only with m0.x > 0
  const float f = 1.0f + (ao - 1.0f) * m0y;               // mix(1, AO, m0.y)
  float so = 1.0f;
  if (m0x > 0.0f)
    so = std::clamp(std::pow(std::max(std::fabs(in.nov_raw + ao), 9.9999997e-05f), std::exp2(-16.0f * in.a - 1.0f)) - 1.0f + ao, 0.0f, 1.0f);
  const bool giveback = (ao_direct & 3u) == 1u;           // D-LS ao_direct=1
  const bool sun_back = giveback && (ao_direct & 4u) && (ao_direct & 8u);
  ScreenTerms s;
  s.direct = add(mul(in.game, f), mul(in.traced, giveback ? 1.0f : f));
  s.sun = mul(in.sun, sun_back ? 1.0f : f);
  s.amb_d = mul(in.amb_d, ao);
  s.amb_s = mul(in.amb_s, so);
  s.gi = {hist.x * in.dcol.x * strength, hist.y * in.dcol.y * strength, hist.z * in.dcol.z * strength};   // gi_composite.hlsl
  s.total = add(add(add(s.direct, s.sun), add(s.amb_d, s.amb_s)), s.gi);
  return s;
}
// rt_reflections.hlsl / native_hit_shade.hlsli with rf_hit_gi=2 at a tier (a) hit: g_hg_screen = (history at
// the pixel, b6 there); A = m0.x > 0 ? saturate(b6) : 1, bounce = max(history, 0).
static HgOut hit_from_screen(const HgIn& in, const HgFrame& fr, float b6, float3 hist) {
  const float A = fr.m0x > 0.0f ? saturate(b6) : 1.0f;
  return hg_compose(in, fr, A, float3{std::max(hist.x, 0.f), std::max(hist.y, 0.f), std::max(hist.z, 0.f)});
}

// gi_trace_rt.hlsl's per-ray occlusion and bounce weight (gi_rt_fallback 0), transcribed.
static float gi_trace_weight(float t, bool aborted, float R) {
  const float u = aborted ? (t >= 0.0f ? std::clamp(t / R, 0.f, 1.f) : 1.0f) : 0.0f;
  const float k = std::clamp(t / R, 0.f, 1.f);
  const float w = aborted ? 1.0f - u * u / 3.0f : 1.0f - k * k;
  if (t < 0.0f && !aborted) return 0.0f;                  // `if (t < 0.0 && !aborted) continue;` before occ += w
  return w;
}

int main() {
  std::mt19937 rng(20261101u);
  std::uniform_real_distribution<float> U(0.0f, 1.0f);
  auto r3 = [&](float s) { return float3{U(rng) * s, U(rng) * s, U(rng) * s}; };

  // 1. Screen identity.
  const float m0xs[] = {0.0f, 1.0f}, m0ys[] = {0.0f, 0.5f, 1.0f}, gi_aos[] = {1.0f, 0.5f};
  const uint32_t ao_directs[] = {0u, 1u, 2u, 1u | 4u, 1u | 8u, 1u | 12u, 2u | 12u};
  int cases = 0;
  for (float m0x : m0xs)
    for (float m0y : m0ys)
      for (float gi_ao : gi_aos)
        for (uint32_t aod : ao_directs)
          for (int n = 0; n < 200; ++n) {
            HgIn in;
            in.amb_d = r3(0.2f); in.amb_s = r3(0.05f); in.traced = r3(0.5f); in.game = r3(0.1f); in.sun = r3(2.0f);
            in.dcol = r3(1.0f); in.nov_raw = 2.0f * U(rng) - 0.2f; in.a = U(rng);
            const float strength = n % 7 == 0 ? 0.0f : 0.5f + U(rng);
            const float ssao = U(rng), vis = U(rng);
            const float3 hist = r3(0.3f);
            float b6;
            const ScreenTerms sc = screen(in, m0x, m0y, aod, strength, gi_ao, ssao, vis, hist, b6);
            const HgFrame fr{m0x, m0y, aod, strength};
            const HgOut h = hit_from_screen(in, fr, b6, hist);
            CHECK(near3(h.direct, sc.direct, 1e-6f));
            CHECK(near3(h.sun, sc.sun, 1e-6f));
            CHECK(near3(h.amb_d, sc.amb_d, 1e-6f));
            CHECK(near3(h.amb_s, sc.amb_s, 1e-6f));
            CHECK(near3(h.gi, sc.gi, 1e-6f));
            CHECK(near3(h.radiance, sc.total, 1e-6f));
            ++cases;
          }
  // Without the GI composite (gi off: the core pushes strength 0) the hit adds no indirect diffuse.
  {
    HgIn in{{0.1f, 0.1f, 0.1f}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0.5f, 0.5f, 0.5f}, 0.7f, 0.3f};
    const HgOut h = hg_compose(in, HgFrame{1.0f, 0.5f, 1u, 0.0f}, 0.6f, float3{1, 1, 1});
    CHECK(h.gi.x == 0.0f && h.gi.y == 0.0f && h.gi.z == 0.0f);
    CHECK(near1(h.radiance.x, 0.06f, 1e-6f));
  }
  // The old rf_hit_shade composition (no GI term) is hg_compose with a zero bounce: the key only adds.
  {
    HgIn in{{0.1f, 0.2f, 0.3f}, {0.01f, 0.02f, 0.03f}, {0.2f, 0.2f, 0.2f}, {0.05f, 0.05f, 0.05f}, {1, 1, 1}, {0.4f, 0.5f, 0.6f}, 0.5f, 0.4f};
    const HgFrame fr{1.0f, 0.5f, 1u | 12u, 1.0f};
    const HgOut a = hg_compose(in, fr, 0.7f, float3{0, 0, 0}), b = hg_compose(in, fr, 0.7f, float3{0.2f, 0.1f, 0.3f});
    CHECK(near3(b.radiance, a.radiance + float3{0.2f * 0.4f, 0.1f * 0.5f, 0.3f * 0.6f}, 1e-6f));
  }

  // 2. The per-ray rule is the GI trace's.
  for (int n = 0; n < 100000; ++n) {
    const float R = 0.05f + 3.0f * U(rng);
    const float t = n % 5 == 0 ? -1.0f : (2.5f * U(rng) - 0.2f) * R;
    const bool aborted = n % 3 == 0;
    CHECK(hg_ray_weight(t, aborted, R) == gi_trace_weight(t, aborted, R));
  }

  // 3. Monte Carlo against the closed form (cosine sampling as gi_common.hlsli cosine_hemisphere: cos^2 = 1 - u1).
  for (float hr : {0.1f, 0.3f, 0.6f, 0.9f}) {
    const float R = 1.0f, h = hr * R, L = 0.37f;
    const int N = 400000;
    double occ = 0, bounce = 0;
    for (int i = 0; i < N; ++i) {
      const float mu = std::sqrt(1.0f - U(rng));
      const float t = mu > 0.0f ? h / mu : 1e30f;
      const float w = hg_ray_weight(t < R ? t : -1.0f, false, R);   // beyond R: a miss within the ray's length
      occ += w;
      bounce += L * w;
    }
    const double c = double(hr) * hr, ew = 1.0 - c + c * std::log(c);
    const double A = 1.0 - occ / N, B = bounce / N;
    CHECK(std::fabs(A - (1.0 - ew)) < 4e-3);
    CHECK(std::fabs(B - L * ew) < 4e-3 * L);
  }
  std::printf("hit_gi_compose_test: %d checks (%d screen-identity cases)\n", checks, cases);
  return 0;
}
