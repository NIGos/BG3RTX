// Shared bindings and helpers for the screen-space GI passes. Everything the
// game provides is bound straight from its own descriptors at the tiled
// lighting dispatch (see docs/FRAME_MAP.md and docs/TRACK_C_GI.md).
#pragma once

// set 0 binding 1 of the lighting shader. Positions in this space are
// camera-relative: P = ray(uv) * linear_depth.
struct CameraCB {
  float4 v0, v1, v2;                   // v0.xyz = direction towards the sun (_m0), v1.xyz = sun colour (_m2)
  float4 m6_r0, m6_r1, m6_r2, m6_r3;   // _m6, row-major: view normal -> lighting space
  float4 ray0, ray1, ray2;             // _m7: frustum rays at the viewport's (0,0), (1,0), (0,1) in ab space
  float4 x[15];                        // _m8.._m37 (sun colour, atmosphere, cloud shadows; rt/bg3_sun.hlsli)
};
// set 0 binding 4: viewport mapping, ab = ((uv - bias) / scale) * 0.5
struct ViewportCB {
  float2 scale;
  float2 bias;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraCB> cam;
[[vk::binding(1, 0)]] ConstantBuffer<ViewportCB> vp;
[[vk::binding(2, 0)]] Texture2D<float4> gNormal;    // RG16F, stereographic, view space
[[vk::binding(3, 0)]] Texture2D<float4> gAlbedo;    // RGBA8 base colour
[[vk::binding(4, 0)]] Texture2D<float4> gMaterial;  // RGBA8: roughness, specular, metalness, flags
[[vk::binding(5, 0)]] Texture2D<float> gDepth;      // R32F linear depth
[[vk::binding(6, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> gDiffuse;
[[vk::binding(7, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> gSpecular;
// GI state, half resolution: rgb = one-bounce radiance, a = visibility
// (fraction of rays that found no occluder within the search radius).
[[vk::binding(8, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> giRaw;
[[vk::binding(9, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> giHistIn;
[[vk::binding(10, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> giHistOut;
// Last frame's lit diffuse + specular at render resolution: radiance at ray hits.
[[vk::binding(11, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> prevRadiance;
// The game's AO target (RGB10A2, AO in .x), read by its ambient term.
[[vk::binding(12, 0)]] [[vk::image_format("rgb10a2")]] RWTexture2D<float4> gAO;
[[vk::binding(13, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> giRawDepth;
[[vk::binding(14, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> giHistDepthIn;
[[vk::binding(15, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> giHistDepthOut;
// The game's screen-space sun shadow mask (R8), read by its sun term.
[[vk::binding(16, 0)]] [[vk::image_format("r8")]] RWTexture2D<float> gSunShadow;
// The motion vectors the game gives DLSS: previous = current + mv * mv_scale, render pixels.
[[vk::binding(17, 0)]] Texture2D<float2> gVelocity;

struct Push {
  uint2 full;       // render resolution
  uint2 half;       // GI resolution
  uint frame;
  float strength;
  float radius;     // march length as a fraction of the pixel's depth
  float thickness;  // depth-relative hit thickness
  uint rays;
  uint steps;
  uint debug;       // 0 off, 1 GI irradiance only, 2 added term x4
  float history;    // temporal blend weight of the new sample
  float ao;         // how strongly visibility multiplies the game's AO (0..1)
  float cs_length;  // contact-shadow march length, fraction of camera distance
  float cs_strength;
  uint cs_steps;
  float2 mv_scale;
  uint has_mv;
  uint bounce_spec; // 1: GI bounces diffuse + specular (old); 0: the diffuse only (QUALITY R3)
  // Track D ray-traced sun shadows (rt_shadow.hlsl)
  float rt_bias;      // ray origin offset along the normal, fraction of camera distance
  float rt_tmax;      // shadow ray length (world units)
  uint rt_debug;      // 0 off, 1 visibility, 2 game mask (red) vs traced (green), 3 primary-ray depth check
  float rt_strength;  // how strongly traced visibility darkens the game's sun shadow mask
  float rt_ao_radius; // ray-traced AO ray length (world units, rt_ao.hlsl)
  uint rt_ao_rays;
  uint ls_kmode;            // D-LS exact-k (DESIGN_SOTA_NEXT 1a): bits 0-3 ls_k (0 trace every light), bits 4-7 ls_rest_samples
  uint sharc_fx;           // sharc_inject.hlsl: 1 the D-EM effects target is bound at 49 (emission)
  // Hybrid ray-traced GI (gi_trace_rt.hlsl)
  float gi_rt_radius;   // ray length and falloff radius (world units)
  float gi_rt_tol;      // gi_rt_depth_k: multiplier on the derived on-screen-hit depth tolerance (1; rt/depth_tol.hlsli)
  uint gi_rt_fallback;  // 0 occlude, no bounce; 1 ignore; 2 occlude + mean on-screen radiance
  uint rt_stats;       // 1: count into rtStats (rt_common.hlsli)
  // Quality pass (docs/QUALITY.md)
  float sun_radius;    // half-angle of the sun disc (rad; the game: 0.00459); 0 = hard shadows
  uint res_shift;      // GI resolution: 1 half res (own denoise), 0 full res, 1 spp, for RR
  uint qa_zero;      // QA light-off traces (docs/QA.md): bit 0 sun mask 0, bit 1 local-light visibility 0, bit 2 AO 0
  uint rt_max_cand;  // QA (rt_max_candidates): ray-query candidate cap, 0 = kMaxCandidates (rt_common.hlsli)
  // D-LS ray-traced local-light shadows (rt_local_shadows.hlsl)
  float ls_source_radius;  // light sphere radius for soft shadows (world units)
  float ls_clear;          // rays stop this short of the light (its own fixture)
  float ls_min_radius;     // unshadowed lights with a smaller range are left alone
  uint ls_samples;         // 0 one ray per light, 1-2 RIS-chosen rays per pixel
  uint ls_shadowed;        // lights with a game shadow map: 0 keep, 1 replace, 2 min
  uint ls_debug;
  uint ls_stats;
  uint ls_pass;            // ls_check: bit 0 the join's own check, bit 1 the reference against itself (check pass)
  uint ls_soft;            // 1 sample the sphere (RR on), 0 hard
  float ls_strength;
  uint ls_has_mask;
  uint ls_flags;            // bit 0 per-light data; bits 8-12 owners; bit 21 HPO receiver-guard diagnostic
  // D-RF ray-traced reflections (rt_reflections.hlsl)
  float rf_max_rough;     // perceptual roughness at which reflections have faded to the probe
  float rf_mirror_rough;  // below it one deterministic mirror ray
  float rf_tmax;          // reflection ray length (world units)
  float rf_depth_tol;     // rf_depth_k: multiplier on the derived on-screen-hit depth tolerance (1; rt/depth_tol.hlsli)
  float rf_edge;          // screen-edge fade width (uv)
  float rf_strength;
  uint rf_debug;
  uint rf_sample;         // 1 GGX bounded VNDF (RR denoises), 0 mirror rays only (no denoiser)
  uint rf_so;             // 1 also apply the game's specular occlusion to traced hits
  uint rf_validate;       // 1 EN4 check: the lighting ran with AO 1 and no sun (prep pass)
  uint rf_models;         // bit mask of the shading models (RT2.w & 7) that get reflections
  uint rf_stats;
  // RT audit fixes (docs/AUDIT_RT.md)
  uint rt_backlit;   // 1: trace the sun for back-lit skin/translucent/hair pixels (0: zero their mask)
  uint mat_classes;  // 1: material classes in D-LS and the GI composite (0: everything class 0)
  uint ao_direct;    // AO on direct light: 0 the game's mix(1, AO, m0.y) on what b6 holds, 1 none on traced lights
  uint rt_origin;    // ray origins: 0 shading normal x rt_bias x distance, 1 geometric normal, robust offset
};
[[vk::push_constant]] Push pc;

float3 decode_normal(float2 rg) {
  float2 e = rg * 3.5554 - 1.7777;
  float t = dot(e, e) + 1.0;
  if (t > 5.4e10) return float3(0, 0, 1);
  float s = 2.0 / t;
  return float3(e * s, 1.0 - s);
}

// Same transform as the game: n_out = n * mat3(columns of _m6).
float3 to_lighting_space(float3 n) {
  return normalize(n.x * cam.m6_r0.xyz + n.y * cam.m6_r1.xyz + n.z * cam.m6_r2.xyz);
}

float2 uv_to_ab(float2 uv) { return ((uv - vp.bias) / vp.scale) * 0.5; }
float2 ab_to_uv(float2 ab) { return (ab * 2.0) * vp.scale + vp.bias; }

float3 ray_at(float2 uv) {
  float2 ab = uv_to_ab(uv);
  return cam.ray0.xyz * (1.0 - ab.x - ab.y) + cam.ray1.xyz * ab.x + cam.ray2.xyz * ab.y;
}

// Angle one render pixel subtends at uv (the frustum rays are not unit length).
float pixel_angle(float2 uv) {
  float3 r = ray_at(uv);
  return length(ray_at(uv + float2(1.0 / float(pc.full.x), 0.0)) - r) / max(length(r), 1e-20);
}

// Inverse of P = d * (r0 + (r1 - r0) a + (r2 - r0) b).
float3x3 frustum_inverse() {
  float3 c0 = cam.ray0.xyz, c1 = cam.ray1.xyz - cam.ray0.xyz, c2 = cam.ray2.xyz - cam.ray0.xyz;
  float3 r0 = cross(c1, c2), r1 = cross(c2, c0), r2 = cross(c0, c1);
  float det = dot(c0, r0);
  return float3x3(r0, r1, r2) / det;   // rows: applying it to P gives (d, d*a, d*b)
}

// Returns false behind the camera. uv in [0,1] when on screen.
bool project(float3x3 inv, float3 P, out float2 uv, out float d) {
  float3 x = mul(inv, P);
  d = x.x;
  uv = ab_to_uv(x.yz / max(d, 1e-12));
  return d > 0.0;
}

// Last frame's lit radiance at the surface now seen at pixel qp: follow the
// motion vector back to where that surface was a frame ago.
// rgb: diffuse + specular, what the camera saw (reflections need it); a: the diffuse's
// share of its luminance (gi_composite.hlsl).
int2 prev_radiance_px(int2 qp) {
  if (pc.has_mv == 0) return qp;
  float2 mv = gVelocity.Load(int3(qp, 0)) * pc.mv_scale;
  int2 pq = int2(floor(float2(qp) + 0.5 + mv));
  return any(pq < 0) || any(pq >= int2(pc.full)) ? qp : pq;
}
float3 prev_radiance_at(int2 qp) { return prevRadiance[prev_radiance_px(qp)].rgb; }
// The GI's bounce source: view-independent radiance (QUALITY R3), i.e. the diffuse part.
// The specular is what the surface sends towards the camera, not towards the GI's
// shading point; its sun glints are the sparsest, brightest values on screen.
float3 prev_bounce_at(int2 qp) {
  float4 v = prevRadiance[prev_radiance_px(qp)];
  return pc.bounce_spec != 0 ? v.rgb : v.rgb * saturate(v.a);
}

// pcg3d (Jarzynski & Olano 2020): decorrelated across pixels and frames,
// unlike a linear mix of coordinates fed to a 1D hash.
uint3 pcg3d(uint3 v) {
  v = v * 1664525u + 1013904223u;
  v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
  v ^= v >> 16u;
  v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
  return v;
}
uint pixel_seed(uint2 p, uint salt) { return pcg3d(uint3(p, pc.frame * 4u + salt)).x; }

// GI sample position: one full-res pixel per GI texel. At half res it rotates
// inside each 2x2 block over frames; at full res (RR mode) it is the pixel itself.
uint2 gi_fp(uint2 id) {
  if (pc.res_shift == 0) return min(id, pc.full - 1);
  return min(id * 2 + uint2(pc.frame & 1, (pc.frame >> 1) & 1), pc.full - 1);
}

uint hash(uint x) {
  x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
  return x;
}

#include "../rt/depth_tol.hlsli"

// This frame's full-resolution sample of half-res texel q (gi_fp), whose depth giRawDepth /
// giHistDepthOut hold; pixel centre, render pixels.
float2 gi_sample_px(int2 q) {
  if (pc.res_shift == 0) return float2(q) + 0.5;
  return float2(min(uint2(q) * 2u + uint2(pc.frame & 1u, (pc.frame >> 1) & 1u), pc.full - 1u)) + 0.5;
}

// Depth-aware 3x3 gather of the accumulated half-resolution history (joint bilateral
// upsample). Depth weight exp(-|dr| / sigma), dr the difference of camera distances (rotation
// invariant; view z would differ by the rays' own length change off axis), sigma =
// depth_tol: the largest difference two samples of the same surface show at their actual
// offset on screen (|q's sample - fp|, render pixels; both from this frame, so the same
// jitter: no jitter term), both values from the depth buffer (2 roundings). The slope is the
// depth buffer's own geometric normal at fp. Was: sigma = 1% of the depth.
// The centre's x2 is the spatial kernel of the half-res denoise (not a depth tolerance),
// unchanged; exact bilinear weights would drop most of that denoise.
float4 gather_history(uint2 fp, float d) {
  // Full res (RR mode): RR denoises; no spatial filter of our own.
  if (pc.res_shift == 0) return giHistOut[min(fp, pc.half - 1)];
  int2 hp = int2(fp >> pc.res_shift);
  const float2 px = float2(fp) + 0.5;
  const float2 inv_full = 1.0 / float2(pc.full);
  const float3 P = ray_at(px * inv_full) * d;
  const float r = length(P);
  const float pa = pixel_angle(px * inv_full);
  const float ndv = r > 0.0 ? dot(depth_geo_normal(int2(fp), P), P) / r : 0.0;
  float4 acc = 0;
  float wsum = 0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
      int2 q = clamp(hp + int2(x, y), int2(0, 0), int2(pc.half) - 1);
      float hd = giHistDepthOut[q];
      float2 qs = gi_sample_px(q);
      float rq = hd * length(ray_at(qs * inv_full));
      float sigma = depth_tol(r, pa, ndv, length(qs - px), 2.0);
      float w = hd > 0.0 ? exp(-abs(rq - r) / sigma) * (x == 0 && y == 0 ? 2.0 : 1.0) : 0.0;
      acc += giHistOut[q] * w;
      wsum += w;
    }
  return wsum > 1e-4 ? acc / wsum : float4(0, 0, 0, 1);
}

// D-LS AO-on-direct give-back (ao_direct): the lighting wrote g >= direct x f_from (each
// term non-negative), so giving the direct light f_to instead adds at most
// g x (f_to - f_from) / f_from per channel. A recomputed direct term the game never had
// (a sun colour read from a wrong or stale descriptor: the S6 green walls, ~14x and
// green) can then not add more light than the game's own pixel implies.
float3 ls_giveback_cap(float3 x, float3 g, float f_to, float f_from) {
  if ((pc.ls_flags & (1u << 18)) == 0u) return x;   // ls_giveback_cap=0 (A/B, ls_check)
  const float r = max(f_to - f_from, 0.0) / max(f_from, 1e-3);
  return min(x, max(g, 0.0) * r);
}

float rand01(inout uint s) { s = hash(s); return float(s & 0x00ffffff) / 16777216.0; }

float3 cosine_hemisphere(float3 n, float u1, float u2) {
  float r = sqrt(u1), phi = 6.2831853 * u2;
  float3 t = abs(n.z) < 0.999 ? normalize(cross(float3(0, 0, 1), n)) : float3(1, 0, 0);
  float3 b = cross(n, t);
  return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1)));
}

// ---- G-buffer material classes: RT2.a bits 0-2 (bits 4-6 are the light channel) ----
// Read the way the lighting permutations do (f469db78 class 0 only, 118833ff class 3
// only, 34a007cc all classes; docs/FRAME_MAP.md). RT3 holds the per-class parameters.
static const uint kMatStandard = 0u;
static const uint kMatUnlit = 1u;        // the lighting skips it: nothing is written to b10/b11
static const uint kMatAniso = 2u;        // RT3.xyz tangent (oct 12:12), RT3.w anisotropy
static const uint kMatSkin = 3u;         // SSS: RT3.x profile, .y scatter scale, .z thickness, .w transmission
static const uint kMatTranslucent = 4u;  // thin translucency (foliage): RT3.x profile, .z thickness
static const uint kMatHair = 5u;         // Marschner hair: RT3.xyz tangent, .w scatter; RT2.z backlit
static const uint kMatCloth = 6u;        // sheen only: RT3.rgb sheen colour
static const uint kMatClothGgx = 7u;     // GGX * RT3.w + sheen * (1 - RT3.w)
uint mat_class(float4 m) { return uint(round(m.w * 255.0)) & 7u; }
// Classes whose sun and local-light terms read the shadow on back-lit pixels: SSS
// and thin translucency add a transmission lobe for N.L < 0.2588, hair has no N.L gate.
bool mat_transmits(uint cls) { return cls == kMatSkin || cls == kMatTranslucent || cls == kMatHair; }

// Extra bindings (core/gi.cpp kExtras). The lighting's set 0 b0 (std140 floats):
// _m4 (b.x) != 0 makes the skin permutations shade with diffuse colour 1, the SSS
// pass multiplying the albedo back in afterwards (118833ff:371, d1669194:359).
struct FrameCB { float4 a, b; };
[[vk::binding(43, 0)]] ConstantBuffer<FrameCB> gFrame;
// Ours, full resolution: .x the game's own AO before the GI fold (gi_ao_apply.hlsl).
[[vk::binding(44, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gAux;

bool mat_on() { return (pc.mat_classes & 1u) != 0u; }
// The class the lighting shaded this pixel as (0 when mat_classes=0).
uint mat_class_at(uint2 fp) { return mat_on() ? mat_class(gMaterial.Load(int3(fp, 0))) : kMatStandard; }
// Diffuse colour 1 on skin pixels (the albedo is applied by the SSS pass).
bool skin_white() { return (pc.mat_classes & 8u) != 0u && gFrame.b.x != 0.0; }
// The diffuse colour the game's lighting multiplies diffuse light by, per class
// (f469db78:343; 118833ff:371; 3033c895:354 hair, metalness forced 0; 95af6e4f:340 cloth, raw albedo).
float3 mat_diffuse_colour(uint cls, float3 albedo, float metal) {
  if (cls == kMatUnlit) return 0.0.xxx;
  if (cls == kMatHair || cls == kMatCloth) return albedo;
  if (cls == kMatSkin && skin_white()) return (1.0 - metal).xxx;
  return albedo * (1.0 - metal);
}
