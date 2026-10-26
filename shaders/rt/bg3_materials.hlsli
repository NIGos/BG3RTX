// BG3's per-material-class local-light shading (RT2.a bits 0-2, gi_common.hlsli
// kMat*), replicated from the lighting permutations of captures/20261026-094651:
//   class 0 f469db78 (bg3_lights.hlsli), 2 4265b844 (anisotropic GGX),
//   3 118833ff (skin: dual-lobe GGX, diffuse colour 1, transmission),
//   4 19763259 (thin translucency), 5 3033c895 (Marschner hair),
//   6 95af6e4f (sheen cloth), 7 7f110c00 (GGX/sheen blend); 1 is skipped by the
//   lighting (702bda11). The mixed-tile permutations (953fb057, 8b781b6e, 4f4077b6,
//   49f89784, cb6d23ac, 7f110c00, 34a007cc) branch per pixel to the same terms.
// Used by rt_local_shadows.hlsl to get each light's exact unshadowed contribution.
#pragma once
#include "bg3_lights.hlsli"

[[vk::binding(41, 0)]] Texture2D<float4> gRT3;            // set 1 b4: class parameters
[[vk::binding(42, 0)]] ByteAddressBuffer lSssProfiles;    // set 1 b28: SSS profiles, std430 vec4 m0..m3 (64 B)

struct Bg3Mat {
  uint cls;
  float4 rt3;
  // skin (3) and thin translucency (4)
  bool trans;       // a transmission lobe (3: RT3.w != 0; 4: always)
  bool thick_sm;    // profile m2.z == 1: thickness from the shadow map instead of the shadow
  bool dual;        // skin: two GGX lobes
  float r0, r1, lobe_mix, metal;
  float3 inv_sc, tint;
  float base_thick;
  // anisotropic (2) and hair (5)
  float3 T;
  float aniso;
  float hair_rough, backlit, scatter;
  float3 albedo;
};

// 12:12 octahedral vector in RT3.xyz (4265b844:349, 3033c895:344).
float3 bg3_oct12(float3 rt3) {
  float3 b = rt3 * 255.0;
  float h = b.y * 0.0625;
  float2 e = clamp(float2(b.x * 16.0 + floor(h), frac(h) * 4096.0 + b.z) * 0.00048851978 - 1.0, -1.0, 1.0);
  float z = (1.0 - abs(e.x)) - abs(e.y);
  float2 sg = float2(e.x >= 0.0 ? 1.0 : -1.0, e.y >= 0.0 ? 1.0 : -1.0);
  float2 xy = z >= 0.0 ? e : (1.0 - abs(e.yx)) * sg;
  return normalize(float3(xy, z));
}

// Per-class inputs and the surface terms that differ from class 0. `sf` arrives set up
// as for class 0 (diff_col = albedo (1 - metal), f0, a with specular AA). `nrot`: the
// _m6 rotation the normal got (anisotropic tangents get it, hair tangents do not).
Bg3Mat bg3_mat_setup(uint cls, float3 albedo, float4 mat, float4 rt3, bool skin_white, float3x3 nrot,
                     inout Bg3Surface sf) {
  Bg3Mat m = (Bg3Mat)0;
  m.cls = cls;
  m.rt3 = rt3;
  m.albedo = albedo;
  if (cls == 3u || cls == 4u) {
    // The profile list's size varies with the area: never read past its end.
    uint bytes;
    lSssProfiles.GetDimensions(bytes);
    uint a = min(uint(int(rt3.x * 255.0)) * 64u, bytes >= 64u ? bytes - 64u : 0u);
    float4 p0 = asfloat(lSssProfiles.Load4(a)), p1 = asfloat(lSssProfiles.Load4(a + 16));
    float4 p2 = asfloat(lSssProfiles.Load4(a + 32)), p3 = asfloat(lSssProfiles.Load4(a + 48));
    float R0 = max(0.09, mat.x);   // before specular AA (118833ff:373)
    m.metal = mat.z;
    m.dual = cls == 3u;
    if (m.dual && skin_white) sf.diff_col = (1.0 - mat.z).xxx;   // 118833ff:371
    m.r0 = max(saturate(p3.x * R0), 0.09);
    m.r1 = max(saturate(p3.y * R0), 0.09);
    m.lobe_mix = p3.z;
    m.trans = m.dual ? rt3.w != 0.0 : true;
    m.thick_sm = p2.z == 1.0;
    m.inv_sc = 1.0 / max(p0.xyz * 0.09660940 * (p0.w * (m.dual ? rt3.y : 1.0)), 9.9999997e-05.xxx);
    m.tint = p1.xyz;
    m.base_thick = p2.y * rt3.z + p2.x;
  } else if (cls == 2u) {
    m.T = mul(bg3_oct12(rt3.xyz), nrot);   // rotated like the normal (4265b844:356)
    m.aniso = rt3.w;
  } else if (cls == 5u) {
    m.T = bg3_oct12(rt3.xyz);              // not rotated (3033c895:351), sic
    m.hair_rough = max(0.09, mat.x);       // no specular AA
    m.backlit = mat.z;
    m.scatter = rt3.w;
    sf.diff_col = albedo;                  // metalness forced to 0
  } else if (cls == 6u) {
    sf.diff_col = albedo;                  // raw albedo, RT2.y/z unused (95af6e4f:340)
  }
  return m;
}

// ---- lobes ----
float bg3_ggx_D(float noh, float a) {
  float a4 = a * a;
  a4 *= a4;
  float dn = (noh * a4 - noh) * noh + 1.0;
  return a4 * (0.31830987 / (dn * dn));
}

// Skin: two GGX lobes from the profile, roughness lerped to the AA'd one by metalness (118833ff:625).
float bg3_sss_D(float noh, float a, Bg3Mat m) {
  float a2 = a * a;
  float l0 = lerp(m.r0 * m.r0, a2, m.metal), l1 = lerp(m.r1 * m.r1, a2, m.metal);
  float q0 = l0 * l0, q1 = l1 * l1;
  float d0 = (noh * q0 - noh) * noh + 1.0, d1 = (noh * q1 - noh) * noh + 1.0;
  return lerp(q0 * (0.31830987 / (d0 * d0)), q1 * (0.31830987 / (d1 * d1)), m.lobe_mix);
}

// Anisotropic GGX (4265b844:548).
float bg3_aniso_D(float noh, float toh, float a, float aniso) {
  float a2 = a * a;
  float s = sqrt(1.0 - aniso * 0.89999998);
  float ax = max(0.001, saturate(a2 / s));
  float ay = max(0.001, a2 * s);
  float q = noh * noh + ((toh * toh) / (ax * ax) + ((1.0 - toh * toh) - noh * noh) / (ay * ay));
  return 1.0 / (((ax * ay) * 3.1415927) * q * q);
}

// Charlie sheen D x Neubelt visibility (95af6e4f:527).
float bg3_sheen(float noh, float nol, float nov, float a) {
  float D = ((2.0 + 1.0 / a) * pow(max(abs(1.0 - noh * noh), 9.9999997e-05), 0.5 / a)) * 0.15915494;
  float nv = saturate(nov);
  return D * (0.25 / (nol + nv - nol * nv));
}

// Skin/translucency transmittance through `thick` (118833ff:580).
float3 bg3_sss_T(Bg3Mat m, float thick) {
  float3 e = exp2(m.inv_sc * (-0.48089835 * thick));
  return m.tint * (e * (e * e + 3.0));
}
float bg3_sss_wrap(float nol_raw) { return saturate((0.25881907 - nol_raw) * 0.63106394); }

// UE4-style Marschner R + TT + TRT (3033c895:515).
float3 bg3_hair_spec(Bg3Mat h, float3 L, float3 V) {
  float sl = clamp(dot(h.T, L), -1.0, 1.0), sv = clamp(dot(h.T, V), -1.0, 1.0);
  float ctd = cos(0.5 * abs(asin(sv) - asin(sl)));
  float3 lp = L - h.T * sl, vp = V - h.T * sv;
  float cphi = dot(lp, vp) * rsqrt(dot(lp, lp) * dot(vp, vp) + 9.9999997e-05);
  float chp = sqrt(saturate(0.5 * cphi + 0.5));
  float r2 = h.hair_rough * h.hair_rough;
  float3 B = float3(r2 * (1.4142135 * chp), r2 * 0.5, r2 * 2.0);
  float3 sh = float3(-0.13988569 * (0.99755102 * chp * sqrt(1.0 - sv * sv) + -0.069942847 * sv), 0.035, 0.14);
  float3 t = max(abs((sl + sv).xxx - sh), 9.9999997e-05);
  float3 Mp = exp((t * t) * -0.5 / (B * B)) / (B * 2.5066283);
  float vol = dot(V, L);
  float fR = 0.046520568 + 0.95347941 * bg3_pow5(1.0 - sqrt(saturate(0.5 * vol + 0.5)));
  float R = 0.25 * chp * fR * lerp(1.0, h.backlit, saturate(-vol)) * Mp.x;
  float ia = 1.0 / (0.36 * ctd + 1.19 / ctd);
  float hh = chp * (ia * (-0.8 * cphi + 0.6) + 1.0);
  float3 base = max(abs(h.albedo), 9.9999997e-05);
  float ft = 0.046520568 + 0.95347941 * bg3_pow5(1.0 - ctd * sqrt(saturate(1.0 - hh * hh)));
  float hx = max(abs(hh * ia), 9.9999997e-05);
  float3 TT = pow(base, (0.5 * sqrt(1.0 - hx * hx) / ctd).xxx) *
              (exp(-3.65 * cphi - 3.98) * pow(max(abs(1.0 - ft), 9.9999997e-05), 2.0)) * h.backlit * Mp.y;
  float f3 = 0.046520568 + 0.95347941 * bg3_pow5(1.0 - 0.5 * ctd);
  float3 TRT = pow(base, (0.8 / ctd).xxx) * (exp(17.0 * cphi - 16.78) * (pow(max(abs(1.0 - f3), 9.9999997e-05), 2.0) * f3)) * Mp.z;
  return R.xxx + TT + TRT;
}

// Hair scatter diffuse (3033c895:517); texp = 1 - S.
float3 bg3_hair_diff(Bg3Mat h, float nl, float tl, float texp) {
  float k = 0.31830987 * lerp(saturate((nl + 1.0) * 0.25), 1.0 - abs(tl), 0.33000001) * h.scatter;
  float3 tint = pow(max(abs(h.albedo / dot(h.albedo, float3(0.30000001, 0.58999997, 0.11))), 9.9999997e-05), texp.xxx);
  return sqrt(h.albedo) * k * tint;
}

// One light's exact unshadowed contribution for the pixel's class, the counterpart of
// bg3_light_eval (identical for class 0).
//   d:  diffuse the light's shadow multiplies (reflected, plus the transmission of
//       profiles that use the shadow);
//   dt: transmission the shadow does not multiply (profiles that take the thickness
//       from the shadow map instead, m2.z == 1);
//   s:  specular. nol: the UNCLAMPED N.L (its sign says from which side the light comes).
// Returns false when the game adds nothing for this light.
bool bg3_light_eval_mat(Bg3Light l, bool spot, Bg3Surface sf, Bg3Mat m, out float3 d, out float3 dt, out float3 s,
                        out float3 Ld, out float d2, out float nol) {
  d = 0; dt = 0; s = 0; Ld = 0; d2 = 0; nol = 0;
  if ((l.chan & (1u << (sf.channel & 31u))) == 0u) return false;
  float3 L = l.pos - sf.P;
  d2 = dot(L, L);
  Ld = L * rsqrt(max(d2, 1.0000000e-10));
  float ir2 = l.inv_r * l.inv_r;
  if (!(d2 < 1.0 / ir2)) return false;
  float x = d2 * ir2;
  float att = pow(max(abs(saturate(1.0 - x * x)), 9.9999997e-05), l.m4);
  float bias = abs(l.m5);
  att *= l.m5 < 0.0 ? (4.0 * ir2 + bias) : (1.0 / max(d2, 9.9999997e-05) + bias);
  float g, norm;
  if (spot) {
    float c = dot(-Ld, normalize(l.dir));
    if (!(c > l.cos_outer)) return false;
    float t = saturate((c - l.cos_outer) / max(l.cos_inner - l.cos_outer, 9.9999997e-05));
    g = att * (t * t);
    norm = 0.31830987;
  } else {
    g = att;
    norm = 0.07957747;
  }
  const float sc = l.m3 < 0.0 ? asfloat(lScale.Load(0)) : 1.0;
  nol = dot(sf.N, Ld);

  if (m.cls == 5u) {   // hair: no N.L gate; self-shadow S in place of N.L for lights without a shadow map
    float S = saturate(2.0 * nol + 0.5);
    if (!(S > 0.0)) return false;
    float3 col = l.col * ((norm * g) * S * sc);
    s = col * bg3_hair_spec(m, Ld, sf.V);
    d = col * bg3_hair_diff(m, nol, dot(m.T, Ld), 1.0 - S);
    return true;
  }

  bool any_t = false;
  if ((m.cls == 3u || m.cls == 4u) && m.trans) {   // transmission, also for N.L <= 0 (118833ff:580-606)
    float w = bg3_sss_wrap(nol);
    if (w > 0.0) {
      float3 tk = l.col * sf.diff_col * (0.31830987 * (norm * (w * g)) * sc);
      float3 tt = tk * bg3_sss_T(m, m.base_thick);
      if (m.thick_sm) dt = tt;
      else d = tt;
      any_t = true;
    }
  }
  const float nolc = saturate(nol);
  if (!(nolc > 0.0)) return any_t;
  float3 col = l.col * ((norm * (nolc * g)) * sc);
  float3 H0 = sf.V + Ld;
  float3 H = H0 * rsqrt(max(dot(H0, H0), 1.0000000e-10));
  float loh = saturate(dot(Ld, H));
  float noh = saturate(dot(sf.N, H));
  float a = sf.a;
  float fd = (2.0 * loh * loh * a + lerp(0.0, 0.5, a)) - 1.0;
  float burley = (1.0 + fd * bg3_pow5(1.0 - nolc)) * (1.0 + fd * bg3_pow5(1.0 - sf.nov));
  d += col * (sf.diff_col * (0.31830987 * (burley * lerp(1.0, 0.66225165, a))));

  float3 ggx = 0;
  if (m.cls != 6u) {
    float k2 = (a + 1.0) * 0.5;
    k2 *= k2;
    float3 F = sf.f0 + (1.0 - sf.f0) * bg3_pow5(1.0 - loh);
    float vis = 0.5 / (nolc * sqrt((sf.nov - sf.nov * k2) * sf.nov + k2) + sf.nov * sqrt((nolc - nolc * k2) * nolc + k2));
    float D = m.cls == 3u ? bg3_sss_D(noh, a, m) : m.cls == 2u ? bg3_aniso_D(noh, dot(H, m.T), a, m.aniso) : bg3_ggx_D(noh, a);
    ggx = F * (vis * D);
  }
  if (m.cls == 6u || m.cls == 7u) {
    float3 sheen = m.rt3.rgb * bg3_sheen(noh, nolc, sf.nov, a);
    s = col * (m.cls == 7u ? ggx * m.rt3.w + sheen * (1.0 - m.rt3.w) : sheen);
  } else {
    s = col * ggx;
  }
  return true;
}
