// Track D: ray queries against the host's TLAS, with alpha-tested candidates
// (phase 3, docs/TRACK_D_RT.md). Included by the core's ray-traced passes.
//
// Geometry 0 of the BLAS is opaque. Geometry 1 (rt_alpha=1) holds the draws
// whose depth-prepass fragment shader alpha tests (layer/alphatest.cpp); it is
// not opaque, so its triangles come back as candidates, and alpha_pass()
// reproduces the game's test at the hit: UV interpolated from the captured
// soup, the texture the game sampled, its cut-off. Set 1 is the host's.
// With the BVH split (rt_bvh_split=1, layer/bvhsplit.cpp) the TLAS holds one
// instance per draw instance instead, each BLAS with one geometry; InstInfo
// (below) says where each instance's vertices and alpha record are.
#pragma once

#ifdef RT_NATIVE_TILESET
#include "../../common/native_tileset_coverage.h"
#include "../../common/native_tileblend_coverage.h"
[[vk::binding(19,1)]] SamplerState gNativeTilesetSampler[16];
static bool gTilesetUnknown=false,gTilesetColorKnown=false;
static float gTilesetGreen=0.0;
#endif

[[vk::binding(18, 0)]] RaytracingAccelerationStructure gScene;
// Counters read back by the core (rt_stats=1, core/gi.cpp lists them).
[[vk::binding(20, 0)]] RWByteAddressBuffer rtStats;

// Adds the number of lanes with `cond` to counter `index` (wave-reduced, and
// spread over 64 bins picked by `bin` so the atomics do not serialise; the core
// sums the bins).
// Out-of-range guards (always counted, logged by core/gi.cpp "[core] guard:"):
// counter 118 a read of the game's light list (cluster, index, light, shadow view)
// out of its buffer, 119 a set-1 read (InstInfo, vertices, alpha records) out of
// range. The read is skipped (no light / a miss) instead of faulting.
static const uint kGuardLightList = 118u, kGuardSet1 = 119u, kGuardRay = 120u, kGuardLoopCap = 121u;
// Ray-query loops end after this many candidates (counted, kGuardLoopCap): a
// corrupt or hostile scene can never keep a lane in traversal for seconds (TDR).
// Such a query is aborted with nothing (or not the closest hit) committed: its answer
// is UNKNOWN (kRtUnknown below), never "lit" or "miss".
static const uint kMaxCandidates = 4096u;
void guard_count(uint k) { rtStats.InterlockedAdd((k * 64u) * 4u, 1u); }
// QA (rt_max_candidates, Push::rt_max_cand): a lower cap, to force aborts in game and
// check every pass's fallback. 0 (the default) means kMaxCandidates; never above it.
uint rt_max_candidates() { return pc.rt_max_cand != 0u ? min(pc.rt_max_cand, kMaxCandidates) : kMaxCandidates; }

// Tri-state visibility of a shadow / occlusion ray (rt_visibility). kRtUnknown: the
// query hit the candidate cap before it found an occluder; the caller must use the
// game's own visibility for that pixel and light, or count it as occluded.
static const uint kRtLit = 0u, kRtOccluded = 1u, kRtUnknown = 2u;

// Which pass is tracing (a pass sets g_rt_pass once at the top of its main), for the
// per-pass count of unknown (aborted) queries: rtStats counter kStatUnknown + pass,
// always counted like the guards and logged by core/gi.cpp in its "[core] guard:" lines.
static const uint kRtPassOther = 0u, kRtPassSun = 1u, kRtPassLocal = 2u, kRtPassRefl = 3u, kRtPassAO = 4u,
                  kRtPassGI = 5u;
static const uint kStatUnknown = 128u;   // counters 128..133 (core/gi.cpp kStatsCounters)
static uint g_rt_pass = kRtPassOther;

// A ray query must get a finite origin, a finite non-zero direction and
// 0 <= TMin <= TMax, none NaN (VUID-RuntimeSpirv-OpRayQueryInitializeKHR-06348..06352):
// anything else is undefined, and on NVIDIA traversal then reads at garbage
// addresses (the D-LS faults: rays built from game light data, a NaN or Inf
// position or source radius gives a NaN/zero direction or a NaN TMax, which
// "tmax <= 0" does not catch). Such a ray is not traced (a miss / not occluded) and
// counts in guard counter 120. TMax is clamped to a finite 1e30.
bool rt_ray_valid(float3 o, float3 d, inout float tmax) {
  const bool ok = all(isfinite(o)) && all(isfinite(d)) && dot(d, d) > 1e-20 && tmax >= 0.0;   // false for NaN
  if (!ok) {
    guard_count(kGuardRay);
    return false;
  }
  tmax = min(tmax, 1e30);
  return true;
}

void stat_add(uint index, bool cond, uint bin) {
  uint n = WaveActiveCountBits(cond);
  uint b = WaveReadLaneFirst(bin) & 63u;
  if (WaveIsFirstLane() && n) rtStats.InterlockedAdd((index * 64 + b) * 4, n);
}

// One query ended by the candidate cap with its answer unknown (wave-reduced: under a
// forced low cap whole waves abort at once).
void rt_count_unknown(uint bin) { stat_add(kStatUnknown + min(g_rt_pass, kRtPassGI), true, bin); }

// Per-draw records: 16-byte header (uint count), then 208 bytes per draw,
// sorted by first_tri (layer/host.h AlphaRec).
[[vk::binding(0, 1)]] ByteAddressBuffer gAlphaRec;
// Geometry 1's vertices, 24 bytes each: camera-relative position (float4), UV.
[[vk::binding(1, 1)]] ByteAddressBuffer gAlphaSoup;
// Sizes: layer/alphatest.cpp kTextures / kCaches.
[[vk::binding(2, 1)]] Texture2D<float4> gAlphaTex[4096];       // indirection (vt) or the alpha texture (tex)
[[vk::binding(3, 1)]] Texture2DArray<float4> gAlphaCache[16];  // virtual texture tile caches
[[vk::binding(4, 1)]] SamplerState gAlphaWrap;
[[vk::binding(5, 1)]] SamplerState gAlphaClamp;
// Geometry 0's vertices (the opaque soup), same layout; the vertex stride is in
// gAlphaRec's header (uint 1). For the geometric normal of a hit (rt_origin=1).
[[vk::binding(6, 1)]] ByteAddressBuffer gOpaqueSoup;
// Per pixel (written by gi/rt_origin.hlsl): xyz the geometric normal facing the
// camera, w = t_hit / |P| - 1 of the TLAS surface under the pixel, or kOriginNoHit.
[[vk::binding(40, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> rtOrigin;
static const float kOriginNoHit = 1000.0;
#ifdef RT_HAIR_PHYSICAL_ORIGIN
#include <../common/native_hair_origin.h>
// Separate physical launch data. rtOrigin remains the native virtual surface
// used by primary checks and history correspondence. Selected by ENTRY, not PC.
[[vk::binding(50, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> rtHairOrigin;
#endif

// The scene stamp of this set's build (layer/alphatest.cpp): uint 0 the generation
// the host expects, uint 1 the one the build's command buffer wrote when it ran.
// Different: that build never ran before this pass (its command buffer dropped
// without a submit, or submitted later): its TLAS is not traced (every ray misses).
#ifdef RT_OMM_SCENE
[[vk::binding(9, 1)]] globallycoherent RWByteAddressBuffer gSceneStamp;
#else
[[vk::binding(9, 1)]] ByteAddressBuffer gSceneStamp;
#endif
#ifdef RT_OMM_SCENE
#include "omm_scene_guard.hlsli"
#endif
#ifdef RT_SCENE_REBASE
// No runtime variant defines RT_SCENE_REBASE since rt_scene_rebase was removed (2026-10-31,
// docs/CONSOLIDATION.md C1); the blocks stay for tools/native_hair_gpu_probe's rebase cases.
// The native camera is already bound at 29 in D-LS/D-RF and at 48 in
// the other RT layouts. Descriptor aliases have the same 14-float4 layout.
struct RtRebaseCameraCB { float4 c[14]; };
#ifdef RT_REBASE_LIGHT_CAMERA
[[vk::binding(29, 0)]] ConstantBuffer<RtRebaseCameraCB> rtRebaseCamera;
#else
[[vk::binding(48, 0)]] ConstantBuffer<RtRebaseCameraCB> rtRebaseCamera;
#endif
float3 rt_scene_translation() {
  // Stamp bytes 256..479 snapshot the build's native camera on the GPU.
  return rtRebaseCamera.c[13].xyz - asfloat(gSceneStamp.Load3(256u + 208u));
}
float3 rt_scene_ray_origin(float3 lighting_origin) { return lighting_origin + rt_scene_translation(); }
#endif

// rt_cascade_casters (docs/TRACK_D_RT.md 6.7 item 2): the TLAS gains one instance of the
// sun cascade's casters the camera did not draw (mask 0x02, instanceCustomIndex
// kRtCascadeId). The cascade variants (gi/cascade*/, RT_CASCADE_MASKS) trace 0x01 so that
// only the sun (RT_CASCADE_SUN: rt_shadow.hlsl's rt_visibility) sees it. Ordinary builds
// trace 0xFF, the literal they always had: their SPIR-V is unchanged.
#ifdef RT_CASCADE_MASKS
#define RT_HIT_MASK 0x01
#ifdef RT_CASCADE_SUN
#define RT_VIS_MASK 0xFF
#elif defined(RT_CASCADE_SUN_HIT)
// gi_hit_sun (rt/native_hit_sun.hlsli NATIVE_HIT_SUN_GI): only the sun ray at a GI hit sees the
// cascade's casters (it sets g_rt_cascade_ray around its rt_visibility); every other ray of
// the pass traces 0x01 as in the other cascade variants.
static bool g_rt_cascade_ray = false;
#define RT_VIS_MASK (g_rt_cascade_ray ? 0xFFu : 0x01u)
#else
#define RT_VIS_MASK 0x01
#endif
#else
#define RT_HIT_MASK 0xFF
#define RT_VIS_MASK 0xFF
#endif
#ifdef RT_SUN_CURRENT
#undef RT_VIS_MASK
#define RT_VIS_MASK (2u<<gSunLayer)
#endif
#if defined(RT_CASCADE_SUN) || (defined(RT_CASCADE_SUN_HIT) && defined(RT_CASCADE_MASKS))
static const uint kRtCascadeId = 0xFFFFFFu;   // common/cascade_casters.h kCustomIndex
// The cascade soup holds the casters again where the camera drew them too, at the shadow
// LOD: its hits nearer than this to the ray origin are ignored (no self-shadowing by a
// coarser copy of the shaded surface). A copy offset by e <= 1 cm is met at e / sin(a)
// <= 0.5 m for sun elevations above the surface a >= 1.2 deg; a roof is >= 2 m (one
// storey) above the floor it shades, so 0.5 m keeps every roof with a 4x margin.
#ifndef RT_CASCADE_TMIN
#define RT_CASCADE_TMIN 0.5
#endif
// The game's cascade draws coarser LODs far from the camera: their copy of a distant surface
// can sit metres off the camera pass's one and shadow it (user, 2026-10-29: distant shadows
// that vanish as the camera approaches). LOD swaps keep the screen error roughly constant in
// pixels, so the world-space offset grows with distance: cascade hits nearer than this
// fraction of the ray origin's camera distance are ignored (2%: 0.6 m at 30 m, 2 m at 100 m).
#ifndef RT_CASCADE_TMIN_REL
#define RT_CASCADE_TMIN_REL 0.02
#endif
#endif
bool rt_scene_ok() {
  uint2 s = gSceneStamp.Load2(0);
#ifdef RT_SCENE_REBASE
  return s.x != 0u && s.x == s.y && s.x == gSceneStamp.Load(8) && all(isfinite(rt_scene_translation()));
#else
  return s.x != 0u && s.x == s.y;
#endif
}

#ifdef RT_SUN_CURRENT
#include "../../common/native_sun_math.h"
[[vk::binding(14,1)]] ByteAddressBuffer gSunParameters;
static uint gSunLayer=0u;
float4 sun_caster_row(uint layer,uint axis) {return asfloat(gSunParameters.Load4(992u+48u*layer+16u*axis));}
float4 sun_domain_row(uint layer,uint axis) {
  uint offset=64u+64u*layer+4u*axis;
  return asfloat(uint4(gSunParameters.Load(offset),gSunParameters.Load(offset+16u),
    gSunParameters.Load(offset+32u),gSunParameters.Load(offset+48u)));
}
float3 sun_domain_position(uint layer,float3 p) {
  return float3(dot(sun_domain_row(layer,0u),float4(p,1)),dot(sun_domain_row(layer,1u),float4(p,1)),
                dot(sun_domain_row(layer,2u),float4(p,1)));
}
float3 sun_domain_direction(uint layer,float3 d) {
  return float3(dot(sun_domain_row(layer,0u).xyz,d),dot(sun_domain_row(layer,1u).xyz,d),dot(sun_domain_row(layer,2u).xyz,d));
}
static const float kSunGamma128=7.62945274e-6; // gamma128: normalization/basis plus affine projection operation budget

// A private build can be withheld when native producer generations/submit order
// do not match. Never touch its unbuilt TLAS in that case. Byte12 belongs to
// native Hair; byte16 is the current cascade's conversion-invalid counter.
bool sun_current_scene_ready() {
  return rt_scene_ok() && gSceneStamp.Load(20u)==gSceneStamp.Load(0u) &&
         gSceneStamp.Load(12u)==0u && gSceneStamp.Load(16u)==0u &&
         (gSceneStamp.Load(24u)&31u)==31u && (gSunParameters.Load(1200u)&1u)!=0u;
}

#endif

#ifdef RT_SUN_CURRENT
[[vk::binding(12,1)]] ByteAddressBuffer gSunDraws;
[[vk::binding(13,1)]] ByteAddressBuffer gSunSoup0;
[[vk::binding(16,1)]] ByteAddressBuffer gSunSoup1;
[[vk::binding(17,1)]] ByteAddressBuffer gSunSoup2;
[[vk::binding(18,1)]] ByteAddressBuffer gSunSoup3;
uint sun_soup_bytes(uint layer) {
  uint bytes;
  if(layer==0u)gSunSoup0.GetDimensions(bytes);else if(layer==1u)gSunSoup1.GetDimensions(bytes);
  else if(layer==2u)gSunSoup2.GetDimensions(bytes);else gSunSoup3.GetDimensions(bytes);
  return bytes;
}
uint3 sun_load3(uint layer,uint offset) {
  if(layer==0u)return gSunSoup0.Load3(offset);if(layer==1u)return gSunSoup1.Load3(offset);
  if(layer==2u)return gSunSoup2.Load3(offset);return gSunSoup3.Load3(offset);
}
uint2 sun_load2(uint layer,uint offset) {
  if(layer==0u)return gSunSoup0.Load2(offset);if(layer==1u)return gSunSoup1.Load2(offset);
  if(layer==2u)return gSunSoup2.Load2(offset);return gSunSoup3.Load2(offset);
}
static bool gSunAlphaKnown=true;
#endif
static const uint kRecBytes = 208;

uint rec_addr(uint i) { return 16 + i * kRecBytes; }

// Bounds (defence in depth; the device need not have robust buffer access, and
// an out-of-range descriptor read can fault). A TLAS is traced with its own
// build's set 1 (rtscene.cpp scene slots), so these should never trigger; they
// turn a mismatch into a miss or an "unknown" alpha instead of a wild read.
// Record at byte address `a` lies inside gAlphaRec (overflow-safe).
bool rec_in_range(uint a) {
  uint rbytes;
  gAlphaRec.GetDimensions(rbytes);
  return a >= 16u && rbytes >= kRecBytes && a <= rbytes - kRecBytes;
}
// Triangle `prim` (3 vertices of `stride` bytes from byte `base`) lies inside a
// buffer of `bytes` bytes (overflow-safe).
bool tri_in_range(uint base, uint prim, uint stride, uint bytes) {
  return stride != 0u && base <= bytes && prim < (bytes - base) / (3u * stride);
}

// Tile-set constant: matrix m (0/1), column c, row r of the SSBO entry (row-major mat4).
float vt_t(uint a, uint m, uint c, uint r) { return asfloat(gAlphaRec.Load(a + 80 + 4 * (m * 16 + r * 4 + c))); }

// ---- alpha-test policy (docs/TRACK_D_RT.md 7.1, "Alpha test at secondary hits") ----
// gAlphaRec header uint 2, written per frame by layer/alphatest.cpp from bg3rtx.ini
// (live A/B switches; every bit set is the default):
//   kAlphaCutEff      rt_alpha_cut=1: the record's effective cut-off (the strictest
//                     constant of the shader's discard tests on the alpha), not the
//                     first test's (an early-out at 0.001 / 0.01 on 76 vt shaders);
//   kAlphaStochastic  rt_alpha_stochastic=1: where the texel footprint has blurred the
//                     mask (level >= 1 above mip 0), compare against a hashed threshold
//                     instead of the hard cut-off: P(solid) -> the filtered alpha, i.e.
//                     the coverage, so partial coverage stays unbiased under RR;
//   kAlphaUnknownClear rt_alpha_unknown=1: a candidate whose alpha cannot be evaluated
//                     (no record or UV, a non-resident tile set) lets visibility rays
//                     (GI, AO, reflections) through; shadow rays still treat it as solid.
static const uint kAlphaCutEff = 1u, kAlphaStochastic = 2u, kAlphaUnknownClear = 4u;
uint alpha_flags() { return gAlphaRec.Load(8); }

// Class of the running ray query, for the counters and the unknown-case policy:
// 0 visibility (closest hit: GI, AO, reflections, origins, debug), 1 shadow (any hit).
static uint g_rt_class = 0u;

// Alpha-test counters (rt_stats=1, core/gi.cpp): 80 + 16 x class + k, k:
//   0 candidates; solid without evaluation: 1 no record, 2 no UV (stride < 24),
//   3 prim >= tris, 4 non-finite uv/footprint, 5 vt tile set not resident;
//   6/7 vt solid/clear, 8/9 tex solid/clear; 10 records whose first cut-off is < 0.1,
//   11 of those solid; 12 level >= 2, 13 level >= 4 (blur level of the alpha read,
//   log2 mip-0 texels); 14 stochastic (level > 1); 15 vt page from a coarser level
//   than requested (not resident at the cone's mip).
void alpha_stat(uint k, bool cond, uint bin) {
  if (pc.rt_stats != 0u) stat_add(80u + 16u * g_rt_class + k, cond, bin);
}

// A candidate whose alpha is unknown: solid (true) or let through.
bool alpha_unknown(uint flags) { return (flags & kAlphaUnknownClear) == 0u || g_rt_class == 1u; }

// Uniform [0, 1) per ray and hit: the hit's uv bits, its triangle and the frame.
float alpha_hash(float2 uv, uint prim) {
  uint3 h = pcg3d(uint3(asuint(uv.x), asuint(uv.y), prim ^ (pc.frame * 0x9E3779B9u) ^ (g_rt_class << 31)));
  return float(h.x >> 8) * (1.0 / 16777216.0);
}

// An alpha read: the value, the blur level of the data it came from (log2 of mip-0
// texels per texel: the mip, or the level of the coarser page that stood in), and
// whether it could be read at all.
#ifdef RT_NATIVE_TILESET
bool tileset_scene_complete(){uint bytes;gAlphaRec.GetDimensions(bytes);return bytes>=16u && (gAlphaRec.Load(NtcHeaderControlOffset)&NtcCaptureIncompleteBit)==0u;}
#endif
struct AlphaRead { float a; float lvl; bool ok; bool coarser; };

// BG3's Granite lookup, as in its prepass fragment shaders (e.g. 84d5df19):
// mip from the uv footprint, page entry from the indirection texture, texel in
// the tile cache; the tile set's albedo layer `col` carries the alpha. Without
// screen derivatives the footprint is the ray cone's, isotropic. The indirection
// entry of a page that is not resident at that mip points at the finest resident
// ancestor (its tile scale, e & 15, is then smaller): the read clamps itself to
// the finest resident level, and `lvl` reports the level actually used.
AlphaRead vt_alpha(uint a, float2 uv, float g, uint tex, uint cache, uint col) {
  AlphaRead r = (AlphaRead)0;
  r.a = 1.0;
  r.lvl = 0.0;
  r.coarser = false;
  r.ok = vt_t(a, 0, 2, 3) == 2202.0;   // the game's "tile set resident" sentinel
  if (!r.ok) return r;
  float4 P0 = asfloat(gAlphaRec.Load4(a + 48));
  float4 P1 = asfloat(gAlphaRec.Load4(a + 64));
  float2 u = frac(uv) * P0.zw + P0.xy;
  float2 s = float2(vt_t(a, 0, 2, 0), vt_t(a, 0, 3, 0));
  float2 dx = P0.zw * float2(g, 0.0) * s, dy = P0.zw * float2(0.0, g) * s;
  float d1 = dot(dx, dx), d2 = dot(dy, dy);
  float l = log2(max(max(d1, d2), 1e-30));
  float aniso = min(0.5 * l - 0.5 * log2(max(min(d1, d2), 1e-30)), vt_t(a, 0, 1, 0));
  float mip = floor(min(max(0.5 * l - aniso - 0.5, 0.0), P1.x) + 0.5);
  float2 dims = float2(vt_t(a, 0, 0, 3), vt_t(a, 0, 0, 3) * vt_t(a, 0, 1, 3));
  float2 ip = floor(u * dims * pow(0.5, mip));
  // Stay inside the indirection texture (constants from another tile set, or a
  // mip it does not have, would fetch out of bounds).
  uint iw, ih, ilevels;
  gAlphaTex[NonUniformResourceIndex(tex)].GetDimensions(0, iw, ih, ilevels);
  mip = min(mip, float(max(ilevels, 1u) - 1u));
  int2 ipi = clamp(int2(ip), int2(0, 0), int2(max(iw >> uint(mip), 1u) - 1u, max(ih >> uint(mip), 1u) - 1u));
  uint e = asuint(gAlphaTex[NonUniformResourceIndex(tex)].Load(int3(ipi, int(mip))).x);
  float sc = pow(2.0, float(e & 15u));
  // Pages across the tile set at the level the entry points to vs at mip 0.
  float used = max(log2(max(dims.x, 1.0)) - float(e & 15u), 0.0);
  r.lvl = max(used, mip);
  r.coarser = used > mip + 0.5;
  float2 s163 = float2(sc, sc * vt_t(a, 0, 1, 3));
  float2 s168 = float2(vt_t(a, 0, 0, 2), vt_t(a, 0, 1, 2));
  float2 s175 = float2(vt_t(a, 1, 0, 0), vt_t(a, 1, 1, 0));
  float2 c = (frac(u * s163) * s168 + float2(float((e >> 14) & 1023u), float((e >> 4) & 1023u))) * s175 +
             float2(vt_t(a, 1, 2, col), vt_t(a, 1, 3, col));
  r.a = gAlphaCache[NonUniformResourceIndex(cache)].SampleLevel(gAlphaClamp, float3(c, float((e >> 24) & 127u)), 0).w;
  return r;
}

AlphaRead tex_alpha(uint a, float2 uv, float g, uint tex, uint ch) {
  float2 sc = asfloat(gAlphaRec.Load2(a + 32));
  uint w, h, levels;
  gAlphaTex[NonUniformResourceIndex(tex)].GetDimensions(0, w, h, levels);
  float lod = clamp(log2(max(g * max(abs(sc.x), abs(sc.y)) * float(max(w, h)), 1e-8)), 0.0, float(levels) - 1.0);
  float4 v = gAlphaTex[NonUniformResourceIndex(tex)].SampleLevel(gAlphaWrap, uv * sc, lod);
  AlphaRead r = (AlphaRead)0;
  r.a = ch == 0 ? v.x : ch == 1 ? v.y : ch == 2 ? v.z : v.w;
  r.lvl = lod;
  r.ok = true;
  r.coarser = false;
  return r;
}

#ifdef RT_NATIVE_SCALP
static bool gNativeCameraQuery=false;
// Native CHAR_Scalp. Sample B and A TOGETHER; the root transform
// is part of coverage, not a replacement cutoff on raw A. Sampling follows our
// declared ray-cone LOD policy, not original raster derivatives.
AlphaRead native_scalp_alpha(uint a, float2 uv, float g, uint tex,float3 position) {
  uint w, h, levels;
  gAlphaTex[NonUniformResourceIndex(tex)].GetDimensions(0, w, h, levels);
  float lod = clamp(log2(max(g * float(max(w, h)), 1e-8)), 0.0, float(levels) - 1.0);
  float4 mask = gAlphaTex[NonUniformResourceIndex(tex)].SampleLevel(gAlphaWrap, uv, lod);
  float2 material = asfloat(gAlphaRec.Load2(a + 48)); // AlphaRec p0.xy
  float hi = clamp(material.x + 0.7, 0.0, 1.0);
  AlphaRead r = (AlphaRead)0;
  r.lvl = lod;
  r.ok = all(isfinite(mask.zw)) && all(isfinite(material)) && material.x < hi;
  if (r.ok) r.a = smoothstep(material.x, hi, mask.w) * mad(mask.z - 1.0, material.y, 1.0);
  if(r.ok && gNativeCameraQuery && asfloat(gAlphaRec.Load(a+56u))!=0.0) {
    float4x4 vp=float4x4(asfloat(gAlphaRec.Load4(a+96u)),asfloat(gAlphaRec.Load4(a+112u)),
                         asfloat(gAlphaRec.Load4(a+128u)),asfloat(gAlphaRec.Load4(a+144u)));
    float4 clip=mul(float4(position,1),vp),viewport=asfloat(gAlphaRec.Load4(a+160u));
    float2 frame=asfloat(gAlphaRec.Load2(a+32u));
    if(!all(isfinite(clip))||clip.w==0||!all(isfinite(viewport))||!all(isfinite(frame))) {r.ok=false;return r;}
    float2 raster=floor(viewport.xy+(float2(clip.x,-clip.y)/clip.w+1.0)*viewport.zw*0.5)+0.5;
    float phase=frame.x-4.0*trunc(frame.x/4.0);
    float2 z=float2(frac(((mad(raster.y,2.0,raster.x)-1.5)+phase)*0.2),
                    frac(mad(raster.x,2.4084,raster.y*3.2535)));
    float noise=frac(dot(float2(0.833,0.1666),z));
    r.a=mad(smoothstep(material.x,hi,mask.w),mad(mask.z-1.0,material.y,1.0),noise-0.6667);
  }
  r.ok = r.ok && isfinite(r.a);
  return r;
}
#endif

#ifdef RT_NATIVE_HAIR
struct NativeHairData { float3 n, t, b, p; float4 fade; float3 ray; bool valid, physical; };
static NativeHairData gNativeHair;
float hair_param(uint a,uint i) { return asfloat(gAlphaRec.Load(a+48u+4u*i)); }
float4 hair_sample(uint tex,float2 uv,float g) {
  uint w,h,levels;
  gAlphaTex[NonUniformResourceIndex(tex)].GetDimensions(0,w,h,levels);
  float lod=clamp(log2(max(g*float(max(w,h)),1e-8)),0.0,float(levels)-1.0);
  return gAlphaTex[NonUniformResourceIndex(tex)].SampleLevel(gAlphaWrap,uv,lod);
}
struct HairMaterialSample {float4 mask;float height;bool ok;};
HairMaterialSample hair_material(uint a,float2 uv,float g,uint mask_tex,uint noise_tex,bool evaluate_depth) {
  HairMaterialSample r=(HairMaterialSample)0;
  float ns=hair_param(a,2), shift=hair_param(a,3);
  float2 nuv=uv*ns;
  if(!all(isfinite(nuv)))return r;
  float4 noise=hair_sample(noise_tex,nuv,g*abs(ns));
  float2 duv=(mad(noise.x,2.0,-1.0)*shift).xx;
  if(!all(isfinite(duv)) || !all(isfinite(uv+duv)))return r;
  float4 ca=hair_sample(mask_tex,uv+duv,g), cb=hair_sample(mask_tex,uv,g);
  if(!all(isfinite(noise)) || !all(isfinite(ca)) || !all(isfinite(cb)))return r;
  float4 center=float4(0.5,0,0,0), x=ca-center, y=cb-center;
  r.mask=mad(max(sign(x),sign(y)),max(abs(x),abs(y)),center);
  bool camera=gAlphaRec.Load(a+20u)!=0u;
  if(camera && evaluate_depth && (hair_param(a,4)!=0.0 || hair_param(a,5)!=0.0)) {
    float width=saturate(hair_param(a,7));
    float blend=smoothstep(mad(-width,0.5,hair_param(a,6)),mad(width,0.5,hair_param(a,6)),r.mask.z);
    r.height=max((1.0-r.mask.y)*lerp(hair_param(a,4)*0.01,hair_param(a,5)*0.01,blend),0.0);
  }
  r.ok=all(isfinite(r.mask)) && isfinite(r.height);return r;
}
AlphaRead native_hair_alpha(uint a,float2 uv,float g,uint mask_tex,uint noise_tex) {
  AlphaRead r=(AlphaRead)0;
  if(!gNativeHair.valid)return r;
  bool physical=g_rt_class==1u || gNativeHair.physical;
  HairMaterialSample sample=hair_material(a,uv,g,mask_tex,noise_tex,!physical);
  if(!sample.ok)return r;
  float4 mask=sample.mask;float thick=sample.height;
  bool camera=gAlphaRec.Load(a+20u)!=0u;
  // Match native camera material for closest/origin queries. Shadow rays use
  // their light-view direction and native shadow hard branch, not camera fade.
  float3 view=physical ? -gNativeHair.ray : -(gNativeHair.p-normalize(gNativeHair.p)*thick);
  float3 v=normalize(view), nn=normalize(gNativeHair.n);
  float3 local=normalize(float3(dot(v,gNativeHair.b),dot(v,gNativeHair.n),dot(v,gNativeHair.t)));
  float3 world=normalize(gNativeHair.b*local.x+gNativeHair.n*local.y+gNativeHair.t*local.z);
  float angular=saturate(abs(dot(world,nn))*5.0);
  float cov=(mask.w*mad(-mask.z,hair_param(a,8),1.0))*angular;
  float density=hair_param(a,9);
  float2 frame=asfloat(gAlphaRec.Load2(a+32u));
  if(camera && !physical && frame.y!=0.0) {
    float4x4 vp=float4x4(asfloat(gAlphaRec.Load4(a+96u)),asfloat(gAlphaRec.Load4(a+112u)),
                         asfloat(gAlphaRec.Load4(a+128u)),asfloat(gAlphaRec.Load4(a+144u)));
    float4 clip=mul(float4(gNativeHair.p,1),vp);
    float4 viewport=asfloat(gAlphaRec.Load4(a+160u));
    if(!all(isfinite(clip)) || clip.w==0.0)return r;
    float2 raster=viewport.xy+(float2(clip.x,-clip.y)/clip.w+1.0)*viewport.zw*0.5;
    // BG3's single-sample native raster coverage domain, including its original
    // viewport offset/negative height; never substitute current RT output pixel.
    raster=floor(raster)+0.5;
    float phase=frame.x-4.0*trunc(frame.x/4.0);
    float2 z=float2(frac(((mad(raster.y,2.0,raster.x)-1.5)+phase)*0.2),
                    frac(mad(raster.x,2.4084,raster.y*3.2535)));
    float dither=frac(dot(float2(0.833,0.1666),z)+gNativeHair.fade.x);
    // Pubes has no density scalar: its native FMA combines base*angular,
    // unlike Hair's already-rounded coverage times the separate density.
    if(gAlphaRec.Load(a+196u)==1u)
      r.a=mad(mask.w*mad(-mask.z,hair_param(a,8),1.0),angular,dither-0.6667);
    else r.a=mad(cov,density,dither-0.6667);
  } else r.a=cov*density;
  r.ok=isfinite(r.a) && isfinite(thick) && all(isfinite(world));
  return r;
}
#endif

// ---- per-instance data (Track D BVH split, layer/bvhsplit.cpp) ----
// One 16-byte InstInfo per TLAS instance (set 1 binding 7, indexed by the
// instance index):
//   base  byte offset of the instance's first vertex: in the mesh pool (binding 8,
//         object space) or in the soup (binding 6, camera-relative world);
//   meta  bits 0-7 vertex stride (16, or 24 with the UV at +16); kInfoPool: the
//         vertices are in the pool (apply the instance's object-to-world);
//         kInfoAlpha: `rec` is the instance's alpha record; kInfoLegacy: the
//         single soup BLAS of rt_bvh_split=0 (geometry 0 opaque, 1 alpha-tested,
//         records found by triangle index);
//   rec   byte address of the alpha record in gAlphaRec (0: none);
//   tris  triangles of the instance's BLAS (bounds).
[[vk::binding(7, 1)]] ByteAddressBuffer gInstInfo;
[[vk::binding(8, 1)]] ByteAddressBuffer gMeshPool;
static const uint kInfoPool = 1u << 8, kInfoLegacy = 1u << 9, kInfoAlpha = 1u << 10;
// kInfoEmissive: an instance of the emissive scene (D-EM, rt/em_common.hlsli: its own
// TLAS and InstInfo; `rec` is then the emissive record's index). Never set in this TLAS.
static const uint kInfoEmissive = 1u << 11;
struct InstInfo { uint base, meta, rec, tris; };

InstInfo inst_info(uint inst) {
  InstInfo r = (InstInfo)0;
  uint bytes;
  gInstInfo.GetDimensions(bytes);
  // Past the InstInfo of this TLAS: no vertices, no record (never the legacy lookup).
  const bool ok_range = inst < bytes / 16u;
  if (!ok_range) guard_count(kGuardSet1);
  uint4 v = ok_range ? gInstInfo.Load4(inst * 16u) : uint4(0, 0, 0, 0);
  r.base = v.x;
  r.meta = v.y;
  r.rec = v.z;
  r.tris = v.w;
  return r;
}

// Vertex k of triangle `prim` of an instance, in the instance's object space
// (pool) or camera-relative world (soup).
float3 inst_pos(InstInfo ii, uint prim, uint k) {
  uint stride = ii.meta & 0xffu;
  uint a = ii.base + (prim * 3u + k) * stride;
  return (ii.meta & kInfoPool) != 0u ? asfloat(gMeshPool.Load3(a)) : asfloat(gOpaqueSoup.Load3(a));
}
float2 inst_uv(InstInfo ii, uint prim, uint k) {
  uint stride = ii.meta & 0xffu;
  uint a = ii.base + (prim * 3u + k) * stride + 16u;
  return (ii.meta & kInfoPool) != 0u ? asfloat(gMeshPool.Load2(a)) : asfloat(gOpaqueSoup.Load2(a));
}

#ifdef RT_NATIVE_HAIR
float4 hair_vertex(uint base,uint meta,uint prim,uint k,uint offset,uint components,bool legacy) {
  uint at=base+(prim*3u+k)*(meta&255u)+offset;
  uint4 bits;
  if(legacy) bits=components==4u?gAlphaSoup.Load4(at):uint4(gAlphaSoup.Load3(at),0);
  else if((meta&kInfoPool)!=0u)bits=components==4u?gMeshPool.Load4(at):uint4(gMeshPool.Load3(at),0);
  else bits=components==4u?gOpaqueSoup.Load4(at):uint4(gOpaqueSoup.Load3(at),0);
  return asfloat(bits);
}
float4 hair_interpolate(uint base,uint meta,uint prim,uint offset,uint n,float2 bary,bool legacy) {
  float4 a=hair_vertex(base,meta,prim,0,offset,n,legacy),b=hair_vertex(base,meta,prim,1,offset,n,legacy),
         c=hair_vertex(base,meta,prim,2,offset,n,legacy);
  return a+(b-a)*bary.x+(c-a)*bary.y;
}
void hair_inputs(uint base,uint meta,uint prim,float2 bary,float3 ray,bool legacy,bool camera) {
  gNativeHair=(NativeHairData)0;
  if((meta&255u)<88u)return;
  gNativeHair.n=hair_interpolate(base,meta,prim,24,3,bary,legacy).xyz;
  gNativeHair.t=hair_interpolate(base,meta,prim,36,3,bary,legacy).xyz;
  gNativeHair.b=hair_interpolate(base,meta,prim,48,3,bary,legacy).xyz;
  gNativeHair.p=hair_interpolate(base,meta,prim,60,3,bary,legacy).xyz;
  if(camera)gNativeHair.fade=hair_interpolate(base,meta,prim,72,4,bary,legacy);
  gNativeHair.ray=ray;
  gNativeHair.valid=all(isfinite(gNativeHair.n)) && all(isfinite(gNativeHair.t)) && all(isfinite(gNativeHair.b)) &&
                    all(isfinite(gNativeHair.p)) && all(isfinite(gNativeHair.fade));
}
#endif

// Byte address of the record of alpha-tested triangle `prim` (legacy: geometry 1's
// triangle index; 0 if none).
uint find_rec(uint prim) {
  uint rbytes;
  gAlphaRec.GetDimensions(rbytes);
  uint n = min(gAlphaRec.Load(0), rbytes >= 16u ? (rbytes - 16u) / kRecBytes : 0u);
  if (n == 0) return 0;
  uint lo = 0, hi = n;   // last record with first_tri <= prim
  while (hi - lo > 1) {
    uint mid = (lo + hi) >> 1;
    if (gAlphaRec.Load(rec_addr(mid)) <= prim) lo = mid;
    else hi = mid;
  }
  return rec_addr(lo);
}

// The game's alpha test of record `a` at barycentrics `bary` of the triangle
// (p: world-space corners, u: their UVs). `footprint`: world-space width of the
// ray's footprint at the hit.
bool alpha_eval(uint a, float3 p0, float3 p1, float3 p2, float2 u0, float2 u1, float2 u2, float2 bary, float footprint,
                uint prim) {
  const uint flags = alpha_flags();
  uint4 h0 = gAlphaRec.Load4(a);        // first_tri, tris, kind, tex
  uint4 h1 = gAlphaRec.Load4(a + 16);   // cache, channel, cut (effective), cut of the first test
  float2 uv = u0 + (u1 - u0) * bary.x + (u2 - u0) * bary.y;
  float2 e1 = u1 - u0, e2 = u2 - u0;
  float area_uv = abs(e1.x * e2.y - e1.y * e2.x);
  float area_w = length(cross(p1 - p0, p2 - p0));
  float g = footprint * sqrt(area_uv / max(area_w, 1e-12));   // uv change across the footprint
  if (!all(isfinite(uv)) || !isfinite(g)) {
#ifdef RT_NATIVE_TILESET
    if(h0.z==5u)gTilesetUnknown=true;
#endif
    alpha_stat(4, true, prim);
#ifdef RT_SUN_CURRENT
    gSunAlphaKnown=false;
#endif
    return alpha_unknown(flags);
  }
  uint tex = min(h0.w, 4095u), cache = min(h1.x, 15u);
#ifdef RT_NATIVE_TILESET
  if(h0.z==5u){
    uint status=uint(asfloat(gAlphaRec.Load(a+52u)));
    float subtype=asfloat(gAlphaRec.Load(a+56u));
    if(subtype==1.f){
      float4 m0=asfloat(gAlphaRec.Load4(a+80u)),m1=asfloat(gAlphaRec.Load4(a+96u));
      bool sampled=false;float baseAlpha=0,maskR=0,maskG=0;
      float2 scale=m0.xy*m0.z;
      float2 scaled=(uv*m0.xy)*m0.z;
      float2 gradient=g*abs(scale);
      if(status==NtcKnown && h0.w>0u && h0.w<4096u && h1.y>0u && h1.y<4096u && h1.x>0u && h1.x<16u && all(isfinite(scaled)) && all(isfinite(gradient))){
        baseAlpha=gAlphaTex[NonUniformResourceIndex(h0.w)].SampleGrad(gNativeTilesetSampler[NonUniformResourceIndex(h1.x)],scaled,float2(gradient.x,0),float2(0,gradient.y)).a;
        float2 mask=gAlphaTex[NonUniformResourceIndex(h1.y)].SampleGrad(gNativeTilesetSampler[NonUniformResourceIndex(h1.x)],uv,float2(g,0),float2(0,g)).rg;
        maskR=mask.r;maskG=mask.g;sampled=true;
      }
      NtcResult result=ntb_evaluate(status,sampled,baseAlpha,maskR,maskG,m0.w,m1.x,m1.y,m1.z);
      if(result.status!=NtcKnown){gTilesetUnknown=true;return false;}
      alpha_stat(8,result.solid,prim);alpha_stat(9,!result.solid,prim);return result.solid;
    }
    if(subtype!=0.f){gTilesetUnknown=true;return false;}
    float tiling=asfloat(gAlphaRec.Load(a+32u));
    bool sampled=false;float alpha=0.0;
    if(status==NtcKnown && gTilesetColorKnown && isfinite(gTilesetGreen) && gTilesetGreen>0.0){
      if(h0.w>0u && h0.w<4096u && h1.x>0u && h1.x<16u && isfinite(tiling)){
        float2 scaled=uv*tiling;float gradient=g*abs(tiling);
        if(all(isfinite(scaled)) && isfinite(gradient)){
          alpha=gAlphaTex[NonUniformResourceIndex(h0.w)].SampleGrad(gNativeTilesetSampler[NonUniformResourceIndex(h1.x)],scaled,float2(gradient,0),float2(0,gradient)).a;
          sampled=true;
        }
      }
    }
    NtcResult result=ntc_evaluate(status,gTilesetColorKnown,gTilesetGreen,sampled,alpha);
    if(result.status!=NtcKnown){gTilesetUnknown=true;return false;}
    alpha_stat(8,result.solid,prim);alpha_stat(9,!result.solid,prim);return result.solid;
  }
#endif
  const bool vt = h0.z == 1;
  AlphaRead r = (AlphaRead)0;
  if (vt) r = vt_alpha(a, uv, g, tex, cache, min(h1.y, 3u));
#ifdef RT_NATIVE_SCALP
  else if (h0.z == 3u) r = native_scalp_alpha(a, uv, g, tex,p0+(p1-p0)*bary.x+(p2-p0)*bary.y);
#endif
#ifdef RT_NATIVE_HAIR
  else if (h0.z == 4u) r = native_hair_alpha(a,uv,g,tex,min(h1.x,4095u));
#endif
  else r = tex_alpha(a, uv, g, tex, h1.y);
  if (!r.ok) {
    alpha_stat(5, true, prim);
#ifdef RT_SUN_CURRENT
    gSunAlphaKnown=false;
#endif
    return alpha_unknown(flags);
  }
  const float cut_first = asfloat(h1.w);
  const float cut = (flags & kAlphaCutEff) != 0u ? asfloat(h1.z) : cut_first;
  // Hard cut-off where the read is as sharp as the game's (levels 0-1); towards
  // a hashed threshold over the next level, where it is a blurred coverage.
#ifdef RT_NATIVE_SCALP
  const float k = h0.z == 3u || h0.z == 4u ? 0.0 : ((flags & kAlphaStochastic) != 0u ? saturate(r.lvl - 1.0) : 0.0);
#else
  const float k = (flags & kAlphaStochastic) != 0u ? saturate(r.lvl - 1.0) : 0.0;
#endif
  const float thr = k > 0.0 ? lerp(cut, alpha_hash(uv, prim), k) : cut;
  const bool solid = r.a >= thr;
  if (pc.rt_stats != 0u) {
    alpha_stat(6, vt && solid, prim);
    alpha_stat(7, vt && !solid, prim);
    alpha_stat(8, !vt && solid, prim);
    alpha_stat(9, !vt && !solid, prim);
    alpha_stat(10, cut_first < 0.1, prim);
    alpha_stat(11, cut_first < 0.1 && solid, prim);
    alpha_stat(12, r.lvl >= 2.0, prim);
    alpha_stat(13, r.lvl >= 4.0, prim);
    alpha_stat(14, k > 0.0, prim);
    alpha_stat(15, r.coarser, prim);
  }
  return solid;
}

#ifdef RT_SUN_CURRENT
// UNKNOWN is a reference fallback, never an opaque interpretation of a native
// shader whose scalar/texture coverage was not captured correctly.
uint sun_caster_visibility(uint layer,uint prim,float2 bary,float footprint,float3x4 o2w,float3 ray) {
  uint bytes;gSunDraws.GetDimensions(bytes);
  if(bytes<64u || layer>=4u)return kRtUnknown;
  uint soup_bytes=sun_soup_bytes(layer);
  uint4 header=gSunDraws.Load4(layer*16u);
  uint base=header.x,n=header.y,stride=header.z,total=header.w;
  if(base<64u || base>bytes || n==0u || n>(bytes-base)/16u || prim>=total)return kRtUnknown;
  uint lo=0u,hi=n;
  while(hi-lo>1u) {uint mid=lo+(hi-lo)/2u;if(gSunDraws.Load(base+16u*mid)<=prim)lo=mid;else hi=mid;}
  uint4 record=gSunDraws.Load4(base+16u*lo);
  if(prim<record.x || prim-record.x>=record.y || stride<16u || !tri_in_range(0u,prim,stride,soup_bytes))return kRtUnknown;
  uint offset=prim*3u*stride;
  float3 raw0=asfloat(sun_load3(layer,offset)),raw1=asfloat(sun_load3(layer,offset+stride)),
         raw2=asfloat(sun_load3(layer,offset+2u*stride));
  float3 p0=mul(o2w,float4(raw0,1)),p1=mul(o2w,float4(raw1,1)),p2=mul(o2w,float4(raw2,1));
  float3 hit=raw0+(raw1-raw0)*bary.x+(raw2-raw0)*bary.y;
  float3 q=float3(dot(sun_caster_row(layer,0u),float4(hit,1)),dot(sun_caster_row(layer,1u),float4(hit,1)),
    dot(sun_caster_row(layer,2u),float4(hit,1)));
  const float offset_error=asfloat(gSunParameters.Load(1184u+4u*layer));
  if(!all(isfinite(q)) || !isfinite(offset_error))return kRtUnknown;
  [unroll]for(uint axis=0u;axis<3u;++axis) {
    float4 row=sun_caster_row(layer,axis);
    float error=kSunGamma128*dot(abs(row),float4(abs(raw0)+abs(raw1-raw0)*abs(bary.x)+abs(raw2-raw0)*abs(bary.y),1))+offset_error;
    if(q[axis]<-error || q[axis]>1.0+error)return kRtLit;
    if(!(q[axis]>error && q[axis]<1.0-error))return kRtUnknown;
  }
  if(record.w>1u)return kRtUnknown;
  if(record.w==0u)return kRtOccluded;
  if(stride<24u || !rec_in_range(record.z))return kRtUnknown;
  float2 u0=asfloat(sun_load2(layer,offset+16u)),u1=asfloat(sun_load2(layer,offset+stride+16u)),
         u2=asfloat(sun_load2(layer,offset+2u*stride+16u));
  uint kind=gAlphaRec.Load(record.z+8u);
#ifndef RT_NATIVE_SCALP
  if(kind==3u || kind==4u)return kRtUnknown;
#endif
#ifndef RT_NATIVE_HAIR
  if(kind==4u)return kRtUnknown;
#else
  if(kind==4u) {
    if(stride<88u)return kRtUnknown;
    gNativeHair=(NativeHairData)0;
    float3 weights=float3(1.0-bary.x-bary.y,bary);
    [unroll]for(uint i=0u;i<3u;++i) {
      uint v=offset+i*stride;
      gNativeHair.n+=asfloat(sun_load3(layer,v+24u))*weights[i];
      gNativeHair.t+=asfloat(sun_load3(layer,v+36u))*weights[i];
      gNativeHair.b+=asfloat(sun_load3(layer,v+48u))*weights[i];
      gNativeHair.p+=asfloat(sun_load3(layer,v+60u))*weights[i];
    }
    gNativeHair.n=mul((float3x3)o2w,gNativeHair.n);
    gNativeHair.t=mul((float3x3)o2w,gNativeHair.t);gNativeHair.b=mul((float3x3)o2w,gNativeHair.b);
    gNativeHair.ray=ray;gNativeHair.physical=true;
    gNativeHair.valid=all(isfinite(gNativeHair.n)) && all(isfinite(gNativeHair.t)) &&
      all(isfinite(gNativeHair.b)) && all(isfinite(gNativeHair.p));
  }
#endif
#ifdef RT_NATIVE_TILESET
  if(kind==5u){
    gTilesetColorKnown=stride>=40u && asfloat(gAlphaRec.Load(record.z+56u))==0.f;
    if(gTilesetColorKnown){float c0=asfloat(sun_load2(layer,offset+28u).x),c1=asfloat(sun_load2(layer,offset+stride+28u).x),c2=asfloat(sun_load2(layer,offset+2u*stride+28u).x);
      gTilesetGreen=c0+(c1-c0)*bary.x+(c2-c0)*bary.y;}
  }
  if(kind<1u || kind>5u)return kRtUnknown;
#else
  if(kind<1u || kind>4u)return kRtUnknown;
#endif
  gSunAlphaKnown=true;
  bool solid=alpha_eval(record.z,p0,p1,p2,u0,u1,u2,bary,footprint,prim);
#ifdef RT_NATIVE_TILESET
  if(gTilesetUnknown)return kRtUnknown;
#endif
  return !gSunAlphaKnown?kRtUnknown:solid?kRtOccluded:kRtLit;
}
#endif

// Legacy: true if the alpha-tested triangle `prim` of geometry 1 is solid at `bary`.
bool alpha_pass(uint prim, float2 bary, float footprint) {
  alpha_stat(0, true, prim);
  uint a = find_rec(prim);
  if (a == 0) {
    alpha_stat(1, true, prim);
    return alpha_unknown(alpha_flags());
  }
  uint4 h0 = gAlphaRec.Load4(a);
  if (prim < h0.x || prim - h0.x >= h0.y) {
#ifdef RT_NATIVE_TILESET
    if(h0.z==5u)gTilesetUnknown=true;
#endif
    alpha_stat(1, true, prim);
    return alpha_unknown(alpha_flags());
  }
#if defined(RT_NATIVE_HAIR) || defined(RT_NATIVE_TILESET)
  uint stride=gAlphaRec.Load(4),vb=prim*3u*stride,sbytes;
#else
  uint vb = prim * 72, sbytes;
#endif
  gAlphaSoup.GetDimensions(sbytes);
#if defined(RT_NATIVE_HAIR) || defined(RT_NATIVE_TILESET)
  if (!tri_in_range(0,prim,stride,sbytes)) {
#else
  if (vb + 72u > sbytes) {
#endif
    alpha_stat(3, true, prim);
#ifdef RT_NATIVE_TILESET
    if(h0.z==5u)gTilesetUnknown=true;
#endif
    return alpha_unknown(alpha_flags());
  }
#if defined(RT_NATIVE_HAIR) || defined(RT_NATIVE_TILESET)
  float3 p0=asfloat(gAlphaSoup.Load3(vb)),p1=asfloat(gAlphaSoup.Load3(vb+stride)),p2=asfloat(gAlphaSoup.Load3(vb+2u*stride));
  float2 u0=asfloat(gAlphaSoup.Load2(vb+16)),u1=asfloat(gAlphaSoup.Load2(vb+stride+16)),u2=asfloat(gAlphaSoup.Load2(vb+2u*stride+16));
  #ifdef RT_NATIVE_HAIR
  if(h0.z==4u) {
    float3 ray=gNativeHair.ray;
    hair_inputs(0,stride,prim,bary,ray,true,gAlphaRec.Load(a+20u)!=0u);
  }
  #endif
#else
  float3 p0 = asfloat(gAlphaSoup.Load3(vb)), p1 = asfloat(gAlphaSoup.Load3(vb + 24)), p2 = asfloat(gAlphaSoup.Load3(vb + 48));
  float2 u0 = asfloat(gAlphaSoup.Load2(vb + 16)), u1 = asfloat(gAlphaSoup.Load2(vb + 40)), u2 = asfloat(gAlphaSoup.Load2(vb + 64));
#endif
#ifdef RT_NATIVE_TILESET
  gTilesetColorKnown=h0.z==5u && stride>=40u && asfloat(gAlphaRec.Load(a+56u))==0.f;
  if(gTilesetColorKnown){float c0=asfloat(gAlphaSoup.Load(vb+28u)),c1=asfloat(gAlphaSoup.Load(vb+stride+28u)),c2=asfloat(gAlphaSoup.Load(vb+2u*stride+28u));
    gTilesetGreen=c0+(c1-c0)*bary.x+(c2-c0)*bary.y;}
#endif
  return alpha_eval(a, p0, p1, p2, u0, u1, u2, bary, footprint, prim);
}

// A non-opaque candidate of any instance: solid at `bary`?
bool alpha_pass_hit(uint inst, uint geom, uint prim, float2 bary, float footprint, float3x4 o2w
#ifdef RT_NATIVE_HAIR
                    ,float3 hair_ray
#endif
                    ) {
#ifdef RT_NATIVE_HAIR
  gNativeHair.ray=hair_ray;
#endif
  InstInfo ii = inst_info(inst);
  if ((ii.meta & kInfoLegacy) != 0u) return geom == 1u ? alpha_pass(prim, bary, footprint) : true;
  alpha_stat(0, true, prim);
  if ((ii.meta & kInfoAlpha) == 0u || ii.rec == 0u || prim >= ii.tris || (ii.meta & 0xffu) < 24u) {
#ifdef RT_NATIVE_TILESET
    if(ii.rec && rec_in_range(ii.rec) && gAlphaRec.Load(ii.rec+8u)==5u)gTilesetUnknown=true;
#endif
    if (pc.rt_stats != 0u) {
      const bool norec = (ii.meta & kInfoAlpha) == 0u || ii.rec == 0u;
      alpha_stat(1, norec, prim);
      alpha_stat(2, !norec && (ii.meta & 0xffu) < 24u, prim);
      alpha_stat(3, !norec && (ii.meta & 0xffu) >= 24u, prim);
    }
    return alpha_unknown(alpha_flags());
  }
  // Never read past the vertex buffer or the records: no robust buffer access is guaranteed.
  uint vbytes;
  if ((ii.meta & kInfoPool) != 0u) gMeshPool.GetDimensions(vbytes);
  else gOpaqueSoup.GetDimensions(vbytes);
  if (!tri_in_range(ii.base, prim, ii.meta & 0xffu, vbytes) || !rec_in_range(ii.rec)) {
#ifdef RT_NATIVE_TILESET
    if(rec_in_range(ii.rec) && gAlphaRec.Load(ii.rec+8u)==5u)gTilesetUnknown=true;
#endif
    guard_count(kGuardSet1);
    alpha_stat(3, true, prim);
    return alpha_unknown(alpha_flags());
  }
  float3 p0 = inst_pos(ii, prim, 0), p1 = inst_pos(ii, prim, 1), p2 = inst_pos(ii, prim, 2);
  if ((ii.meta & kInfoPool) != 0u) {
    p0 = mul(o2w, float4(p0, 1.0));
    p1 = mul(o2w, float4(p1, 1.0));
    p2 = mul(o2w, float4(p2, 1.0));
  }
#ifdef RT_NATIVE_HAIR
  if(gAlphaRec.Load(ii.rec+8u)==4u)
    hair_inputs(ii.base,ii.meta,prim,bary,hair_ray,false,gAlphaRec.Load(ii.rec+20u)!=0u);
#endif
#ifdef RT_NATIVE_TILESET
  gTilesetColorKnown=gAlphaRec.Load(ii.rec+8u)==5u && (ii.meta&255u)>=40u && asfloat(gAlphaRec.Load(ii.rec+56u))==0.f;
  if(gTilesetColorKnown){uint stride=ii.meta&255u,at=ii.base+prim*3u*stride+28u;
    float c0,c1,c2;
    if((ii.meta&kInfoPool)!=0u){c0=asfloat(gMeshPool.Load(at));c1=asfloat(gMeshPool.Load(at+stride));c2=asfloat(gMeshPool.Load(at+2u*stride));}
    else{c0=asfloat(gOpaqueSoup.Load(at));c1=asfloat(gOpaqueSoup.Load(at+stride));c2=asfloat(gOpaqueSoup.Load(at+2u*stride));}
    gTilesetGreen=c0+(c1-c0)*bary.x+(c2-c0)*bary.y;}
#endif
  return alpha_eval(ii.rec, p0, p1, p2, inst_uv(ii, prim, 0), inst_uv(ii, prim, 1), inst_uv(ii, prim, 2), bary, footprint,
                    prim);
}

// Debug: the family of the draw behind a hit (1 vt, 2 tex, 0 not alpha-tested).
uint alpha_kind(uint prim) {
  uint a = find_rec(prim);
  return a ? gAlphaRec.Load(a + 8) : 0;
}
uint alpha_kind_hit(uint inst, uint geom, uint prim) {
  InstInfo ii = inst_info(inst);
  if ((ii.meta & kInfoLegacy) != 0u) return geom == 1u ? alpha_kind(prim) : 0u;
  return (ii.meta & kInfoAlpha) != 0u && rec_in_range(ii.rec) ? gAlphaRec.Load(ii.rec + 8) : 0u;
}
bool alpha_instance(uint inst, uint geom) {
  InstInfo ii = inst_info(inst);
  return (ii.meta & kInfoLegacy) != 0u ? geom == 1u : (ii.meta & kInfoAlpha) != 0u;
}

// The candidate test every ray query loop runs.
#ifdef RT_NATIVE_HAIR
#define RT_ALPHA_CANDIDATE(q, cone)                                                                         \
  alpha_pass_hit(q.CandidateInstanceIndex(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(),        \
                 q.CandidateTriangleBarycentrics(), cone.f0 + cone.spread * q.CandidateTriangleRayT(),       \
                 q.CandidateObjectToWorld3x4(),q.WorldRayDirection())
#else
#define RT_ALPHA_CANDIDATE(q, cone)                                                                             \
  alpha_pass_hit(q.CandidateInstanceIndex(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(),        \
                 q.CandidateTriangleBarycentrics(), cone.f0 + cone.spread * q.CandidateTriangleRayT(),       \
                 q.CandidateObjectToWorld3x4())
#endif

// Ray footprint: width f0 at the origin, growing by `spread` per unit length.
struct RtCone { float f0, spread; };
#ifdef RT_NATIVE_HAIR
#include "native_hair_procedural.hlsli"
#define RT_SKIP_PROCEDURAL 0u
#define RT_HAS_COMMITTED(q) (q.CommittedStatus()!=COMMITTED_NOTHING)
bool hair_bounds_ok(){return gSceneStamp.Load(12u)==0u;}
#else
#define RT_SKIP_PROCEDURAL RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES
#define RT_HAS_COMMITTED(q) (q.CommittedStatus()==COMMITTED_TRIANGLE_HIT)
#endif

#ifdef RT_OCC_STATS
// rt_occ_stats (common/occ_stats.h): the occluder of the last rt_visibility / rt_closest_hit query
// of this lane. Shadow rays then find the CLOSEST occluder (no accept-first: same visibility,
// more traversal), so the kind and the near test describe the nearest blocker.
#include "../../common/occ_stats.h"
static uint g_occ_kind = kOccKindUnknown;
static float g_occ_t = -1.0;   // -1: no committed hit
static bool g_occ_hit = false;
static float3 g_occ_p = float3(0, 0, 0);   // the hit point (the ray's origin + t direction)
uint occ_kind_of(bool procedural, uint inst, uint id, uint geom, uint prim) {
  if (procedural) return kOccKindHair;
  if (id >= 0xFFFFFCu) return kOccKindCascade;   // common/cascade_casters.h kCustomIndex - layer
  InstInfo ii = inst_info(inst);
  if ((ii.meta & kOccInfoHeld) != 0u && (ii.meta & kInfoLegacy) == 0u) return kOccKindHeld;
  const bool alpha = (ii.meta & kInfoLegacy) != 0u ? geom == 1u : (ii.meta & kInfoAlpha) != 0u;
  if (alpha) {
    const uint k = alpha_kind_hit(inst, geom, prim);
    return k == 3u ? kOccKindScalp : k == 4u ? kOccKindHair
         : (ii.meta & kInfoPool) != 0u ? kOccKindCutoutStatic : kOccKindCutoutMoving;
  }
  return (ii.meta & kInfoPool) != 0u ? kOccKindStatic : kOccKindMoving;
}
#define RT_OCC_RECORD(q, aborted)                                                                              \
  g_occ_hit = RT_HAS_COMMITTED(q);                                                                             \
  g_occ_t = g_occ_hit ? q.CommittedRayT() : -1.0;                                                                g_occ_p = q.WorldRayOrigin() + q.WorldRayDirection() * max(g_occ_t, 0.0);                                    \
  g_occ_kind = g_occ_hit ? occ_kind_of(q.CommittedStatus() != COMMITTED_TRIANGLE_HIT, q.CommittedInstanceIndex(), \
                                       q.CommittedInstanceID(), q.CommittedGeometryIndex(),                    \
                                       q.CommittedPrimitiveIndex())                                            \
                         : kOccKindUnknown;
#define RT_OCC_RESET() g_occ_hit = false; g_occ_t = -1.0; g_occ_kind = kOccKindUnknown;
#define RT_VIS_FLAGS RT_SKIP_PROCEDURAL
#else
#define RT_VIS_FLAGS (RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RT_SKIP_PROCEDURAL)
#endif

// Visibility along [0, tmax] (shadow and occlusion rays): kRtOccluded if an
// alpha-tested hit was committed, kRtLit if traversal finished without one, kRtUnknown
// if the candidate cap aborted the query first (a hit committed before the abort is a
// real occluder: kRtOccluded). Invalid rays and an unbuilt scene stay kRtLit as before
// (rt_ray_valid / rt_scene_ok, counted guards).
uint rt_visibility(float3 o, float3 d, float tmax, RtCone cone) {
#ifdef RT_OCC_STATS
  RT_OCC_RESET()
#endif
#ifdef RT_NATIVE_TILESET
  gTilesetUnknown=!tileset_scene_complete();
#endif
#ifdef RT_NATIVE_SCALP
  gNativeCameraQuery=false;
#endif
  g_rt_class = 1u;
#ifdef RT_SUN_CURRENT
  if(g_rt_pass==kRtPassSun && (!sun_current_scene_ready() || !rt_ray_valid(o,d,tmax))) {
    rt_count_unknown(0u);return kRtUnknown;
  }
#endif
#ifdef RT_NATIVE_HAIR
  gHairCameraQuery=false;
  if(!hair_bounds_ok()){rt_count_unknown(asuint(o.x)^asuint(o.z));return kRtUnknown;}
#endif
#ifdef RT_NATIVE_TILESET
  if(gTilesetUnknown){rt_count_unknown(asuint(o.x)^asuint(o.z));return kRtUnknown;}
#endif
  if (!rt_scene_ok() || !rt_ray_valid(o, d, tmax)) return kRtLit;
  RayQuery<RT_VIS_FLAGS> q;
  RayDesc r = (RayDesc)0;
  r.Origin = o;
#ifdef RT_SCENE_REBASE
#ifdef RT_SUN_CURRENT
  // Native current-cascade instances already map caster W into lighting P.
  // Camera-mask primary queries still need the baseline scene translation.
  if(g_rt_pass!=kRtPassSun)
#endif
  r.Origin = rt_scene_ray_origin(o);
#endif
  r.Direction = d;
  r.TMin = 0.0;
  r.TMax = tmax;
#ifdef RT_OMM_SCENE
  if (rt_omm_scene_current())
  q.TraceRayInline(gScene, 0, RT_VIS_MASK, r);
  else q.TraceRayInline(gOmmReferenceScene, 0, RT_VIS_MASK, r);
#else
  q.TraceRayInline(gScene, 0, RT_VIS_MASK, r);
#endif
  const uint cap = rt_max_candidates();
  uint cand_n = 0;
  bool aborted = false;
  while (q.Proceed()) {
#ifdef RT_NATIVE_HAIR
    if(q.CandidateType()==CANDIDATE_PROCEDURAL_PRIMITIVE && hair_proven_inactive(q.CandidateInstanceIndex(),q.CandidatePrimitiveIndex()))continue;
#endif
    if (++cand_n > cap) {
      guard_count(kGuardLoopCap);
      aborted = true;
      q.Abort();
      break;
    }
#ifdef RT_NATIVE_HAIR
    if(q.CandidateType()==CANDIDATE_PROCEDURAL_PRIMITIVE){
      HairProceduralHit ph=hair_intersect(q.CandidateInstanceIndex(),q.CandidatePrimitiveIndex(),r.Origin,d,tmax,cone);
      if(ph.unknown){aborted=true;q.Abort();break;}
      if(ph.t>=0.0)q.CommitProceduralPrimitiveHit(ph.t);
      continue;
    }
#endif
#if defined(RT_CASCADE_SUN) || (defined(RT_CASCADE_SUN_HIT) && defined(RT_CASCADE_MASKS))
    // The cascade instance is not opaque only for this test (no alpha: its triangles are solid).
    if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
#ifdef RT_SUN_CURRENT
        q.CandidateInstanceID()<=kRtCascadeId && q.CandidateInstanceID()>=kRtCascadeId-3u
#else
        q.CandidateInstanceID()==kRtCascadeId
#endif
        ) {
#ifdef RT_SUN_CURRENT
      uint state=sun_caster_visibility(kRtCascadeId-q.CandidateInstanceID(),q.CandidatePrimitiveIndex(),q.CandidateTriangleBarycentrics(),
        cone.f0+cone.spread*q.CandidateTriangleRayT(),q.CandidateObjectToWorld3x4(),d);
      if(state==kRtUnknown) {aborted=true;q.Abort();break;}
      if(state==kRtOccluded)q.CommitNonOpaqueTriangleHit();
#else
      if (q.CandidateTriangleRayT() > max(RT_CASCADE_TMIN, RT_CASCADE_TMIN_REL * length(o))) q.CommitNonOpaqueTriangleHit();
#endif
      continue;
    }
#endif
    if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && RT_ALPHA_CANDIDATE(q, cone)) q.CommitNonOpaqueTriangleHit();
  }
#ifdef RT_NATIVE_TILESET
  if(gTilesetUnknown){rt_count_unknown(asuint(o.x)^asuint(o.z));return kRtUnknown;}
#endif
#ifdef RT_OCC_STATS
  RT_OCC_RECORD(q, aborted)
#endif
  if (RT_HAS_COMMITTED(q)) return kRtOccluded;
  if (aborted) {
    rt_count_unknown(asuint(o.x) ^ asuint(o.z));
    return kRtUnknown;
  }
  return kRtLit;
}

// Any alpha-tested hit within [0, tmax]. An unknown (aborted) query counts as occluded,
// never as a light leak; callers that have the game's own visibility use rt_visibility.
bool rt_occluded(float3 o, float3 d, float tmax, RtCone cone) { return rt_visibility(o, d, tmax, cone) != kRtLit; }

#ifdef RT_SUN_CURRENT
struct NativeSunResult {float visibility;bool unknown;uint samples;uint reason;};
bool sun_origin_constant(float3 normal,float3 L,float3 t,float3 b,float radius) {
  if(all(normal==0.0))return true;
  float center=dot(normal,L),width=radius*(abs(dot(normal,t))+abs(dot(normal,b)));
  float error=kSunGamma128*dot(abs(normal),abs(L)+radius*(abs(t)+abs(b)));
  return center>width+error || center<-(width+error);
}
NativeSunResult sun_native_visibility(float3 P,float depth,float3 origin,float3 direction,float3 L,float3 t,float3 b,
                                     float radius,RtCone cone,float3 shading_normal,float3 geometric_normal) {
  NativeSunResult result;result.visibility=1;result.unknown=true;result.samples=0;result.reason=1;
  if(!sun_current_scene_ready())return result;
  NativeSunAux aux=ns_aux(gSunParameters.Load(548u),asfloat(gSunParameters.Load(884u)),depth,asfloat(gSunParameters.Load(556u)));
  if(aux.valid==0u) {result.reason=2;return result;}
  float3 q[4];
  [unroll]for(uint layer=0u;layer<4u;++layer) {
    uint offset=64u+64u*layer;
    float4x4 receiver_matrix=float4x4(asfloat(gSunParameters.Load4(offset)),asfloat(gSunParameters.Load4(offset+16u)),
      asfloat(gSunParameters.Load4(offset+32u)),asfloat(gSunParameters.Load4(offset+48u)));
    q[layer]=mul(float4(P,1),receiver_matrix).xyz;
  }
  NativeSunSelection selection=ns_select(q,asfloat(gSunParameters.Load(24u)),asfloat(gSunParameters.Load4(32u)),
    asfloat(gSunParameters.Load4(48u)),gSunParameters.Load(1204u),true,true,false);
  if(selection.valid==0u)return result;
  if(aux.active!=0u) {
    float4x4 aux4=float4x4(asfloat(gSunParameters.Load4(560u)),asfloat(gSunParameters.Load4(576u)),
      asfloat(gSunParameters.Load4(592u)),asfloat(gSunParameters.Load4(608u)));
    float4x4 aux5=float4x4(asfloat(gSunParameters.Load4(624u)),asfloat(gSunParameters.Load4(640u)),
      asfloat(gSunParameters.Load4(656u)),asfloat(gSunParameters.Load4(672u)));
    float csm3=ns_weight(q[3],asfloat(gSunParameters.Load(24u)),asfloat(gSunParameters.Load(44u)),asfloat(gSunParameters.Load(60u)));
    aux=ns_aux_domain(aux,mul(float4(P,1),aux4).xyz,mul(float4(P,1),aux5).xyz,csm3);
    if(aux.valid==0u || aux.active!=0u) {result.reason=2;return result;}
  }
  if(selection.selected==4u) {result.unknown=false;result.reason=0;return result;}
  float r=max(tan(radius),0.0);
  if(!isfinite(r) || !sun_origin_constant(shading_normal,L,t,b,r) ||
    (pc.rt_origin!=0u && !sun_origin_constant(geometric_normal,L,t,b,r))) {result.reason=3;return result;}
  uint layers[2]={selection.selected,selection.next};
  float weights[2]={selection.selectedWeight,selection.nextWeight};
  float maximum[2]={0,0};
  // Certify the ENTIRE angular disc before sampling. A direction-dependent
  // native-mask fallback would bias the expected penumbra/energy.
  [unroll]for(uint item=0u;item<2u;++item) if(layers[item]<4u && weights[item]>0) {
    uint layer=layers[item];float3 o=sun_domain_position(layer,origin),center=sun_domain_direction(layer,L);
    float3 width=r*(abs(sun_domain_direction(layer,t))+abs(sun_domain_direction(layer,b)))+.5*r*r*abs(center);
    float3 oe,de;
    [unroll]for(uint axis=0u;axis<3u;++axis) {
      float4 row=sun_domain_row(layer,axis);
      oe[axis]=kSunGamma128*dot(abs(row),float4(abs(origin),1))+asfloat(gSunParameters.Load(1184u+4u*layer));
      de[axis]=kSunGamma128*(dot(abs(row.xyz),abs(L)+r*(abs(t)+abs(b)))+width[axis]);
    }
    NativeSunFront front=ns_cone_front(ns_cone_bounds(o,center,width,oe,de),true);
    if(front.valid==0u) {result.reason=3;return result;}
    maximum[item]=front.maxDistance;
  }
  result.visibility=selection.whiteWeight;
  [unroll]for(uint item=0u;item<2u;++item) if(layers[item]<4u && weights[item]>0) {
    gSunLayer=layers[item];++result.samples;
    uint visibility=rt_visibility(origin,direction,maximum[item],cone);
    if(visibility==kRtUnknown) {result.reason=4;return result;}
    result.visibility+=weights[item]*(visibility==kRtLit?1.0:0.0);
  }
  result.unknown=false;result.reason=0;return result;
}
#endif

// Debug (rt_debug=3/5): the closest hit, whether it is alpha-tested geometry,
// and the family of the nearest alpha-tested candidate along the ray, whether
// it was let through or not.
// aborted: the candidate cap ended the query; t, alpha and hit_kind are then unreliable.
struct RtDebugHit { float t; bool alpha; uint near_kind; uint hit_kind; bool aborted; };
RtDebugHit rt_closest_debug(float3 o, float3 d, float tmax, RtCone cone) {
#ifdef RT_NATIVE_TILESET
  gTilesetUnknown=!tileset_scene_complete();
#endif
#ifdef RT_NATIVE_SCALP
  gNativeCameraQuery=true;
#endif
  g_rt_class = 0u;
  RtDebugHit h = (RtDebugHit)0;
  h.near_kind = 0;
  h.aborted = false;
#ifdef RT_NATIVE_TILESET
  if(gTilesetUnknown){h.t=-1;h.aborted=true;rt_count_unknown(asuint(o.x)^asuint(o.z));return h;}
#endif
#ifdef RT_NATIVE_HAIR
  gHairCameraQuery=true;
  if(!hair_bounds_ok()){h.t=-1;h.aborted=true;rt_count_unknown(asuint(o.x)^asuint(o.z));return h;}
#endif
  if (!rt_scene_ok() || !rt_ray_valid(o, d, tmax)) {
    h.t = -1.0;
    h.alpha = false;
    h.hit_kind = 0u;
    return h;
  }
  RayQuery<RT_SKIP_PROCEDURAL> q;
  RayDesc r = (RayDesc)0;
  r.Origin = o;
#ifdef RT_SCENE_REBASE
  r.Origin = rt_scene_ray_origin(o);
#endif
  r.Direction = d;
  r.TMin = 0.0;
  r.TMax = tmax;
#ifdef RT_OMM_SCENE
  if (rt_omm_scene_current())
  q.TraceRayInline(gScene, 0, RT_HIT_MASK, r);
  else q.TraceRayInline(gOmmReferenceScene, 0, RT_HIT_MASK, r);
#else
  q.TraceRayInline(gScene, 0, RT_HIT_MASK, r);
#endif
  float near_t = 1e30;
  const uint cap = rt_max_candidates();
  uint cand_n = 0;
  while (q.Proceed()) {
#ifdef RT_NATIVE_HAIR
    if(q.CandidateType()==CANDIDATE_PROCEDURAL_PRIMITIVE && hair_proven_inactive(q.CandidateInstanceIndex(),q.CandidatePrimitiveIndex()))continue;
#endif
    if (++cand_n > cap) {
      guard_count(kGuardLoopCap);
      h.aborted = true;
      q.Abort();
      break;
    }
#ifdef RT_NATIVE_HAIR
    if(q.CandidateType()==CANDIDATE_PROCEDURAL_PRIMITIVE){
      float maximum=RT_HAS_COMMITTED(q)?min(tmax,q.CommittedRayT()):tmax;
      HairProceduralHit ph=hair_intersect(q.CandidateInstanceIndex(),q.CandidatePrimitiveIndex(),r.Origin,d,maximum,cone);
      if(ph.unknown){h.aborted=true;q.Abort();break;}
      if(ph.t>=0.0){if(ph.t<near_t){near_t=ph.t;h.near_kind=4;}q.CommitProceduralPrimitiveHit(ph.t);}
      continue;
    }
#endif
    if (q.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
    float ct = q.CandidateTriangleRayT();
    if (ct < near_t) {
      near_t = ct;
      h.near_kind = alpha_kind_hit(q.CandidateInstanceIndex(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex());
    }
    if (RT_ALPHA_CANDIDATE(q, cone)) q.CommitNonOpaqueTriangleHit();
  }
#ifdef RT_NATIVE_TILESET
  h.aborted=h.aborted||gTilesetUnknown;
#endif
  bool hit = RT_HAS_COMMITTED(q);
  h.t = hit ? q.CommittedRayT() : -1.0;
  h.alpha = hit && alpha_instance(q.CommittedInstanceIndex(), q.CommittedGeometryIndex());
  h.hit_kind = h.alpha ? alpha_kind_hit(q.CommittedInstanceIndex(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex())
                       : 0u;
  if (h.aborted) rt_count_unknown(asuint(o.x) ^ asuint(o.z));
  return h;
}

// Closest hit (alpha-tested): distance (-1 on a miss), the triangle and its instance.
// aborted: the candidate cap ended the query, so the closest hit is UNKNOWN: t may be -1
// (nothing committed yet) or a hit farther than the true closest one, and geom / prim /
// inst / o2w are that hit's. Treat it like a hidden / off-screen hit (the game's own
// term for the pixel), never as a miss or as the closest surface.
struct RtHit { float t; uint geom; uint prim; uint inst; float3x4 o2w; bool aborted;
#ifdef RT_HIT_MATERIAL
  float2 bary;
#ifdef RT_HIT_INSTANCED
  float3 dir;   // the ray direction (rt_hit_material_instanced: nv_two_sided_sign at the hit)
#endif
#endif
};
RtHit rt_closest_hit(float3 o, float3 d, float tmax, RtCone cone
#if defined(RT_NATIVE_HAIR) || defined(RT_NATIVE_SCALP)
                     ,bool camera_primary=false
#endif
                     ) {
#ifdef RT_OCC_STATS
  RT_OCC_RESET()
#endif
#ifdef RT_NATIVE_TILESET
  gTilesetUnknown=!tileset_scene_complete();
#endif
#ifdef RT_NATIVE_SCALP
  gNativeCameraQuery=camera_primary;
#endif
  g_rt_class = 0u;
#ifdef RT_NATIVE_HAIR
  gHairCameraQuery=camera_primary;
  if(!hair_bounds_ok()){RtHit bad=(RtHit)0;bad.t=-1;bad.aborted=true;rt_count_unknown(asuint(o.x)^asuint(o.z));return bad;}
#endif
#ifdef RT_NATIVE_TILESET
  if(gTilesetUnknown){RtHit bad=(RtHit)0;bad.t=-1;bad.aborted=true;rt_count_unknown(asuint(o.x)^asuint(o.z));return bad;}
#endif
  if (!rt_scene_ok() || !rt_ray_valid(o, d, tmax)) {
    RtHit m = (RtHit)0;
    m.t = -1.0;
    m.geom = m.prim = m.inst = 0u;
    m.o2w = float3x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0);
    m.aborted = false;
    return m;
  }
  RayQuery<RT_SKIP_PROCEDURAL> q;
  RayDesc r = (RayDesc)0;
  r.Origin = o;
#ifdef RT_SCENE_REBASE
  r.Origin = rt_scene_ray_origin(o);
#endif
  r.Direction = d;
  r.TMin = 0.0;
  r.TMax = tmax;
#ifdef RT_OMM_SCENE
  if (rt_omm_scene_current())
  q.TraceRayInline(gScene, 0, RT_HIT_MASK, r);
  else q.TraceRayInline(gOmmReferenceScene, 0, RT_HIT_MASK, r);
#else
  q.TraceRayInline(gScene, 0, RT_HIT_MASK, r);
#endif
  const uint cap = rt_max_candidates();
  uint cand_n = 0;
  bool aborted = false;
  while (q.Proceed()) {
#ifdef RT_NATIVE_HAIR
    if(q.CandidateType()==CANDIDATE_PROCEDURAL_PRIMITIVE && hair_proven_inactive(q.CandidateInstanceIndex(),q.CandidatePrimitiveIndex()))continue;
#endif
    if (++cand_n > cap) {
      guard_count(kGuardLoopCap);
      aborted = true;
      q.Abort();
      break;
    }
#ifdef RT_NATIVE_HAIR
    if(q.CandidateType()==CANDIDATE_PROCEDURAL_PRIMITIVE){
      float maximum=RT_HAS_COMMITTED(q)?min(tmax,q.CommittedRayT()):tmax;
      HairProceduralHit ph=hair_intersect(q.CandidateInstanceIndex(),q.CandidatePrimitiveIndex(),r.Origin,d,maximum,cone);
      if(ph.unknown){aborted=true;q.Abort();break;}
      if(ph.t>=0.0)q.CommitProceduralPrimitiveHit(ph.t);
      continue;
    }
#endif
    if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && RT_ALPHA_CANDIDATE(q, cone)) q.CommitNonOpaqueTriangleHit();
  }
#ifdef RT_NATIVE_TILESET
  aborted=aborted||gTilesetUnknown;
#endif
  if (aborted) rt_count_unknown(asuint(o.x) ^ asuint(o.z));
#ifdef RT_OCC_STATS
  RT_OCC_RECORD(q, aborted)
#endif
  RtHit h = (RtHit)0;
  h.aborted = aborted;
  bool hit = RT_HAS_COMMITTED(q);
  h.t = hit ? q.CommittedRayT() : -1.0;
  h.geom = hit ? q.CommittedGeometryIndex() : 0u;
  h.prim = hit ? q.CommittedPrimitiveIndex() : 0u;
  h.inst = hit ? q.CommittedInstanceIndex() : 0u;
  h.o2w = hit ? q.CommittedObjectToWorld3x4() : float3x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0);
#ifdef RT_HIT_MATERIAL
  [branch]if(q.CommittedStatus()==COMMITTED_TRIANGLE_HIT)h.bary=q.CommittedTriangleBarycentrics();
#ifdef RT_HIT_INSTANCED
  h.dir=d;
#endif
#endif
#ifdef RT_SCENE_REBASE
  // Ray t is translation-invariant. Return transforms in lighting space so
  // callers reconstruct lighting-relative hit positions as before.
  if (hit) {
    float3 delta = rt_scene_translation();
    h.o2w[0][3] -= delta.x;
    h.o2w[1][3] -= delta.y;
    h.o2w[2][3] -= delta.z;
  }
#endif
  return h;
}

// Legacy: unnormalised geometric normal of a triangle of the soup BLAS (the soups
// are camera-relative, the lighting space).
float3 rt_tri_normal(uint geom, uint prim) {
  float3 p0, p1, p2;
  // Never read past the soups' descriptors (a hit is always inside them; this
  // only guards against a scene and a set from different builds).
  uint bytes;
  if (geom == 0u) {
    uint stride = gAlphaRec.Load(4);
    gOpaqueSoup.GetDimensions(bytes);
#ifdef RT_NATIVE_HAIR
    if (stride == 0u || stride > 128u || prim >= bytes / (3u * stride)) return 0.0.xxx;
#else
    if (stride == 0u || stride > 64u || prim >= bytes / (3u * stride)) return 0.0.xxx;
#endif
    uint vb = prim * 3u * stride;
    p0 = asfloat(gOpaqueSoup.Load3(vb));
    p1 = asfloat(gOpaqueSoup.Load3(vb + stride));
    p2 = asfloat(gOpaqueSoup.Load3(vb + 2u * stride));
  } else {
    gAlphaSoup.GetDimensions(bytes);
#ifdef RT_NATIVE_HAIR
    uint stride=gAlphaRec.Load(4);
    if(!tri_in_range(0,prim,stride,bytes))return 0.0.xxx;
    uint vb=prim*3u*stride;
    p0=asfloat(gAlphaSoup.Load3(vb));p1=asfloat(gAlphaSoup.Load3(vb+stride));p2=asfloat(gAlphaSoup.Load3(vb+2u*stride));
#else
    if (prim >= bytes / 72u) return 0.0.xxx;
    uint vb = prim * 72u;
    p0 = asfloat(gAlphaSoup.Load3(vb));
    p1 = asfloat(gAlphaSoup.Load3(vb + 24u));
    p2 = asfloat(gAlphaSoup.Load3(vb + 48u));
#endif
  }
  float3 n = cross(p1 - p0, p2 - p0);
  return all(isfinite(n)) ? n : 0.0.xxx;
}

// Unnormalised geometric normal of the hit triangle in the lighting space
// (camera-relative world), for any instance.
float3 rt_hit_normal(RtHit h) {
  InstInfo ii = inst_info(h.inst);
  if ((ii.meta & kInfoLegacy) != 0u) return rt_tri_normal(h.geom, h.prim);
  uint stride = ii.meta & 0xffu, bytes;
  if ((ii.meta & kInfoPool) != 0u) gMeshPool.GetDimensions(bytes);
  else gOpaqueSoup.GetDimensions(bytes);
  if (stride < 16u || h.prim >= ii.tris) return 0.0.xxx;
  if (!tri_in_range(ii.base, h.prim, stride, bytes)) {
    guard_count(kGuardSet1);
    return 0.0.xxx;
  }
  float3 p0 = inst_pos(ii, h.prim, 0), p1 = inst_pos(ii, h.prim, 1), p2 = inst_pos(ii, h.prim, 2);
#ifdef RT_NATIVE_HAIR
  if((ii.meta&kInfoHairProcedural)!=0u){
    p0=hair_vertex(ii.base,ii.meta,h.prim,0,60,3,false).xyz;
    p1=hair_vertex(ii.base,ii.meta,h.prim,1,60,3,false).xyz;
    p2=hair_vertex(ii.base,ii.meta,h.prim,2,60,3,false).xyz;
  }
#endif
  if ((ii.meta & kInfoPool) != 0u) {
    p0 = mul(h.o2w, float4(p0, 1.0));
    p1 = mul(h.o2w, float4(p1, 1.0));
    p2 = mul(h.o2w, float4(p2, 1.0));
  }
  float3 n = cross(p1 - p0, p2 - p0);
  return all(isfinite(n)) ? n : 0.0.xxx;
}

// Wachter & Binder, "A Fast and Robust Method for Avoiding Self-Intersection"
// (Ray Tracing Gems, ch. 6): moves p off its surface along n (facing the side the
// ray leaves on) by a fixed number of float ULPs, or a fixed distance near 0.
float3 rt_offset_ray(float3 p, float3 n) {
  const float origin = 1.0 / 32.0, float_scale = 1.0 / 65536.0, int_scale = 256.0;
  int3 of_i = int3(int_scale * n);
  float3 p_i = asfloat(asint(p) + int3(p.x < 0.0 ? -of_i.x : of_i.x, p.y < 0.0 ? -of_i.y : of_i.y, p.z < 0.0 ? -of_i.z : of_i.z));
  return float3(abs(p.x) < origin ? p.x + float_scale * n.x : p_i.x,
                abs(p.y) < origin ? p.y + float_scale * n.y : p_i.y,
                abs(p.z) < origin ? p.z + float_scale * n.z : p_i.z);
}

// Origin of a secondary ray in direction `dir` from the surface at pixel fp (P its
// depth-buffer point, Ns its shading normal, `foot` the pixel's footprint there).
//   rt_origin=0: P + Ns x rt_bias x |P| (towards the ray's side of Ns), the old way;
//   rt_origin=1: the exact TLAS surface point under the pixel, pushed off along the
//     geometric normal towards the ray's side by Wachter & Binder's ULP offset.
//     Where the shading and the geometric normal disagree about the side (the
//     shadow terminator of coarse meshes) or no triangle matched the depth buffer,
//     the offset is 2 footprints, along the shading normal resp. the depth normal.
// `o`: rt_origin_at(fp), read once per pixel by passes that trace several rays.
float4 rt_origin_at(uint2 fp) { return pc.rt_origin != 0u ? rtOrigin[fp] : float4(0, 0, 0, kOriginNoHit); }
float3 rt_ray_origin_o(float4 o, float3 P, float3 Ns, float foot, float3 dir) {
  float ss = dot(Ns, dir) < 0.0 ? -1.0 : 1.0;
  if (pc.rt_origin == 0u) return P + Ns * (length(P) * pc.rt_bias * ss);
  float3 r;
  float sg = dot(o.xyz, dir) < 0.0 ? -1.0 : 1.0;
  if (sg != ss) r = P + Ns * (2.0 * foot * ss);
  else if (!(o.w < kOriginNoHit)) r = P + o.xyz * (2.0 * foot * sg);
  else r = rt_offset_ray(P * (1.0 + o.w), o.xyz * sg);
  // A non-finite ray origin is undefined behaviour for ray queries.
  return all(isfinite(r)) ? r : P + Ns * (2.0 * foot * ss);
}
#ifdef RT_HAIR_PHYSICAL_ORIGIN
bool rt_hair_receiver(uint2 fp,float3 P,out float3 receiver) {
  receiver=P;
  if(pc.rt_origin==0u)return false;
  float4 physical=rtHairOrigin[fp];
  if(physical.w<=0.0 || !all(isfinite(physical)) || !hpo_unit_normal(physical.xyz))return false;
  receiver=hpo_reconstruct(P,physical.w);return all(isfinite(receiver));
}
float3 rt_secondary_ray_origin_o(uint2 fp,float4 virtual_origin,float3 P,float3 Ns,float foot,float3 dir) {
  if(pc.rt_origin!=0u) {
    float4 physical=rtHairOrigin[fp];
    if(physical.w>0.0 && all(isfinite(physical)) && hpo_unit_normal(physical.xyz)) {
      // Direct float32 distance: error scales with physical position, not the
      // possibly much larger virtual point. No offset-ratio cancellation.
      float3 base=hpo_reconstruct(P,physical.w);
      if(all(isfinite(base)))return rt_ray_origin_o(float4(physical.xyz,0.0),base,Ns,foot,dir);
    }
  }
  return rt_ray_origin_o(virtual_origin,P,Ns,foot,dir);
}
#define RT_SECONDARY_ORIGIN(fp,o,P,N,f,d) rt_secondary_ray_origin_o(fp,o,P,N,f,d)
#else
#define RT_SECONDARY_ORIGIN(fp,o,P,N,f,d) rt_ray_origin_o(o,P,N,f,d)
#endif
float3 rt_ray_origin(uint2 fp, float3 P, float3 Ns, float foot, float3 dir) {
  return RT_SECONDARY_ORIGIN(fp,rt_origin_at(fp), P, Ns, foot, dir);
}

// Closest alpha-tested hit distance, or -1. `aborted`: the candidate cap ended the
// query; the distance is then unknown (-1, or a hit farther than the closest one).
float rt_closest(float3 o, float3 d, float tmax, RtCone cone, out bool aborted) {
#ifdef RT_NATIVE_TILESET
  gTilesetUnknown=!tileset_scene_complete();
#endif
#ifdef RT_NATIVE_SCALP
  gNativeCameraQuery=false;
#endif
  g_rt_class = 0u;
  aborted = false;
#ifdef RT_NATIVE_HAIR
  gHairCameraQuery=false;
  if(!hair_bounds_ok()){aborted=true;rt_count_unknown(asuint(o.x)^asuint(o.z));return -1.0;}
#endif
#ifdef RT_NATIVE_TILESET
  if(gTilesetUnknown){aborted=true;rt_count_unknown(asuint(o.x)^asuint(o.z));return -1.0;}
#endif
  if (!rt_scene_ok() || !rt_ray_valid(o, d, tmax)) return -1.0;
  RayQuery<RT_SKIP_PROCEDURAL> q;
  RayDesc r = (RayDesc)0;
  r.Origin = o;
#ifdef RT_SCENE_REBASE
  r.Origin = rt_scene_ray_origin(o);
#endif
  r.Direction = d;
  r.TMin = 0.0;
  r.TMax = tmax;
#ifdef RT_OMM_SCENE
  if (rt_omm_scene_current())
  q.TraceRayInline(gScene, 0, RT_HIT_MASK, r);
  else q.TraceRayInline(gOmmReferenceScene, 0, RT_HIT_MASK, r);
#else
  q.TraceRayInline(gScene, 0, RT_HIT_MASK, r);
#endif
  const uint cap = rt_max_candidates();
  uint cand_n = 0;
  while (q.Proceed()) {
#ifdef RT_NATIVE_HAIR
    if(q.CandidateType()==CANDIDATE_PROCEDURAL_PRIMITIVE && hair_proven_inactive(q.CandidateInstanceIndex(),q.CandidatePrimitiveIndex()))continue;
#endif
    if (++cand_n > cap) {
      guard_count(kGuardLoopCap);
      aborted = true;
      q.Abort();
      break;
    }
#ifdef RT_NATIVE_HAIR
    if(q.CandidateType()==CANDIDATE_PROCEDURAL_PRIMITIVE){
      float maximum=RT_HAS_COMMITTED(q)?min(tmax,q.CommittedRayT()):tmax;
      HairProceduralHit ph=hair_intersect(q.CandidateInstanceIndex(),q.CandidatePrimitiveIndex(),r.Origin,d,maximum,cone);
      if(ph.unknown){aborted=true;q.Abort();break;}
      if(ph.t>=0.0)q.CommitProceduralPrimitiveHit(ph.t);
      continue;
    }
#endif
    if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && RT_ALPHA_CANDIDATE(q, cone)) q.CommitNonOpaqueTriangleHit();
  }
#ifdef RT_NATIVE_TILESET
  aborted=aborted||gTilesetUnknown;
#endif
  if (aborted) rt_count_unknown(asuint(o.x) ^ asuint(o.z));
  return RT_HAS_COMMITTED(q) ? q.CommittedRayT() : -1.0;
}
// (For callers that cannot act on an abort: the aborted query reads as above.)
float rt_closest(float3 o, float3 d, float tmax, RtCone cone) {
  bool aborted;
  return rt_closest(o, d, tmax, cone, aborted);
}
