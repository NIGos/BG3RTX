// Dual scattering for the fibre BSDF (docs/RTX_HAIR_PRIMARY.md 3b; Zinke, Yuksel, Weber, Keyser 2008), written from
// the paper. The HLSL twin of tools/hair_groom/dual.py shade_table (CTest hair_primary_gpu_test compares them):
// fibre averages and the path-traced per-channel correction come from hair_dual_table.hlsli (generated).
// hd_shade(n_front = 0): a directly lit fibre, f_s + d_b f_back; n_front > 0 fibres between it and the light:
// T_f (f_s + pi d_b f_back), T_f = d_f a_f^n. The multiple-scattering part is scaled by k (path-traced slab).
#pragma once
#include "hair_bsdf.hlsli"
#include "hair_dual_table.hlsli"

static const float kHdDf = 0.7, kHdDb = 0.7;   // the paper's density factors

// Bilinear lookup over (log kHdC, kHdBeta), clamped: field f of an entry (0..5 the fibre averages, 6 the correction k).
float hd_at(uint i, uint j, uint f) { return kHdAvg[(i * 3u + j) * 7u + f]; }
float hd_lookup(float c, float beta, uint f) {
  c = clamp(c, kHdC[0], kHdC[7]);
  beta = clamp(beta, kHdBeta[0], kHdBeta[2]);
  uint i = 0u;
  [unroll] for (uint a = 0u; a < 6u; ++a) i = c > kHdC[a + 1u] ? a + 1u : i;
  const uint j = beta > kHdBeta[1] ? 1u : 0u;
  // Linear in log c (the averages are smooth there), linear in beta.
  const float u = (log(c) - log(kHdC[i])) / (log(kHdC[i + 1u]) - log(kHdC[i]));
  const float v = (beta - kHdBeta[j]) / (kHdBeta[j + 1u] - kHdBeta[j]);
  return (1.0 - u) * (1.0 - v) * hd_at(i, j, f) + u * (1.0 - v) * hd_at(i + 1u, j, f) +
         (1.0 - u) * v * hd_at(i, j + 1u, f) + u * v * hd_at(i + 1u, j + 1u, f);
}

float3 hd_lookup3(float3 c, float beta, uint f) {
  return float3(hd_lookup(c.x, beta, f), hd_lookup(c.y, beta, f), hd_lookup(c.z, beta, f));
}

float3 hd_gauss(float3 x, float3 var) { return exp(-x * x / (2.0 * var)) / sqrt(2.0 * kHbPi * var); }

// F(wi, wo) per unit irradiance for a fibre with colour `color` (the multiple-scattering albedo), roughness `beta`,
// offset h, and n_front fibres on the light ray in front of it.
float3 hd_shade_with(float3 f_s, float3 t, float3 wo, float3 wi, float n_front, float3 color, float beta);
float3 hd_shade(float3 t, float3 wo, float3 wi, float h, float n_front, float3 color, float beta) {
  const float rough = clamp(beta, 0.05, 1.0);
  return hd_shade_with(hb_eval(t, wo, wi, h, hb_sigma_a(color, rough), rough), t, wo, wi, n_front, color, beta);
}
// A pixel wider than the fibre: the single scattering averaged over four fibre offsets (hb_eval_pixel).
float3 hd_shade_pixel(float3 t, float3 wo, float3 wi, float n_front, float3 color, float beta) {
  const float rough = clamp(beta, 0.05, 1.0);
  return hd_shade_with(hb_eval_pixel(t, wo, wi, hb_sigma_a(color, rough), rough), t, wo, wi, n_front, color, beta);
}

// The dual-scattering response given the single-scattering term f_s at the chosen offset(s).
float3 hd_shade_with(float3 f_s, float3 t, float3 wo, float3 wi, float n_front, float3 color, float beta) {
  const float3 a_f = hd_lookup3(color, beta, 0u), a_b = hd_lookup3(color, beta, 1u);
  const float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
  const float alpha_f = hd_lookup(lum, beta, 2u), alpha_b = hd_lookup(lum, beta, 3u);
  const float beta_f = hd_lookup(lum, beta, 4u), beta_b = hd_lookup(lum, beta, 5u);
  // Backward multiple scattering (Zinke 2008 eq. 22-25).
  const float3 af2 = min(a_f * a_f, 0.999), q = 1.0 - af2;
  const float3 ab = a_b, ab3 = ab * ab * ab;
  const float3 A_b = ab * af2 / q + ab3 * af2 / (q * q * q);
  const float3 delta_b = alpha_b * (1.0 - 2.0 * ab * ab / (q * q)) + alpha_f * (2.0 * q * q + 4.0 * af2 * ab * ab) / (q * q * q);
  const float3 sigma_b = (1.0 + kHdDb * af2) *
                         (ab * sqrt(2.0 * beta_f * beta_f + beta_b * beta_b) + ab3 * sqrt(2.0 * beta_f * beta_f + 3.0 * beta_b * beta_b)) /
                         max(ab + ab3 * (2.0 * beta_f + 3.0 * beta_b), 1e-12);
  const float th_o = asin(clamp(dot(wo, t), -1.0, 1.0)), th_i = asin(clamp(dot(wi, t), -1.0, 1.0));
  const float th_h = 0.5 * (th_o + th_i), th_d = 0.5 * (th_o - th_i);
  const float3 var = n_front * beta_f * beta_f + sigma_b * sigma_b;
  const float cd = cos(th_d);
  const float3 f_back = 2.0 * A_b * hd_gauss(2.0 * th_h - delta_b, var) / (kHbPi * cd * cd);
  const bool direct = n_front <= 0.0;
  const float3 F = direct ? f_s + kHdDb * f_back : kHdDf * pow(a_f, n_front) * (f_s + kHbPi * kHdDb * f_back);
  const float3 single = direct ? f_s : 0.0;
  return single + hd_lookup3(color, beta, 6u) * (F - single);
}
