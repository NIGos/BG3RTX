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
//   tier (c) a miss, a hit off screen or hidden on screen: w = 0, the game's probe
//            stays exactly as it was (a TLAS miss is not sky: roofs are cut away).
// Tier (b), shading hidden and off-screen hits with the hit material (EN2), is the
// HITMAT_PASS variant (rt_reflections_hm.hlsl, rf_hitmat=1; see below).
// The game's specular occlusion is not applied to traced hits (rf_so=0): the ray
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

// HITMAT_PASS (rt_reflections_hm.hlsl, rt_hitmat=1; layout: the D-RF push set, the host's set 1,
// the game's set 2, the D-LS sun set as set 3). Reads the material at the hit
// (rt/rt_hitmat.hlsli, EN2) for:
//   - the coverage counters: sampled hits per shader family with a known base colour,
//     roughness, metalness (rt_hitmat_stats=1, layer/hitmat.cpp logs them);
//   - the on-screen self-check: at tier (a) hits the G-buffer is ground truth for the
//     same surface, so the hit material's base colour (roughness, metalness) is compared
//     with RT1 (RT2) at the hit's pixel, next to the G-buffer's own difference to the
//     neighbouring pixel on the same surface (the texture's variation at one pixel, which
//     bounds what the sub-pixel position and the footprint can explain). Sums and
//     histograms; scripts/qa/coverage.py derives the verdict from them;
//   - rf_debug=7: the hit's base colour (dark magenta: material unknown, dark red: no
//     G-buffer draw joined, dark yellow: legacy scene, black: miss or not traced);
//   - tier (b) (rf_hitmat=1, off by default): a hidden or off-screen hit with a known base
//     colour and metalness is shaded as the game's class-0 lighting would shade it: the sun
//     (colour, clouds and b13[0] as the lighting has them, visibility traced) with the
//     game's diffuse/specular terms, plus the global ambient (irradiance probe b34/b36
//     x DFG.z, specular probe b35/b37 x env BRDF) x b13[0]*100. Not there: local lights
//     and local probes (listed per froxel: on screen only), AO at the hit, emission. So
//     tier (b) is a lower bound where local lights matter (interiors); the specular terms
//     need the roughness and reflectance too, else they are left out.
#if HITMAT_PASS
#define BG3_SUN_SET 3
#include "../rt/bg3_sun.hlsli"
#include "../rt/rt_hitmat.hlsli"
[[vk::binding(34, 2)]] TextureCube<float4> lgIrrCube0;   // b34: global irradiance probe
[[vk::binding(36, 2)]] TextureCube<float4> lgIrrCube1;   // b36: second global irradiance probe

// rf_models bits 8-11 (core/gi.cpp): 8 read hit materials, 9 tier (b), 10 statistics, 11 sun set bound.
bool hm_shade_on() { return (pc.rf_models & (1u << 9)) != 0u; }
bool hm_stats_on() { return (pc.rf_models & (1u << 10)) != 0u && gMatRec.Load(12) != 0u; }
bool hm_sun_on() { return (pc.rf_models & (1u << 11)) != 0u; }
// One pixel in 64, at a position that moves every frame.
bool hm_sampled(uint2 fp) { return ((fp.x + pc.frame) & 7u) == 0u && ((fp.y + (pc.frame >> 3)) & 7u) == 0u; }
float hm_y(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
uint hm_fx(float v) { return uint(min(abs(v), 16.0) * 4096.0 + 0.5); }
// Histogram bin of |v|: 0 below 1/1024, k for [2^(k-1), 2^k) / 1024.
uint hm_bin(float v) {
  const float x = abs(v) * 1024.0;
  return x < 1.0 ? 0u : uint(clamp(floor(log2(x)) + 1.0, 1.0, 15.0));
}

// Self-check sums (layer/hitmat.cpp prints them raw, scripts/qa/coverage.py reads them):
//  0 n | 1-3 sum|d| rgb | 4-6 sum d+ | 7-9 sum d- | 10-12 sum|nb| rgb | 13 sum dY^2 |
//  14 n rough, 15 sum|d|, 16 sum|nb| | 17 n metal, 18 sum|d|, 19 sum|nb| |
//  20 tier (a) hits, 21 with a known base colour | 22 hidden/off-screen hits, 23 known, 24 tier (b) shaded |
//  25 sum nbY^2 | 26-41 histogram |dY| | 42-57 histogram |nbY|
// x 4096 for the sums. d = hit material - G-buffer at the hit's pixel; nb = G-buffer at that
// pixel - G-buffer at its neighbour on the same surface; Y = Rec.709 luminance.
void hm_selfcheck(HitMaterial m, int2 qp) {
  const int2 full = int2(pc.full);
  const float4 mq = gMaterial.Load(int3(qp, 0));
  if ((uint(round(mq.w * 255.0)) & 7u) != 0u) return;   // standard class only (RT1/RT2 as decoded)
  const int2 qn = qp + int2(qp.x + 1 < full.x ? 1 : -1, 0);
  const float dq = gDepth.Load(int3(qp, 0)), dn = gDepth.Load(int3(qn, 0));
  const float4 mn = gMaterial.Load(int3(qn, 0));
  if (!(dn > 0.0) || abs(dn - dq) >= dq * pc.rf_depth_tol || (uint(round(mn.w * 255.0)) & 7u) != 0u) return;
  const float3 g = gAlbedo.Load(int3(qp, 0)).rgb, gn = gAlbedo.Load(int3(qn, 0)).rgb;
  if ((m.known & kHmAlbedo) != 0u) {
    const float3 d = m.albedo - g, nb = g - gn;
    const float dy = hm_y(d), ny = hm_y(nb);
    hm_qa_add(0, 1u);
    hm_qa_add(1, hm_fx(d.x));
    hm_qa_add(2, hm_fx(d.y));
    hm_qa_add(3, hm_fx(d.z));
    hm_qa_add(4, hm_fx(max(d.x, 0.0)));
    hm_qa_add(5, hm_fx(max(d.y, 0.0)));
    hm_qa_add(6, hm_fx(max(d.z, 0.0)));
    hm_qa_add(7, hm_fx(max(-d.x, 0.0)));
    hm_qa_add(8, hm_fx(max(-d.y, 0.0)));
    hm_qa_add(9, hm_fx(max(-d.z, 0.0)));
    hm_qa_add(10, hm_fx(nb.x));
    hm_qa_add(11, hm_fx(nb.y));
    hm_qa_add(12, hm_fx(nb.z));
    hm_qa_add(13, hm_fx(dy * dy));
    hm_qa_add(25, hm_fx(ny * ny));
    hm_qa_add(26u + hm_bin(dy), 1u);
    hm_qa_add(42u + hm_bin(ny), 1u);
  }
  if ((m.known & kHmRough) != 0u) {
    hm_qa_add(14, 1u);
    hm_qa_add(15, hm_fx(m.rough - mq.x));
    hm_qa_add(16, hm_fx(mq.x - mn.x));
  }
  if ((m.known & kHmMetal) != 0u) {
    hm_qa_add(17, 1u);
    hm_qa_add(18, hm_fx(m.metal - mq.z));
    hm_qa_add(19, hm_fx(mq.z - mn.z));
  }
}

// Tier (b): the outgoing radiance towards -dir of a hit at P (lighting space), as the game's
// class-0 lighting computes it from the terms it has off screen (see above).
bool hm_shade(HitMaterial m, float3 P, float3 dir, RtCone cone, float t, out float3 L) {
  L = 0.0.xxx;
  if ((m.known & (kHmAlbedo | kHmMetal)) != (kHmAlbedo | kHmMetal)) return false;
  const bool spec = (m.known & (kHmRough | kHmRefl)) == (kHmRough | kHmRefl);
  Bg3Surface hs = (Bg3Surface)0;
  hs.P = P;
  hs.N = m.N;
  hs.V = -dir;
  hs.diff_col = m.albedo * (1.0 - m.metal);
  hs.f0 = spec ? lerp((0.16 * m.refl * m.refl).xxx, m.albedo, m.metal) : 0.0.xxx;
  hs.a = max(0.09, spec ? m.rough : 1.0);
  hs.nov_raw = dot(hs.N, hs.V);
  hs.nov = max(hs.nov_raw, 9.9999997e-06);
  hs.dist = length(P);
  Bg3Mat mt = (Bg3Mat)0;   // class 0
  const float k13 = asfloat(lScale.Load(0));
  float3 d = 0.0.xxx, s = 0.0.xxx;
  const float3 Ls = normalize(cam.v0.xyz);
  if (hm_sun_on() && dot(hs.N, Ls) > 0.0) {
    const float fp = cone.f0 + cone.spread * t;
    RtCone sc = {fp, pc.sun_radius};
    const float vis = rt_occluded(rt_offset_ray(P, hs.N), Ls, pc.rt_tmax, sc) ? 0.0 : 1.0;
    const float S = vis > 0.0 ? vis * bg3_cloud_shadow(P) : 0.0;
    if (S > 0.0) bg3_sun_eval_mat(hs, mt, Ls, bg3_sun_colour() * (S * k13), S, d, s);
  }
  // The global ambient, as the lighting computes it (f469db78:968-994), without AO.
  const float m27 = lConsts.c[31].z, m28 = lConsts.c[31].w, m29 = lConsts.c[32].x;
  float3 irr = lgIrrCube0.SampleLevel(lgSampCube, hs.N, 0.0).rgb * m27;
  [branch] if (m29 > 0.0) irr = lerp(irr, lgIrrCube1.SampleLevel(lgSampCube, hs.N, 0.0).rgb * m28, m29);
  const float4 dfg = bg3_dfg(max(hs.nov_raw, 0.001953125), hs.a);
  const float ka = k13 * 100.0;
  float3 amb = irr * (hs.diff_col * dfg.z) * ka;
  if (spec) {
    float3 E;
    uint nl;
    amb += bg3_spec_ambient(P, hs.N, hs.V, hs.f0, hs.a, uint4(0, 0, 0, 0), E, nl) * ka;
  } else {
    s = 0.0.xxx;
  }
  L = d + s + amb;
  return all(isfinite(L));
}
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
  uint tier = 0;   // 0 not traced, 1 on-screen hit (a), 2 hidden on screen, 3 off screen, 4 miss, 5 below horizon
  float w = 0.0, hit_t = kNoHit;
  float3 Lrt = 0;
  bool cached = false;   // tier 2/3 shaded from the radiance cache (SHARC_QUERY_PASS) or the hit material (HITMAT_PASS)
#if HITMAT_PASS
  int2 hm_qp = int2(-1, -1);   // the tier (a) hit's pixel
  bool hm_have = false;
  HitMaterial hm_dbg = (HitMaterial)0;
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
      float3 o = sf.P + Nn * (sf.dist * pc.rt_bias);
#if SHARC_QUERY_PASS
      RtHit hh = rt_closest_hit(o, dir, pc.rf_tmax, cone);
      float t = hh.t;
#elif HITMAT_PASS
      RtHitEx hx = rt_closest_hit_ex(o, dir, pc.rf_tmax, cone);
      float t = hx.h.t;
#else
      float t = rt_closest(o, dir, pc.rf_tmax, cone);
#endif
      if (t < 0.0) {
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
          // The camera must see the same point, from the side the ray arrives at.
          if (sd > 0.0 && abs(qd - sd) < sd * pc.rf_depth_tol && dot(nq, dir) < 0.2) {
            float3 L = prev_radiance_at(qp);
            if (all(isfinite(L))) {
              float2 e = min(quv, 1.0 - quv) / max(pc.rf_edge, 1e-4);
              w = rough_w * saturate(min(e.x, e.y)) * saturate(pc.rf_strength);
              Lrt = max(L, 0.0);
              hit_t = t;
              tier = 1;
#if HITMAT_PASS
              hm_qp = qp;
#endif
            }
          }
        }
#if SHARC_QUERY_PASS
        if (tier != 1 && (shc.flags & 4u) != 0u && sf.a >= shc.rf_rough) {
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
#if HITMAT_PASS
        {
          const bool smp = hm_stats_on() && hm_sampled(fp);
          const bool hidden = tier == 2u || tier == 3u;
          if (smp || pc.rf_debug == 7u || (hm_shade_on() && hidden)) {
            HitMaterial hmat = rt_hit_material(hx, dir, cone);
            hm_have = true;
            hm_dbg = hmat;
            float3 Lb = 0.0.xxx;
            const bool shade = hm_shade_on() && hidden && hm_shade(hmat, o + dir * t, dir, cone, t, Lb);
            if (shade) {
              w = rough_w * saturate(pc.rf_strength);
              Lrt = max(Lb, 0.0);
              hit_t = t;
              cached = true;
            }
            if (smp) {
              hm_count(hmat);
              const bool kn = (hmat.known & kHmAlbedo) != 0u;
              hm_qa_add(20, tier == 1u ? 1u : 0u);
              hm_qa_add(21, tier == 1u && kn ? 1u : 0u);
              hm_qa_add(22, hidden ? 1u : 0u);
              hm_qa_add(23, hidden && kn ? 1u : 0u);
              hm_qa_add(24, shade ? 1u : 0u);
              if (tier == 1u && all(hm_qp >= 0)) hm_selfcheck(hmat, hm_qp);
            }
          }
        }
#endif
      }
    }
  }

  float3 gs = gSpecular[fp];
  float3 Trt = Lrt * E * (pc.rf_so != 0 ? so : 1.0);
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

  if (pc.rf_debug == 1) {   // tier: green on-screen hit, red hidden, orange off screen, blue miss, grey not traced
    float3 c = cached ? float3(0, 1, 1) : tier == 1 ? float3(0, 1, 0) : tier == 2 ? float3(1, 0, 0) : tier == 3 ? float3(1, 0.5, 0)
             : tier == 4 ? float3(0, 0, 1) : tier == 5 ? float3(1, 0, 1) : float3(0.2, 0.2, 0.2);
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
#if HITMAT_PASS
  if (pc.rf_debug == 7u) {   // the hit's base colour (EN2)
    od = 0.0.xxx;
    if (hm_have)
      od = (hm_dbg.known & kHmAlbedo) != 0u ? hm_dbg.albedo
           : hm_dbg.family == kFamUnjoined   ? float3(0.25, 0.0, 0.0)
           : hm_dbg.family == kFamLegacy     ? float3(0.25, 0.25, 0.0)
                                             : float3(0.25, 0.0, 0.25);
    os = 0;
  }
#endif
  // Never hand the game a non-finite value.
  if (any(!isfinite(os))) os = gs;
  if (any(!isfinite(od))) od = gd;
  gSpecular[fp] = os;
  if (pc.rf_debug != 0) gDiffuse[fp] = od;
  rfHitDist[fp] = (tier == 1 || cached) && w > 0.0 ? min(hit_t, kNoHit) : kNoHit;

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
  }
}
