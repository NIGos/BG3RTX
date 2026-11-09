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
  uint64_t empty_since = 0; // frame it was first seen empty (rt_as_chunk_reuse_frames), 0 = in use
  bool alloc(uint64_t size, uint64_t align, uint64_t* off) {
    if (!RangeAlloc::alloc(size, align, off)) return false;
    live += size;
    empty_since = 0;
    return true;
  }
  void mark_pending(uint64_t size) { live -= size; }
  void cancel(uint64_t off, uint64_t size) {
    mark_pending(size);
    release(off, size);
  }
  bool reclaimable() const { return cap && !used && !kept; }
  bool retained() const { return reclaimable() && empty_since; } // kept empty for reuse (any arena)
};

// rt_as_chunk_reuse_frames (opt-vram, 2026-11-08): an empty chunk stays allocated for this many frames
// and takes the next allocation that needs a new chunk, of any arena (it holds no range). The user's
// 2026-11-08 log: 1531 chunk allocations in 4 minutes (1056 of 64 MB), "freed 85 (5972 MB)" in one
// window: per-draw and cascade rebuilds empty a chunk, it is freed, the next frame allocates another.
// Reuse never adds a chunk: peak reservation is unchanged, the free/alloc pair (and the overlap of
// the deferred-freed chunk with its replacement) goes away. A VRAM guard ask frees every empty chunk.
inline bool keep_empty_chunk(ChunkRanges& r, uint64_t now, uint64_t frames, bool yield) {
  if (yield || !frames) return false;
  if (!r.empty_since) r.empty_since = now ? now : 1;
  return now - r.empty_since < frames;
}

// A new chunk for a caller rt_vram_guard does not ask (the cascade casters' BLAS, compaction copies of cached
// meshes and grooms): allowed only while the chunks stay within their cap for the budget (vram_guard::cap,
// UINT64_MAX with the guard off: unchanged). The guarded callers already stop at that cap, so the reserve
// can no longer pass it. User log 2026-11-09 08:02:05: 8196 MB in 91 chunks (15 MB live, 0 empty) against a
// cap of about 3.9 GB while the guard refused 64640 guarded requests; only these callers could add them.
inline bool unguarded_chunk_ok(uint64_t reserved, uint64_t chunk_bytes, uint64_t cap) {
  return chunk_bytes <= cap && reserved <= cap - chunk_bytes;
}

// rt_as_cap_occupied (claude/blas-frag, 2026-11-09, default 0). rv3 191231 key off (S8 zoomed-out turns, 19:15:01-14):
// 4189 MB reserved in 50 chunks, 1237 MB live, little pending, 0 empty, at the guard's cap (about 4013-4189 MB);
// 290-575 requests refused per window, all larger than a 64 MB base chunk: the cascade casters' BLAS (64-136 MB,
// rebuilt into a new range almost every frame) and grooms (72-136 MB). A range larger than `base` needs a chunk of
// its own and can never use the free space inside base chunks (sparse after the turns: about 26 MB live in each),
// so the free reserve did not help it. With the key such a chunk may pass the reserved cap while the occupied
// bytes (live + pending, every chunk) plus it stay within the cap, up to the cap + 25% reserved (the elastic
// captures' reserve). Without the key, or for base-sized chunks: reserved + chunk within the cap, as before
// (exactly unguarded_chunk_ok).
inline bool chunk_within_cap(uint64_t reserved, uint64_t occupied, uint64_t chunk, uint64_t base, uint64_t cap,
                             bool by_occupied) {
  if (chunk <= cap && reserved <= cap - chunk) return true;
  if (!by_occupied || chunk <= base || chunk > cap || cap > UINT64_MAX / 2) return false;
  const uint64_t ceiling = cap + cap / 4;
  return occupied <= cap - chunk && reserved <= ceiling - chunk;
}

// Pending ranges cannot be reused based on elapsed presents alone. The fence
// AND the oldest potentially referenced scene slot must have retired.
inline bool retired(uint64_t frame, uint64_t now, uint64_t gpu_done, uint64_t oldest_live) {
  return now >= frame && now - frame >= 6 && gpu_done >= frame &&
         gpu_done - frame >= 2 && frame < oldest_live;
}

// Try the fullest chunks first; skip holes (cap=0), preserve stable indices,
// and still try less-full chunks when alignment/fragmentation prevents a fit.
// Pending ranges count as occupied: they are not allocatable yet.
// A retained empty chunk (rt_as_chunk_reuse_frames: no range, none pending, none kept) is taken by
// any arena and joins it; with the key at 0 no chunk is retained and arenas never mix.
template<class Chunks>
int allocate_fullest(Chunks& chunks, uint64_t size, uint64_t align, uint64_t* off, int excluded = -1,
                     uint32_t arena = 0) {
  std::vector<size_t> order;
  order.reserve(chunks.size());
  for (size_t i = 0; i < chunks.size(); ++i)
    if (int(i) != excluded && chunks[i].ra.cap &&
        (arena == UINT32_MAX || chunks[i].ra.arena == arena || chunks[i].ra.retained()) &&
        chunks[i].ra.cap - chunks[i].ra.used >= size) order.push_back(i);
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const double x = double(chunks[a].ra.used) / double(chunks[a].ra.cap);
    const double y = double(chunks[b].ra.used) / double(chunks[b].ra.cap);
    return x != y ? x > y : a < b;
  });
  for (size_t i : order)
    if (chunks[i].ra.alloc(size, align, off)) {
      if (arena != UINT32_MAX) chunks[i].ra.arena = arena;   // same arena, or an empty chunk joining it
      return int(i);
    }
  return -1;
}
} // namespace bg3::as_chunks
