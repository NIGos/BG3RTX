#pragma once
// Keep native material inputs separate from the ray alpha representation.
// In particular, an offline VT mask replaces the latter's kind and texture slots.
#include <vector>
namespace bg3 {
template<class Record, class Substitute>
void append_alpha_record(std::vector<Record>& ray, std::vector<Record>& native,
                         Record record, Substitute substitute) {
  native.push_back(record);
  substitute(record);
  ray.push_back(record);
}
} // namespace bg3
