#pragma once
// Per-frame memo of host lookups (core/gi.cpp refresh): refresh() runs about 25 times per frame and each run read
// about 15 cfg_int keys and 6 persist words through the host (a string, a lock and two hash lookups each, about
// 0.1 ms CPU per frame in all). The first read of a key in a frame loads it; later reads in the same frame return
// that value. A new frame index clears the memo, so a key change takes effect at the next frame, as it would
// have for every pass recorded after it.
//
// Keys are compared by address (string literals and the constexpr keys of common/*.h): a key spelled at two call
// sites may be stored twice, never confused with another. Not thread safe: the caller holds its own lock.
#include <cstdint>
#include <utility>
#include <vector>

namespace bg3 {

template <class V>
class FrameMemo {
 public:
  template <class Load>
  V get(uint64_t frame, const void* key, Load&& load) {
    if (frame != frame_) {
      frame_ = frame;
      items_.clear();
    }
    for (const auto& it : items_)
      if (it.first == key) return it.second;
    V v = load();
    items_.emplace_back(key, v);
    return v;
  }
  void clear() {
    frame_ = UINT64_MAX;
    items_.clear();
  }
  size_t size() const { return items_.size(); }

 private:
  uint64_t frame_ = UINT64_MAX;
  std::vector<std::pair<const void*, V>> items_;
};

}  // namespace bg3
