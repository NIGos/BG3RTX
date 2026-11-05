#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace bg3::cpu_opt {
// An alpha snapshot notes two views and their two images, in that order.
// Preserve duplicates: each lifetime extension remains exactly as before.
class SnapshotHandles {
  std::array<uint64_t, 4> stack;
  std::vector<uint64_t> heap;
  size_t count = 0;
  bool optimized;
 public:
  explicit SnapshotHandles(bool enabled) : optimized(enabled) {}
  void push_back(uint64_t h) {
    if (!optimized) { heap.push_back(h); return; }
    assert(count < stack.size()); stack[count++] = h;
  }
  template<class Fn> void for_each(Fn fn) const {
    if (optimized) { for (size_t i = 0; i < count; ++i) fn(stack[i]); }
    else { for (uint64_t h : heap) fn(h); }
  }
};
}
