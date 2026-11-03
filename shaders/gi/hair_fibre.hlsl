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

[[vk::constant_id(1100)]] const uint kHfRecordLo = 0u;   // the layer's hair primary header (persist key)
[[vk::constant_id(1101)]] const uint kHfRecordHi = 0u;
static const uint kMaxStrands = 8u;

uint64_t hf_record() { return (uint64_t(kHfRecordHi) << 32) | uint64_t(kHfRecordLo); }
uint hf_ld(uint64_t a) { return vk::RawBufferLoad<uint>(a, 4); }
uint64_t hf_addr(uint64_t a) { const uint2 v = vk::RawBufferLoad<uint2>(a, 4); return (uint64_t(v.y) << 32) | uint64_t(v.x); }
float3 hf_f3(uint64_t a) { return asfloat(vk::RawBufferLoad<uint3>(a, 4)); }

// The strand's unit tangent (TLAS frame: camera-relative world) when the pixel shows a strand of this frame.
bool hf_strand(uint2 fp, float d, out float3 T) {
  T = float3(0.0, 0.0, 1.0);
  const uint64_t rec = hf_record();
  if (rec == 0u) return false;
  const uint tag = hf_ld(rec), width = hf_ld(rec + 4u), height = hf_ld(rec + 8u), grooms = hf_ld(rec + 12u);
  if (tag == 0u || grooms == 0u || any(fp >= uint2(width, height))) return false;
  const uint4 e = vk::RawBufferLoad<uint4>(hf_addr(rec + 32u) + uint64_t(fp.y * width + fp.x) * hair_primary_pixel_bytes, 16);
  if (e.w != hp_seal(e.xyz, tag) || e.x == hair_primary_no_strand || (e.x >> 24) >= grooms) return false;
  if (abs(asfloat(e.z) - d) > 1e-6 * max(abs(d), 1e-6)) return false;   // something else won this pixel
  const uint4 row = vk::RawBufferLoad<uint4>(rec + hair_primary_table_offset + (e.x >> 24) * 16u, 16);
  const uint64_t pool = hf_addr(rec + 24u);
  const uint i0 = hf_ld(pool + row.z + (e.x & hair_primary_max_segment) * 4u);
  const float3 dp = hf_f3(pool + row.x + (i0 + 1u) * 12u) - hf_f3(pool + row.x + i0 * 12u);
  if (!(dot(dp, dp) > 0.0)) return false;
  T = normalize(dp);
  return true;
}

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
  uint n = 0u;
  [loop] for (; n < kMaxStrands; ++n) {
    const RtGroomHit g = rt_groom_trace(r, false);
    if (g.t < 0.0) break;
    r.TMin = g.t + 1e-4;
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
      t = v > 0.0 && n == 0.0 ? 1.0 : 0.0;   // D-LS's visibility (the strands occlude there)
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
