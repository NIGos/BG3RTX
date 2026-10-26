// Track D phase 2: ray-traced sun shadows against the frame's own G-buffer
// geometry (TLAS built by the host, layer/rtscene.cpp), folded into the game's
// screen-space sun shadow mask before the tiled lighting, so BG3's own sun
// term shades with it. Runs in place of the screen-space contact shadows.
//
// Space: the TLAS holds camera-relative world positions (W of the G-buffer
// VS), the lighting reconstructs P = ray(uv) * linear depth in the same space
// (checked with rt_debug=3), and cam.v0 points towards the sun.
#include "gi_common.hlsli"

[[vk::binding(18, 0)]] RaytracingAccelerationStructure gScene;
// r: traced sun visibility, g: the game's sun mask before the fold,
// b: primary-ray check (1 match, 0.66 hit in front, 0.33 hit behind, 0 miss; rt_debug=3 only)
[[vk::binding(19, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> rtVis;

float shadow_ray(float3 o, float3 dir, float tmax) {
  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE> q;
  RayDesc r;
  r.Origin = o;
  r.Direction = dir;
  r.TMin = 0.0;
  r.TMax = tmax;
  q.TraceRayInline(gScene, 0, 0xFF, r);
  q.Proceed();
  return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}

// Closest hit distance along a ray, or -1.
float closest_hit(float3 o, float3 dir, float tmax) {
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE> q;
  RayDesc r;
  r.Origin = o;
  r.Direction = dir;
  r.TMin = 0.0;
  r.TMax = tmax;
  q.TraceRayInline(gScene, 0, 0xFF, r);
  while (q.Proceed()) {}
  return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? q.CommittedRayT() : -1.0;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  float d = gDepth.Load(int3(fp, 0));
  float mask = gSunShadow[fp];
  if (d <= 0.0) {
    rtVis[fp] = float4(1, mask, 0, 1);
    return;
  }
  float3 L = normalize(cam.v0.xyz);
  float3 N = to_lighting_space(decode_normal(gNormal.Load(int3(fp, 0)).xy));
  float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  float3 P = ray_at(uv) * d;
  float dist = length(P);

  float vis = 0.0;
  if (dot(N, L) > 0.0) {
    float3 o = P + N * (dist * pc.rt_bias);
    vis = shadow_ray(o, L, pc.rt_tmax);
  }
  float check = 0.0;
  if (pc.rt_debug == 3) {
    float t = closest_hit(float3(0, 0, 0), P / dist, dist * 1.5 + 1.0);
    float rel = t / dist - 1.0;
    check = t < 0.0 ? 0.0 : abs(rel) < 0.005 ? 1.0 : rel < 0.0 ? 0.66 : 0.33;
  }
  rtVis[fp] = float4(vis, mask, check, 1);
  // Only ever darkens: off-screen casters still come from the game's cascades.
  if (mask > 0.0 && pc.rt_debug == 0) gSunShadow[fp] = mask * lerp(1.0, vis, saturate(pc.rt_strength));
}
