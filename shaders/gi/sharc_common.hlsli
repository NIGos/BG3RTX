// SHaRC radiance cache, v1: screen-injected (sharc=1; docs/DESIGN_SOTA_NEXT.md item 2, core/sharc.cpp).
//
// A world-space hash grid (NVIDIA SHaRC 1.8.3, external/sharc-1.8.3/include) holding the
// radiance of surfaces the camera has seen: every frame sharc_inject.hlsl writes one pixel
// per sharc_inject_stride^2 block of the game's lit diffuse b10 (plus the D-EM effects-
// target emission when that branch is present) as a one-vertex SHaRC update;
// sharc_resolve.hlsl merges it into the cross-frame average (accumulation cap sharc_frames,
// eviction after sharc_stale frames without a sample); GI rays at hidden or off-screen
// hits (gi_trace_rt_sharc.hlsl) and the reflection fallbacks (rt_reflections_sharc.hlsl)
// read it. Positions are absolute world (camera-relative + the lighting's set 0 b2 _m6),
// normals geometric, facing the side the camera or the ray sees: the key's normal octant
// keeps the two faces of a wall apart (the leak mitigation of 2.7).
//
// Every file that includes this must start with "// dxc: -enable-16bit-types" (the SDK
// stores radiance as float16_t4; layer/shaders.cpp passes the flag).
#pragma once

#ifndef BG3RTX_SHARC_SDK
#error SHaRC SDK headers not found (external/sharc-1.8.3/include; layer/shaders.cpp adds the include path)
#endif

#ifndef SHARC_UPDATE
#define SHARC_UPDATE 0
#endif
// One-vertex updates: no path to propagate along, no resampling of the cache in the update.
#define SHARC_ENABLE_CACHE_RESAMPLING 0
#define SHARC_PROPAGATION_DEPTH 1
// 0 only in sharc_inject_lock.hlsl (a device without shaderBufferInt64Atomics).
#ifndef HASH_GRID_ENABLE_64_BIT_ATOMICS
#define HASH_GRID_ENABLE_64_BIT_ATOMICS 1
#endif
#include "SharcCommon.h"

// Set 4: written once per allocation by core/sharc.cpp. Sets 2-3 of the passes' layouts
// are the D-RF game set / empty (and D-EM's set 3 later).
[[vk::binding(0, 4)]] RWStructuredBuffer<uint64_t> shHash;                    // 8 B per entry
[[vk::binding(1, 4)]] RWStructuredBuffer<SharcAccumulationData> shAccum;      // 16 B per entry
[[vk::binding(2, 4)]] RWStructuredBuffer<SharcPackedData> shResolved;         // 16 B per entry

// core/sharc.cpp SharcConsts (std140, updated once per frame before the resolve).
struct SharcConsts {
  uint capacity;          // entries (2^sharc_log2)
  float scene_scale;      // HashGridParameters::sceneScale (sharc_scene_scale)
  float log_base;         // ::logarithmBase (sharc_log_base, SDK 2)
  float level_bias;       // ::levelBias (sharc_level_bias, SDK 0)
  float radiance_scale;   // fixed-point scale of the accumulation (SDK 1e3)
  uint accum_frames;      // sharc_frames
  uint stale_frames;      // sharc_stale
  uint flags;             // bit 0 GI hidden hits query, 1 GI off-screen, 2 reflections, 3 statistics,
                          // 4 leak test (sharc_leak_check), 5 reject occluded cells (sharc_reject_occluded),
                          // 6 fine-cell reference (sharc_fine_check)
  uint stride;            // sharc_inject_stride
  uint debug;             // sharc_debug
  uint stats_off;         // byte offset of this frame's counters in shState
  uint frame;
  uint groups_x;          // resolve: thread groups per row
  float rf_rough;         // reflections use the cache from this roughness (sharc_rf_rough)
  float rf_cone;          // ... when the ray cone at the hit is at least this many voxels wide
  float near_voxels;      // hits closer than this many voxels keep the old rule (SHaRC's segment rule)
  float max_radiance;     // injected radiance is clamped to it (accumulation overflow)
  float reset_jump;       // camera moved farther in one frame: the cache is cleared (m)
  float em_scale;         // weight of the D-EM effects-target emission (sharc_em)
  float gi_base_radius;   // gi_rt_radius: hits without radiance occlude only within it (not to sharc_gi_radius)
  float fine_scale;       // sharc_fine_check: the reference grid's cells are this many times smaller
  float pad0, pad1, pad2;
};
[[vk::binding(3, 4)]] ConstantBuffer<SharcConsts> shc;
// Device local: bytes 0-15 the camera (absolute) of the last injection, w = 1 once written;
// from 16 this frame's 16 counters x 64 bins (zeroed per frame, copied into a host ring and
// summed by core/sharc.cpp).
[[vk::binding(4, 4)]] RWByteAddressBuffer shState;
#if !HASH_GRID_ENABLE_64_BIT_ATOMICS
[[vk::binding(5, 4)]] RWStructuredBuffer<uint> shLock;                        // 4 B per entry
#endif

static const uint kShInjected = 0, kShInsertFailed = 1, kShEmissive = 2, kShGiHidden = 3, kShGiOffscreen = 4,
                  kShGiQueried = 5, kShGiFound = 6, kShGiNear = 7, kShRfEligible = 8, kShRfFound = 9,
                  kShLive = 10, kShEvicted = 11, kShReset = 12, kShInjectSkipped = 13,
                  // leak test (sharc_cell_test): cache hits whose cell is occluded / thin, and the
                  // GI contribution (luminance x 256 of L x w) in all, from occluded cells, from thin ones
                  kShCellOccluded = 14, kShCellThin = 15, kShContrib = 16, kShContribOccluded = 17,
                  kShContribThin = 18,
                  // fine-cell reference (sharc_fine_check): per cache hit with a fine-cell entry too, the
                  // difference coarse - fine of the radiance's luminance (x 256): count, positive and negative
                  // sums, sum of squares (x 1/256, clamped per hit at 65535), sum of the fine luminance;
                  // for clean cells (19-23) and occluded cells (24-28)
                  kShFineN = 19, kShFinePos = 20, kShFineNeg = 21, kShFineSq = 22, kShFineRef = 23,
                  kShFineOccN = 24, kShFineOccPos = 25, kShFineOccNeg = 26, kShFineOccSq = 27, kShFineOccRef = 28;

// Lanes with `cond` added to counter i of this frame's slot (sharc_stats=1), spread over 64
// bins by the wave's first lane (one address per counter serialises the whole frame's waves:
// 3.6 ms instead of 0.25 for the GI trace in the first in-game run).
// Each pass sets sharc_bin to something that varies across its waves (the thread's position).
static const uint kShBins = 64;
static uint sharc_bin = 0;
void sharc_stat(uint i, bool cond) {
  if ((shc.flags & 8u) == 0u) return;
  uint n = WaveActiveCountBits(cond);
  uint b = (WaveReadLaneFirst(sharc_bin) * 0x9E3779B9u) >> 26;   // 0..63
  if (WaveIsFirstLane() && n) shState.InterlockedAdd(shc.stats_off + (i * kShBins + b) * 4u, n);
}
// The lanes' sum of v added to counter i.
void sharc_stat_sum(uint i, uint v) {
  if ((shc.flags & 8u) == 0u) return;
  uint n = WaveActiveSum(v);
  uint b = (WaveReadLaneFirst(sharc_bin) * 0x9E3779B9u) >> 26;
  if (WaveIsFirstLane() && n) shState.InterlockedAdd(shc.stats_off + (i * kShBins + b) * 4u, n);
}

// The absolute camera position: the lighting's set 0 b2, _m6 at byte 208. Bound at 48 in the
// ray-traced layout; the D-RF layout has it at 29 (rt/bg3_lights.hlsli cam2).
#ifdef SHARC_CAM_FROM_LIGHTS
float3 sharc_cam() { return cam2.c[13].xyz; }
#else
struct ShCam2CB { float4 c[14]; };
[[vk::binding(48, 0)]] ConstantBuffer<ShCam2CB> shCam2;
float3 sharc_cam() { return shCam2.c[13].xyz; }
#endif

// The fine-cell reference grid (sharc_fine_check, a QA diagnostic): the same estimator with cells
// fine_scale times smaller, which converges to the surface's own injected radiance as the cell
// shrinks below the thickness of what separates two sides; a coarse cell's excess over it is the
// cache's spatial leak. Its keys live in the same table, 16 levels above the coarse grid's (the
// level is part of the key), so the two never share an entry. Valid for a static camera: the
// resolve's adjacent-level blend assumes the coarse grid's voxel size.
HashGridParameters sharc_grid_fine(float3 cam_abs) {
  HashGridParameters g;
  g.cameraPosition = cam_abs;
  g.logarithmBase = shc.log_base;
  g.sceneScale = shc.scene_scale * shc.fine_scale;
  g.levelBias = shc.level_bias + 16.0;
  return g;
}

HashGridParameters sharc_grid(float3 cam_abs) {
  HashGridParameters g;
  g.cameraPosition = cam_abs;
  g.logarithmBase = shc.log_base;
  g.sceneScale = shc.scene_scale;
  g.levelBias = shc.level_bias;
  return g;
}

SharcParameters sharc_params(float3 cam_abs) {
  SharcParameters p;
  p.hashGridParameters = sharc_grid(cam_abs);
  p.hashGridData.capacity = shc.capacity;
  p.hashGridData.hashEntriesBuffer = shHash;
#if !HASH_GRID_ENABLE_64_BIT_ATOMICS
  p.hashGridData.lockBuffer = shLock;
#endif
  p.radianceScale = shc.radiance_scale;
  p.accumulationBuffer = shAccum;
  p.resolvedBuffer = shResolved;
  return p;
}

// Edge length of the voxel holding p (absolute world).
float sharc_voxel_size(float3 cam_abs, float3 p_abs) {
  HashGridParameters g = sharc_grid(cam_abs);
  return HashGridGetVoxelSize(HashGridGetLevel(p_abs, g), g);
}

// A normal turned to face against `dir` (towards the ray's origin or the camera).
float3 sharc_face(float3 n, float3 dir) {
  n = normalize(n);
  return dot(n, dir) > 0.0 ? -n : n;
}

// The cached radiance at a surface (absolute position, normal facing the viewer).
bool sharc_lookup(float3 cam_abs, float3 p_abs, float3 n, out float3 L) {
  SharcParameters sp = sharc_params(cam_abs);
  SharcHitData h;
  h.positionWorld = p_abs;
  h.normalWorld = n;
  L = 0.0.xxx;
  bool ok = SharcGetCachedRadiance(sp, h, L, true);
  if (!ok || any(!isfinite(L))) {
    L = 0.0.xxx;
    return false;
  }
  L = max(L, 0.0);
  return true;
}

// The fine reference grid's radiance at a surface.
bool sharc_lookup_fine(float3 cam_abs, float3 p_abs, float3 n, out float3 L) {
  SharcParameters sp = sharc_params(cam_abs);
  sp.hashGridParameters = sharc_grid_fine(cam_abs);
  SharcHitData h;
  h.positionWorld = p_abs;
  h.normalWorld = n;
  L = 0.0.xxx;
  bool ok = SharcGetCachedRadiance(sp, h, L, true);
  if (!ok || any(!isfinite(L))) return false;
  L = max(L, 0.0);
  return true;
}

// sharc_fine_check: the coarse entry's luminance against the fine reference's at the same point.
void sharc_fine_stat(float3 cam_abs, float3 p_abs, float3 n, float3 Lc, bool occluded) {
  if ((shc.flags & (64u | 8u)) != (64u | 8u)) return;
  float3 Lf;
  const bool both = sharc_lookup_fine(cam_abs, p_abs, n, Lf);
  const float3 k = float3(0.2126, 0.7152, 0.0722);
  const float d = both ? (dot(Lc, k) - dot(Lf, k)) * 256.0 : 0.0;
  const uint pos = both ? uint(clamp(d, 0.0, 4e6)) : 0u, neg = both ? uint(clamp(-d, 0.0, 4e6)) : 0u;
  const uint sq = both ? uint(min(d * d / 256.0, 65535.0)) : 0u;
  const uint ref = both ? uint(clamp(dot(Lf, k) * 256.0, 0.0, 4e6)) : 0u;
  const uint b = occluded ? kShFineOccN : kShFineN;
  sharc_stat(b, both);
  sharc_stat_sum(b + 1u, pos);
  sharc_stat_sum(b + 2u, neg);
  sharc_stat_sum(b + 3u, sq);
  sharc_stat_sum(b + 4u, ref);
}

#ifdef SHARC_RT_QUERY
// What the leak test found for the cell that supplied a cache hit (sharc_leak_check=1 or
// sharc_reject_occluded=1).
struct SharcCell {
  bool occluded;   // geometry lies between the query point and the cell (see sharc_cell_test)
  bool thin;       // the surface is thinner than the cell: its far side lies inside the cell
};

// The leak mechanism of a spatial cache: one cell averages every injected surface of the same
// normal octant inside it, so a cell that straddles a wall carries the far side's radiance to
// the near side. Two geometric tests, no tolerances:
//   occluded: the cell centre, projected into the plane of the query surface (so that the
//     query surface itself never counts), is not visible from the query point along that
//     plane: a wall, floor or ceiling crosses the cell between them (the typical case: the
//     interior floor and the sunlit ground outside, both facing up, in one cell at the foot
//     of a wall). The path runs half a cell edge above the plane (the cell's extent).
//   thin: a ray into the surface (along -n) leaves it again within one cell edge: the slab is
//     thinner than the cell. Its far side has the opposite normal octant, so this is reported,
//     not rejected.
SharcCell sharc_cell_test(float3 cam_abs, float3 p, float3 n, RtCone cone) {
  SharcCell c;
  float vox;
  const float3 pa = p + cam_abs;
  const int4 g = HashGridCalculatePositionLogWithVoxelSize(pa, sharc_grid(cam_abs), vox);
  const float3 centre = (float3(g.xyz) + 0.5) * vox - cam_abs;   // camera-relative
  // The test path runs half a cell edge above the plane: the cell's own extent along the normal,
  // so a wall that crosses the cell's upper half blocks it, while the query surface's own
  // curvature and tessellation within the cell do not. (The first in-game run lifted it by the
  // ULP offset only: the grazing ray then hit neighbouring triangles of curved and tessellated
  // surfaces, and 27-34% of the cache hits in Water Queen's House read as occluded.)
  const float h = 0.5 * vox;
  const float3 inplane = centre - n * dot(centre - p, n);
  const float3 o = rt_offset_ray(p, n) + n * h, e = inplane + n * h;
  const float3 d = e - o;
  const float len = length(d);
  c.occluded = len > 0.0 && rt_occluded(o, d / len, len, cone);
  c.thin = (shc.flags & 16u) != 0u && rt_occluded(rt_offset_ray(p, -n), -n, vox, cone);
  return c;
}

// A ray's hit at distance t (camera-relative point p) that no screen pixel shades: the
// cache's radiance there, unless the hit is closer than near_voxels voxels (the voxel would
// average the ray's own origin surface in: the old rule stays) or its normal is unknown.
// With sharc_reject_occluded=1 (flags bit 5) an entry whose cell is occluded (sharc_cell_test)
// is not used. `cell` says what the test found (all false when it did not run).
bool sharc_ray_hit(RtHit h, float3 p, float3 dir, float t, float3 cam_abs, RtCone cone, out float3 L, out bool near,
                   out SharcCell cell) {
  L = 0.0.xxx;
  near = false;
  cell.occluded = false;
  cell.thin = false;
  float3 ng = rt_hit_normal(h);
  if (!(dot(ng, ng) > 1e-30)) return false;
  const float3 pa = p + cam_abs;
  near = t < sharc_voxel_size(cam_abs, pa) * shc.near_voxels;
  if (near) return false;
  const float3 n = sharc_face(ng, dir);
  if (!sharc_lookup(cam_abs, pa, n, L)) return false;
  if ((shc.flags & (16u | 32u | 64u)) == 0u) return true;
  cell = sharc_cell_test(cam_abs, p, n, cone);
  sharc_fine_stat(cam_abs, pa, n, L, cell.occluded);
  sharc_stat(kShCellOccluded, cell.occluded);
  sharc_stat(kShCellThin, cell.thin);
  return !(cell.occluded && (shc.flags & 32u) != 0u);
}
#endif
