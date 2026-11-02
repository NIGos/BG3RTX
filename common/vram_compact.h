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
// Pool growth doubles capacity. Shrink only below one quarter, retaining twice
// the live demand: a shrink frees at least half and leaves one growth step free.
inline uint64_t pool_target(uint64_t used, uint64_t reserved, uint64_t initial) {
  if (!reserved || used > reserved / 4 || initial >= reserved) return 0;
  if (used > (std::numeric_limits<uint64_t>::max)() / 2 - 15) return 0;
  const uint64_t target = (std::max)(initial, (used * 2 + 15) / 16 * 16);
  return target <= reserved / 2 ? target : 0;
}
// Bounded replacement overlap is the operation that releases the old reserve.
// Applying the growth guard here deadlocks shrink precisely when headroom is low.
inline bool pool_shrink_overlap(uint64_t target,uint64_t reserved){
 return target && target<=reserved/2;
}
inline bool hold_move_matches(uint64_t offset,uint64_t size,uint64_t source,uint64_t bytes){
 return size && offset==source && size==bytes;
}
struct QuietWindow {
  uint64_t since = 0;
  bool active = false;
  bool ready(uint64_t frame, bool low, uint64_t window) {
    if (!low || (active && frame < since)) { active = false; return false; }
    if (!active) { since = frame; active = true; }
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
      if (i != source && ranges[i].used && ranges[i].cap &&
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
