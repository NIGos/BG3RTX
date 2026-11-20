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
  // rt_mesh_pool_trim: the start of the free block that ends at the capacity (cap when the last byte is in use),
  // and cutting the capacity down to ncap >= that start. No range moves.
  uint64_t tail() const {
    if (free.empty()) return cap;
    const auto last = std::prev(free.end());
    return last->first + last->second == cap ? last->first : cap;
  }
  bool shrink(uint64_t ncap) {
    if (ncap >= cap) return ncap == cap;
    if (ncap < tail()) return false;
    const auto last = std::prev(free.end());
    const uint64_t start = last->first;
    free.erase(last);
    if (ncap > start) free[start] = ncap - start;
    cap = ncap;
    return true;
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
//
// rt_as_chunk_fit (claude/vram-rightsize, 2026-11-14, default 0). A range larger than a base chunk gets a chunk
// of its own, sized exactly; the cascade casters' BLAS (arena 2, 64-136 MB) is rebuilt into a new range whenever
// its draw list changes, each time a slightly different size. A retained empty chunk only takes a range that
// fits, so a growing BLAS found every retained chunk too small and allocated another: hq-s8-ultra 2026-11-14
// 21:54:48-51, 448 -> 1770 MB in 7 -> 25 chunks for 356 MB live (freed chunks of 67.0, 67.2, 72.1, 72.6, 73.8,
// 79.2 MB in the windows after), and the user's play log the same night: 99 to 1214 MB of empty chunks in 15 of
// 19 samples. With the key such a chunk is sized size + 1/8, rounded up to base / 4 (oversize_chunk_bytes: the
// elastic captures' growth step), so the next rebuilds fit the retained chunk; and among retained empty chunks
// the smallest that fits is taken (allocate_fullest best_fit), so a base-sized range does not occupy a large one.
// Cost bound: at most size / 8 + base / 4 unused per chunk of the cycle (live + pending ranges, 2 to 8 chunks).
inline uint64_t oversize_chunk_bytes(uint64_t size, uint64_t base, bool rebuilt, bool fit) {
  const uint64_t exact = (std::max)(base, size);
  if (!fit || !rebuilt || size <= base || size > UINT64_MAX / 2) return exact;
  const uint64_t q = (std::max)(base / 4, uint64_t(1));
  return (size + size / 8 + q - 1) / q * q;
}

// rt_as_size_class (claude/r2-layer, 2026-11-20, default 0): a range of at most a base chunk only goes into chunks
// of at most a base chunk, a larger range only into larger chunks (same_class). The user's log of 2026-11-20
// 16:47-16:48 (Ultra, a dolly at the inn): 137 of 162 BLAS chunk refusals were the cascade casters' BLAS (99-189 MB)
// at the chunks' cap with 4.0 GB reserved in 25-31 chunks (mean 155 MB, so most of them larger than a base chunk)
// for 0.55-1.35 GB live, 0 empty at 16:48:11. A chunk made for a cascade or groom BLAS is taken, once retained
// empty or through its tail, by per-draw and cached ranges of 0.1-16 MB (the fullest chunk first, any arena may
// join a retained one); one surviving small range then pins 100-190 MB that no later range larger than 64 MB can
// use, so the next cascade rebuild needs a new chunk past the cap. With the key small ranges never enter a large
// chunk: it empties when its one range retires, and is reused by the next large range or freed at the cap check.
inline bool same_class(uint64_t chunk_cap, uint64_t size, uint64_t base) {
  return !base || (size > base) == (chunk_cap > base);
}

template<class Chunks>
int allocate_fullest(Chunks& chunks, uint64_t size, uint64_t align, uint64_t* off, int excluded = -1,
                     uint32_t arena = 0, bool best_fit = false, uint64_t class_base = 0) {
  std::vector<size_t> order;
  order.reserve(chunks.size());
  for (size_t i = 0; i < chunks.size(); ++i)
    if (int(i) != excluded && chunks[i].ra.cap &&
        (arena == UINT32_MAX || chunks[i].ra.arena == arena || chunks[i].ra.retained()) &&
        same_class(chunks[i].ra.cap, size, class_base) &&
        chunks[i].ra.cap - chunks[i].ra.used >= size) order.push_back(i);
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const double x = double(chunks[a].ra.used) / double(chunks[a].ra.cap);
    const double y = double(chunks[b].ra.used) / double(chunks[b].ra.cap);
    if (best_fit && !chunks[a].ra.used && !chunks[b].ra.used && chunks[a].ra.cap != chunks[b].ra.cap)
      return chunks[a].ra.cap < chunks[b].ra.cap;   // empty chunks: the smallest that fits
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
