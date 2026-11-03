#pragma once
#include <array>
// Minimal capture-layout fixture: scene set 0 b2 (224 B) and lighting CameraCB
// (400 B) from one frame. Deliberately distinct origins expose buffer mix-ups.
// Synthetic values, exactly representable, so translation checks need no epsilon.
struct WaterCameraCapture {
  std::array<float, 56> scene{};
  std::array<float, 100> lighting{};
  WaterCameraCapture() {
    scene[52] = 128.0f; scene[53] = 16.0f; scene[54] = -256.0f; scene[55] = 1.0f;
    lighting[52] = 0.25f; lighting[53] = -0.5f; lighting[54] = 0.75f; lighting[55] = 1.0f;
  }
};
