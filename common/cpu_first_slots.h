#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace bg3::cpu_opt {
// Index only. The caller's ordered list remains the authoritative descriptor
// contents. View identity alone selects the first slot, including its layout.
class FirstSlots {
  struct Entry { uint64_t key = 0; uint32_t slot = 0; };
  std::vector<Entry> entries;
  size_t indexed = 0, used = 0;
  size_t position(uint64_t key) const {
    key ^= key >> 30; key *= 0xbf58476d1ce4e5b9ull;
    key ^= key >> 27; key *= 0x94d049bb133111ebull;
    return size_t(key ^ (key >> 31)) & (entries.size() - 1);
  }
  void insert(uint64_t key, uint32_t slot) {
    if (used >= entries.size() / 2) {
      auto old = std::move(entries);
      entries.assign(old.empty() ? 16 : old.size() * 2, Entry{}); used = 0;
      for (const auto& e : old) if (e.slot) insert(e.key, e.slot);
    }
    size_t p = position(key);
    while (entries[p].slot && entries[p].key != key) p = (p + 1) & (entries.size() - 1);
    if (!entries[p].slot) { entries[p] = {key, slot}; ++used; }
  }
 public:
  void clear() { std::fill(entries.begin(), entries.end(), Entry{}); indexed = used = 0; }
  size_t capacity() const { return entries.size(); }
  template<class List, class Key, class Layout>
  uint32_t get(List& list, uint32_t cap, Key key, Layout layout) {
    // Key-off appends are legal between calls. Index them before looking up.
    for (; indexed < list.size(); ++indexed) insert(uint64_t(list[indexed].first), uint32_t(indexed + 1));
    if (!entries.empty()) {
      size_t p = position(uint64_t(key));
      while (entries[p].slot) {
        if (entries[p].key == uint64_t(key)) return entries[p].slot;
        p = (p + 1) & (entries.size() - 1);
      }
    }
    if (list.size() + 1 >= cap) return 0;
    list.push_back({key, layout});
    insert(uint64_t(key), uint32_t(list.size())); indexed = list.size();
    return uint32_t(list.size());
  }
};
}
