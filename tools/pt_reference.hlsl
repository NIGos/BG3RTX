// Offline opaque path-traced reference (tools/frame_replay.md "PT reference", docs/PT_FEASIBILITY_OFFLINE.md).
// NOT a game shader: only tools/frame_replay --pt-reference compiles it, over a replay pack, with the
// gi_hit_direct pass contract (common/replay_pack.h kGiHitDirect; it reads a subset of it).
//
// Per GI pixel (the production pass's sample position, raster G-buffer primary point and normal) it
// estimates the same quantity as giRaw.rgb: the cosine-weighted mean INCIDENT radiance from other
// surfaces, but with no radius, no screen radiance and no fallback: N-bounce paths of
//   NEE at every vertex: the froxel's point/spot lights (rt/native_hit_direct.hlsli, the screen's light
//     policy, RT shadow rays) + the sun (cam.v0/v1, b13[0], one disc-sampled RT ray), through the
//     declared FresnelBlend BSDF (common/transport_bsdf.h: diffuse + GGX specular);
//   continuation: the same BSDF's sample (common/native_hit_bounce.h hb_continuation), Russian
//     roulette from vertex PT_RR_START (unbiased);
//   material: the native hit material where it qualifies (rt_hit_material: VT layers), else the
//     declared fallback PT_ALBEDO / PT_ROUGH / reflectance 0.5 (a pack without the VT cache).
// Not modelled (reported by the harness): sky/environment radiance (a miss returns 0, its share is
// the miss fraction), emission, projector/box lights, cloud shadows, the atmosphere LUT (PT_SUN_T
// approximates the transmittance), roofs the camera cut away (absent from the TLAS), transmission.
//
// Push::debug selects what one dispatch writes (the same seeds: every mode traces the same paths):
// ("near": first hit t < gi_rt_radius, the live pass's ray length; w its fade 1 - (t/R)^2)
//   rgb (giRaw)                          a (giRaw)                    r (giRawDepth, lum L of ...)
//   0 total L                            miss fraction                first hit visible on screen, near
//   1 L at the first vertex (1 bounce)   visible-on-screen fraction   first hit visible, beyond
//   2 screen radiance at visible hits    qualified hit material frac  first hit on screen but hidden, near
//   3 L x w, near first hits             near fraction                first hit hidden, beyond
//   4 screen radiance x w, near visible  aborted (unknown) fraction   first hit off screen
//   5 diagnostics: r vertices without a  mean path vertices           the sun's part
//     froxel light list, g sun ray
//     unknown, b fallback material
//   6 L x w, near hits not visible       hidden-on-screen fraction    the local lights' part
//     (what fallback0 leaves at 0)
// PT_FURNACE_LE > 0 (CTest): every vertex also emits PT_FURNACE_LE; PT_LAMBERT=1 scatters with a
// Lambertian of albedo PT_ALBEDO (common/transport_bsdf.h tb_sample) for the closed-form furnace.
#define RT_HIT_MATERIAL 1
#include "gi/gi_common.hlsli"
#include "rt/rt_common.hlsli"
#include "rt/native_hit_material.hlsli"
#define NATIVE_HIT_PROJECTOR_SKIP 1
#define NATIVE_HIT_DLS_POLICY 1
#define NATIVE_HIT_SLOTS_SET 0
#define NATIVE_HIT_SLOTS_BINDING 46
#include "rt/native_hit_direct.hlsli"
#include "../common/native_hit_bounce.h"

#ifndef PT_SPP
#define PT_SPP 4
#endif
#ifndef PT_BOUNCES
#define PT_BOUNCES 8
#endif
#ifndef PT_RR_START
#define PT_RR_START 3
#endif
#ifndef PT_ALBEDO
#define PT_ALBEDO 0.5
#endif
#ifndef PT_ROUGH
#define PT_ROUGH 0.5
#endif
#ifndef PT_TMAX
#define PT_TMAX 300.0
#endif
#ifndef PT_SUN_T
#define PT_SUN_T float3(1, 1, 1)
#endif
#ifndef PT_SUN
#define PT_SUN 1
#endif
#ifndef PT_LIGHTS
#define PT_LIGHTS 1
#endif
#ifndef PT_FURNACE_LE
#define PT_FURNACE_LE 0.0
#endif
#ifndef PT_LAMBERT
#define PT_LAMBERT 0
#endif

float pt_lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// The sun's radiance this frame without the atmosphere LUT (bg3_sun.hlsli bg3_sun_colour): the
// atmosphere path's colour x intensity x PT_SUN_T (the transmittance the pack does not carry), else
// _m2; x b13[0] as native_hit_sun. Cloud shadows: 1 (their maps are not in the pack).
float3 pt_sun_radiance() {
  float3 c = (cam.x[5].x > 0.0 && cam.x[13].w == 0.0) ? cam.x[13].xyz * cam.x[5].y * PT_SUN_T : cam.v1.xyz;
  return c * asfloat(lScale.Load(0));
}

NativeHitMaterial pt_fallback_material(float3 ng) {
  NtInput input;
  input.linear_albedo = PT_ALBEDO.xxx; input.roughness = PT_ROUGH; input.reflectance = 0.5; input.metalness = 0.0;
  input.material_class = 0; input.evidence = NtSamplesKnown | NtLinearColorKnown | NtOpaqueCoverageKnown | NtLobesKnown;
  NativeHitMaterial m = (NativeHitMaterial)0;
  m.parameters = nt_class0_parameters(input); m.scattering = nt_to_fresnel_blend(m.parameters);
  m.normal = ng; m.channel = 0; m.status = 0;
  return m;
}

struct PtPath {
  float3 total, first, sun, local;
  uint cls;          // first hit: 0 miss, 1 visible on screen, 2 hidden on screen, 3 off screen, 4 aborted
  float t1, w;       // first hit distance, live fade weight
  float3 screen;     // prev_bounce_at at a visible first hit
  bool qualified;
  float vertices, nolist, sununknown, fallback;
};

PtPath pt_trace(uint2 fp, float3 P, float3 N, float4 org, float dist, float pa, float3x3 inv, inout uint seed) {
  PtPath r = (PtPath)0;
  float3 dir = cosine_hemisphere(N, rand01(seed), rand01(seed));
  float3 o = RT_SECONDARY_ORIGIN(fp, org, P, N, dist * pa, dir);
  RtCone cone = {dist * pa * 2.0, pa * 2.0};
  float3 thr = 1.0;
  const float3 sunL = normalize(cam.v0.xyz);
  const float3 sunE = PT_SUN ? pt_sun_radiance() : 0.0.xxx;
  [loop] for (uint b = 1; b <= PT_BOUNCES; ++b) {
    RtHit hh = rt_closest_hit(o, dir, PT_TMAX, cone);
    if (hh.aborted) { if (b == 1) r.cls = 4; break; }
    if (hh.t < 0.0) break;   // a miss: environment radiance unknown in the pack (0)
    const float t = hh.t;
    const float foot = cone.f0 + cone.spread * t;
    if (b == 1) {
      r.t1 = t;
      const float k = saturate(t / max(pc.gi_rt_radius, 1e-4));
      r.w = 1.0 - k * k;
      float2 quv; float qd;
      const bool onscreen = project(inv, o + dir * t, quv, qd) && all(quv >= 0.0) && all(quv <= 1.0);
      r.cls = 3;
      if (onscreen) {
        r.cls = 2;
        int2 qp = min(int2(quv * float2(pc.full)), int2(pc.full) - 1);
        float sd = gDepth.Load(int3(qp, 0));
        const float4 oq = rt_origin_at(uint2(qp));
        const bool has_tlas = pc.rt_origin != 0u && oq.w < kOriginNoHit;
        const float3 ngq = pc.rt_origin != 0u ? oq.xyz : depth_geo_normal(qp, ray_at((float2(qp) + 0.5) / float2(pc.full)) * sd);
        if (sd > 0.0 && depth_same_surface(qp, qd, sd, ngq, has_tlas, oq.w, pc.gi_rt_tol)) {
          float3 L = prev_bounce_at(qp);
          if (all(isfinite(L))) { r.cls = 1; r.screen = L; }
        }
      }
    }
    const float3 p = native_hit_position(hh, o + dir * t);
    float3 ng = rt_hit_normal(hh);
    if (!(dot(ng, ng) > 0.0) || !all(isfinite(ng))) break;
    ng = normalize(ng);
    if (dot(ng, dir) > 0.0) ng = -ng;   // two-sided: the side the ray arrives on
    r.vertices += 1.0;
    NativeHitMaterial m = rt_hit_material(hh, foot);
    if (b == 1) r.qualified = m.status == 0u;
    if (m.status != 0u) { m = pt_fallback_material(ng); r.fallback += 1.0; }
    else if (dot(m.normal, -dir) <= 0.0) m.normal = ng;   // shading normal facing away: geometric (declared)
    float3 Lv = PT_FURNACE_LE.xxx;
    float3 ls = 0, ll = 0;
#if PT_LIGHTS
    NativeHitDirect d = native_hit_direct(m, p, -dir, ng, foot);
    if (d.status == HdOk) ll = d.radiance; else r.nolist += 1.0;
#endif
    if (any(sunE > 0.0) && dot(ng, sunL) > 0.0) {
      HdIncident inc; inc.direction = sunL; inc.distance = 0; inc.status = HdOk; inc.radiance = sunE;
      float3 L = sunL;
      if (pc.sun_radius > 0.0) {   // disc sample (native_hit_sun_dir)
        const float u1 = rand01(seed), u2 = rand01(seed);
        const float3 tt = abs(L.y) < 0.99 ? normalize(cross(float3(0, 1, 0), L)) : normalize(cross(float3(0, 0, 1), L));
        const float3 bb = cross(L, tt);
        const float rr = sqrt(u1) * tan(pc.sun_radius), phi = 6.2831853 * u2;
        L = normalize(L + (tt * cos(phi) + bb * sin(phi)) * rr);
      }
      RtCone sc = {foot, 0};
      const uint vis = rt_visibility(rt_offset_ray(p, ng), L, PT_TMAX, sc);
      if (vis == kRtUnknown) r.sununknown += 1.0;
      else {
        HdDirect s = hd_scatter(m.scattering, m.normal, ng, -dir, inc, true, vis == kRtLit ? 1.0 : 0.0);
        if (s.status == HdOk) ls = s.radiance;
      }
    }
    r.sun += thr * ls;
    r.local += thr * ll;
    r.total += thr * (Lv + ls + ll);
    if (b == 1) r.first = Lv + ls + ll;
    if (b == PT_BOUNCES) break;
    // Continuation.
#if PT_LAMBERT
    TbMaterial lam; lam.rho = PT_ALBEDO.xxx; lam.F0 = 0.0.xxx; lam.diffuse_weight = 1; lam.specular_weight = 0; lam.alpha = 0;
    TfFrame fr = tf_frame(ng);
    TbSample sm = tb_sample(lam, tf_to_local(fr, -dir), rand01(seed), rand01(seed), rand01(seed));
    if (sm.status != TbOk || !(sm.pdf > 0.0)) break;
    const float3 ndir = normalize(tf_to_world(fr, sm.wi));
    thr *= sm.f * (sm.wi.z / sm.pdf);
#else
    HbContinuation c = hb_continuation(m.scattering, m.normal, ng, -dir, rand01(seed), rand01(seed), rand01(seed));
    if (c.status != HbPathComplete) break;   // NDF null atom / normal domain: the path ends (no retry)
    const float3 ndir = c.direction;
    thr *= c.throughput;
#endif
    if (b + 1 >= PT_RR_START) {
      const float q = min(max(thr.x, max(thr.y, thr.z)), 0.95);
      if (!(q > 0.0) || rand01(seed) >= q) break;
      thr /= q;
    }
    o = rt_offset_ray(p, ng);
    dir = ndir;
    cone.f0 = foot;
  }
  return r;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  g_rt_pass = kRtPassGI;
  uint2 id = tid.xy;
  if (any(id >= pc.half)) return;
  uint2 fp = gi_fp(id);
  float d = gDepth.Load(int3(fp, 0));
  float2 enc = gNormal.Load(int3(fp, 0)).xy;
  float2 e = enc * 3.5554 - 1.7777;
  if (d <= 0.0 || dot(e, e) + 1.0 > 5.4e10) { giRaw[id] = 0; giRawDepth[id] = 0; return; }
  float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  float3 P = ray_at(uv) * d;
  float3 N = to_lighting_space(decode_normal(enc));
  float3x3 inv = frustum_inverse();
  float dist = length(P), pa = pixel_angle(uv);
  const float4 org = rt_origin_at(fp);
  // The iteration arrives in Push::ls_kmode (unused by this include chain): Push::frame must stay the
  // pack's, it is the hit-material epoch (rt_hit_material header check).
  uint seed = pcg3d(uint3(fp, pc.frame ^ (pc.ls_kmode * 0x9e3779b9u)) ^ uint3(0x243f6a88u, 0x85a308d3u, 0x13198a2eu)).x;
  float4 acc = 0; float aux = 0;
  const uint mode = pc.debug;
  [loop] for (uint s = 0; s < PT_SPP; ++s) {
    PtPath r = pt_trace(fp, P, N, org, dist, pa, inv, seed);
    const float lt = pt_lum(r.total);
    const bool hit = r.cls != 0u && r.cls != 4u, near = hit && r.t1 < pc.gi_rt_radius;
    if (mode == 0u) { acc += float4(r.total, r.cls == 0u ? 1.0 : 0.0); aux += r.cls == 1u && near ? lt : 0.0; }
    else if (mode == 1u) { acc += float4(r.first, r.cls == 1u ? 1.0 : 0.0); aux += r.cls == 1u && !near ? lt : 0.0; }
    else if (mode == 2u) { acc += float4(r.screen, r.qualified ? 1.0 : 0.0); aux += r.cls == 2u && near ? lt : 0.0; }
    else if (mode == 3u) { acc += float4(near ? r.total * r.w : 0.0.xxx, near ? 1.0 : 0.0); aux += r.cls == 2u && !near ? lt : 0.0; }
    else if (mode == 4u) { acc += float4(near ? r.screen * r.w : 0.0.xxx, r.cls == 4u ? 1.0 : 0.0); aux += r.cls == 3u ? lt : 0.0; }
    else if (mode == 5u) { const float v = max(r.vertices, 1.0); acc += float4(r.nolist / v, r.sununknown / v, r.fallback / v, r.vertices); aux += pt_lum(r.sun); }
    else { acc += float4(near && r.cls != 1u ? r.total * r.w : 0.0.xxx, r.cls == 2u ? 1.0 : 0.0); aux += pt_lum(r.local); }
  }
  giRaw[id] = acc / float(PT_SPP);
  giRawDepth[id] = aux / float(PT_SPP);
}
