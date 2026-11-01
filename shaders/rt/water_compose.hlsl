// M5.3 water composition (rt_water_compose=1, docs/PLAN_M5_WATER.md M5.3, core/water.cpp).
// Replaces the forward water FS's probe reflection term with the ray-traced reflection where
// the ray resolves, exactly, with the FS's own factors (common/water_compose.h: the patched FS
// stored them per pixel, common/water_fs_patch.h):
//     C' = max(C + A L - B, 0),  A = G E,  B = G k Ks,  G = a T X
// a the FS's opacity, T its fog transmittance, X its atmosphere transmittance, k its probe-to-
// lighting scale (set 2 b0 _m0[0] * 100), E = F0 DFG.x + DFG.y its own split-sum LUT value at
// (N.V, roughness), Ks its probe term (global + local probes, times E), L the traced radiance
// in the lighting's units (a probe of radiance S lights as S k: D-EN4). Energy: the traced
// radiance takes the same env-BRDF weight E the probe had, so a ray that sees what the probe
// holds (L = S k) changes nothing, and the probe term is subtracted exactly once (no double
// count). Unresolved rays keep C.
//
// water_copy (at the lighting, after water_reflect, run_post): this frame's lit radiance
//   (gDiffuse + gSpecular, the M5.2 tier (a) source), its depth and material class, and the
//   lighting's camera constants, into images / a buffer of ours, so the composition at the end
//   of the forward water pass does not depend on the game's lighting targets still being
//   intact then. Thread (0,0) copies the camera (CameraCB, ViewportCB) into cCam: one block
//   (not per frame slot) that every frame's copy overwrites, so the compose reads the camera of
//   the lighting the GPU ran last, the same lighting as wRad / wDepOut, whatever the order the
//   CPU recorded the lighting and the forward pass in (core/water.cpp, common/water_compose.h).
// water_compose (the end of each forward water pass, core/water.cpp water_on_pass_end): per
//   pixel with a record (and the record's colour IS the target's: overlapping water fragments
//   store in any order, the attachment keeps the last), one mirror ray along reflect(-V, N)
//   with the FS's own N, from P = t * dir (t the FS's camera distance), resolved from the screen
//   as M5.2 (tier (a): the hit projects, the depth there is that surface at its geometric
//   normal, it faces the ray, not skin). Rough pixels (roughness above rf_mirror_rough: the
//   probe is a pre-filtered lobe, a mirror ray is not) and rays below the surface keep C.
//   rt_debug 25: the delta view instead of the image (below).
// Statistics (cStats words; core/water.cpp "[water] compose"): 0 compose dispatches, 1 records,
//   2 owned, 3 not owned (colour mismatch), 4 invalid, 5 rough (kept), 6 below the surface
//   (kept), 7 rays, 8..13 tiers 1..6 (as water_reflect), 14 skin rejections, 15 clamped at 0,
//   16/17 sum lum(C) over owned, 18/19 sum lum(B) over composed, 20/21 sum lum(A L) over composed,
//   22/23 sum positive lum(C' - C), 24/25 sum negative |lum(C' - C)|, 26/27 sum lum(B) over owned,
//   28 composed, 29/30/31 composed with A L / B below 0.5 / 0.5..2 / above 2; sums x 4096 (64-bit).
//   cStats is a DEVICE-LOCAL slot (core/water.cpp copies it to the host slot after the dispatch,
//   outside the timed region), and every word is reduced per 8x8 group in groupshared first: one
//   global atomic per non-zero word per group. m5w3-fg2 (11-01) had cStats in host-visible memory:
//   ~25 per-wave atomics (6 of them returning the old value for the 64-bit carry, a PCIe round
//   trip each) cost 7.2 ms at 275k records and 2.0 ms at 85k (proportional to the record waves),
//   while M5.2 traced the same rays in 0.12 ms. The group sum of a 64-bit word's quantised values
//   is exact in 32 bits (64 lanes x (1e4 x 4096 + 1) < 2^32, common/water_compose.h
//   kCsGroupSumMax), so the logged sums are the same integers as the per-wave adds.
// Key off: nothing of this runs (no ordinary shader includes this file).
//
// M5.4 water shadows (WATER_SHADOWS=1, rt_water_shadows on when the water pipelines were created:
// the records have kRecordBytesSun with the FS's colour without its sun term R = C - D and lum(D),
// common/water_compose.h). The audited water FS light the water with sun visibility 1 (clouds only,
// no cascade), so where the record has a sun term one ray towards the sun disc (rt_shadow.hlsl's
// sample, the lighting's sun_radius and rt_tmax) gives V and C' = max(V C + (1 - V) R [+ A L - B], 0):
// only the visibility changes. Lit and unknown rays keep C. rt_debug 26: the shadow view. Statistics: the same group
// reduction into the device-local slot, with the sun words 32..43 (kCsSun*, sums at 40 and 42).
//
// M5.4b water local-light shadows (WATER_LS=1 on top of WATER_SHADOWS, rt_water_ls_shadows on when the
// water pipelines were created: 96-byte records with the FS's point + spot sum L, its factor F = a T X m,
// its BRDF inputs, froxel depth, position and normal, common/water_compose.h kRecordBytesLs). The
// audited water FS shadow only the lights with a game shadow map (the atlas); every other light has
// visibility 1. Per record the pass walks the FS's own froxel list (the forward pass's set 2 b0, b10,
// b11, b12, b26, b27, b28 re-bound at 22..28: the D-LS structures of bg3_lights.hlsli, the same
// decorations), recomputes each light's term c_k with bg3_light_eval (the FS's loops: identical terms,
// tests/water_compose_test 4g) and s_k (bg3_atlas_shadow) for shadow-mapped lights, checks
// sum c_k s_k against L (the gate: kLsSumTol), and traces one ray to the centre of each light the game
// leaves unshadowed: C' = max(C [...] - F sum_k (1 - V_k) c_k, 0). Lit, unknown and shadow-mapped lights
// keep their term exactly (no ray, nothing removed): C' = C where nothing is occluded. Projector lights
// (the FS's third loop) are not replaced (their share is logged). rt_debug 27: the view. Statistics:
// words 64..90 of the 128-word slot (kCsLs*).
//
// M5.5 step 1 water refraction (WATER_REFR=1 on top of WATER_SHADOWS;WATER_LS, rt_water_refraction on when
// the water pipelines were created: 104-byte records with the FS's screen-space refraction sample Refr and
// its weight 1 - a = dC/dRefr, common/water_compose.h kRecordBytesRefr). The audited water FS see the bed
// through a screen-space sample of the pre-water colour copy (set 2 b4) at the pixel itself plus a small
// wave offset: no Snell bending, so the bed is seen where the VIEW ray meets it. Here one ray along
// refract(-V, N, 1 / n) (the FS's own N; n from the FS's own F0, water 1.326) into the scene TLAS; where
// its hit resolves on screen (tier (a), as the reflection) the same colour copy is read there (bilinear,
// the forward pass's own copy, bound at 68): C' = max(C + (1 - a) (Copy(hit) - Refr), 0). The medium is
// the FS's (its opacity a over the view-depth difference); hidden, off-screen, miss and aborted rays keep
// Refr. rt_debug 28: the view. Statistics: words 96..119 of the slot (kCsRr*).
//
// M5.4c the game's sun shadow (WATER_GSH=1 on top of any WATER_SHADOWS variant, rt_water_game_shadow with
// rt_water_shadows on when the water pipelines were created: 8 more bytes per record after the form's last,
// common/water_compose.h kGshBytes). The FS that sample the game's cascade shadow map (PL_noCaustics_Shadows,
// huge_PL_Shadow) carry its visibility Vg as a scalar factor of their sun radiance: they record Dg = D (1 - Vg)
// / Vg, the sun light the game's shadow removed, and Vg (the other FS Dg = 0, Vg = 1). The traced sun ray
// replaces Vg: lit gives C + Dg (kFlagGsh), occluded R (as M5.4: the FS at sun visibility 0), unknown C.
// rt_debug 26: a restored record is cyan. Statistics: words 44..53 of the slot (kCsGsh*).
#include "../gi/gi_common.hlsli"
#include "rt_common.hlsli"
#ifdef WATER_LS
#include "bg3_lights.hlsli"
#endif

struct ComposeParams {
  uint4 a;    // x pitch (records per row), y capacity (records), z rt_debug, w flags (bit 0 apply)
  float4 b;   // x roughness above which the probe stays (rf_mirror_rough)
};
[[vk::binding(60, 0)]] ConstantBuffer<ComposeParams> cp;
[[vk::binding(61, 0)]] RWByteAddressBuffer cStats;
[[vk::binding(62, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> wRad;   // rgb L, a material class
[[vk::binding(63, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> wDepOut;     // water_copy's target (gDepth copy)
[[vk::binding(64, 0)]] RWByteAddressBuffer cCam;                                     // water_copy: CameraCB + ViewportCB
[[vk::binding(66, 0)]] RWByteAddressBuffer cRec;                                     // the FS's records
[[vk::binding(67, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> target;   // the forward water target
#ifdef WATER_REFR
[[vk::binding(68, 0)]] Texture2D<float4> wCopy;   // the water draw's colour copy (its set 2 b4: the FS's Refr source)
#endif

#if defined(WATER_GSH) && defined(WATER_REFR)   // M5.4c: the form + kGshBytes, the words at the form's end
static const uint kHeaderBytes = 64u, kRecordBytes = 112u, kGshAt = 104u;
#elif defined(WATER_GSH) && defined(WATER_LS)
static const uint kHeaderBytes = 64u, kRecordBytes = 104u, kGshAt = 96u;
#elif defined(WATER_GSH)
static const uint kHeaderBytes = 64u, kRecordBytes = 48u, kGshAt = 40u;
#elif defined(WATER_REFR)
static const uint kHeaderBytes = 64u, kRecordBytes = 104u;  // common/water_compose.h kRecordBytesRefr
#elif defined(WATER_LS)
static const uint kHeaderBytes = 64u, kRecordBytes = 96u;   // common/water_compose.h kRecordBytesLs
#elif defined(WATER_SHADOWS)
static const uint kHeaderBytes = 64u, kRecordBytes = 40u;   // common/water_compose.h kRecordBytesSun
#else
static const uint kHeaderBytes = 64u, kRecordBytes = 32u;   // common/water_compose.h
#endif
static const uint kVpOffset = 512u;                         // cCam: CameraCB (25 float4) at 0, ViewportCB here

// Group-reduced statistics (words 1..31; word 0 is one add per dispatch): cstat / csum64 add a
// wave's count / sum into gsStat, cstat_flush (after a group barrier every thread reaches) adds the
// group's non-zero words to cStats, with the carry into the high word for the 64-bit sums.
#if defined(WATER_REFR)
static const uint kGsWords = 120u;  // common/water_compose.h kCsRrWords
#elif defined(WATER_LS)
static const uint kGsWords = 91u;   // common/water_compose.h kCsLsWords
#elif defined(WATER_SHADOWS) && defined(WATER_GSH)
static const uint kGsWords = 54u;   // common/water_compose.h kCsGshWords
#elif defined(WATER_SHADOWS)
static const uint kGsWords = 44u;   // common/water_compose.h kCsSunWords
#else
static const uint kGsWords = 32u;
#endif
groupshared uint gsStat[kGsWords];
void cstat(uint word, bool cond) {
  const uint n = WaveActiveCountBits(cond);
  if (WaveIsFirstLane() && n != 0u) InterlockedAdd(gsStat[word], n);
}
void csum64(uint word, float v, bool cond) {
  const uint q = cond ? uint(clamp(v, 0.0, 1e4) * 4096.0 + 0.5) : 0u;
  const uint s = WaveActiveSum(q);
  if (WaveIsFirstLane() && s != 0u) InterlockedAdd(gsStat[word], s);
}
#if defined(WATER_LS)
// A per-lane count (lights per record): at most kMaxFroxelLights per lane, 64 lanes: no overflow.
void cstatn(uint word, uint n) {
  const uint s = WaveActiveSum(n);
  if (WaveIsFirstLane() && s != 0u) InterlockedAdd(gsStat[word], s);
}
#if defined(WATER_REFR) && defined(WATER_GSH)
bool is_sum_lo(uint w) {   // + the sun sums, the M5.4c sums 48..52, the local sums 80..88 and the refraction sums 108..118
  return ((w >= 16u && w <= 26u) || w == 40u || w == 42u || (w >= 48u && w <= 52u) || (w >= 80u && w <= 88u) ||
          (w >= 108u && w <= 118u)) &&
         (w & 1u) == 0u;
}
#elif defined(WATER_GSH)
bool is_sum_lo(uint w) {   // + the sun sums, the M5.4c sums 48..52 and the local sums 80..88
  return ((w >= 16u && w <= 26u) || w == 40u || w == 42u || (w >= 48u && w <= 52u) || (w >= 80u && w <= 88u)) &&
         (w & 1u) == 0u;
}
#elif defined(WATER_REFR)
bool is_sum_lo(uint w) {   // + the sun sums, the local sums 80..88 and the refraction sums 108..118
  return ((w >= 16u && w <= 26u) || w == 40u || w == 42u || (w >= 80u && w <= 88u) || (w >= 108u && w <= 118u)) &&
         (w & 1u) == 0u;
}
#else
bool is_sum_lo(uint w) {   // + the sun sums and the local sums 80..88
  return ((w >= 16u && w <= 26u) || w == 40u || w == 42u || (w >= 80u && w <= 88u)) && (w & 1u) == 0u;
}
#endif
#elif defined(WATER_SHADOWS) && defined(WATER_GSH)
bool is_sum_lo(uint w) { return ((w >= 16u && w <= 26u) || w == 40u || w == 42u || (w >= 48u && w <= 52u)) && (w & 1u) == 0u; }
#elif defined(WATER_SHADOWS)
bool is_sum_lo(uint w) { return ((w >= 16u && w <= 26u) || w == 40u || w == 42u) && (w & 1u) == 0u; }   // + the sun sums
#else
bool is_sum_lo(uint w) { return w >= 16u && w <= 26u && (w & 1u) == 0u; }   // 16, 18, .. 26 (hi = w + 1)
#endif
#if defined(WATER_LS)
// 91 words over 64 lanes: each lane flushes its words gi, gi + 64.
void cstat_flush(uint gi) {
  GroupMemoryBarrierWithGroupSync();
  for (uint w = gi; w < kGsWords; w += 64u) {
    if (w == 0u) continue;   // word 0: one add per dispatch
    const uint s = gsStat[w];
    if (s == 0u) continue;
    if (is_sum_lo(w)) {
      uint old;
      cStats.InterlockedAdd(w * 4u, s, old);
      if (old + s < old) cStats.InterlockedAdd(w * 4u + 4u, 1u);
    } else {
      cStats.InterlockedAdd(w * 4u, s);
    }
  }
}
#else
void cstat_flush(uint gi) {
  GroupMemoryBarrierWithGroupSync();
  if (gi >= 1u && gi < kGsWords) {
    const uint s = gsStat[gi];
    if (s != 0u) {
      if (is_sum_lo(gi)) {
        uint old;
        cStats.InterlockedAdd(gi * 4u, s, old);
        if (old + s < old) cStats.InterlockedAdd(gi * 4u + 4u, 1u);
      } else {
        cStats.InterlockedAdd(gi * 4u, s);
      }
    }
  }
}
#endif
float lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

#ifdef WATER_LS
// The FS's froxel: log2-sliced depth z (its location-0 input) and the 2D tile of its pixel, set 2 b26
// (_m21.._m26, the offsets of bg3_lights.hlsli lConsts): first index, point and spot counts, cut.
uint4 ls_cluster(uint2 fp, float z) {
  const uint tsize = max(asuint(lConsts.c[31].x), 1u);
  const uint2 tile = fp / tsize;
  const float zmax = float(asuint(lConsts.c[31].y));
  const float sl = float(int(clamp(mad(log2(z), lConsts.c[30].x, lConsts.c[30].y), 0.0, zmax)));
  const uint idx = uint(int(mad(mad(sl, lConsts.c[30].w, float(tile.y)), lConsts.c[30].z, float(tile.x))));
  if (!bg3_in(lClusters, idx, 16u)) return uint4(0, 0, 0, 0);
  const uint4 c = lClusters.Load4(idx * 16u);
  uint np = c.y & 4095u, ns = (c.y >> 12) & 4095u, cut = 0u;
  if (c.x > 0xffffffffu - 2u * 4096u) return uint4(0, 0, 0, 1u);
  if (np + ns > kMaxFroxelLights) {
    cut = 1u;
    np = min(np, kMaxFroxelLights);
    ns = min(ns, kMaxFroxelLights - np);
  }
  return uint4(c.x, np, ns, cut);
}
// common/water_compose.h ls_check_bin: 0 unchecked, 1 within kLsSumTol, 2 up to kLsSumFar, 3 beyond.
uint ls_check_bin(float3 sum, float3 rec) {
  if (!all(isfinite(sum)) || !all(isfinite(rec))) return 3u;
  const float mx = max(max(max(rec.r, rec.g), rec.b), max(max(sum.r, sum.g), sum.b));
  const float3 e = abs(sum - rec);
  const float err = max(max(e.r, e.g), e.b);
  if (!(mx >= 6.103515625e-05)) return 0u;
  const float r = err / mx;
  return r <= (1.0 / 1024.0) ? 1u : r <= 0.1 ? 2u : 3u;
}
#endif
#ifdef WATER_REFR
// The colour copy at uv (viewport [0, 1]), bilinear between texel centres (the copy has the target's
// size: checked by the caller), as the FS's own linear sample of it.
float3 copy_bilinear(float2 quv) {
  const float2 x = quv * float2(pc.full) - 0.5;
  const float2 f = floor(x), w = x - f;
  const int2 i = int2(f), hi = int2(pc.full) - 1;
  const float3 c00 = wCopy.Load(int3(clamp(i, int2(0, 0), hi), 0)).rgb;
  const float3 c10 = wCopy.Load(int3(clamp(i + int2(1, 0), int2(0, 0), hi), 0)).rgb;
  const float3 c01 = wCopy.Load(int3(clamp(i + int2(0, 1), int2(0, 0), hi), 0)).rgb;
  const float3 c11 = wCopy.Load(int3(clamp(i + int2(1, 1), int2(0, 0), hi), 0)).rgb;
  return lerp(lerp(c00, c10, w.x), lerp(c01, c11, w.x), w.y);
}
#endif

[numthreads(8, 8, 1)]
void water_copy(uint3 tid : SV_DispatchThreadID) {
  const uint2 fp = tid.xy;
  if (all(fp == 0)) {
    const float4 c[25] = {cam.v0, cam.v1, cam.v2, cam.m6_r0, cam.m6_r1, cam.m6_r2, cam.m6_r3, cam.ray0, cam.ray1,
                          cam.ray2, cam.x[0], cam.x[1], cam.x[2], cam.x[3], cam.x[4], cam.x[5], cam.x[6], cam.x[7],
                          cam.x[8], cam.x[9], cam.x[10], cam.x[11], cam.x[12], cam.x[13], cam.x[14]};
    [unroll] for (uint i = 0; i < 25u; ++i) cCam.Store4(i * 16u, asuint(c[i]));
    cCam.Store4(kVpOffset, asuint(float4(vp.scale, vp.bias)));
  }
  if (any(fp >= pc.full)) return;
  const float3 L = gDiffuse[fp] + gSpecular[fp];
  const uint cls = uint(gMaterial.Load(int3(fp, 0)).a * 255.0 + 0.5) & 7u;
  wRad[fp] = float4(all(isfinite(L)) ? max(L, 0.0) : 0.0.xxx, float(cls));
  wDepOut[fp] = gDepth.Load(int3(fp, 0));
}

// B10G11R11 ownership (common/water_compose.h same_encoded): t within one ulp of c's channel format.
bool same_encoded(float c, float t, int mbits) {
  if (!(c >= 0.0) || !(t >= 0.0) || !isfinite(c) || !isfinite(t)) return false;
  const float m = max(c, 6.103515625e-05);
  float e;
  frexp(m, e);   // m = f 2^e, f in [0.5, 1): ilogb(m) = e - 1
  return abs(t - c) <= ldexp(1.0, e - 1.0 - float(mbits));
}

[numthreads(8, 8, 1)]
void water_compose(uint3 tid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  g_rt_pass = kRtPassRefl;
  const uint2 fp = tid.xy;
  if (all(fp == 0)) cStats.InterlockedAdd(0, 1u);
#ifdef WATER_LS
  for (uint gw = gi; gw < kGsWords; gw += 64u) gsStat[gw] = 0u;
#else
  if (gi < kGsWords) gsStat[gi] = 0u;
#endif
  GroupMemoryBarrierWithGroupSync();
  const bool inside = all(fp < pc.full);
  const uint idx = fp.y * cp.a.x + fp.x;
  bool has = inside && fp.x < cp.a.x && idx < cp.a.y;
  const uint at = kHeaderBytes + idx * kRecordBytes;
  uint4 r0 = 0u, r1 = 0u;
  if (has) {
    r0 = cRec.Load4(at);
    r1 = cRec.Load4(at + 16u);
  }
  has = has && (r1.w & 0xffffu) != 0u;
  if (has) cRec.Store(at + 28u, 0u);   // consumed
  const float2 a01 = f16tof32(uint2(r0.x, r0.x >> 16)), ab = f16tof32(uint2(r0.y, r0.y >> 16));
  const float2 b12 = f16tof32(uint2(r0.z, r0.z >> 16)), n01 = f16tof32(uint2(r0.w, r0.w >> 16));
  const float2 nc = f16tof32(uint2(r1.x, r1.x >> 16)), c12 = f16tof32(uint2(r1.y, r1.y >> 16));
  const float3 A = float3(a01, ab.x), B = float3(ab.y, b12), Crec = float3(nc.y, c12);
  float3 N = float3(n01, nc.x);
  const float t = asfloat(r1.z);
  const float rough = float(r1.w >> 16) / 65535.0;
  const float3 Ct = has ? target[fp] : 0.0.xxx;
  const bool own = has && same_encoded(Crec.r, Ct.r, 6) && same_encoded(Crec.g, Ct.g, 6) && same_encoded(Crec.b, Ct.b, 5);
  const float ln = length(N);
  const bool valid = own && all(isfinite(A)) && all(isfinite(B)) && all(A >= 0.0) && all(B >= 0.0) && t > 0.0 &&
                     isfinite(t) && ln > 0.5 && ln < 1.5;
  N = valid ? N / ln : float3(0, 1, 0);
  const float2 uv = (float2(min(fp, pc.full - 1)) + 0.5) / float2(pc.full);
  const float3 dir = normalize(ray_at(uv));
  const float3 P = dir * t;
  const float3 V = -dir;
  const bool smooth = rough <= cp.b.x;
  const bool above = dot(N, V) > 0.0;
#ifdef WATER_SHADOWS
  const bool trace = valid && smooth && above && (cp.a.w & 1u) != 0u;   // kFlagRf: rt_water_compose
#else
  const bool trace = valid && smooth && above;
#endif
  uint tier = 0u;
  bool skin = false;
  float3 L = 0.0.xxx;
  if (trace) {
    const float3 R = reflect(-V, N);
    const float pa = pixel_angle(uv);
    // The scene TLAS has no water (its own TLAS): one footprint off the surface clears it.
    const float3 o = P + N * (t * pa);
    RtCone cone = {t * pa, pa};
    bool aborted;
    if (!all(isfinite(o)) || !all(isfinite(R))) {
      tier = 6u;
    } else {
      const float th = rt_closest(o, R, pc.rf_tmax, cone, aborted);
      if (aborted) tier = 5u;
      else if (th < 0.0) tier = 4u;
      else {
        const float3 H = o + R * th;
        float2 quv;
        float qd;
        const bool onscreen = project(frustum_inverse(), H, quv, qd) && all(quv >= 0.0) && all(quv <= 1.0);
        tier = 3u;
        if (onscreen) {
          tier = 2u;
          const int2 qp = min(int2(quv * float2(pc.full)), int2(pc.full) - 1);
          const float sd = gDepth.Load(int3(qp, 0));
          if (sd > 0.0) {
            const float3 Pq = ray_at((float2(qp) + 0.5) / float2(pc.full)) * sd;
            float3 ngq = depth_geo_normal(qp, Pq);
            if (dot(ngq, Pq) > 0.0) ngq = -ngq;   // towards the camera
            const float4 rq = wRad[qp];
            skin = (uint(rq.a + 0.5) & 7u) == kMatSkin;
            if (depth_same_surface(qp, qd, sd, ngq, false, 0.0, 1.0) && dot(ngq, R) < 0.0 && !skin) {
              L = rq.rgb;
              tier = 1u;
            }
          }
        }
      }
    }
  }
  const bool composed = tier == 1u;
#ifdef WATER_SHADOWS
  // M5.4: the FS's sun visibility (1) replaced by one traced ray: occluded, the FS's own colour
  // without its sun term R = C - D (f32 in the FS, then half: common/water_compose.h; not C - D here,
  // where the B10G11R11 target's rounding of C swamps the rest wherever the sun is most of C).
  uint2 r2 = 0u;
  if (has) r2 = cRec.Load2(at + 32u);
  const float2 r01 = f16tof32(uint2(r2.x, r2.x >> 16)), r2d = f16tof32(uint2(r2.y, r2.y >> 16));
  const float3 Rs = float3(r01, r2d.x);
  const float lumD = r2d.y;   // lum(D) of the FS's unshadowed sun term
  const bool sun_on = (cp.a.w & 2u) != 0u;   // kFlagSun: rt_water_shadows
  const bool d_ok = all(isfinite(Rs)) && isfinite(lumD) && lumD >= 0.0;
#ifdef WATER_GSH
  // M5.4c: the sun light the game's shadow removed Dg = D_u - D and its visibility Vg (1: no game shadow).
  uint2 rg = 0u;
  if (has) rg = cRec.Load2(at + kGshAt);
  const float2 g01 = f16tof32(uint2(rg.x, rg.x >> 16)), g2v = f16tof32(uint2(rg.y, rg.y >> 16));
  const float3 Dg = float3(g01, g2v.x);
  const float Vg = g2v.y;
  const bool gsh_on = (cp.a.w & 16u) != 0u;   // kFlagGsh: rt_water_game_shadow
  const bool g_ok = all(isfinite(Dg)) && isfinite(Vg) && Vg >= 0.0 && Vg <= 1.0;
  const bool gshrec = sun_on && valid && d_ok && g_ok && Vg < 1.0;
  // A record the game shadowed fully has lum(D) = Vg lum(D_u) below the half's range: its sun term is Dg's.
  const bool sunrec = sun_on && valid && d_ok && (lumD > 0.0 || (gshrec && any(Dg > 0.0)));
#else
  const bool sunrec = sun_on && valid && d_ok && lumD > 0.0;
#endif
  const float3 Ls = normalize(cam.v0.xyz);
  uint sv = kRtLit;
  bool straced = false;
  if (sunrec && dot(N, Ls) > 0.0) {
    g_rt_pass = kRtPassSun;
    float3 sdir = Ls;
    if (pc.sun_radius > 0.0) {   // rt_shadow.hlsl's uniform sample of the sun's disc
      uint sd = pixel_seed(fp, 1u);
      const float u1 = rand01(sd), u2 = rand01(sd);
      const float3 st = abs(Ls.y) < 0.99 ? normalize(cross(float3(0, 1, 0), Ls)) : normalize(cross(float3(0, 0, 1), Ls));
      const float3 sb = cross(Ls, st);
      const float sr = sqrt(u1) * tan(pc.sun_radius), sphi = 6.2831853 * u2;
      sdir = normalize(Ls + (st * cos(sphi) + sb * sin(sphi)) * sr);
    }
    const float spa = pixel_angle(uv);
    const float3 so = P + N * (t * spa);   // the scene TLAS has no water: one footprint clears the surface
    RtCone scone = {t * spa, spa};
    if (all(isfinite(so)) && all(isfinite(sdir))) {
      sv = rt_visibility(so, sdir, pc.rt_tmax, scone);
      straced = true;
    }
  }
  const bool occluded = straced && sv == kRtOccluded;
#ifdef WATER_GSH
  // M5.4c: lit where the game shadowed: the FS at sun visibility 1, C + Dg (V (C + Dg) + (1 - V) R at V = 1).
  const bool restored = gsh_on && gshrec && straced && sv == kRtLit && any(Dg != 0.0);
  const bool sunchg = occluded || restored;
  const float3 Cs = occluded ? Rs : restored ? Ct + Dg : Ct;
#else
  const bool sunchg = occluded;
  const float3 Cs = occluded ? Rs : Ct;           // V C + (1 - V) R
#endif
  const float rmL = occluded ? lumD : 0.0;        // lum((1 - V) D)
#ifdef WATER_LS
  // M5.4b: the FS's local lights, walked as the FS walks them; the unshadowed ones traced.
  uint4 r3 = 0u, r4 = 0u, r5 = 0u;
  uint2 r6 = 0u;
  if (has) {
    r3 = cRec.Load4(at + 40u);   // w10..w13
    r4 = cRec.Load4(at + 56u);   // w14..w17
    r5 = cRec.Load4(at + 72u);   // w18..w21
    r6 = cRec.Load2(at + 88u);   // w22, w23
  }
  const float2 f01 = f16tof32(uint2(r3.x, r3.x >> 16)), f2f0 = f16tof32(uint2(r3.y, r3.y >> 16));
  const float2 l01 = f16tof32(uint2(r3.z, r3.z >> 16)), l2c = f16tof32(uint2(r3.w, r3.w >> 16));
  const float2 df01 = f16tof32(uint2(r4.x, r4.x >> 16)), df2b = f16tof32(uint2(r4.y, r4.y >> 16));
  const float3 Fl = float3(f01, f2f0.x), Lrec = float3(l01, l2c.x), dif = float3(df01, df2b.x);
  const float lz = asfloat(r4.z);
  const float3 Pl = asfloat(uint3(r4.w, r5.x, r5.y)), Nl = asfloat(uint3(r5.z, r5.w, r6.x));
  const float rl = asfloat(r6.y);
  const bool ls_on = (cp.a.w & 4u) != 0u;   // kFlagLs: rt_water_ls_shadows
  const float lnl = length(Nl);
  const bool ls_ok = l2c.y >= 1.0 && l2c.y <= 32.0 && all(isfinite(Fl)) && all(Fl >= 0.0) && all(isfinite(Lrec)) &&
                     all(Lrec >= 0.0) && all(isfinite(dif)) && isfinite(f2f0.y) && isfinite(lz) && lz > 0.0 &&
                     all(isfinite(Pl)) && isfinite(rl) && lnl > 0.5 && lnl < 1.5 && isfinite(df2b.y);
  const bool lswalk = ls_on && valid && ls_ok;
  const bool lsrec = lswalk && any(Lrec > 0.0);
  uint nEval = 0u, nSh = 0u, nTr = 0u, nOcc = 0u, nUnk = 0u, lsCut = 0u;
  float3 lsSum = 0.0.xxx, lsRem = 0.0.xxx;
  if (lswalk) {
    g_rt_pass = kRtPassLocal;
    Bg3Surface sf = (Bg3Surface)0;
    sf.P = Pl;
    sf.N = Nl;   // the FS's normalised _1207 / _1294 / _665, as its loops read it
    sf.V = normalize(-Pl);
    sf.diff_col = dif;
    sf.f0 = f2f0.yyy;
    sf.a = rl;
    sf.nov = max(dot(Nl, sf.V), 9.9999997e-06);
    sf.channel = uint(l2c.y - 1.0 + 0.5);
    // The FS's PCF rotation: fract(52.98 fract(0.0671 x + 0.00584 y)) at gl_FragCoord (pixel centre).
    const float2 fc = float2(fp) + 0.5;
    sf.ign = frac(52.98291778564453125 * frac(mad(0.067110560834407806396484375, fc.x, 0.005837149918079376220703125 * fc.y)));
    const uint4 cl = ls_cluster(fp, lz);
    lsCut = cl.w;
    const float lpa = pixel_angle(uv);
    const float lt = length(Pl);
    const float3 lo = Pl + Nl * (lt * lpa);   // the scene TLAS has no water: one footprint clears it
    RtCone lcone = {lt * lpa, lpa};
    const uint n = cl.y + cl.z;
    [loop] for (uint i = 0u; i < n; ++i) {
      const uint li = bg3_light_index(cl.x + i);
      const Bg3Light lg = load_light(li);
      const bool spot = i >= cl.y;
      float3 dd, ss, Ld;
      float d2, nol;
      if (!bg3_light_eval(lg, spot, sf, dd, ss, Ld, d2, nol)) continue;
      ++nEval;
      const float3 ck = dd + ss;
      if ((lg.flags & 1u) != 0u) {   // a game shadow map: its atlas term, kept as the FS's
        ++nSh;
        lsSum += ck * bg3_atlas_shadow(lg, spot, sf, Ld, d2, nol);
        continue;
      }
      lsSum += ck;
      if (!any(ck > 0.0)) continue;
      const float3 tl = lg.pos - lo;
      const float dist = length(tl);
      const float tmax = dist - pc.ls_clear;
      if (!(tmax > 0.0) || !all(isfinite(lo))) continue;   // the light at the surface: lit
      ++nTr;
      const uint lv = rt_visibility(lo, tl / dist, tmax, lcone);
      if (lv == kRtOccluded) {
        ++nOcc;
        lsRem += ck;
      } else if (lv == kRtUnknown) {
        ++nUnk;
      }
    }
  }
  const bool lsocc = nOcc != 0u;
  const float3 Rls = lsocc ? Fl * lsRem : 0.0.xxx;   // F sum_k (1 - V_k) c_k
  const uint lsbin = lswalk ? ls_check_bin(lsSum, Lrec) : 0u;
#ifdef WATER_REFR
  // M5.5 step 1: the FS's screen sample Refr replaced by the colour copy where the refracted ray's hit is.
  uint2 r7 = 0u;
  if (has) r7 = cRec.Load2(at + 96u);   // w24, w25
  const float2 rr01 = f16tof32(uint2(r7.x, r7.x >> 16)), rr2w = f16tof32(uint2(r7.y, r7.y >> 16));
  const float3 Refr = float3(rr01, rr2w.x);
  const float Rw = rr2w.y;   // 1 - a = dC/dRefr
  const bool rr_on = (cp.a.w & 8u) != 0u;   // kFlagRefr: rt_water_refraction
  uint cw = 0u, chh = 0u;
  wCopy.GetDimensions(cw, chh);
  const bool copy_ok = cw == pc.full.x && chh == pc.full.y;
  const bool rr_words = all(isfinite(Refr)) && all(Refr >= 0.0) && isfinite(Rw) && Rw >= 0.0 && Rw <= 1.0;
  const bool rr_valid = rr_on && valid && rr_words;
  const bool rr_rec = rr_valid && Rw > 0.0 && copy_ok;
  // n from the FS's own F0 (w11 high half; common/water_compose.h ior_from_f0): water 0.0196 -> 1.326.
  const float rf0 = f2f0.y;
  const bool has_n = rf0 >= 0.0 && rf0 < 1.0;
  const float rsf = sqrt(saturate(rf0));
  const float3 Tdir = has_n ? refract(-V, N, (1.0 - rsf) / (1.0 + rsf)) : 0.0.xxx;   // eta = 1 / n
  const bool rtraced = rr_rec && above && has_n && any(Tdir != 0.0);
  uint rtier = 0u;
  float3 Lc = 0.0.xxx;
  float rshift = 0.0;
  if (rtraced) {
    g_rt_pass = kRtPassRefl;
    const float rpa = pixel_angle(uv);
    RtCone rcone = {t * rpa, rpa};   // the scene TLAS has no water: the ray starts at the surface
    bool raborted;
    if (!all(isfinite(P)) || !all(isfinite(Tdir))) {
      rtier = 6u;
    } else {
      const float th = rt_closest(P, Tdir, pc.rf_tmax, rcone, raborted);
      if (raborted) rtier = 5u;
      else if (th < 0.0) rtier = 4u;
      else {
        const float3 H = P + Tdir * th;
        float2 quv;
        float qd;
        const bool onscreen = project(frustum_inverse(), H, quv, qd) && all(quv >= 0.0) && all(quv <= 1.0);
        rtier = 3u;
        if (onscreen) {
          rtier = 2u;
          const int2 qp = min(int2(quv * float2(pc.full)), int2(pc.full) - 1);
          const float sd = gDepth.Load(int3(qp, 0));
          if (sd > 0.0) {
            const float3 Pq = ray_at((float2(qp) + 0.5) / float2(pc.full)) * sd;
            float3 ngq = depth_geo_normal(qp, Pq);
            if (dot(ngq, Pq) > 0.0) ngq = -ngq;   // towards the camera
            // The copy shows at qp the surface the ray hit (the bed, a submerged wall, a wading leg): its
            // colour as the FS's own Refr source holds it.
            if (depth_same_surface(qp, qd, sd, ngq, false, 0.0, 1.0) && dot(ngq, Tdir) < 0.0) {
              rtier = 1u;
              Lc = copy_bilinear(quv);
              rshift = length(quv * float2(pc.full) - (float2(fp) + 0.5));
            }
          }
        }
      }
    }
  }
  const bool rr_hit = rtier == 1u && all(isfinite(Lc));
  const float3 Rr = rr_hit ? Rw * (max(Lc, 0.0) - Refr) : 0.0.xxx;   // (1 - a) (Copy(hit) - Refr)
  const float3 Cu = Cs + (composed ? A * L - B : 0.0.xxx) - Rls + Rr;
  const float3 Cn = composed || sunchg || lsocc || rr_hit ? max(Cu, 0.0) : Ct;
#else
  const float3 Cu = Cs + (composed ? A * L - B : 0.0.xxx) - Rls;
  const float3 Cn = composed || sunchg || lsocc ? max(Cu, 0.0) : Ct;
#endif
#else
  const float3 Cu = Cs + (composed ? A * L - B : 0.0.xxx);
  const float3 Cn = composed || sunchg ? max(Cu, 0.0) : Ct;
#endif
#else
  const float3 Cu = Ct + A * L - B;
  const float3 Cn = composed ? max(Cu, 0.0) : Ct;
#endif
  const float d = lum(Cn) - lum(Ct);
  cstat(1, has);
  cstat(2, own);
  cstat(3, has && !own);
  cstat(4, own && !valid);
  cstat(5, valid && !smooth);
  cstat(6, valid && smooth && !above);
  cstat(7, trace && tier != 6u);
  // Tiers 1..6 at words 8..13 (common/water_compose.h kCsTier1). Was 6 + k: tier 1 went into the
  // ray count (7) and every logged tier was the next one (m5w3: "off screen 66%" were sky misses).
  [unroll] for (uint k = 1u; k <= 6u; ++k) cstat(7u + k, trace && tier == k);
  cstat(14, trace && tier == 2u && skin);
#ifdef WATER_SHADOWS
  cstat(15, composed && any(Ct + A * L - B < 0.0));
#else
  cstat(15, composed && any(Cu < 0.0));
#endif
  csum64(16, lum(Ct), own);
  csum64(18, lum(B), composed);
  csum64(20, lum(A * L), composed);
  csum64(22, max(d, 0.0), composed);
  csum64(24, max(-d, 0.0), composed);
  csum64(26, lum(B), valid);
  cstat(28, composed);
  // A L / B per composed pixel (common/water_compose.h ratio_bin): L / (k Sp), the traced radiance
  // over the probe's in that direction. A unit mismatch is one factor for every pixel (one bin);
  // occlusion of the probe's sky by the hit geometry spreads it.
  const float rb = lum(B), ra = lum(A * L), rr = rb > 0.0 ? ra / rb : 0.0;
  cstat(29, composed && rb > 0.0 && rr < 0.5);
  cstat(30, composed && rb > 0.0 && rr >= 0.5 && rr <= 2.0);
  cstat(31, composed && rb > 0.0 && rr > 2.0);
#ifdef WATER_SHADOWS
  cstat(32, sunrec);
  cstat(33, straced);
  cstat(34, occluded);
  cstat(35, straced && sv == kRtUnknown);
  cstat(36, sun_on && valid && d_ok && !(lumD > 0.0));
  cstat(37, sun_on && valid && !d_ok);
  cstat(38, occluded && any(Cu < 0.0));
  csum64(40, lumD, sunrec);
  csum64(42, rmL, sunrec);
#endif
#ifdef WATER_GSH
  // common/water_compose.h kCsGsh*.
  cstat(44, gshrec);
  cstat(45, restored);
  cstat(46, occluded && g_ok && Vg == 1.0);
  cstat(47, sun_on && valid && !g_ok);
  csum64(48, lum(Dg), gshrec);
  csum64(50, lum(Dg), restored);
  csum64(52, Vg, gshrec);
#endif
#ifdef WATER_LS
  // common/water_compose.h kCsLs*.
  cstat(64, lsrec);
  cstat(65, lswalk && !any(Lrec > 0.0));
  cstat(66, ls_on && valid && !ls_ok);
  cstatn(67, nEval);
  cstatn(68, nSh);
  cstatn(69, nTr);
  cstatn(70, nOcc);
  cstatn(71, nUnk);
  cstat(72, lsocc);
  cstat(73, lsbin != 0u);
  cstat(74, lsbin == 1u);
  cstat(75, lsbin == 2u);
  cstat(76, lsbin == 3u);
  cstat(77, lsocc && any(Cu < 0.0));
  cstat(78, lswalk && lsCut != 0u);
  cstat(79, lsbin != 0u && nSh != 0u);
  cstat(90, lsbin == 1u && nSh != 0u);
  csum64(80, lum(Fl * Lrec), lsrec);
  csum64(82, lum(Rls), lsrec);
  csum64(84, lum(Fl * lsSum), lsbin != 0u);
  csum64(86, lum(Fl * Lrec), lsbin != 0u);
  csum64(88, max(df2b.y, 0.0), lsrec);
#endif
#ifdef WATER_REFR
  // common/water_compose.h kCsRr*.
  cstat(96, rr_rec);
  cstat(97, rr_valid && Rw == 0.0);
  cstat(98, rr_on && valid && !rr_words);
  cstat(99, rtraced);
  cstat(100, rr_hit);
  cstat(101, rtraced && rtier == 2u);
  cstat(102, rtraced && rtier == 3u);
  cstat(103, rtraced && rtier == 4u);
  cstat(104, rtraced && (rtier == 5u || rtier == 6u || (rtier == 1u && !rr_hit)));
  cstat(105, rr_rec && !rtraced);
  cstat(106, rr_hit && any(Cu < 0.0));
  cstat(107, rr_valid && Rw > 0.0 && !copy_ok);
  csum64(108, lum(Rw * Refr), rr_hit);
  csum64(110, lum(Rw * max(Lc, 0.0)), rr_hit);
  csum64(112, lum(Rw * Refr), rr_rec);
  csum64(114, rshift, rr_hit);
  csum64(116, Rw, rtraced && rtier == 2u);
  csum64(118, Rw, rtraced);
#endif
  cstat_flush(gi);
  if (!has) return;
  // rt_debug 25, over every record of this pass (exact codes, no tonemap of ours):
  //   composed: green = the relative brightening d / lum(C), red = the relative darkening;
  //   kept (unresolved ray): dark blue; kept (rough or below the surface): dark cyan;
  //   not owned (another fragment's colour) or invalid: yellow.
#ifdef WATER_SHADOWS
  // rt_debug 26, over every record of this pass: occluded = red (the removed share of C, at least
  // 0.25), lit = green 0.35, unknown (aborted, kept) = blue, no sun term = dark grey, a sun term at
  // N.L <= 0 of the FS's N (not traced) = dark magenta, not owned / invalid = yellow.
  if (cp.a.z == 26u) {
    const float share = rmL / max(lum(Ct), 1e-4);
    target[fp] = !own || !valid ? float3(1, 1, 0)
               : !sunrec ? float3(0.08, 0.08, 0.08)
               : !straced ? float3(0.25, 0, 0.25)
               : occluded ? float3(max(saturate(share), 0.25), 0, 0)
#ifdef WATER_GSH
               : restored ? float3(0, 0.6, 0.6)
#endif
               : sv == kRtUnknown ? float3(0, 0, 1)
               : float3(0, 0.35, 0);
    return;
  }
#endif
#ifdef WATER_LS
  // rt_debug 27, over every record of this pass: a light occluded = red (the removed share of C, at
  // least 0.25), every traced light lit = green 0.35, only shadow-mapped lights (nothing traced) =
  // dark magenta, an unknown ray and nothing occluded = blue, no local term = dark grey; the sum check
  // beyond kLsSumFar = white (the gate's failures, over every other code); not owned / invalid = yellow.
  if (cp.a.z == 27u) {
    const float share = lum(Rls) / max(lum(Ct), 1e-4);
    target[fp] = !own || !valid || !ls_ok ? float3(1, 1, 0)
               : lsbin == 3u ? float3(1, 1, 1)
               : !lsrec ? float3(0.08, 0.08, 0.08)
               : lsocc ? float3(max(saturate(share), 0.25), 0, 0)
               : nUnk != 0u ? float3(0, 0, 1)
               : nTr == 0u ? float3(0.25, 0, 0.25)
               : float3(0, 0.35, 0);
    return;
  }
#endif
#ifdef WATER_REFR
  // rt_debug 28, over every record of this pass: the refracted hit composed = green (its weight 1 - a, at
  // least 0.15), hidden = red, off screen = blue, miss = magenta, aborted = white, not traced (N.V <= 0,
  // no index) = dark cyan, opaque water (1 - a = 0) = dark grey, no usable copy = orange, not owned /
  // invalid = yellow.
  if (cp.a.z == 28u) {
    target[fp] = !own || !valid || !rr_words ? float3(1, 1, 0)
               : Rw == 0.0 ? float3(0.08, 0.08, 0.08)
               : !copy_ok ? float3(1, 0.4, 0)
               : !rtraced ? float3(0, 0.25, 0.25)
               : rr_hit ? float3(0, max(Rw, 0.15), 0)
               : rtier == 2u ? float3(0.8, 0, 0)
               : rtier == 3u ? float3(0, 0, 0.8)
               : rtier == 4u ? float3(0.6, 0, 0.6)
               : float3(1, 1, 1);
    return;
  }
#endif
  if (cp.a.z == 25u) {
    const float rel = d / max(lum(Ct), 1e-4);
    const float3 c = !own || !valid ? float3(1, 1, 0)
                   : !trace ? float3(0, 0.25, 0.25)
                   : composed ? float3(max(-rel, 0.0), max(rel, 0.0), 0.0)
                   : float3(0, 0, 0.3);
    target[fp] = c;
#if defined(WATER_REFR)
  } else if (composed || sunchg || lsocc || rr_hit) {   // each term only where its flag traced it
    target[fp] = Cn;
  }
#elif defined(WATER_LS)
  } else if (composed || sunchg || lsocc) {   // each term only where its flag traced it
    target[fp] = Cn;
  }
#elif defined(WATER_SHADOWS)
  } else if (composed || sunchg) {   // each term only where its flag traced it
    target[fp] = Cn;
  }
#else
  } else if (composed && (cp.a.w & 1u) != 0u) {
    target[fp] = Cn;
  }
#endif
}
