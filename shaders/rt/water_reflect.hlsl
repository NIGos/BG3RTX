#ifdef WATER_SPEC_CASCADE
#define RT_SPEC_CASCADE 1
#endif
// M5.2 water reflections (rt_water_reflections=1, docs/PLAN_M5_WATER.md M5.2, core/water.cpp).
// Two entries, after the tiled lighting (run_post, after the GI composite: gDiffuse +
// gSpecular are the frame's final lit opaque radiance, pre-fog, pre-SSS):
//
// water_primary: one primary ray per pixel against the WATER TLAS (layer/water.cpp, the
//   previous submission's build, moved into this frame's space by the published camera
//   shift, common/water_scene.h). The water writes no depth (M5.1), so gDepth is the bed.
//   Classes (wT.y): 1 water in front of the bed, 2 water over an empty G-buffer pixel,
//   3 water at the bed within the derived tolerance (shoreline: not traced), 4 water BEHIND
//   the opaque depth beyond it: OCCLUDED water, not a violation. The forward water draw writes
//   no depth but depth-tests against the opaque D24S8, so it is hidden there (m5w2 live,
//   QuickSave_3784 = the Steel Watch Foundry interior: the "huge" sea plane 19 m below the
//   camera is hit by 95.75% of the primary rays and is behind the foundry floor at all of
//   them, 100% class 4, and the water pass output shows no water).
//   Tolerance (rt/depth_tol.hlsli): the bed's depth_tol at its own geometric normal over the
//   DLSS jitter (kJitterPx, the bed was rasterised at the jittered sample, our ray is at the
//   centre) plus kTlasRel (the water TLAS is the XFB of the same raster draws, compared with a
//   raster depth). Nothing tuned.
//
// water_reflect: for classes 1 and 2, the water's GEOMETRIC normal from the neighbours'
//   water hits (per axis the neighbour that continues the surface best, as depth_geo_normal):
//   the facet normal of the captured, displaced wave mesh. It is NOT the shading normal: the
//   forward FS adds three flow-mapped normal maps (7b44a567 _1205/_1207) that this pass cannot
//   reconstruct (a recompute of the FS, M5.3). Fallback where no neighbour on an axis hits:
//   world up (camera-relative world is Y-up, the water VS displaces Y), counted.
//   One mirror ray (Roughness_Base 0 at the S1 docks; the FS's own specular-AA floor 0.09 is a
//   filter, not a lobe of the geometric surface) into the scene TLAS (rt_common.hlsli
//   rt_closest, the alpha-tested scene). Hit radiance, tier (a) only (as D-RF): the hit
//   projects on screen, the depth there is the same surface (depth_same_surface at that
//   pixel's own geometric normal, k = 1), the camera sees its ray-facing side, and its
//   G-buffer class is not skin (class 3 keeps diffuse irradiance for the SSS pass):
//   L = gDiffuse + gSpecular there. Everything else is unresolved, never invented:
//   hidden on screen, off screen, a TLAS miss (sky: the TLAS has no sky and its roofs are cut
//   away, TRACK_D_REFLECTIONS), an aborted query.
//   Weight E = F0 + (1 - F0)(1 - N.V)^5 (Schlick, F0 = 0.16 Reflectance_Base^2, the FS's own
//   F0 convention _1470): the analytic stand-in for the FS's split-sum DFG LUT (set 2 b25,
//   sampled at (N.V, roughness)): the exact replacement uses the LUT (M5.3).
//
// Outputs: wOut rgb = L (pre-exposed lighting units, the water FS's _3379 * _3799 scale),
//   a = E > 0 where resolved (the composition input), else -tier (0 not traced); wT.xy =
//   (t_water, class). Tiers: 0 not traced, 1 screen hit, 2 hidden on screen, 3 off screen,
//   4 miss (sky), 5 aborted (candidate cap), 6 invalid ray. Normal fallbacks: counted only.
// Statistics (wStats, words; core/water.cpp logs "[water] reflections"). wStats is a
// DEVICE-LOCAL buffer copied to the host slot after the dispatches (core/water.cpp): the
// m5w2 live run had it in host-visible memory, where every wave's InterlockedAdd crossed
// PCIe (~4 per wave, ~340k per frame at 2560x1067, all on 4 words): the cause of the
// dispatches' 15.1-15.9 ms (the D-RF pass traces a full screen in 0.59 ms). Counters are
// also reduced per 16x16 group in groupshared (one global atomic per word per group):
//   0 ran, 1 pixels, 2 with bed depth, 3 water hits, 4 front, 5 empty G-buffer, 6 at bed,
//   7 behind, 8 rays, 9..14 tiers 1..6, 15 normal fallback, 16/17 sum E x 65536 (64-bit),
//   18/19 sum lum(L) x 4096 over tier 1, 20/21 sum lum(E L) x 4096 over tier 1, 22 skin
//   rejections (in tier 2), 23 shift applied (cam2 bound and the build camera known).
// Key off: nothing of this runs (no ordinary shader includes this file).
#include "../gi/gi_common.hlsli"
#ifdef WATER_HIT_SHADE_LAYOUT
#define RT_HIT_MATERIAL 1
#define RT_CASCADE_SUN_HIT 1
#define RT_HIT_INSTANCED 1
#define RT_HIT_WIND_RGB 1
#endif
#include "rt_common.hlsli"
#include "water_hit_shade.hlsli"

struct WaterParams {
  float4 cam_build;   // xyz the build's absolute camera, w F0
  uint4 flags;        // x bit 0 cam_build known, bit 1 cam2 bound; y unused; zw unused
};
struct Camera2 { float4 v[14]; };   // the lighting's set 0 b2: v[13].xyz = absolute camera (byte 208)
[[vk::binding(60, 0)]] ConstantBuffer<WaterParams> wp;
[[vk::binding(61, 0)]] RWByteAddressBuffer wStats;
[[vk::binding(62, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> wOut;
[[vk::binding(63, 0)]] [[vk::image_format("rg32f")]] RWTexture2D<float2> wT;
[[vk::binding(64, 0)]] RaytracingAccelerationStructure waterTlas;
#ifdef WATER_HIT_SHADE_LAYOUT
#define WATER_CAMERA waterCam2
#else
#define WATER_CAMERA cam2
#endif
[[vk::binding(65, 0)]] ConstantBuffer<Camera2> WATER_CAMERA;
#ifdef WATER_RR_HIT
// rt_water_rr_hitdist (core/water.cpp): RR's specular hit distance (core/gi.cpp g_rf_hit, DLSSD.SpecularHitDistance)
// is D-RF's at the G-buffer surface, which under water is the BED (the water writes no depth), so RR reprojects
// the water's reflection with the bed's (or no-hit) virtual depth. Written here for water pixels (classes 1, 2):
// the mirror ray's hit distance from the water surface where it resolves (tier 1, the image the compose
// replaces), else no hit (the probe stays: a distant reflection), as D-RF writes it (kNoHit 65504).
[[vk::binding(30, 0)]] [[vk::image_format("r16f")]] RWTexture2D<float> rfHitDist;
#endif

static const float kNoHit = -1.0;

// Group-reduced counters (words 1..22; word 0 and 23 are plain stores): wstat adds a wave's
// count into gsStat, wstat_flush (after a group barrier, every thread reaches it) adds the
// group's non-zero words to wStats.
static const uint kGroup = 16u;   // core/water.cpp kGroup: the dispatch size
#ifdef WATER_HIT_SHADE_LAYOUT
static const uint kGsWords = 32u;
#else
static const uint kGsWords = 23u;
#endif
groupshared uint gsStat[kGsWords];
void wstat_begin(uint gi) {
  if (gi < kGsWords) gsStat[gi] = 0u;
  GroupMemoryBarrierWithGroupSync();
}
void wstat(uint word, bool cond) {
  const uint n = WaveActiveCountBits(cond);
  if (WaveIsFirstLane() && n != 0u) InterlockedAdd(gsStat[word], n);
}
void wstat_flush(uint gi) {
  GroupMemoryBarrierWithGroupSync();
  if (gi < kGsWords && gsStat[gi] != 0u) wStats.InterlockedAdd(gi * 4u, gsStat[gi]);
}
// A wave's sum of v (quantised, >= 0) into the 64-bit counter at `word` (lo, hi).
void wsum64(uint word, float v, float scale, bool cond) {
  const uint q = cond ? uint(clamp(v, 0.0, 1e4) * scale + 0.5) : 0u;
  const uint s = WaveActiveSum(q);
  if (WaveIsFirstLane() && s != 0u) {
    uint old;
    wStats.InterlockedAdd(word * 4u, s, old);
    if (old + s < old) wStats.InterlockedAdd(word * 4u + 4u, 1u);
  }
}

// common/water_scene.h shift(): p_build = p_now + shift; zero when a camera is unknown or the
// difference is not finite / beyond 1e6 (the static-camera answer either way).
float3 water_shift() {
  if ((wp.flags.x & 3u) != 3u) return float3(0, 0, 0);
  const float3 d = WATER_CAMERA.v[13].xyz - wp.cam_build.xyz;
  return all(isfinite(d)) && all(abs(d) <= 1e6) ? d : float3(0, 0, 0);
}

// Closest water hit along unit direction `dir` from the camera (this frame's space), or kNoHit.
float water_hit(float3 dir, float3 shift) {
  RayDesc r;
  r.Origin = shift;
  r.Direction = dir;
  r.TMin = 0.0;
  r.TMax = 1e30;
  RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
  q.TraceRayInline(waterTlas, RAY_FLAG_NONE, 0xFF, r);
  q.Proceed();
  return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? q.CommittedRayT() : kNoHit;
}

float lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

[numthreads(kGroup, kGroup, 1)]
void water_primary(uint3 tid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  wstat_begin(gi);
  const uint2 fp = tid.xy;
  if (all(fp == 0)) wStats.Store(0, 1u);
  const bool inside = all(fp < pc.full);
  const uint2 p = min(fp, pc.full - 1);
  const float2 uv = (float2(p) + 0.5) / float2(pc.full);
  const float3 ray = ray_at(uv);
  const float3 dir = normalize(ray);
  const float d = gDepth.Load(int3(p, 0));
  const float3 shift = water_shift();
  const float tw = inside ? water_hit(dir, shift) : kNoHit;
  uint cls = 0u;
  if (tw > 0.0) {
    if (!(d > 0.0)) {
      cls = 2u;
    } else {
      const float3 P = ray * d;
      const float to = length(P);
      const float pa = pixel_angle(uv);
      const float3 ng = depth_geo_normal(int2(p), P);
      const float tol = depth_tol(to, pa, dot(ng, P) / max(to, 1e-20), kJitterPx, 1.0) + kTlasRel * to;
      cls = tw < to - tol ? 1u : tw > to + tol ? 4u : 3u;
    }
  }
  wstat(1, inside);
  wstat(2, inside && d > 0.0);
  wstat(3, inside && tw > 0.0);
  wstat(4, inside && cls == 1u);
  wstat(5, inside && cls == 2u);
  wstat(6, inside && cls == 3u);
  wstat(7, inside && cls == 4u);
  wstat_flush(gi);
  if (all(fp == 0)) wStats.Store(23 * 4, (wp.flags.x & 3u) == 3u ? 1u : 0u);
  if (inside) wT[fp] = float2(tw, float(cls));
}

// The water position along pixel q's ray (this frame's space), or false (no visible water).
bool water_pos(int2 q, out float3 P, out float t) {
  P = 0.0.xxx;
  t = kNoHit;
  if (any(q < 0) || any(q >= int2(pc.full))) return false;
  const float2 v = wT[q];
  const uint c = uint(v.y + 0.5);
  if (c != 1u && c != 2u) return false;
  t = v.x;
  P = normalize(ray_at((float2(q) + 0.5) / float2(pc.full))) * t;
  return true;
}

[numthreads(kGroup, kGroup, 1)]
void water_reflect(uint3 tid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  wstat_begin(gi);
  g_rt_pass = kRtPassRefl;
  const uint2 fp = tid.xy;
  const bool inside = all(fp < pc.full);
  float3 P;
  float t;
  const bool water = inside && water_pos(int2(fp), P, t);
  uint tier = 0u;
  bool fallback = false, skin = false;
  float E = 0.0, hit_t = -1.0;
  float3 L = 0.0.xxx;
#ifdef WATER_HIT_SHADE_LAYOUT
  bool shaded=false, shade_attempt=false;
  uint shade_reason=0;
#endif
  if (water) {
    // Geometric normal: per axis the neighbour whose water hit continues the surface best.
    float3 Pl, Pr, Pu, Pd;
    float tl, tr, tu, td;
    const bool hl = water_pos(int2(fp) - int2(1, 0), Pl, tl), hr = water_pos(int2(fp) + int2(1, 0), Pr, tr);
    const bool hu = water_pos(int2(fp) - int2(0, 1), Pu, tu), hd = water_pos(int2(fp) + int2(0, 1), Pd, td);
    const bool okx = hl || hr, oky = hu || hd;
    const float3 dx = (hr && (!hl || abs(tr - t) < abs(t - tl))) ? Pr - P : P - Pl;
    const float3 dy = (hd && (!hu || abs(td - t) < abs(t - tu))) ? Pd - P : P - Pu;
    float3 N = cross(dy, dx);
    const float ln = length(N);
    fallback = !okx || !oky || !(ln > 1e-20);
    N = fallback ? float3(0, 1, 0) : N / ln;
    const float3 V = -normalize(P);
    if (dot(N, V) < 0.0) N = -N;   // the side the camera sees
    const float ndv = saturate(dot(N, V));
    const float f0 = wp.cam_build.w;
    E = f0 + (1.0 - f0) * pow(1.0 - ndv, 5.0);
    const float3 R = reflect(-V, N);
    const float dist = length(P);
    const float pa = pixel_angle((float2(fp) + 0.5) / float2(pc.full));
    // The scene TLAS has no water (its own TLAS): one footprint off the surface is enough
    // for the bed and dock geometry at the waterline.
    const float3 o = P + N * (dist * pa);
    RtCone cone = {dist * pa, pa};
    bool aborted;
    float tmax = pc.rf_tmax;
    if (!all(isfinite(o)) || !all(isfinite(R))) {
      tier = 6u;
    } else {
      #ifdef WATER_HIT_SHADE_LAYOUT
      RtHit hh=rt_closest_hit(o,R,tmax,cone);
      const float th=hh.t; aborted=hh.aborted;
#else
      const float th = rt_closest(o, R, tmax, cone, aborted);
#endif
      if (aborted) tier = 5u;
      else if (th < 0.0) tier = 4u;
      else {
        hit_t = th;
        const float3 H = o + R * th;
        float2 quv;
        float qd;
        const bool onscreen = project(frustum_inverse(), H, quv, qd) && all(quv >= 0.0) && all(quv <= 1.0);
        tier = 3u;
        if (onscreen) {
          tier = 2u;
          const int2 qp = min(int2(quv * float2(pc.full)), int2(pc.full) - 1);
          const float sd = gDepth.Load(int3(qp, 0));
          if (sd > 0.0) {
            const float3 Pq = ray_at((float2(qp) + 0.5) / float2(pc.full)) * sd;
            float3 ngq = depth_geo_normal(qp, Pq);
            if (dot(ngq, Pq) > 0.0) ngq = -ngq;   // towards the camera
            const uint cls = uint(gMaterial.Load(int3(qp, 0)).a * 255.0 + 0.5) & 7u;
            skin = cls == kMatSkin;
            if (depth_same_surface(qp, qd, sd, ngq, false, 0.0, 1.0) && dot(ngq, R) < 0.0 && !skin) {
              const float3 Lq = gDiffuse[qp] + gSpecular[qp];
              if (all(isfinite(Lq))) {
                L = max(Lq, 0.0);
                tier = 1u;
              }
            }
          }
        }
#ifdef WATER_HIT_SHADE_LAYOUT
        if(kWaterHitShade!=0u && (wp.flags.y & 1u)!=0u && (tier==2u || tier==3u)) {
          shade_attempt=true;
          shade_reason=water_hit_shade(hh,H,R,cone,fp,L);
          shaded=shade_reason==0u;
        }
#endif
      }
    }
  }
  wstat(8, water && tier != 6u);
  [unroll] for (uint k = 1u; k <= 6u; ++k) wstat(8u + k, water && tier == k);
  wstat(15, water && fallback);
  wstat(22, water && tier == 2u && skin);
#ifdef WATER_HIT_SHADE_LAYOUT
  wstat(24,shade_attempt); wstat(25,shaded && tier==2u); wstat(26,shaded && tier==3u);
  [unroll] for(uint reason=1u;reason<=4u;++reason) wstat(26u+reason,shade_attempt && shade_reason==reason);
#endif
  wstat_flush(gi);
  wsum64(16, E, 65536.0, water && tier != 6u);
  wsum64(18, lum(L), 4096.0, tier == 1u);
  wsum64(20, lum(E * L), 4096.0, tier == 1u);
#ifdef WATER_HIT_SHADE_LAYOUT
  const bool resolved=tier==1u || shaded;
#else
#define resolved tier == 1u
#endif
  // wT is only read here (the neighbours' hits): the tier goes into wOut.a.
  if (inside) wOut[fp] = resolved ? float4(L, E) : float4(0, 0, 0, -float(tier));
#ifdef WATER_RR_HIT
  if (water) rfHitDist[fp] = resolved ? min(hit_t, 65504.0) : 65504.0;
#endif
}
