#pragma once
// Box (projector) light math shared by the HLSL (shaders/rt/bg3_boxes.hlsli) and the CPU test
// tests/box_light_test.cpp, which checks it against a literal transcription of the game's box loop
// (f469db78, captures/alpha_cache/f469db7865d7c524.glsl lines 736-930).
// Box space: uvw = world -> box (the record's _m6 rows); lit strictly inside the unit cube. The
// editor's position is the near face's centre (0.5, 0.5, 0) (docs/ASSET_DB.md "Projector anchor").
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace bg3::box_light {
using uint = uint32_t;
struct float2 { float x, y; };
struct float3 { float x, y, z; float operator[](unsigned i) const { return i == 0 ? x : i == 1 ? y : z; } };
struct float4 { float x, y, z, w; };
using std::abs; using std::min; using std::max; using std::sqrt;
inline float saturate(float a) { return std::clamp(a, 0.f, 1.f); }
inline float smoothstep(float a, float b, float x) { float t = saturate((x - a) / (b - a)); return t * t * (3.f - 2.f * t); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float bl_pow(float a, float b) { return std::pow(a, b); }
#define BL_INLINE inline
#define BL_OUT(T) T&
#define BL3(x, y, z) float3{x, y, z}
inline float bl_dot4p(float4 r, float3 P) { return r.x * P.x + r.y * P.y + r.z * P.z + r.w; }
#else
float bl_pow(float a, float b) { return pow(a, b); }
#define BL_INLINE
#define BL_OUT(T) out T
#define BL3(x, y, z) float3(x, y, z)
float bl_dot4p(float4 r, float3 P) { return dot(r, float4(P, 1.0)); }
#endif

static const float kBlFar = 3.0e38f;

// One ramp: _304[curve] . (t, t^2, smoothstep, smootherstep). A curve index past 3 reads past
// the game's constant array; it is treated as no light.
BL_INLINE float bl_curve(uint k, float t) {
  const float t2 = t * t;
  const float v0 = t, v1 = t2, v2 = smoothstep(0.0f, 1.0f, t), v3 = (t2 * t) * (t * (t * 6.0f - 15.0f) + 10.0f);
  return k == 0u ? v0 : k == 1u ? v1 : k == 2u ? v2 : k == 3u ? v3 : 0.0f;
}
// f469db78: lit only strictly inside the box.
BL_INLINE bool bl_inside(float3 uvw) {
  return abs(uvw.x - 0.5f) < 0.5f && abs(uvw.y - 0.5f) < 0.5f && abs(uvw.z - 0.5f) < 0.5f;
}
// Does the box light reach P at all (f469db78: its channel mask _m4 bits 0-23 holds the surface's
// channel, and P is strictly inside the box)? From the four record words _m4 and the _m6 rows alone,
// so a caller can skip the rest of the record (rf_hit_shade's early-out). uvw out (0 when the channel
// is off).
BL_INLINE bool bl_reaches(uint m4, float4 r0, float4 r1, float4 r2, uint channel, float3 P, BL_OUT(float3) uvw) {
  uvw = BL3(0.0f, 0.0f, 0.0f);
  if (((m4 & 16777215u) & (1u << (channel & 31u))) == 0u) return false;
  uvw = BL3(bl_dot4p(r0, P), bl_dot4p(r1, P), bl_dot4p(r2, P));
  return bl_inside(uvw);
}
// The box falloff x * y * z (f469db78 _1320.x * _1320.y * _1320.z): along z a ramp below ramp.x
// (slope ramp.z) or above ramp.y (slope ramp.w), the if/else of the game; across x/y the edge ramps.
BL_INLINE float bl_falloff(float3 uvw, float4 ramp, float2 edge, uint k) {
  float z = 1.0f;
  if (uvw.z < ramp.x) z = bl_curve(k, uvw.z * ramp.z);
  else if (uvw.z > ramp.y) z = bl_curve(k, (1.0f - uvw.z) * ramp.w);
  const float ex = (0.5f - abs(uvw.x - 0.5f)) * edge.x, ey = (0.5f - abs(uvw.y - 0.5f)) * edge.y;
  const float x = ex < 1.0f ? bl_curve(k, ex) : 1.0f;
  const float y = ey < 1.0f ? bl_curve(k, ey) : 1.0f;
  return (x * y) * z;
}

// Distance along a unit world direction from box point u (box-space direction dv = rows . dir)
// to where the ray leaves the box. The map is affine, so the world parameter is the box one.
// <= 0: already outside on that side.
BL_INLINE float bl_exit(float3 u, float3 dv) {
  float t = kBlFar;
  if (dv.x > 0.0f) t = min(t, (1.0f - u.x) / dv.x); else if (dv.x < 0.0f) t = min(t, (0.0f - u.x) / dv.x);
  if (dv.y > 0.0f) t = min(t, (1.0f - u.y) / dv.y); else if (dv.y < 0.0f) t = min(t, (0.0f - u.y) / dv.y);
  if (dv.z > 0.0f) t = min(t, (1.0f - u.z) / dv.z); else if (dv.z < 0.0f) t = min(t, (0.0f - u.z) / dv.z);
  return t;
}

// ls_box=2, a box light WITH a game shadow map: where the ray towards the light reaches its shadow
// view's near plane. The game compares the receiver's depth ref = ((z - dp.y) / dp.x) / dw (z the
// view row 2, linear: an orthographic view) against the atlas with "lit iff ref < stored", so depth
// grows away from the light and nothing nearer the light than ref = 0 can be in its map. `ref0` the
// receiver's ref, `slope` d ref / dt along the ray. kBlFar when the ray does not approach the plane.
BL_INLINE float bl_shadow_near(float ref0, float slope) {
  if (!(slope < 0.0f)) return kBlFar;
  return max(ref0, 0.0f) / -slope;
}

// ls_box=2, a box light WITHOUT a game shadow map: the light-side end of the segment that may shadow
// it. The box's own z ramp says where the light fades in from its source face: geometry inside that
// fade-in is the fixture that emits it (the main-menu skull at the near face of its beam), so the
// segment ends at the plane where the falloff along z reaches 1 (uvw.z = ramp.x on the z = 0 side,
// ramp.y on the z = 1 side). The ray's light side is the face it leaves through (sign of dvz). With
// no fade-in on that side (ramp.x <= 0, ramp.y >= 1) the segment runs to the face. <= 0: the
// receiver itself is inside the fade-in (not traced: the game's look). kBlFar when dvz is 0.
BL_INLINE float bl_fixture_end(float uz, float dvz, float4 ramp) {
  if (dvz < 0.0f) return (max(ramp.x, 0.0f) - uz) / dvz;
  if (dvz > 0.0f) return (min(ramp.y, 1.0f) - uz) / dvz;
  return kBlFar;
}

// The standard-class lobes of the box loop, per unit (colour x N.L x falloff x scale) and before the
// diffuse colour and F: Burley diffuse x 1/pi x mix(1, 0.6622, a) (f469db78 _1598), the GGX
// visibility (_1597's 0.5 / ...) and D (a^4 / pi / d^2). nolc, nov (>= 1e-5), loh, noh as the game.
struct BlLobes { float diff, vis, D, f5; };
BL_INLINE float bl_pow5(float x) { return bl_pow(max(abs(x), 9.9999997e-05f), 5.0f); }
BL_INLINE BlLobes bl_std_lobes(float nolc, float nov, float loh, float noh, float a) {
  BlLobes r;
  const float fd = (2.0f * loh * loh * a + lerp(0.0f, 0.5f, a)) - 1.0f;
  const float burley = (1.0f + fd * bl_pow5(1.0f - nolc)) * (1.0f + fd * bl_pow5(1.0f - nov));
  r.diff = 0.31830987f * (burley * lerp(1.0f, 0.66225165f, a));
  float k2 = (a + 1.0f) * 0.5f;
  k2 *= k2;
  r.vis = 0.5f / (nolc * sqrt((nov - nov * k2) * nov + k2) + nov * sqrt((nolc - nolc * k2) * nolc + k2));
  float a4 = a * a;
  a4 *= a4;
  const float dn = (noh * a4 - noh) * noh + 1.0f;
  r.D = a4 * (0.31830987f / (dn * dn));
  r.f5 = bl_pow5(1.0f - loh);
  return r;
}

#ifdef __cplusplus
}  // namespace bg3::box_light
#endif
