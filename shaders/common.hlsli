// Shared helpers for BG3RTX shaders.
#pragma once

// ---- BG3 G-buffer (decoded from the tiled lighting compute shader f469db78,
// see docs/FRAME_MAP.md) ----
//   RT0 RG16F   view-space normal, stereographic projection
//   RT1 RGBA8   base colour (rgb)
//   RT2 RGBA8   x perceptual roughness, y specular reflectance (F0 = 0.16*y^2),
//               z metalness, w bit flags (bits 4-6: light channel mask)

// RT0.xy -> unit view-space normal. Identical to the game's own decode,
// including the degenerate case.
float3 bg3_decode_normal(float2 rg) {
  float2 e = rg * 3.5554 - 1.7777;
  float t = dot(e, e) + 1.0;
  if (t > 5.4e10) return float3(0.0, 0.0, 1.0);
  float k = 2.0 / t;
  return float3(e * k, 1.0 - k);
}

// Specular reflectance at normal incidence, as BG3's lighting computes it.
float3 bg3_f0(float3 base, float spec, float metal) {
  return lerp((0.16 * spec * spec).xxx, base, metal);
}

// Old guess, kept for reference: BG3 does NOT use octahedral normals.
float3 oct_decode(float2 e) {
  e = e * 2.0 - 1.0;
  float3 n = float3(e.xy, 1.0 - abs(e.x) - abs(e.y));
  float t = saturate(-n.z);
  n.xy += select(n.xy >= 0.0, -t, t);
  return normalize(n);
}

// DLSS-RR integration guide, appendix "Specular Albedo Calculation"
// (Ray Tracing Gems ch. 32). alpha = roughness^2.
float3 env_brdf_approx2(float3 specular_color, float alpha, float NoV) {
  NoV = abs(NoV);
  float4 X = float4(1.0, NoV, NoV * NoV, NoV * NoV * NoV);
  float4 Y = float4(1.0, alpha, alpha * alpha, alpha * alpha * alpha);
  float2x2 M1 = float2x2(0.99044, -1.28514, 1.29678, -0.755907);
  float3x3 M2 = float3x3(1.0, 2.92338, 59.4188, 20.3225, -27.0302, 222.592, 121.563, 626.13, 316.627);
  float2x2 M3 = float2x2(0.0365463, 3.32707, 9.0632, -9.04756);
  float3x3 M4 = float3x3(1.0, 3.59685, -1.36772, 9.04401, -16.3174, 9.22949, 5.56589, 19.7886, -20.2123);
  float bias = dot(mul(M1, X.xy), Y.xy) * rcp(dot(mul(M2, X.xyw), Y.xyw));
  float scale = dot(mul(M3, X.xy), Y.xy) * rcp(dot(mul(M4, X.xzw), Y.xyw));
  bias *= saturate(specular_color.g * 50.0);
  return mad(specular_color, max(0.0, scale), max(0.0, bias));
}
