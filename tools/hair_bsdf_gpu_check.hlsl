// GPU half of the fibre BSDF check (tools/hair_primary_gpu_check.cpp "bsdf" mode, tests/hair_primary_gpu_test.py):
// shaders/rt/hair_bsdf.hlsli hb_eval on the samples tools/hair_groom/bsdf.py evaluates on the CPU.
#include "../shaders/rt/hair_bsdf.hlsli"

[[vk::binding(1)]] ByteAddressBuffer gIn;     // per sample 16 floats: t, h; wo, roughness; wi, 0; colour, 0
[[vk::binding(3)]] RWByteAddressBuffer gOut;  // per sample float4: S (RGB), 0
struct Push { uint count; };
[[vk::push_constant]] Push pc;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= pc.count) return;
  const uint a = id.x * 64u;
  const float4 th = asfloat(gIn.Load4(a)), wob = asfloat(gIn.Load4(a + 16u));
  const float4 wi = asfloat(gIn.Load4(a + 32u)), col = asfloat(gIn.Load4(a + 48u));
  const float beta = clamp(wob.w, 0.05, 1.0);
  const float3 s = hb_eval(th.xyz, wob.xyz, wi.xyz, th.w, hb_sigma_a(col.xyz, beta), beta);
  gOut.Store4(id.x * 16u, asuint(float4(s, 0.0)));
}
