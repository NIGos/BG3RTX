// D-LS validation (ls_debug=4/5/6), before the tiled lighting: no sun and no AO,
// so the lighting's diffuse/specular hold only direct local light (and projector
// lights and in-scatter, which rt_local_shadows.hlsl does not recompute).
// AO 0 removes the ambient; direct light keeps 1 - m0.y of itself (BG3 multiplies
// direct light by mix(1, AO, set1/b0 m0.y), 0.5 in gameplay), which the recompute
// applies too. ls_debug=6 sets AO 1 instead (ambient unoccluded).
#include "gi_common.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  float4 ao = gAO[fp];
  gAO[fp] = float4(pc.ls_debug == 6 ? 1.0 : 0.0, ao.yzw);
  // ls_debug=7 keeps the sun (validates the sun recompute of rt/bg3_sun.hlsli).
  if (pc.ls_has_mask != 0 && pc.ls_debug != 7) gSunShadow[fp] = 0.0;
}
