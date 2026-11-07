// rt_hair_fibre (docs/RTX_HAIR_PRIMARY.md 3b-2b): the visible strands (rt_hair_groom_primary) lit with the physical
// fibre BSDF and dual scattering (rt/hair_dual.hlsli, Chiang 2016 + Zinke 2008, path-traced correction) instead of the
// game's class-5 Marschner terms. Runs right after D-LS (core/gi.cpp, same layout, sets and push constants) on the
// pixels whose G-buffer sample is a strand of this frame (class 5, the hair primary pixel entry current and at this
// pixel's depth). For the sun and every local light without a game shadow map:
//     buffer += E * hd_shade_pixel(T, V, L, n, colour, roughness) * v  -  (what the buffer holds for that light)
// v: the scene's visibility without the strands (RT_VIS_NO_GROOM), n: the strands crossed on the way to the light
// (counted against the groom TLAS, at most kMaxStrands). What the buffer holds: D-LS's c * t for a traced light
// (t = v with the strands as occluders: v and n == 0), the game's c for an untraced one, the game's sun term.
// Kept as the game's: lights with a game shadow map (the game's hair atlas self-shadow is not modelled, as in D-LS),
// box lights, unknown rays, D-LS's AO-on-direct give-back. The new light goes to the diffuse buffer.
#define LS_NO_MAIN 1
#define RT_VIS_NO_GROOM 1
#include "rt_local_shadows.hlsl"
#include "../rt/hair_dual.hlsli"
#include "../rt/hair_primary.hlsli"

#include "hair_strand_pixel.hlsli"
static const uint kMaxStrands = 8u;

// Strands crossed from P toward L within dist (the pixel's own fibre skipped: rays start 1 mm out).
float hf_strands(float3 P, float3 L, float dist) {
#ifdef RT_SPEC_NATIVE
  if (!RT_GROOM_ON) return 0.0;
  RayDesc r;
  r.Origin = P;
#ifdef RT_SCENE_REBASE
  r.Origin = rt_scene_ray_origin(P);
#endif
  r.Direction = L;
  r.TMin = 1e-3;
  r.TMax = dist;
  // One non-opaque query, each strand once: the joint of two chained segments reports both, so a candidate next to
  // an already seen segment is the same crossing (hp_crossing; tests/hair_primary_gpu_test.py shadow check: equal to
  // the CPU count on every ray; successive closest hits counted 35% of the rays one or more strands too many).
  if (!rt_groom_live()) return 0.0;
  uint n = 0u;
  const uint first = gSceneStamp.Load(60u);
  HpCrossings seen = hp_crossings_init();
  RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> q;
  q.TraceRayInline(rt_groom_scene(gSceneStamp.Load2(48u)), 0u, 0xFFu, r);
  while (q.Proceed()) {
    const uint k = q.CandidateInstanceIndex(), prim = q.CandidatePrimitiveIndex();
    bool lp, ln;
    rt_groom_links(first, k, prim, lp, ln);
    if (hp_crossing(seen, k, prim, lp, ln) && ++n >= kMaxStrands) q.Abort();
  }
  return float(n);
#else
  return 0.0;
#endif
}

// A local light's radiance at P (bg3_light_eval_mat's attenuation, cone and scale; no N.L or hair self-shadow).
bool hf_light(Bg3Light l, bool spot, Bg3Surface sf, out float3 E, out float3 Ld, out float dist) {
  E = 0;
  Ld = 0;
  dist = 0;
  if ((l.chan & (1u << (sf.channel & 31u))) == 0u) return false;
  const float3 L = l.pos - sf.P;
  const float d2 = dot(L, L);
  Ld = L * rsqrt(max(d2, 1.0000000e-10));
  dist = sqrt(d2);
  const float ir2 = l.inv_r * l.inv_r;
  if (!(d2 < 1.0 / ir2)) return false;
  const float x = d2 * ir2;
  float att = pow(max(abs(saturate(1.0 - x * x)), 9.9999997e-05), l.m4);
  const float bias = abs(l.m5);
  att *= l.m5 < 0.0 ? (4.0 * ir2 + bias) : (1.0 / max(d2, 9.9999997e-05) + bias);
  float g = att, norm = 0.07957747;
  if (spot) {
    const float c = dot(-Ld, normalize(l.dir));
    if (!(c > l.cos_outer)) return false;
    const float t = saturate((c - l.cos_outer) / max(l.cos_inner - l.cos_outer, 9.9999997e-05));
    g = att * (t * t);
    norm = 0.31830987;
  }
  E = l.col * (norm * g * (l.m3 < 0.0 ? asfloat(lScale.Load(0)) : 1.0));
  return true;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  const uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  g_rt_pass = kRtPassLocal;
  const float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) return;
  LsLite s = (LsLite)0;
#ifdef RT_HAIR_PHYSICAL_ORIGIN
  s.pixel = fp;
#endif
  if (!ls_lite(fp, d, s) || s.cls != kMatHair) return;
  float3 T;
  if (!hf_strand(fp, d, T)) return;
  Bg3Surface sf;
  Bg3Mat mt;
  ls_surface(fp, s, sf, mt);
  const float3 colour = saturate(mt.albedo);
  const float beta = mt.hair_rough;
  float3 add = 0, sub_d = 0, sub_s = 0;
  // ---- local lights (point and spot lists of the pixel's froxel) ----
  const uint3 cl = bg3_cluster(fp, d);
  const uint end_p = cl.x + cl.y, end_s = end_p + cl.z;
  [loop] for (uint k = cl.x; k < end_s; ++k) {
    const uint li = bg3_light_index(k);
    const bool spot = k >= end_p;
    const Bg3Light l = load_light(li);
    if ((l.flags & 1u) != 0u) continue;   // a game shadow map: the game's hair term stays
    float3 cd, ct, cs, Ld0;
    float d2, nol;
    if (!bg3_light_eval_mat(l, spot, sf, mt, cd, ct, cs, Ld0, d2, nol)) continue;   // the game added nothing
    float3 E, Ld;
    float dist;
    if (!hf_light(l, spot, sf, E, Ld, dist)) continue;
    float3 lpos;
    float R;
    float v = 1.0, n = 0.0, t = 1.0;
    if (ls_light_traced(li, spot, s, lpos, R)) {
      ls_light_ray(li, lpos, R);
      const uint vs = trace_light(s.P, s.N, s.org, lpos, fp, li, s.cone, R);
      if (vs == kRtUnknown) continue;   // D-LS kept the game's term: so do we
      v = vs == kRtLit ? 1.0 : 0.0;
      n = v > 0.0 ? hf_strands(s.P, Ld, dist) : 0.0;
      // D-LS's visibility of this light: blocked by the strands, or (rt_hair_groom_shadow_t) passed with their
      // transmittance d_f lum(a_f^n) in expectation (rt_common.hlsli rt_groom_blocks; a_f of this pixel's groom colour:
      // exact for its own strands, an approximation where another groom's strands lie on the ray).
      t = v > 0.0 && n == 0.0 ? 1.0 : 0.0;
#ifdef RT_SPEC_NATIVE
      if (v > 0.0 && n > 0.0 && kRtGroomShadowT != 0u)
        t = 0.7 * dot(pow(hd_lookup3(colour, max(0.09, beta), 0u), n.xxx), float3(0.2126, 0.7152, 0.0722));
#endif
    }
    add += E * hd_shade_pixel(T, sf.V, Ld, n, colour, beta) * v;
    sub_d += cd * t;
    sub_s += cs * t;
  }
  // ---- the sun (rt/bg3_sun.hlsli, as ls_sun) ----
  const float m = pc.ls_has_mask != 0u && lFlags.m0.w > 0.0 ? gSunShadow[fp] : 1.0;
  const float clouds = bg3_cloud_shadow(sf.P);
  const float3 Ls = normalize(cam.v0.xyz);
  const float3 Esun = bg3_sun_colour() * (clouds * asfloat(lScale.Load(0)));
  if (any(Esun > 0.0)) {
    const uint vs = trace_light(s.P, s.N, s.org, s.P + Ls * 1.0e4, fp, 0xFFFFu, s.cone, 0.0);
    if (vs != kRtUnknown && m * clouds > 0.0) {   // unknown: the game's sun term stays (as D-LS keeps it)
      float3 sd, ss;
      bg3_sun_eval_mat(sf, mt, Ls, Esun * m, m * clouds, sd, ss);
      sub_d += sd;
      sub_s += ss;
    }
    if (vs == kRtLit) add += Esun * hd_shade_pixel(T, sf.V, Ls, hf_strands(s.P, Ls, 1.0e4), colour, beta);
  }
  const float3 od = max(gDiffuse[fp] - sub_d + add, 0.0), os = max(gSpecular[fp] - sub_s, 0.0);
  if (all(isfinite(od)) && all(isfinite(os))) {
    gDiffuse[fp] = od;
    gSpecular[fp] = os;
  }
}
