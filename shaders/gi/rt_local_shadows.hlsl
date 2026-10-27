// Track D, D-LS: ray-traced shadows for BG3's local point and spot lights
// (rt_local_shadows=1; docs/TRACK_D_RT.md section 9).
//
// Runs at full resolution right after the tiled lighting (before the GI
// composite, SSS and fog). For every point/spot light the game evaluated at
// this pixel (same froxel walk, channel mask, range and cone as f469db78), the
// unshadowed contribution c_i (diffuse and specular, the game's exact BRDF) is
// recomputed, and the exact delta
//     out += sum_i c_i * (v_i - s_i) * AO-on-direct factor
// is added into the game's diffuse/specular buffers, where s_i is the game's
// own shadow term (1 for lights without a shadow map, the atlas PCF for the
// others) and v_i the ray-traced visibility. Lights the game does not shadow
// can then only lose light (leaks through walls and floors), never gain any.
//
// Lights that have a game shadow map (ls_shadowed):
//   0 (default) left alone: the atlas also holds casters that are not on screen,
//     which the TLAS lacks (cut-away roofs, culled walls), so replacing it could
//     un-shadow them;
//   1 replace the atlas with the ray-traced visibility;
//   2 min(traced, atlas).
//
// Sampling: with RR (rr=1) and ls_source_radius > 0, one ray per light and pixel
// towards a uniform point of the light's sphere (solid angle), white noise from
// pcg3d keyed by (pixel, frame, light): RR denoises the penumbra. Without RR
// (no denoiser), one deterministic ray to the light centre (hard shadows).
// ls_samples > 0 instead traces that many rays per pixel to lights chosen by
// RIS over the unshadowed luminance (unbiased, noisier, cheaper with many lights).
//
// Per-light data (ls_lights=1, ls_flags bit 0; docs/SE_BRIDGE.md): lsSlots holds, per
// light array slot, what rt_local_shadows_match.hlsl joined to it from the Script
// Extender bridge or the asset database: the light's source radius (from its fixture
// class) and its owner. Lights of an owner whose bit is clear in ls_flags bits 8-12
// (ls_owners; default: not the character lights) are left as the game lit them.
// Unmatched lights keep ls_source_radius and are traced as before.
//
// Two variants of this file (docs/TRACK_D_RT.md 9.9; core/gi.cpp picks one per frame):
//   LS_FULL=0 (this file): the default settings only (ls_shadowed=0, ls_samples=0,
//     ls_debug=0, ls_stats=0), in three phases per 32 list entries (tests, rays, terms;
//     see below): 96 registers instead of 252.
//   LS_FULL=1 (rt_local_shadows_full.hlsl): every mode, debug view and statistic, in
//     the single loop of before.
// Both give bit-identical output to the pre-optimisation pass (ls_check=1 compares them
// per pixel against its frozen copy, shaders/gi/ls_ref).
//
// Validation (ls_debug=4/5/6): rt_local_shadows_prep.hlsl zeroes the game's AO and
// sun mask before the lighting, so its output holds only direct local light (plus
// projector lights and in-scatter, which are not recomputed).
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
#include "../rt/bg3_lights.hlsli"
#include "../rt/bg3_sun.hlsli"

#ifndef LS_FULL
#define LS_FULL 0
#endif

[[vk::binding(46, 0)]] [[vk::image_format("rgba32ui")]] RWTexture2D<uint4> lsSlots;

float lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

uint3 ls_rand(uint2 fp, uint key) { return pcg3d(uint3(fp, pc.frame * 1024u + (key & 1023u))); }
float u01(uint x) { return float(x >> 8) * (1.0 / 16777216.0); }

float3 n_world(float3 nv) { return nv.x * cam.m6_r0.xyz + nv.y * cam.m6_r1.xyz + nv.z * cam.m6_r2.xyz; }
float3 n_view_at(int2 p) { return decode_normal(gNormal.Load(int3(clamp(p, int2(0, 0), int2(pc.full) - 1), 0)).xy); }
float3 nan0(float3 v) { return any(isnan(v)) ? float3(0, 0, 0) : v; }

// Visibility of the light from the surface: 1 ray, towards the centre or a
// point of its sphere. `key` selects the random stream. The ray starts on the side
// of the surface its direction leaves on (back-lit transmission, hair).
float trace_light(float3 P, float3 N, float4 org, float3 lpos, uint2 fp, uint key, RtCone cone, float R) {
  float3 L = lpos - P;
  float dist = length(L);
  float3 Ld = L / max(dist, 1e-6);
  float3 dir = Ld;
  if (pc.ls_soft != 0 && R > 0.0) {
    uint3 r = ls_rand(fp, key);
    float sin_max = min(R / max(dist, 1e-6), 1.0);
    float cos_max = sqrt(max(1.0 - sin_max * sin_max, 0.0));
    float ct = 1.0 - u01(r.x) * (1.0 - cos_max);
    float st = sqrt(max(1.0 - ct * ct, 0.0));
    float phi = 6.2831853 * u01(r.y);
    float3 t = abs(Ld.y) < 0.99 ? normalize(cross(float3(0, 1, 0), Ld)) : float3(1, 0, 0);
    float3 b = cross(Ld, t);
    dir = normalize(Ld * ct + (t * cos(phi) + b * sin(phi)) * st);
  }
  float3 o = rt_ray_origin_o(org, P, N, cone.f0, dir);
  float tmax = length(lpos - o) - max(R, pc.ls_clear);
  if (tmax <= 0.0) return 1.0;
  return rt_occluded(o, dir, tmax, cone) ? 0.0 : 1.0;
}

// The pixel as f469db78 sets it up, in two parts: what the light tests and the rays need
// (LsLite), and the rest of the surface and the class terms (ls_surface).
struct LsLite {
  float3 P, N;
  float4 org;
  RtCone cone;
  uint channel, cls;
  bool trans;   // Bg3Mat.trans
};
// False: nothing to do (the lighting skips unlit pixels).
bool ls_lite(uint2 fp, float d, out LsLite s) {
  s = (LsLite)0;
  float2 uv = (float2(fp) + 0.5) / float2(lFlags.res);
  s.P = ray_at(uv) * d;
  s.N = n_world(decode_normal(gNormal.Load(int3(fp, 0)).xy));
  float4 mat = gMaterial.Load(int3(fp, 0));
  uint cls = mat_class_at(fp);
  if (cls == kMatUnlit) return false;
  // Classes whose parameters are missing are shaded as class 0 (logged by the core).
  if ((cls == kMatSkin || cls == kMatTranslucent) && (pc.mat_classes & 4u) == 0u) cls = kMatStandard;
  if (cls >= kMatAniso && (pc.mat_classes & 2u) == 0u) cls = kMatStandard;
  s.cls = cls;
  s.trans = cls == kMatTranslucent || (cls == kMatSkin && gRT3.Load(int3(fp, 0)).w != 0.0);
  s.channel = (uint(round(mat.w * 255.0)) & 112u) >> 4;
  s.org = rt_origin_at(fp);
  float pa = pixel_angle(uv);
  s.cone.f0 = length(s.P) * pa;
  s.cone.spread = pa;
  return true;
}

void ls_surface(uint2 fp, LsLite s, out Bg3Surface sf, out Bg3Mat mt) {
  sf.P = s.P;
  sf.dist = length(sf.P);
  float3 nv = decode_normal(gNormal.Load(int3(fp, 0)).xy);
  sf.N = s.N;
  sf.V = normalize(-sf.P);
  float4 alb = gAlbedo.Load(int3(fp, 0));
  float4 mat = gMaterial.Load(int3(fp, 0));
  float4 rt3 = s.cls != kMatStandard ? gRT3.Load(int3(fp, 0)) : 0.0.xxxx;
  sf.channel = s.channel;
  sf.diff_col = alb.rgb * (1.0 - mat.z);
  sf.f0 = lerp((0.16 * mat.y * mat.y).xxx, alb.rgb, mat.z);
  // Specular AA from the quad's normal differences (n(odd) - n(even) in each direction).
  // The pixel's own normal is one of each pair; its partner is fp ^ 1, clamped to the
  // screen (the last row of an odd height pairs with itself, as the clamp did before).
  float3 nx = n_view_at(int2(fp.x ^ 1u, fp.y)), ny = n_view_at(int2(fp.x, fp.y ^ 1u));
  float3 dh = nan0((fp.x & 1u) != 0u ? nv - nx : nx - nv);
  float3 dv = nan0((fp.y & 1u) != 0u ? nv - ny : ny - nv);
  float r = max(0.09, mat.x);
  sf.a = sqrt(saturate(r * r + min(dot(dh, dh) + dot(dv, dv), 0.18)));
  sf.nov_raw = dot(sf.N, sf.V);
  sf.nov = max(sf.nov_raw, 9.9999997e-06);
  sf.ign = frac(52.982918 * frac(0.06711056 * float(fp.x) + 0.0058371499 * float(fp.y)));
  sf.org = s.org;
  mt = bg3_mat_setup(s.cls, alb.rgb, mat, rt3, skin_white(), float3x3(cam.m6_r0.xyz, cam.m6_r1.xyz, cam.m6_r2.xyz), sf);
}

struct LsPixel {
  Bg3Surface sf;
  Bg3Mat mt;
  uint cls;
  float d;
  RtCone cone;
};
// The whole pixel at once (LS_FULL). False: nothing to do (sky, unlit class).
bool ls_pixel(uint2 fp, out LsPixel px) {
  px = (LsPixel)0;
  float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) return false;
  LsLite s = (LsLite)0;
  if (!ls_lite(fp, d, s)) return false;
  ls_surface(fp, s, px.sf, px.mt);
  px.cls = s.cls;
  px.d = d;
  px.cone = s.cone;
  return true;
}

// The game's AO-on-direct factors (b6 .x, as the lighting read it).
void ls_ao_factors(uint2 fp, Bg3Surface sf, out float ao, out float fdif, out float fspec) {
  ao = lFlags.m0.x > 0.0 ? gAO[fp].x : 1.0;
  float spec_occ = lFlags.m0.x > 0.0 ? saturate(pow(max(abs(sf.nov_raw + ao), 9.9999997e-05), exp2(-16.0 * sf.a - 1.0)) - 1.0 + ao) : 1.0;
  fdif = lerp(1.0, ao, lFlags.m0.y);
  fspec = lerp(1.0, spec_occ, lFlags.m0.y);
}

// The sun's direct light as the lighting computed it (rt/bg3_sun.hlsli).
void ls_sun(uint2 fp, Bg3Surface sf, Bg3Mat mt, out float3 sund, out float3 suns) {
  sund = 0;
  suns = 0;
  float m = pc.ls_has_mask != 0u && lFlags.m0.w > 0.0 ? gSunShadow[fp] : 1.0;
  float S = m > 0.0 ? m * bg3_cloud_shadow(sf.P) : 0.0;   // (0 x the finite cloud factor is 0)
  if (S > 0.0)
    bg3_sun_eval_mat(sf, mt, normalize(cam.v0.xyz), bg3_sun_colour() * (S * asfloat(lScale.Load(0))), S, sund, suns);
}

#if !LS_FULL
// ---- the default settings (LS_FULL=0) ----
//
// Three phases per chunk of 32 froxel list entries, so that the ray queries run with
// almost nothing else live (the per-class surface terms took the old single loop to
// 252 registers and 8 warps per SM, docs/TRACK_D_RT.md 9.9):
//   1. which entries are traced: the trace conditions (no game shadow map, range,
//      owner) and the exact acceptance tests of bg3_light_eval_mat (channel, range,
//      cone, N.L or the class's transmission/hair test), from the few surface terms
//      they need;
//   2. the rays, the k-th traced light of every lane together;
//   3. the rest of the surface (reloaded, not kept across the rays), each traced light's
//      exact unshadowed diffuse/specular, and the sums in list order, as before.
// The BRDF is evaluated only for the lights that are traced; the arithmetic of every
// term and sum is the old one, so the output is bit-identical.

// Phase 1: does this pass trace light `li`? R: its source radius.
bool ls_light_traced(uint li, bool spot, LsLite s, out float3 lpos, out float R) {
  R = pc.ls_source_radius;
  Bg3Light l = load_light(li);
  lpos = l.pos;
  // ls_shadowed=0: lights with a game shadow map keep the game's term.
  if ((l.flags & 1u) != 0u) return false;
  if (!(1.0 / max(l.inv_r, 1e-12) >= pc.ls_min_radius)) return false;
  if ((pc.ls_flags & 1u) != 0u) {
    uint4 si = lsSlots[uint2(min(li, 4095u), 0)];
    if ((si.z & 0x10000u) != 0u) {
      R = asfloat(si.x);
      if (((pc.ls_flags >> (8u + min(si.z & 255u, 4u))) & 1u) == 0u) return false;
    }
  }
  // bg3_light_eval_mat's "the game adds nothing for this light" tests, in its order and arithmetic.
  if ((l.chan & (1u << (s.channel & 31u))) == 0u) return false;
  float3 L = l.pos - s.P;
  float d2 = dot(L, L);
  float3 Ld = L * rsqrt(max(d2, 1.0000000e-10));
  float ir2 = l.inv_r * l.inv_r;
  if (!(d2 < 1.0 / ir2)) return false;
  if (spot && !(dot(-Ld, normalize(l.dir)) > l.cos_outer)) return false;
  float nol = dot(s.N, Ld);
  if (s.cls == 5u) return saturate(2.0 * nol + 0.5) > 0.0;
  if ((s.cls == 3u || s.cls == 4u) && s.trans && bg3_sss_wrap(nol) > 0.0) return true;
  return saturate(nol) > 0.0;
}

// Phase 2: what the ray to a traced light needs (as ls_light_traced found it).
void ls_light_ray(uint li, out float3 lpos, out float R) {
  lpos = bg3_light_pos(li);
  R = pc.ls_source_radius;
  if ((pc.ls_flags & 1u) != 0u) {
    uint4 si = lsSlots[uint2(min(li, 4095u), 0)];
    if ((si.z & 0x10000u) != 0u) R = asfloat(si.x);
  }
}

// Phases 1 and 2 for the list entries [base, base + n), n <= 32: x the entries traced,
// y those of them whose ray found no occluder.
uint2 ls_trace_chunk(uint2 fp, LsLite s, uint base, uint n, uint end_p) {
  uint traced = 0;
  for (uint j = 0; j < n; ++j) {
    float3 lpos;
    float R;
    if (ls_light_traced(bg3_light_index(base + j), base + j >= end_p, s, lpos, R)) traced |= 1u << j;
  }
  // the k-th traced light of every lane together
  uint lit = 0;
  for (uint m = traced; m != 0u; m &= m - 1u) {
    const uint j = firstbitlow(m);
    const uint li = bg3_light_index(base + j);
    float3 lpos;
    float R;
    ls_light_ray(li, lpos, R);
    if (trace_light(s.P, s.N, s.org, lpos, fp, li, s.cone, R) > 0.5) lit |= 1u << j;
  }
  return uint2(traced, lit);
}

bool ls_main(uint2 fp, inout float3 o_d, inout float3 o_s) {
  if (any(fp >= pc.full)) return false;
  float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) return false;
  LsLite s = (LsLite)0;
  if (!ls_lite(fp, d, s)) return false;   // the lighting skips unlit pixels

  const uint3 cl = bg3_cluster(fp, d);
  const uint end_p = cl.x + cl.y, end_s = end_p + cl.z;
  float3 dd = 0, ds = 0;   // delta diffuse / specular
  float3 td = 0, ts = 0;   // direct light of the traced lights after our shadow (ao_direct)
  LsPixel px = (LsPixel)0;
  uint base = cl.x;
  [loop] do {
    const uint n = min(end_s - base, 32u);
    // 1-2. the entries this pass traces, and their rays
    const uint2 tl = ls_trace_chunk(fp, s, base, n, end_p);
    const uint traced = tl.x, lit = tl.y;
    // 3. the rest of the surface (at an index the compiler cannot prove equal to fp, so that
    // it reloads it here instead of keeping it live across the rays), the exact terms
    ls_surface(WaveReadLaneAt(fp, WaveGetLaneIndex()), s, px.sf, px.mt);
    for (uint m3 = traced; m3 != 0u; m3 &= m3 - 1u) {
      const uint j = firstbitlow(m3);
      Bg3Light l = load_light(bg3_light_index(base + j));
      float3 cd, ct, cs, Ld;
      float d2, nol;
      if (!bg3_light_eval_mat(l, base + j >= end_p, px.sf, px.mt, cd, ct, cs, Ld, d2, nol)) continue;
      const float v = (lit >> j) & 1u ? 1.0 : 0.0;
      dd += cd * (v - 1.0);
      ds += cs * (v - 1.0);
      td += cd * v;
      ts += cs * v;
    }
    base += n;
  } while (base < end_s);

  float ao, fdif, fspec;
  ls_ao_factors(fp, px.sf, ao, fdif, fspec);
  // ---- the sun's direct light, as the lighting computed it (rt/bg3_sun.hlsli) ----
  // It only enters through (td_f - fdif) below. In mode 1 that factor is 1 - fdif, so
  // where both AO factors are exactly 1 the sun's share is 0 and it is not recomputed.
  const uint ao_mode = pc.ao_direct & 3u;
  float3 sund = 0, suns = 0;
  if ((pc.ao_direct & 8u) != 0u && ((ao_mode != 0u && (pc.ao_direct & 4u) != 0u) || ao_mode == 2u) &&
      (ao_mode == 2u || fdif != 1.0 || fspec != 1.0))
    ls_sun(fp, px.sf, px.mt, sund, suns);
  // ---- AO on direct light (docs/AUDIT_RT.md fix 4; the modes are listed under LS_FULL) ----
  // (The same expressions as the LS_FULL path, so that the arithmetic is the same.)
  float3 xd = 0, xs = 0;
  if (ao_mode != 0u) {
    float ssao = ao_mode == 2u && (pc.ao_direct & 16u) != 0u ? gAux[fp].x : ao;   // (mode 1 does not use it)
    float so = lFlags.m0.x > 0.0 ? saturate(pow(max(abs(px.sf.nov_raw + ssao), 9.9999997e-05), exp2(-16.0 * px.sf.a - 1.0)) - 1.0 + ssao) : 1.0;
    float td_f = ao_mode == 1u ? 1.0 : lerp(1.0, lFlags.m0.x > 0.0 ? ssao : 1.0, lFlags.m0.y);
    float ts_f = ao_mode == 1u ? 1.0 : lerp(1.0, so, lFlags.m0.y);
    float3 dir_d = td, dir_s = ts;
    if (ao_mode == 2u || (pc.ao_direct & 4u) != 0u) {
      dir_d += sund;
      dir_s += suns;
    }
    xd = dir_d * (td_f - fdif);
    xs = dir_s * (ts_f - fspec);
  }

  float3 gd = gDiffuse[fp], gs = gSpecular[fp];
  float k = saturate(pc.ls_strength);
  float3 od = max(gd + dd * (fdif * k) + xd, 0.0);
  float3 os = max(gs + ds * (fspec * k) + xs, 0.0);
  // Never hand the game a non-finite value.
  if (any(!isfinite(od))) od = gd;
  if (any(!isfinite(os))) os = gs;
  o_d = od;
  o_s = os;
  return true;
}

#else
// ---- every mode (LS_FULL=1) ----

// ls_debug=9: owner colours (unmatched red, placed white, item orange, scenery green,
// character magenta, unknown visual light cyan).
float3 owner_colour(uint owner, bool matched) {
  if (!matched) return float3(1, 0, 0);
  return owner == 1u ? float3(1, 1, 1) : owner == 2u ? float3(1, 0.5, 0) : owner == 3u ? float3(0, 1, 0)
       : owner == 4u ? float3(1, 0, 1) : float3(0, 1, 1);
}

// Lanes' sum of v added to counter `index` (fixed point), spread over 64 bins.
void stat_addv(uint index, uint v, uint bin) {
  uint n = WaveActiveSum(v);
  uint b = WaveReadLaneFirst(bin) & 63u;
  if (WaveIsFirstLane() && n) rtStats.InterlockedAdd((index * 64 + b) * 4, n);
}
uint fx(float v) { return uint(clamp(v, 0.0, 64.0) * 256.0 + 0.5); }

bool ls_main(uint2 fp, inout float3 o_d, inout float3 o_s) {
  if (any(fp >= pc.full)) return false;
  LsPixel px = (LsPixel)0;
  if (!ls_pixel(fp, px)) return false;
  const float d = px.d;
  const Bg3Surface sf = px.sf;
  const Bg3Mat mt = px.mt;
  const uint cls = px.cls;
  const RtCone cone = px.cone;
  const bool validate = pc.ls_debug >= 4 && pc.ls_debug <= 7;

  uint3 cl = bg3_cluster(fp, d);
  uint end_p = cl.x + cl.y, end_s = end_p + cl.z;
  float3 dd = 0, ds = 0;          // delta diffuse / specular
  float3 md = 0, ms = 0;          // validation: recomputed lighting with the game's shadows
  float3 cd_all = 0, cs_all = 0;  // debug: unshadowed sum of the traced lights
  float3 vd_all = 0;              // debug: traced-visible part
  float3 td = 0, ts = 0;          // direct light of the traced lights after our shadow (ao_direct)
  uint n_lights = 0, n_shadowed = 0, n_traced = 0, n_occ = 0;
  uint n_matched = 0, n_se = 0, n_skipped = 0, n_unmatched = 0;   // per-light data (ls_stats)
  float3 dbg_owner = 0;                                            // ls_debug=9
  // RIS reservoirs (ls_samples > 0)
  const uint ns = min(pc.ls_samples, 2u);
  float wsum = 0;
  float3 rd[2] = {float3(0, 0, 0), float3(0, 0, 0)}, rs[2] = {float3(0, 0, 0), float3(0, 0, 0)}, rp[2] = {float3(0, 0, 0), float3(0, 0, 0)};
  float rw[2] = {0, 0}, rsh[2] = {1, 1}, rsel[2] = {0, 0}, rR[2] = {0, 0};
  uint rk[2] = {0, 0};

  for (uint i = cl.x; i < end_s; ++i) {
    uint li = bg3_light_index(i);
    bool spot = i >= end_p;
    Bg3Light l = load_light(li);
    // cd: diffuse the shadow multiplies, ct: transmission it does not (thickness profiles).
    float3 cd, ct, cs, Ld;
    float d2, nol;
    if (!bg3_light_eval_mat(l, spot, sf, mt, cd, ct, cs, Ld, d2, nol)) continue;
    ++n_lights;
    bool shadowed = (l.flags & 1u) != 0u;
    float s = 1.0;
    if (shadowed && (validate || pc.ls_shadowed != 0)) s = bg3_atlas_shadow(l, spot, sf, Ld, d2, saturate(nol));
    n_shadowed += shadowed ? 1u : 0u;
    if (validate) {
      md += cd * s + ct;
      ms += cs * s;
      continue;
    }
    // Hair replaces its self-shadow by the atlas term for lights with a shadow map
    // (3033c895:483), which bg3_light_eval_mat does not model: those stay the game's.
    bool traced = shadowed ? pc.ls_shadowed != 0 && cls != kMatHair : (1.0 / max(l.inv_r, 1e-12) >= pc.ls_min_radius);
    // Per-light data: the source radius, and whether this owner's lights are shadowed.
    float R = pc.ls_source_radius;
    if ((pc.ls_flags & 1u) != 0u) {
      uint4 si = lsSlots[uint2(min(li, 4095u), 0)];
      bool matched = (si.z & 0x10000u) != 0u;
      uint owner = si.z & 255u;
      if (matched) {
        ++n_matched;
        n_se += (si.y & 0x10000u) != 0u ? 1u : 0u;
        R = asfloat(si.x);
        if (((pc.ls_flags >> (8u + min(owner, 4u))) & 1u) == 0u) {
          n_skipped += traced ? 1u : 0u;
          traced = false;
        }
      } else {
        ++n_unmatched;
      }
      dbg_owner += lum(cd + cs) * owner_colour(owner, matched);
    }
    if (!traced) continue;
    cd_all += cd;
    cs_all += cs;
    if (ns == 0) {
      float v = trace_light(sf.P, sf.N, sf.org, l.pos, fp, li, cone, R);
      ++n_traced;
      n_occ += v < 0.5 ? 1u : 0u;
      float t = shadowed && pc.ls_shadowed == 2 ? min(v, s) : v;
      dd += cd * (t - s);
      ds += cs * (t - s);
      td += cd * t;
      ts += cs * t;
      vd_all += cd * v;
    } else {
      float w = lum(cd + cs);
      if (!(w > 0.0)) continue;
      wsum += w;
      uint3 rr = ls_rand(fp, li ^ 0x200u);
      [unroll] for (uint k = 0; k < 2; ++k) {
        if (k >= ns) break;
        if (u01(k == 0 ? rr.x : rr.y) * wsum < w) {
          rd[k] = cd; rs[k] = cs; rp[k] = l.pos; rw[k] = w; rsh[k] = s; rk[k] = li; rR[k] = R;
          rsel[k] = shadowed && pc.ls_shadowed == 2 ? 1.0 : 0.0;
        }
      }
    }
  }
  if (ns > 0 && wsum > 0.0) {
    [unroll] for (uint k = 0; k < 2; ++k) {
      if (k >= ns || !(rw[k] > 0.0)) continue;
      float v = trace_light(sf.P, sf.N, sf.org, rp[k], fp, rk[k] ^ (0x300u + k), cone, rR[k]);
      ++n_traced;
      n_occ += v < 0.5 ? 1u : 0u;
      float t = rsel[k] > 0.5 ? min(v, rsh[k]) : v;
      float inv_p = wsum / (rw[k] * float(ns));
      dd += rd[k] * ((t - rsh[k]) * inv_p);
      ds += rs[k] * ((t - rsh[k]) * inv_p);
      td += rd[k] * (t * inv_p);
      ts += rs[k] * (t * inv_p);
      vd_all += rd[k] * (v * inv_p);
    }
  }

  // ---- the game's factors on direct light: AO (b6 .x, as the lighting read it) ----
  float ao, fdif, fspec;
  ls_ao_factors(fp, sf, ao, fdif, fspec);

  // ---- the sun's direct light, as the lighting computed it (rt/bg3_sun.hlsli) ----
  const uint ao_mode = pc.ao_direct & 3u;
  const bool sun_in = (pc.ao_direct & 8u) != 0u;
  float3 sund = 0, suns = 0;
  if (sun_in && ((ao_mode != 0u && (pc.ao_direct & 4u) != 0u) || ao_mode == 2u || pc.ls_debug == 7)) {
    ls_sun(fp, sf, mt, sund, suns);
    if (validate) {   // (ls_debug=7 only: the prep pass zeroes the mask otherwise)
      md += sund;
      ms += suns;
    }
  }
  // ---- AO on direct light (docs/AUDIT_RT.md fix 4) ----
  // The lighting gives direct light mix(1, AO, m0.y) of the AO in b6, which holds
  // our 1-ray RT visibility once gi_ao_apply has run. ao_direct:
  //   0 leave it (the RT visibility darkens direct light, noisily, and contacts
  //     the RT shadows already resolve are darkened twice);
  //   1 none on the lights whose shadow is ray traced (D-LS lights, the sun with
  //     rt_shadows): their visibility IS the occlusion (default);
  //   2 the game's own factor with its own SSAO (the look of the original, noise-free).
  float3 xd = 0, xs = 0;
  if (ao_mode != 0u && !validate) {
    float ssao = (pc.ao_direct & 16u) != 0u ? gAux[fp].x : ao;
    float so = lFlags.m0.x > 0.0 ? saturate(pow(max(abs(sf.nov_raw + ssao), 9.9999997e-05), exp2(-16.0 * sf.a - 1.0)) - 1.0 + ssao) : 1.0;
    float td_f = ao_mode == 1u ? 1.0 : lerp(1.0, lFlags.m0.x > 0.0 ? ssao : 1.0, lFlags.m0.y);
    float ts_f = ao_mode == 1u ? 1.0 : lerp(1.0, so, lFlags.m0.y);
    float3 dir_d = td, dir_s = ts;
    if (ao_mode == 2u || (pc.ao_direct & 4u) != 0u) {
      dir_d += sund;
      dir_s += suns;
    }
    xd = dir_d * (td_f - fdif);
    xs = dir_s * (ts_f - fspec);
  }

  float3 gd = gDiffuse[fp], gs = gSpecular[fp];
  float3 od = gd, os = gs;
  if (validate) {
    md *= fdif;
    ms *= fspec;
    if (pc.ls_debug == 4 || pc.ls_debug == 6) {   // residual in grey; red where the recompute exceeds the game
      float over = lum(max(md - gd, 0.0) + max(ms - gs, 0.0));
      od = over > 1e-3 + 0.01 * lum(gd + gs) ? float3(1, 0, 0) : (gd + gs - md - ms);
      os = 0;
    } else {
      od = md;
      os = ms;
    }
  } else if (pc.ls_debug == 1) {   // light count: red unshadowed, green shadowed (1 = 8 lights)
    od = float3(float(n_lights - n_shadowed), float(n_shadowed), 0.0) / 8.0 * 0.5;
    os = 0;
  } else if (pc.ls_debug == 2) {   // light removed by the traced shadows, x4
    od = max(-(dd * fdif + ds * fspec), 0.0) * 4.0;
    os = 0;
  } else if (pc.ls_debug == 3) {   // traced visibility of the traced lights (luminance-weighted)
    float c = lum(cd_all);
    od = (c > 1e-6 ? saturate(lum(vd_all) / c) : 1.0).xxx * 0.5;
    os = 0;
  } else if (pc.ls_debug == 9) {   // the evaluated lights by owner (per-light data), luminance-weighted
    od = dbg_owner * 4.0;
    os = 0;
  } else if (pc.ls_debug == 8) {   // light given back by ao_direct (AO no longer on traced direct light), x4
    od = (xd + xs) * 4.0;
    os = 0;
  } else {
    float k = saturate(pc.ls_strength);
    od = max(gd + dd * (fdif * k) + xd, 0.0);
    os = max(gs + ds * (fspec * k) + xs, 0.0);
  }
  // Never hand the game a non-finite value.
  if (any(!isfinite(od))) od = gd;
  if (any(!isfinite(os))) os = gs;
  o_d = od;
  o_s = os;

  if (pc.ls_stats != 0) {
    uint bin = (fp.x >> 3) * 7 + (fp.y >> 3) * 13;
    stat_addv(16, 1u, bin);
    stat_addv(17, n_lights, bin);
    stat_addv(18, n_shadowed, bin);
    stat_addv(19, n_traced, bin);
    // Validation sums only where no projector light is listed (they are not recomputed).
    bool vpx = bg3_cluster_projectors(fp, d) == 0u;
    stat_addv(21, vpx ? fx(lum(gd + gs)) : 0u, bin);
    stat_addv(22, vpx ? fx(lum((md + ms))) : 0u, bin);
    stat_addv(15, vpx ? fx(lum(abs(gd - md) + abs(gs - ms))) : 0u, bin);
    // validation: the recompute above the game's own output (must be ~0), and where
    float over = lum(max(md - gd, 0.0) + max(ms - gs, 0.0));
    stat_addv(23, vpx ? fx(over) : 0u, bin);
    // the same, on the non-standard material classes alone
    bool nstd = cls != kMatStandard;
    stat_addv(69, nstd ? 1u : 0u, bin);
    bool nv = validate && nstd && vpx && n_lights > 0;
    stat_addv(64, nv ? 1u : 0u, bin);
    stat_addv(65, nv ? fx(lum(gd + gs)) : 0u, bin);
    stat_addv(66, nv ? fx(lum(md + ms)) : 0u, bin);
    stat_addv(67, nv ? fx(over) : 0u, bin);
    stat_addv(68, nv ? fx(lum(abs(gd - md) + abs(gs - ms))) : 0u, bin);
    // Regression game = k * recomputed + r over lit pixels, and r where no local light
    // reaches (validation: k must be 1 and r the same in both if the recompute is exact).
    if (validate) {
      float m = lum(md + ms), g = lum(gd + gs);
      // standard-class pixels only (the other classes have their own counters, 48-52)
      bool lit = vpx && n_lights > 0 && cls == kMatStandard, unlit = vpx && n_lights == 0 && cls == kMatStandard;
      stat_addv(32, lit ? 1u : 0u, bin);
      stat_addv(33, lit ? uint(min(m, 0.5) * 1e5) : 0u, bin);
      stat_addv(34, lit ? uint(min(g, 0.5) * 1e5) : 0u, bin);
      stat_addv(35, lit ? uint(min(m * m, 0.02) * 1e7) : 0u, bin);
      stat_addv(36, lit ? uint(min(m * g, 0.02) * 1e7) : 0u, bin);
      stat_addv(37, unlit ? 1u : 0u, bin);
      stat_addv(38, unlit ? uint(min(g, 0.5) * 1e5) : 0u, bin);
    }
    stat_addv(20, validate && over > 1e-3 + 0.01 * lum(gd + gs) ? 1u : n_occ, bin);
    stat_addv(24, fx(-lum(dd * fdif + ds * fspec)), bin);
    stat_addv(74, fx(lum(xd + xs)), bin);   // light given back by ao_direct
    stat_addv(75, fx(lum(sund * fdif + suns * fspec)), bin);
    // per-light data: evaluations matched (any source / the bridge), owner not traced, unmatched
    stat_addv(76, n_matched, bin);
    stat_addv(77, n_se, bin);
    stat_addv(78, n_skipped, bin);
    stat_addv(79, n_unmatched, bin);   // the sun as recomputed (with the game's AO factor)
    stat_addv(25, n_lights > 0 ? 1u : 0u, bin);
    stat_addv(26, cl.y + cl.z, bin);   // lights listed in the froxel
    stat_addv(31, bg3_cluster_projectors(fp, d), bin);   // projector lights listed (not handled)
    if (all(fp == uint2(0, 0))) {
      // raw values, read from the latest slot (not summed)
      rtStats.Store((27 * 64) * 4, asuint(lFlags.m0.x));
      rtStats.Store((28 * 64) * 4, asuint(lFlags.m0.y));
      rtStats.Store((29 * 64) * 4, asuint(lFlags.m0.w));
      rtStats.Store((30 * 64) * 4, lScale.Load(0));
      rtStats.Store((70 * 64) * 4, asuint((pc.mat_classes & 8u) != 0u ? gFrame.b.x : -1.0));   // set0/b0 _m4
    }
  }
  return true;
}
#endif

#ifndef LS_NO_MAIN
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  float3 od = 0, os = 0;
  if (ls_main(tid.xy, od, os)) {
    gDiffuse[tid.xy] = od;
    gSpecular[tid.xy] = os;
  }
}
#endif
