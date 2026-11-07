// rt_hair_fibre (docs/RTX_HAIR_PRIMARY.md 3b-2b): the visible strands (rt_hair_groom_primary) lit with the physical
// fibre BSDF and dual scattering (rt/hair_dual.hlsli, Chiang 2016 + Zinke 2008, path-traced correction) instead of the
// game's class-5 Marschner terms. Runs right after D-LS (core/gi.cpp, same layout, sets and push constants) on the
// pixels whose G-buffer sample is a strand of this frame (class 5, the hair primary pixel entry current and at this
// pixel's depth). For the sun and every local light without a game shadow map:
//     buffer += E * hd_shade_pixel(T, V, L, n, colour, roughness) * v  -  (what the buffer holds for that light)
// v: the scene's visibility without the strands (RT_VIS_NO_GROOM), n: the strands crossed on the way to the light
// (counted against the groom TLAS, at most kMaxStrands). What the buffer holds: D-LS's c * t for a traced light
// (t = v with the strands as occluders: v and n == 0), the game's c for an untraced one, the game's sun term.
// Kept as the game's: lights with a game shadow map unless rt_hair_fibre_mapped (M4.3, below; D-LS keeps them),
// box lights, unknown rays, D-LS's AO-on-direct give-back. The new light goes to the diffuse buffer.
#define LS_NO_MAIN 1
#define RT_VIS_NO_GROOM 1
#include "rt_local_shadows.hlsl"
#include "../rt/hair_dual.hlsli"
#include "../rt/hair_primary.hlsli"

#include "hair_strand_pixel.hlsli"
static const uint kMaxStrands = 8u;

// rt_hair_fibre_mapped (specialization 1030, docs/RTX_HAIR_PRIMARY.md M4.3): the local lights with a game shadow map
// too. The game shades them on hair with its atlas term S in place of the self-shadow saturate(2 N.L + 0.5)
// (3033c895, the point and spot loops: the atlas sampled from P + L 3.5 nb sqrt(d2), no N.L gate, 12 taps, faded
// by the distance), so the buffer holds E S (spec + diff(texp = 1 - S)) for each; that is replaced by the fibre
// response E F v with the traced visibility and the strand count, like every other traced light. Only where D-LS
// trusts the trace for mapped lights (ls_shadowed 1, or 3 with the light on screen); otherwise, and for unknown rays,
// the game's term stays. 0: mapped lights keep the game's hair term (as before).
[[vk::constant_id(1030)]] const uint kHfMapped = 0u;

// bg3_atlas_shadow as 3033c895 samples it for hair: offset along the light direction, no N.L gate.
float hf_atlas_hair(Bg3Light l, bool spot, Bg3Surface sf, float3 Ld, float d2) {
  uint slot = l.flags >> 2;
  if (!spot) {
    const float3 m = -Ld;
    const float f0 = dot(m, float3(0.0, -0.57735026, 0.81649661)), f1 = dot(m, float3(0.0, -0.57735026, -0.81649661));
    const float f2 = dot(m, float3(-0.81649661, 0.57735026, 0.0)), f3 = dot(m, float3(0.81649661, 0.57735026, 0.0));
    const float mx = max(max(f0, f1), max(f2, f3));
    slot += mx == f0 ? 0u : mx == f1 ? 1u : mx == f2 ? 2u : 3u;
  }
  if (!bg3_in(lShadows, slot, 96u)) return 1.0;
  const uint a = slot * 96u;
  const float4 r0 = asfloat(lShadows.Load4(a)), r1 = asfloat(lShadows.Load4(a + 16));
  const float4 r2 = asfloat(lShadows.Load4(a + 32)), r3 = asfloat(lShadows.Load4(a + 48));
  const float2 org = asfloat(lShadows.Load2(a + 64));
  const float size = asfloat(lShadows.Load(a + 72)), nb = asfloat(lShadows.Load(a + 80));
  const float2 dp = asfloat(lShadows.Load2(a + 84));
  const float dw = asfloat(lShadows.Load(a + 92));
  const float4 q = float4(sf.P + Ld * (3.5 * (nb * sqrt(d2))), 1.0);
  const float4 c = float4(dot(r0, q), dot(r1, q), dot(r2, q), dot(r3, q));
  const float3 p = c.xyz / c.w;
  float2 uv = p.xy * 0.5 + 0.5;
  uv.y = 1.0 - uv.y;
  const float ref = (dp.y / dw) / (p.z - dp.x);
  const float2 texel = lConsts.c[33].xy;
  const float rot = sf.ign * 6.2831855;
  const float2 lo = org + texel, hi = org + size.xx - texel;
  uint aw, ah;
  lAtlas.GetDimensions(aw, ah);
  const int2 amax = int2(aw, ah) - 1;
  float sum = 0.0;
  [loop] for (int i = 0; i < 12; ++i) {
    const float fi = float(i);
    const float r = sqrt(fi + 0.5) * 0.28867513;
    const float ang = fi * 2.3999631 + rot;
    const float2 u = clamp(float2(cos(ang), sin(ang)) * r * 2.5 * texel + uv, lo, hi);
    const float2 f = frac(u / texel + 0.5);
    const int2 b = int2(floor(u / texel - 0.5));
    const float g00 = lAtlas.Load(int3(clamp(b, 0, amax), 0)), g10 = lAtlas.Load(int3(clamp(b + int2(1, 0), 0, amax), 0));
    const float g01 = lAtlas.Load(int3(clamp(b + int2(0, 1), 0, amax), 0)), g11 = lAtlas.Load(int3(clamp(b + int2(1, 1), 0, amax), 0));
    const float4 cmp = float4(ref < g01, ref < g11, ref < g10, ref < g00);
    sum += saturate(lerp(lerp(cmp.w, cmp.z, f.x), lerp(cmp.x, cmp.y, f.x), f.y));
  }
  const float fade = saturate(lConsts.c[33].w * length(sf.P) + lConsts.c[33].z);
  return lerp(1.0, sum * 0.083333336, min(fade, l.strength));
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
    if ((l.flags & 1u) != 0u) {   // a game shadow map: the game's hair term stays unless rt_hair_fibre_mapped
      if (kHfMapped == 0u || pc.ls_shadowed == 0u || ls_qa_off() || ls_atlas_fallback(l.pos)) continue;
      float R = pc.ls_source_radius;
      if ((pc.ls_flags & 1u) != 0u) {   // D-LS's per-light data: the source radius and its owner rules
        const uint4 si = lsSlots[uint2(min(li, 4095u), 0)];
        if ((si.z & 0x10000u) != 0u) {
          R = asfloat(si.x);
          if (ls_skip_light(si)) continue;
        }
      }
      float3 E, Ld;
      float dist;
      if (!hf_light(l, spot, sf, E, Ld, dist)) continue;   // outside its range, cone or channel: the game added nothing
      const float3 Lv = l.pos - sf.P;
      const float S = hf_atlas_hair(l, spot, sf, Ld, dot(Lv, Lv));
      const uint vs = trace_light(s.P, s.N, s.org, l.pos, fp, li, s.cone, R);
      if (vs == kRtUnknown) continue;
      const float v = vs == kRtLit ? 1.0 : 0.0;
      const float n = v > 0.0 ? hf_strands(s.P, Ld, dist) : 0.0;
      add += E * hd_shade_pixel(T, sf.V, Ld, n, colour, beta) * v;
      sub_d += E * S * bg3_hair_diff(mt, dot(sf.N, Ld), dot(mt.T, Ld), 1.0 - S);
      sub_s += E * S * bg3_hair_spec(mt, Ld, sf.V);
      continue;
    }
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
