// GPU half of the dual-scattering check (tools/hair_primary_gpu_check.cpp "bsdf" mode with this module,
// tests/hair_primary_gpu_test.py): shaders/rt/hair_dual.hlsli hd_shade vs tools/hair_groom/dual.py shade_table.
#define RT_HAIR_FIBRE_GI_REFERENCE 1
#include "../shaders/rt/hair_dual.hlsli"
#include "../shaders/rt/hair_depth_match.hlsli"

[[vk::binding(1)]] ByteAddressBuffer gIn;     // per sample 16 floats: t, h; wo, roughness; wi, fibres in front; colour, 0
[[vk::binding(3)]] RWByteAddressBuffer gOut;  // per sample float4: F (RGB), 0
struct Push { uint count; };
[[vk::push_constant]] Push pc;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= pc.count) return;
  const uint a = id.x * 64u;
  const float4 th = asfloat(gIn.Load4(a)), wob = asfloat(gIn.Load4(a + 16u));
  const float4 win = asfloat(gIn.Load4(a + 32u)), col = asfloat(gIn.Load4(a + 48u));
  // h > 1.5: the pixel response (hd_shade_pixel, four offsets), as gi/hair_fibre.hlsl uses it; h > 3.5: its single
  // scattering part alone (hd_shade_pixel_parts, the part the fibre pass writes to the specular buffer).
  float3 single, multi;
  hd_shade_pixel_parts(th.xyz, wob.xyz, win.xyz, win.w, col.xyz, wob.w, single, multi);
  // h > 4.5: the strand pixel depth test (hair_depth_match: t.x the device depth, t.y the linear depth, t.z zr).
  const float3 F = th.w > 4.5 ? float3(hair_depth_match(th.x, th.y, th.z) ? 1.0 : 0.0, 0.0, 0.0)
                : th.w > 3.5 ? single
                : th.w > 2.5 ? hd_volume_albedo(col.xyz, wob.w)
                : th.w > 1.5 ? hd_shade_pixel(th.xyz, wob.xyz, win.xyz, win.w, col.xyz, wob.w)
                              : hd_shade(th.xyz, wob.xyz, win.xyz, th.w, win.w, col.xyz, wob.w);
  gOut.Store4(id.x * 16u, asuint(float4(F, 0.0)));
}
