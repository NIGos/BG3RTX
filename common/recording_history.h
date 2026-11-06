#pragma once
#include <cstdint>

namespace bg3 {
// GI image layout/history metadata is speculative until its producer submits.
// Only that recording may consume speculative metadata. GPU-only histories do
// not wait for completion after submission: queue barriers order their use.
struct RecordingHistory {
  bool ready = false;
  uint32_t parity = 0;
  uint64_t owner = 0;
  bool saved_ready = false;
  uint32_t saved_parity = 0;
  void resolve(bool submitted, bool discarded) {
    if (!owner) return;
    if (submitted) owner = 0;
    else if (discarded) {
      ready = saved_ready;
      parity = saved_parity;
      owner = 0;
    }
  }
  bool begin(uint64_t recording) {
    if (!recording || (owner && owner != recording)) return false;
    if (!owner) {
      owner = recording;
      saved_ready = ready;
      saved_parity = parity;
    }
    return true;
  }
};
}
