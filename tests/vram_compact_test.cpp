#include "../common/vram_compact.h"
#include <cstdio>
#include <cstdlib>
using namespace bg3;
static unsigned checks;
static void check(bool ok, int line) {
  ++checks;
  if (!ok) { std::fprintf(stderr, "VRAM compact check at line %d failed\n", line); std::exit(1); }
}
#define CHECK(x) check((x), __LINE__)
struct Chunk { as_chunks::ChunkRanges ra; };
int main() {
  using namespace vram_compact;
  CHECK(copy_outcome(false, false, true) == CopyOutcome::Wait);
  CHECK(copy_outcome(false, false, false) == CopyOutcome::Wait); // key off cannot free an executing copy
  CHECK(copy_outcome(false, true, true) == CopyOutcome::Discard);
  CHECK(copy_outcome(true, false, false) == CopyOutcome::Discard);
  CHECK(copy_outcome(true, false, true) == CopyOutcome::Publish);
  CHECK(copy_outcome(true, true, true) == CopyOutcome::Publish); // done word proves execution
  CHECK(batch_wait(false, false, true, true)); // stalled/unsubmitted relocation never times out
  CHECK(!batch_wait(false, false, true, false)); // existing ordinary batch watchdog retained
  CHECK(!batch_wait(false, true, false, true));
  CHECK(!batch_wait(true, false, false, true));
  RecordPins records;
  records.retain(42);records.retain(42); // pool + BLAS copy, same command
  records.retain(43);
  CHECK(records.contains(42) && records.contains(43));
  records.release(42); // early pool completion leaves BLAS drop state pinned
  CHECK(records.contains(42));
  records.release(42);
  CHECK(!records.contains(42) && records.contains(43));
  records.release(42); // cancelled cleanup cannot underflow another record
  CHECK(records.contains(43));
  records.clear();CHECK(!records.contains(43));
  UncertainCommands uncertain;
  uncertain.note(1,100);uncertain.note(2,100);uncertain.note(3,200);
  CHECK(!uncertain.empty());
  uncertain.reset(100);CHECK(uncertain.commands.size()==1 && uncertain.commands.count(3));
  uncertain.reset(999);CHECK(!uncertain.empty());
  uncertain.reset(200);CHECK(uncertain.empty());
  constexpr uint64_t MB = 1ull << 20;
  CHECK(pool_target(467 * MB, 2047 * MB, 256 * MB) == (467 * MB * 5 / 4 + 15) / 16 * 16);
  CHECK(pool_target(512 * MB, 2047 * MB, 256 * MB) == 640 * MB);
  CHECK(pool_target(0, 1024 * MB, 256 * MB) == 256 * MB);
  CHECK(pool_target(1, 256 * MB, 256 * MB) == 0);
  CHECK(pool_target(256, 1024, 16) == 320);
  CHECK(pool_target(257, 1024, 16) == 336);
  CHECK(pool_target(1, 1024, 16) == 16);
  CHECK(pool_target(1, 1024, 17) == 17);
  CHECK(pool_target(UINT64_MAX, UINT64_MAX, 16) == 0);
  CHECK(pool_target(296 * MB,2047 * MB,256 * MB)==370 * MB);
  CHECK(pool_shrink_overlap(592 * MB,2047 * MB));
  CHECK(pool_shrink_overlap(1024 * MB,2047 * MB));
  CHECK(!pool_shrink_overlap(1536 * MB,2047 * MB));
  CHECK(pool_target(1050 * MB,2047 * MB,256 * MB)==1312 * MB + MB/2);
  CHECK(pool_target(1300 * MB,2047 * MB,256 * MB)==0);
  CHECK(!pool_shrink_overlap(0,2047 * MB));
  CHECK(hold_move_matches(16,32,16,32));
  CHECK(!hold_move_matches(32,32,16,32));
  CHECK(!hold_move_matches(16,0,16,32));
  CHECK(!hold_move_matches(16,16,16,32));
  // Repack a mixed pool with a high-offset live hold. Ignoring the hold
  // fails accounting; copying it preserves its bytes and old-scene allocator.
  as_chunks::RangeAlloc old_pool,new_pool;
  old_pool.init(2047*MB);new_pool.init(592*MB);
  uint64_t mesh=0,hole=0,hold=0,moved_mesh=0,moved_hold=0;
  CHECK(old_pool.alloc(280*MB,16,&mesh));
  CHECK(old_pool.alloc(1500*MB,16,&hole));
  CHECK(old_pool.alloc(16*MB,16,&hold));
  old_pool.release(hole,1500*MB);
  CHECK(new_pool.alloc(280*MB,16,&moved_mesh));
  CHECK(new_pool.used!=old_pool.used);
  CHECK(new_pool.alloc(16*MB,16,&moved_hold));
  CHECK(new_pool.used==old_pool.used && moved_hold<hold);
  CHECK(old_pool.cap==2047*MB && old_pool.used==296*MB);
  // A hold removed while copying must release its destination, never reappear.
  if(!hold_move_matches(hold,0,hold,16*MB))new_pool.release(moved_hold,16*MB);
  CHECK(new_pool.used==280*MB && old_pool.used==296*MB);
  QuietWindow peaks;
  CHECK(!peaks.ready(0,true,254,1400*MB));
  CHECK(peaks.ready(254,true,254,1312*MB));
  CHECK(peaks.peak_target==1400*MB);
  CHECK(!peaks.ready(255,false,254));
  CHECK(!peaks.ready(256,true,254,800*MB));
  CHECK(peaks.peak_target==800*MB);
  QuietWindow quiet;
  CHECK(!quiet.ready(100, true, 254));
  CHECK(!quiet.ready(353, true, 254));
  CHECK(quiet.ready(354, true, 254));
  CHECK(!quiet.ready(355, false, 254));
  CHECK(!quiet.ready(500, true, 254));
  CHECK(!quiet.ready(499, true, 254)); // frame rewind resets qualification
  CHECK(!quiet.ready(600, true, 254));
  CHECK(quiet.ready(854, true, 254));
  std::vector<Chunk> chunks(3);
  for (auto& c : chunks) c.ra.init(1024);
  uint64_t off = 0;
  CHECK(chunks[0].ra.alloc(256, 256, &off));
  CHECK(chunks[1].ra.alloc(512, 256, &off));
  CHECK(chunks[2].ra.alloc(768, 256, &off));
  std::vector<Move> moves;
  CHECK(evacuation(chunks, 0, {256}, moves));
  CHECK(moves.size() == 1 && moves[0].chunk == 2 && moves[0].off == 768);
  CHECK(chunks[2].ra.used == 768 && chunks[0].ra.used == 256); // plan doesn't reserve
  CHECK(!evacuation(chunks, 0, {128}, moves)); // non-aligned source
  CHECK(!evacuation(chunks, 0, {512}, moves)); // dynamic/unaccounted live bytes
  ++chunks[0].ra.kept;
  CHECK(!evacuation(chunks, 0, {256}, moves));
  --chunks[0].ra.kept;
  chunks[0].ra.mark_pending(256);
  CHECK(!evacuation(chunks, 0, {256}, moves)); // GPU retirement still owns source
  chunks[0].ra.live = 256;
  CHECK(as_chunks::allocate_fullest(chunks, 256, 256, &off, 2) == 1); // draining source excluded
  CHECK(chunks[2].ra.used == 768);
  chunks[1].ra.mark_pending(768); // pending destination never becomes a hole
  CHECK(!evacuation(chunks, 0, {256, 256}, moves));
  chunks = std::vector<Chunk>(3);
  for (auto& c : chunks) c.ra.init(2048);
  CHECK(chunks[0].ra.alloc(256, 256, &off));
  CHECK(chunks[0].ra.alloc(512, 256, &off));
  CHECK(chunks[1].ra.alloc(1280, 256, &off));
  CHECK(chunks[2].ra.alloc(1536, 256, &off));
  CHECK(evacuation(chunks, 0, {256, 512}, moves));
  CHECK(moves.size() == 2 && moves[0].item == 1 && moves[0].chunk == 2 && moves[1].chunk == 1);
  // Enough total free bytes but no contiguous destination large enough.
  chunks = std::vector<Chunk>(2);
  chunks[0].ra.init(2048);chunks[1].ra.init(2048);
  CHECK(chunks[0].ra.alloc(768, 256, &off));
  for (int i = 0; i < 8; ++i) CHECK(chunks[1].ra.alloc(256, 256, &off));
  for (int i = 0; i < 8; i += 2) chunks[1].ra.cancel(i * 256, 256);
  CHECK(!evacuation(chunks, 0, {768}, moves));
  CHECK(moves.empty() && chunks[1].ra.used == 1024);
  // Real allocator can reproduce every planned destination without growth.
  chunks[1].ra.cancel(256, 256);
  CHECK(evacuation(chunks, 0, {768}, moves));
  CHECK(chunks[moves[0].chunk].ra.alloc(768, 256, &off) && off == moves[0].off);
  // Cached relocation must not reintroduce long-lived pins into rebuild arenas.
  chunks = std::vector<Chunk>(3);
  for (auto& c : chunks) c.ra.init(1024);
  CHECK(chunks[0].ra.alloc(256, 256, &off));
  CHECK(chunks[1].ra.alloc(768, 256, &off));chunks[1].ra.arena = 1;
  CHECK(chunks[2].ra.alloc(512, 256, &off));chunks[2].ra.arena = 2;
  CHECK(!evacuation(chunks, 0, {256}, moves));
  chunks[2].ra.arena = 0;
  CHECK(evacuation(chunks, 0, {256}, moves) && moves[0].chunk == 2);
  std::printf("VRAM compact: %u checks passed\n", checks);
}
