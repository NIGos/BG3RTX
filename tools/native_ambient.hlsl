// Offline replica of the game's native ambient at the G-buffer (tools/frame_replay.md "Native ambient").
// NOT a game shader: only tools/frame_replay --native-ambient (and its self-test) compiles it, over a
// replay pack that carries the lighting inputs (common/replay_pack.h kGiHitDirect sets 2-3). It reads
// the same functions the in-game replicas use (shaders/rt/bg3_ambient.hlsli, native_hit_shade.hlsli,
// bg3_sun.hlsli), at each GI pixel's full-resolution sample (gi_fp), at the raster G-buffer point.
//
// The tiled lighting's diffuse ambient (f469db78:967-1178) is
//     b10 += T * irr' * dcol * AO * b13[0] 100,    irr' = the local diffuse probes lerped over
//     irr x DFG.z, irr = the sky irradiance cubes b34 / b36 at N (x _m27 / _m28, blended by _m29),
// with dcol the pixel's diffuse colour, AO the game's b6 .x (when b0 m0.x > 0), T the aerial
// perspective (1 indoors; not replicated). E_native = irr' x b13[0] 100 at dcol = 1 is the same
// quantity as giRaw.rgb (mean incident radiance; the lighting multiplies both by dcol): it is what
// the game adds for the light beyond the GI rays.
// Roughness: RT2.x clamped at 0.09, without the quad specular AA (declared; DFG.z and the probe
// parallax roughness move little with it). Skin pixels: albedo x (1 - metalness), not white
// (the SSS "diffuse colour 1" permutation is not modelled).
//
// Push::debug selects what one dispatch writes:
//   rgb (giRaw)                               a (giRaw)                       r (giRawDepth)
//   0 E_native (local probes over the sky)    game AO (b6 .x, or 1)           lum of the sky-only part irr x DFG.z x b13 100
//   1 as lit: E_native x dcol x AO            1 if the froxel lists a probe   lum of the specular ambient S x b13 100 x SO
//   2 sun transmittance (LUT colour / the     cloud shadow at P (both layers) 1 on the atmosphere path, else 0
//     atmosphere colour x intensity; 1 off it)
#define RT_HIT_MATERIAL 1
#include "gi/gi_common.hlsli"
#include "rt/rt_common.hlsli"
#include "rt/bg3_lights.hlsli"
#include "rt/bg3_ambient.hlsli"
#define BG3_SUN_SET 3
#include "rt/bg3_sun.hlsli"
#include "rt/native_hit_material.hlsli"
#define NATIVE_HIT_PROJECTOR_SKIP 1
#define NATIVE_HIT_DLS_POLICY 1
#define NATIVE_HIT_SLOTS_SET 0
#define NATIVE_HIT_SLOTS_BINDING 46
#include "rt/native_hit_direct.hlsli"
#define NATIVE_HIT_SHADE_FULL 1
#include "rt/native_hit_shade.hlsli"

[[vk::binding(2, 2)]] Texture2D<float4> lgAlbedo;     // the lighting's set 1 b2: RT1 base colour
[[vk::binding(3, 2)]] Texture2D<float4> lgMaterial;   // b3: RT2 roughness, specular, metalness, flags

float na_lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  g_rt_pass = kRtPassGI;
  uint2 id = tid.xy;
  if (any(id >= pc.half)) return;
  uint2 fp = gi_fp(id);
  float d = gDepth.Load(int3(fp, 0));
  float2 enc = gNormal.Load(int3(fp, 0)).xy;
  float2 e = enc * 3.5554 - 1.7777;
  if (d <= 0.0 || dot(e, e) + 1.0 > 5.4e10) { giRaw[id] = 0; giRawDepth[id] = 0; return; }
  float2 uv = (float2(fp) + 0.5) / float2(pc.full);
  float3 P = ray_at(uv) * d;
  float3 N = to_lighting_space(decode_normal(enc));
  float3 V = normalize(-P);
  const float4 alb = lgAlbedo.Load(int3(fp, 0)), mat = lgMaterial.Load(int3(fp, 0));
  const float a = max(0.09, mat.x);
  const float nov_raw = dot(V, N), nov = max(nov_raw, 0.001953125);
  const float k = asfloat(lScale.Load(0)) * 100.0;
  const uint mode = pc.debug;
  if (mode == 2u) {
    const bool atmo = cam.x[5].x > 0.0 && cam.x[13].w == 0.0;
    const float3 base = cam.x[13].xyz * cam.x[5].y, c = bg3_sun_colour();
    const float3 t = atmo ? c / max(base, 1e-30.xxx) : 1.0.xxx;
    giRaw[id] = float4(t, bg3_cloud_shadow(P));
    giRawDepth[id] = atmo ? 1.0 : 0.0;
    return;
  }
  float m27 = lConsts.c[31].z, m28 = lConsts.c[31].w, m29 = lConsts.c[32].x;
  float3 irr = lgIrrCube0.SampleLevel(lgSampCube, N, 0.0).rgb * m27;
  [branch] if (m29 > 0.0) irr = lerp(irr, lgIrrCube1.SampleLevel(lgSampCube, N, 0.0).rgb * m28, m29);
  const uint4 cl = bg3_cluster_raw(fp, d);
  const float3 sky = irr * bg3_dfg(nov, a).z;
  // dcol = 1: the probes' term is linear in the diffuse colour (native_hit_diffuse_probes).
  const float3 E = native_hit_diffuse_probes(sky, P, N, nov, 1.0.xxx, a, cl) * k;
  const float ao = lFlags.m0.x > 0.0 ? lgAO.Load(int3(fp, 0)).x : 1.0;
  if (mode == 0u) {
    giRaw[id] = float4(E, ao);
    giRawDepth[id] = na_lum(sky * k);
    return;
  }
  const uint cls = mat_class(mat);
  const float metal = mat.z;
  const float3 dcol = cls == kMatUnlit ? 0.0.xxx : (cls == kMatHair || cls == kMatCloth) ? alb.rgb : alb.rgb * (1.0 - metal);
  const float3 f0 = lerp((0.16 * mat.y * mat.y).xxx, alb.rgb, metal);
  float3 env_brdf; uint n_local;
  const float3 S = bg3_spec_ambient(P, N, V, f0, a, cl, env_brdf, n_local) * k * bg3_spec_occlusion(fp, nov_raw, a);
  giRaw[id] = float4(E * dcol * ao, (cl.y >> 24) != 0u ? 1.0 : 0.0);
  giRawDepth[id] = na_lum(S);
}
