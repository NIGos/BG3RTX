// Temporal accumulation. With the game's DLSS motion vectors the history is
// reprojected; without them it is reused in place. Either way a depth
// mismatch rejects it, so disocclusions fall back to the fresh sample.
#include "gi_common.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 id = tid.xy;
  if (any(id >= pc.half)) return;
  float4 cur = giRaw[id];
  float cd = giRawDepth[id];
  int2 hp = int2(id);
  if (pc.has_mv != 0) {
    float2 fpix = float2(min(id * 2 + 1, pc.full - 1)) + 0.5;
    float2 mv = gVelocity.Load(int3(int2(fpix), 0)) * pc.mv_scale;
    float2 prev = (fpix + mv) * 0.5;
    hp = int2(floor(prev));
  }
  bool inside = all(hp >= 0) && all(hp < int2(pc.half));
  float4 hist = inside ? giHistIn[hp] : float4(0, 0, 0, 1);
  float hd = inside ? giHistDepthIn[hp] : 0.0;
  bool valid = inside && hd > 0.0 && cd > 0.0 && abs(hd - cd) < cd * 0.03 && all(isfinite(hist));
  float a = valid ? pc.history : 1.0;
  giHistOut[id] = lerp(hist, cur, a);
  giHistDepthOut[id] = cd;
}
