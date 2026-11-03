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
#if GI_PREV_RADIANCE_GUARD || GI_SPEC_GUARDS
#include "gi_prev_guard.hlsli"   // gi_prev_radiance_guard: guarded last-frame radiance reads
#endif
// Limit the experiment to GI closest rays; sun/local visibility stays unchanged.
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
#if RT_HIT_GI_DIRECT
// Non-finite guard of the qualified-hit bounce (rt_stats=1 counters 417-418, core/gi.cpp kGiHitNfFirst, on
// the "GI hit direct" line). giRaw is RGBA16F and gi_composite.hlsl reverts a non-finite pixel to the game's
// diffuse: one Inf or NaN there drops the bounce of every pixel whose 3x3 history gather reads it, while
// the occlusion (alpha) stays, so it shows as darkening, never as a non-finite scene colour.
//   417: qualified-hit rays whose term is not finite and >= 0: the ray takes fallback0 (occlusion w, no
//        bounce: exactly the key-off ray), never the poisoned sum;
//   418: pixels whose mean bounce exceeds the half-float maximum 65504 (the giRaw encoding), stored as
//        65504 instead of +Inf (the energy above the encoding limit is lost, counted here).
static const uint kGiHitNfFirst = 417u;
static const float kGiRawHalfMax = 65504.0;
bool gi_hit_term_ok(float3 c, uint bin) {
  const bool ok = all(isfinite(c)) && all(c >= 0.0);
  if (pc.rt_stats != 0u) stat_add(kGiHitNfFirst, !ok, bin);
  return ok;
}
#endif

#if defined(RT_SPEC_GI_FAR)
// Variant migration step S2b (docs/BUILD_AND_PACKAGE_EFFICIENCY.md "Migration plan"): one module for both gi_far
// values, specialization constant 1007 (core/gi.cpp refresh: gi_far). The far code below is compiled in and
// selected by the constant; the RT_GI_FAR blocks keep the old define build (the equivalence reference).
#if RT_GI_FAR
#error RT_SPEC_GI_FAR replaces RT_GI_FAR
#endif
[[vk::constant_id(1007)]] const uint kSpecGiFar = 0u;
#endif
#if RT_GI_FAR || defined(RT_SPEC_GI_FAR)
// gi_far (docs/TRACK_C_GI.md "Far-field sun bounce"; core/gi.cpp hit_gi_dispatch, request flag kGiFar on
// the gi_hit_sun files): rays of Push::radius (gi_far_m) instead of gi_rt_radius, the per-ray rule of
// gi_far.hlsli. Needs RT_HIT_GI_DIRECT (the qualified-hit code below). Push::radius is gi_trace's screen
// march radius, unused by this pass otherwise; the base radius R0 stays gi_rt_radius (SHaRC: its base).
#if !RT_HIT_GI_DIRECT
#error RT_GI_FAR needs RT_HIT_GI_DIRECT
#endif
#include "gi_far.hlsli"
// rt_stats=1 counters 372-381 (core/gi.cpp kGiFarFirst, "GI far"): per thread over its rays, then one
// wave sum and one atomic per 8x8 group per counter (the stats buffer is host-visible). The old rule
// (gi_hit_direct / gi_hit_sun without gi_far: every hit occludes with w0 within R0) is evaluated on the
// same rays, so ON and OFF of one frame are compared without a second run.
//   0 rays; beyond R0: 1 screen hits, 2 qualified sun-lit hits, 3 of which outside the froxel volume,
//   4 hits kept as native ambient; x 8192: 5 occlusion sum, 6 the old rule's, 7 bounce luminance sum,
//   8 the old rule's, 9 ambient kept at qualified hits (w x rho).
//   10 qualified sun-lit hits beyond R0 left out by the roof rule.
static const uint kGiFarStatFirst = 372u, kGiFarStatCount = 11u;
#if RT_HIT_GI_SUN
// The roof rule for the far GI ray itself (the sun ray from the hit has its own, native_hit_sun.hlsli):
// the TLAS has no roof the camera cut away for GI rays (rt_cascade_casters puts them in mask 0x02, seen
// by sun rays only), so a ray from a roofed room would reach sunlit surfaces through the missing roof.
// A segment [0, t] through a padded roof volume (gi/sun_roof.hlsli's boxes from the sun set b8; any
// ray from inside one) takes no far radiance from a hidden hit; nor does any hit while the volumes are
// unknown (no trust slot: Push::ls_flags bit 25; no asset DB roofs: header flag 2). Conservative: the
// native ambient stays for those rays.
bool gi_far_roof_clear(float3 o, float3 d, float t) {
  if ((pc.ls_flags & (1u << 25)) == 0u || (st_u(4) & 2u) == 0u) return false;
  const float3 cam_abs = cam2.c[13].xyz;
  const uint n = min(st_u(0), SUN_MAX_VOLUMES);
  for (uint i = 0; i < n; ++i) {
    const uint a = 192u + i * 48u;   // kSunHeader
    const float4 c = st_f4(a), q = st_f4(a + 16u);
    const float3 h = st_f4(a + 32u).xyz;
    const float3 lo = qrot_inv(q, o - (c.xyz - cam_abs)), ld = qrot_inv(q, d);
    const float3 inv = 1.0 / select(abs(ld) > 1e-8, ld, select(ld >= 0.0, 1e-8, -1e-8));
    const float3 t1 = (-h - lo) * inv, t2 = (h - lo) * inv, tn = min(t1, t2), tf = max(t1, t2);
    const float t0 = max(max(max(tn.x, tn.y), tn.z), 0.0), t3 = min(min(tf.x, tf.y), tf.z);
    if (t3 >= t0 && t0 <= t) return false;
  }
  return true;
}
#endif
static uint g_far_stat[kGiFarStatCount];
groupshared uint gs_far_stat[kGiFarStatCount];
void far_stat(uint k, float v) { g_far_stat[k] += uint(clamp(v, 0.0, 8.0) * 8192.0 + 0.5); }
void gi_trace_px(uint3 tid);
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gix : SV_GroupIndex) {
  for (uint k = 0; k < kGiFarStatCount; ++k) g_far_stat[k] = 0u;
  gi_trace_px(tid);
#if defined(RT_SPEC_GI_FAR)
  if (pc.rt_stats != 0u && kSpecGiFar != 0u) {   // uniform (a push constant and the constant)
#else
  if (pc.rt_stats != 0u) {   // uniform (a push constant): the barriers are reached by the whole group
#endif
    if (gix < kGiFarStatCount) gs_far_stat[gix] = 0u;
    GroupMemoryBarrierWithGroupSync();
    for (uint k = 0; k < kGiFarStatCount; ++k) {
      const uint s = WaveActiveSum(g_far_stat[k]);
      if (WaveIsFirstLane() && s) InterlockedAdd(gs_far_stat[k], s);
    }
    GroupMemoryBarrierWithGroupSync();
    const uint bin = (gid.x * 7u + gid.y * 13u) & 63u;
    if (gix < kGiFarStatCount && gs_far_stat[gix]) rtStats.InterlockedAdd(((kGiFarStatFirst + gix) * 64u + bin) * 4u, gs_far_stat[gix]);
  }
}
void gi_trace_px(uint3 tid) {
#else
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
#endif
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
#if RT_GI_FAR
#ifdef GI_FAR_M
  // Offline only (frame_replay --define GI_FAR_M=6): a replay pack's push holds the base pass's radius.
  const float R = max(float(GI_FAR_M), pc.gi_rt_radius);
#else
  const float R = max(pc.radius, pc.gi_rt_radius);
#endif
#if SHARC_QUERY_PASS
  const float RB = max(shc.gi_base_radius, 0.01);   // the base radius (SHaRC's gi_base_radius)
#else
  const float RB = max(pc.gi_rt_radius, 0.01);      // the base radius (gi_rt_radius)
#endif
#elif defined(RT_SPEC_GI_FAR)
  float R;
  float RB = 0.0;
  // Keep far-only arithmetic inside the specialization branch so it disappears when frozen off.
  if (kSpecGiFar != 0u) {
    R = max(pc.radius, pc.gi_rt_radius);
#if SHARC_QUERY_PASS
    RB = max(shc.gi_base_radius, 0.01);
#else
    RB = max(pc.gi_rt_radius, 0.01);
#endif
  } else {
    R = pc.gi_rt_radius;
  }
#else
  const float R = pc.gi_rt_radius;
#endif

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
#if RT_GI_FAR
    // w: the far fade (radiance hits); fr.w0: the base fade (every other hit, as without gi_far).
    const GiFarRay fr = gi_far_ray(t, aborted, R, RB);
    if (pc.rt_stats != 0u) {
      g_far_stat[0] += 1u;
      if (t >= 0.0 || aborted) far_stat(6u, fr.w0);   // the old rule occludes every hit with w0
    }
#elif defined(RT_SPEC_GI_FAR)
    GiFarRay fr = (GiFarRay)0;
    if (kSpecGiFar != 0u) {
      fr = gi_far_ray(t, aborted, R, RB);
      if (pc.rt_stats != 0u) {
        g_far_stat[0] += 1u;
        if (t >= 0.0 || aborted) far_stat(6u, fr.w0);
      }
    }
#endif
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
#if RT_GI_FAR || defined(RT_SPEC_GI_FAR)
#if defined(RT_SPEC_GI_FAR)
          if (kSpecGiFar != 0u && pc.rt_stats != 0u) {
#else
          if (pc.rt_stats != 0u) {
#endif
            far_stat(7u, gi_far_luminance(L * w));
            if (t <= RB) far_stat(8u, gi_far_luminance(L * fr.w0));
            else g_far_stat[1] += 1u;
          }
#endif
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
#if defined(RT_SPEC_GI_FAR)
      if (kSpecGiFar == 0u) {
        const float k0 = saturate(t / R0);
        w = 1.0 - k0 * k0;
        if (w <= 0.0) continue;   // beyond the base radius: a miss, as before
      }
#elif !RT_GI_FAR
      const float k0 = saturate(t / R0);
      w = 1.0 - k0 * k0;
      if (w <= 0.0) continue;   // beyond the base radius: a miss, as before
#endif
      // gi_far: w stays the far fade for a qualified hit below; the fallback uses fr.w0 (0 beyond R0).
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
        // Reuse the geometric normal across direct, sun and AO only in the opt-in path.
        float3 compact_hit_n=0.0.xxx;
        if(RT_COMPACT_HIT_STATE_ON)compact_hit_n=rt_hit_normal(hh);
        NativeHitDirect direct=native_hit_direct(material,hit_p,-dir,RT_COMPACT_HIT_STATE_ON?compact_hit_n:rt_hit_normal(hh),cone.f0+cone.spread*t);
#if RT_GI_FAR
        // Off screen, outside the froxel volume (no light list exists there): the point/spot term is
        // declared 0 and the sun alone lights the hit (gi_far.hlsli kGiFarLocalOutside).
        const uint far_local=direct.status==HdOk?kGiFarLocalKnown:(!onscreen && direct.status==kHitDirectSource)?kGiFarLocalOutside:kGiFarLocalUnknown;
        if(far_local==kGiFarLocalOutside){direct.radiance=0;direct.native=0;direct.lights=0u;}
#elif defined(RT_SPEC_GI_FAR)
        uint far_local=kGiFarLocalUnknown;
        if(kSpecGiFar!=0u){
          far_local=direct.status==HdOk?kGiFarLocalKnown:(!onscreen && direct.status==kHitDirectSource)?kGiFarLocalOutside:kGiFarLocalUnknown;
          if(far_local==kGiFarLocalOutside){direct.radiance=0;direct.native=0;direct.lights=0u;}
        }
#endif
        if(pc.rt_stats!=0u)stat_add(284u,direct.status==HdOk,hit_bin);
#if RT_GI_FAR
        if(far_local!=kGiFarLocalUnknown){
#elif defined(RT_SPEC_GI_FAR)
        if(kSpecGiFar!=0u ? far_local!=kGiFarLocalUnknown : direct.status==HdOk){
#else
        if(direct.status==HdOk){
#endif
#if RT_HIT_GI_SUN
          // gi_hit_sun: the sun at the same hit (counters 320-328, core/gi.cpp "GI hit sun"). It
          // joins the point/spot term in the same zero-fallback replacement (the fallback added no
          // bounce here, and the screen's prevRadiance / SHaRC paths above never reach this code),
          // so it is never counted twice. Unsupported (roof, unknown visibility, missing sun inputs:
          // Push::ao_direct bit 3) leaves the sun out: the point/spot term alone, as gi_hit_direct.
          const float3 hit_n=RT_COMPACT_HIT_STATE_ON?compact_hit_n:rt_hit_normal(hh);const float hit_f=cone.f0+cone.spread*t;
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
            direct.radiance=native_hit_ao_direct(direct,native_hit_ao(hit_p,RT_COMPACT_HIT_STATE_ON?compact_hit_n:rt_hit_normal(hh),cone.f0+cone.spread*t,uint3(fp,pc.frame*8u+r)));
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
#if RT_GI_FAR
#if RT_HIT_GI_SUN
          // Beyond R0 the hit needs the sun (gi_far_hit) and a roof-clear ray: else the fallback.
          const bool far_roof=t>RB && sun_ok && !gi_far_roof_clear(o,dir,t);
          if(pc.rt_stats!=0u && far_roof)g_far_stat[10]+=1u;
          const bool far_sun=sun_ok && !far_roof;const float3 far_sun_l=sun_l;
#else
          const bool far_sun=false;const float3 far_sun_l=0.0.xxx;
#endif
          const float far_rho=gi_far_luminance(material.scattering.value.Rd);
          const GiFarHit fh=gi_far_hit(fr,t,RB,far_local,direct.radiance,far_sun,far_sun_l,far_rho);
          if(fh.radiance && gi_hit_term_ok(fh.bounce,hit_bin)){
            if(pc.rt_stats!=0u){
              far_stat(5u,fh.occ);far_stat(7u,gi_far_luminance(fh.bounce));far_stat(9u,fr.w*saturate(far_rho));
              if(t>RB){g_far_stat[2]+=1u;g_far_stat[3]+=far_local==kGiFarLocalOutside?1u:0u;}
              // The old rule's bounce on this ray: the complete local term (+ the sun) within R0, with w0.
              else if(far_local==kGiFarLocalKnown)far_stat(8u,gi_far_luminance((direct.radiance+far_sun_l)*fr.w0));
            }
            bounce+=fh.bounce;occ+=fh.occ;continue;
          }
          // No known radiance (sun unsupported beyond R0, or the local term outside the froxels without
          // the sun): the fallback below, with the base fade.
#elif defined(RT_SPEC_GI_FAR)
          if(kSpecGiFar!=0u){
#if RT_HIT_GI_SUN
          // Beyond R0 the hit needs the sun (gi_far_hit) and a roof-clear ray: else the fallback.
          const bool far_roof=t>RB && sun_ok && !gi_far_roof_clear(o,dir,t);
          if(pc.rt_stats!=0u && far_roof)g_far_stat[10]+=1u;
          const bool far_sun=sun_ok && !far_roof;const float3 far_sun_l=sun_l;
#else
          const bool far_sun=false;const float3 far_sun_l=0.0.xxx;
#endif
          const float far_rho=gi_far_luminance(material.scattering.value.Rd);
          const GiFarHit fh=gi_far_hit(fr,t,RB,far_local,direct.radiance,far_sun,far_sun_l,far_rho);
          if(fh.radiance && gi_hit_term_ok(fh.bounce,hit_bin)){
            if(pc.rt_stats!=0u){
              far_stat(5u,fh.occ);far_stat(7u,gi_far_luminance(fh.bounce));far_stat(9u,fr.w*saturate(far_rho));
              if(t>RB){g_far_stat[2]+=1u;g_far_stat[3]+=far_local==kGiFarLocalOutside?1u:0u;}
              // The old rule's bounce on this ray: the complete local term (+ the sun) within R0, with w0.
              else if(far_local==kGiFarLocalKnown)far_stat(8u,gi_far_luminance((direct.radiance+far_sun_l)*fr.w0));
            }
            bounce+=fh.bounce;occ+=fh.occ;continue;
          }
          // No known radiance (sun unsupported beyond R0, or the local term outside the froxels without
          // the sun): the fallback below, with the base fade.
          }else{
#if RT_HIT_GI_SUN
          if(gi_hit_term_ok(direct.radiance+sun_l,hit_bin)){bounce+=(direct.radiance+sun_l)*w;occ+=w;continue;}
#else
          if(gi_hit_term_ok(direct.radiance,hit_bin)){bounce+=direct.radiance*w;occ+=w;continue;}
#endif
          }
#elif RT_HIT_GI_SUN
          if(gi_hit_term_ok(direct.radiance+sun_l,hit_bin)){bounce+=(direct.radiance+sun_l)*w;occ+=w;continue;}
#else
          if(gi_hit_term_ok(direct.radiance,hit_bin)){bounce+=direct.radiance*w;occ+=w;continue;}
#endif
          // Not finite: fallback0 below (the key-off ray).
        }
      }
    }
#endif
#if RT_GI_FAR
    if (visible) {
      occ += w;
      if (pc.rt_stats != 0u) far_stat(5u, w);
    } else if (pc.gi_rt_fallback != 1) {
      // As without gi_far: the base fade, so a hit beyond R0 without known radiance keeps the native ambient.
      occ += fr.w0;
      wfall += fr.w0;
      if (pc.rt_stats != 0u) {
        far_stat(5u, fr.w0);
        g_far_stat[4] += !aborted && t > RB ? 1u : 0u;
      }
    }
#elif defined(RT_SPEC_GI_FAR)
    if (kSpecGiFar != 0u) {
      if (visible) {
        occ += w;
        if (pc.rt_stats != 0u) far_stat(5u, w);
      } else if (pc.gi_rt_fallback != 1) {
        occ += fr.w0;
        wfall += fr.w0;
        if (pc.rt_stats != 0u) {
          far_stat(5u, fr.w0);
          g_far_stat[4] += !aborted && t > RB ? 1u : 0u;
        }
      }
    } else {
      if (visible) occ += w;
      else if (pc.gi_rt_fallback != 1) {
        occ += w;
        wfall += w;
      }
    }
#else
    if (visible) occ += w;
    else if (pc.gi_rt_fallback != 1) {
      occ += w;
      wfall += w;
    }
#endif
  }
  if (pc.gi_rt_fallback == 2 && nseen > 0.0) bounce += seen / nseen * wfall;
  float n = float(max(pc.rays, 1u));
#if SHARC_QUERY_PASS
  if (shc.debug == 4u) bounce = tiers;
  if (shc.debug == 6u) bounce = leak;
#endif
#if RT_HIT_GI_DIRECT
  float3 gi_out = bounce / n;   // every term finite and >= 0: only an overflow can exceed the half maximum
  const bool gi_over = !all(gi_out <= kGiRawHalfMax);
  if (pc.rt_stats != 0u) stat_add(kGiHitNfFirst + 1u, gi_over, (id.x >> 3) * 7 + (id.y >> 3) * 13);
  gi_out = select(gi_out <= kGiRawHalfMax, gi_out, kGiRawHalfMax.xxx);
  giRaw[id] = float4(gi_out, saturate(1.0 - occ / n));
#else
  giRaw[id] = float4(bounce / n, saturate(1.0 - occ / n));
#endif
  giRawDepth[id] = d;
}
