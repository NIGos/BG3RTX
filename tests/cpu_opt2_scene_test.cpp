#include "../common/cpu_first_slots.h"
#include "../common/cpu_deferred_ranges.h"
#include "../common/cpu_snapshot_handles.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <utility>

static void check(bool ok) { if (!ok) std::abort(); }
using Views = std::vector<std::pair<uint64_t, uint32_t>>;
static uint32_t legacy(Views& list, uint32_t cap, uint64_t view, uint32_t layout) {
  for (size_t i = 0; i < list.size(); ++i) if (list[i].first == view) return uint32_t(i + 1);
  if (list.size() + 1 >= cap) return 0;
  list.push_back({view, layout}); return uint32_t(list.size());
}
struct Range {
  uint64_t buffer, lo, hi;
  bool operator==(const Range& b) const { return buffer == b.buffer && lo == b.lo && hi == b.hi; }
};
int main() {
  bg3::cpu_opt::FirstSlots slots;
  std::mt19937_64 rng(123);
  Views a, b;
  // Include zero/all-one handles, duplicates with different layouts, full
  // lists, small and empty scenes, recycled handles and key-off interludes.
  for (int frame = 0; frame < 16; ++frame) {
    slots.clear(); a.clear(); b.clear();
    const uint32_t cap = frame % 2 ? 4096 : 16;
    for (int draw = 0; draw < 12000; ++draw) {
      uint64_t key = draw < 2 ? uint64_t(draw ? UINT64_MAX : 0) : rng() % (frame % 3 ? 5000 : 4);
      const auto layout = uint32_t(rng() % 8);
      const auto off = legacy(a, cap, key, layout);
      const auto on = draw % 7 ? slots.get(b, cap, key, layout) : legacy(b, cap, key, layout);
      check(off == on && a == b);
    }
  }
  // The exact helper used by bvh_classify: all 16 vertex ranges plus index,
  // unchanged ordered contents whenever a mesh needs extraction. No heap
  // capacity for cached/rejected/non-instanceable draws under the key.
  for (size_t n = 0; n <= 17; ++n) for (bool extract : {false, true}) {
    std::vector<Range> off, on;
    bg3::cpu_opt::DeferredRanges<Range, 17> old_ranges(off, false), new_ranges(on, true);
    for (size_t i = 0; i < n; ++i) {
      Range r{rng(), rng(), rng()}; old_ranges.push_back(r); new_ranges.push_back(r);
    }
    if (extract) { old_ranges.commit(); new_ranges.commit(); check(off == on); }
    else check(on.empty() && on.capacity() == 0);
  }
  for (size_t n = 0; n <= 4; ++n) {
    bg3::cpu_opt::SnapshotHandles off(false), on(true);
    std::vector<uint64_t> expected, actual;
    for (size_t i = 0; i < n; ++i) {
      const uint64_t h = i % 2 ? UINT64_MAX : 0; // duplicates and zero retain order
      off.push_back(h); on.push_back(h);
    }
    off.for_each([&](uint64_t h) { expected.push_back(h); });
    on.for_each([&](uint64_t h) { actual.push_back(h); });
    check(expected == actual && actual.size() == n);
  }
  auto bench = [&](bool opt) {
    uint64_t receipt = 0; Views list;
    auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 200; ++frame) {
      list.clear(); slots.clear();
      for (uint64_t draw = 0; draw < 5000; ++draw)
        receipt += opt ? slots.get(list, 4096, draw % 1500, uint32_t(draw % 3)) : legacy(list, 4096, draw % 1500, uint32_t(draw % 3));
    }
    return std::pair{std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 200, receipt};
  };
  const auto off = bench(false), on = bench(true); check(off.second == on.second);
  std::printf("cpu_opt2 alpha slots: identical ordered records/layouts; 5000 draws/1500 views OFF %.4f ON %.4f ms/frame\n", off.first, on.first);
  auto range_bench = [&](bool opt) {
    uint64_t receipt = 0, allocations = 0;
    auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 200; ++frame) for (uint64_t draw = 0; draw < 5000; ++draw) {
      std::vector<Range> ranges;
      bg3::cpu_opt::DeferredRanges<Range, 17> staged(ranges, opt);
      for (uint64_t binding = 0; binding < 3; ++binding) staged.push_back({binding, draw * 16, draw * 16 + 192});
      if (draw % 1000 == 0) { staged.commit(); for (const auto& r : ranges) receipt += r.buffer + r.lo + r.hi; }
      allocations += ranges.capacity() != 0;
    }
    return std::pair{std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 200, std::pair{receipt, allocations}};
  };
  const auto old_ranges = range_bench(false), new_ranges = range_bench(true);
  check(old_ranges.second.first == new_ranges.second.first);
  check(old_ranges.second.second == 1000000 && new_ranges.second.second == 1000);
  std::printf("cpu_opt2 mesh ranges: identical extraction contents; 5000 draws/5 extracts OFF %.4f ON %.4f ms/frame; vectors allocating %llu -> %llu\n",
              old_ranges.first, new_ranges.first, (unsigned long long)old_ranges.second.second, (unsigned long long)new_ranges.second.second);
}
