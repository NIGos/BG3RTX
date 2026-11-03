#include "compatibility.h"
#include <sstream>

namespace launcher {
Compatibility assess_compatibility(const GraphicsInspection& probe, int selected) {
  Compatibility c;
  c.hardware_text = "Graphics compatibility could not be checked. Retry or create a report.";
  c.driver_text = "Driver unknown. Recheck graphics details.";
  c.memory_text = "Video memory budget unknown. No preset has long-session memory qualification.";
  if (!probe.error.empty() || probe.adapters.empty()) return c;
  if (selected < 0 && probe.adapters.size() == 1) selected = 0;
  if (selected < 0 || size_t(selected) >= probe.adapters.size()) {
    c.adapter_text = "Choose an adapter. The game's selected adapter is not confirmed.";
    return c;
  }
  const auto& a = probe.adapters[selected];
  c.adapter_text = "Selected adapter: " + a.gpu.name + ". Game adapter not confirmed.";
  c.caps = bg3::settings::gpu_caps(a.gpu);
  c.model_preset = bg3::settings::suggest_preset(a.gpu);
  if (!a.gpu.ray_query) {
    c.hardware = CheckState::Unsupported;
    c.hardware_text = "Hardware ray tracing unavailable. Low uses screen-space lighting.";
  } else if (!(c.caps & bg3::settings::kCapDlss)) {
    c.hardware = CheckState::Warning;
    c.hardware_text = "DLSS features require a compatible NVIDIA RTX GPU. Non-NVIDIA RT is untested; use Low.";
  } else {
    c.hardware = CheckState::Supported;
    c.hardware_text = "RTX and ray-query features detected. This is not rendering qualification.";
  }
  if (!a.driver_version.empty()) {
    c.driver = a.gpu.vendor == 0x10DE && a.driver_version == "621.01" ? CheckState::Supported : CheckState::Warning;
    c.driver_text = "Installed driver: " + a.driver_version + (c.driver == CheckState::Supported
      ? ". Recorded test driver; no verified minimum is known."
      : ". Untested driver. Recorded NVIDIA test reference: 621.01; no verified minimum is known. Recheck after updating through your vendor.");
  }
  if (a.dedicated_bytes) {
    const auto gb = a.dedicated_bytes / (uint64_t(1) << 30);
    std::string guide = gb < 12 ? "No qualified recommendation. Low is least demanding."
      : gb < 16 ? "Start with Low. Medium is high risk; High and Ultra are not recommended."
      : gb < 24 ? "Start with Low. Medium is unmeasured; High is high risk. Avoid Ultra."
      : gb < 32 ? "Low or Medium has more headroom. High is unqualified. Ultra is not recommended."
      : "More headroom, but Ultra remains at risk: a port view reached 31.7 GB against a 30.6 GB budget.";
    c.memory_text = std::to_string(gb) + " GiB card memory. Risk estimate at 4K-class output with DLSS Quality: " + guide;
    c.memory_text += " No card size has passed long-session memory qualification.";
  }
  if (a.budget_known) {
    const auto free = a.budget_bytes > a.usage_bytes ? a.budget_bytes - a.usage_bytes : 0;
    c.memory_text += " Driver budget: " + std::to_string(a.budget_bytes >> 20) + " MiB; current headroom: "
      + std::to_string(free >> 20) + " MiB. Snapshot only; preset demand is unqualified.";
  } else c.memory_text += " Driver budget unavailable.";
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
