#include "../common/camera_epoch.h"
#include <limits>

int main() {
  float geometry[56]{}, lighting[56]{};
  geometry[0] = lighting[0] = geometry[5] = lighting[5] = geometry[10] = lighting[10] = 1;
  geometry[52] = lighting[52] = 120;
  lighting[32] = .001f; // projection jitter is independent of the camera pose
  lighting[55] = std::numeric_limits<float>::quiet_NaN(); // native packed footer
  if (!same_camera_pose(geometry, lighting)) return 1;
  lighting[52] += .001f;
  if (same_camera_pose(geometry, lighting)) return 2;
  lighting[52] = geometry[52];
  lighting[1] = .001f;
  if (same_camera_pose(geometry, lighting)) return 3;
  lighting[1] = 0;
  geometry[53] = lighting[53] = std::numeric_limits<float>::quiet_NaN();
  return same_camera_pose(geometry, lighting) ? 4 : 0;
}
