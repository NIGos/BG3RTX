// Temporal accumulation with depth-based rejection (no reprojection yet:
// a moving camera falls back to the fresh, noisier sample instead of ghosting).
#include "gi_common.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 id = tid.xy;
  if (any(id >= pc.half)) return;
  float4 cur = giRaw[id];
  float cd = giRawDepth[id];
  float4 hist = giHistIn[id];
  float hd = giHistDepthIn[id];
  bool valid = hd > 0.0 && cd > 0.0 && abs(hd - cd) < cd * 0.01 && all(isfinite(hist));
  float a = valid ? pc.history : 1.0;
  giHistOut[id] = lerp(hist, cur, a);
  giHistDepthOut[id] = cd;
}
