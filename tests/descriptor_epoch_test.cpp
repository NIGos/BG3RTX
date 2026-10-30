#include "../common/descriptor_epoch.h"
#include <cstdint>
#include <unordered_map>

struct Tracker {
  std::unordered_map<uint64_t, std::unordered_map<uint32_t, uint64_t>> sets;
  std::unordered_map<uint64_t, uint32_t> dynamic_counts;
  std::unordered_map<uint64_t, uint64_t> set_layouts;
};

int main() {
  Tracker t;
  descriptor_set_allocated(t, uint64_t(42), uint64_t(7));
  if (t.set_layouts.at(42) != 7 || t.sets.count(42) || t.dynamic_counts.count(42)) return 1;

  // A retired UI set had a profile-sized buffer, an image and dynamic metadata.
  t.sets[42] = {{28, 900}, {42, 700}};
  t.dynamic_counts[42] = 1;
  t.sets[99] = {{13, 901}};
  t.dynamic_counts[99] = 2;
  t.set_layouts[99] = 8;
  descriptor_set_allocated(t, uint64_t(42), uint64_t(9));
  if (t.sets.count(42) || t.dynamic_counts.count(42) || t.set_layouts.at(42) != 9) return 2;
  if (t.sets.at(99).at(13) != 901 || t.dynamic_counts.at(99) != 2 || t.set_layouts.at(99) != 8) return 3;

  // Partial writes within a lifetime keep the other entries. Reallocation with
  // the SAME layout must still discard all of that lifetime's descriptors.
  t.sets[42][13] = 1000;
  t.sets[42][28] = 1001;
  if (t.sets.at(42).at(13) != 1000) return 4;
  descriptor_set_allocated(t, uint64_t(42), uint64_t(9));
  if (t.sets.count(42) || t.dynamic_counts.count(42) || t.set_layouts.at(42) != 9) return 5;
  t.sets[42][13] = 1002;
  if (t.sets.at(42).count(28) || t.sets.at(42).count(42) || t.sets.at(42).at(13) != 1002) return 6;

  // All returned handles in a batch get a fresh lifetime, not only the first.
  for (uint64_t set : {uint64_t(42), uint64_t(99)}) descriptor_set_allocated(t, set, uint64_t(10));
  if (!t.sets.empty() || !t.dynamic_counts.empty() || t.set_layouts.at(42) != 10 || t.set_layouts.at(99) != 10) return 7;
  return 0;
}
