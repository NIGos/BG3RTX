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
  ao.x *= lerp(1.0, vis, pc.ao);
  if (pc.debug == 3) ao.x = vis;
  gAO[fp] = ao;
}
