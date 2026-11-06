#pragma once
#include "core.h"
#include <algorithm>
#include <vector>

// A mapped slot remains immutable while any executable recording can consume
// it again, and until the last submission of every consumer has completed.
struct CoreCompletionLease {
  std::vector<uint64_t> recordings;
  void record(VkCommandBuffer cmd) {
    const uint64_t id = host->recording_index(cmd);
    if (std::find(recordings.begin(), recordings.end(), id) == recordings.end()) {
      host->retain_recording(id);
      recordings.push_back(id);
    }
  }
  bool reusable() const {
    for (uint64_t id : recordings) {
      const auto state = host->recording_state(id);
      if ((state.flags & BG3_RECORDING_EXECUTABLE) || !(state.flags & BG3_RECORDING_COMPLETE)) return false;
    }
    return true;
  }
  bool readable() const {
    if (!reusable()) return false;
    for (uint64_t id : recordings)
      if (host->recording_state(id).flags & BG3_RECORDING_SUBMITTED) return true;
    return false;
  }
  void clear() {
    for (uint64_t id : recordings) host->release_recording(id);
    recordings.clear();
  }
};
