// BG3's local-light list and its exact per-light shading, replicated from the
// tiled lighting compute shader f469db78 (captures/20261025-235042, see
// docs/TRACK_D_RT.md 8.1 and 9). The buffers are the lighting dispatch's own
// (set 1 of f469db78), re-bound by the core (core/gi.cpp, D-EN1).
//
// Offsets are the SPIR-V Offset/ArrayStride decorations of f469db78:
//   b25 light (struct _50, stride 80): m0 xyz position, w 1/radius; m1 xyz colour,
//       w spot cos-outer; m2 xyz spot direction, w cos-inner; 48 m3 (< 0: x global
//       scale b13[0]); 52 m4 falloff exponent; 56 m5 falloff bias (sign = model);
//       60 m6 light-channel mask; 64 m7 bit 0 casts a shadow, >> 2 atlas slot;
//       68 m8 shadow strength; 72 m9 ivec2.
//   b23 cluster (ivec4): x first index, y bits 0-11 points, 12-23 spots, 24-31
//       env probes; z bits 0-9 projector lights. b24 index list (int).
//   b40 shadow view (struct _62, stride 96, scalar): row-major mat4 at 0, vec2 atlas
//       rect origin at 64, float rect size at 72, 76 unused here, 80 normal bias,
//       vec2 depth params at 84, float at 92.
//   b39 lighting constants (std140, 544 bytes): 480 _m21/_m22 froxel log-z
//       scale/bias, 488 _m23/_m24 tiles x/y, 496 _m25 tile size (uint), 500 _m26
//       last z slice (uint), 528 _m33 atlas texel size, 536 _m34 shadow fade.
//   set 1 b0 (scalar UBO, _34): m0.x > 0 the AO texture is used, m0.y how much AO
//       multiplies direct light, m0.w > 0 the sun mask is used; 16 uvec2 resolution.
//   set 0 b2 (_27, std140): 196 _m4, the depth scale of the froxel slices.
#pragma once

struct LightFlagsCB { float4 m0; uint2 res; uint m2, m3; };
struct LightConstsCB { float4 c[34]; };
struct Cam2CB { float4 c[14]; };

[[vk::binding(21, 0)]] ConstantBuffer<LightFlagsCB> lFlags;
[[vk::binding(22, 0)]] ByteAddressBuffer lScale;     // b13: float[0] global light scale
[[vk::binding(23, 0)]] ByteAddressBuffer lClusters;  // b23
[[vk::binding(24, 0)]] ByteAddressBuffer lIndex;     // b24
[[vk::binding(25, 0)]] ByteAddressBuffer lLights;    // b25
[[vk::binding(26, 0)]] ConstantBuffer<LightConstsCB> lConsts;   // b39
[[vk::binding(27, 0)]] ByteAddressBuffer lShadows;   // b40
[[vk::binding(28, 0)]] Texture2D<float> lAtlas;      // b41
[[vk::binding(29, 0)]] ConstantBuffer<Cam2CB> cam2;  // set 0 b2

struct Bg3Light {
  float3 pos; float inv_r;
  float3 col; float cos_outer;
  float3 dir; float cos_inner;
  float m3, m4, m5;
  uint chan, flags;
  float strength;
};

Bg3Light load_light(uint i) {
  uint a = i * 80u;
  Bg3Light l;
  float4 v0 = asfloat(lLights.Load4(a)), v1 = asfloat(lLights.Load4(a + 16)), v2 = asfloat(lLights.Load4(a + 32));
  uint4 v3 = lLights.Load4(a + 48);
  l.pos = v0.xyz; l.inv_r = v0.w;
  l.col = v1.xyz; l.cos_outer = v1.w;
  l.dir = v2.xyz; l.cos_inner = v2.w;
  l.m3 = asfloat(v3.x); l.m4 = asfloat(v3.y); l.m5 = asfloat(v3.z); l.chan = v3.w;
  l.flags = lLights.Load(a + 64);
  l.strength = asfloat(lLights.Load(a + 68));
  return l;
}

// The pixel's froxel: first index into b24, number of point and spot lights.
uint3 bg3_cluster(uint2 px, float depth) {
  uint tsize = max(asuint(lConsts.c[31].x), 1u);
  uint2 tile = px / tsize;
  float zmax = float(asuint(lConsts.c[31].y));
  float z = float(int(clamp(log2(depth * cam2.c[12].y) * lConsts.c[30].x + lConsts.c[30].y, 0.0, zmax)));
  uint idx = uint(int((z * lConsts.c[30].w + float(tile.y)) * lConsts.c[30].z + float(tile.x)));
  uint4 c = lClusters.Load4(idx * 16u);
  return uint3(c.x, c.y & 4095u, (c.y >> 12) & 4095u);
}

// The pixel's froxel record as stored (b23): x first index, y point/spot/env-probe
// counts, z projector count.
uint4 bg3_cluster_raw(uint2 px, float depth) {
  uint tsize = max(asuint(lConsts.c[31].x), 1u);
  uint2 tile = px / tsize;
  float zmax = float(asuint(lConsts.c[31].y));
  float z = float(int(clamp(log2(depth * cam2.c[12].y) * lConsts.c[30].x + lConsts.c[30].y, 0.0, zmax)));
  uint idx = uint(int((z * lConsts.c[30].w + float(tile.y)) * lConsts.c[30].z + float(tile.x)));
  return lClusters.Load4(idx * 16u);
}

uint bg3_cluster_projectors(uint2 px, float depth) {
  uint tsize = max(asuint(lConsts.c[31].x), 1u);
  uint2 tile = px / tsize;
  float zmax = float(asuint(lConsts.c[31].y));
  float z = float(int(clamp(log2(depth * cam2.c[12].y) * lConsts.c[30].x + lConsts.c[30].y, 0.0, zmax)));
  uint idx = uint(int((z * lConsts.c[30].w + float(tile.y)) * lConsts.c[30].z + float(tile.x)));
  return lClusters.Load4(idx * 16u).z & 1023u;
}

// Surface terms of f469db78 at one pixel.
struct Bg3Surface {
  float3 P, N, V;
  float3 diff_col, f0;
  float a;        // roughness after the game's clamp and specular AA (_515)
  float nov_raw;  // _580
  float nov;      // _581
  uint channel;
  float ign;      // interleaved gradient noise of the atlas PCF (_556)
  float dist;
  float4 org;     // rt_origin_at(): this pixel's ray-origin data (rt_common.hlsli)
};

float bg3_pow5(float x) { return pow(max(abs(x), 9.9999997e-05), 5.0); }

// One light's unshadowed contribution: diffuse in .xyz of `d`, specular in `s`.
// Returns false when the game would not evaluate it (channel, range, cone, N.L).
// `k` is the light's scalar factor before the shadow, `nol` its N.L.
bool bg3_light_eval(Bg3Light l, bool spot, Bg3Surface sf, out float3 d, out float3 s, out float3 Ld,
                    out float d2, out float nol) {
  d = 0; s = 0; Ld = 0; d2 = 0; nol = 0;
  if ((l.chan & (1u << (sf.channel & 31u))) == 0u) return false;
  float3 L = l.pos - sf.P;
  d2 = dot(L, L);
  Ld = L * rsqrt(max(d2, 1.0000000e-10));
  float ir2 = l.inv_r * l.inv_r;
  if (!(d2 < 1.0 / ir2)) return false;
  float3 H0 = sf.V + Ld;
  float3 H = H0 * rsqrt(max(dot(H0, H0), 1.0000000e-10));
  float loh = saturate(dot(Ld, H));
  float noh = saturate(dot(sf.N, H));
  nol = saturate(dot(sf.N, Ld));
  float x = d2 * ir2;
  float att = pow(max(abs(saturate(1.0 - x * x)), 9.9999997e-05), l.m4);
  float bias = abs(l.m5);
  att *= l.m5 < 0.0 ? (4.0 * ir2 + bias) : (1.0 / max(d2, 9.9999997e-05) + bias);
  float k;
  if (spot) {
    float c = dot(-Ld, normalize(l.dir));
    if (!(c > l.cos_outer)) return false;
    float t = saturate((c - l.cos_outer) / max(l.cos_inner - l.cos_outer, 9.9999997e-05));
    k = 0.31830987 * (nol * (att * (t * t)));
  } else {
    k = 0.07957747 * (nol * att);
  }
  if (!(nol > 0.0)) return false;
  float3 col = l.col * (k * (l.m3 < 0.0 ? asfloat(lScale.Load(0)) : 1.0));
  float a = sf.a;
  float3 fd = (2.0 * loh * loh * a + lerp(0.0, 0.5, a)).xxx - 1.0;
  float k2 = (a + 1.0) * 0.5;
  k2 *= k2;
  float a4 = a * a;
  a4 *= a4;
  float dn = (noh * a4 - noh) * noh + 1.0;
  float3 F = sf.f0 + (1.0 - sf.f0) * bg3_pow5(1.0 - loh);
  float vis = 0.5 / (nol * sqrt((sf.nov - sf.nov * k2) * sf.nov + k2) + sf.nov * sqrt((nol - nol * k2) * nol + k2));
  s = col * (F * (vis * (a4 * (0.31830987 / (dn * dn)))));
  float burley = (1.0 + fd.x * bg3_pow5(1.0 - nol)) * (1.0 + fd.x * bg3_pow5(1.0 - sf.nov));
  d = col * (sf.diff_col * (0.31830987 * (burley * lerp(1.0, 0.66225165, a))));
  return true;
}

// The game's atlas shadow term for a light with m7 bit 0: tetrahedral face (points)
// or the single view (spots), 12-tap Vogel-disc PCF rotated by IGN, faded by the
// camera distance (b39 _m34) and the light's strength m8. 1 = lit.
float bg3_atlas_shadow(Bg3Light l, bool spot, Bg3Surface sf, float3 Ld, float d2, float nol) {
  uint slot = l.flags >> 2;
  if (!spot) {
    float3 m = -Ld;
    float f0 = dot(m, float3(0.0, -0.57735026, 0.81649661));
    float f1 = dot(m, float3(0.0, -0.57735026, -0.81649661));
    float f2 = dot(m, float3(-0.81649661, 0.57735026, 0.0));
    float f3 = dot(m, float3(0.81649661, 0.57735026, 0.0));
    float mx = max(max(f0, f1), max(f2, f3));
    slot += mx == f0 ? 0u : mx == f1 ? 1u : mx == f2 ? 2u : 3u;
  }
  float pcf = 0.0;
  if (nol > 0.0) {
    uint a = slot * 96u;
    float4 r0 = asfloat(lShadows.Load4(a)), r1 = asfloat(lShadows.Load4(a + 16));
    float4 r2 = asfloat(lShadows.Load4(a + 32)), r3 = asfloat(lShadows.Load4(a + 48));
    float2 org = asfloat(lShadows.Load2(a + 64));
    float size = asfloat(lShadows.Load(a + 72));
    float nb = asfloat(lShadows.Load(a + 80));
    float2 dp = asfloat(lShadows.Load2(a + 84));
    float dw = asfloat(lShadows.Load(a + 92));
    float4 q = float4(sf.P + sf.N * ((3.5 * sign(nol)) * (nb * sqrt(d2))), 1.0);
    float4 c = float4(dot(r0, q), dot(r1, q), dot(r2, q), dot(r3, q));
    float3 p = c.xyz / c.w;
    float2 uv = p.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    float ref = (dp.y / dw) / (p.z - dp.x);
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
      // textureGather: x (i0, j1), y (i1, j1), z (i1, j0), w (i0, j0)
      int2 b = int2(floor(u / texel - 0.5));
      float g00 = lAtlas.Load(int3(clamp(b, 0, amax), 0));
      float g10 = lAtlas.Load(int3(clamp(b + int2(1, 0), 0, amax), 0));
      float g01 = lAtlas.Load(int3(clamp(b + int2(0, 1), 0, amax), 0));
      float g11 = lAtlas.Load(int3(clamp(b + int2(1, 1), 0, amax), 0));
      float4 cmp = float4(ref < g01, ref < g11, ref < g10, ref < g00);
      sum += saturate(lerp(lerp(cmp.w, cmp.z, f.x), lerp(cmp.x, cmp.y, f.x), f.y));
    }
    pcf = sum * 0.083333336;
  }
  float fade = saturate(lConsts.c[33].w * length(sf.P) + lConsts.c[33].z);
  return lerp(1.0, pcf, min(fade, l.strength));
}
