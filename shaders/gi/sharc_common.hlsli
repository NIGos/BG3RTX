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
  uint flags;             // bit 0 GI hidden hits query, 1 GI off-screen, 2 reflections, 3 statistics
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
  uint pad;
};
[[vk::binding(3, 4)]] ConstantBuffer<SharcConsts> shc;
// Device local: bytes 0-15 the camera (absolute) of the last injection, w = 1 once written;
// from 16 this frame's 16 counters (zeroed per frame, copied into a host ring by core/sharc.cpp).
[[vk::binding(4, 4)]] RWByteAddressBuffer shState;
#if !HASH_GRID_ENABLE_64_BIT_ATOMICS
[[vk::binding(5, 4)]] RWStructuredBuffer<uint> shLock;                        // 4 B per entry
#endif

static const uint kShInjected = 0, kShInsertFailed = 1, kShEmissive = 2, kShGiHidden = 3, kShGiOffscreen = 4,
                  kShGiQueried = 5, kShGiFound = 6, kShGiNear = 7, kShRfEligible = 8, kShRfFound = 9,
                  kShLive = 10, kShEvicted = 11, kShReset = 12, kShInjectSkipped = 13;

// Lanes with `cond` added to counter i of this frame's slot (sharc_stats=1).
void sharc_stat(uint i, bool cond) {
  if ((shc.flags & 8u) == 0u) return;
  uint n = WaveActiveCountBits(cond);
  if (WaveIsFirstLane() && n) shState.InterlockedAdd(shc.stats_off + i * 4u, n);
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

#ifdef SHARC_RT_QUERY
// A ray's hit at distance t (camera-relative point p) that no screen pixel shades: the
// cache's radiance there, unless the hit is closer than near_voxels voxels (the voxel would
// average the ray's own origin surface in: the old rule stays) or its normal is unknown.
bool sharc_ray_hit(RtHit h, float3 p, float3 dir, float t, float3 cam_abs, out float3 L, out bool near) {
  L = 0.0.xxx;
  near = false;
  float3 ng = rt_hit_normal(h);
  if (!(dot(ng, ng) > 1e-30)) return false;
  const float3 pa = p + cam_abs;
  near = t < sharc_voxel_size(cam_abs, pa) * shc.near_voxels;
  if (near) return false;
  return sharc_lookup(cam_abs, pa, sharc_face(ng, dir), L);
}
#endif
