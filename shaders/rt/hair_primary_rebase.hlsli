// Strand passes of the game's frame (G-buffer, velocity, coverage probe): the groom publish they read is the previous
// frame's (docs/RTX_HAIR_PRIMARY.md 5b, 2026-11-04). Its own header so the other hair_primary.hlsli users keep their
// preprocessed text (no recompile, no identity change).
#pragma once
#include "hair_primary.hlsli"

static const uint hair_primary_flag_camera = 2u;
static const uint hair_primary_camera_offset = 3136u;   // common/hair_primary.h kCameraOffset
static const uint hair_primary_flag_prev_camera = 4u;
static const uint hair_primary_prev_camera_offset = 3152u;   // kPrevCameraOffset
static const uint hair_primary_ray_debug_offset = 3168u;   // kRayDebugOffset
// The ray origin of this frame's camera in the published groom's camera-relative frame (the publish of the previous
// frame: the scene build is recorded after the G-buffer pass). false: no build camera, the caller keeps no strand.
bool hp_rebase(uint64_t rec, float3 cam_now, out float3 o) {
  o = 0.0.xxx;
  if ((vk::RawBufferLoad<uint>(rec + 60u, 4) & hair_primary_flag_camera) == 0u) return false;
  o = cam_now - asfloat(vk::RawBufferLoad<uint3>(rec + hair_primary_camera_offset, 4));
  return all(isfinite(o));
}

// The previous publish's build camera (the origin of the rows' previous points). false: not known.
bool hp_prev_build_camera(uint64_t rec, out float3 c) {
  c = 0.0.xxx;
  if ((vk::RawBufferLoad<uint>(rec + 60u, 4) & hair_primary_flag_prev_camera) == 0u) return false;
  c = asfloat(vk::RawBufferLoad<uint3>(rec + hair_primary_prev_camera_offset, 4));
  return all(isfinite(c));
}

// Breadcrumbs (common/hair_primary.h kCrumbAddrOffset): a violation of [lo, hi) for `bytes` at addr is recorded as
// (site, address) at crumbs + 16 site, and the caller skips the access (fail closed).
static const uint hair_primary_crumb_offset = 3232u;
uint64_t hp_crumbs(uint64_t rec) {
  const uint2 a = vk::RawBufferLoad<uint2>(rec + hair_primary_crumb_offset, 8);
  return (uint64_t(a.y) << 32) | uint64_t(a.x);
}
bool hp_crumb_ok(uint64_t crumbs, uint site, uint64_t addr, uint64_t bytes, uint64_t lo, uint64_t hi) {
  if (addr >= lo && addr + bytes <= hi) return true;
  if (crumbs != 0u) vk::RawBufferStore<uint4>(crumbs + uint64_t(site) * 16u, uint4(site, uint(addr), uint(addr >> 32), 1u), 16);
  return false;
}
// The last value of a used address (site 16 + n: TLAS, pool, pixels), last writer wins.
void hp_crumb_note(uint64_t crumbs, uint n, uint64_t value) {
  if (crumbs != 0u) vk::RawBufferStore<uint4>(crumbs + uint64_t(16u + n) * 16u, uint4(16u + n, uint(value), uint(value >> 32), 2u), 16);
}
