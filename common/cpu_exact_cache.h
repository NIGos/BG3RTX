#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace bg3::cpu_opt {
// Bounded owning cache. Hash collisions evict; full keys decide hits. Epochs
// belong in the key when a value depends on an object incarnation or binding.
template<size_t Words, class Value, size_t Slots = 1024> class ExactCache {
 public:
  using Key = std::array<uint64_t, Words>;
  template<class Load> const Value& get(const Key& key, Load&& load) {
    uint64_t hash = 1469598103934665603ull;
    for (auto word : key) hash = (hash ^ word) * 1099511628211ull;
    // Buffer offsets are aligned; fold high bits before selecting a power-of-two
    // slot count, otherwise unrelated draws systematically collide.
    hash ^= hash >> 33;hash *= 0xff51afd7ed558ccdull;
    hash ^= hash >> 33;hash *= 0xc4ceb9fe1a85ec53ull;hash ^= hash >> 33;
    auto& entry = entries_[hash % Slots];
    if (!entry.valid || entry.key != key) {
      entry.value = std::forward<Load>(load)(); entry.key = key; entry.valid = true;
    }
    return entry.value;
  }
 private:
  struct Entry { Key key{}; Value value{}; bool valid = false; };
  std::array<Entry, Slots> entries_{};
};
}
