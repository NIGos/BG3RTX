// Track C + D: hybrid ray-traced GI (gi_rt=1, needs rt=1). Same samples and
// outputs as gi_trace (half resolution, giRaw: rgb one-bounce radiance,
// a ambient visibility; giRawDepth), so the temporal, AO and composite passes
// are unchanged. The rays are hardware ray queries against the frame's TLAS
// instead of a march against the depth buffer: occluders that are off-screen,
// behind the visible surface or thinner than the depth test's thickness count,
// and alpha-tested geometry lets light through (rt_common.hlsli).
//
// Radiance at a hit: if the hit projects on screen and the depth buffer there
// is the hit itself (within gi_rt_depth_tol), it is last frame's lit radiance
// at that pixel (prevRadiance), as in the screen-space trace. Otherwise the
// hit point is not visible in any image we have. gi_rt_fallback chooses:
//   0 (default) it occludes the game's ambient but adds no bounce: the game's
//     IBL is what the hidden surface would mostly reflect, and that is exactly
//     the term the occlusion removes, so this is a slight darkening bias;
//   1 it is ignored (a miss for both: the game's ambient stays);
//   2 it occludes, and returns the mean radiance of this sample's on-screen
//     hits (a cheap estimate; nothing if it has none).
// Each hit weighs 1 - (t / gi_rt_radius)^2 for both its occlusion and its
// bounce, so rays fade out smoothly at the radius.
//
// SHARC_QUERY_PASS (gi_trace_rt_sharc.hlsl, sharc=1): a hidden or off-screen hit first
// reads the radiance cache (sharc_common.hlsli) at the hit's position and geometric normal;
// found, it is a bounce like an on-screen hit. Not found, or nearer than the voxel, the
// gi_rt_fallback rule above applies. sharc_debug=4 writes the tiers instead of the bounce
// (r cache, g screen, b no radiance: fallback), as fractions of the rays.
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
#if SHARC_QUERY_PASS
#define SHARC_RT_QUERY 1
#include "sharc_common.hlsli"
#endif

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 id = tid.xy;
  if (any(id >= pc.half)) return;
  uint2 fp = gi_fp(id);
  float d = gDepth.Load(int3(fp, 0));
  float2 enc = gNormal.Load(int3(fp, 0)).xy;
  float2 e = enc * 3.5554 - 1.7777;
  if (d <= 0.0 || dot(e, e) + 1.0 > 5.4e10) {
    giRaw[id] = float4(0, 0, 0, 1);
    giRawDepth[id] = 0;
    return;
  }
  float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  float3 P = ray_at(uv) * d;
  float3 N = to_lighting_space(decode_normal(enc));
  float3x3 inv = frustum_inverse();
  float dist = length(P);
  float pa = pixel_angle(uv);
  RtCone cone = {dist * pa * 2.0, pa * 2.0};   // half resolution
  const float4 org = rt_origin_at(fp);
  const float R = pc.gi_rt_radius;

  uint seed = pixel_seed(fp, 0u);
  float3 bounce = 0, seen = 0;
  float occ = 0, nseen = 0, wfall = 0;
#if SHARC_QUERY_PASS
  const float3 cam_abs = sharc_cam();
  float3 tiers = 0;   // rays: cache, screen, fallback
#endif
  for (uint r = 0; r < pc.rays; ++r) {
    float3 dir = cosine_hemisphere(N, rand01(seed), rand01(seed));
    float3 o = rt_ray_origin_o(org, P, N, dist * pa, dir);
#if SHARC_QUERY_PASS
    RtHit hh = rt_closest_hit(o, dir, R, cone);
    float t = hh.t;
#else
    float t = rt_closest(o, dir, R, cone);
#endif
    float k = saturate(t / R);
    float w = 1.0 - k * k;
    float2 quv;
    float qd;
    bool onscreen = t >= 0.0 && project(inv, o + dir * t, quv, qd) && all(quv >= 0.0) && all(quv <= 1.0);
    bool visible = false;
    if (onscreen) {
      int2 qp = min(int2(quv * float2(pc.full)), int2(pc.full) - 1);
      float sd = gDepth.Load(int3(qp, 0));
      if (sd > 0.0 && abs(qd - sd) < sd * pc.gi_rt_tol) {
        float3 L = prev_radiance_at(qp);
        if (all(isfinite(L))) {
          bounce += L * w;
          seen += L;
          nseen += 1.0;
          visible = true;
        }
      }
    }
    if (pc.rt_stats != 0) {
      uint bin = (id.x >> 3) * 7 + (id.y >> 3) * 13;
      stat_add(8, true, bin);
      stat_add(9, t >= 0.0, bin);
      stat_add(10, visible, bin);
      stat_add(11, onscreen && !visible, bin);
      stat_add(12, t >= 0.0 && !onscreen, bin);
    }
    if (t < 0.0) continue;
#if SHARC_QUERY_PASS
    if (visible) {
      tiers.y += 1.0;
    } else {
      sharc_stat(kShGiHidden, onscreen);
      sharc_stat(kShGiOffscreen, !onscreen);
      float3 Lc;
      bool near;
      const bool ask = (shc.flags & (onscreen ? 1u : 2u)) != 0u;
      const bool found = ask && sharc_ray_hit(hh, o + dir * t, dir, t, cam_abs, Lc, near);
      sharc_stat(kShGiQueried, ask && !near);
      sharc_stat(kShGiNear, ask && near);
      sharc_stat(kShGiFound, found);
      if (found) {
        bounce += Lc * w;
        occ += w;
        tiers.x += 1.0;
        continue;
      }
      tiers.z += 1.0;
    }
#endif
    if (visible) occ += w;
    else if (pc.gi_rt_fallback != 1) {
      occ += w;
      wfall += w;
    }
  }
  if (pc.gi_rt_fallback == 2 && nseen > 0.0) bounce += seen / nseen * wfall;
  float n = float(max(pc.rays, 1u));
#if SHARC_QUERY_PASS
  if (shc.debug == 4u) bounce = tiers;
#endif
  giRaw[id] = float4(bounce / n, saturate(1.0 - occ / n));
  giRawDepth[id] = d;
}
