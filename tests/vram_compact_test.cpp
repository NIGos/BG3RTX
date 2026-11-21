#include "../common/vram_compact.h"
#include "../common/as_chunk_alloc.h"
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
  // Replay of the 2026-11-09 stall: 10000 records already registered, then the current scene slot's
  // material record R, then 30 s of failed builds at 60 fps with 4 records per frame (7200 records) during
  // which R's slot is never re-picked. Unpinned, R is forgotten: slot_retired then never trusts it again and
  // the slot pins rt_scene_oldest_live forever. Pinned, R keeps its state; unpinned records of its age go.
  {
    auto replay = [](bool pin_slot, size_t& forgotten) {
      std::map<uint64_t, int> states;
      vram_compact::RecordPins pins;
      uint64_t next = 0;
      forgotten = 0;
      auto reg = [&] {
        const uint64_t id = ++next;
        states[id] = 1;   // submitted
        vram_compact::prune_records(states, next, pins, [&](uint64_t) { ++forgotten; });
        return id;
      };
      for (int i = 0; i < 10000; ++i) reg();
      const uint64_t r = reg();
      if (pin_slot) pins.retain(r);
      const uint64_t neighbour = reg();
      for (int i = 0; i < 1800 * 4; ++i) reg();
      CHECK(!states.count(neighbour));
      const bool kept = states.count(r) && states[r] == 1;
      if (pin_slot) {   // the slot is built again: released, then forgotten like any old record
        pins.release(r);
        for (int i = 0; i < 8192; ++i) reg();
        CHECK(!states.count(r));
      }
      return kept;
    };
    size_t forgotten_off = 0, forgotten_on = 0;
    const bool kept_off = replay(false, forgotten_off), kept_on = replay(true, forgotten_on);
    std::printf("Record pruning replay: slot record kept %d unpinned, %d pinned (%zu / %zu forgotten)\n", int(kept_off),
                int(kept_on), forgotten_off, forgotten_on);
    CHECK(!kept_off && kept_on);
    // Below the limit nothing is forgotten; the newest `keep` ids always stay.
    std::map<uint64_t, int> small{{1, 1}, {2, 1}};
    vram_compact::RecordPins none;
    vram_compact::prune_records(small, 10000, none, [](uint64_t) {});
    CHECK(small.size() == 2);
    std::map<uint64_t, int> big;
    for (uint64_t i = 1; i <= 8193; ++i) big[i] = 1;
    vram_compact::prune_records(big, 8193, none, [](uint64_t) {});
    CHECK(big.size() == 4097 && big.begin()->first == 4097);
  }
  {
    // rt_mesh_pool_trim, the user's log of 2026-11-14: the pool at 1947 MB, never repacked with native hair.
    constexpr uint64_t MB = 1ull << 20;
    // 22:24:23-22:25:45: use 535-672 MB, the window's target 1353 MB, the highest groom range uploaded since
    // 22:24 ends at 452 MB and the highest of the session still loaded at 1162 MB: trimmed to 1353 MB (-594).
    CHECK(pool_trim_target(1353 * MB, 1162 * MB, 1947 * MB) == 1353 * MB);
    CHECK(pool_trim_target(1353 * MB, 452 * MB, 1947 * MB) == 1353 * MB);
    // 22:19: a groom at 1585 MB: the end is in use, less than a quarter to save, no trim (and no thrash).
    CHECK(pool_trim_target(1213 * MB, 1585 * MB, 1947 * MB) == 0);
    CHECK(pool_trim_target(1213 * MB, 1400 * MB + 5, 1947 * MB) == 1408 * MB);   // the tail, in 16 MiB
    CHECK(pool_trim_target(1213 * MB, 1461 * MB, 1947 * MB) == 0);               // 1472 > 75% of 1947
    CHECK(pool_trim_target(0, 100 * MB, 1947 * MB) == 0 && pool_trim_target(600 * MB, 1947 * MB, 1947 * MB) == 0);
    CHECK(pool_trim_target(600 * MB, 3000 * MB, 1947 * MB) == 0);
    // The allocator: the tail is the free block at the end; a shrink keeps every range and offset.
    as_chunks::RangeAlloc ra;
    ra.init(1947 * MB);
    uint64_t a = 0, b = 0, c = 0;
    CHECK(ra.tail() == 0);
    CHECK(ra.alloc(600 * MB, 16, &a) && ra.alloc(500 * MB, 16, &b) && ra.alloc(62 * MB, 16, &c));
    CHECK(a == 0 && b == 600 * MB && c == 1100 * MB && ra.tail() == 1162 * MB);
    ra.release(b, 500 * MB);                                 // a hole below the last range does not move the tail
    CHECK(ra.tail() == 1162 * MB && ra.used == 662 * MB);
    CHECK(!ra.shrink(1161 * MB) && ra.cap == 1947 * MB);     // would cut a live range
    const uint64_t t = pool_trim_target(pool_target(ra.used, ra.cap, 256 * MB), ra.tail(), ra.cap);
    CHECK(t == 1168 * MB && ra.shrink(t) && ra.cap == t && ra.used == 662 * MB && ra.tail() == 1162 * MB);
    uint64_t d = 0, e = 0;
    CHECK(ra.alloc(500 * MB, 16, &d) && d == 600 * MB);      // the hole is still there
    CHECK(ra.alloc(6 * MB, 16, &e) && e == 1162 * MB && ra.tail() == t && !ra.alloc(16, 16, &e));
    CHECK(ra.shrink(t) && !ra.shrink(t - 16));
    ra.grow(2 * t);                                          // and it grows again as before
    CHECK(ra.cap == 2 * t && ra.tail() == t && ra.alloc(100 * MB, 16, &e) && e == t);
    as_chunks::RangeAlloc full;
    full.init(64 * MB);
    CHECK(full.alloc(64 * MB, 16, &a) && full.tail() == 64 * MB && full.shrink(64 * MB) && !full.shrink(32 * MB));
    full.release(32 * MB, 32 * MB);
    CHECK(full.tail() == 32 * MB && full.shrink(32 * MB) && full.cap == 32 * MB && full.free.empty());
  }
  {
    // rt_mesh_pool_trim_move, hx1-s8-hair-stable 001645 00:19:54: 1479 MB for 648 used, target 988 MB, "the pool's
    // end is in use: no trim". Layout: 600 MB pinned at 0, a hole, then a 32 MB mesh at 1400 and a 16 MB hold at the end.
    constexpr uint64_t MB = 1ull << 20;
    auto layout = [&](as_chunks::RangeAlloc& ra, std::vector<std::pair<uint64_t, uint64_t>> used) {
      ra.init(1479 * MB);
      uint64_t pos = 0, off = 0;
      std::vector<std::pair<uint64_t, uint64_t>> gaps;
      for (const auto& [at, size] : used) {   // ascending: fill the gap, take the range; the gaps are freed at the end
        if (at > pos) { CHECK(ra.alloc(at - pos, 16, &off) && off == pos); gaps.push_back({pos, at - pos}); }
        CHECK(ra.alloc(size, 16, &off) && off == at);
        pos = at + size;
      }
      for (const auto& [at, size] : gaps) ra.release(at, size);
    };
    using vram_compact::PoolRange;
    using vram_compact::PoolMoveTo;
    as_chunks::RangeAlloc ra, out;
    std::vector<PoolMoveTo> mv;
    layout(ra, {{0, 600 * MB}, {1400 * MB, 32 * MB}, {1463 * MB, 16 * MB}});
    CHECK(ra.used == 648 * MB && ra.tail() == 1479 * MB);
    CHECK(pool_trim_target(988 * MB, ra.tail(), ra.cap) == 0);                   // today: no trim
    const std::vector<PoolRange> both = {{1400 * MB, 32 * MB}, {1463 * MB, 16 * MB}};
    CHECK(pool_trim_move_plan(ra, both, 988 * MB, out, mv) == 988 * MB);       // -491 MB
    CHECK(mv.size() == 2 && out.cap == 988 * MB && out.used == ra.used && out.tail() <= 988 * MB);
    for (const PoolMoveTo& m : mv) CHECK(m.to >= 600 * MB && m.to + both[m.range].size <= 988 * MB);
    CHECK(mv[0].range == 0 && mv[1].range == 1 && mv[0].to + 32 * MB <= mv[1].to);   // largest first, no overlap
    // Only the hold listed (meshes pinned, hit-material jobs not frozen): the mesh at 1400 pins 1440 MB, over 75%.
    CHECK(pool_trim_move_plan(ra, {{1463 * MB, 16 * MB}}, 988 * MB, out, mv) == 0 && mv.empty());
    // A groom range (pinned) at 1303 MB: cut 1312 MB, over 75% of 1479 (1109): no trim, moves or not.
    as_chunks::RangeAlloc g;
    layout(g, {{0, 600 * MB}, {1303 * MB, MB / 8}, {1400 * MB, 32 * MB}, {1463 * MB, 16 * MB}});
    CHECK(pool_trim_move_plan(g, both, 988 * MB, out, mv) == 0 && mv.empty());
    // A pinned range below the target and a mesh across the cut: the cut is the target, the mesh moves whole.
    layout(g, {{0, 600 * MB}, {900 * MB, 16 * MB}, {980 * MB, 32 * MB}, {1463 * MB, 16 * MB}});
    const std::vector<PoolRange> cross = {{980 * MB, 32 * MB}, {1463 * MB, 16 * MB}};
    CHECK(pool_trim_move_plan(g, cross, 988 * MB, out, mv) == 988 * MB && mv.size() == 2 && out.used == g.used);
    for (const PoolMoveTo& m : mv) CHECK(m.to + cross[m.range].size <= 988 * MB);
    // The pinned end above the target, within 75%: cut at the pinned end in 16 MiB.
    layout(g, {{0, 600 * MB}, {1000 * MB, 4 * MB}, {1463 * MB, 16 * MB}});
    CHECK(pool_trim_move_plan(g, {{1463 * MB, 16 * MB}}, 988 * MB, out, mv) == 1008 * MB && mv.size() == 1);
    // No room below the cut: no trim (nothing half moved).
    layout(g, {{0, 980 * MB}, {1400 * MB, 32 * MB}});
    CHECK(pool_trim_move_plan(g, {{1400 * MB, 32 * MB}}, 988 * MB, out, mv) == 0 && mv.empty());
    // An unlisted range is pinned; nothing to move gives the plain trim's cut.
    layout(g, {{0, 600 * MB}, {1463 * MB, 16 * MB}});
    CHECK(pool_trim_move_plan(g, {}, 988 * MB, out, mv) == 0);
    layout(g, {{0, 600 * MB}, {700 * MB, 16 * MB}});
    CHECK(pool_trim_move_plan(g, {}, 988 * MB, out, mv) == pool_trim_target(988 * MB, g.tail(), g.cap) && mv.empty());
    CHECK(pool_trim_move_plan(g, {{700 * MB, 16 * MB}}, 0, out, mv) == 0);    // no target
  }
  std::printf("VRAM compact: %u checks passed\n", checks);
}
