// Full-resolution composite: depth-aware 3x3 upsample of the accumulated GI,
// modulated by the diffuse colour, added into the game's diffuse lighting
// buffer before its subsurface-scattering and fog passes run.
#include "gi_common.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) return;

  int2 hp = int2(fp / 2);
  float3 gi = 0;
  float wsum = 0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
      int2 q = clamp(hp + int2(x, y), int2(0, 0), int2(pc.half) - 1);
      float4 h = giHistOut[q];
      float w = (h.w > 0.0) ? exp(-abs(h.w - d) / (d * 0.01)) * (x == 0 && y == 0 ? 2.0 : 1.0) : 0.0;
      gi += h.rgb * w;
      wsum += w;
    }
  gi = wsum > 1e-4 ? gi / wsum : 0;

  float4 alb = gAlbedo.Load(int3(fp, 0));
  float metal = gMaterial.Load(int3(fp, 0)).z;
  float3 add = gi * alb.rgb * (1.0 - metal) * pc.strength;

  if (pc.debug == 1) gDiffuse[fp] = gi;
  else if (pc.debug == 2) gDiffuse[fp] = add * 4.0;
  else gDiffuse[fp] = gDiffuse[fp] + add;
}
