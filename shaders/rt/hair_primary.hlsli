// Primary strand hair (docs/RTX_HAIR_PRIMARY.md, key rt_hair_groom_primary): what the strand variants of the
// matched hair draws write. Math twin of tools/hair_groom/primary.py (CTest hair_groom_test, test_primary checks
// the encodings through the game's decoders and the depth formula against the explicit projection).
#pragma once

static const uint kHairPrimaryClass = 5u;   // RT2.w bits 0-2: Marschner hair (3033c895)

// RT0 (RG16F): stereographic view-space normal, as the G-buffer FS aa4efa4d writes it.
float2 hp_stereo_encode(float3 n) { return n.xy / max(1.0 - n.z, 1e-7) * 0.28125 + 0.5; }

// RT3.xyz (RGBA8 UNORM): 12:12 octahedral unit vector, the inverse of bg3_oct12 (bg3_materials.hlsli).
float3 hp_oct12_encode(float3 v) {
  const float3 p = v / (abs(v.x) + abs(v.y) + abs(v.z));
  const float2 sg = float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
  const float2 e = p.z >= 0.0 ? p.xy : (1.0 - abs(p.yx)) * sg;
  const uint2 q = uint2(clamp(round((e + 1.0) * 2047.0), 0.0, 4095.0));
  return float3(q.x >> 4, ((q.x & 15u) << 4) | (q.y >> 8), q.y & 255u) / 255.0;
}

// RT2.w (UNORM8): class bits 0-2, RG16F-copy flag bit 3, light-channel mask bits 4-6.
float hp_rt2_w(uint cls, uint light_mask, bool copy_flag) {
  return float((cls & 7u) | (copy_flag ? 8u : 0u) | ((light_mask & 7u) << 4)) / 255.0;
}

// Fibre shading normal: the direction to the camera made perpendicular to the tangent.
float3 hp_fibre_normal(float3 t, float3 to_camera) {
  const float3 n = to_camera - t * dot(to_camera, t);
  return n * rsqrt(max(dot(n, n), 1e-24));
}

// Native perspective depth D = A + B / w (A = near / (near - far)): on one camera ray w is proportional to the
// distance, so the strand hit at t_s on the card fragment's ray (card depth D_c at t_c) has this depth.
float hp_strand_depth(float d_card, float t_card, float t_strand, float a) {
  return a + (d_card - a) * (t_card / t_strand);
}
// Orthographic: D affine in the distance along the parallel ray.
float hp_strand_depth_ortho(float d_card, float t_card, float t_strand, float dd_dt) {
  return d_card + (t_strand - t_card) * dd_dt;
}
