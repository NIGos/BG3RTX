#pragma once
#include <cstdint>

// A written private FG input cannot be overwritten until its present has
// published a nonzero SDK completion value and that value has completed.
struct FgCopyLease {
  bool written = false;
  uint64_t value = 0;
  bool reusable(bool completed) const { return !written || (value != 0 && completed); }
  void write() { written = true; value = 0; }
  void presented(uint64_t completion) { value = completion; }
};

// QueuePresentKHR increments the host counter before tagging the work that
// was submitted under the preceding counter value.
inline bool fg_copy_for_present(uint64_t submitted, uint64_t present) {
  return present != 0 && submitted == present - 1;
}

inline bool fg_copy_precedes_present(uint64_t submitted, uint64_t present) {
  return present != 0 && submitted < present;
}

inline bool fg_copy_pair_for_present(uint64_t copied, uint64_t camera, uint64_t color, uint64_t present) {
  return fg_copy_for_present(copied, present) && camera == copied && color == copied;
}
