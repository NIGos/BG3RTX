#pragma once
#include "../../common/settings.h"
#include <cstdint>
#include <string>
#include <vector>

namespace launcher {
// Read-only Vulkan observations. No settings writes, device creation, game launch,
// save discovery, network requests or driver installation. Index is probe-local;
// never claim it is the adapter selected by the game.
struct GraphicsAdapter {
  bg3::settings::Gpu gpu;
  uint32_t api_version = 0, driver_raw = 0;
  std::string driver_version;
  uint64_t dedicated_bytes = 0, budget_bytes = 0, usage_bytes = 0;
  bool budget_known = false;
};
struct GraphicsInspection {
  std::vector<GraphicsAdapter> adapters;
  std::string error;
};
// Serializes its probes and restores loader environment on return. A2 must not
// concurrently start a game while the process-local loader environment is set.
GraphicsInspection inspect_graphics();
enum class CheckState { Unknown, Supported, Warning, Unsupported };
struct Compatibility {
  CheckState hardware = CheckState::Unknown, driver = CheckState::Unknown;
  unsigned caps = 0; // Unknown is never ~0u. Use A1 choice_available/preset_values_for.
  int model_preset = bg3::settings::kLow; // Model estimate, never a memory qualification.
  std::string hardware_text, driver_text, memory_text, adapter_text;
};
// selected < 0 requires explicit adapter selection when more than one is found.
// No verified driver minimum exists in README: 621.01 is a test reference only.
Compatibility assess_compatibility(const GraphicsInspection&, int selected = -1);
// Initial adapter when the user has not chosen one: the only adapter, or the single best
// one (discrete, ray tracing, NVIDIA first, as best_gpu). A tie at the top returns -1 so
// the user chooses (e.g. two equal RTX cards). A discrete RTX next to an iGPU is not a tie.
int default_adapter(const GraphicsInspection&);
std::string graphics_report(const GraphicsInspection&);
} // namespace launcher
