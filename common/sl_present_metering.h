#pragma once
#include <cstdint>

// This optional Vulkan capability needs both the extension and its feature.
// Key off never alters the application's own extension/feature selection.
inline bool sl_enable_present_metering(bool requested, bool extension, bool feature) {
  return requested && extension && feature;
}

struct SlMeteringOverride {
  uint32_t* caller=nullptr;
  uint32_t previous=0;
  void capture(uint32_t& field) { caller=&field; previous=field; }
  void restore() {
    if (caller) { *caller=previous; caller=nullptr; }
  }
};
