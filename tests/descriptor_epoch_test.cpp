#include "../common/descriptor_epoch.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

struct Tracker {
  std::unordered_map<uint64_t, std::unordered_map<uint32_t, uint64_t>> sets;
  std::unordered_map<uint64_t, uint32_t> dynamic_counts;
  std::unordered_map<uint64_t, uint64_t> set_layouts;
  std::unordered_map<uint64_t, uint64_t> set_pools;
  std::unordered_map<uint64_t, std::vector<uint64_t>> pool_sets;
  std::unordered_map<uint64_t, size_t> pool_stale;
};

static bool forgotten(const Tracker& t, uint64_t s) {
  return !t.sets.count(s) && !t.dynamic_counts.count(s) && !t.set_layouts.count(s) && !t.set_pools.count(s);
}

// claude/final-layer 2026-11-20: freed sets and the sets of reset or destroyed pools leave every map.
static int pools() {
  Tracker t;
  const uint64_t A = 1000, B = 2000;
  for (uint64_t s : {1, 2, 3}) descriptor_set_allocated(t, s, uint64_t(7), A);
  descriptor_set_allocated(t, uint64_t(4), uint64_t(8), B);
  t.sets[1][5] = 1; t.sets[2][5] = 2; t.dynamic_counts[2] = 1; t.sets[4][5] = 4;
  const uint64_t freed[] = {2, 0};   // a null handle is allowed and ignored
  descriptor_sets_freed(t, A, freed, 2);
  if (!forgotten(t, 2) || t.sets.at(1).at(5) != 1 || t.set_layouts.at(3) != 7) return 10;
  // Freeing through the wrong pool forgets nothing.
  const uint64_t other[] = {4};
  descriptor_sets_freed(t, A, other, 1);
  if (t.sets.at(4).at(5) != 4 || t.set_pools.at(4) != B) return 11;
  // Handle 2 comes back from pool B, then A is reset: the set of B stays.
  descriptor_set_allocated(t, uint64_t(2), uint64_t(8), B);
  t.sets[2][6] = 22;
  descriptor_pool_reset(t, A);
  if (!forgotten(t, 1) || !forgotten(t, 3) || t.sets.at(2).at(6) != 22 || t.set_pools.at(2) != B) return 12;
  if (t.pool_sets.count(A) || t.pool_stale.count(A)) return 13;
  // A destroyed pool (the same call) forgets the rest.
  descriptor_pool_reset(t, B);
  if (!t.sets.empty() || !t.dynamic_counts.empty() || !t.set_layouts.empty() || !t.set_pools.empty() ||
      !t.pool_sets.empty()) return 14;
  // Free and allocate the same handle 10000 times from one pool: the pool's list stays bounded.
  descriptor_set_allocated(t, uint64_t(9), uint64_t(7), A);
  for (int i = 0; i < 10000; ++i) {
    const uint64_t h[] = {uint64_t(50 + i % 3)};
    descriptor_set_allocated(t, h[0], uint64_t(7), A);
    descriptor_sets_freed(t, A, h, 1);
  }
  if (t.pool_sets.at(A).size() > 4 || t.set_pools.size() != 1 || t.set_layouts.size() != 1) return 15;
  descriptor_pool_reset(t, A);
  if (!t.set_pools.empty() || !t.set_layouts.empty() || !t.pool_sets.empty() || !t.pool_stale.empty()) return 16;
  return 0;
}

int main() {
  if (int r = pools()) return r;
  Tracker t;
  descriptor_set_allocated(t, uint64_t(42), uint64_t(7));
  if (t.set_layouts.at(42) != 7 || t.sets.count(42) || t.dynamic_counts.count(42)) return 1;

  // A retired UI set had a profile-sized buffer, an image and dynamic metadata.
  t.sets[42] = {{28, 900}, {42, 700}};
  t.dynamic_counts[42] = 1;
  t.sets[99] = {{13, 901}};
  t.dynamic_counts[99] = 2;
  t.set_layouts[99] = 8;
  descriptor_set_allocated(t, uint64_t(42), uint64_t(9));
  if (t.sets.count(42) || t.dynamic_counts.count(42) || t.set_layouts.at(42) != 9) return 2;
  if (t.sets.at(99).at(13) != 901 || t.dynamic_counts.at(99) != 2 || t.set_layouts.at(99) != 8) return 3;

  // Partial writes within a lifetime keep the other entries. Reallocation with
  // the SAME layout must still discard all of that lifetime's descriptors.
  t.sets[42][13] = 1000;
  t.sets[42][28] = 1001;
  if (t.sets.at(42).at(13) != 1000) return 4;
  descriptor_set_allocated(t, uint64_t(42), uint64_t(9));
  if (t.sets.count(42) || t.dynamic_counts.count(42) || t.set_layouts.at(42) != 9) return 5;
  t.sets[42][13] = 1002;
  if (t.sets.at(42).count(28) || t.sets.at(42).count(42) || t.sets.at(42).at(13) != 1002) return 6;

  // All returned handles in a batch get a fresh lifetime, not only the first.
  for (uint64_t set : {uint64_t(42), uint64_t(99)}) descriptor_set_allocated(t, set, uint64_t(10));
  if (!t.sets.empty() || !t.dynamic_counts.empty() || t.set_layouts.at(42) != 10 || t.set_layouts.at(99) != 10) return 7;
  return 0;
}
