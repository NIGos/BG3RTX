// GPU half of the M4.3 check (tools/hair_primary_gpu_check.cpp "bsdf" mode with this module, tests/hair_primary_gpu_test.py
// game_light_check): the game's hair term of one local light with a shadow map, as gi/hair_fibre.hlsl removes it,
// S (bg3_hair_spec + bg3_hair_diff(texp = 1 - S)) per unit E, against tools/hair_groom/bsdf.py game_hair_light.
// rt/bg3_materials.hlsli's own lobes; its lighting bindings are declared but unused here.
// rt_common.hlsli's guard counters, which bg3_lights.hlsli's list loads call (not reached here).
static const uint kGuardLightList = 118u, kGuardLoopCap = 121u;
void guard_count(uint k) {}
#include "../shaders/rt/bg3_materials.hlsli"

[[vk::binding(1)]] ByteAddressBuffer gIn;     // per sample 16 floats: t, S; wo, roughness; wi, scatter; colour, backlit
[[vk::binding(3)]] RWByteAddressBuffer gOut;  // per sample float4: spec + diff (RGB), 0
struct Push { uint count; };
[[vk::push_constant]] Push pc;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= pc.count) return;
  const uint a = id.x * 64u;
  const float4 ts = asfloat(gIn.Load4(a)), wob = asfloat(gIn.Load4(a + 16u));
  const float4 wis = asfloat(gIn.Load4(a + 32u)), cb = asfloat(gIn.Load4(a + 48u));
  Bg3Mat m = (Bg3Mat)0;
  m.cls = 5u;
  m.T = ts.xyz;
  m.hair_rough = max(0.09, wob.w);
  m.backlit = cb.w;
  m.scatter = wis.w;
  m.albedo = cb.xyz;
  // The strand pixel's normal: the view vector made perpendicular to the tangent (hair_primary_gbuffer's fibre normal).
  const float3 N = normalize(wob.xyz - ts.xyz * dot(wob.xyz, ts.xyz));
  const float S = ts.w;
  const float3 F = S * (bg3_hair_spec(m, wis.xyz, wob.xyz) + bg3_hair_diff(m, dot(N, wis.xyz), dot(m.T, wis.xyz), 1.0 - S));
  gOut.Store4(id.x * 16u, asuint(float4(F, 0.0)));
}
