#pragma once
#include <atomic>
#include <cstdint>

// Only the presenting thread updates stable_from. Resource destruction can run
// on recording threads, so the invalidation frame remains atomic.
struct FgStability {
  std::atomic<uint64_t> last_destroy{0};
  uint64_t stable_from = 0;

  bool destroyed(uint64_t frame, uint32_t current_roles, bool current_only) {
    const bool reset = !current_only || current_roles != 0;
    if (reset) last_destroy = frame;
    return reset;
  }

  bool update(uint64_t frame, bool inputs_ready, uint64_t stable_frames) {
    // Preserve the original two-stage cooldown, including its boundaries.
    if (!inputs_ready || last_destroy.load() + stable_frames > frame) stable_from = 0;
    else if (!stable_from) stable_from = frame;
    return inputs_ready && stable_from && frame >= stable_from + stable_frames;
  }
};
