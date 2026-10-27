// SHaRC radiance cache, v1: screen-injected (docs/DESIGN_SOTA_NEXT.md item 2; sharc=1).
//
// A world-space hash grid (NVIDIA SHaRC 1.8.3) that remembers the radiance of surfaces the
// camera has seen, so GI rays at hidden or off-screen hits and the glossy reflection
// fallbacks get a bounce instead of "occlusion without bounce" / the probe. The passes are
// in core/gi.cpp and shaders/gi/sharc_*.hlsl; this module owns the cache:
//   - its buffers (2^sharc_log2 entries: 8 B hash key + 16 B accumulation + 16 B resolved,
//     160 MiB at the default 2^22; + 4 B lock per entry without 64-bit atomics), device
//     local, cleared on allocation, when re-enabled and on a camera jump (in the resolve);
//   - set 4 of the passes (written once per allocation): the buffers, the per-frame
//     constants (a UBO updated with vkCmdUpdateBuffer) and a small state buffer (the last
//     injection's camera, this frame's counters, copied into a host ring for sharc_stats);
//   - GPU timing of the resolve and the injection ("sharc: resolve ... inject ..." log).
// It needs the device features the layer enables at device creation (BG3_DEVICE_INFO_KEY):
// shaderInt64, storageBuffer16BitAccess and shaderFloat16, and shaderBufferInt64Atomics
// (else the lock-buffer variant of the injection).
#pragma once
#include "core.h"

struct SharcFrame {
  VkDescriptorSet set;        // set 4, this allocation's
  uint32_t debug;             // sharc_debug
  uint32_t query;             // sharc_query bits: 1 GI hidden hits, 2 GI off-screen, 4 reflections
  uint32_t resolve_groups[2]; // resolve dispatch size (256 threads per group)
  bool lock;                  // inject through the lock buffer (sharc_inject_lock.hlsl)
};

// Set 4's layout, created on first call (the pipeline layouts need it even with sharc=0).
VkDescriptorSetLayout sharc_set_layout();
// The device can run the cache (logs once why not).
bool sharc_supported();
// Once per frame, before the resolve (run_pre): allocates or reallocates, clears what needs
// clearing, writes this frame's constants and zeroes its counters. False: the cache is off
// this frame (allocation failed, unsupported device).
bool sharc_begin(VkCommandBuffer cmd, SharcFrame* out);
// End of the frame's cache work (run_post, after the injection): this frame's counters
// into the host ring, read back a few frames later (sharc_stats=1).
void sharc_end(VkCommandBuffer cmd);
// GPU timestamps: 0/1 around the resolve, 2/3 around the injection.
void sharc_timestamp(VkCommandBuffer cmd, uint32_t index);
void sharc_release();
