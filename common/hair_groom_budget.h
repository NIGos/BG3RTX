#pragma once
// rt_hair_groom mesh-pool budget (layer/bvhsplit.cpp groom_*): groom arrays share the mesh pool with the cached
// meshes, so they get at most a quarter of its capacity, and unused LODs are evicted oldest first to make room.
#include <algorithm>
#include <cstdint>
#include <vector>

namespace bg3::hair_groom {

// A quarter of the mesh pool's capacity (the larger of the configured cap and the device-budget cap is never
// used: the pool itself is limited by the smaller one). The rest stays for the cached meshes, which filled the
// pool to 2047 MB in Test Dialogue3 without any groom.
inline uint64_t pool_budget(uint64_t pool_capacity) { return pool_capacity / 4; }

struct Resident {
  uint64_t key = 0;        // the LOD (groom id, LOD index) or a slot's points
  uint64_t bytes = 0;
  uint64_t last_used = 0;  // frame
  bool pinned = false;     // used by this frame's instances: never evicted
};

// The residents to evict (oldest last use first, unpinned, unused for at least min_age frames) so that
// used - freed + need <= budget. False when even evicting every candidate cannot make room: the caller then
// keeps the card path for that draw and evicts nothing.
inline bool plan_evictions(std::vector<Resident> residents, uint64_t used, uint64_t need, uint64_t budget,
                           uint64_t now, uint64_t min_age, std::vector<uint64_t>& evict) {
  evict.clear();
  if (need > budget) return false;
  if (used + need <= budget) return true;
  std::sort(residents.begin(), residents.end(), [](const Resident& a, const Resident& b) { return a.last_used < b.last_used; });
  uint64_t freed = 0;
  for (const Resident& r : residents) {
    if (used - freed + need <= budget) break;
    if (r.pinned || now < r.last_used + min_age) continue;
    evict.push_back(r.key);
    freed += r.bytes;
  }
  if (used - freed + need <= budget) return true;
  evict.clear();
  return false;
}

}  // namespace bg3::hair_groom
