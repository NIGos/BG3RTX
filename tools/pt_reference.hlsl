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
// approximates the transmittance), roofs the camera cut away (absent from the TLAS; PT_ROOFS=1 below), transmission.
// With a pack that carries the lighting inputs (common/replay_pack.h sets 2-3; frame_replay
// --native-ambient sets these):
//   PT_SUN_LUT=1  the sun's colour from the game's transmittance LUT (bg3_sun.hlsli bg3_sun_colour,
//                 set 3) and its two cloud-shadow layers at every vertex, instead of PT_SUN_T;
//   PT_ENV=1      mode 7: the environment radiance a miss sees, the game's global specular cube at
//                 mip 0 (b35 / b37 blended by _m29, x _m27 / _m28, x b13[0] 100: bg3_ambient.hlsli's
//                 scale) along the ray, carried by the path throughput; kept OUT of modes 0-6 (the
//                 traced bounce split stays the same), in game units (no k).
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
//   7 (PT_ENV) environment at misses      first-vertex miss fraction   environment at first-vertex misses
// PT_FURNACE_LE > 0 (CTest): every vertex also emits PT_FURNACE_LE; PT_LAMBERT=1 scatters with a
// Lambertian of albedo PT_ALBEDO (common/transport_bsdf.h tb_sample) for the closed-form furnace.
#define RT_HIT_MATERIAL 1
#include "gi/gi_common.hlsli"
#include "rt/rt_common.hlsli"
#ifndef PT_SUN_LUT
#define PT_SUN_LUT 0
#endif
#ifndef PT_ENV
#define PT_ENV 0
#endif
#if PT_SUN_LUT || PT_ENV
#include "rt/bg3_lights.hlsli"
#endif
#if PT_ENV
#include "rt/bg3_ambient.hlsli"
#endif
#if PT_SUN_LUT
#define BG3_SUN_SET 3
#include "rt/bg3_sun.hlsli"
#endif
#include "rt/native_hit_material.hlsli"
#define NATIVE_HIT_PROJECTOR_SKIP 1
#define NATIVE_HIT_DLS_POLICY 1
#define NATIVE_HIT_SLOTS_SET 0
#define NATIVE_HIT_SLOTS_BINDING 46
#include "rt/native_hit_direct.hlsli"
#include "../common/native_hit_bounce.h"
// PT_ROOFS=1: the roofs the camera cut away (absent from the TLAS of a pack captured with
// rt_cascade_casters=0), from the asset database's roof volumes (tools/pt_roofs.py writes
// captures/pt_roofs.hlsli for the pack). Two declared rules:
//   sun rays: the production roof rule (gi/sun_roof.hlsli, core/sun_trust.cpp padding 3 m around,
//     PT_ROOF_SUN_UP 50 m up): a sun ray through a padded volume is occluded (gi_hit_sun leaves it out);
//   path rays: a ray crossing a volume's roof plane (its top + PT_ROOF_PLANE_UP, 3 m padding around)
//     upward before its hit (or a miss) ends there with no radiance (the roof's underside is unlit by the
//     sun and its bounce is not in the pack: a lower bound). First rays that end so are reported in the
//     "aborted" fraction (no query aborts in a pack run without PT_ROOFS: check that line first).
#if PT_ROOFS
#include "../captures/pt_roofs.hlsli"
#ifndef PT_ROOF_PAD_XZ
#define PT_ROOF_PAD_XZ 3.0
#endif
#ifndef PT_ROOF_SUN_UP
#define PT_ROOF_SUN_UP 50.0
#endif
#ifndef PT_ROOF_PLANE_UP
#define PT_ROOF_PLANE_UP 3.0
#endif
float3 pt_qrot_inv(float4 q, float3 v) { const float3 u = -q.xyz, t = 2.0 * cross(u, v); return v + q.w * t + cross(u, t); }
bool pt_sun_under_roof(float3 o, float3 d) {   // gi/sun_roof.hlsli sun_under_roof on the padded volumes
  [loop] for (uint i = 0; i < kPtRoofCount; ++i) {
    const float4 q = kPtRoof[3u * i + 1u];
    const float3 h = kPtRoof[3u * i + 2u].xyz + float3(PT_ROOF_PAD_XZ, 0.5 * PT_ROOF_SUN_UP, PT_ROOF_PAD_XZ);
    const float3 lo = pt_qrot_inv(q, o - kPtRoof[3u * i].xyz) - float3(0, 0.5 * PT_ROOF_SUN_UP, 0), ld = pt_qrot_inv(q, d);
    const float3 inv = 1.0 / select(abs(ld) > 1e-8, ld, select(ld >= 0.0, 1e-8, -1e-8));
    const float3 t1 = (-h - lo) * inv, t2 = (h - lo) * inv, tn = min(t1, t2), tf = max(t1, t2);
    if (min(min(tf.x, tf.y), tf.z) >= max(max(max(tn.x, tn.y), tn.z), 0.0)) return true;
  }
  return false;
}
float pt_roof_plane(float3 o, float3 d, float tmax) {   // nearest upward roof-plane crossing in (0, tmax), else -1
  float best = -1.0;
  [loop] for (uint i = 0; i < kPtRoofCount; ++i) {
    const float4 q = kPtRoof[3u * i + 1u];
    const float3 h = kPtRoof[3u * i + 2u].xyz;
    const float3 lo = pt_qrot_inv(q, o - kPtRoof[3u * i].xyz), ld = pt_qrot_inv(q, d);
    if (!(ld.y > 1e-6)) continue;
    const float tr = (h.y + PT_ROOF_PLANE_UP - lo.y) / ld.y;
    if (!(tr > 0.0) || tr >= tmax || (best >= 0.0 && tr >= best)) continue;
    const float2 xz = lo.xz + ld.xz * tr;
    if (all(abs(xz) <= h.xz + PT_ROOF_PAD_XZ)) best = tr;
  }
  return best;
}
#endif

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
#if PT_SUN_LUT
  float3 c = bg3_sun_colour();
#else
  float3 c = (cam.x[5].x > 0.0 && cam.x[13].w == 0.0) ? cam.x[13].xyz * cam.x[5].y * PT_SUN_T : cam.v1.xyz;
#endif
  return c * asfloat(lScale.Load(0));
}

#if PT_ENV
// The environment a miss sees (game units): the global specular cubes at mip 0 (a = 0 in
// bg3_spec_ambient: mip _m30 - 8), blended as the lighting blends them, x b13[0] 100.
float3 pt_env(float3 dir) {
  float m27 = lConsts.c[31].z, m28 = lConsts.c[31].w, m29 = lConsts.c[32].x, m30 = lConsts.c[32].y, m31 = lConsts.c[32].z;
  float3 g = lgSpecCube0.SampleLevel(lgSampCube, dir, max(m30 - 8.0, 0.0)).rgb * m27;
  [branch] if (m29 > 0.0) g = lerp(g, lgSpecCube1.SampleLevel(lgSampCube, dir, max(m31 - 8.0, 0.0)).rgb * m28, m29);
  return g * asfloat(lScale.Load(0)) * 100.0;
}
#endif

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
  float3 total, first, sun, local, env, env1;   // env, env1: PT_ENV (mode 7), all misses / first-vertex miss
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
#if PT_ROOFS
    if (pt_roof_plane(o, dir, hh.t >= 0.0 ? hh.t : PT_TMAX) >= 0.0) { if (b == 1) r.cls = 4; break; }   // the cut-away roof
#endif
    if (hh.t < 0.0) {   // a miss: environment radiance not in the traced bounce (PT_ENV: mode 7 only)
#if PT_ENV
      const float3 le = pt_env(dir);
      if (all(isfinite(le))) { r.env += thr * le; if (b == 1) r.env1 = le; }
#endif
      break;
    }
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
#if PT_ROOFS
      uint vis = rt_visibility(rt_offset_ray(p, ng), L, PT_TMAX, sc);
      if (vis == kRtLit && pt_sun_under_roof(rt_offset_ray(p, ng), L)) vis = kRtOccluded;
#else
      const uint vis = rt_visibility(rt_offset_ray(p, ng), L, PT_TMAX, sc);
#endif
      if (vis == kRtUnknown) r.sununknown += 1.0;
      else {
#if PT_SUN_LUT
        inc.radiance = sunE * bg3_cloud_shadow(p);
#endif
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
    else if (mode == 6u) { acc += float4(near && r.cls != 1u ? r.total * r.w : 0.0.xxx, r.cls == 2u ? 1.0 : 0.0); aux += pt_lum(r.local); }
    else { acc += float4(r.env, r.cls == 0u ? 1.0 : 0.0); aux += pt_lum(r.env1); }
  }
  giRaw[id] = acc / float(PT_SPP);
  giRawDepth[id] = aux / float(PT_SPP);
}
