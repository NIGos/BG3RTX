// Diagnostic producer only. No lighting, history or sun-mask writes.
// Header row stores exact float32 camera bits as four integer bytes in FP16.
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
struct PanCamera { float4 c[14]; };
[[vk::binding(48, 0)]] ConstantBuffer<PanCamera> panCamera;

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 p = tid.xy;
  if (any(p >= pc.full)) return;
  if (p.y == 0 && p.x < 48) {
    float values[48] = {
      cam.ray0.x,cam.ray0.y,cam.ray0.z,cam.ray0.w,
      cam.ray1.x,cam.ray1.y,cam.ray1.z,cam.ray1.w,
      cam.ray2.x,cam.ray2.y,cam.ray2.z,cam.ray2.w,
      vp.scale.x,vp.scale.y,vp.bias.x,vp.bias.y,
      panCamera.c[13].x,panCamera.c[13].y,panCamera.c[13].z,0,
      cam.m6_r0.x,cam.m6_r0.y,cam.m6_r0.z,cam.m6_r0.w,
      cam.m6_r1.x,cam.m6_r1.y,cam.m6_r1.z,cam.m6_r1.w,
      cam.m6_r2.x,cam.m6_r2.y,cam.m6_r2.z,cam.m6_r2.w,
      cam.m6_r3.x,cam.m6_r3.y,cam.m6_r3.z,cam.m6_r3.w,
      panCamera.c[8].x,panCamera.c[8].y,panCamera.c[8].z,panCamera.c[8].w,
      panCamera.c[9].x,panCamera.c[9].y,panCamera.c[9].z,panCamera.c[9].w,
      panCamera.c[10].x,panCamera.c[10].y,panCamera.c[10].z,panCamera.c[10].w};
    uint bits = asuint(values[p.x]);
    gAux[p] = float4(bits & 255u,(bits >> 8) & 255u,(bits >> 16) & 255u,bits >> 24);
    return;
  }
  float d = gDepth.Load(int3(p,0));
  if (d <= 0) { gAux[p] = 0; return; }
  float2 uv = (float2(p)+0.5)/float2(pc.full);
  float3 P = ray_at(uv)*d;
  float dist = length(P), pa = pixel_angle(uv);
  RtCone cone = {0,pa};
  RtDebugHit hit = rt_closest_debug(0.0.xxx,P/dist,dist*1.5+1.0,cone);
  float4 org = rt_origin_at(p);
  float3 ng = org.w < kOriginNoHit ? org.xyz : depth_geo_normal(int2(p),P);
  float tol = depth_tol(dist,pa,dot(ng,P)/dist,kJitterPx,1.0)+kTlasRel*dist;
  bool valid = hit.t >= 0 && !hit.aborted;
  // Normalized signed depth residual, absolute ray distance, derived bound,
  // status: 0 absent, 1 opaque, 2 alpha hit, -1 miss/unknown.
  gAux[p] = float4(valid ? clamp((hit.t-dist)/tol,-65000.0,65000.0) : 0,
                   dist,tol,valid ? (hit.alpha ? 2 : 1) : -1);
  uint bin = (p.x >> 3)*7+(p.y >> 3)*13;
  if (pc.rt_stats != 0) {
    stat_add(0,true,bin);
    stat_add(1,valid && abs(hit.t-dist)<tol,bin);
    stat_add(2,valid && hit.t-dist <= -tol,bin);
    stat_add(3,valid && hit.t-dist >= tol,bin);
    stat_add(4,!valid,bin);
  }
}
