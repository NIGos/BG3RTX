#include "../common/as_chunk_alloc.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3::as_chunks;
static unsigned checks;
static void check(bool ok, int line) {
  ++checks;
  if (!ok) { std::fprintf(stderr, "AS chunk check at line %d failed\n", line); std::exit(1); }
}
#define CHECK(x) check((x), __LINE__)
struct Chunk { ChunkRanges ra; };
struct MotionResult { uint64_t reserved, occupied, live, peak; };
// Increasing camera-turn rebuild sizes leave successively larger holes. A tiny
// cached compacted mesh placed in a tail outlives the rebuild and pins 64 MB.
static MotionResult motion(bool separate) {
  constexpr uint64_t MB = 1ull << 20, chunk_size = 64 * MB;
  struct Pending { unsigned frame; int chunk; uint64_t off, size; };
  std::vector<Chunk> chunks;
  std::vector<Pending> pending;
  MotionResult result{};
  for (unsigned frame = 0; frame < 96; ++frame) {
    for (auto it = pending.begin(); it != pending.end();) {
      if (!retired(it->frame, frame, frame, frame)) { ++it;continue; }
      chunks[it->chunk].ra.release(it->off, it->size);
      it = pending.erase(it);
    }
    for (auto& c : chunks) if (c.ra.reclaimable()) c.ra = {};
    auto allocate = [&](uint64_t size, uint32_t arena) {
      uint64_t off = 0;
      int ci = allocate_fullest(chunks, size, 256, &off, -1, separate ? arena : UINT32_MAX);
      if (ci < 0) {
        Chunk c;c.ra.init(chunk_size);c.ra.arena = arena;
        CHECK(c.ra.alloc(size, 256, &off));
        chunks.push_back(c);ci = int(chunks.size() - 1);
      }
      return Pending{frame, ci, off, size};
    };
    const auto transient = allocate(32 * MB + frame * MB / 4, 1);
    // Old range still occupies bytes for six presents AND GPU/scene retirement.
    chunks[transient.chunk].ra.mark_pending(transient.size);
    pending.push_back(transient);
    allocate(MB / 4, 0); // persistent compacted destination, never released in turn
    result.reserved = result.occupied = result.live = 0;
    for (const auto& c : chunks) {
      result.reserved += c.ra.cap;result.occupied += c.ra.used;result.live += c.ra.live;
    }
    result.peak = (std::max)(result.peak, result.reserved);
    if (separate) {
      // At most six in-flight rebuild chunks and one ordinary cache chunk.
      CHECK(result.reserved <= 7 * chunk_size);
      CHECK(result.reserved >= result.occupied && result.occupied >= result.live);
    }
  }
  return result;
}
int main() {
  uint64_t off = 0;
  // Live ownership ends immediately, but occupied bytes survive all pending
  // frames. A long GPU stall must never turn into a free at 240 presents.
  ChunkRanges r;
  r.init(1024);
  CHECK(r.reclaimable());
  CHECK(r.alloc(256, 256, &off) && off == 0);
  CHECK(r.live == 256 && r.used == 256 && !r.reclaimable());
  r.mark_pending(256);
  CHECK(!r.live && r.used == 256 && !r.reclaimable());
  CHECK(!retired(10, 15, 12, 11)); // CPU delay
  CHECK(!retired(10, 16, 11, 11)); // GPU fence
  CHECK(!retired(10, 16, 12, 10)); // old TLAS references (equality included)
  CHECK(!retired(10, 1000, 11, 11)); // no timeout-based release
  CHECK(retired(10, 16, 12, 11));
  CHECK(!retired(10, 9, 12, 11));
  CHECK(!retired(UINT64_MAX - 1, UINT64_MAX, UINT64_MAX, UINT64_MAX));
  r.release(off, 256);
  CHECK(!r.live && !r.used && r.reclaimable());
  CHECK(r.free.size() == 1 && r.free[0] == 1024);
  ++r.kept;
  CHECK(!r.reclaimable()); // retained handles pin the buffer across key toggles
  --r.kept;
  CHECK(r.reclaimable());
  // A failed Vulkan AS creation cancels both live and occupied bytes.
  CHECK(r.alloc(512, 256, &off));
  r.cancel(off, 512);
  CHECK(!r.live && !r.used && r.reclaimable());
  r = ChunkRanges{};
  CHECK(!r.reclaimable()); // freed slot, no second free

  std::vector<Chunk> chunks(4);
  for (auto& c : chunks) c.ra.init(1024);
  CHECK(chunks[0].ra.alloc(256, 256, &off));
  CHECK(chunks[1].ra.alloc(768, 256, &off));
  CHECK(chunks[2].ra.alloc(512, 256, &off));
  chunks[3].ra = ChunkRanges{}; // stable hole
  CHECK(allocate_fullest(chunks, 256, 256, &off) == 1 && off == 768);
  CHECK(allocate_fullest(chunks, 256, 256, &off) == 2 && off == 512);
  CHECK(allocate_fullest(chunks, 2048, 256, &off) == -1);
  // The policy leaves the low-occupancy chunk empty when its old owner dies:
  // first-fit-by-index would have put the new 256 bytes there and pinned it.
  chunks[0].ra.mark_pending(256);
  CHECK(!chunks[0].ra.reclaimable());
  CHECK(retired(100, 106, 102, 101));
  chunks[0].ra.release(0, 256);
  CHECK(chunks[0].ra.reclaimable());
  CHECK(chunks[1].ra.live == 1024 && chunks[2].ra.live == 768);
  // Fullness means fraction, not absolute occupied bytes (oversized chunks).
  chunks = std::vector<Chunk>(2);
  chunks[0].ra.init(2048); chunks[1].ra.init(1024);
  CHECK(chunks[0].ra.alloc(1024, 256, &off));
  CHECK(chunks[1].ra.alloc(768, 256, &off));
  CHECK(allocate_fullest(chunks, 256, 256, &off) == 1);
  // Equal fullness has a stable first-index tie break.
  chunks = std::vector<Chunk>(3);
  for (auto& c : chunks) c.ra.init(1024);
  CHECK(allocate_fullest(chunks, 256, 256, &off) == 0);
  // Fragmented fullest chunk: enough total free space, no fitting range.
  CHECK(chunks[1].ra.alloc(256, 256, &off)); // [0,256)
  CHECK(chunks[1].ra.alloc(256, 256, &off)); // [256,512)
  CHECK(chunks[1].ra.alloc(256, 256, &off)); // [512,768)
  chunks[1].ra.cancel(256, 256); // holes [256,512), [768,1024)
  CHECK(allocate_fullest(chunks, 512, 256, &off) == 0 && off == 256);
  // Pending retirement counts as occupied, never as available free space.
  chunks[0].ra.mark_pending(768);
  CHECK(allocate_fullest(chunks, 512, 256, &off) == 2);
  CHECK(chunks[0].ra.live == 0 && chunks[0].ra.used == 768);
  chunks[0].ra.release(0, 256); chunks[0].ra.release(256, 512);
  CHECK(chunks[0].ra.reclaimable());
  // Reuse an empty slot in place; other chunk indices and ranges do not move.
  const uint64_t used2 = chunks[2].ra.used;
  chunks[0].ra = ChunkRanges{};
  chunks[0].ra.init(1024);
  CHECK(chunks[2].ra.used == used2 && !chunks[0].ra.live);
  CHECK(chunks[0].ra.alloc(1024, 256, &off) && off == 0);
  CHECK(chunks[0].ra.live == 1024 && chunks[0].ra.used == 1024);

  // Alignment holes merge on rollback; shared mesh-pool grow still coalesces.
  RangeAlloc pool;
  pool.init(1024);
  CHECK(pool.alloc(1, 1, &off) && off == 0);
  CHECK(pool.alloc(256, 256, &off) && off == 256);
  pool.release(256, 256); pool.release(0, 1);
  CHECK(pool.free.size() == 1 && pool.free[0] == 1024);
  pool.grow(2048);
  CHECK(pool.cap == 2048 && !pool.used && pool.free.size() == 1 && pool.free[0] == 2048);
  // Arena filtering cannot borrow a pending range or a persistent owner's hole.
  chunks = std::vector<Chunk>(2);
  chunks[0].ra.init(1024);chunks[1].ra.init(1024);chunks[1].ra.arena = 1;
  CHECK(allocate_fullest(chunks, 256, 256, &off, -1, 1) == 1);
  CHECK(allocate_fullest(chunks, 256, 256, &off) == 0);
  CHECK(allocate_fullest(chunks, 256, 256, &off, 1, 1) == -1);
  CHECK(allocate_fullest(chunks, 256, 256, &off, -1, UINT32_MAX) == 0);
  const auto mixed = motion(false), separated = motion(true);
  std::printf("Motion reproduction: mixed/separated reserve %.1f/%.1f MB, occupied %.1f MB, live %.1f MB\n",
              double(mixed.reserved) / (1ull << 20), double(separated.reserved) / (1ull << 20),
              double(separated.occupied) / (1ull << 20), double(separated.live) / (1ull << 20));
  std::fflush(stdout);
  CHECK(mixed.reserved > separated.reserved);
  CHECK(mixed.occupied == separated.occupied && mixed.live == separated.live);
  CHECK(separated.peak == 448 * (1ull << 20));
  std::printf("AS chunk allocator: %u checks passed\n", checks);
}
