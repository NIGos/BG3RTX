// rt_occ_stats=1 (docs/DIALOGUE_SHADOWS.md "Occluder-kind diagnostic", common/occ_stats.h): which
// occluder darkens the character pixels (G-buffer class skin / hair) in the RT sun and the RT AO.
// Runs after the sun pass and the GI trace (core/gi.cpp run_pre, outside the timed RT region), on
// the same TLAS, push constants and variant defines:
//   sun: the darkening is the real pass's (rtVis: r traced visibility, g the game's mask before the
//     fold; game mask x (1 - traced)); the occluder comes from a replica of rt_shadow.hlsl's ray (same
//     seed, disc sample and origin), traced to its CLOSEST occluder (rt_common.hlsli RT_OCC_STATS).
//     Disagreements with rtVis.r are counted (kOccSunMismatch).
//   AO: the darkening is the real trace's 1 - giRaw.a (ambient visibility); the occluders come from
//     a replica of gi_trace_rt.hlsl's rays (same seed, directions, origin, radius gi_rt_radius and
//     the 1 - (t/R)^2 fade; gi_far's longer radiance rays and the hit-material passes' extra random
//     draws are not replicated: same ray distribution, not the same rays after the first).
// pc.bounce_spec (unused by every other pass) carries this pass's flags: bit 0 the sun pass traced
// this frame (rtVis valid), bit 1 the RT GI trace ran (giRaw valid).
#define RT_OCC_STATS 1
// With rt_cascade_casters (constant 1000): the sun replica sees the cascade copy
// (g_rt_cascade_ray, as the sun pass's RT_CASCADE_SUN), the AO rays trace 0x01 as the GI trace.
#define RT_CASCADE_SUN_HIT 1
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
#include "../rt/occ_stats.hlsli"

[[vk::binding(19, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> rtVis;

void occ_sun(uint2 fp) {
  if (any(fp >= pc.full) || (pc.bounce_spec & 1u) == 0u) return;
  const float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) return;
  const uint cls = mat_class(gMaterial.Load(int3(fp, 0)));
  if (!occ_character(cls)) return;
  g_rt_pass = kRtPassSun;
  // rt_shadow.hlsl's ray, step by step.
  const float3 L = normalize(cam.v0.xyz);
  const float3 N = to_lighting_space(decode_normal(gNormal.Load(int3(fp, 0)).xy));
  const float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  const float3 P = ray_at(uv) * d;
  const float dist = length(P);
  const float pa = pixel_angle(uv);
  const float nl = dot(N, L);
  const bool backlit = pc.rt_backlit != 0 && nl <= 0.0 && mat_transmits(cls);
  if (!(nl > 0.0 || backlit)) return;
  RtCone cone = {dist * pa, pa};
  float3 dir = L;
  if (pc.sun_radius > 0.0) {
    uint s = pixel_seed(fp, 1u);
    float u1 = rand01(s), u2 = rand01(s);
    float3 t = abs(L.y) < 0.99 ? normalize(cross(float3(0, 1, 0), L)) : normalize(cross(float3(0, 0, 1), L));
    float3 b = cross(L, t);
    float r = sqrt(u1) * tan(pc.sun_radius), phi = 6.2831853 * u2;
    dir = normalize(L + (t * cos(phi) + b * sin(phi)) * r);
  }
  const float3 o = rt_ray_origin(fp, P, N, dist * pa, dir);
#if defined(RT_CASCADE_MASKS) || defined(RT_SPEC_CASCADE)
  g_rt_cascade_ray = true;
#endif
  const uint vs = rt_visibility(o, dir, pc.rt_tmax, cone);
#if defined(RT_CASCADE_MASKS) || defined(RT_SPEC_CASCADE)
  g_rt_cascade_ray = false;
#endif
  // The real pass: traced r (the mask itself when its ray was unknown), the game's mask g.
  // (a = 8/255, rt_shadow.hlsl kSunUnknownCode: the real ray was unknown and kept the mask.)
  const float4 v = rtVis[fp];
  const bool real_unknown = abs(v.a - 8.0 / 255.0) < 2.0 / 255.0;
  const bool real_occ = !real_unknown && v.r < 0.5;
  const float dark = vs == kRtUnknown || !real_occ ? 0.0 : v.g * saturate(pc.rt_strength);
  occ_add(kOccSunMismatch, vs != kRtUnknown && !real_unknown && (vs == kRtOccluded) != real_occ ? 1u : 0u);
  occ_pixel(kOccSun, dark);
  occ_ray(kOccSun, vs, dark);
}

void occ_ao(uint2 id) {
  if (any(id >= pc.half) || (pc.bounce_spec & 2u) == 0u) return;
  const uint2 fp = gi_fp(id);
  const float d = gDepth.Load(int3(fp, 0));
  const float2 enc = gNormal.Load(int3(fp, 0)).xy;
  const float2 e = enc * 3.5554 - 1.7777;
  if (d <= 0.0 || dot(e, e) + 1.0 > 5.4e10) return;
  if (!occ_character(mat_class(gMaterial.Load(int3(fp, 0))))) return;
  g_rt_pass = kRtPassGI;
  const float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  const float3 P = ray_at(uv) * d;
  const float3 N = to_lighting_space(decode_normal(enc));
  const float dist = length(P);
  const float pa = pixel_angle(uv);
  RtCone cone = {dist * pa * 2.0, pa * 2.0};
  const float4 org = rt_origin_at(fp);
  const float R = max(pc.gi_rt_radius, 1e-3);
  const float n = float(max(pc.rays, 1u));
  uint seed = pixel_seed(fp, 0u);
  for (uint r = 0; r < pc.rays; ++r) {
    const float3 dir = cosine_hemisphere(N, rand01(seed), rand01(seed));
    const float3 o = RT_SECONDARY_ORIGIN(fp, org, P, N, dist * pa, dir);
    const RtHit hh = rt_closest_hit(o, dir, R, cone);
    const bool hit = hh.t >= 0.0 && !hh.aborted;
    const float k = saturate(hh.t / R);
    const float w = hit ? 1.0 - k * k : 0.0;
    occ_ray(kOccAO, hh.aborted ? kRtUnknown : hit ? kRtOccluded : kRtLit, w / n);   // + the distance histogram
  }
  // The real trace's ambient occlusion of this texel (gi_trace_rt.hlsl: a = 1 - occ / n).
  occ_pixel(kOccAO, 1.0 - saturate(giRaw[id].a));
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint gi : SV_GroupIndex, uint3 gid : SV_GroupID) {
  occ_clear(gi);
  GroupMemoryBarrierWithGroupSync();
  occ_sun(tid.xy);
  occ_ao(tid.xy);
  if (all(tid.xy == 0u)) occ_add(kOccPassMain, 1u);
  GroupMemoryBarrierWithGroupSync();
  occ_flush(gi, gid.xy);
}
