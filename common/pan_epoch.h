#pragma once
#include <cstdint>

// A presentation on another thread must not relabel a partially recorded frame.
struct PanEpoch {
  uint64_t recording = 0;
  uint64_t frame = UINT64_MAX;
  int serial = 0;
  void clear() { *this = {}; }
  void bind(uint64_t rec, uint64_t index, int burst) {
    recording = rec; frame = index; serial = burst;
  }
  bool matches(uint64_t rec, int burst) const {
    return recording != 0 && recording == rec && serial == burst && frame != UINT64_MAX;
  }
};
