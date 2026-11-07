// Memory type choice for host-visible, host-coherent buffers (layer/bvhsplit.cpp make_buf,
// layer/hit_material.cpp create). Vulkan-free so the CTest needs no SDK.
//
// VRAM through the BAR (DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT) is tried first: with Resizable BAR
// it is all of VRAM. Without Resizable BAR it is a separate ~214 MiB heap that the game and driver
// also fill (vulkaninfo on the user's 5090 after the 2026-11-07 reinstall: heap 2, 214 MiB, 157 MiB
// budget on the desktop). An allocation there can fail with plenty of VRAM free; system memory
// (HOST_VISIBLE|HOST_COHERENT, not DEVICE_LOCAL) is then tried, as it already was when no BAR type
// exists. Before this, a failed BAR allocation failed the buffer: the BVH split's 3 MB instance ring
// did (2026-11-07 hair-nojoin log), the split turned off and no hair groom ever joined a draw.
#pragma once
#include <cstdint>

namespace bg3::host_memory {

constexpr uint32_t kNoType = UINT32_MAX;
// VkMemoryPropertyFlagBits values.
constexpr uint32_t kDeviceLocal = 0x1, kHostVisible = 0x2, kHostCoherent = 0x4;
constexpr uint32_t kBar = kDeviceLocal | kHostVisible | kHostCoherent, kSystem = kHostVisible | kHostCoherent;

// The first type allowed by `bits` whose flags hold `want` and none of `exclude`.
inline uint32_t find_type(const uint32_t* type_flags, uint32_t count, uint32_t bits, uint32_t want, uint32_t exclude = 0) {
  for (uint32_t i = 0; i < count && i < 32; ++i)
    if ((bits & (1u << i)) && (type_flags[i] & want) == want && !(type_flags[i] & exclude)) return i;
  return kNoType;
}

// alloc(type) -> true when the allocation from that type succeeded. Returns the type allocated
// from, or kNoType; *fell_back is set when a BAR type existed, failed, and system memory took it.
template <class Alloc>
uint32_t allocate_host_visible(const uint32_t* type_flags, uint32_t count, uint32_t bits, Alloc&& alloc,
                               bool* fell_back = nullptr) {
  if (fell_back) *fell_back = false;
  const uint32_t bar = find_type(type_flags, count, bits, kBar);
  if (bar != kNoType && alloc(bar)) return bar;
  uint32_t sys = find_type(type_flags, count, bits, kSystem, kDeviceLocal);
  if (sys == kNoType && bar == kNoType) sys = find_type(type_flags, count, bits, kSystem);
  if (sys == kNoType || !alloc(sys)) return kNoType;
  if (fell_back) *fell_back = bar != kNoType;
  return sys;
}

}  // namespace bg3::host_memory
