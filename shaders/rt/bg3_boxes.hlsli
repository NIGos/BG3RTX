// BG3's box ("projector") lights: the third light loop of the tiled lighting f469db78
// (captures/20261026-165848/shaders/f469db7865d7c524.glsl lines 735-930; the class
// permutations 3033c895 / 118833ff evaluate them with their own terms, as the point/spot
// loop). docs/TRACK_D_RT.md 9.12. Used by rt_local_shadows.hlsl when LS_BOX=1.
//
// A box light is a directional light inside an oriented box. Its record is set 1 b29,
// struct _53, stride 128; the offsets are the SPIR-V Offset decorations of f469db78:
//   0 _m0 vec3 colour; 12 _m1 uint (bit 0: x b13[0] global light scale; >> 1: cookie
//   index + 1 into the bindless b45, 0 = none); 16 _m2 vec4 z ramp (x start, y end,
//   z/w slopes); 32 _m3 vec2 x/y edge slopes; 40 _m4 uint (bits 0-23 light channels,
//   24-31 ramp curve); 44 _m5 float shadow strength; 48 _m6 mat4x3 row-major (3 rows of
//   vec4, MatrixStride 16) world -> box; 96 _m7 vec3 (unused by the lighting);
//   108 _m8 uint (bit 0 casts a shadow, >> 2 atlas slot); 112 _m9 vec3 direction TO the
//   light; 124 _m10 float (unused by the lighting).
// These bindings live in the lighting's own set 1, bound as our set 3 (core/gi.cpp ls_box), or
// BG3_BOX_SET (rf_hit_shade: set 2, rt/native_hit_direct.hlsli NATIVE_HIT_BOXES).
#pragma once
#include "bg3_materials.hlsli"
#include "../../common/box_light.h"   // the shared math (tests/box_light_test.cpp: against the game's loop)

// BG3_BOX_SET: the set the lighting's set 1 is bound as (3 in D-LS; rf_hit_shade binds it as 2).
#ifndef BG3_BOX_SET
#define BG3_BOX_SET 3
#endif
[[vk::binding(29, BG3_BOX_SET)]] ByteAddressBuffer lBoxes;        // b29: box lights (struct _53)
[[vk::binding(16, BG3_BOX_SET)]] SamplerState lBoxCookieSmp;      // b16 (_7): the cookies' sampler
[[vk::binding(45, BG3_BOX_SET)]] Texture2D<float4> lBoxCookies[]; // b45: bindless cookies (_25)

static const uint kBoxStride = 128u;

struct Bg3Box {
  float3 col;
  uint m1;
  float4 ramp;
  float2 edge;
  uint m4;
  float strength;
  float4 r0, r1, r2;   // world -> box rows
  uint flags;          // _m8
  float3 dir;          // _m9, towards the light
};

bool bg3_box_in(uint bi) { return bg3_in(lBoxes, bi, kBoxStride); }

// The record (no light when out of the buffer: no channel).
Bg3Box load_box(uint bi) {
  Bg3Box b = (Bg3Box)0;
  if (!bg3_box_in(bi)) return b;
  const uint a = bi * kBoxStride;
  float4 v0 = asfloat(lBoxes.Load4(a));
  b.col = v0.xyz;
  b.m1 = asuint(v0.w);
  b.ramp = asfloat(lBoxes.Load4(a + 16));
  uint4 v2 = lBoxes.Load4(a + 32);
  b.edge = asfloat(v2.xy);
  b.m4 = v2.z;
  b.strength = asfloat(v2.w);
  b.r0 = asfloat(lBoxes.Load4(a + 48));
  b.r1 = asfloat(lBoxes.Load4(a + 64));
  b.r2 = asfloat(lBoxes.Load4(a + 80));
  b.flags = lBoxes.Load(a + 108);
  b.dir = asfloat(lBoxes.Load3(a + 112));
  return b;
}

float3 bg3_box_uvw(Bg3Box b, float3 P) {
  float4 q = float4(P, 1.0);
  return float3(dot(b.r0, q), dot(b.r1, q), dot(b.r2, q));
}
// f469db78: lit only strictly inside the box.
bool bg3_box_inside(float3 uvw) { return bl_inside(uvw); }
// The box falloff x * y * z (f469db78 _1320.x * _1320.y * _1320.z; common/box_light.h bl_falloff).
float bg3_box_falloff(Bg3Box b, float3 uvw) { return bl_falloff(uvw, b.ramp, b.edge, b.m4 >> 24); }

// The light's colour at uv: _m0, times the cookie when it has one. `duv_x`, `duv_y`: the
// uv differences across the pixel's 2x2 quad (the game's quad swaps), zeroed as the game
// does when either is non-finite or any component exceeds 1/64.
float3 bg3_box_colour(Bg3Box b, float2 uv, float2 duv_x, float2 duv_y) {
  const int ci = int(b.m1 >> 1) - 1;
  if (ci < 0) return b.col;
  float2 gx = any(isnan(duv_x)) ? float2(0, 0) : duv_x;
  float2 gy = any(isnan(duv_y)) ? float2(0, 0) : duv_y;
  if (any(abs(float4(gx, gy)) > 0.015625)) {
    gx = 0;
    gy = 0;
  }
  return b.col * lBoxCookies[NonUniformResourceIndex(uint(ci))].SampleGrad(lBoxCookieSmp, uv, gx, gy).xyz;
}

// Is the surface lit by the box at all (channel, inside the box)? uvw out.
bool bg3_box_reaches(Bg3Box b, uint channel, float3 P, out float3 uvw) {
  return bl_reaches(b.m4, b.r0, b.r1, b.r2, channel, P, uvw);
}
// The same test from record bi's four words (_m4 and the _m6 rows) without loading the rest: the
// rf_hit_shade early-out (native_hit_direct.hlsli). The caller checks bg3_box_in(bi).
bool bg3_box_reaches_rec(uint bi, uint channel, float3 P, out float3 uvw) {
  const uint a = bi * kBoxStride;
  return bl_reaches(lBoxes.Load(a + 40), asfloat(lBoxes.Load4(a + 48)), asfloat(lBoxes.Load4(a + 64)),
                    asfloat(lBoxes.Load4(a + 80)), channel, P, uvw);
}

// One box light's exact unshadowed contribution for the pixel's class: bg3_light_eval_mat
// with the direction _m9 in place of the light vector, the box falloff in place of the
// range/cone falloff, no 1/pi or 1/4pi normalisation, and `col` the (cookie) colour.
// d / dt / s / nol as there. Returns false when the game adds nothing for this light.
bool bg3_box_eval_mat(Bg3Box b, float3 col_in, float g, Bg3Surface sf, Bg3Mat m, out float3 d, out float3 dt,
                      out float3 s, out float nol) {
  d = 0; dt = 0; s = 0; nol = 0;
  const float3 Ld = b.dir;
  const float sc = (b.m1 & 1u) != 0u ? asfloat(lScale.Load(0)) : 1.0;
  nol = dot(sf.N, Ld);
  if (m.cls == 5u) {   // hair: no N.L gate; self-shadow S in place of N.L for lights without a shadow map
    float S = saturate(2.0 * nol + 0.5);
    if (!(S > 0.0)) return false;
    float3 col = col_in * (g * S * sc);
    s = col * bg3_hair_spec(m, Ld, sf.V);
    d = col * bg3_hair_diff(m, nol, dot(m.T, Ld), 1.0 - S);
    return true;
  }
  bool any_t = false;
  if ((m.cls == 3u || m.cls == 4u) && m.trans) {   // transmission, also for N.L <= 0
    float w = bg3_sss_wrap(nol);
    if (w > 0.0) {
      float3 tk = col_in * sf.diff_col * (0.31830987 * (w * g) * sc);
      float3 tt = tk * bg3_sss_T(m, m.base_thick);
      if (m.thick_sm) dt = tt;
      else d = tt;
      any_t = true;
    }
  }
  const float nolc = saturate(nol);
  if (!(nolc > 0.0)) return any_t;
  float3 col = col_in * ((nolc * g) * sc);
  float3 H0 = sf.V + Ld;
  float3 H = H0 * rsqrt(max(dot(H0, H0), 1.0000000e-10));
  float loh = saturate(dot(Ld, H));
  float noh = saturate(dot(sf.N, H));
  float a = sf.a;
  // The standard lobes (common/box_light.h bl_std_lobes, checked against f469db78 by box_light_test).
  const BlLobes lb = bl_std_lobes(nolc, sf.nov, loh, noh, a);
  d += col * (sf.diff_col * lb.diff);
  float3 ggx = 0;
  if (m.cls != 6u) {
    float3 F = sf.f0 + (1.0 - sf.f0) * lb.f5;
    float D = m.cls == 3u ? bg3_sss_D(noh, a, m) : m.cls == 2u ? bg3_aniso_D(noh, dot(H, m.T), a, m.aniso) : lb.D;
    ggx = F * (lb.vis * D);
  }
  if (m.cls == 6u || m.cls == 7u) {
    float3 sheen = m.rt3.rgb * bg3_sheen(noh, nolc, sf.nov, a);
    s = col * (m.cls == 7u ? ggx * m.rt3.w + sheen * (1.0 - m.rt3.w) : sheen);
  } else {
    s = col * ggx;
  }
  return true;
}

// The game's atlas term for a box light with _m8 bit 0 (f469db78 lines 818-858): an
// orthographic view, normal bias 3.5 x _m4 of the view (no distance factor), depth
// ((z - _m5.y) / _m5.x) / _m6, the same 12-tap Vogel PCF as spots, x min(fade, _m5).
float bg3_box_atlas_shadow(Bg3Box b, Bg3Surface sf, float nolc) {
  const uint slot = b.flags >> 2;
  if (!bg3_in(lShadows, slot, 96u)) return 1.0;   // no shadow view: as a light without a shadow map
  float pcf = 0.0;
  if (nolc > 0.0) {
    uint a = slot * 96u;
    float4 r0 = asfloat(lShadows.Load4(a)), r1 = asfloat(lShadows.Load4(a + 16));
    float4 r2 = asfloat(lShadows.Load4(a + 32));
    float2 org = asfloat(lShadows.Load2(a + 64));
    float size = asfloat(lShadows.Load(a + 72));
    float nb = asfloat(lShadows.Load(a + 80));
    float2 dp = asfloat(lShadows.Load2(a + 84));
    float dw = asfloat(lShadows.Load(a + 92));
    float4 q = float4(sf.P + sf.N * ((3.5 * sign(nolc)) * nb), 1.0);
    float3 c = float3(dot(r0, q), dot(r1, q), dot(r2, q));
    float2 uv = c.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    float ref = ((c.z - dp.y) / dp.x) / dw;
    float2 texel = lConsts.c[33].xy;
    float rot = sf.ign * 6.2831855;
    float2 lo = org + texel, hi = org + size.xx - texel;
    uint aw, ah;
    lAtlas.GetDimensions(aw, ah);
    int2 amax = int2(aw, ah) - 1;
    float sum = 0.0;
    [loop] for (int i = 0; i < 12; ++i) {
      float fi = float(i);
      float r = sqrt(fi + 0.5) * 0.28867513;
      float ang = fi * 2.3999631 + rot;
      float2 u = clamp(float2(cos(ang), sin(ang)) * r * 2.5 * texel + uv, lo, hi);
      float2 f = frac(u / texel + 0.5);
      int2 bb = int2(floor(u / texel - 0.5));
      float g00 = lAtlas.Load(int3(clamp(bb, 0, amax), 0));
      float g10 = lAtlas.Load(int3(clamp(bb + int2(1, 0), 0, amax), 0));
      float g01 = lAtlas.Load(int3(clamp(bb + int2(0, 1), 0, amax), 0));
      float g11 = lAtlas.Load(int3(clamp(bb + int2(1, 1), 0, amax), 0));
      float4 cmp = float4(ref < g01, ref < g11, ref < g10, ref < g00);
      sum += saturate(lerp(lerp(cmp.w, cmp.z, f.x), lerp(cmp.x, cmp.y, f.x), f.y));
    }
    pcf = sum * 0.083333336;
  }
  float fade = saturate(lConsts.c[33].w * length(sf.P) + lConsts.c[33].z);
  return lerp(1.0, pcf, min(fade, b.strength));
}

// Distance along `dir` (unit, world) from `o` to where the ray leaves the box (the box is
// the light's extent: casters beyond its light-side face are outside the light). The box
// map is affine, so the world parameter is the box-space one. <= 0: already outside.
float bg3_box_exit(Bg3Box b, float3 o, float3 dir) {
  const float3 dv = float3(dot(b.r0.xyz, dir), dot(b.r1.xyz, dir), dot(b.r2.xyz, dir));
  return bl_exit(bg3_box_uvw(b, o), dv);
}

// ls_box=2, a box light with a game shadow map (_m8 bit 0): the distance along `dir` from `o` to
// its shadow view's near plane (common/box_light.h bl_shadow_near): nothing nearer the light can be in
// the game's map, so the traced segment that replaces the atlas term covers the same space.
float bg3_box_shadow_near(Bg3Box b, float3 o, float3 dir) {
  const uint slot = b.flags >> 2;
  if (!bg3_in(lShadows, slot, 96u)) return kBlFar;
  const uint a = slot * 96u;
  const float4 r2 = asfloat(lShadows.Load4(a + 32));
  const float2 dp = asfloat(lShadows.Load2(a + 84));
  const float dw = asfloat(lShadows.Load(a + 92));
  const float ref0 = ((dot(r2, float4(o, 1.0)) - dp.y) / dp.x) / dw;
  const float slope = (dot(r2.xyz, dir) / dp.x) / dw;
  const float t = bl_shadow_near(ref0, slope);
  return isfinite(t) ? t : kBlFar;
}
// ls_box=2, a box light without one: the end of the segment that may shadow it, the light-side
// plane where its z fade-in ends (common/box_light.h bl_fixture_end; <= 0: the receiver is in it).
float bg3_box_fixture_end(Bg3Box b, float3 o, float3 dir) {
  return bl_fixture_end(bg3_box_uvw(b, o).z, dot(b.r2.xyz, dir), b.ramp);
}
