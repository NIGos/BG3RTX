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
//   0 left alone (the game's atlas term);
//   1 replace the atlas with the ray-traced visibility;
//   2 min(traced, atlas);
//   3 (default) replace it where the segment to the light lies in the view frustum (every
//     caster on it is in the TLAS), min(traced, atlas) elsewhere (ls_atlas_fallback).
// The default variant (LS_FULL=0) handles every ls_shadowed mode.
//
// Sampling: with RR (rr=1) and ls_source_radius > 0, one ray per light and pixel
// towards a uniform point of the light's sphere (solid angle), white noise from
// pcg3d keyed by (pixel, frame, light): RR denoises the penumbra. Without RR
// (no denoiser), one deterministic ray to the light centre (hard shadows).
// ls_samples > 0 instead traces that many rays per pixel to lights chosen by
// RIS over the unshadowed luminance (unbiased, noisier, cheaper with many lights).
// ls_k > 0 (default 0; variant LS_K=1, gi/rt_local_shadows*_k.hlsl, TRACK_D_LOCAL_LIGHTS 9.11):
// the ls_k strongest lights exactly, ls_rest_samples RIS rays for the rest (unbiased, at most
// ls_k + ls_rest_samples rays per pixel; common/ls_exact_k.h). With LS_K=0 none of it is compiled.
//
// Per-light data (ls_lights=1, ls_flags bit 0; docs/SE_BRIDGE.md): lsSlots holds, per
// light array slot, what rt_local_shadows_match.hlsl joined to it from the Script
// Extender bridge or the asset database: the light's source radius (from its fixture
// class), its owner and its game flags. ls_skip_light() says which are left as the game
// lit them (ls_owners, now every owner by default, and the art-intent rule ls_noshadow).
// Unmatched lights keep ls_source_radius and are traced as before.
//
// Two variants of this file (docs/TRACK_D_RT.md 9.9; core/gi.cpp picks one per frame):
//   LS_FULL=0 (this file): ls_samples=0, ls_debug=0, ls_stats=0 (any ls_shadowed), in three phases per 32 list entries (tests, rays, terms;
//     see below): 96 registers instead of 252.
//   LS_FULL=1 (rt_local_shadows_full.hlsl): every mode, debug view and statistic, in
//     the single loop of before.
// Both give bit-identical output to the pre-optimisation pass (ls_check=1 compares them
// per pixel against its frozen copy, shaders/gi/ls_ref).
//
// Unknown rays (rt_common.hlsli rt_visibility: the candidate cap aborted the query): that
// light keeps the game's own term at that pixel, t = s, so its delta is 0: the atlas PCF
// for a light with a game shadow map, 1 for a light without one. For the latter the game's
// own visibility IS unshadowed (it never shadows those lights): this is the game's
// behaviour for that light, not a "lit by default" for the ray.
//
// QA ambient floor (qa_zero bit 1, docs/QA.md; ls_qa_off below): every point/spot light the
// game's tiled lighting evaluated at the pixel gets t = 0 and its transmission term ct is
// removed too, so the pass subtracts exactly what the game added for that light; no rays are
// traced. This includes the lights D-LS normally leaves alone (ls_shadowed=0, ranges below
// ls_min_radius, owners off in ls_owners, ls_noshadow). It cannot remove: a light with a game
// shadow map on a hair pixel (the game's hair self-shadow is not modelled, so c*s is not
// what it added), projector lights (cluster z, not recomputed), anything outside the tiled
// lighting's froxel list (forward-shaded draws, fog in-scatter, emissives), and nothing at
// all on frames D-LS does not run (rt_local_shadows=0, no TLAS, sun-set ring full) or in
// the validation views ls_debug=4-7. RIS sampling (ls_samples) is bypassed
// under it: the exact sum, no noise.
// ls_debug=10 (LS_FULL): pixels where any light's ray was unknown, in yellow.
//
// Validation (ls_debug=4/5/6): rt_local_shadows_prep.hlsl zeroes the game's AO and
// sun mask before the lighting, so its output holds only direct local light (plus
// projector lights and in-scatter, which are not recomputed).
#include "gi_common.hlsli"
#include "gi_aux_ao.hlsli"
#include "../rt/rt_common.hlsli"
#include "../rt/bg3_lights.hlsli"
#include "../rt/bg3_sun.hlsli"
#if LS_NATIVE_DIRECT_VALIDATION
#include "../../common/ufloat_interval.h"
#endif

#ifndef LS_FULL
#define LS_FULL 0
#endif
// Box (projector) lights (ls_box=1; docs/TRACK_D_RT.md 9.12): rt_local_shadows_box.hlsl and
// rt_local_shadows_full_box.hlsl. They read the lighting's own set 1 (bound as set 3), so they
// need their own pipeline layout; with LS_BOX=0 none of the box code is compiled and the pass
// is the one before (ls_check against the frozen reference unchanged).
#ifndef LS_BOX
#define LS_BOX 0
#endif
#if LS_BOX
#include "../rt/bg3_boxes.hlsli"
#endif
#if LS_BOX && LS_FULL
#include "../../common/ufloat_interval.h"
// Box statistics (ls_stats=1): a device-local buffer (set 4 b0, core/gi.cpp ensure_bx), reduced per
// 8x8 group in groupshared memory (one atomic per counter and group), copied after the pass into
// rtStats 385..416 (kLsBoxXFirst). Counters 0-15 counts, 16-31 eight 64-bit sums (Q24: 24 fractional bits)
// as (low, high) words. Never host-visible atomics (META_AUDIT 2026-11-01 5).
[[vk::binding(0, 4)]] RWByteAddressBuffer lsBoxStats;
static const uint kBxN = 32u, kBxPairs = 16u;
groupshared uint gsBx[kBxN];
void bx_add(uint k, uint v) {
  const uint w = WaveActiveSum(v);
  if (WaveIsFirstLane() && w != 0u) InterlockedAdd(gsBx[k], w);
}
void bx_sum(uint k, float x) {
  const float w = WaveActiveSum(isfinite(x) && x > 0.0 ? x : 0.0);
  if (WaveIsFirstLane() && w > 0.0) {
    const float q = min(round(w * 16777216.0), 1.8e19);
    const uint hi = uint(floor(q / 4294967296.0));
    const uint lo = uint(q - float(hi) * 4294967296.0);
    uint old;
    InterlockedAdd(gsBx[k], lo, old);
    const uint up = hi + (old > 0xffffffffu - lo ? 1u : 0u);
    if (up != 0u) InterlockedAdd(gsBx[k + 1u], up);
  }
}
// After the group's barrier: thread gi adds counter gi (or the sum pair 16 + 2 (gi - 16)) to the bin of
// its group (the host sums the 64 bins of each counter).
void bx_flush(uint gi, uint2 gid) {
  const uint bin = (gid.x * 7u + gid.y * 13u) & 63u;
  if (gi < kBxPairs) {
    const uint v = gsBx[gi];
    if (v != 0u) lsBoxStats.InterlockedAdd((gi * 64u + bin) * 4u, v);
  } else if (gi < kBxPairs + (kBxN - kBxPairs) / 2u) {
    const uint k = kBxPairs + 2u * (gi - kBxPairs);
    const uint lo = gsBx[k], hi = gsBx[k + 1u];
    uint old = 0u;
    if (lo != 0u) lsBoxStats.InterlockedAdd((k * 64u + bin) * 4u, lo, old);
    const uint up = hi + (lo != 0u && old > 0xffffffffu - lo ? 1u : 0u);
    if (up != 0u) lsBoxStats.InterlockedAdd(((k + 1u) * 64u + bin) * 4u, up);
  }
}
// The counters (core/gi.cpp "ls box trace", "ls box bound").
static const uint kBxBoundPx = 0u, kBxBoundOver = 1u, kBxBoundInconcl = 2u, kBxBoundCh = 3u, kBxBoundChOver = 4u;
static const uint kBxShTraced = 5u, kBxShOcc = 6u, kBxShOccAtlasLit = 7u, kBxShCut = 8u;
static const uint kBxUnTraced = 9u, kBxUnOcc = 10u, kBxUnFixture = 11u, kBxUnknown = 12u;
static const uint kBxGame = 16u, kBxRec = 18u, kBxBox = 20u, kBxExcess = 22u, kBxRemUn = 24u, kBxRemSh = 26u;
#endif
#ifndef LS_K
#define LS_K 0
#endif
#if defined(RT_OCC_STATS) || defined(RT_SPEC_OCC_STATS)
// rt_occ_stats (common/occ_stats.h, core/gi.cpp): the occluder kinds of the point/spot and box rays
// from character pixels. Only in the every-mode box variant (its group barrier; set 4 is the box
// statistics set, binding 1 the occluder counters).
#if !(LS_BOX && LS_FULL) || LS_K
#error "RT_OCC_STATS needs LS_BOX=1, LS_FULL=1, LS_K=0 (rt_local_shadows_full_box.hlsl)"
#endif
#include "../rt/occ_stats.hlsli"
#endif

[[vk::binding(46, 0)]] [[vk::image_format("rgba32ui")]] RWTexture2D<uint4> lsSlots;

float lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// ---- which lights are traced (docs/TRACK_D_RT.md 9.10) ----
// Bg3RtLightFlags (common/assetdb_api.h), in lsSlots .y of a matched light.
static const uint kRtlShadow = 32u, kRtlCastShadow = 64u, kRtlPlayer = 256u;
// A matched light left exactly as the game lit it: its owner's bit is clear in ls_owners
// (ls_flags bits 8-12), or the art-intent rule ls_noshadow (bits 16-17) applies: the game's
// template sets neither Shadow nor CastShadow and the light is (1) the darkvision light that
// follows the active character, or (2) any light (strict; 98% of placed point lights).
bool ls_skip_light(uint4 si) {
  if (((pc.ls_flags >> (8u + min(si.z & 255u, 4u))) & 1u) == 0u) return true;
  const uint rule = (pc.ls_flags >> 16) & 3u;
  const bool noshadow = (si.y & (kRtlShadow | kRtlCastShadow)) == 0u;
  return noshadow && (rule == 2u || (rule == 1u && (si.y & kRtlPlayer) != 0u));
}

// Lights with a game shadow map (ls_shadowed): the visibility that replaces the atlas term s.
//   1 the traced visibility; 2 min(traced, atlas);
//   3 the traced visibility where the whole segment surface -> light is inside the view
//     frustum (the light projects on screen: the frustum is convex and the surface is on
//     screen), so every caster along it was drawn and is in the TLAS; elsewhere
//     min(traced, atlas), since the atlas has the off-screen casters the TLAS lacks.
bool ls_light_on_screen(float3 lpos) {
  float2 uv;
  float d;
  return project(frustum_inverse(), lpos, uv, d) && all(uv >= 0.0) && all(uv <= 1.0);
}
bool ls_atlas_fallback(float3 lpos) {
  return pc.ls_shadowed == 2u || (pc.ls_shadowed == 3u && !ls_light_on_screen(lpos));
}

// QA ambient floor (qa_zero bit 1): every light this pass can remove gets visibility 0.
bool ls_qa_off() { return (pc.qa_zero & 2u) != 0u; }

uint3 ls_rand(uint2 fp, uint key) { return pcg3d(uint3(fp, pc.frame * 1024u + (key & 1023u))); }
float u01(uint x) { return float(x >> 8) * (1.0 / 16777216.0); }

float3 n_world(float3 nv) { return nv.x * cam.m6_r0.xyz + nv.y * cam.m6_r1.xyz + nv.z * cam.m6_r2.xyz; }
float3 n_view_at(int2 p) { return decode_normal(gNormal.Load(int3(clamp(p, int2(0, 0), int2(pc.full) - 1), 0)).xy); }
float3 nan0(float3 v) { return any(isnan(v)) ? float3(0, 0, 0) : v; }

// Visibility of the light from the surface (kRtLit / kRtOccluded / kRtUnknown): 1 ray,
// towards the centre or a point of its sphere. `key` selects the random stream. The ray
// starts on the side of the surface its direction leaves on (back-lit transmission, hair).
uint trace_light(float3 P, float3 N, float4 org, float3 lpos, uint2 fp, uint key, RtCone cone, float R) {
  float3 L = lpos - P;
#ifdef RT_HAIR_PHYSICAL_ORIGIN
  float3 receiver;bool physical_receiver=rt_hair_receiver(fp,P,receiver);
  if(physical_receiver)L=lpos-receiver;
#endif
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
    float3 t = abs(Ld.y) < 0.99 ? normalize(cross(float3(0, 1, 0), Ld)) : normalize(cross(float3(0, 0, 1), Ld));
    float3 b = cross(Ld, t);
    dir = normalize(Ld * ct + (t * cos(phi) + b * sin(phi)) * st);
  }
  float3 o = RT_SECONDARY_ORIGIN(fp,org, P, N, cone.f0, dir);
  float tmax = length(lpos - o) - max(R, pc.ls_clear);
#ifdef RT_SPEC_HAIR_ORIGIN
  if(physical_receiver){
    HairLightSegment segment=hpo_light_segment(receiver,o,lpos,dir,pc.ls_soft!=0?R:0.0,pc.ls_soft!=0?pc.ls_clear:max(R,pc.ls_clear));
    if(!segment.valid)return kRtUnknown;dir=segment.direction;tmax=segment.tmax;
  }
#else
#ifdef RT_HAIR_PHYSICAL_ORIGIN
  if(physical_receiver){
    HairLightSegment segment=hpo_light_segment(receiver,o,lpos,dir,pc.ls_soft!=0?R:0.0,pc.ls_soft!=0?pc.ls_clear:max(R,pc.ls_clear));
    if(!segment.valid)return kRtUnknown;dir=segment.direction;tmax=segment.tmax;
  }
#endif
#endif
  if (tmax <= 0.0) return kRtLit;
  return rt_visibility(o, dir, tmax, cone);
}

// The pixel as f469db78 sets it up, in two parts: what the light tests and the rays need
// (LsLite), and the rest of the surface and the class terms (ls_surface).
struct LsLite {
  #ifdef RT_HAIR_PHYSICAL_ORIGIN
  uint2 pixel;
  #endif
  float3 P, N;
  float4 org;
  RtCone cone;
  uint channel, cls;
  bool trans;   // Bg3Mat.trans
};
// False: nothing to do (the lighting skips unlit pixels).
bool ls_lite(uint2 fp, float d, out LsLite s) {
  s = (LsLite)0;
  #ifdef RT_HAIR_PHYSICAL_ORIGIN
  s.pixel=fp;
  #endif
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
  #ifdef RT_HAIR_PHYSICAL_ORIGIN
  s.pixel=fp;
  #endif
  if (!ls_lite(fp, d, s)) return false;
  ls_surface(fp, s, px.sf, px.mt);
  px.cls = s.cls;
  px.d = d;
  px.cone = s.cone;
  return true;
}

// The game's AO-on-direct factors (b6 .x, as the lighting read it).
void ls_ao_factors(uint2 fp, Bg3Surface sf, out float ao, out float fdif, out float fspec) {
#if LS_NATIVE_DIRECT_VALIDATION
  if((pc.ls_flags&(1u<<19))!=0u) {ao=gAO[fp].x;fdif=1.0;fspec=1.0;return;}
#endif
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

// The pass's output from the delta sums: the sun recompute, the AO-on-direct give-back
// (ao_direct) and the guarded result (the default variant's tail).
void ls_finish(uint2 fp, Bg3Surface sf, Bg3Mat mt, float3 dd, float3 ds, float3 td, float3 ts, out float3 o_d,
               out float3 o_s) {
  float ao, fdif, fspec;
  ls_ao_factors(fp, sf, ao, fdif, fspec);
  // ---- the sun's direct light, as the lighting computed it (rt/bg3_sun.hlsli) ----
  // It only enters through (td_f - fdif) below. In mode 1 that factor is 1 - fdif, so
  // where both AO factors are exactly 1 the sun's share is 0 and it is not recomputed.
  const uint ao_mode = pc.ao_direct & 3u;
  float3 sund = 0, suns = 0;
  if ((pc.ao_direct & 8u) != 0u && ((ao_mode != 0u && (pc.ao_direct & 4u) != 0u) || ao_mode == 2u) &&
      (ao_mode == 2u || fdif != 1.0 || fspec != 1.0))
    ls_sun(fp, sf, mt, sund, suns);
  // ---- AO on direct light (docs/archive/AUDIT_RT.md fix 4; the modes are listed under LS_FULL) ----
  // (The same expressions as the LS_FULL path, so that the arithmetic is the same.)
  float3 xd = 0, xs = 0;
  if (ao_mode != 0u) {
    float ssao = ao_mode == 2u && (pc.ao_direct & 16u) != 0u ? aux_ao_at(fp) : ao;   // (mode 1 does not use it)
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
    xd = ls_giveback_cap(xd, gDiffuse[fp] + dd * (fdif * saturate(pc.ls_strength)), td_f, fdif);
    xs = ls_giveback_cap(xs, gSpecular[fp] + ds * (fspec * saturate(pc.ls_strength)), ts_f, fspec);
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
}

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
  const bool qa = ls_qa_off();   // QA: every light but hair x shadow map (see the top)
  // Lights with a game shadow map: ls_shadowed=0 keeps the game's term; hair replaces its
  // self-shadow by the atlas term for them (3033c895:483), which is not modelled: the game's.
  if ((l.flags & 1u) != 0u) {
    if ((pc.ls_shadowed == 0u && !qa) || s.cls == kMatHair) return false;
  } else if (!qa && !(1.0 / max(l.inv_r, 1e-12) >= pc.ls_min_radius)) {
    return false;
  }
  if ((pc.ls_flags & 1u) != 0u) {
    uint4 si = lsSlots[uint2(min(li, 4095u), 0)];
    if ((si.z & 0x10000u) != 0u) {
      R = asfloat(si.x);
      if (!qa && ls_skip_light(si)) return false;
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

#if LS_BOX
// ---- box (projector) lights (ls_box=1; docs/TRACK_D_RT.md 9.12) ----
//
// The froxel's projector entries follow its spot lights in the index list b24 and index the
// box records b29 (rt/bg3_boxes.hlsli). Each one the game evaluated is treated like a point or
// spot light: its exact unshadowed contribution c (the class terms with the direction _m9,
// the box falloff and the cookie), one ray, the delta c (t - s). Rules as for point/spot:
//   - ls_shadowed for the box lights with a game shadow map (the atlas term s replicated: an
//     orthographic view); hair keeps it; mode 3's on-screen test uses the ray's exit point;
//   - no per-light data (the join covers the b25 array only): every box light is traced. Under
//     the default art-intent rule (ls_noshadow=1, only the darkvision point lights) that is
//     the same set; ls_noshadow=2 cannot be applied to them;
//   - the QA ambient floor removes them too (t = 0, transmission included);
//   - not in the RIS selection: always one exact ray each.
// The ray: from the surface along _m9 (a directional light has no extent: hard, no sampling),
// up to where it leaves the box on the light's side. The box is the light's extent, so
// geometry beyond that face is outside the light and does not shadow it.

// The world position under another pixel (non-finite where it has no depth, so that the
// cookie's uv difference is dropped, as the game's isnan test does).
float3 ls_P_at(int2 p) {
  p = clamp(p, int2(0, 0), int2(pc.full) - 1);
  const float d = gDepth.Load(int3(p, 0));
  const float2 uv = (float2(p) + 0.5) / float2(lFlags.res);
  return d > 0.0 ? ray_at(uv) * d : asfloat(0x7fc00000u).xxx;
}

float3 ls_box_dir(Bg3Box b) { return b.dir * rsqrt(max(dot(b.dir, b.dir), 1e-20)); }

// Phase 1: does this pass trace box light `bi` at this pixel (the game adds light, and the
// rules above)?
// ls_box=2 (Push::ls_flags bit 27): the box lights without a game shadow map are traced too, and
// every box ray covers only the segment that can shadow it (ls_box_ray).
bool ls_box_all_lights() { return (pc.ls_flags & (1u << 27)) != 0u; }

bool ls_box_traced(uint bi, LsLite s) {
  const Bg3Box b = load_box(bi);
  if ((b.flags & 1u) != 0u && ((pc.ls_shadowed == 0u && !ls_qa_off()) || s.cls == kMatHair)) return false;
  // Honour the game's shadow flag, including mode 2. Bit 29 enables the old A/B policy.
  if (!bl_shadow_selected(b.flags, (pc.ls_flags & (1u << 29)) != 0u)) return false;
  float3 uvw;
  if (!bg3_box_reaches(b, s.channel, s.P, uvw)) return false;
  if (!(bg3_box_falloff(b, uvw) > 0.0)) return false;   // c = 0: no delta
  const float nol = dot(s.N, b.dir);
  if (s.cls == 5u) return saturate(2.0 * nol + 0.5) > 0.0;
  if ((s.cls == 3u || s.cls == 4u) && s.trans && bg3_sss_wrap(nol) > 0.0) return true;
  return saturate(nol) > 0.0;
}

// The ray's origin and extent towards box light `b` (tmax <= 0: the origin left the box, or with
// ls_box=2 the receiver lies in the light's fade-in / before its shadow view: nothing to trace, lit).
// ls_box=2 ends it, within the box, at the shadow view's near plane (a light with a game shadow map:
// the segment the atlas term it replaces covers) or at the end of the light-side fade-in (without one).
float ls_box_ray(Bg3Box b, LsLite s, out float3 o, out float3 dir) {
  dir = ls_box_dir(b);
  o = RT_SECONDARY_ORIGIN(s.pixel,s.org, s.P, s.N, s.cone.f0, dir);
  float t = bg3_box_exit(b, o, dir);
  if (ls_box_all_lights())
    t = min(t, (b.flags & 1u) != 0u ? bg3_box_shadow_near(b, o, dir) : bg3_box_fixture_end(b, o, dir));
  return t;
}
#if LS_FULL
// Statistics: 1 when ls_box=2 cut the segment short of the box exit (still traced), 2 when the cut
// leaves nothing to trace (the receiver in the fade-in / before the near plane), 0 otherwise.
uint ls_box_cut(Bg3Box b, LsLite s) {
  if (!ls_box_all_lights()) return 0u;
  float3 o, dir;
  const float t = ls_box_ray(b, s, o, dir);
  const float te = bg3_box_exit(b, o, dir);
  return !(te > 0.0) ? 0u : !(t > 0.0) ? 2u : t < te ? 1u : 0u;
}
#endif

// Phase 2: the ray (kRtLit / kRtOccluded / kRtUnknown).
uint ls_box_trace(uint bi, LsLite s) {
  float3 o, dir;
  const float tmax = ls_box_ray(load_box(bi), s, o, dir);
  if (!(tmax > 0.0)) return kRtLit;
  return rt_visibility(o, dir, tmax, s.cone);
}

// ls_shadowed 2, or 3 where the segment surface -> box exit is not wholly in the view frustum.
bool ls_box_fallback(Bg3Box b, LsLite s) {
  if (pc.ls_shadowed == 2u) return true;
  if (pc.ls_shadowed != 3u) return false;
  float3 o, dir;
  const float tmax = ls_box_ray(b, s, o, dir);
  return !ls_light_on_screen(o + dir * max(tmax, 0.0));
}

// The pixel's quad partners (cookie gradients).
struct LsQuad {
  float3 Px, Py;
};
LsQuad ls_quad(uint2 fp) {
  LsQuad q;
  q.Px = ls_P_at(int2(fp.x ^ 1u, fp.y));
  q.Py = ls_P_at(int2(fp.x, fp.y ^ 1u));
  return q;
}

// Phase 3: box light `bi`'s exact terms. vs: its ray (ignored under QA). False: the game adds
// nothing. cd/ct/cs: unshadowed diffuse / unshadowed transmission / specular; sh the game's
// shadow term, t ours.
bool ls_box_eval(uint bi, LsLite s, Bg3Surface sf, Bg3Mat mt, LsQuad q, uint vs, out float3 cd, out float3 ct,
                 out float3 cs, out float sh, out float t, out bool cookie, out bool shadowed) {
  cd = 0; ct = 0; cs = 0; sh = 1.0; t = 1.0;
  const Bg3Box b = load_box(bi);
  cookie = (b.m1 >> 1) != 0u;
  shadowed = (b.flags & 1u) != 0u;
  float3 uvw;
  if (!bg3_box_reaches(b, sf.channel, sf.P, uvw)) return false;
  const float g = bg3_box_falloff(b, uvw);
  const float3 col = bg3_box_colour(b, uvw.xy, bg3_box_uvw(b, q.Px).xy - uvw.xy, bg3_box_uvw(b, q.Py).xy - uvw.xy);
  float nol;
  if (!bg3_box_eval_mat(b, col, g, sf, mt, cd, ct, cs, nol)) return false;
  const float v = vs == kRtLit ? 1.0 : 0.0;
  t = v;
  if (shadowed) {
    sh = bg3_box_atlas_shadow(b, sf, saturate(nol));
    if (ls_box_fallback(b, s)) t = min(v, sh);
  }
  if (vs == kRtUnknown) t = sh;   // unknown ray: the game's own term
  if (ls_qa_off()) t = 0.0;       // QA ambient floor: none of this light
  return true;
}

// Every box light of the pixel (list entries [first, first + n)), in the three phases of the
// point/spot loop; adds to the delta and ao_direct sums. px: reloaded here (phase 3).
// (The default variant; LS_FULL's ls_main has its own loop with the statistics.)
void ls_box_all(uint2 fp, LsLite s, uint first, uint n, inout LsPixel px, inout float3 dd, inout float3 ds,
                inout float3 td, inout float3 ts) {
  const bool qa = ls_qa_off();
  for (uint base = first; base < first + n; base += 32u) {
    const uint m = min(first + n - base, 32u);
    uint traced = 0;
    for (uint j = 0; j < m; ++j)
      if (ls_box_traced(bg3_light_index(base + j), s)) traced |= 1u << j;
    if (traced == 0u) continue;
    uint vsb[2] = {0u, 0u};   // 2 bits per entry: kRtLit / kRtOccluded / kRtUnknown
    for (uint mm = qa ? 0u : traced; mm != 0u; mm &= mm - 1u) {
      const uint j = firstbitlow(mm);
      const uint vs = ls_box_trace(bg3_light_index(base + j), s);
      vsb[j >> 4] |= vs << ((j & 15u) * 2u);
    }
    ls_surface(WaveReadLaneAt(fp, WaveGetLaneIndex()), s, px.sf, px.mt);
    const LsQuad q = ls_quad(fp);
    for (uint m3 = traced; m3 != 0u; m3 &= m3 - 1u) {
      const uint j = firstbitlow(m3);
      const uint vs = qa ? kRtOccluded : (vsb[j >> 4] >> ((j & 15u) * 2u)) & 3u;
      float3 cd, ct, cs;
      float sh, t;
      bool cookie, shadowed;
      if (!ls_box_eval(bg3_light_index(base + j), s, px.sf, px.mt, q, vs, cd, ct, cs, sh, t, cookie, shadowed)) continue;
      if (qa) dd -= ct;
      dd += cd * (t - sh);
      ds += cs * (t - sh);
      td += cd * t;
      ts += cs * t;
    }
  }
}
#endif

#if !LS_FULL
// Phases 1 and 2 for the list entries [base, base + n), n <= 32: x the entries traced,
// y those of them whose ray found no occluder, z those whose ray was aborted (unknown).
// QA (ls_qa_off): no rays, y = z = 0.
uint3 ls_trace_chunk(uint2 fp, LsLite s, uint base, uint n, uint end_p) {
  uint traced = 0;
  for (uint j = 0; j < n; ++j) {
    float3 lpos;
    float R;
    if (ls_light_traced(bg3_light_index(base + j), base + j >= end_p, s, lpos, R)) traced |= 1u << j;
  }
  // the k-th traced light of every lane together
  uint lit = 0, unk = 0;
  for (uint m = ls_qa_off() ? 0u : traced; m != 0u; m &= m - 1u) {
    const uint j = firstbitlow(m);
    const uint li = bg3_light_index(base + j);
    float3 lpos;
    float R;
    ls_light_ray(li, lpos, R);
    const uint vs = trace_light(s.P, s.N, s.org, lpos, fp, li, s.cone, R);
    if (vs == kRtLit) lit |= 1u << j;
    if (vs == kRtUnknown) unk |= 1u << j;
  }
  return uint3(traced, lit, unk);
}

bool ls_main(uint2 fp, inout float3 o_d, inout float3 o_s) {
  if (any(fp >= pc.full)) return false;
  g_rt_pass = kRtPassLocal;
  float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) return false;
  LsLite s = (LsLite)0;
  #ifdef RT_HAIR_PHYSICAL_ORIGIN
  s.pixel=fp;
  #endif
  if (!ls_lite(fp, d, s)) return false;   // the lighting skips unlit pixels
  const bool qa = ls_qa_off();

  const uint3 cl = bg3_cluster(fp, d);
  const uint end_p = cl.x + cl.y, end_s = end_p + cl.z;
  float3 dd = 0, ds = 0;   // delta diffuse / specular
  float3 td = 0, ts = 0;   // direct light of the traced lights after our shadow (ao_direct)
  LsPixel px = (LsPixel)0;
  uint base = cl.x;
  [loop] do {
    const uint n = min(end_s - base, 32u);
    // 1-2. the entries this pass traces, and their rays
    const uint3 tl = ls_trace_chunk(fp, s, base, n, end_p);
    const uint traced = tl.x, lit = tl.y, unk = tl.z;
    // 3. the rest of the surface (at an index the compiler cannot prove equal to fp, so that
    // it reloads it here instead of keeping it live across the rays), the exact terms
    ls_surface(WaveReadLaneAt(fp, WaveGetLaneIndex()), s, px.sf, px.mt);
    for (uint m3 = traced; m3 != 0u; m3 &= m3 - 1u) {
      const uint j = firstbitlow(m3);
      Bg3Light l = load_light(bg3_light_index(base + j));
      float3 cd, ct, cs, Ld;
      float d2, nol;
      const bool spot = base + j >= end_p;
      if (!bg3_light_eval_mat(l, spot, px.sf, px.mt, cd, ct, cs, Ld, d2, nol)) continue;
      const float v = (lit >> j) & 1u ? 1.0 : 0.0;
      // s: the game's own shadow term, which the delta replaces (1 without a shadow map).
      float s = 1.0, t = v;
      if ((l.flags & 1u) != 0u) {
        s = bg3_atlas_shadow(l, spot, px.sf, Ld, d2, saturate(nol));
        if (ls_atlas_fallback(l.pos)) t = min(v, s);
      }
      if ((unk >> j) & 1u) t = s;   // unknown ray: the game's own term (see the top)
      if (qa) {                     // QA ambient floor: none of this light (see the top)
        t = 0.0;
        dd -= ct;
      }
      dd += cd * (t - s);
      ds += cs * (t - s);
      td += cd * t;
      ts += cs * t;
    }
    base += n;
  } while (base < end_s);
#if LS_BOX
  ls_box_all(fp, s, end_s, bg3_cluster_projectors(fp, d), px, dd, ds, td, ts);
#endif

  ls_finish(fp, px.sf, px.mt, dd, ds, td, ts, o_d, o_s);
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
// Projector QA: exact integer sum with24 fractional bits, not fx's1/256.
// Bin is constant over each8x8 group;7 and13 are coprime to64, so a bin
// receives at most64*ceil(width/512)*ceil(height/8) invocations per dispatch.
// Half the uint64 capacity leaves rounding/carry headroom. Never silently cap.
void ls_sum24(uint index,float value,uint bin) {
  float group_bound=64.0*float((pc.full.x+511u)/512u)*float((pc.full.y+7u)/8u);
  float safe_max=549755813888.0/max(group_bound,1.0); //2^39 / bin population
  bool valid=isfinite(value) && value>=0.0 && value<safe_max;
  stat_addv(226,valid?0u:1u,bin);
  float q=round((valid?value:0.0)*16777216.0);
  uint hi=uint(floor(q/4294967296.0));
  uint lo=uint(q-float(hi)*4294967296.0);
  uint low16=WaveActiveSum(lo&65535u);
  uint high16=WaveActiveSum(lo>>16u)+(low16>>16u);
  uint sum_lo=(high16<<16u)|(low16&65535u);
  uint sum_hi=WaveActiveSum(hi)+(high16>>16u);
  uint b=WaveReadLaneFirst(bin)&63u;
  if(WaveIsFirstLane()) {
    uint old;rtStats.InterlockedAdd((index*64u+b)*4u,sum_lo,old);
    sum_hi+=old>0xffffffffu-sum_lo?1u:0u;
    if(sum_hi)rtStats.InterlockedAdd(((index+1u)*64u+b)*4u,sum_hi);
  }
}


bool ls_main(uint2 fp, inout float3 o_d, inout float3 o_s) {
  if (any(fp >= pc.full)) return false;
  g_rt_pass = kRtPassLocal;
  LsPixel px = (LsPixel)0;
  if (!ls_pixel(fp, px)) return false;
  const bool qa = ls_qa_off();
  const float d = px.d;
  const Bg3Surface sf = px.sf;
  const Bg3Mat mt = px.mt;
  const uint cls = px.cls;
  const RtCone cone = px.cone;
  const bool validate = pc.ls_debug >= 4 && pc.ls_debug <= 7;

  uint3 cl = bg3_cluster(fp, d);
  uint end_p = cl.x + cl.y, end_s = end_p + cl.z;
#if defined(RT_OCC_STATS) || defined(RT_SPEC_OCC_STATS)
  // A ray's darkening: the light it removes, unscaled by the AO-on-direct factor, over the pixel's
  // lit diffuse + specular before this pass.
  const float occ_den = max(lum(gDiffuse[fp] + gSpecular[fp]), 1e-8);
  uint occ_nl = 0, occ_nb = 0;
#endif
  float3 dd = 0, ds = 0;          // delta diffuse / specular
  float3 md = 0, ms = 0;          // validation: recomputed lighting with the game's shadows
  float3 cd_all = 0, cs_all = 0;  // debug: unshadowed sum of the traced lights
  float3 vd_all = 0;              // debug: traced-visible part
  float3 td = 0, ts = 0;          // direct light of the traced lights after our shadow (ao_direct)
  uint n_lights = 0, n_shadowed = 0, n_traced = 0, n_occ = 0;
  uint n_unk = 0;                 // rays aborted by the candidate cap (unknown; ls_debug=10)
  uint n_matched = 0, n_se = 0, n_skipped = 0, n_unmatched = 0;   // per-light data (ls_stats)
  float3 dbg_owner = 0;                                            // ls_debug=9
#if LS_BOX
  // ls_stats bound (debug 0): every evaluated point/spot light with the game's own shadow term.
  float3 rec_d = 0, rec_s = 0;
#endif
  // RIS reservoirs (ls_samples > 0)
  const uint ns = qa ? 0u : min(pc.ls_samples, 2u);   // (QA: the exact sum)
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
    if (shadowed && (validate || pc.ls_shadowed != 0 || qa)) s = bg3_atlas_shadow(l, spot, sf, Ld, d2, saturate(nol));
    n_shadowed += shadowed ? 1u : 0u;
#if LS_BOX
    {   // (ls_shadowed=0 does not compute s: the bound needs the game's own term)
      const float sg = shadowed && !(validate || pc.ls_shadowed != 0 || qa) ? bg3_atlas_shadow(l, spot, sf, Ld, d2, saturate(nol)) : s;
      rec_d += cd * sg + ct;
      rec_s += cs * sg;
    }
#endif
    if (validate) {
      md += cd * s + ct;
      ms += cs * s;
      continue;
    }
    // Hair replaces its self-shadow by the atlas term for lights with a shadow map
    // (3033c895:483), which bg3_light_eval_mat does not model: those stay the game's.
    // (QA ambient floor: every light but hair x shadow map, see the top.)
    bool traced = shadowed ? (pc.ls_shadowed != 0 || qa) && cls != kMatHair : (qa || 1.0 / max(l.inv_r, 1e-12) >= pc.ls_min_radius);
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
        if (!qa && ls_skip_light(si)) {
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
      const uint vs = qa ? kRtOccluded : trace_light(sf.P, sf.N, sf.org, l.pos, fp, li, cone, R);
      float v = vs == kRtLit ? 1.0 : 0.0;
      n_traced += qa ? 0u : 1u;
      n_occ += vs == kRtOccluded && !qa ? 1u : 0u;
      float t = shadowed && ls_atlas_fallback(l.pos) ? min(v, s) : v;
      if (vs == kRtUnknown) v = t = s;   // unknown ray: the game's own term (see the top)
      n_unk += vs == kRtUnknown ? 1u : 0u;
      if (qa) {                          // QA ambient floor: none of this light
        t = 0.0;
        dd -= ct;
      }
#if defined(RT_OCC_STATS) || defined(RT_SPEC_OCC_STATS)
      if (RT_OCC_ON && occ_character(cls) && !qa) {
        occ_ray(kOccLocal, vs, max(lum(cd * (s - t)) + lum(cs * (s - t)), 0.0) / occ_den);
        ++occ_nl;
      }
#endif
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
          rsel[k] = shadowed && ls_atlas_fallback(l.pos) ? 1.0 : 0.0;
        }
      }
    }
  }
  if (ns > 0 && wsum > 0.0) {
    [unroll] for (uint k = 0; k < 2; ++k) {
      if (k >= ns || !(rw[k] > 0.0)) continue;
      const uint vs = trace_light(sf.P, sf.N, sf.org, rp[k], fp, rk[k] ^ (0x300u + k), cone, rR[k]);
      float v = vs == kRtLit ? 1.0 : 0.0;
      ++n_traced;
      n_occ += vs == kRtOccluded ? 1u : 0u;
      float t = rsel[k] > 0.5 ? min(v, rsh[k]) : v;
      if (vs == kRtUnknown) v = t = rsh[k];   // unknown ray: the game's own term
      n_unk += vs == kRtUnknown ? 1u : 0u;
      float inv_p = wsum / (rw[k] * float(ns));
      dd += rd[k] * ((t - rsh[k]) * inv_p);
      ds += rs[k] * ((t - rsh[k]) * inv_p);
      td += rd[k] * (t * inv_p);
      ts += rs[k] * (t * inv_p);
      vd_all += rd[k] * (v * inv_p);
    }
  }

  // ---- box (projector) lights (ls_box=1): one exact ray each, also under ls_samples ----
  const uint n_proj = bg3_cluster_projectors(fp, d);
  float3 bgd = 0, bgs = 0;   // stats: the box light as the game lit it (c s), diffuse / specular
  float3 bdd = 0, bds = 0;   // stats: the box lights' delta
  uint nb_ev = 0, nb_sh = 0, nb_tr = 0, nb_occ = 0, nb_unk = 0, nb_ck = 0;
#if LS_BOX
  // ls_stats, per kind (device-local counters kBx*): traced, occluded, occluded where the atlas term
  // is 1, segment cut by ls_box=2 (shadow-mapped); traced, occluded, receiver in the fade-in
  // (unshadowed); unknown; the light each kind's rays remove.
  uint xb_sh_tr = 0, xb_sh_occ = 0, xb_sh_lit = 0, xb_sh_cut = 0, xb_un_tr = 0, xb_un_occ = 0, xb_un_fix = 0, xb_unk = 0;
  float3 xb_rem_sh_d = 0, xb_rem_sh_s = 0, xb_rem_un_d = 0, xb_rem_un_s = 0;
  if (n_proj > 0u) {
    LsLite bs_ = (LsLite)0;
    ls_lite(fp, d, bs_);
    const LsQuad q = ls_quad(fp);
    for (uint bi_ = end_s; bi_ < end_s + n_proj; ++bi_) {
      const uint bi = bg3_light_index(bi_);
      const bool tr = validate ? false : ls_box_traced(bi, bs_);
      const uint vs = tr && !qa ? ls_box_trace(bi, bs_) : kRtOccluded;
      float3 cd, ct, cs;
      float sh, t;
      bool cookie, shadowed;
      if (!ls_box_eval(bi, bs_, sf, mt, q, vs, cd, ct, cs, sh, t, cookie, shadowed)) continue;
      ++n_lights;
      ++nb_ev;
      nb_ck += cookie ? 1u : 0u;
      n_shadowed += shadowed ? 1u : 0u;
      nb_sh += shadowed ? 1u : 0u;
      bgd += cd * sh + ct;
      bgs += cs * sh;
      if (validate) {
        md += cd * sh + ct;
        ms += cs * sh;
        continue;
      }
      if (!tr) continue;
      cd_all += cd;
      cs_all += cs;
      const bool occ = vs == kRtOccluded && !qa;
#if defined(RT_OCC_STATS) || defined(RT_SPEC_OCC_STATS)
      if (RT_OCC_ON && occ_character(cls) && !qa) {
        occ_ray(kOccBox, vs, max(lum(cd * (sh - t)) + lum(cs * (sh - t)), 0.0) / occ_den);
        ++occ_nb;
      }
#endif
      n_traced += qa ? 0u : 1u;
      nb_tr += qa ? 0u : 1u;
      n_occ += occ ? 1u : 0u;
      nb_occ += occ ? 1u : 0u;
      n_unk += vs == kRtUnknown ? 1u : 0u;
      nb_unk += vs == kRtUnknown ? 1u : 0u;
      if (qa) dd -= ct;
      dd += cd * (t - sh);
      ds += cs * (t - sh);
      td += cd * t;
      ts += cs * t;
      bdd += cd * (t - sh) - (qa ? ct : 0.0);
      bds += cs * (t - sh);
      if (!qa) {
        const uint cut = ls_box_cut(load_box(bi), bs_);
        if (shadowed) {
          ++xb_sh_tr;
          xb_sh_occ += occ ? 1u : 0u;
          xb_sh_lit += occ && sh == 1.0 ? 1u : 0u;
          xb_sh_cut += cut == 1u ? 1u : 0u;
          xb_rem_sh_d += cd * (sh - t);
          xb_rem_sh_s += cs * (sh - t);
        } else {
          ++xb_un_tr;
          xb_un_occ += occ ? 1u : 0u;
          xb_un_fix += cut == 2u ? 1u : 0u;
          xb_rem_un_d += cd * (sh - t);
          xb_rem_un_s += cs * (sh - t);
        }
        xb_unk += vs == kRtUnknown ? 1u : 0u;
      }
      vd_all += cd * (vs == kRtUnknown ? sh : vs == kRtLit ? 1.0 : 0.0);
    }
  }
#endif

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
  // ---- AO on direct light (docs/archive/AUDIT_RT.md fix 4) ----
  // The lighting gives direct light mix(1, AO, m0.y) of the AO in b6, which holds
  // our 1-ray RT visibility once gi_ao_apply has run. ao_direct:
  //   0 leave it (the RT visibility darkens direct light, noisily, and contacts
  //     the RT shadows already resolve are darkened twice);
  //   1 none on the lights whose shadow is ray traced (D-LS lights, the sun with
  //     rt_shadows): their visibility IS the occlusion (default);
  //   2 the game's own factor with its own SSAO (the look of the original, noise-free).
  float3 xd = 0, xs = 0;
  if (ao_mode != 0u && !validate) {
    float ssao = (pc.ao_direct & 16u) != 0u ? aux_ao_at(fp) : ao;
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
    xd = ls_giveback_cap(xd, gDiffuse[fp] + dd * (fdif * saturate(pc.ls_strength)), td_f, fdif);
    xs = ls_giveback_cap(xs, gSpecular[fp] + ds * (fspec * saturate(pc.ls_strength)), ts_f, fspec);
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
  } else if (pc.ls_debug == 10) {   // in-frame A/B of the unknown fallback: yellow where a light's ray
    // was aborted by the candidate cap (that light kept the game's term), over this pixel's
    // final output in grey (luminance x 0.2). Force with rt_max_candidates.
    float k = saturate(pc.ls_strength);
    float3 fo = max(gd + dd * (fdif * k) + xd, 0.0) + max(gs + ds * (fspec * k) + xs, 0.0);
    od = n_unk > 0u ? float3(0.5, 0.5, 0.0) : min(lum(fo), 1.0).xxx * 0.2;
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

#ifdef PAN_CAPTURE
  gAux[fp].z = lum(cd_all) > 1e-6 ? saturate(lum(vd_all) / lum(cd_all)) : 1.0;
  gAux[fp].w = 1.0;
#endif
  if (pc.ls_stats != 0) {
    uint bin = (fp.x >> 3) * 7 + (fp.y >> 3) * 13;
    stat_addv(16, 1u, bin);
    stat_addv(17, n_lights, bin);
    stat_addv(18, n_shadowed, bin);
    stat_addv(19, n_traced, bin);
    // Validation sums only where no projector light is listed (they are not recomputed),
    // except with ls_box=1 (LS_BOX), which recomputes them: every pixel.
    bool vpx = LS_BOX != 0 || n_proj == 0u;
    // Box lights (ls_box=1, TRACK_D_RT.md 9.12): evaluations, with a game shadow map, traced,
    // occluded, unknown, with a cookie; their light as the game lit it and the light our
    // shadows remove (with the game's AO factor); validation on the projector pixels alone.
    if (LS_BOX != 0) {
      stat_addv(200, nb_ev, bin);
      stat_addv(201, nb_sh, bin);
      stat_addv(202, nb_tr, bin);
      stat_addv(203, nb_occ, bin);
      stat_addv(204, nb_unk, bin);
      stat_addv(207, nb_ck, bin);
      stat_addv(213, nb_ev > 0u ? 1u : 0u, bin);
      if (!validate) {
        stat_addv(205, fx(lum(bgd * fdif + bgs * fspec)), bin);
        stat_addv(206, fx(-lum(bdd * fdif + bds * fspec)), bin);
      }
    }
#if LS_BOX
    // The box accuracy bound in normal play (debug 0; device-local counters). The game's stored diffuse /
    // specular = transmittance x (direct x mix(1, AO, m0.y) + ambient x AO) + in-scatter, every term >= 0,
    // so the recompute of its direct local light (every evaluated point/spot and box light with the
    // game's own shadow term, x the same AO factors; no sun) can only be at or below it. The stored value
    // is B10G11R11 (6/6/5-bit mantissas): it fixes the true value only to the open interval between its
    // neighbours (common/ufloat_interval.h), so a channel whose recompute reaches the upper neighbour is a
    // violation: an over-estimate beyond the encoding precision, no tuned allowance. Hair (shadow-mapped
    // lights keep the game's model) and frames under QA are left out. One-sided: an under-estimate is not
    // seen here (ls_native_direct_validate is the two-sided check), and fog transmittance < 1 can flag.
    {
      const bool bnd = !validate && !qa && nb_ev > 0u && cls != kMatHair;
      uint b_over = 0u, b_inc = 0u, b_ch = 0u, b_chover = 0u;
      float b_exc = 0.0;
      const float3 rd = (rec_d + bgd) * fdif, rs = (rec_s + bgs) * fspec;
      if (bnd) {
        [unroll] for (uint lobe = 0u; lobe < 2u; ++lobe) {
          const float3 nat = lobe == 0u ? gd : gs;
          const float3 est = lobe == 0u ? rd : rs;
          [unroll] for (uint c = 0u; c < 3u; ++c) {
            const UfInterval iv = uf_interval(nat[c], c == 2u ? 5u : 6u);
            if (iv.status != 1u || !isfinite(est[c]) || est[c] < 0.0) {
              b_inc = 1u;
              continue;
            }
            ++b_ch;
            if (!(est[c] < iv.upper)) {
              ++b_chover;
              b_over = 1u;
              b_exc += (est[c] - iv.upper) * (c == 0u ? 0.2126 : c == 1u ? 0.7152 : 0.0722);
            }
          }
        }
      }
      bx_add(kBxBoundPx, bnd ? 1u : 0u);
      bx_add(kBxBoundOver, b_over);
      bx_add(kBxBoundInconcl, b_inc);
      bx_add(kBxBoundCh, b_ch);
      bx_add(kBxBoundChOver, b_chover);
      bx_sum(kBxGame, bnd ? lum(gd + gs) : 0.0);
      bx_sum(kBxRec, bnd ? lum(rd + rs) : 0.0);
      bx_sum(kBxBox, bnd ? lum(bgd * fdif + bgs * fspec) : 0.0);
      bx_sum(kBxExcess, b_exc);
      bx_add(kBxShTraced, xb_sh_tr);
      bx_add(kBxShOcc, xb_sh_occ);
      bx_add(kBxShOccAtlasLit, xb_sh_lit);
      bx_add(kBxShCut, xb_sh_cut);
      bx_add(kBxUnTraced, xb_un_tr);
      bx_add(kBxUnOcc, xb_un_occ);
      bx_add(kBxUnFixture, xb_un_fix);
      bx_add(kBxUnknown, xb_unk);
      bx_sum(kBxRemUn, lum(xb_rem_un_d * fdif + xb_rem_un_s * fspec));
      bx_sum(kBxRemSh, lum(xb_rem_sh_d * fdif + xb_rem_sh_s * fspec));
    }
#endif
    {   // (also with ls_box=0: there the recompute lacks the box lights, the comparison's baseline)
      // Keep the same raster population with boxes off/on. Requiring a
      // recomputed light would drop box-only pixels from the off baseline.
      const bool pv = validate && n_proj > 0u;
      const float pover = lum(max(md - gd, 0.0) + max(ms - gs, 0.0));
      stat_addv(208, pv ? 1u : 0u, bin);
      stat_addv(209, pv ? fx(lum(gd + gs)) : 0u, bin);
      stat_addv(210, pv ? fx(lum(md + ms)) : 0u, bin);
      stat_addv(211, pv ? fx(lum(abs(gd - md) + abs(gs - ms))) : 0u, bin);
      stat_addv(212, pv ? fx(pover) : 0u, bin);
      stat_addv(214, pv ? fx(lum(bgd * fdif + bgs * fspec)) : 0u, bin);   // the box share of the recompute
      float game_y=pv?lum(gd+gs):0.0,recomputed_y=pv?lum(md+ms):0.0;
      ls_sum24(216,game_y,bin);
      ls_sum24(218,recomputed_y,bin);
      ls_sum24(220,pv?lum(abs(gd-md)+abs(gs-ms)):0.0,bin);
      ls_sum24(222,pv?pover:0.0,bin);
      ls_sum24(224,pv?lum(bgd*fdif+bgs*fspec):0.0,bin);
      stat_addv(227,pv && isfinite(game_y) && game_y>0.0?1u:0u,bin);
      stat_addv(228,pv && isfinite(recomputed_y) && recomputed_y>0.0?1u:0u,bin);
    }
#if LS_NATIVE_DIRECT_VALIDATION
    // Independent diffuse/specular RGB channels, BEFORE output write conversion.
    // Actual queried native formats are B10G11R11_UFLOAT_PACK32 (snapshot gate).
    // Measure every shaded validation receiver, not only projector-list pixels.
    {
      bool sample = validate && (pc.ls_flags & (1u << 19)) != 0u;
      bool invalid = false, unbounded = false, failed = false;
      bool above = false, below = false;
      uint bounded_channels = 0u, failed_channels = 0u;
      float3 excess = 0.0;
      [unroll] for (uint lobe = 0u; lobe < 2u; ++lobe) {
        float3 native_rgb = lobe == 0u ? gd : gs;
        float3 estimate_rgb = lobe == 0u ? md : ms;
        [unroll] for (uint channel = 0u; channel < 3u; ++channel) {
          UfInterval interval = uf_interval(native_rgb[channel], channel == 2u ? 5u : 6u);
          float estimate = estimate_rgb[channel];
          bool bad = interval.status == 0u || !isfinite(estimate) || estimate < 0.0;
          invalid = invalid || bad;
          unbounded = unbounded || interval.status == 2u;
          if (!bad && interval.status == 1u) {
            ++bounded_channels;
            bool mismatch = !uf_contains(interval, native_rgb[channel], estimate);
            failed = failed || mismatch;
            above = above || estimate >= interval.upper;
            below = below || (estimate <= interval.lower && native_rgb[channel] != 0.0);
            failed_channels += mismatch ? 1u : 0u;
            excess[channel] += max(max(interval.lower - estimate, estimate - interval.upper), 0.0);
          }
        }
      }
      stat_addv(229, sample ? 1u : 0u, bin);
      stat_addv(230, sample && invalid ? 1u : 0u, bin);
      stat_addv(231, sample && unbounded ? 1u : 0u, bin);
      stat_addv(232, sample && failed ? 1u : 0u, bin);
      stat_addv(233, sample ? bounded_channels : 0u, bin);
      stat_addv(234, sample ? failed_channels : 0u, bin);
      ls_sum24(235, sample ? lum(excess) : 0.0, bin);
      [unroll] for (uint material_class = 0u; material_class < 8u; ++material_class) {
        stat_addv(237u + material_class, sample && cls == material_class ? 1u : 0u, bin);
        stat_addv(245u + material_class, sample && failed && cls == material_class ? 1u : 0u, bin);
      }
      if (sample && pc.ls_debug == 4u) {
        // Diagnostic colour only, through the existing diffuse capture path:
        // red=above interval, blue=below, magenta=both, green=invalid,
        // cyan=unbounded. Passing pixels retain native lighting in grey.
        o_d = invalid ? float3(0.0,0.5,0.0) : unbounded ? float3(0.0,0.5,0.5) :
              failed ? float3(above ? 0.5 : 0.0,0.0,below ? 0.5 : 0.0) :
              min(lum(gd + gs),1.0).xxx * 0.2;
        o_s = 0.0;
      }
    }
#endif
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
#if defined(RT_OCC_STATS) || defined(RT_SPEC_OCC_STATS)
  // The pixel's darkening per source: the light the traced shadows remove as the pass applies it
  // (ls_debug=2's term), the box lights' share from their own delta, over the lit buffers before.
  if (RT_OCC_ON && occ_character(cls) && !validate) {
    const float k = saturate(pc.ls_strength);
    const float all_rem = lum(max(-(dd * fdif + ds * fspec) * k, 0.0)), box_rem = lum(max(-(bdd * fdif + bds * fspec) * k, 0.0));
    if (occ_nl != 0u) occ_pixel(kOccLocal, max(all_rem - box_rem, 0.0) / occ_den);
    if (occ_nb != 0u) occ_pixel(kOccBox, box_rem / occ_den);
  }
#endif
  return true;
}
#endif

#if LS_K
// ---- exact-k + RIS remainder (ls_k > 0; TRACK_D_LOCAL_LIGHTS.md 9.11, common/ls_exact_k.h) ----
//
// The lights this pass traces at a pixel (the same set as ls_main: ls_light_traced and
// bg3_light_eval_mat's tests) are split by w = lum(c_d + c_s), their exact unshadowed
// contribution: the ls_k largest are traced as in ls_main; ls_rest_samples lights of the rest
// are drawn with p_j = w_j / sum_R w and their terms (delta and ao_direct sums alike) scaled by
// 1 / (n p_j). Unbiased: the expected output is ls_main's. Energy: the same terms, the same
// ls_finish tail. Noise for RR (QUALITY R1): one pcg3d draw per (pixel, frame), from an input
// domain disjoint from ls_rand's (x | 2^20), so independent of every ray direction.
// The QA ambient floor, ls_samples and the debug views run ls_main (the exact sum).
#include "../../common/ls_exact_k.h"

bool ls_k_active() { return lk_k(pc.ls_kmode) != 0u && pc.ls_samples == 0u && pc.ls_debug == 0u && !ls_qa_off(); }

bool ls_main_k(uint2 fp, inout float3 o_d, inout float3 o_s) {
  if (any(fp >= pc.full)) return false;
  g_rt_pass = kRtPassLocal;
  float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) return false;
  LsLite s = (LsLite)0;
  #ifdef RT_HAIR_PHYSICAL_ORIGIN
  s.pixel=fp;
  #endif
  if (!ls_lite(fp, d, s)) return false;   // the lighting skips unlit pixels

  const uint3 cl = bg3_cluster(fp, d);
  const uint end_p = cl.x + cl.y, end_s = end_p + cl.z;
  const uint K = lk_k(pc.ls_kmode), NR = lk_rest(pc.ls_kmode);

  // 1. the traced lights in one pass: the running top-K and the remainder reservoirs
  const uint3 ru = pcg3d(uint3(fp.x | 0x100000u, fp.y, pc.frame));
  LkSel sel = lk_begin(u01(ru.x), u01(ru.y));
  LsPixel px = (LsPixel)0;
  ls_surface(fp, s, px.sf, px.mt);
  for (uint i = cl.x; i < end_s; ++i) {
    const uint li = bg3_light_index(i);
    const bool spot = i >= end_p;
    float3 lpos;
    float R;
    if (!ls_light_traced(li, spot, s, lpos, R)) continue;
    float3 cd, ct, cs, Ld;
    float d2, nol;
    if (!bg3_light_eval_mat(load_light(li), spot, px.sf, px.mt, cd, ct, cs, Ld, d2, nol)) continue;
    const float cw = lum(cd + cs);
    if (!(cw > 0.0) || !isfinite(cw)) continue;   // c = 0: no delta, no ray
    sel = lk_add(sel, cw, i, K);
  }
  sel = lk_end(sel, NR);

  // 2. the rays (at most K + NR per pixel); unk: aborted (unknown) ones
  uint lit = 0, unk = 0;
  for (uint q = 0; q < 6u; ++q) {
    const uint e = lk_entry(sel, q);
    if (e == kLkNone) continue;
    const uint li = bg3_light_index(e);
    float3 lpos;
    float R;
    ls_light_ray(li, lpos, R);
    // the second remainder sample may repeat the first light: its own random stream
    const uint vs = trace_light(s.P, s.N, s.org, lpos, fp, q == 5u ? li ^ 0x155u : li, s.cone, R);
    if (vs == kRtLit) lit |= 1u << q;
    if (vs == kRtUnknown) unk |= 1u << q;
  }

  // 3. the surface (reloaded, as in ls_main), the exact terms of the selected lights
  ls_surface(WaveReadLaneAt(fp, WaveGetLaneIndex()), s, px.sf, px.mt);
  float3 dd = 0, ds = 0, td = 0, ts = 0;
  for (uint q3 = 0; q3 < 6u; ++q3) {
    const uint e = lk_entry(sel, q3);
    if (e == kLkNone) continue;
    Bg3Light l = load_light(bg3_light_index(e));
    float3 cd, ct, cs, Ld;
    float d2, nol;
    const bool spot = e >= end_p;
    if (!bg3_light_eval_mat(l, spot, px.sf, px.mt, cd, ct, cs, Ld, d2, nol)) continue;
    const float v = (lit >> q3) & 1u ? 1.0 : 0.0;
    float sh = 1.0, t = v;
    if ((l.flags & 1u) != 0u) {
      sh = bg3_atlas_shadow(l, spot, px.sf, Ld, d2, saturate(nol));
      if (ls_atlas_fallback(l.pos)) t = min(v, sh);
    }
    if ((unk >> q3) & 1u) t = sh;   // unknown ray: the game's own term (see the top)
    const float f = lk_factor(sel, q3, NR);
    dd += cd * ((t - sh) * f);
    ds += cs * ((t - sh) * f);
    td += cd * (t * f);
    ts += cs * (t * f);
  }
#if LS_BOX
  ls_box_all(fp, s, end_s, bg3_cluster_projectors(fp, d), px, dd, ds, td, ts);
#endif
  ls_finish(fp, px.sf, px.mt, dd, ds, td, ts, o_d, o_s);

#if LS_FULL
  if (pc.ls_stats != 0) {   // core/gi.cpp "ls exact-k stats" (kLkStatFirst) and the ls stats counters that keep their meaning
    const uint bin = (fp.x >> 3) * 7 + (fp.y >> 3) * 13;
    uint n_rays = 0, n_occ = 0, n_rr = 0, n_rocc = 0;
    for (uint q4 = 0; q4 < 6u; ++q4) {
      if (lk_entry(sel, q4) == kLkNone) continue;
      const bool occ = ((lit >> q4) & 1u) == 0u && ((unk >> q4) & 1u) == 0u;
      ++n_rays;
      n_occ += occ ? 1u : 0u;
      n_rr += q4 >= 4u ? 1u : 0u;
      n_rocc += q4 >= 4u && occ ? 1u : 0u;
    }
    const float wall = sel.rsum + sel.ew.x + sel.ew.y + sel.ew.z + sel.ew.w;
    const float3 gd = gDiffuse[fp], gs = gSpecular[fp];
    const bool vpx = bg3_cluster_projectors(fp, d) == 0u;
    float ao, fdif, fspec;
    ls_ao_factors(fp, px.sf, ao, fdif, fspec);
    stat_addv(16, 1u, bin);
    stat_addv(19, n_rays, bin);
    stat_addv(20, n_occ, bin);
    stat_addv(21, vpx ? fx(lum(gd + gs)) : 0u, bin);
    stat_addv(24, fx(-lum(dd * fdif + ds * fspec)), bin);
    stat_addv(25, sel.n > 0u ? 1u : 0u, bin);
    stat_addv(26, cl.y + cl.z, bin);
    stat_addv(kLkStatFirst + 0u, sel.n, bin);
    stat_addv(kLkStatFirst + 1u, sel.n > K ? 1u : 0u, bin);
    stat_addv(kLkStatFirst + 2u, sel.n > K ? sel.n - K : 0u, bin);
    stat_addv(kLkStatFirst + 3u, n_rr, bin);
    stat_addv(kLkStatFirst + 4u, n_rocc, bin);
    stat_addv(kLkStatFirst + 5u, sel.n > K && wall > 0.0 ? fx(sel.rsum / wall) : 0u, bin);
  }
#endif
  return true;
}
#endif

#ifndef LS_NO_MAIN
#if LS_BOX && LS_FULL && defined(PAN_CAPTURE)
#error "the box statistics' group barrier needs every thread of the group (no early return)"
#endif
[numthreads(8, 8, 1)]
#if LS_BOX && LS_FULL
void main(uint3 tid : SV_DispatchThreadID, uint gi : SV_GroupIndex, uint3 gid : SV_GroupID) {
  if (gi < kBxN) gsBx[gi] = 0u;
#if defined(RT_OCC_STATS) || defined(RT_SPEC_OCC_STATS)
  if (RT_OCC_ON) occ_clear(gi);
#endif
  GroupMemoryBarrierWithGroupSync();
#if defined(RT_OCC_STATS) || defined(RT_SPEC_OCC_STATS)
  if (RT_OCC_ON && all(tid.xy == 0u)) occ_add(kOccPassLs, 1u);
#endif
#else
void main(uint3 tid : SV_DispatchThreadID) {
#endif
#ifdef PAN_CAPTURE
  if (any(tid.xy >= pc.full)) return;
  gAux[tid.xy].zw = float2(1, 0);
#endif
  float3 od = 0, os = 0;
#if LS_K
  if (ls_k_active() ? ls_main_k(tid.xy, od, os) : ls_main(tid.xy, od, os)) {
#else
  if (ls_main(tid.xy, od, os)) {
#endif
    gDiffuse[tid.xy] = od;
    gSpecular[tid.xy] = os;
  }
#if LS_BOX && LS_FULL
  GroupMemoryBarrierWithGroupSync();
  if (pc.ls_stats != 0u) bx_flush(gi, gid.xy);
#if defined(RT_OCC_STATS) || defined(RT_SPEC_OCC_STATS)
  if (RT_OCC_ON) occ_flush(gi, gid.xy);
#endif
#endif
}
#endif
