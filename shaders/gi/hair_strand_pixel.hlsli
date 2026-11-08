// The visible strand at a pixel (rt_hair_groom_primary): the layer's hair primary header, read through its device
// address (specialization constants 1100/1101, core/gi.cpp refresh). Shared by gi/hair_fibre.hlsl (direct light after
// D-LS) and gi/hair_fibre_spec.hlsl (specular indirect after D-RF). Needs rt/hair_primary.hlsli and gi_common.hlsli.
#pragma once
#include "../rt/hair_depth_match.hlsli"
[[vk::constant_id(1100)]] const uint kHfRecordLo = 0u;   // the layer's hair primary header (persist key)
[[vk::constant_id(1101)]] const uint kHfRecordHi = 0u;

// The view depth of the frustum rays: their tips lie on one plane (P = ray_at(uv) d), its distance from the camera.
float hf_frustum_zr() {
  const float3 n = cross(cam.ray1.xyz - cam.ray0.xyz, cam.ray2.xyz - cam.ray0.xyz);
  return abs(dot(cam.ray0.xyz, n)) / max(length(n), 1e-30);
}

uint64_t hf_record() { return (uint64_t(kHfRecordHi) << 32) | uint64_t(kHfRecordLo); }
uint hf_ld(uint64_t a) { return vk::RawBufferLoad<uint>(a, 4); }
uint64_t hf_addr(uint64_t a) { const uint2 v = vk::RawBufferLoad<uint2>(a, 4); return (uint64_t(v.y) << 32) | uint64_t(v.x); }
float3 hf_f3(uint64_t a) { return asfloat(vk::RawBufferLoad<uint3>(a, 4)); }

// The strand's unit tangent (TLAS frame: camera-relative world) when the pixel shows a strand of this frame.
bool hf_strand(uint2 fp, float d, out float3 T) {
  T = float3(0.0, 0.0, 1.0);
  const uint64_t rec = hf_record();
  if (rec == 0u) return false;
  const uint tag = hf_ld(rec), width = hf_ld(rec + 4u), height = hf_ld(rec + 8u), grooms = hf_ld(rec + 12u);
  if (tag == 0u || grooms == 0u || any(fp >= uint2(width, height))) return false;
  const uint4 e = vk::RawBufferLoad<uint4>(hf_addr(rec + 32u) + uint64_t(fp.y * width + fp.x) * hair_primary_pixel_bytes, 16);
  if (e.w != hp_seal(e.xyz, tag) || e.x == hair_primary_no_strand || (e.x >> 24) >= grooms) return false;
  if (!hair_depth_match(asfloat(e.z), d, hf_frustum_zr())) return false;   // something else won this pixel
  const uint4 row = vk::RawBufferLoad<uint4>(rec + hair_primary_table_offset + (e.x >> 24) * 16u, 16);
  const uint64_t pool = hf_addr(rec + 24u);
  uint i0;
  if (!hp_segment(pool, hf_ld(rec + 56u), row, e.x & hair_primary_max_segment, i0)) return false;
  const float3 dp = hf_f3(hp_at(pool, row.x, i0 + 1u, 12u)) - hf_f3(hp_at(pool, row.x, i0, 12u));
  if (!(dot(dp, dp) > 0.0)) return false;
  T = normalize(dp);
  return true;
}
