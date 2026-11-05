#pragma once
#include <array>
#include <cassert>
#include <vector>

namespace bg3::cpu_opt {
// Eviction ranges are consumed only for a newly extracted mesh. Build the
// identical ordered list in stack storage until that decision is known.
template<class Range, size_t Capacity>
class DeferredRanges {
  std::array<Range, Capacity> pending;
  size_t count = 0;
  std::vector<Range>& output;
  bool deferred;
 public:
  DeferredRanges(std::vector<Range>& out, bool enabled) : output(out), deferred(enabled) {}
  void push_back(const Range& range) {
    if (!deferred) { output.push_back(range); return; }
    assert(count < Capacity); pending[count++] = range;
  }
  void commit() {
    if (deferred) output.assign(pending.begin(), pending.begin() + count);
  }
};
}
