// Physically based hair fibre BSDF (docs/RTX_HAIR_PRIMARY.md step 3, roadmap step 4): Chiang, Bitterli, Tappan, Burley
// 2016 (R, TT, TRT lobes and the p >= 3 residual; absorption from the colour by their eq. 9) with d'Eon et al. 2011's
// longitudinal function. Written from the papers; the HLSL twin of tools/hair_groom/bsdf.py `evaluate` (CTest
// hair_primary_gpu_test compares them on the GPU). No RTXCR or pbrt code.
// Frame: unit tangent t (root to tip), sin theta = w . t, phi the azimuth around t; h in [-1, 1] the offset across the
// fibre. S(wi, wo) per unit irradiance (L_o = S E for a directional light); white furnace integral 1.
#pragma once

static const float kHbEta = 1.55;                  // keratin
static const float kHbAlpha = 0.034906585;         // cuticle tilt 2 degrees
static const float kHbPi = 3.14159265;

// Chiang 2016 eq. 9: the absorption whose multiple-scattering albedo is `color` at azimuthal roughness beta_n.
float3 hb_sigma_a(float3 color, float beta_n) {
  const float b = beta_n;
  const float d = 5.969 - 0.215 * b + 2.532 * b * b - 10.73 * b * b * b + 5.574 * b * b * b * b + 0.245 * pow(b, 5.0);
  const float3 l = log(clamp(color, 1e-4, 1.0)) / d;
  return l * l;
}

float hb_log_i0(float x) {
  if (x >= 12.0) return x + 0.5 * (-log(2.0 * kHbPi) + log(1.0 / x)) + 1.0 / (8.0 * x);
  float val = 0.0, term = 1.0;
  [loop] for (int k = 1; k < 30; ++k) {
    term *= (x * x * 0.25) / float(k * k);
    val += term;
  }
  return log(1.0 + val);
}

// d'Eon 2011 longitudinal lobe of variance v (its integral over theta_i weighted by cos theta_i is 1).
float hb_mp(float cos_ti, float cos_to, float sin_ti, float sin_to, float v) {
  const float a = cos_ti * cos_to / v, b = sin_ti * sin_to / v;
  if (v <= 0.1) return exp(hb_log_i0(a) - b - 1.0 / v + 0.6931 + log(1.0 / (2.0 * v)));
  return exp(-b) * exp(hb_log_i0(a)) / (sinh(1.0 / v) * 2.0 * v);
}

float hb_fresnel(float cos_i, float eta) {
  cos_i = saturate(cos_i);
  const float sin_t2 = (1.0 - cos_i * cos_i) / (eta * eta);
  if (sin_t2 >= 1.0) return 1.0;
  const float cos_t = sqrt(saturate(1.0 - sin_t2));
  const float rs = (cos_i - eta * cos_t) / (cos_i + eta * cos_t), rp = (eta * cos_i - cos_t) / (eta * cos_i + cos_t);
  return 0.5 * (rs * rs + rp * rp);
}

float hb_trimmed_logistic(float x, float s) {
  x = abs(x);
  const float e = exp(-x / s);
  const float pdf = e / (s * (1.0 + e) * (1.0 + e));
  const float c = 1.0 / (1.0 + exp(-kHbPi / s));
  return pdf / (c - (1.0 - c));
}

float hb_np(float phi, float p, float s, float gamma_o, float gamma_t) {
  float d = phi - (2.0 * p * gamma_t - 2.0 * gamma_o + p * kHbPi);
  d = d - 2.0 * kHbPi * floor((d + kHbPi) / (2.0 * kHbPi));   // to [-pi, pi)
  return hb_trimmed_logistic(d, s);
}

// S(wi, wo) at fibre offset h, RGB. sigma_a from hb_sigma_a, beta = roughness clamped to [0.05, 1].
float3 hb_eval(float3 t, float3 wo, float3 wi, float h, float3 sigma_a, float beta) {
  beta = clamp(beta, 0.05, 1.0);
  const float v0 = pow(0.726 * beta + 0.812 * beta * beta + 3.7 * pow(beta, 20.0), 2.0);
  const float v[4] = {v0, 0.25 * v0, 4.0 * v0, 4.0 * v0};
  const float s = sqrt(kHbPi / 8.0) * (0.265 * beta + 1.194 * beta * beta + 5.372 * pow(beta, 22.0));
  const float sin_to = clamp(dot(wo, t), -1.0, 1.0), sin_ti = clamp(dot(wi, t), -1.0, 1.0);
  const float cos_to = sqrt(1.0 - sin_to * sin_to), cos_ti = sqrt(1.0 - sin_ti * sin_ti);
  const float3 po = wo - sin_to * t, pi = wi - sin_ti * t;
  const float nrm = max(length(po) * length(pi), 1e-9);
  const float phi = atan2(dot(cross(po, pi), t) / nrm, clamp(dot(po, pi) / nrm, -1.0, 1.0));
  // Attenuation A_p (Fresnel at the cuticle, absorption along the refracted path).
  const float sin_tt = sin_to / kHbEta, cos_tt = sqrt(saturate(1.0 - sin_tt * sin_tt));
  const float etap = sqrt(max(kHbEta * kHbEta - sin_to * sin_to, 1e-12)) / max(cos_to, 1e-6);
  const float sin_gt = h / etap, cos_gt = sqrt(saturate(1.0 - sin_gt * sin_gt));
  const float cos_go = sqrt(saturate(1.0 - h * h));
  const float f = hb_fresnel(cos_to * cos_go, kHbEta);
  const float3 T = exp(-sigma_a * (2.0 * cos_gt / max(cos_tt, 1e-6)));
  const float3 a1 = (1.0 - f) * (1.0 - f) * T, a2 = a1 * f * T;
  const float3 a3 = a2 * f * T / max(1.0 - T * f, 1e-6);
  const float gamma_t = asin(clamp(sin_gt, -1.0, 1.0)), gamma_o = asin(clamp(h, -1.0, 1.0));
  float3 sum = 0.0;
  [unroll] for (int p = 0; p < 3; ++p) {
    const float k = p == 0 ? -2.0 : p == 1 ? 1.0 : 4.0;   // cuticle tilt per lobe
    const float so = sin_to * cos(k * kHbAlpha) + cos_to * sin(k * kHbAlpha);
    const float co = abs(cos_to * cos(k * kHbAlpha) - sin_to * sin(k * kHbAlpha));
    const float3 A = p == 0 ? f.xxx : p == 1 ? a1 : a2;
    sum += hb_mp(cos_ti, co, sin_ti, so, v[p]) * hb_np(phi, float(p), s, gamma_o, gamma_t) * A;
  }
  return sum + hb_mp(cos_ti, cos_to, sin_ti, sin_to, v[3]) / (2.0 * kHbPi) * a3;
}

// A pixel wider than the fibre sees every offset: the mean over four stratified h (the far-field response).
float3 hb_eval_pixel(float3 t, float3 wo, float3 wi, float3 sigma_a, float beta) {
  float3 sum = 0.0;
  [unroll] for (int i = 0; i < 4; ++i) sum += hb_eval(t, wo, wi, -0.75 + 0.5 * float(i), sigma_a, beta);
  return sum * 0.25;
}
