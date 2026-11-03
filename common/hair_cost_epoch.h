#pragma once
#include <cstdint>

// GPU timing buckets must contain one hair configuration, including queries
// recorded before a toggle but returned by the GPU afterwards.
template<class Read>
inline uint32_t hair_cost_mask(Read read) {
  constexpr const char* keys[] = {"rt_native_hair", "rt_native_scalp", "rt_hair_physical_origin",
    "rt_hair_gi_cull", "rt_hair_pool_trim", "rt_hair_cheap", "rt_hair_gi_mean", "rt_hair_visibility"};
  uint32_t mask = 0;
  for (uint32_t i=0;i<8;++i) if (read(keys[i],0)!=0) mask |= 1u<<i;
  return mask;
}
struct HairCostEpoch {
  uint32_t mask = 0;
  uint64_t generation = 0;
  bool update(uint32_t next) {
    if (next == mask) return false;
    mask = next; ++generation; return true;
  }
  bool accepts(uint64_t recorded) const { return recorded == generation; }
};
