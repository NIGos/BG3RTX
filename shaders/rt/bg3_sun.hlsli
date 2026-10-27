// BG3's sun term, replicated from the tiled lighting (f469db78:1345-1452 class 0;
// the class variants from 4265b844, 118833ff, 19763259, 3033c895, 95af6e4f, 7f110c00),
// so that the D-LS post pass knows the sun's direct light exactly and can give
// it the AO-on-direct factor that ao_direct asks for (docs/AUDIT_RT.md fix 4).
//
// sun radiance = colour (per frame: _m2, or the atmosphere's transmittance LUT)
//              x mask (b7, after our RT fold) x cloud shadows (two layers) x b13[0].
// Needs gi_common.hlsli (cam = set 0 b1), bg3_lights.hlsli and bg3_materials.hlsli.
#pragma once
#include "bg3_materials.hlsli"

// The set these bindings live in: 2 in the D-LS layout; a pass whose set 2 is taken
// (D-RF: the game's lighting set) defines BG3_SUN_SET before including this file.
#ifndef BG3_SUN_SET
#define BG3_SUN_SET 2
#endif

struct AtmoCB { float4 a; };   // set 5 b0 (scalar): .x planet radius, .y atmosphere top radius
// Set 2 of the D-LS layout (an ordinary descriptor set, core/gi.cpp write_sun_set).
[[vk::binding(0, BG3_SUN_SET)]] ConstantBuffer<AtmoCB> lAtmo;
[[vk::binding(1, BG3_SUN_SET)]] Texture2D<float4> lTransLut;   // set 5 b1: transmittance LUT
[[vk::binding(2, BG3_SUN_SET)]] Texture2D<float4> lCloudA0;    // set 5 b4: cloud shadow layer A (two maps blended by _m16)
[[vk::binding(3, BG3_SUN_SET)]] Texture2D<float4> lCloudA1;    // set 5 b5
[[vk::binding(4, BG3_SUN_SET)]] Texture2D<float4> lCloudB0;    // set 1 b42: cloud shadow layer B
[[vk::binding(5, BG3_SUN_SET)]] Texture2D<float4> lCloudB1;    // set 1 b43
[[vk::binding(6, BG3_SUN_SET)]] SamplerState lLinearClamp;     // ours (the game's set 1 b16 is not readable)

// The camera CB (set 0 b1, std140) fields past _m7: cam.x[i] = bytes 160 + 16 i.
//   x[0] _m8, x[1] _m9, x[2] _m10-_m13, x[3] _m14-_m17, x[4] _m18, x[5] _m19-_m22, x[6] _m23,
//   x[7] _m24 (xyz) _m25 (w), x[8] _m26 _m27, x[9] _m28, x[10] _m29 _m30, x[11] _m31,
//   x[12] _m32 _m33, x[13] _m34 _m35, x[14] _m36 _m37.
float3 bg3_cam_pos() { return cam2.c[13].xyz; }   // set 0 b2 _m6 (std140 offset 208)

// The sun's colour this frame (f469db78:1347-1373).
float3 bg3_sun_colour() {
  if (cam.x[5].x > 0.0 && cam.x[13].w == 0.0) {
    float R = lAtmo.a.x, Rt = lAtmo.a.y;
    float3 c = float3(0.0, 636100.0, 0.0) + bg3_cam_pos();
    float l = length(c);
    if (l < 0.89999998 * R) {
      c.z += R;
      l = length(c);
    }
    float mu = dot(c / l, cam.x[14].xyz);
    float r = R / l;
    float2 uv = float2(atan((mu + 0.15000001) * 12.262105) * 0.66666669, sqrt((l - R) / (Rt - R)));
    float3 t = lTransLut.SampleLevel(lLinearClamp, uv, 0.0).xyz;
    if (mu < -sqrt(max(1.0 - r * r, 0.0))) t = 0.0;
    return t * (cam.x[13].xyz * cam.x[5].y);
  }
  return cam.v1.xyz;
}

float2 bg3_cloud_uv(float3 W, float4 p, float h, float3 dir, float3 shift) {
  float3 a = float3(0.0, p.z, 0.0) + ((shift + W) - float3(0.0, h, 0.0));
  float b = dot(a, dir);
  float2 uv = (((a + dir * (sqrt(b * b + (p.z * p.z - dot(a, a))) - b)).xz * p.w) * 0.5 + 0.5) + p.xy;
  uv.x = 1.0 - uv.x;
  return uv;
}

// Both cloud-shadow factors at lighting-space position P (f469db78:1384-1421).
float bg3_cloud_shadow(float3 P) {
  float3 W = bg3_cam_pos() + P;
  float f = 1.0;
  if (cam.x[5].z > 0.0) {   // _m21
    float2 u0 = bg3_cloud_uv(W, cam.x[4], cam.x[7].w, cam.x[7].xyz, 0.0.xxx);
    float2 u1 = bg3_cloud_uv(W, cam.x[6], cam.x[8].w, cam.x[8].xyz, 0.0.xxx);
    float v = lerp(lCloudA0.SampleLevel(lLinearClamp, u0, 0.0).x, lCloudA1.SampleLevel(lLinearClamp, u1, 0.0).x, cam.x[3].z);
    f *= 1.0 - saturate(smoothstep(0.050000001, 0.55000001, v) * cam.x[5].z);
  }
  if (cam.x[3].w > 0.0) {   // _m17
    float2 u0 = bg3_cloud_uv(W, cam.x[9], cam.x[10].w, cam.x[7].xyz, cam.x[10].xyz);
    float2 u1 = bg3_cloud_uv(W, cam.x[11], cam.x[12].w, cam.x[8].xyz, cam.x[12].xyz);
    float v = lerp(lCloudB0.SampleLevel(lLinearClamp, u0, 0.0).x, lCloudB1.SampleLevel(lLinearClamp, u1, 0.0).x, cam.x[3].z);
    f *= 1.0 - saturate(v * cam.x[3].w);
  }
  return f;
}

// The sun's direct diffuse `d` and specular `s` for the pixel's class, given the
// radiance `col` (colour x mask x clouds x b13[0]; S = mask x clouds), before the
// AO-on-direct factor.
// Transmission (skin with profiles that use the shadow, thin translucency) is in d;
// thickness profiles (m2.z == 1) take theirs from the cascades and are left out.
void bg3_sun_eval_mat(Bg3Surface sf, Bg3Mat m, float3 Ls, float3 col, float S, out float3 d, out float3 s) {
  d = 0;
  s = 0;
  const float nl_raw = dot(sf.N, Ls);
  if (m.cls == 5u) {   // hair: no disc, no N.L (3033c895:1739-1773)
    s = col * bg3_hair_spec(m, Ls, sf.V);
    d = col * bg3_hair_diff(m, nl_raw, dot(m.T, Ls), 1.0 - S * S);   // tint exponent 1 - S^2 (3033c895:1772)
    return;
  }
  const float a = sf.a;
  const float nols = saturate(nl_raw);
  float lohs = saturate(dot(Ls, normalize(sf.V + Ls)));
  float fd = (2.0 * lohs * lohs * a + lerp(0.0, 0.5, a)) - 1.0;
  float burley = (1.0 + fd * bg3_pow5(1.0 - nols)) * (1.0 + fd * bg3_pow5(1.0 - sf.nov));
  d = col * (sf.diff_col * (0.31830987 * (burley * lerp(1.0, 0.66225165, a)))) * nols;
  if ((m.cls == 3u || m.cls == 4u) && m.trans && !m.thick_sm)
    d += bg3_sss_T(m, m.base_thick) * (col * bg3_sss_wrap(nl_raw)) * (sf.diff_col * 0.31830987);
  // Specular: the representative point of the sun's disc, as the game builds it
  // (reflect(V, N), sic; f469db78:1424-1428).
  float3 R = reflect(sf.V, sf.N);
  float rl = dot(Ls, R);
  float3 Lp = rl < 0.99998945 ? normalize(Ls * 0.99998945 + normalize(R - Ls * rl) * 0.0045900648) : R;
  float3 Hp = normalize(sf.V + Lp);
  float nohp = saturate(dot(sf.N, Hp)), nolp = saturate(dot(sf.N, Lp)), lohp = saturate(dot(Lp, Hp));
  float3 ggx = 0;
  if (m.cls != 6u) {
    float k2 = (a + 1.0) * 0.5;
    k2 *= k2;
    float3 F = sf.f0 + (1.0 - sf.f0) * bg3_pow5(1.0 - lohp);
    float vis = 0.5 / (nolp * sqrt((sf.nov - sf.nov * k2) * sf.nov + k2) + sf.nov * sqrt((nolp - nolp * k2) * nolp + k2));
    float D = m.cls == 3u ? bg3_sss_D(nohp, a, m) : m.cls == 2u ? bg3_aniso_D(nohp, dot(Hp, m.T), a, m.aniso) : bg3_ggx_D(nohp, a);
    ggx = F * (vis * D);
  }
  if (m.cls == 6u || m.cls == 7u) {   // x N.L' (95af6e4f:1521, 7f110c00:2873)
    float3 sheen = m.rt3.rgb * bg3_sheen(nohp, nolp, sf.nov, a);
    s = col * (m.cls == 7u ? ggx * m.rt3.w + sheen * (1.0 - m.rt3.w) : sheen) * nolp;
  } else {
    s = col * ggx * nols;
  }
}
