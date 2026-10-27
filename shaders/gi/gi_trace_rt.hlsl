// Track C + D: hybrid ray-traced GI (gi_rt=1, needs rt=1). Same samples and
// outputs as gi_trace (half resolution, giRaw: rgb one-bounce radiance,
// a ambient visibility; giRawDepth), so the temporal, AO and composite passes
// are unchanged. The rays are hardware ray queries against the frame's TLAS
// instead of a march against the depth buffer: occluders that are off-screen,
// behind the visible surface or thinner than the depth test's thickness count,
// and alpha-tested geometry lets light through (rt_common.hlsli).
//
// Radiance at a hit: if the hit projects on screen and the depth buffer there
// is the hit itself (rt/depth_tol.hlsli depth_same_surface: the footprint, slope,
// jitter and depth precision at that pixel, against rtOrigin's TLAS surface there when
// rt_origin=1, times gi_rt_depth_k), it is last frame's lit radiance
// at that pixel (prevRadiance), as in the screen-space trace. Otherwise the
// hit point is not visible in any image we have. A query the candidate cap aborted
// (RtHit.aborted, rt_common.hlsli: its closest hit is unknown) is such a hit too, never
// a miss: it occludes under the same gi_rt_fallback rule, and is never shaded from the
// screen or the radiance cache (its hit point would be a guess). gi_rt_fallback chooses:
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
// (r cache, g screen, b no radiance: fallback), as fractions of the rays; sharc_debug=6 the
// cache's contribution split by the leak test (r occluded cell, g clean, b thin surface).
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
#if SHARC_QUERY_PASS
#define SHARC_RT_QUERY 1
#include "sharc_common.hlsli"
#endif

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  g_rt_pass = kRtPassGI;   // aborted queries count as this pass's unknowns (rt_common.hlsli)
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
  float3 leak = 0;    // cache contribution (luminance): r occluded cell, g clean, b thin (sharc_debug=6)
  sharc_bin = (id.x >> 3) * 7u + (id.y >> 3) * 13u;
  // Rays reach sharc_gi_radius (R) for radiance only: a hit without radiance (no cache entry)
  // occludes with the fade of the base radius, as without the cache, so the longer rays add
  // bounce but never unexplained darkening (the first in-game run: 1.6% of the Adamantine
  // Forge's hidden hits are in the cache, and the 8 m occlusion darkened 47 per mille).
  const float R0 = max(shc.gi_base_radius, 0.01);
#endif
  for (uint r = 0; r < pc.rays; ++r) {
    float3 dir = cosine_hemisphere(N, rand01(seed), rand01(seed));
    float3 o = rt_ray_origin_o(org, P, N, dist * pa, dir);
#if SHARC_QUERY_PASS
    RtHit hh = rt_closest_hit(o, dir, R, cone);
    float t = hh.t;
    const bool aborted = hh.aborted;
#else
    bool aborted;
    float t = rt_closest(o, dir, R, cone, aborted);
#endif
    // Aborted: the closest hit is somewhere in [0, min(t, R)] (t: a committed hit, possibly
    // farther than the true one; -1: none yet). Its occlusion weight is then the fade's mean
    // over a uniform distance in that range, 1 - u^2 / 3 with u = min(t, R) / R (u = 1: 2/3).
    const float u = aborted ? (t >= 0.0 ? saturate(t / R) : 1.0) : 0.0;
    float k = saturate(t / R);
    float w = aborted ? 1.0 - u * u / 3.0 : 1.0 - k * k;
    float2 quv;
    float qd;
    bool onscreen = !aborted && t >= 0.0 && project(inv, o + dir * t, quv, qd) && all(quv >= 0.0) && all(quv <= 1.0);
    bool visible = false;
    if (onscreen) {
      int2 qp = min(int2(quv * float2(pc.full)), int2(pc.full) - 1);
      float sd = gDepth.Load(int3(qp, 0));
      // The surface at qp: rtOrigin's geometric normal (TLAS, else the depth buffer's) and
      // its TLAS surface along qp's ray; without rt_origin the depth buffer's own normal.
      const float4 oq = rt_origin_at(uint2(qp));
      const bool has_tlas = pc.rt_origin != 0u && oq.w < kOriginNoHit;
      const float3 ngq = pc.rt_origin != 0u ? oq.xyz : depth_geo_normal(qp, ray_at((float2(qp) + 0.5) / float2(pc.full)) * sd);
      if (sd > 0.0 && depth_same_surface(qp, qd, sd, ngq, has_tlas, oq.w, pc.gi_rt_tol)) {
        float3 L = prev_bounce_at(qp);
        if (all(isfinite(L))) {
          bounce += L * w;
          seen += L;
          nseen += 1.0;
          visible = true;
        }
      }
    }
    if (pc.rt_stats != 0) {
      // An aborted query counts as a hit (not a miss) in none of the three tiers: the
      // logged screen / hidden / off-screen shares then sum to 100% minus the unknowns
      // (also counted per pass by rt_count_unknown).
      uint bin = (id.x >> 3) * 7 + (id.y >> 3) * 13;
      stat_add(8, true, bin);
      stat_add(9, t >= 0.0 || aborted, bin);
      stat_add(10, visible, bin);
      stat_add(11, onscreen && !visible, bin);
      stat_add(12, t >= 0.0 && !aborted && !onscreen, bin);
    }
    if (t < 0.0 && !aborted) continue;
#if SHARC_QUERY_PASS
    if (aborted) {
      // Unknown: never the cache (the hit point is not known), straight to the fallback with
      // the base radius's fade, as a hidden hit there would get: its mean over a uniform
      // distance in [0, T], T = min(t, R) (R with nothing committed), of max(0, 1 - (x/R0)^2):
      // 1 - (T/R0)^2 / 3 for T <= R0, else (2/3) R0 / T.
      const float T = t >= 0.0 ? min(t, R) : R;
      tiers.z += 1.0;
      w = T <= R0 ? 1.0 - (T / R0) * (T / R0) / 3.0 : (2.0 / 3.0) * R0 / max(T, 1e-6);
    } else if (visible) {
      tiers.y += 1.0;
    } else {
      sharc_stat(kShGiHidden, onscreen);
      sharc_stat(kShGiOffscreen, !onscreen);
      float3 Lc = 0;
      bool near = false;
      SharcCell cell;
      cell.occluded = false;
      cell.thin = false;
      const bool ask = (shc.flags & (onscreen ? 1u : 2u)) != 0u;
      const bool found = ask && sharc_ray_hit(hh, o + dir * t, dir, t, cam_abs, cone, Lc, near, cell);
      sharc_stat(kShGiQueried, ask && !near);
      sharc_stat(kShGiNear, ask && near);
      sharc_stat(kShGiFound, found);
      // The leak test's share of the cache's GI contribution (luminance of L x w), counted
      // whether or not sharc_reject_occluded then dropped the entry; per pixel for view 6.
      const float lc = (found || cell.occluded) ? dot(Lc * w, float3(0.2126, 0.7152, 0.0722)) : 0.0;
      const uint lq = uint(clamp(lc, 0.0, 1e5) * 256.0 + 0.5);
      sharc_stat_sum(kShContrib, lq);
      sharc_stat_sum(kShContribOccluded, cell.occluded ? lq : 0u);
      sharc_stat_sum(kShContribThin, cell.thin && !cell.occluded ? lq : 0u);
      leak += cell.occluded ? float3(lc, 0, 0) : cell.thin ? float3(0, 0, lc) : float3(0, lc, 0);
      if (found) {
        bounce += Lc * w;
        occ += w;
        tiers.x += 1.0;
        continue;
      }
      tiers.z += 1.0;
      const float k0 = saturate(t / R0);
      w = 1.0 - k0 * k0;
      if (w <= 0.0) continue;   // beyond the base radius: a miss, as before
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
  if (shc.debug == 6u) bounce = leak;
#endif
  giRaw[id] = float4(bounce / n, saturate(1.0 - occ / n));
  giRawDepth[id] = d;
}
