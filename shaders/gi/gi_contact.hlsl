// Screen-space contact shadows for the sun: a short march from each pixel
// towards the sun against the linear depth. Occlusion the cascaded shadow maps
// are too coarse to resolve (feet, pipes on walls, folds in cloth) darkens
// the game's own sun shadow mask, so its lighting shades it consistently.
#include "gi_common.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) return;
  float mask = gSunShadow[fp];
  if (mask <= 0.0) return;   // already fully shadowed
  float3 L = normalize(cam.v0.xyz);
  float3 N = to_lighting_space(decode_normal(gNormal.Load(int3(fp, 0)).xy));
  if (dot(N, L) <= 0.0) return;

  float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  float3 P = ray_at(uv) * d;
  float dist = length(P);
  P += N * (dist * 0.002);
  float3x3 inv = frustum_inverse();
  uint seed = hash(fp.x * 7919u + fp.y * 104729u + pc.frame * 31337u);
  float jitter = rand01(seed);
  float len = dist * pc.cs_length;
  float occluded = 0.0;
  for (uint s = 1; s <= pc.cs_steps; ++s) {
    float t = len * (float(s) - jitter) / float(pc.cs_steps);
    float2 quv;
    float qd;
    if (!project(inv, P + L * t, quv, qd) || any(quv < 0.0) || any(quv >= 1.0)) break;
    float sd = gDepth.Load(int3(int2(quv * float2(pc.full)), 0));
    // Thin-object test: the surface in front must be close behind the ray.
    if (sd > 0.0 && qd > sd * 1.001 && qd < sd * 1.02) {
      occluded = 1.0 - float(s - 1) / float(pc.cs_steps);   // soften with distance along the ray
      break;
    }
  }
  if (pc.debug == 6) mask = 1.0 - occluded;
  else mask *= 1.0 - occluded * pc.cs_strength;
  gSunShadow[fp] = mask;
}
