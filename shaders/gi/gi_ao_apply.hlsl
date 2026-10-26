// Full resolution, before the tiled lighting: folds the traced visibility into
// the game's AO, so its own diffuse and specular ambient get occluded by it.
#include "gi_common.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) return;
  float vis = gather_history(fp, d).a;
  float4 ao = gAO[fp];
  // The game's own AO, for the D-LS pass's AO-on-direct factor (ao_direct=2).
  gAux[fp] = float4(ao.x, 0.0, 0.0, 0.0);
  // Replace rather than multiply: the game's SSAO and our visibility measure
  // the same occlusion, and their product darkens contacts twice.
  ao.x = lerp(ao.x, vis, pc.ao);
  if (pc.debug == 3) ao.x = vis;
  gAO[fp] = ao;
}
