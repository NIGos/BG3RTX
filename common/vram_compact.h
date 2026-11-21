#pragma once
#include "as_chunk_alloc.h"
#include <limits>

namespace bg3::vram_compact {
enum class CopyOutcome { Wait, Publish, Discard };
// Completion wins over a late drop notification, but disabling the key never
// publishes. Time alone is deliberately absent from this decision.
inline CopyOutcome copy_outcome(bool done, bool dropped, bool enabled) {
  if (done) return enabled ? CopyOutcome::Publish : CopyOutcome::Discard;
  return dropped ? CopyOutcome::Discard : CopyOutcome::Wait;
}
inline bool batch_wait(bool done, bool dropped, bool timeout, bool relocation) {
  return !done && !dropped && (relocation || !timeout);
}
// A pool copy and BLAS evacuation can share one command record. Completing
// either operation must not let registry pruning forget the other's drop state.
struct RecordPins {
  std::map<uint64_t, size_t> counts;
  void retain(uint64_t rec) { ++counts[rec]; }
  void release(uint64_t rec) {
    auto it = counts.find(rec);
    if (it != counts.end() && !--it->second) counts.erase(it);
  }
  bool contains(uint64_t rec) const { return counts.count(rec) != 0; }
  void clear() { counts.clear(); }
};
// The build-record registry (bvhsplit g_rec_state): past `limit` entries, records more than `keep` ids old are
// forgotten (an unknown record is never trusted), except pinned ones: pending compaction copies, and the
// scene slots' material records (rtscene slot_retired reads them until the slot is built again). User log
// 2026-11-09: builds failed for 30 s (08:01:07-39), the current slot was never re-picked meanwhile, its
// record was pruned, and once builds resumed that slot never retired: it pinned rt_scene_oldest_live and
// every freed BLAS range (8 GB pending by 08:02:05).
template<class States, class Forget>
void prune_records(States& states, uint64_t next, const RecordPins& pins, Forget forget, size_t limit = 8192,
                   uint64_t keep = 4096) {
  if (states.size() <= limit) return;
  for (auto it = states.begin(); it != states.end();)
    if (it->first + keep < next && !pins.contains(it->first)) { forget(it->first); it = states.erase(it); }
    else ++it;
}
// Timed-out pool users are not proof of completion. A reset of their exact
// command recording removes the possibility of a late pool access.
struct UncertainCommands {
  std::map<uint64_t, uint64_t> commands; // recording identity -> command handle
  void note(uint64_t recording, uint64_t command) { commands[recording] = command; }
  void reset(uint64_t command) {
    for (auto it = commands.begin(); it != commands.end();)
      if (it->second == command) it = commands.erase(it); else ++it;
  }
  bool empty() const { return commands.empty(); }
  void clear() { commands.clear(); }
};
// Keep the capture policy's 25% demand reserve and require at least 25% savings.
// The old quarter-full rule never qualified vg2's 1050 / 2047 MiB pool.
inline uint64_t pool_target(uint64_t used, uint64_t reserved, uint64_t initial) {
  if (!reserved || initial >= reserved || used > reserved) return 0;
  if (used > (std::numeric_limits<uint64_t>::max)() - used/4 - 15) return 0;
  const uint64_t target = (std::max)(initial, (used + used/4 + 15) / 16 * 16);
  return target <= reserved - reserved/4 ? target : 0;
}
// Bounded replacement overlap is the operation that releases the old reserve.
// Applying the growth guard here deadlocks shrink precisely when headroom is low.
inline bool pool_shrink_overlap(uint64_t target,uint64_t reserved){
 return target && target<=reserved-reserved/4;
}
// rt_mesh_pool_trim (claude/vram-rightsize, 2026-11-14, default 0, live). The repack moves cached meshes and hold
// snapshots only, so it never runs with native Hair (groom points, LOD arrays and hair bounds live in the pool and
// are written every frame): user log 22:17-22:26, 36 "shrink deferred: native hair ranges" lines, the pool at 1947 MB
// for 400 to 1420 MB used. A trim moves nothing: the pool is replaced by a shorter one with the same offsets, like a
// growth in reverse, when the bytes past the window's target are free. Target: the larger of the repack's target
// (the quiet window's peak use + 25%) and the end of the last range in use, in 16 MiB; at least a quarter must be
// saved (pool_shrink_overlap), so a trim and the next growth cannot alternate within the same demand.
// Cost bound: one copy of the kept bytes and old + new allocated until the old scenes retire, at most once per
// quiet window (254 frames); a later growth is the existing one (a copy, doubling).
inline uint64_t pool_trim_target(uint64_t target,uint64_t tail,uint64_t reserved){
 const uint64_t q=16ull<<20;
 if(!target || tail>reserved || tail>UINT64_MAX-q)return 0;
 const uint64_t trimmed=(std::max)(target,(tail+q-1)/q*q);
 return pool_shrink_overlap(trimmed,reserved)?trimmed:0;
}
// rt_mesh_pool_trim_move (claude/r3-layer, 2026-11-21, default 0). With native hair a single range at the pool's end
// blocks the trim (hx1-s8-hair-stable 001645 00:19:54: "the pool's end is in use", 1479 MB for 648 used, target 988 MB).
// Cached meshes and hold snapshots can move (the repack moves them); every other range is pinned (groom arrays and
// points, hair bounds, OMM indices, pending frees, anything not listed). The cut is the larger of the target and the end
// of the highest pinned range, in 16 MiB; the movable ranges that reach past it move into free space below it, in the
// same in-command copy as the trim. Returns the new capacity (0: no trim) and fills `out` (the allocator after the
// moves) and `moves` (index into `movable`, new offset). Ranges in `movable` must be live ranges of `ra`.
struct PoolRange { uint64_t off, size; };
struct PoolMoveTo { size_t range; uint64_t to; };
inline uint64_t pool_trim_move_plan(const as_chunks::RangeAlloc& ra, const std::vector<PoolRange>& movable,
                                    uint64_t target, as_chunks::RangeAlloc& out, std::vector<PoolMoveTo>& moves) {
  moves.clear();
  const uint64_t q = 16ull << 20;
  if (!target || !ra.cap) return 0;
  // The highest pinned byte: walk the used intervals (the gaps between free blocks) from the top, stripping the movable
  // ranges that end exactly where the remaining interval ends.
  std::map<uint64_t, uint64_t> by_end;   // end -> start of each movable range
  for (const PoolRange& r : movable) {
    if (!r.size || r.off > ra.cap || r.size > ra.cap - r.off) return 0;
    by_end[r.off + r.size] = r.off;
  }
  uint64_t pinned_end = 0, hi = ra.cap;
  for (auto it = ra.free.rbegin();; ++it) {
    const uint64_t lo = it == ra.free.rend() ? 0 : it->first + it->second;   // used interval [lo, hi)
    uint64_t top = hi;
    for (auto m = by_end.find(top); top > lo && m != by_end.end() && m->second >= lo; m = by_end.find(top)) top = m->second;
    if (top > lo) { pinned_end = top; break; }
    if (it == ra.free.rend()) break;
    hi = it->first;
  }
  if (pinned_end > UINT64_MAX - q) return 0;
  const uint64_t cut = (std::max)(target, (pinned_end + q - 1) / q * q);
  if (!pool_shrink_overlap(cut, ra.cap)) return 0;
  out = ra;
  std::vector<size_t> above;
  for (size_t i = 0; i < movable.size(); ++i)
    if (movable[i].off + movable[i].size > cut) { above.push_back(i); out.release(movable[i].off, movable[i].size); }
  if (!out.shrink(cut)) return 0;
  std::sort(above.begin(), above.end(), [&](size_t a, size_t b) { return movable[a].size > movable[b].size; });
  for (size_t i : above) {
    uint64_t to = 0;
    if (!out.alloc(movable[i].size, 16, &to)) { moves.clear(); return 0; }
    moves.push_back({i, to});
  }
  return out.used == ra.used ? cut : 0;
}
inline bool hold_move_matches(uint64_t offset,uint64_t size,uint64_t source,uint64_t bytes){
 return size && offset==source && size==bytes;
}
struct QuietWindow {
  uint64_t since = 0;
  uint64_t peak_target = 0;
  bool active = false;
  bool ready(uint64_t frame, bool low, uint64_t window, uint64_t target = 0) {
    if (!low || (active && frame < since)) { active = false; peak_target = 0; return false; }
    if (!active) { since = frame; active = true; peak_target = 0; }
    peak_target = (std::max)(peak_target, target);
    return frame - since >= window;
  }
};
struct Move { size_t item, chunk; uint64_t off; };
// Plan a complete evacuation, never a partial shuffle or a new allocation.
// Real allocators are untouched until the whole source fits. Pending and kept
// bytes remain occupied. Only fuller destinations may receive the source.
template<class Chunks>
bool evacuation(const Chunks& chunks, size_t source, const std::vector<uint64_t>& sizes,
                std::vector<Move>& moves) {
  moves.clear();
  if (source >= chunks.size() || chunks[source].ra.kept || !chunks[source].ra.live) return false;
  uint64_t sum = 0;
  for (auto size : sizes) {
    if (!size || size % 256 || sum > UINT64_MAX - size) return false;
    sum += size;
  }
  if (sum != chunks[source].ra.live || chunks[source].ra.used != sum) return false;
  std::vector<as_chunks::ChunkRanges> ranges;
  for (const auto& c : chunks) ranges.push_back(c.ra);
  std::vector<size_t> items;
  for (size_t i = 0; i < sizes.size(); ++i) items.push_back(i);
  std::sort(items.begin(), items.end(), [&](size_t a, size_t b) { return sizes[a] > sizes[b]; });
  for (size_t item : items) {
    std::vector<size_t> order;
    for (size_t i = 0; i < ranges.size(); ++i)
      if (i != source && ranges[i].arena == 0 && ranges[i].used && ranges[i].cap &&
          double(ranges[i].used) / double(ranges[i].cap) >=
              double(ranges[source].used) / double(ranges[source].cap)) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
      const double x = double(ranges[a].used) / double(ranges[a].cap);
      const double y = double(ranges[b].used) / double(ranges[b].cap);
      return x != y ? x > y : a < b;
    });
    bool fit = false;
    for (size_t i : order) {
      uint64_t off = 0;
      if (!ranges[i].alloc(sizes[item], 256, &off)) continue;
      moves.push_back({item, i, off}); fit = true; break;
    }
    if (!fit) { moves.clear(); return false; }
  }
  return !moves.empty();
}
} // namespace bg3::vram_compact
