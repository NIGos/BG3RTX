// Track D, D-RF: ray-traced reflections (rt_reflections=1; docs/TRACK_D_RT.md section 10).
//
// Full resolution, right after the tiled lighting and D-LS, before the GI composite
// (which keeps the result as next frame's radiance), SSS and fog.
//
// The game's only reflections are its probes: b11 holds S·k·SO, the specular ambient
// of bg3_ambient.hlsli (k = b13[0]·100, SO its specular occlusion). This pass
// replicates that term exactly (Tp) from the lighting's own bindings, traces one
// reflection ray against the TLAS, and adds the exact delta
//     b11 += w · (L_rt · E − Tp)
// where E = F0·DFG.x + DFG.y is the game's own env BRDF at this pixel and L_rt the
// radiance where the ray hits:
//   tier (a) the hit projects on screen and the depth there is the hit itself:
//            last frame's lit radiance, reprojected with the motion vectors
//            (prev_radiance_at); w = 1, faded at the screen edge and towards
//            rf_max_rough;
//   tier (c) a miss, a hit off screen or hidden on screen, or a query the candidate cap
//            aborted (closest hit unknown, rt_common.hlsli): w = 0, the game's probe
//            stays exactly as it was (a TLAS miss is not sky: roofs are cut away).
//   tier (b) RF_HIT_SHADE (rt_reflections_hit_shade.hlsl, rf_hit_shade=1, needs rt_hit_material=1):
//            a hidden or off-screen hit inside the frustum on a qualified native material
//            takes L_b, its full shaded radiance (rt/native_hit_shade.hlsli: point/spot RT-
//            shadowed, sun with one RT ray, the game's diffuse + specular ambient at AO 1),
//            w = 1 faded towards rf_max_rough; anything unsupported keeps the probe (tier c).
//            With rf_stats=1 the tier (a) hits also compute L_b and count it against the
//            screen's L_a (logged "rf hit shade"); their output is unchanged.
// The game's specular occlusion is not applied to traced hits: the ray
// already found the occluder and returns its radiance.
//
// Sampling (QUALITY R1, R2). With RR (rf_sample=1): one ray per pixel, GGX bounded
// VNDF (Eto & Tokuyoshi 2023) with alpha = a², white noise from pcg3d keyed by
// (pixel, frame); a mirror ray below rf_mirror_rough. Nothing of ours filters it; RR
// gets the hit distance (rfHitDist, DLSSD.SpecularHitDistance). Without RR there is no
// denoiser: mirror rays only, for smooth surfaces (the core lowers rf_max_rough).
//
// rf_validate=1 checks the replication: a pre-pass set the AO to 1 and the sun mask
// to 0 before the lighting, so b11 = sum of local-light specular (recomputed here with
// the game's own shadows, as D-LS does) + projector lights + Tp, and the residual
// b11 − local − Tp must be 0 where no projector light is listed.
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
#include "../rt/bg3_lights.hlsli"
#include "../rt/bg3_ambient.hlsli"
// SHARC_QUERY_PASS (rt_reflections_sharc.hlsl, sharc=1 with sharc_query bit 2): a hidden or
// off-screen hit on a surface at least sharc_rf_rough rough, whose ray cone at the hit is
// wider than sharc_rf_cone voxels (SHaRC's glossy rule: the cache's blur is then below the
// lobe's), takes the radiance cache's (diffuse) radiance there with the tier (a) weight
// instead of keeping the probe. Mirrors and narrow cones keep the probe; misses always do.
#if SHARC_QUERY_PASS
#define SHARC_RT_QUERY 1
#define SHARC_CAM_FROM_LIGHTS 1
#include "sharc_common.hlsli"
#endif
#if RF_HIT_SHADE
// Layout (core/gi.cpp rf_hit_dispatch, common/native_hit_rf_bindings.h): the D-RF push set, the
// material AlphaDSL (rt_hit_material_use) as set 1, the game's lighting set 1 as set 2, the D-LS
// sun set as set 3 (and the SHaRC set 4).
#define BG3_SUN_SET 3
#include "../rt/bg3_sun.hlsli"
#include "../rt/native_hit_material.hlsli"
// Projector (box) lights at the hit: the box records b29 / cookies b45 / sampler b16 of the
// game's set 1, which this variant binds as set 2 (common/native_hit_rf_bindings.h).
#define BG3_BOX_SET 2
#define NATIVE_HIT_BOXES 1
// The screen's light policy and AO at hits (native_hit_direct.hlsli NATIVE_HIT_DLS_POLICY); lsSlots
// in the sun set b7 (D-RF's push set already holds the 32 push descriptors).
#define NATIVE_HIT_DLS_POLICY 1
#define NATIVE_HIT_SLOTS_SET 3
#define NATIVE_HIT_SLOTS_BINDING 7
#include "../rt/native_hit_direct.hlsli"
#define NATIVE_HIT_SHADE_FULL 1
#include "../rt/native_hit_shade.hlsli"
#endif

[[vk::binding(30, 0)]] [[vk::image_format("r16f")]] RWTexture2D<float> rfHitDist;

static const float kNoHit = 65504.0;

float lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float u01(uint x) { return float(x >> 8) * (1.0 / 16777216.0); }
float3 n_world(float3 nv) { return nv.x * cam.m6_r0.xyz + nv.y * cam.m6_r1.xyz + nv.z * cam.m6_r2.xyz; }
float3 n_view_at(int2 p) { return decode_normal(gNormal.Load(int3(clamp(p, int2(0, 0), int2(pc.full) - 1), 0)).xy); }
float3 nan0(float3 v) { return any(isnan(v)) ? float3(0, 0, 0) : v; }

// Lanes' sum of v added to counter `index`, spread over 64 bins.
void stat_addv(uint index, uint v, uint bin) {
  uint n = WaveActiveSum(v);
  uint b = WaveReadLaneFirst(bin) & 63u;
  if (WaveIsFirstLane() && n) rtStats.InterlockedAdd((index * 64 + b) * 4, n);
}
uint fxl(float v) { return uint(clamp(v, 0.0, 4.0) * 4096.0 + 0.5); }
#if RF_HIT_SHADE
// rf hit shade counters (core/gi.cpp kRfHitFirst): luminance x 8192, clamped at 8 (clip counted).
// x 128 rounded every b13[0]-scaled direct and sun term (~1e-3) to 0 (live S8: "direct 0, sun 0").
uint fxh(float v) { return uint(clamp(v, 0.0, 8.0) * 8192.0 + 0.5); }
#endif

// GGX visible normals within the spherical cap that keeps the reflection above the
// horizon (Eto & Tokuyoshi 2023, isotropic). wi in tangent space (z = normal).
float3 sample_vndf_bounded(float3 wi, float alpha, float2 u) {
  float3 wi_std = normalize(float3(wi.xy * alpha, wi.z));
  float phi = (2.0 * u.x - 1.0) * 3.14159265;
  float a = saturate(alpha);
  float s = 1.0 + length(wi.xy);
  float a2 = a * a, s2 = s * s;
  float k = (1.0 - a2) * s2 / (s2 + a2 * wi.z * wi.z);
  float b = wi.z > 0.0 ? k * wi_std.z : wi_std.z;
  float z = (1.0 - u.y) * (1.0 + b) - b;
  float st = sqrt(saturate(1.0 - z * z));
  float3 c = float3(st * cos(phi), st * sin(phi), z) + wi_std;
  return normalize(float3(c.xy * alpha, c.z));
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  g_rt_pass = kRtPassRefl;
  float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) {
    rfHitDist[fp] = kNoHit;
    return;
  }

  // ---- the surface, as f469db78 sets it up (same as D-LS) ----
  Bg3Surface sf;
  float2 uv = (float2(fp) + 0.5) / float2(lFlags.res);
  sf.P = ray_at(uv) * d;
  sf.dist = length(sf.P);
  float3 nv = decode_normal(gNormal.Load(int3(fp, 0)).xy);
  sf.N = n_world(nv);
  sf.V = normalize(-sf.P);
  float4 alb = gAlbedo.Load(int3(fp, 0));
  float4 mat = gMaterial.Load(int3(fp, 0));
  uint flags = uint(round(mat.w * 255.0));
  sf.channel = (flags & 112u) >> 4;
  uint model = flags & 7u;
  sf.diff_col = alb.rgb * (1.0 - mat.z);
  sf.f0 = lerp((0.16 * mat.y * mat.y).xxx, alb.rgb, mat.z);
  int2 q0 = int2(fp & ~1u);
  float3 dh = nan0(n_view_at(int2(q0.x + 1, fp.y)) - n_view_at(int2(q0.x, fp.y)));
  float3 dv = nan0(n_view_at(int2(fp.x, q0.y + 1)) - n_view_at(int2(fp.x, q0.y)));
  float r = max(0.09, mat.x);
  sf.a = sqrt(saturate(r * r + min(dot(dh, dh) + dot(dv, dv), 0.18)));
  sf.nov_raw = dot(sf.N, sf.V);
  sf.nov = max(sf.nov_raw, 9.9999997e-06);
  sf.ign = frac(52.982918 * frac(0.06711056 * float(fp.x) + 0.0058371499 * float(fp.y)));

  const bool model_ok = ((pc.rf_models >> model) & 1u) != 0u;
  const bool validate = pc.rf_validate != 0;
  const float k = asfloat(lScale.Load(0)) * 100.0;

  // ---- the game's specular ambient term, exactly (EN4) ----
  uint4 cl = bg3_cluster_raw(fp, d);
  float3 E;
  uint n_local;
  float3 S = bg3_spec_ambient(sf.P, sf.N, sf.V, sf.f0, sf.a, cl, E, n_local);
  float so = bg3_spec_occlusion(fp, sf.nov_raw, sf.a);
  float3 Tp = S * (k * so);

  // ---- one reflection ray ----
  float rough_w = 1.0 - smoothstep(pc.rf_max_rough * 0.75, pc.rf_max_rough, sf.a);
  bool eligible = model_ok && rough_w > 0.0 && !validate;
  // 0 not traced, 1 on-screen hit (a), 2 hidden on screen, 3 off screen, 4 miss, 5 below horizon,
  // 6 unknown (aborted by the candidate cap: the probe is kept)
  uint tier = 0;
  float w = 0.0, hit_t = kNoHit;
  float3 Lrt = 0;
  bool cached = false;   // tier 2/3 shaded from the radiance cache (SHARC_QUERY_PASS)
#if RF_HIT_SHADE
  bool shaded = false;   // tier (b): a hidden / off-screen hit shaded with its material
  // counters: hidden hits asked, unknown material, unsupported lighting, local probes listed at a
  // shaded hit; the tier (a) check: complete, unsupported
  uint hs_asked = 0, hs_material = 0, hs_light = 0, hs_probes = 0, hs_checked = 0, hs_check_fail = 0;
  float3 hs_Lb = 0, hs_La = 0, hs_direct = 0, hs_sun = 0, hs_amb = 0;
  // the tier (a) check's per-term breakdown (counters 304-311): L_b's direct part from lights at
  // the game's visibility, ambient diffuse / specular, L_b at AO 1, A; the screen's AO at the hit's
  // pixel (b6 as the lighting read it), L_a's diffuse / specular parts (prevRadiance .a share).
  float3 hs_native = 0, hs_amb_d = 0, hs_amb_s = 0, hs_Lb1 = 0;
  float hs_ao = 1, hs_ao_screen = 1, hs_La_share = 1;
#endif
  if (eligible) {
    float3 Nn = normalize(sf.N);
    float3 dir = reflect(-sf.V, Nn);
    bool valid = dot(Nn, sf.V) > 0.0;
    if (valid && pc.rf_sample != 0 && sf.a >= pc.rf_mirror_rough) {
      float3 t = abs(Nn.y) < 0.999 ? normalize(cross(float3(0, 1, 0), Nn)) : float3(1, 0, 0);
      float3 b = cross(Nn, t);
      float3 wi = float3(dot(sf.V, t), dot(sf.V, b), dot(sf.V, Nn));
      uint3 rnd = pcg3d(uint3(fp, pc.frame) ^ uint3(0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u));
      float3 h = sample_vndf_bounded(wi, sf.a * sf.a, float2(u01(rnd.x), u01(rnd.y)));
      float3 H = normalize(t * h.x + b * h.y + Nn * h.z);
      dir = reflect(-sf.V, H);
    }
    valid = valid && dot(Nn, dir) > 0.0;
    if (!valid) {
      tier = 5;
    } else {
      float pa = pixel_angle(uv);
      RtCone cone = {sf.dist * pa, pa + sf.a * sf.a};
      // The shared ray origin (rt_common.hlsli rt_ray_origin; rt_origin=0 the old rt_bias
      // offset): dir leaves on Nn's side (dot(Nn, dir) > 0 above), and the origin is pushed
      // to that side of the geometric normal (or, where the geometric and the shading
      // normal disagree, 2 footprints along Nn).
      float3 o = rt_ray_origin(fp, sf.P, Nn, sf.dist * pa, dir);
      bool aborted;
#if SHARC_QUERY_PASS || RF_HIT_SHADE
      RtHit hh = rt_closest_hit(o, dir, pc.rf_tmax, cone);
      float t = hh.t;
      aborted = hh.aborted;
#else
      float t = rt_closest(o, dir, pc.rf_tmax, cone, aborted);
#endif
      if (aborted) {
        // Unknown (the candidate cap ended the query): the closest hit is not known, so the
        // game's probe stays exactly as it was (w = 0), as for a hidden / off-screen hit,
        // and the radiance cache is not asked either (its hit point would be a guess).
        tier = 6;
      } else if (t < 0.0) {
        tier = 4;
      } else {
        float3x3 inv = frustum_inverse();
        float2 quv;
        float qd;
        bool onscreen = project(inv, o + dir * t, quv, qd) && all(quv >= 0.0) && all(quv <= 1.0);
        tier = 3;
        if (onscreen) {
          tier = 2;
          int2 qp = min(int2(quv * float2(pc.full)), int2(pc.full) - 1);
          float sd = gDepth.Load(int3(qp, 0));
          float3 nq = n_world(decode_normal(gNormal.Load(int3(qp, 0)).xy));
          // The camera must see the same point, from the side the ray arrives at. Same point:
          // rt/depth_tol.hlsli depth_same_surface, x rf_depth_k, as the GI trace: rtOrigin's
          // geometric normal and TLAS surface at qp (rt_origin=1), else the depth buffer's
          // own normal and the measured TLAS-raster error.
          const float4 oq = rt_origin_at(uint2(qp));
          const bool has_tlas = pc.rt_origin != 0u && oq.w < kOriginNoHit;
          const float3 ngq = pc.rt_origin != 0u ? oq.xyz : depth_geo_normal(qp, ray_at((float2(qp) + 0.5) / float2(pc.full)) * sd);
          if (sd > 0.0 && depth_same_surface(qp, qd, sd, ngq, has_tlas, oq.w, pc.rf_depth_tol) && dot(nq, dir) < 0.2) {
            float3 L = prev_radiance_at(qp);
            if (all(isfinite(L))) {
              float2 e = min(quv, 1.0 - quv) / max(pc.rf_edge, 1e-4);
              w = rough_w * saturate(min(e.x, e.y)) * saturate(pc.rf_strength);
              Lrt = max(L, 0.0);
              hit_t = t;
              tier = 1;
            }
          }
        }
#if RF_HIT_SHADE
        // Tier (b) on hidden / off-screen hits; the same shading on tier (a) hits only checks it
        // against the screen (rf_stats=1 or rf_debug=8): their output stays the screen's.
        const bool hs_check = tier == 1 && (pc.rf_stats != 0u || pc.rf_debug == 8u);
        if ((tier == 2 || tier == 3 || hs_check) && hit_material_publication_ready()) {
          // Light / sun diagnostics 176-187, 191-195 (native_hit_direct.hlsli g_hd_*).
          g_hd_stats = pc.rf_stats != 0u;
          g_hd_base = 176u;
          g_hd_xbase = 196u;
          g_hd_pbase = 316u;   // policy / AO counters (core/gi.cpp kHitPolicyFirst + 4)
          g_hd_bin = (fp.x >> 3) * 7u + (fp.y >> 3) * 13u;
          NativeHitShade hs = native_hit_shade(hh, o + dir * t, -dir, cone.f0 + cone.spread * t, uint3(fp, pc.frame));
          // Hit-material reasons (253+status, 127) for the normal stats windows; run_post's push
          // has rt_stats 0, so the D-RF stats key gates it (core/gi.cpp "hit material" line).
          if (pc.rf_stats != 0u) hit_material_count(hh, hs.material_status, (fp.x >> 3) * 7u + (fp.y >> 3) * 13u);
          if (tier == 1) {
            hs_checked = hs.status == kHitShadeOk ? 1u : 0u;
            hs_check_fail = 1u - hs_checked;
            if (hs_checked != 0u) {
              hs_Lb = hs.radiance;
              hs_La = Lrt;
              hs_direct = hs.direct;
              hs_sun = hs.sun;
              hs_amb = hs.ambient;
              hs_native = hs.native;
              hs_amb_d = hs.ambient_d;
              hs_amb_s = hs.ambient_s;
              hs_Lb1 = hs.radiance_ao1;
              hs_ao = hs.ao;
              const int2 qq = min(int2(quv * float2(pc.full)), int2(pc.full) - 1);
              hs_ao_screen = lFlags.m0.x > 0.0 ? saturate(lgAO.Load(int3(qq, 0)).x) : 1.0;
              hs_La_share = saturate(prevRadiance[prev_radiance_px(qq)].a);
            }
          } else {
            hs_asked = 1u;
            hs_material = hs.status == kHitShadeMaterial ? 1u : 0u;
            hs_light = hs.status != kHitShadeOk && hs.status != kHitShadeMaterial ? 1u : 0u;
            if (hs.status == kHitShadeOk) {
              // The probe is replaced only by the hit's complete supported radiance (no edge
              // fade: the hit is not read from the screen).
              w = rough_w * saturate(pc.rf_strength);
              Lrt = hs.radiance;
              hit_t = t;
              shaded = true;
              hs_probes = hs.local_probes ? 1u : 0u;
              hs_Lb = hs.radiance;
            }
          }
        }
#endif
#if SHARC_QUERY_PASS
#if RF_HIT_SHADE
        if (tier != 1 && !shaded && (shc.flags & 4u) != 0u && sf.a >= shc.rf_rough) {
#else
        if (tier != 1 && (shc.flags & 4u) != 0u && sf.a >= shc.rf_rough) {
#endif
          const float3 cam_abs = sharc_cam();
          const float3 ph = o + dir * t;
          sharc_bin = (fp.x >> 3) * 7u + (fp.y >> 3) * 13u;
          const bool wide = cone.f0 + cone.spread * t >= sharc_voxel_size(cam_abs, ph + cam_abs) * shc.rf_cone;
          sharc_stat(kShRfEligible, wide);
          float3 Lc;
          bool near;
          SharcCell cell;
          if (wide && sharc_ray_hit(hh, ph, dir, t, cam_abs, cone, Lc, near, cell)) {
            w = rough_w * saturate(pc.rf_strength);
            Lrt = Lc;
            hit_t = t;
            cached = true;
          }
          sharc_stat(kShRfFound, cached);
        }
#endif
      }
    }
  }

  float3 gs = gSpecular[fp];
  float3 Trt = Lrt * E;
  float3 delta = w * (Trt - Tp);
  float3 os = max(gs + delta, 0.0);
  float3 gd = gDiffuse[fp], od = gd;

  // ---- validation (rf_validate=1): local lights recomputed with the game's shadows ----
  float3 ms = 0;
  if (validate) {
    uint end_p = cl.x + (cl.y & 4095u), end_s = end_p + ((cl.y >> 12) & 4095u);
    for (uint i = cl.x; i < end_s; ++i) {
      uint li = bg3_light_index(i);
      bool spot = i >= end_p;
      Bg3Light l = load_light(li);
      float3 cd, cs, Ld;
      float d2, nol;
      if (!bg3_light_eval(l, spot, sf, cd, cs, Ld, d2, nol)) continue;
      float s = (l.flags & 1u) != 0u ? bg3_atlas_shadow(l, spot, sf, Ld, d2, nol) : 1.0;
      ms += cs * s;
    }
    os = gs;
  }

  if (pc.rf_debug == 1) {   // tier: green on-screen hit, red hidden, orange off screen, blue miss, grey not traced, yellow aborted
    float3 c = cached ? float3(0, 1, 1) : tier == 1 ? float3(0, 1, 0) : tier == 2 ? float3(1, 0, 0) : tier == 3 ? float3(1, 0.5, 0)
             : tier == 4 ? float3(0, 0, 1) : tier == 5 ? float3(1, 0, 1) : tier == 6 ? float3(1, 1, 0) : float3(0.2, 0.2, 0.2);
    od = c * 0.25;
    os = 0;
  } else if (pc.rf_debug == 2) {   // specular change x4: green added, red removed
    od = float3(lum(max(-delta, 0.0)), lum(max(delta, 0.0)), 0.0) * 4.0;
    os = 0;
  } else if (pc.rf_debug == 3) {   // the game's probe term as replicated
    od = Tp;
    os = 0;
  } else if (pc.rf_debug == 4) {   // the probe term with tier (a) replaced
    od = Tp + delta;
    os = 0;
  } else if (pc.rf_debug == 5) {   // validation residual x4 (grey), red where the recompute exceeds the game
    float3 res = gs - ms - Tp;
    float over = lum(max(-res, 0.0));
    od = over > 1e-3 + 0.01 * lum(gs) ? float3(1, 0, 0) : abs(res) * 4.0;
    os = 0;
  } else if (pc.rf_debug == 6) {   // hit distance / 10 (tier a only)
    od = (tier == 1 || cached ? saturate(hit_t / 10.0) : 0.0).xxx * 0.5;
    os = 0;
  }
#if RF_HIT_SHADE
  if (pc.rf_debug == 1 && shaded) od = float3(0.25, 0.25, 0.25);   // tier (b): white (x 0.25)
  if (pc.rf_debug == 8) {   // rf_hit_shade check: tier (a) red L_a, green L_b (yellow = equal); tier (b) blue L_b
    od = hs_checked != 0u ? float3(lum(hs_La), lum(hs_Lb), 0.0) : shaded ? float3(0.0, 0.0, lum(hs_Lb)) : 0.0.xxx;
    os = 0;
  }
#endif
  // Never hand the game a non-finite value.
  if (any(!isfinite(os))) os = gs;
  if (any(!isfinite(od))) od = gd;
  gSpecular[fp] = os;
  if (pc.rf_debug != 0) gDiffuse[fp] = od;
#if RF_HIT_SHADE
  rfHitDist[fp] = (tier == 1 || cached || shaded) && w > 0.0 ? min(hit_t, kNoHit) : kNoHit;
#else
  rfHitDist[fp] = (tier == 1 || cached) && w > 0.0 ? min(hit_t, kNoHit) : kNoHit;
#endif

  if (pc.rf_stats != 0) {
    uint bin = (fp.x >> 3) * 7 + (fp.y >> 3) * 13;
    stat_addv(48, 1u, bin);
    stat_addv(49, eligible ? 1u : 0u, bin);
    stat_addv(50, tier == 1 ? 1u : 0u, bin);
    stat_addv(51, tier == 2 ? 1u : 0u, bin);
    stat_addv(52, tier == 3 ? 1u : 0u, bin);
    stat_addv(53, tier == 4 ? 1u : 0u, bin);
    stat_addv(54, tier == 5 ? 1u : 0u, bin);
    stat_addv(55, model_ok ? 0u : 1u, bin);
    stat_addv(56, fxl(lum(Tp)), bin);
    stat_addv(57, fxl(lum(max(Tp + delta, 0.0))), bin);
    // validation sums, only where no projector light is listed (not recomputed)
    bool vpx = validate && model_ok && (cl.z & 1023u) == 0u;
    stat_addv(58, !validate || vpx ? fxl(lum(gs)) : 0u, bin);
    stat_addv(59, n_local, bin);
    stat_addv(60, vpx ? 1u : 0u, bin);
    stat_addv(61, vpx ? fxl(lum(ms + Tp)) : 0u, bin);
    stat_addv(62, vpx ? fxl(lum(abs(gs - ms - Tp))) : 0u, bin);
    stat_addv(63, vpx ? fxl(lum(max(ms + Tp - gs, 0.0))) : 0u, bin);
#if RF_HIT_SHADE
    // rf hit shade (core/gi.cpp kRfHitFirst = 289, logged every 300 frames as "rf hit shade")
    const bool clip = any(max(max(hs_Lb, hs_La), max(max(hs_direct, hs_sun), hs_amb)) > 8.0);
    stat_addv(289, hs_asked, bin);
    stat_addv(290, shaded ? 1u : 0u, bin);
    stat_addv(291, hs_material, bin);
    stat_addv(292, hs_light, bin);
    stat_addv(293, hs_probes, bin);
    stat_addv(294, shaded ? fxh(lum(hs_Lb)) : 0u, bin);
    // The tier (a) check reads L_a from prevRadiance: under any rf_debug view that is last
    // frame's debug image (live S8 rf_debug=8: L_a 0.0019 against 0.047), so it is not counted.
    if (pc.rf_debug != 0u) {
      hs_checked = 0u;
      hs_check_fail = 0u;
    }
    stat_addv(295, hs_checked, bin);
    stat_addv(296, hs_check_fail, bin);
    stat_addv(297, hs_checked != 0u ? fxh(lum(hs_Lb)) : 0u, bin);
    stat_addv(298, hs_checked != 0u ? fxh(lum(hs_La)) : 0u, bin);
    stat_addv(299, hs_checked != 0u ? fxh(abs(lum(hs_Lb) - lum(hs_La))) : 0u, bin);
    stat_addv(300, hs_checked != 0u ? fxh(lum(hs_direct)) : 0u, bin);
    stat_addv(301, hs_checked != 0u ? fxh(lum(hs_sun)) : 0u, bin);
    stat_addv(302, hs_checked != 0u ? fxh(lum(hs_amb)) : 0u, bin);
    stat_addv(303, (hs_checked != 0u || shaded) && clip ? 1u : 0u, bin);
    stat_addv(304, hs_checked != 0u ? fxh(lum(hs_native)) : 0u, bin);
    stat_addv(305, hs_checked != 0u ? fxh(lum(hs_amb_d)) : 0u, bin);
    stat_addv(306, hs_checked != 0u ? fxh(lum(hs_amb_s)) : 0u, bin);
    stat_addv(307, hs_checked != 0u ? fxh(lum(hs_Lb1)) : 0u, bin);
    stat_addv(308, hs_checked != 0u ? fxh(hs_ao) : 0u, bin);
    stat_addv(309, hs_checked != 0u ? fxh(hs_ao_screen) : 0u, bin);
    stat_addv(310, hs_checked != 0u ? fxh(lum(hs_La) * hs_La_share) : 0u, bin);
    stat_addv(311, hs_checked != 0u ? fxh(lum(hs_La) * (1.0 - hs_La_share)) : 0u, bin);
#endif
  }
}
