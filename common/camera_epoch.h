#pragma once
#include <cmath>

// Native camera UBO: view at float 0, projection at 32, absolute origin at 52.
// Uniform upload-ring addresses and projection jitter do not identify a pose.
inline bool same_camera_pose(const float* a, const float* b) {
  constexpr int fields[] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 52, 53, 54};
  for (int i : fields)
    if (!std::isfinite(a[i]) || a[i] != b[i]) return false;
  return true;
}
