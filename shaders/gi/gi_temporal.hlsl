// Temporal accumulation with depth-based rejection. No reprojection yet:
// history is reused where the depth still matches, so a moving camera falls
// back to the fresh (noisier) sample instead of ghosting.
#include "gi_common.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 id = tid.xy;
  if (any(id >= pc.half)) return;
  float4 cur = giRaw[id];
  float4 hist = giHistIn[id];
  bool valid = hist.w > 0.0 && cur.w > 0.0 && abs(hist.w - cur.w) < cur.w * 0.01 && all(isfinite(hist.rgb));
  float a = valid ? pc.history : 1.0;
  giHistOut[id] = float4(lerp(hist.rgb, cur.rgb, a), cur.w);
}
