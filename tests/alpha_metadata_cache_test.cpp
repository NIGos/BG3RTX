#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
using uint = uint32_t;
struct AlphaBuffer {
  uint bytes = 0, dimensions = 0, loads = 0;
  std::array<uint, 3> header{};
  void GetDimensions(uint& value) { ++dimensions; value = bytes; }
  uint Load(uint address) { ++loads; return header.at(address / 4u); }
} gAlphaRec;
// Exercise the production shader helpers, not a second cache implementation.
#include "../shaders/rt/alpha_metadata_cache.hlsli"
static void check(bool value) { if (!value) std::abort(); }
static uint rng = 0x12345678u;
static uint random_word() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
int main() {
  const std::array<uint, 12> sizes{0u, 4u, 8u, 15u, 16u, 207u, 208u, 223u, 224u, 432u, 0xfffffffeu, 0xffffffffu};
  uint64_t checks = 0;
  for (uint dispatch = 0; dispatch != 10000u; ++dispatch) {
    gAlphaRec.bytes = dispatch < sizes.size() ? sizes[dispatch] : random_word();
    for (auto& word : gAlphaRec.header) word = random_word();
    // Match HLSL's per-invocation initialization. Include entirely different
    // descriptor contents on every dispatch to catch accidental stale reuse.
    gAlphaMetadataValid = 0u;
    kSpecAlphaMetadataCache = 1u;
    gAlphaRec.dimensions = gAlphaRec.loads = 0u;
    for (uint candidate = 0; candidate != 128u; ++candidate) {
      uint bytes = alpha_record_bytes(), count = alpha_record_count();
      uint stride = alpha_record_stride(), flags = alpha_record_policy();
      check(bytes == gAlphaRec.bytes && count == gAlphaRec.header[0] &&
            stride == gAlphaRec.header[1] && flags == gAlphaRec.header[2]);
      // Exact original overflow-safe range/count expressions, including end
      // points and uint wraparound. These are all integers: tolerance is zero.
      uint address = candidate < 4u ? std::array<uint, 4>{0u, 16u, bytes - 208u, bytes - 207u}[candidate] : random_word();
      check((address >= 16u && bytes >= 208u && address <= bytes - 208u) ==
            (address >= 16u && gAlphaRec.bytes >= 208u && address <= gAlphaRec.bytes - 208u));
      uint capacity = bytes >= 16u ? (bytes - 16u) / 208u : 0u;
      uint original_capacity = gAlphaRec.bytes >= 16u ? (gAlphaRec.bytes - 16u) / 208u : 0u;
      check((count < capacity ? count : capacity) ==
            (gAlphaRec.header[0] < original_capacity ? gAlphaRec.header[0] : original_capacity));
      checks += 3u;
    }
    check(gAlphaRec.dimensions == 1u && gAlphaRec.loads == 3u);
    kSpecAlphaMetadataCache = 0u;
    for (uint repeat = 0; repeat != 3u; ++repeat) {
      check(alpha_record_bytes() == gAlphaRec.bytes);
      check(alpha_record_count() == gAlphaRec.header[0]);
      check(alpha_record_stride() == gAlphaRec.header[1]);
      check(alpha_record_policy() == gAlphaRec.header[2]);
    }
    check(gAlphaRec.dimensions == 4u && gAlphaRec.loads == 12u);
  }
  // Lazy range lookup must not introduce a header read on malformed/short data.
  gAlphaMetadataValid = 0u; kSpecAlphaMetadataCache = 1u;
  gAlphaRec.bytes = 0u; gAlphaRec.loads = 0u;
  check(alpha_record_bytes() == 0u && gAlphaRec.loads == 0u);
  std::printf("alpha metadata: %llu exact integer output checks; 10000 descriptor epochs; cached 1 dimensions + 3 header loads per invocation\n", checks);
}
