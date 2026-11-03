// CPU roof-query policy. Whole-level selection has no camera/readback dependency.
#pragma once
#include <cstdint>
#include <limits>

struct SunRoofSelection {
  bool valid = false, world = false;
  int32_t level = -2;
  float pad = 0, up = 0;
  float center[3] = {};
  float radius = 0;

  bool update(int32_t next_level, bool next_world, float next_pad, float next_up,
              const float cam[3], float local_radius, uint64_t calls) {
    const float dx = cam[0] - center[0], dy = cam[1] - center[1], dz = cam[2] - center[2];
    const bool refresh = !valid || level != next_level || world != next_world ||
        pad != next_pad || up != next_up ||
        (!next_world && (calls % 30 == 0 || dx * dx + dy * dy + dz * dz > 25.0f));
    if (!refresh) return false;
    valid = true;
    level = next_level;
    world = next_world;
    pad = next_pad;
    up = next_up;
    // A fixed origin also makes overflow ordering independent of the camera.
    for (int k = 0; k < 3; ++k) center[k] = world ? 0.0f : cam[k];
    radius = world ? std::numeric_limits<float>::max() : local_radius;
    return true;
  }
};
