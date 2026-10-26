// Track D debug views (rt_debug), written over the lit diffuse at the end of
// the tiled lighting so they show up in every later pass and dump:
//   1 traced sun visibility (white lit, black shadowed)
//   2 game's sun mask in red, traced visibility in green (yellow: both lit)
//   3 primary-ray check of the TLAS against the depth buffer: green match
//     (within 0.5%), red geometry in front, blue behind, magenta miss
//   4 accumulated ambient visibility as folded into the game's AO (ray-traced
//     with rt_ao=1, screen-space otherwise)
// Views 1-3 need rt_shadows=1 (they read its visibility image).
#include "gi_common.hlsli"

[[vk::binding(18, 0)]] RaytracingAccelerationStructure gScene;
[[vk::binding(19, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> rtVis;

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  float4 v = rtVis[fp];
  float3 c;
  if (pc.rt_debug == 1) c = v.rrr;
  else if (pc.rt_debug == 2) c = float3(v.g, v.r, 0.0);
  else if (pc.rt_debug == 4) {
    float d = gDepth.Load(int3(fp, 0));
    c = d > 0.0 ? gather_history(fp, d).aaa : 0.0;
  }
  else {
    float k = v.b;
    c = k > 0.9 ? float3(0, 1, 0) : k > 0.5 ? float3(1, 0, 0) : k > 0.2 ? float3(0, 0.3, 1) : float3(1, 0, 1);
    if (gDepth.Load(int3(fp, 0)) <= 0.0) c = 0.0;
  }
  gDiffuse[fp] = c * 0.5;
  gSpecular[fp] = float3(0, 0, 0);
}
