// Fixed request. All traversal capabilities specialize; no define product.
#define RT_HIT_MATERIAL 1
#define RT_HIT_GI_DIRECT 1
#define RT_HIT_GI_SUN 1
#define RT_CASCADE_SUN_HIT 1
#define RT_COUNTERS_SILENT 1
#define RT_SPEC_NATIVE 1
#define RT_SPEC_HIT_INST 1
#define RT_SPEC_CASCADE 1
#include "gi_common.hlsli"
#include "gi_sun_wave.hlsli"
#include "../rt/rt_common.hlsli"
[numthreads(64,1,1)]
void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
  const uint i = (group.y * 65535u + group.x) * 64u + lane;
  if (i >= giSunWave.Load(0u)) return;
  const uint slot = giSunWave.Load(GI_SUN_WAVE_HEADER_BYTES + i * 4u);
  const uint a = sw_record(slot);
  const float3 o=asfloat(giSunWave.Load3(a+132u)), d=asfloat(giSunWave.Load3(a+144u));
  const float2 ft=asfloat(giSunWave.Load2(a+156u));
  RtCone cone={ft.x,0};
  g_rt_pass=kRtPassGI;
  g_rt_cascade_ray=RT_CASCADE_ON;
  const uint v=rt_visibility(o,d,ft.y,cone);
  giSunWave.Store(a+8u,v);
}
