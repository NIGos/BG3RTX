#pragma once
// Single device-local allocation: header, stable-slot queue, ray records, pixel tails.
#define GI_SUN_WAVE_SPEC_ID 1034
#define GI_SUN_WAVE_HEADER_BYTES 16u
#define GI_SUN_WAVE_RECORD_BYTES 192u
#define GI_SUN_WAVE_PIXEL_BYTES 32u
#ifdef __cplusplus
#include <cstdint>
#include <limits>
namespace bg3::sun_wave {
// All shader addresses and queue slots are uint32. Validate before multiplying.
constexpr uint64_t allocation_bytes(uint32_t width,uint32_t height,uint32_t rays) {
  constexpr uint64_t limit=std::numeric_limits<uint32_t>::max();
  const uint64_t pixels=uint64_t(width)*height;
  if(!pixels || !rays || pixels>limit || rays>limit/pixels)return 0;
  const uint64_t slots=pixels*rays;
  const uint64_t bytes=GI_SUN_WAVE_HEADER_BYTES+slots*(4u+GI_SUN_WAVE_RECORD_BYTES)+pixels*GI_SUN_WAVE_PIXEL_BYTES;
  return bytes<=limit?bytes:0;
}
}
#endif
