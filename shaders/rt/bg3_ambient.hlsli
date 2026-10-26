// D-EN4: BG3's specular ambient (the probe/IBL term), replicated from the tiled
// lighting compute shader f469db78 (captures/20261025-235042, lines 967-1344; see
// docs/FRAME_MAP.md "Specular ambient" and docs/TRACK_D_RT.md section 10).
//
// The game's specular output is
//     b11 = T * ( direct · mix(1, SO, b0.m0.y)  +  S · (b13[0]·100) · SO )
// with T the aerial-perspective transmittance (1 indoors), SO Lagarde's specular
// occlusion from the AO target, and S (_1684) this file's bg3_spec_ambient():
//   1. the global probes: cube b35 (and b37, blended by b39 _m29) at R = reflect(-V, N),
//      mip a·8 + _m30 − 8 (resp. _m31), × _m27 (resp. _m28);
//   2. × the split-sum env BRDF  E = F0·DFG.x + DFG.y, DFG = LUT b38 at (max(N·V, 1/512), a);
//   3. the local probes listed in the pixel's froxel (b23 y >> 24, after the point, spot
//      and projector lights): box or sphere, parallax-corrected, fading at the edge,
//      each lerped over S by its weight cube × edge fade.
// `a` is the game's roughness after its clamp and specular AA (_515), not squared.
//
// The game's own set 1 of the lighting dispatch is bound as our set 2 (the core
// binds the exact descriptor set the lighting used, core/gi.cpp), so every resource
// here, including the bindless cube array b45, is the game's.
#pragma once

[[vk::binding(6, 2)]] Texture2D<float4> lgAO;            // b6: the AO target as the lighting sampled it
[[vk::binding(16, 2)]] SamplerState lgSampCube;          // b16 (_7): the probes' sampler
[[vk::binding(32, 2)]] SamplerState lgSampLut;           // b32 (_12): the LUT's sampler
[[vk::binding(33, 2)]] ByteAddressBuffer lgProbes;       // b33: local env probes (struct _57, stride 144)
[[vk::binding(35, 2)]] TextureCube<float4> lgSpecCube0;  // b35: global specular probe
[[vk::binding(37, 2)]] TextureCube<float4> lgSpecCube1;  // b37: second global specular probe
[[vk::binding(38, 2)]] Texture2D<float4> lgDfg;          // b38: DFG LUT (x F0 scale, y bias, z diffuse)
[[vk::binding(45, 2)]] TextureCube<float4> lgCubes[];    // b45: bindless probe cubes

float4 bg3_dfg(float nov, float a) { return lgDfg.SampleLevel(lgSampLut, float2(nov, a), 0.0); }

float4 bg3_probe_cube(uint i, float3 dir, float mip) {
  return float4(lgCubes[NonUniformResourceIndex(i)].SampleLevel(lgSampCube, dir, mip).rgb,
                lgCubes[NonUniformResourceIndex(i + 1u)].SampleLevel(lgSampCube, dir, mip).x);
}

// S (_1684) at a surface point P (lighting space), normal N (as the game has it,
// not renormalised), view V, F0, roughness a. E: the pixel's env BRDF
// (F0·DFG.x + DFG.y at its own roughness). n_local: local probes applied.
float3 bg3_spec_ambient(float3 P, float3 N, float3 V, float3 f0, float a, uint4 cl, out float3 E, out uint n_local) {
  float3 R = reflect(-V, N);
  float nov = max(dot(V, N), 0.001953125);
  float m27 = lConsts.c[31].z, m28 = lConsts.c[31].w, m29 = lConsts.c[32].x;
  float m30 = lConsts.c[32].y, m31 = lConsts.c[32].z;
  float3 g = lgSpecCube0.SampleLevel(lgSampCube, R, a * 8.0 + (m30 - 8.0)).rgb * m27;
  [branch] if (m29 > 0.0) g = lerp(g, lgSpecCube1.SampleLevel(lgSampCube, R, a * 8.0 + (m31 - 8.0)).rgb * m28, m29);
  float4 dfg = bg3_dfg(nov, a);
  E = f0 * dfg.x + dfg.y;
  float3 S = g * E;

  n_local = 0;
  uint first = (cl.z & 1023u) + ((cl.y >> 12) & 4095u) + (cl.y & 4095u) + cl.x;
  uint end = first + (cl.y >> 24);
  [loop] for (uint k = first; k < end; ++k) {
    uint pi = lIndex.Load(k * 4u);
    uint b = pi * 144u;
    float4 m0 = asfloat(lgProbes.Load4(b));
    float4 r0 = asfloat(lgProbes.Load4(b + 16u)), r1 = asfloat(lgProbes.Load4(b + 32u)), r2 = asfloat(lgProbes.Load4(b + 48u));
    float m3 = asfloat(lgProbes.Load(b + 128u)), m4 = asfloat(lgProbes.Load(b + 132u));
    uint m5 = lgProbes.Load(b + 136u), m6 = lgProbes.Load(b + 140u);
    if ((m5 & 4u) == 0u) continue;   // ((m5 >> 1) & 3) & 2: has a specular cube
    bool box = (m5 & 1u) != 0u;
    float3 q = P - float3(r0.w, r1.w, r2.w);
    float3 p = float3(dot(r0.xyz, q), dot(r1.xyz, q), dot(r2.xyz, q));
    float3 ap = abs(p);
    float pp = dot(p, p);
    if (box ? any(ap >= m0.xyz) : (pp >= m0.y)) continue;
    float3 d = float3(dot(r0.xyz, R), dot(r1.xyz, R), dot(r2.xyz, R));
    float2 ts = 0;
    bool hit;
    if (box) {
      float3 t0 = (m0.xyz - p) / d, t1 = (-m0.xyz - p) / d;
      float3 tn = min(t0, t1), tf = max(t0, t1);
      ts = float2(max(tn.x, max(tn.y, tn.z)), min(tf.x, min(tf.y, tf.z)));
      hit = !(ts.x > ts.y);
    } else {
      float qa = dot(d, d), hb = dot(d, p), qb = 2.0 * hb, qc = pp - m0.y;
      float disc = qb * qb - 4.0 * qa * qc;
      hit = disc >= 0.0;
      float x0 = 0, x1 = 0;
      if (hit) {
        if (disc == 0.0) {
          x0 = x1 = -hb / qa;
        } else {
          float qq = qb > 0.0 ? -0.5 * (qb + sqrt(disc)) : -0.5 * (qb - sqrt(disc));
          x0 = qc / qq;
          x1 = qq / qa;
        }
        hit = !(x1 < 0.0 && x0 < 0.0);
        ts = x1 > x0 ? float2(x0, x1) : float2(x1, x0);
      }
    }
    float3 X = p + d * ts.y;
    float fade = box ? 1.0 - saturate(length(max(ap - (m0.xyz - m0.w), 0.0)) / max(m0.w, 9.9999997e-05))
                     : saturate((m0.x - length(p)) / max(m0.w, 9.9999997e-05));
    float w = (hit ? 1.0 : 0.0) * smoothstep(0.0, 1.0, fade);
    if (!(w > 9.9999997e-05)) continue;   // the game lerps towards 0 with weight 0
    int i0 = (int(m6 & 65535u) - 1) * 2, i1 = (int((m6 >> 16) & 65535u) - 1) * 2;
    if (i0 < 0) continue;
    float al = lerp(clamp(ts.y / length(X) * a, 0.0, a), a, a);
    float3 dir = lerp(X, d, al);
    float4 c = bg3_probe_cube(uint(i0), dir, al * 8.0);
    [branch] if (i1 >= 0) c = lerp(c, bg3_probe_cube(uint(i1), dir, al * 8.0), m3);
    c *= m3 < 0.0 ? -m3 : 1.0;
    float4 dl = bg3_dfg(nov, al);
    float3 rgb = c.rgb * m4 * (f0 * dl.x + dl.y);
    S = lerp(S, rgb, c.w * w);
    ++n_local;
  }
  return S;
}

// The game's Lagarde specular occlusion (_2469) from its AO target.
float bg3_spec_occlusion(uint2 px, float nov_raw, float a) {
  if (!(lFlags.m0.x > 0.0)) return 1.0;
  float ao = lgAO.Load(int3(px, 0)).x;
  return saturate(pow(max(abs(nov_raw + ao), 9.9999997e-05), exp2(-16.0 * a - 1.0)) - 1.0 + ao);
}
