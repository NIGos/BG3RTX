#pragma once
#include <cstdint>
#include <cstddef>

namespace resource_retirement {
constexpr bool complete(uint64_t seq, uint64_t idle, uint64_t frame, uint64_t done, bool same_queue) {
  return seq <= idle || (same_queue && frame < done);
}
constexpr size_t batch_size(size_t queued) { return queued < 128 ? queued : 128; }
constexpr bool can_free_memory(size_t active_objects, bool queued_objects) {
  return active_objects == 0 && !queued_objects;
}
}  // namespace resource_retirement
