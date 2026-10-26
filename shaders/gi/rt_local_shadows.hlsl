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
// Validation (ls_debug=4/5/6): rt_local_shadows_prep.hlsl zeroes the game's AO and
// sun mask before the lighting, so its output holds only direct local light (plus
// projector lights and in-scatter, which are not recomputed).
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
#include "../rt/bg3_lights.hlsli"
#include "../rt/bg3_sun.hlsli"

float lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

uint3 ls_rand(uint2 fp, uint key) { return pcg3d(uint3(fp, pc.frame * 1024u + (key & 1023u))); }
float u01(uint x) { return float(x >> 8) * (1.0 / 16777216.0); }

// Lanes' sum of v added to counter `index` (fixed point), spread over 64 bins.
void stat_addv(uint index, uint v, uint bin) {
  uint n = WaveActiveSum(v);
  uint b = WaveReadLaneFirst(bin) & 63u;
  if (WaveIsFirstLane() && n) rtStats.InterlockedAdd((index * 64 + b) * 4, n);
}
uint fx(float v) { return uint(clamp(v, 0.0, 64.0) * 256.0 + 0.5); }

float3 n_world(float3 nv) { return nv.x * cam.m6_r0.xyz + nv.y * cam.m6_r1.xyz + nv.z * cam.m6_r2.xyz; }
float3 n_view_at(int2 p) { return decode_normal(gNormal.Load(int3(clamp(p, int2(0, 0), int2(pc.full) - 1), 0)).xy); }
float3 nan0(float3 v) { return any(isnan(v)) ? float3(0, 0, 0) : v; }

// Visibility of the light from the surface: 1 ray, towards the centre or a
// point of its sphere. `key` selects the random stream. `side` < 0: the light is
// behind the surface (transmission, hair), so the ray starts on the back side.
float trace_light(Bg3Surface sf, float3 lpos, uint2 fp, uint key, RtCone cone, float side) {
  float3 L = lpos - sf.P;
  float dist = length(L);
  float3 Ld = L / max(dist, 1e-6);
  float R = pc.ls_source_radius;
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
  // (`side` < 0 gives the ray a direction below the surface: the origin follows it.)
  float3 o = rt_ray_origin_o(sf.org, sf.P, sf.N, cone.f0, dir);
  float tmax = length(lpos - o) - max(R, pc.ls_clear);
  if (tmax <= 0.0) return 1.0;
  return rt_occluded(o, dir, tmax, cone) ? 0.0 : 1.0;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  float d = gDepth.Load(int3(fp, 0));

  if (d <= 0.0) return;

  // ---- the surface, exactly as f469db78 sets it up ----
  Bg3Surface sf;
  float2 uv = (float2(fp) + 0.5) / float2(lFlags.res);
  sf.P = ray_at(uv) * d;
  sf.dist = length(sf.P);
  float3 nv = decode_normal(gNormal.Load(int3(fp, 0)).xy);
  sf.N = n_world(nv);
  sf.V = normalize(-sf.P);
  float4 alb = gAlbedo.Load(int3(fp, 0));
  float4 mat = gMaterial.Load(int3(fp, 0));
  // The lighting skips unlit pixels: no light to remove.
  uint cls = mat_class_at(fp);
  if (cls == kMatUnlit) return;
  // Classes whose parameters are missing are shaded as class 0 (logged by the core).
  if ((cls == kMatSkin || cls == kMatTranslucent) && (pc.mat_classes & 4u) == 0u) cls = kMatStandard;
  if (cls >= kMatAniso && (pc.mat_classes & 2u) == 0u) cls = kMatStandard;
  float4 rt3 = cls != kMatStandard ? gRT3.Load(int3(fp, 0)) : 0.0.xxxx;
  sf.channel = (uint(round(mat.w * 255.0)) & 112u) >> 4;
  sf.diff_col = alb.rgb * (1.0 - mat.z);
  sf.f0 = lerp((0.16 * mat.y * mat.y).xxx, alb.rgb, mat.z);
  // Specular AA from the quad's normal differences (n(odd) - n(even) in each direction).
  int2 q0 = int2(fp & ~1u);
  float3 dh = nan0(n_view_at(int2(q0.x + 1, fp.y)) - n_view_at(int2(q0.x, fp.y)));
  float3 dv = nan0(n_view_at(int2(fp.x, q0.y + 1)) - n_view_at(int2(fp.x, q0.y)));
  float r = max(0.09, mat.x);
  sf.a = sqrt(saturate(r * r + min(dot(dh, dh) + dot(dv, dv), 0.18)));
  sf.nov_raw = dot(sf.N, sf.V);
  sf.nov = max(sf.nov_raw, 9.9999997e-06);
  sf.ign = frac(52.982918 * frac(0.06711056 * float(fp.x) + 0.0058371499 * float(fp.y)));
  sf.org = rt_origin_at(fp);
  const Bg3Mat mt = bg3_mat_setup(cls, alb.rgb, mat, rt3, skin_white(),
                                  float3x3(cam.m6_r0.xyz, cam.m6_r1.xyz, cam.m6_r2.xyz), sf);

  float pa = pixel_angle(uv);
  RtCone cone = {sf.dist * pa, pa};
  const bool validate = pc.ls_debug >= 4 && pc.ls_debug <= 7;

  uint3 cl = bg3_cluster(fp, d);
  uint end_p = cl.x + cl.y, end_s = end_p + cl.z;
  float3 dd = 0, ds = 0;          // delta diffuse / specular
  float3 md = 0, ms = 0;          // validation: recomputed lighting with the game's shadows
  float3 cd_all = 0, cs_all = 0;  // debug: unshadowed sum of the traced lights
  float3 vd_all = 0;              // debug: traced-visible part
  float3 td = 0, ts = 0;          // direct light of the traced lights after our shadow (ao_direct)
  uint n_lights = 0, n_shadowed = 0, n_traced = 0, n_occ = 0;
  // RIS reservoirs (ls_samples > 0)
  const uint ns = min(pc.ls_samples, 2u);
  float wsum = 0;
  float3 rd[2] = {float3(0, 0, 0), float3(0, 0, 0)}, rs[2] = {float3(0, 0, 0), float3(0, 0, 0)}, rp[2] = {float3(0, 0, 0), float3(0, 0, 0)};
  float rw[2] = {0, 0}, rsh[2] = {1, 1}, rsel[2] = {0, 0}, rside[2] = {1, 1};
  uint rk[2] = {0, 0};

  for (uint i = cl.x; i < end_s; ++i) {
    uint li = lIndex.Load(i * 4u);
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
    if (!traced) continue;
    cd_all += cd;
    cs_all += cs;
    if (ns == 0) {
      float v = trace_light(sf, l.pos, fp, li, cone, nol);
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
          rd[k] = cd; rs[k] = cs; rp[k] = l.pos; rw[k] = w; rsh[k] = s; rk[k] = li; rside[k] = nol;
          rsel[k] = shadowed && pc.ls_shadowed == 2 ? 1.0 : 0.0;
        }
      }
    }
  }
  if (ns > 0 && wsum > 0.0) {
    [unroll] for (uint k = 0; k < 2; ++k) {
      if (k >= ns || !(rw[k] > 0.0)) continue;
      float v = trace_light(sf, rp[k], fp, rk[k] ^ (0x300u + k), cone, rside[k]);
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
  float ao = lFlags.m0.x > 0.0 ? gAO[fp].x : 1.0;
  float spec_occ = lFlags.m0.x > 0.0 ? saturate(pow(max(abs(sf.nov_raw + ao), 9.9999997e-05), exp2(-16.0 * sf.a - 1.0)) - 1.0 + ao) : 1.0;
  float fdif = lerp(1.0, ao, lFlags.m0.y), fspec = lerp(1.0, spec_occ, lFlags.m0.y);

  // ---- the sun's direct light, as the lighting computed it (rt/bg3_sun.hlsli) ----
  const uint ao_mode = pc.ao_direct & 3u;
  const bool sun_in = (pc.ao_direct & 8u) != 0u;
  float3 sund = 0, suns = 0;
  if (sun_in && ((ao_mode != 0u && (pc.ao_direct & 4u) != 0u) || ao_mode == 2u || pc.ls_debug == 7)) {
    float m = pc.ls_has_mask != 0u && lFlags.m0.w > 0.0 ? gSunShadow[fp] : 1.0;
    float S = m * bg3_cloud_shadow(sf.P);
    if (S > 0.0)
      bg3_sun_eval_mat(sf, mt, normalize(cam.v0.xyz), bg3_sun_colour() * (S * asfloat(lScale.Load(0))), S, sund, suns);
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
  gDiffuse[fp] = od;
  gSpecular[fp] = os;

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
    stat_addv(53, nstd ? 1u : 0u, bin);
    bool nv = validate && nstd && vpx && n_lights > 0;
    stat_addv(48, nv ? 1u : 0u, bin);
    stat_addv(49, nv ? fx(lum(gd + gs)) : 0u, bin);
    stat_addv(50, nv ? fx(lum(md + ms)) : 0u, bin);
    stat_addv(51, nv ? fx(over) : 0u, bin);
    stat_addv(52, nv ? fx(lum(abs(gd - md) + abs(gs - ms))) : 0u, bin);
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
    stat_addv(58, fx(lum(xd + xs)), bin);   // light given back by ao_direct
    stat_addv(59, fx(lum(sund * fdif + suns * fspec)), bin);   // the sun as recomputed (with the game's AO factor)
    stat_addv(25, n_lights > 0 ? 1u : 0u, bin);
    stat_addv(26, cl.y + cl.z, bin);   // lights listed in the froxel
    stat_addv(31, bg3_cluster_projectors(fp, d), bin);   // projector lights listed (not handled)
    if (all(fp == uint2(0, 0))) {
      // raw values, read from the latest slot (not summed)
      rtStats.Store((27 * 64) * 4, asuint(lFlags.m0.x));
      rtStats.Store((28 * 64) * 4, asuint(lFlags.m0.y));
      rtStats.Store((29 * 64) * 4, asuint(lFlags.m0.w));
      rtStats.Store((30 * 64) * 4, lScale.Load(0));
      rtStats.Store((54 * 64) * 4, asuint((pc.mat_classes & 8u) != 0u ? gFrame.b.x : -1.0));   // set0/b0 _m4
    }
  }
}
