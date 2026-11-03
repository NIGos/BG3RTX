#pragma once
#include <atomic>
#include <cstdint>

namespace bg3::water {
inline constexpr char kBuildCameraKey[] = "bg3rtx.water_build_camera.v1";
inline constexpr uint32_t kBuildCameraCount = 4, kBuildCameraBytes = 16;
inline constexpr uint32_t build_camera_index(uint64_t frame) { return uint32_t(frame % kBuildCameraCount); }
struct BuildCameraEntry {
  uint64_t frame = 0, tlas = 0, reference = 0;
};
// persist() zero-initializes storage. The lock protects publication across the host/core threads.
struct BuildCameraRing {
  uint32_t lock = 0, pad = 0;
  uint64_t buffer = 0;
  BuildCameraEntry entries[kBuildCameraCount]{};
};
struct BuildCameraLock {
  BuildCameraRing& ring;
  explicit BuildCameraLock(BuildCameraRing& r) : ring(r) {
    uint32_t expected = 0;
    while (!std::atomic_ref<uint32_t>(ring.lock).compare_exchange_weak(expected, 1, std::memory_order_acquire)) expected = 0;
  }
  ~BuildCameraLock() { std::atomic_ref<uint32_t>(ring.lock).store(0, std::memory_order_release); }
};
inline BuildCameraEntry find_build_camera(const BuildCameraRing& ring, uint64_t tlas, uint64_t now, uint64_t frame = 0) {
  BuildCameraEntry found{};
  for (const auto& e : ring.entries)
    if ((!frame || e.frame == frame) && tlas && e.frame > found.frame && e.frame <= now && now - e.frame < kBuildCameraCount &&
        (e.tlas == tlas || e.reference == tlas) && &e == &ring.entries[build_camera_index(e.frame)]) found = e;
  return found;
}
} // namespace bg3::water
