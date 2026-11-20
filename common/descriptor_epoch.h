#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

// Called under the tracker's exclusive lock after a successful allocation.
// A recycled handle identifies a new set, even when its layout is unchanged.
template <class Tracker, class Set, class Layout>
void descriptor_set_allocated(Tracker& tracked, Set set, Layout layout) {
  tracked.sets.erase(set);
  tracked.dynamic_counts.erase(set);
  tracked.set_layouts[set] = layout;
}

// The same, recording the pool, so that vkFreeDescriptorSets, vkResetDescriptorPool and vkDestroyDescriptorPool can
// forget the set (claude/final-layer, 2026-11-20). Before, `sets`, `dynamic_counts` and `set_layouts` were erased only
// when a handle value came back from a new allocation: a freed set whose handle never returned (a destroyed pool) kept
// its descriptors for the session. A pool's list may hold handles freed or reallocated from another pool since: they
// are erased only while `set_pools` still names this pool.
template <class Tracker, class Set, class Layout, class Pool>
void descriptor_set_allocated(Tracker& tracked, Set set, Layout layout, Pool pool) {
  descriptor_set_allocated(tracked, set, layout);
  tracked.set_pools[set] = pool;
  tracked.pool_sets[pool].push_back(set);
}

template <class Tracker, class Set>
void descriptor_set_forget(Tracker& tracked, Set set) {
  tracked.sets.erase(set);
  tracked.dynamic_counts.erase(set);
  tracked.set_layouts.erase(set);
  tracked.set_pools.erase(set);
}

// vkFreeDescriptorSets: null handles are ignored (the API allows them). A pool's list is compacted once the sets
// freed since its last compaction are half of it, so freeing sets one by one from a long-lived pool does not grow the
// list, at an amortised constant cost per freed set.
template <class Tracker, class Pool, class Set>
void descriptor_sets_freed(Tracker& tracked, Pool pool, const Set* sets, size_t count) {
  size_t freed = 0;
  for (size_t i = 0; i < count; ++i) {
    const auto it = tracked.set_pools.find(sets[i]);
    if (!sets[i] || it == tracked.set_pools.end() || it->second != pool) continue;
    descriptor_set_forget(tracked, sets[i]);
    ++freed;
  }
  const auto list = tracked.pool_sets.find(pool);
  if (list == tracked.pool_sets.end()) return;
  size_t& stale = tracked.pool_stale[pool];
  stale += freed;
  if (stale * 2 < list->second.size()) return;
  stale = 0;
  std::vector<Set> kept;
  for (const Set s : list->second) {
    const auto it = tracked.set_pools.find(s);
    if (it != tracked.set_pools.end() && it->second == pool) kept.push_back(s);
  }
  // A handle freed and allocated again from this pool is listed twice: keep one.
  std::sort(kept.begin(), kept.end());
  kept.erase(std::unique(kept.begin(), kept.end()), kept.end());
  if (kept.empty()) {
    tracked.pool_sets.erase(list);
    tracked.pool_stale.erase(pool);
  } else list->second = std::move(kept);
}

// vkResetDescriptorPool and vkDestroyDescriptorPool: every set allocated from the pool is gone.
template <class Tracker, class Pool>
void descriptor_pool_reset(Tracker& tracked, Pool pool) {
  const auto list = tracked.pool_sets.find(pool);
  if (list == tracked.pool_sets.end()) return;
  for (const auto s : list->second) {
    const auto it = tracked.set_pools.find(s);
    if (it != tracked.set_pools.end() && it->second == pool) descriptor_set_forget(tracked, s);
  }
  tracked.pool_sets.erase(list);
  tracked.pool_stale.erase(pool);
}
