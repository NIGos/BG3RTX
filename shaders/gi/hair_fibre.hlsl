// rt_hair_fibre (docs/RTX_HAIR_PRIMARY.md 3b-2b): the visible strands (rt_hair_groom_primary) lit with the physical
// fibre BSDF and dual scattering (rt/hair_dual.hlsli, Chiang 2016 + Zinke 2008, path-traced correction) instead of the
// game's class-5 Marschner terms. Runs right after D-LS (core/gi.cpp, same layout, sets and push constants) on the
// pixels whose G-buffer sample is a strand of this frame (class 5, the hair primary pixel entry current and at this
// pixel's depth). For the sun and every local light without a game shadow map:
//     buffer += E * hd_shade_pixel(T, V, L, n, colour, roughness) * v  -  (what the buffer holds for that light)
// v: the scene's visibility without the strands (RT_VIS_NO_GROOM), n: the strands crossed on the way to the light
// (counted against the groom TLAS, at most kMaxStrands), both on D-LS's own ray (hf_ray). What the buffer holds: D-LS's
// c * t for a traced light (t its decision, replayed exactly: hf_groom), the game's c for an untraced one, the game's
// sun term, each at its AO-on-direct factor (hf_ao: the game's, or D-LS's ao_direct give-back); the fibre response of a
// light takes the same factor. Kept as the game's: lights with a game shadow map unless rt_hair_fibre_mapped (M4.3,
// below; D-LS keeps them), box lights, unknown rays. The fibre's single scattering goes to the specular buffer (where
// the game's R/TT/TRT terms were), its multiple scattering to the diffuse buffer.
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

// rt_hair_fibre=2 (specialization 1046): the in-frame consistency check. The fibre response of every light D-LS traced
// is replaced by the game's own terms at the fibre pass's transmittance model (v, and T_f's luminance from this pixel's
// colour and strand count where rt_hair_groom_shadow_t passes strands); the sun, the mapped and the untraced lights
// are left alone. What is subtracted is D-LS's own write (the replay below), so the strand pixels must equal key off:
// exactly without rt_hair_groom_shadow_t, in the mean with it (D-LS draws each ray with the groom's measured a_f).
[[vk::constant_id(1046)]] const uint kHfCheck = 0u;

// D-LS's ray to a light: trace_light's origin, soft sample and extent (rt_local_shadows.hlsl, the same random stream),
// kept so that the strands are counted, and D-LS's strand decision replayed, on exactly the ray D-LS traced.
struct HfRay {
  float3 o, dir;
  float tmax;
  bool valid;   // false: trace_light returns unknown (the physical hair origin's segment failed)
};
HfRay hf_ray(float3 P, float3 N, float4 org, float3 lpos, uint2 fp, uint key, RtCone cone, float R) {
  HfRay ray = (HfRay)0;
  ray.valid = true;
  float3 L = lpos - P;
#ifdef RT_HAIR_PHYSICAL_ORIGIN
  float3 receiver;
  const bool physical_receiver = rt_hair_receiver(fp, P, receiver);
  if (physical_receiver) L = lpos - receiver;
#endif
  const float dist = length(L);
  const float3 Ld = L / max(dist, 1e-6);
  float3 dir = Ld;
  if (pc.ls_soft != 0 && R > 0.0) {
    const uint3 r = ls_rand(fp, key);
    const float sin_max = min(R / max(dist, 1e-6), 1.0);
    const float cos_max = sqrt(max(1.0 - sin_max * sin_max, 0.0));
    const float ct = 1.0 - u01(r.x) * (1.0 - cos_max);
    const float st = sqrt(max(1.0 - ct * ct, 0.0));
    const float phi = 6.2831853 * u01(r.y);
    const float3 t = abs(Ld.y) < 0.99 ? normalize(cross(float3(0, 1, 0), Ld)) : normalize(cross(float3(0, 0, 1), Ld));
    const float3 b = cross(Ld, t);
    dir = normalize(Ld * ct + (t * cos(phi) + b * sin(phi)) * st);
  }
  ray.o = RT_SECONDARY_ORIGIN(fp, org, P, N, cone.f0, dir);
  ray.tmax = length(lpos - ray.o) - max(R, pc.ls_clear);
#ifdef RT_HAIR_PHYSICAL_ORIGIN
  if (physical_receiver) {
    HairLightSegment segment = hpo_light_segment(receiver, ray.o, lpos, dir, pc.ls_soft != 0 ? R : 0.0,
                                                 pc.ls_soft != 0 ? pc.ls_clear : max(R, pc.ls_clear));
    ray.valid = segment.valid;
    dir = segment.direction;
    ray.tmax = segment.tmax;
  }
#endif
  ray.dir = dir;
  return ray;
}

// trace_light on the ray (the scene alone: RT_VIS_NO_GROOM).
uint hf_visibility(HfRay ray, RtCone cone) {
  if (!ray.valid) return kRtUnknown;
  if (ray.tmax <= 0.0) return kRtLit;
  return rt_visibility(ray.o, ray.dir, ray.tmax, cone);
}

// The strands on the ray, as rt_visibility's groom query sees them after a lit scene query (rt_common.hlsli): returns
// the crossings (counted up to kMaxStrands for the shading), `passes` D-LS's decision for this ray: no strand without
// rt_hair_groom_shadow_t (rt_groom_trace), else rt_groom_blocks' draw against d_f prod a_f (the same hash, the
// groom's measured a_f). One non-opaque query, each strand once: the joint of two chained segments reports both, so a
// candidate next to an already seen segment is the same crossing (hp_crossing; tests/hair_primary_gpu_test.py
// shadow check: equal to the CPU count on every ray).
float hf_groom(HfRay ray, out bool passes) {
  passes = true;
#ifdef RT_SPEC_NATIVE
  if (!RT_GROOM_ON || !(ray.tmax > 0.0) || !rt_groom_live()) return 0.0;
  RayDesc r = (RayDesc)0;
  r.Origin = ray.o;
#ifdef RT_SCENE_REBASE
  r.Origin = rt_scene_ray_origin(ray.o);
#endif
  r.Direction = ray.dir;
  r.TMin = 0.0;
  r.TMax = ray.tmax;
  uint n = 0u;
  float3 T = 1.0.xxx;
  const uint first = gSceneStamp.Load(60u);
  HpCrossings seen = hp_crossings_init();
  RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> q;
  q.TraceRayInline(rt_groom_scene(gSceneStamp.Load2(48u)), 0u, 0xFFu, r);
  while (q.Proceed()) {
    const uint k = q.CandidateInstanceIndex(), prim = q.CandidatePrimitiveIndex();
    bool lp, ln;
    rt_groom_links(first, k, prim, lp, ln);
    if (!hp_crossing(seen, k, prim, lp, ln)) continue;
    const uint t = inst_info(first + k).rec;
    const float3 a_f = (t >> 31) != 0u ? float3(t & 1023u, (t >> 10) & 1023u, (t >> 20) & 1023u) / 1023.0 : 0.0.xxx;
    T *= a_f * (n == 0u ? 0.7 : 1.0);
    ++n;
    // Stop once the count is full and the decision known (rt_groom_blocks itself stops when T reaches 0).
    if (n >= kMaxStrands && (kRtGroomShadowT == 0u || all(T <= 0.0))) q.Abort();
  }
  if (n == 0u) return 0.0;
  passes = false;
  if (kRtGroomShadowT != 0u) {
    const uint3 h = pcg3d(uint3(asuint(ray.o.x) ^ asuint(r.Direction.y), asuint(ray.o.y) ^ asuint(r.Direction.z),
                                asuint(ray.o.z) ^ (pc.frame * 0x85EBCA6Bu)));
    passes = float(h.x >> 8) * (1.0 / 16777216.0) < dot(T, float3(0.2126, 0.7152, 0.0722));
  }
  return float(min(n, kMaxStrands));
#else
  return 0.0;
#endif
}

// The AO-on-direct factor of each kind of term in the buffer (diffuse x, specular y), as D-LS left it: the game's
// mix(1, AO, m0.y) (ls_ao_factors) on what it lit; D-LS's ao_direct give-back (ls_finish) on the lights it traced
// (mode 1: none, mode 2: its own AO) and on the sun when bits 8 and (mode 2 or bit 4) say so. The fibre response of a
// light takes the factor of the term it replaces. (ls_giveback_cap only limits a recomputed term the game never had.)
void hf_ao(uint2 fp, Bg3Surface sf, out float2 game, out float2 traced, out float2 sun) {
  float ao, fdif, fspec;
  ls_ao_factors(fp, sf, ao, fdif, fspec);
  game = float2(fdif, fspec);
  traced = game;
  const uint mode = pc.ao_direct & 3u;
  if (mode != 0u) {
    const float ssao = mode == 2u && (pc.ao_direct & 16u) != 0u ? gAux[fp].x : ao;
    const float so = lFlags.m0.x > 0.0 ? saturate(pow(max(abs(sf.nov_raw + ssao), 9.9999997e-05), exp2(-16.0 * sf.a - 1.0)) - 1.0 + ssao) : 1.0;
    traced = mode == 1u ? 1.0.xx : float2(lerp(1.0, lFlags.m0.x > 0.0 ? ssao : 1.0, lFlags.m0.y), lerp(1.0, so, lFlags.m0.y));
  }
  sun = (pc.ao_direct & 8u) != 0u && mode != 0u && (mode == 2u || (pc.ao_direct & 4u) != 0u) ? traced : game;
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
  float2 f_game, f_traced, f_sun;
  hf_ao(fp, sf, f_game, f_traced, f_sun);
  // The fibre response: its single scattering (T_f f_s, every lobe) to the specular buffer as the game's R/TT/TRT
  // terms (RR demodulates the diffuse buffer by the hair albedo), the multiple scattering to the diffuse buffer.
  float3 add_d = 0, add_s = 0, sub_d = 0, sub_s = 0;
  // ---- local lights (point and spot lists of the pixel's froxel) ----
  const uint3 cl = bg3_cluster(fp, d);
  const uint end_p = cl.x + cl.y, end_s = end_p + cl.z;
  [loop] for (uint k = cl.x; k < end_s; ++k) {
    const uint li = bg3_light_index(k);
    const bool spot = k >= end_p;
    const Bg3Light l = load_light(li);
    if ((l.flags & 1u) != 0u) {   // a game shadow map: the game's hair term stays unless rt_hair_fibre_mapped
      if (kHfCheck != 0u || kHfMapped == 0u || pc.ls_shadowed == 0u || ls_qa_off() || ls_atlas_fallback(l.pos)) continue;
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
      const HfRay ray = hf_ray(s.P, s.N, s.org, l.pos, fp, li, s.cone, R);
      const uint vs = hf_visibility(ray, s.cone);
      if (vs == kRtUnknown) continue;
      bool passes;
      const float n = vs == kRtLit ? hf_groom(ray, passes) : 0.0;
      if (vs == kRtLit) {
        float3 fs, fm;
        hd_shade_pixel_parts(T, sf.V, Ld, n, colour, beta, fs, fm);
        add_d += E * fm * f_game.x;
        add_s += E * fs * f_game.y;
      }
      // D-LS keeps these lights on hair: the buffer holds the game's term at the game's AO factor.
      sub_d += E * S * bg3_hair_diff(mt, dot(sf.N, Ld), dot(mt.T, Ld), 1.0 - S) * f_game.x;
      sub_s += E * S * bg3_hair_spec(mt, Ld, sf.V) * f_game.y;
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
    float2 f = f_game;
    const bool traced = ls_light_traced(li, spot, s, lpos, R);
    if (kHfCheck != 0u && !traced) continue;
    if (traced && ls_qa_off()) continue;   // D-LS's QA ambient floor removed this light: nothing to replace
    if (traced) {
      ls_light_ray(li, lpos, R);
      const HfRay ray = hf_ray(s.P, s.N, s.org, lpos, fp, li, s.cone, R);
      const uint vs = hf_visibility(ray, s.cone);
      if (vs == kRtUnknown) continue;   // D-LS kept the game's term: so do we
      v = vs == kRtLit ? 1.0 : 0.0;
      // D-LS's visibility of this light, replayed on its own ray: the scene, then the strands (blocking, or with
      // rt_hair_groom_shadow_t passed by its draw against the groom's measured transmittance).
      bool passes = true;
      n = v > 0.0 ? hf_groom(ray, passes) : 0.0;
      t = v > 0.0 && passes ? 1.0 : 0.0;
      f = f_traced;
    }
    if (kHfCheck != 0u) {   // the game's terms at the fibre's own transmittance model (see kHfCheck)
      float tm = v > 0.0 && n == 0.0 ? 1.0 : 0.0;
#ifdef RT_SPEC_NATIVE
      if (v > 0.0 && n > 0.0 && kRtGroomShadowT != 0u)
        tm = 0.7 * dot(pow(hd_lookup3(colour, max(0.09, beta), 0u), n.xxx), float3(0.2126, 0.7152, 0.0722));
#endif
      add_d += cd * (tm * f.x);
      add_s += cs * (tm * f.y);
    } else if (v > 0.0) {
      float3 fs, fm;
      hd_shade_pixel_parts(T, sf.V, Ld, n, colour, beta, fs, fm);
      add_d += E * fm * f.x;
      add_s += E * fs * f.y;
    }
    sub_d += cd * (t * f.x);
    sub_s += cs * (t * f.y);
  }
  // ---- the sun (rt/bg3_sun.hlsli, as ls_sun) ----
  const float m = pc.ls_has_mask != 0u && lFlags.m0.w > 0.0 ? gSunShadow[fp] : 1.0;
  const float clouds = bg3_cloud_shadow(sf.P);
  const float3 Ls = normalize(cam.v0.xyz);
  const float3 Esun = bg3_sun_colour() * (clouds * asfloat(lScale.Load(0)));
  if (kHfCheck == 0u && any(Esun > 0.0)) {
    const HfRay ray = hf_ray(s.P, s.N, s.org, s.P + Ls * 1.0e4, fp, 0xFFFFu, s.cone, 0.0);
    const uint vs = hf_visibility(ray, s.cone);
    if (vs != kRtUnknown && m * clouds > 0.0) {   // unknown: the game's sun term stays (as D-LS keeps it)
      float3 sd, ss;
      bg3_sun_eval_mat(sf, mt, Ls, Esun * m, m * clouds, sd, ss);
      sub_d += sd * f_sun.x;
      sub_s += ss * f_sun.y;
    }
    if (vs == kRtLit) {
      bool passes;
      float3 fs, fm;
      hd_shade_pixel_parts(T, sf.V, Ls, hf_groom(ray, passes), colour, beta, fs, fm);
      add_d += Esun * fm * f_sun.x;
      add_s += Esun * fs * f_sun.y;
    }
  }
  const float3 od = max(gDiffuse[fp] - sub_d + add_d, 0.0), os = max(gSpecular[fp] - sub_s + add_s, 0.0);
  if (all(isfinite(od)) && all(isfinite(os))) {
    gDiffuse[fp] = od;
    gSpecular[fp] = os;
  }
}
