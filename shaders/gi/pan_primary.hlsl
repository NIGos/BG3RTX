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
#ifdef RT_PRIMARY_EPOCH
  if (all(p == 0)) {
    const uint3 stamp = gSceneStamp.Load3(0);
    const bool pose_valid = stamp.x != 0 && stamp.x == stamp.y && stamp.x == stamp.z;
    const float3 source_origin = asfloat(gSceneStamp.Load3(464u));
    const float3 delta = pose_valid ? panCamera.c[13].xyz-source_origin : 0.0.xxx;
    float trace_rotation = 0;
    [unroll] for (uint row=0; row<3; ++row)
      trace_rotation += dot(panCamera.c[row].xyz,asfloat(gSceneStamp.Load3(256u+row*16u)));
    const float scene_angle = pose_valid ? acos(clamp((trace_rotation-1)*.5,-1.0,1.0)) : 0;
    const float3 forward = float3(panCamera.c[0].z,panCamera.c[1].z,panCamera.c[2].z);
    rtStats.Store(144u*256u,asuint(delta.x));
    rtStats.Store(145u*256u,asuint(delta.y));
    rtStats.Store(146u*256u,asuint(delta.z));
    rtStats.Store(147u*256u,asuint(scene_angle));
    rtStats.Store(148u*256u,asuint(forward.x));
    rtStats.Store(149u*256u,asuint(forward.y));
    rtStats.Store(150u*256u,asuint(forward.z));
    rtStats.Store(151u*256u,pose_valid ? 1u : 0u);
    rtStats.Store(152u*256u,1u);
    rtStats.Store(155u*256u,asuint(panCamera.c[13].x));
    rtStats.Store(156u*256u,asuint(panCamera.c[13].y));
    rtStats.Store(157u*256u,asuint(panCamera.c[13].z));
  }
#endif
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
#if defined(RT_PRIMARY_EPOCH) || defined(RT_PRIMARY_IDENTITY)
  RtHit hit = rt_closest_hit(0.0.xxx,P/dist,dist*1.5+1.0,cone);
  const bool hit_alpha = hit.t >= 0 && alpha_instance(hit.inst,hit.geom);
#else
  RtDebugHit hit = rt_closest_debug(0.0.xxx,P/dist,dist*1.5+1.0,cone);
  const bool hit_alpha = hit.alpha;
#endif
  float4 org = rt_origin_at(p);
  float3 ng = org.w < kOriginNoHit ? org.xyz : depth_geo_normal(int2(p),P);
  float tol = depth_tol(dist,pa,dot(ng,P)/dist,kJitterPx,1.0)+kTlasRel*dist;
  bool valid = hit.t >= 0 && !hit.aborted;
  // Normalized signed depth residual, absolute ray distance, derived bound,
  // status: 0 absent, 1 opaque, 2 alpha hit, -1 miss/unknown.
  gAux[p] = float4(valid ? clamp((hit.t-dist)/tol,-65000.0,65000.0) : 0,
                   dist,tol,valid ? (hit_alpha ? 2 : 1) : -1);
#ifdef RT_PRIMARY_IDENTITY
  // Preserve residual/status; exact instance index bytes survive FP16 storage.
  gAux[p].yz = float2(hit.inst & 255u, (hit.inst >> 8) & 255u);
#endif
  uint bin = (p.x >> 3)*7+(p.y >> 3)*13;
#ifdef RT_PRIMARY_EPOCH
  { // Keep primary/epoch checks with full per-ray alpha/light statistics off.
#else
  if (pc.rt_stats != 0) {
#endif
    stat_add(0,true,bin);
    stat_add(1,valid && abs(hit.t-dist)<tol,bin);
    stat_add(2,valid && hit.t-dist <= -tol,bin);
    stat_add(3,valid && hit.t-dist >= tol,bin);
    stat_add(4,!valid,bin);
#ifdef RT_PRIMARY_EPOCH
    const bool pool = valid && (inst_info(hit.inst).meta & kInfoPool) != 0;
    stat_add(136,valid && pool,bin);
    stat_add(137,valid && pool && abs(hit.t-dist)<tol,bin);
    stat_add(138,valid && pool && hit.t-dist<=-tol,bin);
    stat_add(139,valid && pool && hit.t-dist>=tol,bin);
    stat_add(140,valid && !pool,bin);
    stat_add(141,valid && !pool && abs(hit.t-dist)<tol,bin);
    stat_add(142,valid && !pool && hit.t-dist<=-tol,bin);
    stat_add(143,valid && !pool && hit.t-dist>=tol,bin);
#endif
  }
}
