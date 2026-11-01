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
#if RT_HIT_GI_DIRECT
#include "../rt/native_hit_material.hlsli"
// No box-light binding in this pipeline: the projector term is left out (counted), not the hit.
#define NATIVE_HIT_PROJECTOR_SKIP 1
// The screen's light policy and AO at hits (native_hit_direct.hlsli NATIVE_HIT_DLS_POLICY): lsSlots
// at b46 of this pipeline's push set (common/native_hit_gi_bindings.h).
#define NATIVE_HIT_DLS_POLICY 1
#define NATIVE_HIT_SLOTS_SET 0
#define NATIVE_HIT_SLOTS_BINDING 46
#include "../rt/native_hit_direct.hlsli"
#if RT_HIT_GI_SUN
// gi_hit_sun (gi_trace_rt_hit_sun.hlsl, docs/PLAN_M4_REFLECTIONS_GI.md M4.3): the sun at the
// same qualified hidden hits, rf_hit_shade's code (rt/native_hit_sun.hlsli) with the sun pass's
// disc sampling. The D-LS sun set (core/gi.cpp g_sun_dsl, write_sun_set) is set 3, as D-RF's.
#define BG3_SUN_SET 3
#include "../rt/bg3_sun.hlsli"
// The R0 roof rule (shared with rf_hit_shade) and the sun pass's disc sampling.
#define NATIVE_HIT_SUN_ROOF 1
#define NATIVE_HIT_SUN_DISC 1
#define NATIVE_HIT_SUN_STAT_BASE 320u
#define NATIVE_HIT_SUN_XBASE 325u
#include "../rt/native_hit_sun.hlsli"
#endif
#if RT_HIT_EMISSION
// rt_hit_emission (gi_trace_rt*_hit_*_em.hlsl, docs/PLAN_M4_REFLECTIONS_GI.md M4.4): the D-EM emissive
// scene as set 2 (the empty set of the hit-GI layouts; core/gi.cpp hit_gi_dispatch). A hidden hit on a
// captured emitter adds its record's emission where fallback0 added no bounce; with HIT_EM_FLAGS bit 0
// a screen hit adds it too: prevRadiance is the lit diffuse + specular before compose adds the effects
// target (gi_composite.hlsl), so the screen value has no emission either. SHaRC hits are not touched
// (the injection already adds the effects target, sharc_inject.hlsl).
#define EM_SET 2
#include "../rt/em_common.hlsli"
#include "../rt/native_hit_emission.hlsli"
#endif
#endif
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
    float3 o = RT_SECONDARY_ORIGIN(fp,org, P, N, dist * pa, dir);
#if SHARC_QUERY_PASS || RT_HIT_GI_DIRECT
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
#if RT_HIT_GI_DIRECT && RT_HIT_EMISSION
    if (visible && (HIT_EM_FLAGS & 1u) != 0u) {
      const HitEmission he = native_hit_emission(o, dir, t, o + dir * t, cone.f0 + cone.spread * t, cam2.c[13].xyz, HIT_EM_TOL);
      bounce += he.radiance * w;
      if (pc.rt_stats != 0u) hit_em_count(1u, he, dot(he.radiance * w, float3(0.2126, 0.7152, 0.0722)), fp.x ^ fp.y);
    }
#endif
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
#if RT_HIT_GI_DIRECT && RT_HIT_EMISSION
    // rt_hit_emission: the hit's own emission where fallback0 adds no bounce (an addition, the
    // occlusion is unchanged); the point/spot (and sun) term below adds to it.
    if (!visible && !aborted && t >= 0.0 && pc.gi_rt_fallback == 0u) {
      const HitEmission he = native_hit_emission(o, dir, t, o + dir * t, cone.f0 + cone.spread * t, cam2.c[13].xyz, HIT_EM_TOL);
      bounce += he.radiance * w;
      if (pc.rt_stats != 0u) hit_em_count(0u, he, dot(he.radiance * w, float3(0.2126, 0.7152, 0.0722)), fp.x ^ fp.y);
    }
#endif
#if RT_HIT_GI_DIRECT
    // Screen and successful SHaRC hits already returned their radiance above.
    // This replaces only fallback0's zero bounce; its native ambient occlusion
    // remains exactly the same. Direct-only radiance never replaces a full probe.
    if(!visible && !aborted && t>=0 && pc.gi_rt_fallback==0u && hit_material_publication_ready()){
      uint hit_bin=fp.x^fp.y;
      if(pc.rt_stats!=0u)stat_add(282u,true,hit_bin);
      NativeHitMaterial material=rt_hit_material(hh,cone.f0+cone.spread*t);
      if(pc.rt_stats!=0u){stat_add(283u,material.status==0u,hit_bin);hit_material_count(hh,material.status,hit_bin);}
      if(material.status==0u){
        // Counters 160-175 (native_hit_direct.hlsli g_hd_*; core/gi.cpp "GI hit direct lights").
        g_hd_stats=pc.rt_stats!=0u;g_hd_base=160u;g_hd_xbase=172u;g_hd_pbase=312u;g_hd_bin=hit_bin;g_hd_inst=hh.inst;g_hd_prim=hh.prim;
        const float3 hit_p=native_hit_position(hh,o+dir*t);
        NativeHitDirect direct=native_hit_direct(material,hit_p,-dir,rt_hit_normal(hh),cone.f0+cone.spread*t);
        if(pc.rt_stats!=0u)stat_add(284u,direct.status==HdOk,hit_bin);
        if(direct.status==HdOk){
#if RT_HIT_GI_SUN
          // gi_hit_sun: the sun at the same hit (counters 320-328, core/gi.cpp "GI hit sun"). It
          // joins the point/spot term in the same zero-fallback replacement (the fallback added no
          // bounce here, and the screen's prevRadiance / SHaRC paths above never reach this code),
          // so it is never counted twice. Unsupported (roof, unknown visibility, missing sun inputs:
          // Push::ao_direct bit 3) leaves the sun out: the point/spot term alone, as gi_hit_direct.
          const float3 hit_n=rt_hit_normal(hh);const float hit_f=cone.f0+cone.spread*t;
          g_hs_seed=pcg3d(uint3(fp,pc.frame*8u+r)^uint3(0x6a09e667u,0xbb67ae85u,0x3c6ef372u)).x;
          HdDirect sun;sun.radiance=0;sun.status=HdInvalid;
          if((pc.ao_direct&8u)!=0u)sun=native_hit_sun(material,hit_p,-dir,hit_n,hit_f);
          const bool sun_ok=sun.status==HdOk;
          if(pc.rt_stats!=0u)stat_add(327u,!sun_ok && sun.status!=kHitSunRoof && sun.status!=HdUnknownVisibility,hit_bin);
          // The AO-on-direct factor as rf_hit_shade applies it to the sun (native_hit_shade.hlsli):
          // mix(1, AO, m0.y) unless ao_direct=1 gives the traced sun its full value back.
          const bool sun_ao=sun_ok && any(sun.radiance>0) && !((pc.ao_direct&3u)==1u && (pc.ao_direct&12u)==12u);
          float hit_a=1.0;
          if(any(direct.native>0) || ((pc.ao_direct&3u)!=1u && any(direct.radiance>0)) || sun_ao){
            hit_a=native_hit_ao(hit_p,hit_n,hit_f,uint3(fp,pc.frame*8u+r));
            direct.radiance=native_hit_ao_direct(direct,hit_a);
          }
          const float3 sun_l=sun_ok?sun.radiance*(sun_ao?native_hit_ao_factor(hit_a):1.0):0.0.xxx;
          if(pc.rt_stats!=0u){
            uint sv=uint(clamp(dot(sun_l*w,float3(.2126,.7152,.0722)),0.0,8.0)*8192.0+0.5);
            uint ss=WaveActiveSum(sv);if(WaveIsFirstLane() && ss)rtStats.InterlockedAdd((328u*64u+(WaveReadLaneFirst(hit_bin)&63u))*4u,ss);
          }
#else
          // The screen's AO-on-direct factor (native_hit_ao_direct): its AO rays only where a
          // factor applies (lights at the game's visibility, or ao_direct != 1).
          if(any(direct.native>0) || ((pc.ao_direct&3u)!=1u && any(direct.radiance>0)))
            direct.radiance=native_hit_ao_direct(direct,native_hit_ao(hit_p,rt_hit_normal(hh),cone.f0+cone.spread*t,uint3(fp,pc.frame*8u+r)));
#endif
          if(pc.rt_stats!=0u){
            stat_add(285u,any(direct.radiance>0),hit_bin);
            // Zero direct split: 134 no positive unshadowed point/spot term (empty froxel, or
            // every listed light out of range/channel/cone), 135 positive terms all traced occluded.
            stat_add(134u,!any(direct.radiance>0) && direct.lights==0u,hit_bin);
            stat_add(135u,!any(direct.radiance>0) && direct.lights!=0u,hit_bin);
            // Luminance x 8192, clamped at 8: hit radiance is b13[0]-scaled (~1e-3), below the
            // old x 256 step (live S8: a sum of 0 could not tell zero from small).
            uint value=uint(clamp(dot(direct.radiance*w,float3(.2126,.7152,.0722)),0.0,8.0)*8192.0+0.5);
            uint sum=WaveActiveSum(value);if(WaveIsFirstLane() && sum)rtStats.InterlockedAdd((286u*64u+(WaveReadLaneFirst(hit_bin)&63u))*4u,sum);
          }
#if RT_HIT_GI_SUN
          bounce+=(direct.radiance+sun_l)*w;occ+=w;continue;
#else
          bounce+=direct.radiance*w;occ+=w;continue;
#endif
        }
      }
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
