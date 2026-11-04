#include "compatibility.h"
#include <sstream>

namespace launcher {
Compatibility assess_compatibility(const GraphicsInspection& probe, int selected) {
  Compatibility c;
  c.hardware_text = "Graphics check failed. Recheck graphics or create a report.";
  c.driver_text = "Driver not detected. Recheck graphics.";
  c.memory_text = "Available video memory is unknown. Long-session memory use is untested.";
  if (!probe.error.empty() || probe.adapters.empty()) return c;
  if (selected < 0 && probe.adapters.size() == 1) selected = 0;
  if (selected < 0 || size_t(selected) >= probe.adapters.size()) {
    c.hardware_text = "Several GPUs found. Choose the one the game uses under Game and graphics.";
    c.driver_text.clear();
    return c;
  }
  const auto& a = probe.adapters[selected];
  c.adapter_text = "Selected GPU: " + a.gpu.name + ". The game's GPU has not been confirmed.";
  c.caps = bg3::settings::gpu_caps(a.gpu);
  c.model_preset = bg3::settings::suggest_preset(a.gpu);
  if (!a.gpu.ray_query) {
    c.hardware = CheckState::Unsupported;
    c.hardware_text = "Hardware ray tracing unavailable. Use Low.";
  } else if (!(c.caps & bg3::settings::kCapDlss)) {
    c.hardware = CheckState::Warning;
    c.hardware_text = "DLSS needs an NVIDIA RTX GPU. BG3RTX is tested only on an RTX 5090; on this GPU use Low.";
  } else {
    c.hardware = CheckState::Supported;
    c.hardware_text = "RTX ray tracing detected. BG3RTX is tested only on an RTX 5090; other RTX GPUs are untested.";
  }
  if (!a.driver_version.empty()) {
    c.driver = a.gpu.vendor == 0x10DE && a.driver_version == "621.01" ? CheckState::Supported : CheckState::Warning;
    c.driver_text = "Driver: " + a.driver_version + ". Tested reference: NVIDIA 621.01; minimum version unknown.";
  }
  if (a.dedicated_bytes) {
    const auto gb = a.dedicated_bytes / (uint64_t(1) << 30);
    std::string guide = gb < 12 ? "Low uses the least memory."
      : gb < 16 ? "Start with Low; higher presets may exceed memory."
      : gb < 24 ? "Start with Low; Medium is unmeasured, and High or Ultra may exceed memory."
      : gb < 32 ? "Start with Low or Medium; High is untested and Ultra may exceed memory."
      : "Ultra may exceed available memory even on a 32 GiB GPU.";
    c.memory_text = std::to_string(gb) + " GiB GPU memory. Guidance is for approximately 4K output with DLSS Quality. " + guide;
    c.memory_text += " Long-session memory use is untested.";
  }
  if (a.budget_known) {
    const auto free = a.budget_bytes > a.usage_bytes ? a.budget_bytes - a.usage_bytes : 0;
    c.memory_text += " Available budget: " + std::to_string(a.budget_bytes >> 20) + " MiB; currently free: "
      + std::to_string(free >> 20) + " MiB. Values change during play.";
  } else c.memory_text += " Available memory budget unknown.";
  return c;
}

std::string graphics_report(const GraphicsInspection& p) {
  std::ostringstream s;
  if (!p.error.empty()) s << "Probe error: " << p.error << '\n';
  if (p.adapters.empty()) s << "No graphics adapter information available.\n";
  for (size_t i = 0; i < p.adapters.size(); ++i) {
    const auto& a = p.adapters[i];
    auto c = assess_compatibility(p, int(i));
    s << "Adapter " << i << ": " << a.gpu.name << "\nVendor/device: " << a.gpu.vendor << '/' << a.gpu.device
      << "\nVulkan API/raw driver: " << a.api_version << '/' << a.driver_raw
      << "\nCapabilities: " << c.caps << '\n' << c.hardware_text << '\n' << c.driver_text
      << '\n' << c.memory_text << '\n' << c.adapter_text << "\n\n";
  }
  return s.str();
}
} // namespace launcher
