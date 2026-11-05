#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace bg3::cpu_opt {
// Membership only: never use bucket order to select or order scene work.
// Owned by bvh_build under its mutex; clear between builds, retain capacity.
class BuildKeys {
  struct Entry { uint64_t key = 0; bool occupied = false; };
  std::vector<Entry> entries;
  size_t used = 0;
  static uint64_t hash(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27; x *= 0x94d049bb133111ebull;
    return x ^ (x >> 31);
  }
  size_t position(uint64_t key) const {
    size_t i = size_t(hash(key)) & (entries.size() - 1);
    while (entries[i].occupied && entries[i].key != key) i = (i + 1) & (entries.size() - 1);
    return i;
  }
 public:
  void clear() { std::fill(entries.begin(), entries.end(), Entry{}); used = 0; }
  size_t capacity() const { return entries.size(); }
  bool contains(uint64_t key) const {
    return !entries.empty() && entries[position(key)].occupied;
  }
  bool insert(uint64_t key) {
    if (contains(key)) return false;
    if (used >= entries.size() / 2) {
      auto old = std::move(entries);
      entries.assign(old.empty() ? 16 : old.size() * 2, Entry{});
      used = 0;
      for (const auto& e : old) if (e.occupied) insert(e.key);
    }
    entries[position(key)] = {key, true}; ++used; return true;
  }
};
}
