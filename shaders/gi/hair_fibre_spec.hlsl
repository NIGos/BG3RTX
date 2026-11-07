// rt_hair_fibre_spec (docs/RTX_HAIR_PRIMARY.md M4.2): specular indirect light on the visible strands with the fibre
// BSDF's R lobe (rt/hair_bsdf.hlsli, Chiang 2016 p = 0) instead of the game's class-5 probe term. Runs right after D-RF
// (core/gi.cpp, D-RF's layout, sets and push constants) on the pixels whose G-buffer sample is a strand of this frame
// (gi/hair_strand_pixel.hlsli):
//     b11 += A_0 L(wi) - Tp
// Tp: the game's hair specular ambient as the lighting permutation 3033c895 computes it (lines 955-1000 and 1340-1640 of
//     its SPIR-V disassembly): two-sided, 0.3 of the probes about R = reflect(-V, N) and 0.7 about -R, each times the
//     env BRDF F0 DFG.x + DFG.y at max(|N.V|, 1/512); the local probes of the froxel mixed the same way; times
//     k = b13[0] 100 and Lagarde's SO. D-RF leaves class 5 to the game (rf_models), so b11 still holds exactly this term
//     (with D-RF's convention: the aerial-perspective factor, 1 indoors, is not modelled).
// wi:  one direction drawn exactly from the R lobe at a uniform offset h (hb_sample_r); its weight is A_0, the Fresnel
//     reflectance at cos theta_o cos gamma_o, so E[A_0 L(wi)] = the R lobe's integral of L (white furnace: bsdf.r_furnace).
// L:   D-RF's tier (a): an on-screen hit the camera sees there gives last frame's radiance at that pixel, weight w (the
//     screen-edge fade x rf_strength); the rest (1 - w, and every other hit, miss or unknown query) takes the game's own
//     probes in direction wi (global and local, the game's parallax and blend, at its sharpest roughness 0.09) x k x SO:
//     the environment the game's term integrates, now through the fibre's lobe instead of the GGX split sum.
// The R lobe is the fibre's surface reflection; TT, TRT and multiple scattering of indirect light are M4.1's diffuse
// volume response (gi_composite, rt_hair_fibre_gi). Key off: the pass is not dispatched (no shared module changes).
#include "gi_common.hlsli"
#include "../rt/rt_common.hlsli"
#include "../rt/bg3_lights.hlsli"
#include "../rt/bg3_ambient.hlsli"
#include "../rt/hair_bsdf.hlsli"
#include "../rt/hair_primary.hlsli"
#include "hair_strand_pixel.hlsli"

static const float kHsEnvRough = 0.09;   // the game's roughness floor: the sharpest probe level it reads

float3 hs_n_world(float3 nv) { return nv.x * cam.m6_r0.xyz + nv.y * cam.m6_r1.xyz + nv.z * cam.m6_r2.xyz; }
float hs_u01(uint x) { return float(x >> 8) * (1.0 / 16777216.0); }

// The global probes in direction R at roughness a (b35, blended with b37 by _m29), as bg3_spec_ambient.
float3 hs_global(float3 R, float a) {
  const float m27 = lConsts.c[31].z, m28 = lConsts.c[31].w, m29 = lConsts.c[32].x;
  const float m30 = lConsts.c[32].y, m31 = lConsts.c[32].z;
  float3 g = lgSpecCube0.SampleLevel(lgSampCube, R, a * 8.0 + (m30 - 8.0)).rgb * m27;
  [branch] if (m29 > 0.0) g = lerp(g, lgSpecCube1.SampleLevel(lgSampCube, R, a * 8.0 + (m31 - 8.0)).rgb * m28, m29);
  return g;
}

// The local probe record b (lgProbes, stride 144) in direction R from P: rgb = its radiance (x the env BRDF at the
// probe's own blur when brdf), w = its blend weight (0 when the ray misses the volume or the weight is <= 1e-4). The
// caller has checked that P is inside the volume (`inside`).
float4 hs_local(uint b, float3 p, float3 R, float a, float nov, float3 f0, bool brdf) {
  const float4 m0 = asfloat(lgProbes.Load4(b));
  const float4 r0 = asfloat(lgProbes.Load4(b + 16u)), r1 = asfloat(lgProbes.Load4(b + 32u)), r2 = asfloat(lgProbes.Load4(b + 48u));
  const float m3 = asfloat(lgProbes.Load(b + 128u)), m4 = asfloat(lgProbes.Load(b + 132u));
  const uint m5 = lgProbes.Load(b + 136u), m6 = lgProbes.Load(b + 140u);
  const bool box = (m5 & 1u) != 0u;
  const float3 ap = abs(p);
  const float pp = dot(p, p);
  const float3 d = float3(dot(r0.xyz, R), dot(r1.xyz, R), dot(r2.xyz, R));
  float2 ts = 0;
  bool hit;
  if (box) {
    const float3 t0 = (m0.xyz - p) / d, t1 = (-m0.xyz - p) / d;
    const float3 tn = min(t0, t1), tf = max(t0, t1);
    ts = float2(max(tn.x, max(tn.y, tn.z)), min(tf.x, min(tf.y, tf.z)));
    hit = !(ts.x > ts.y);
  } else {
    const float qa = dot(d, d), hb = dot(d, p), qb = 2.0 * hb, qc = pp - m0.y;
    const float disc = qb * qb - 4.0 * qa * qc;
    hit = disc >= 0.0;
    float x0 = 0, x1 = 0;
    if (hit) {
      if (disc == 0.0) {
        x0 = x1 = -hb / qa;
      } else {
        const float qq = qb > 0.0 ? -0.5 * (qb + sqrt(disc)) : -0.5 * (qb - sqrt(disc));
        x0 = qc / qq;
        x1 = qq / qa;
      }
      hit = !(x1 < 0.0 && x0 < 0.0);
      ts = x1 > x0 ? float2(x0, x1) : float2(x1, x0);
    }
  }
  const float3 X = p + d * ts.y;
  const float fade = box ? 1.0 - saturate(length(max(ap - (m0.xyz - m0.w), 0.0)) / max(m0.w, 9.9999997e-05))
                         : saturate((m0.x - length(p)) / max(m0.w, 9.9999997e-05));
  const float w = (hit ? 1.0 : 0.0) * smoothstep(0.0, 1.0, fade);
  const int i0 = (int(m6 & 65535u) - 1) * 2, i1 = (int((m6 >> 16) & 65535u) - 1) * 2;
  if (!(w > 9.9999997e-05) || i0 < 0) return 0.0.xxxx;
  const float al = lerp(clamp(ts.y / length(X) * a, 0.0, a), a, a);
  const float3 dir = lerp(X, d, al);
  float4 c = bg3_probe_cube(uint(i0), dir, al * 8.0);
  [branch] if (i1 >= 0) c = lerp(c, bg3_probe_cube(uint(i1), dir, al * 8.0), m3);
  c *= m3 < 0.0 ? -m3 : 1.0;
  float3 rgb = c.rgb * m4;
  if (brdf) {
    const float4 dl = bg3_dfg(nov, al);
    rgb *= f0 * dl.x + dl.y;
  }
  return float4(rgb, c.w * w);
}

// Probe record offset of froxel entry k when P lies inside its volume and it has a specular cube; p: P in its frame.
bool hs_inside(uint k, float3 P, out uint b, out float3 p) {
  b = 0u;
  p = 0;
  const uint pi = bg3_light_index(k);
  if (!bg3_in(lgProbes, pi, 144u)) return false;
  b = pi * 144u;
  if ((lgProbes.Load(b + 136u) & 4u) == 0u) return false;   // ((m5 >> 1) & 3) & 2: a specular cube
  const float4 m0 = asfloat(lgProbes.Load4(b));
  const float4 r0 = asfloat(lgProbes.Load4(b + 16u)), r1 = asfloat(lgProbes.Load4(b + 32u)), r2 = asfloat(lgProbes.Load4(b + 48u));
  const float3 q = P - float3(r0.w, r1.w, r2.w);
  p = float3(dot(r0.xyz, q), dot(r1.xyz, q), dot(r2.xyz, q));
  return (lgProbes.Load(b + 136u) & 1u) != 0u ? !any(abs(p) >= m0.xyz) : dot(p, p) < m0.y;
}

uint2 hs_probe_range(uint4 cl) {
  const uint first = (cl.z & 1023u) + ((cl.y >> 12) & 4095u) + (cl.y & 4095u) + cl.x;
  return uint2(first, first + (cl.y >> 24));
}

// The game's class-5 specular ambient S (before k and SO): 3033c895's two-sided probe term.
float3 hs_game_term(float3 P, float3 N, float3 V, float3 f0, float a, uint4 cl) {
  const float3 R = reflect(-V, N);
  const float nov = max(abs(dot(V, N)), 0.001953125);
  const float4 dfg = bg3_dfg(nov, a);
  const float3 E = f0 * dfg.x + dfg.y;
  float3 S = hs_global(R, a) * E * 0.3 + hs_global(-R, a) * E * 0.7;
  const uint2 range = hs_probe_range(cl);
  [loop] for (uint k = range.x; k < range.y; ++k) {
    uint b;
    float3 p;
    if (!hs_inside(k, P, b, p)) continue;
    const float4 m = hs_local(b, p, R, a, nov, f0, true) * 0.3 + hs_local(b, p, -R, a, nov, f0, true) * 0.7;
    S = lerp(S, m.rgb, m.w);
  }
  return S;
}

// The game's probes' radiance in direction R (no env BRDF): what its split sum integrates, at roughness a.
float3 hs_env(float3 P, float3 R, float a, uint4 cl) {
  float3 S = hs_global(R, a);
  const uint2 range = hs_probe_range(cl);
  [loop] for (uint k = range.x; k < range.y; ++k) {
    uint b;
    float3 p;
    if (!hs_inside(k, P, b, p)) continue;
    const float4 m = hs_local(b, p, R, a, 1.0, 0.0.xxx, false);
    S = lerp(S, m.rgb, m.w);
  }
  return S;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  const uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  g_rt_pass = kRtPassRefl;
  const float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) return;
  const float4 mat = gMaterial.Load(int3(fp, 0));
  if ((uint(round(mat.w * 255.0)) & 7u) != kMatHair) return;
  float3 T;
  if (!hf_strand(fp, d, T)) return;
  const float2 uv = (float2(fp) + 0.5) / float2(lFlags.res);
  const float3 P = ray_at(uv) * d;
  const float dist = length(P);
  const float3 V = normalize(-P);
  const float3 N = hs_n_world(decode_normal(gNormal.Load(int3(fp, 0)).xy));
  const float a = max(0.09, mat.x);            // 3033c895:356, no specular AA
  const float3 f0 = (0.16 * mat.y * mat.y).xxx;  // 3033c895:355, metalness forced to 0
  const float k = asfloat(lScale.Load(0)) * 100.0;
  const uint4 cl = bg3_cluster_raw(fp, d);
  const float so = bg3_spec_occlusion(fp, dot(N, V), a);
  const float3 Tp = hs_game_term(P, N, V, f0, a, cl) * (k * so);

  // ---- one R-lobe direction at a uniform offset ----
  const uint3 rnd = pcg3d(uint3(fp, pc.frame) ^ uint3(0x510e527fu, 0x9b05688cu, 0x1f83d9abu));
  const uint3 rnd2 = pcg3d(rnd ^ uint3(0x5be0cd19u, 0x6a09e667u, 0xbb67ae85u));
  const float h = 2.0 * hs_u01(rnd2.x) - 1.0;
  float3 wi;
  const float A0 = hb_sample_r(T, V, h, a, float3(hs_u01(rnd.x), hs_u01(rnd.y), hs_u01(rnd.z)), wi);

  // ---- its radiance: D-RF's tier (a), else the game's probes in that direction ----
  float w = 0.0;
  float3 Lrt = 0;
  const float pa = pixel_angle(uv);
  RtCone cone = {dist * pa, pa};
  const float3 o = P + wi * max(1e-3, 2.0 * dist * pa);   // off the pixel's own fibre (as hf_strands' 1 mm)
  bool aborted;
  const float t = rt_closest(o, wi, pc.rf_tmax, cone, aborted);
  if (!aborted && t >= 0.0) {
    const float3x3 inv = frustum_inverse();
    float2 quv;
    float qd;
    if (project(inv, o + wi * t, quv, qd) && all(quv >= 0.0) && all(quv <= 1.0)) {
      const int2 qp = min(int2(quv * float2(pc.full)), int2(pc.full) - 1);
      const float sd = gDepth.Load(int3(qp, 0));
      const float3 nq = hs_n_world(decode_normal(gNormal.Load(int3(qp, 0)).xy));
      const float4 oq = rt_origin_at(uint2(qp));
      const bool has_tlas = pc.rt_origin != 0u && oq.w < kOriginNoHit;
      const float3 ngq = pc.rt_origin != 0u ? oq.xyz : depth_geo_normal(qp, ray_at((float2(qp) + 0.5) / float2(pc.full)) * sd);
      float3 L;
      if (sd > 0.0 && depth_same_surface(qp, qd, sd, ngq, has_tlas, oq.w, pc.rf_depth_tol) && dot(nq, wi) < 0.2 &&
          prev_radiance_at(qp, L)) {
        const float2 e = min(quv, 1.0 - quv) / max(pc.rf_edge, 1e-4);
        w = saturate(min(e.x, e.y)) * saturate(pc.rf_strength);
        Lrt = max(L, 0.0);
      }
    }
  }
  const float3 Lenv = w < 1.0 ? hs_env(P, wi, kHsEnvRough, cl) * (k * so) : 0.0.xxx;
  const float3 gs = gSpecular[fp];
  const float3 os = max(gs - Tp + A0 * (w * Lrt + (1.0 - w) * Lenv), 0.0);
  if (all(isfinite(os))) gSpecular[fp] = os;
}
