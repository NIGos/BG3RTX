#pragma once
// rf_hit_gi (docs/TRACK_D_REFLECTIONS.md 14 "Indirect diffuse at hits"): how a shaded reflection hit
// composes its terms so that it gets what the screen gives the same point. Shared by the HLSL
// (shaders/rt/native_hit_shade.hlsli NATIVE_HIT_GI) and the CPU test tests/hit_gi_compose_test.cpp,
// which checks it against a literal transcription of the screen's chain: gi_ao_apply.hlsl (the traced
// visibility replaces the game's SSAO in b6), the tiled lighting's use of b6 (ambient diffuse x AO,
// specular x Lagarde's SO, direct light x mix(1, AO, m0.y) where the screen keeps that factor) and
// gi_composite.hlsl (+ the GI bounce x the diffuse colour x gi_strength).
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace bg3::hit_gi {
using uint = uint32_t;
struct float3 { float x, y, z; };
inline float3 operator+(float3 a, float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline float3 operator*(float3 a, float3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float3 operator*(float3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
using std::abs; using std::max; using std::exp2;
inline float saturate(float a) { return std::clamp(a, 0.f, 1.f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float hg_pow(float a, float b) { return std::pow(a, b); }
#define HG_INLINE inline
#else
float hg_pow(float a, float b) { return pow(a, b); }
#define HG_INLINE
#endif

// One AO / GI ray of length R (gi_trace_rt.hlsl with gi_rt_fallback 0, native_hit_direct.hlsli
// native_hit_ao): a hit at t weighs 1 - (t/R)^2, a query the candidate cap aborted the fade's mean
// 1 - u^2/3 (u = min(t, R)/R, 1 with nothing committed), a miss 0. The weight is the ray's occlusion,
// and its bounce is the hit's radiance x the weight where the radiance is known.
HG_INLINE float hg_ray_weight(float t, bool aborted, float R) {
  const float u = aborted ? (t >= 0.0f ? saturate(t / R) : 1.0f) : 0.0f;
  const float k = saturate(t / R);
  return aborted ? 1.0f - u * u / 3.0f : (t >= 0.0f ? 1.0f - k * k : 0.0f);
}
// The game's AO-on-direct factor mix(1, AO, m0.y) (f469db78; native_hit_ao_factor).
HG_INLINE float hg_ao_factor(float A, float m0x, float m0y) { return lerp(1.0f, m0x > 0.0f ? A : 1.0f, m0y); }
// Lagarde's specular occlusion as the lighting computes it from b6 (bg3_spec_occlusion).
HG_INLINE float hg_spec_occlusion(float A, float nov_raw, float a, float m0x) {
  if (!(m0x > 0.0f)) return 1.0f;
  return saturate(hg_pow(max(abs(nov_raw + A), 9.9999997e-05f), exp2(-16.0f * a - 1.0f)) - 1.0f + A);
}

// The hit's terms before any AO (at AO 1): ambient diffuse / specular (x b13[0]*100), direct light of
// traced lights and of lights at the game's visibility (box lights the screen leaves to the game
// included), the sun, the diffuse colour (the GI composite's mat_diffuse_colour), N.V and roughness.
struct HgIn { float3 amb_d, amb_s, traced, game, sun, dcol; float nov_raw, a; };
// Frame constants: b5 m0.x (the game uses its AO) and m0.y (AO-on-direct), Push::ao_direct (bits 0-1
// mode, 2 the sun traced, 3 its inputs bound) and the GI composite's strength (0 when it does not run).
struct HgFrame { float m0x, m0y; uint ao_direct; float strength; };
struct HgOut { float3 direct, sun, amb_d, amb_s, gi, radiance; float f; };

// A: the AO at the point (the screen's b6 there, or the same estimator at a hidden hit); bounce: the
// GI's irradiance term there (giRaw / history .rgb at the pixel, or the estimator's mean L x w).
HG_INLINE HgOut hg_compose(HgIn i, HgFrame fr, float A, float3 bounce) {
  HgOut o;
  o.f = hg_ao_factor(A, fr.m0x, fr.m0y);
  const float ft = (fr.ao_direct & 3u) == 1u ? 1.0f : o.f;   // ao_direct=1: traced lights get their full value
  o.direct = i.traced * ft + i.game * o.f;
  o.sun = i.sun * (((fr.ao_direct & 3u) == 1u && (fr.ao_direct & 12u) == 12u) ? 1.0f : o.f);
  o.amb_d = i.amb_d * A;
  o.amb_s = i.amb_s * hg_spec_occlusion(A, i.nov_raw, i.a, fr.m0x);
  o.gi = bounce * i.dcol * fr.strength;
  o.radiance = o.direct + o.sun + o.amb_d + o.amb_s + o.gi;
  return o;
}

#ifdef __cplusplus
}  // namespace bg3::hit_gi
#endif
