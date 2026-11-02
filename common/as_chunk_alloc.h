#pragma once
// CPU-only BLAS chunk bookkeeping, shared by bvhsplit and its unit test.
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <iterator>
#include <map>
#include <vector>

namespace bg3::as_chunks {
// ---- a first-fit range allocator ----
struct RangeAlloc {
  uint64_t cap = 0, used = 0;
  std::map<uint64_t, uint64_t> free;   // offset -> size
  void init(uint64_t c) {
    cap = c;
    used = 0;
    free.clear();
    if (c) free[0] = c;
  }
  bool alloc(uint64_t size, uint64_t align, uint64_t* off) {
    for (auto it = free.begin(); it != free.end(); ++it) {
      uint64_t s0 = it->first, e0 = it->first + it->second, a = (s0 + align - 1) / align * align;
      if (a + size > e0) continue;
      free.erase(it);
      if (a > s0) free[s0] = a - s0;
      if (a + size < e0) free[a + size] = e0 - (a + size);
      *off = a;
      used += size;
      return true;
    }
    return false;
  }
  void release(uint64_t off, uint64_t size) {
    used -= (std::min)(used, size);
    auto next = free.lower_bound(off);
    if (next != free.end() && off + size == next->first) {
      size += next->second;
      next = free.erase(next);
    }
    if (next != free.begin()) {
      auto prev = std::prev(next);
      if (prev->first + prev->second == off) {
        prev->second += size;
        return;
      }
    }
    free[off] = size;
  }
  void grow(uint64_t ncap) {
    if (ncap <= cap) return;
    uint64_t old = cap;
    cap = ncap;
    used += ncap - old;   // release() subtracts it again
    release(old, ncap - old);
  }
};


// used includes pending GPU retirement; live excludes it. Kept diagnostic AS
// handles pin their backing chunk even if rt_as_keep is subsequently disabled.
struct ChunkRanges : RangeAlloc {
  uint32_t arena = 0; // cached mesh, per-draw, or cascade lifetime; never move live ranges
  uint64_t live = 0;
  size_t kept = 0;
  bool alloc(uint64_t size, uint64_t align, uint64_t* off) {
    if (!RangeAlloc::alloc(size, align, off)) return false;
    live += size;
    return true;
  }
  void mark_pending(uint64_t size) { live -= size; }
  void cancel(uint64_t off, uint64_t size) {
    mark_pending(size);
    release(off, size);
  }
  bool reclaimable() const { return cap && !used && !kept; }
};

// Pending ranges cannot be reused based on elapsed presents alone. The fence
// AND the oldest potentially referenced scene slot must have retired.
inline bool retired(uint64_t frame, uint64_t now, uint64_t gpu_done, uint64_t oldest_live) {
  return now >= frame && now - frame >= 6 && gpu_done >= frame &&
         gpu_done - frame >= 2 && frame < oldest_live;
}

// Try the fullest chunks first; skip holes (cap=0), preserve stable indices,
// and still try less-full chunks when alignment/fragmentation prevents a fit.
// Pending ranges count as occupied: they are not allocatable yet.
template<class Chunks>
int allocate_fullest(Chunks& chunks, uint64_t size, uint64_t align, uint64_t* off, int excluded = -1,
                     uint32_t arena = 0) {
  std::vector<size_t> order;
  order.reserve(chunks.size());
  for (size_t i = 0; i < chunks.size(); ++i)
    if (int(i) != excluded && chunks[i].ra.cap &&
        (arena == UINT32_MAX || chunks[i].ra.arena == arena) &&
        chunks[i].ra.cap - chunks[i].ra.used >= size) order.push_back(i);
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const double x = double(chunks[a].ra.used) / double(chunks[a].ra.cap);
    const double y = double(chunks[b].ra.used) / double(chunks[b].ra.cap);
    return x != y ? x > y : a < b;
  });
  for (size_t i : order)
    if (chunks[i].ra.alloc(size, align, off)) return int(i);
  return -1;
}
} // namespace bg3::as_chunks
