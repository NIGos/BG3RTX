// GPU half of the dual-scattering check (tools/hair_primary_gpu_check.cpp "bsdf" mode with this module,
// tests/hair_primary_gpu_test.py): shaders/rt/hair_dual.hlsli hd_shade vs tools/hair_groom/dual.py shade_table.
#include "../shaders/rt/hair_dual.hlsli"

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
  gOut.Store4(id.x * 16u, asuint(float4(hd_shade(th.xyz, wob.xyz, win.xyz, th.w, win.w, col.xyz, wob.w), 0.0)));
}
