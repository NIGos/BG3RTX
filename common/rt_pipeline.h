// gi_rt_pipeline (docs/TRACK_C_GI.md "Ray tracing pipeline GI trace"): the pure decisions of the ray tracing
// pipeline GI trace, shared by the layer (device features, layer/rt_pipeline.cpp) and the core (the shader binding
// table and the compute fallback, core/gi.cpp hit_gi_rt). Unit-tested by tests/rt_pipeline_test.cpp.
#pragma once
#include <cstdint>

namespace bg3::rt_pipeline {

// ---- device features (layer) ----
// What the device can do and what the application already chained for VK_KHR_ray_tracing_pipeline and
// VK_EXT_ray_tracing_invocation_reorder. The GI raygen modules declare the reorder capability, so both are needed.
struct DeviceQuery {
  bool pipeline_extension = false, reorder_extension = false;   // listed by vkEnumerateDeviceExtensionProperties
  bool pipeline_feature = false, reorder_feature = false;       // vkGetPhysicalDeviceFeatures2
  // The application's create info: a feature struct present in its chain, and its value.
  bool app_pipeline_struct = false, app_pipeline_value = false;
  bool app_reorder_struct = false, app_reorder_value = false;
};
enum class Feature : uint32_t {
  kRequest = 0,        // add the extensions and the feature structs we need (keep the application's)
  kUnsupported = 1,    // the device lacks an extension or a feature: compute only
  kAppDisabled = 2,    // the application chains a struct with the feature off: never override it
};
struct FeatureDecision {
  Feature result = Feature::kUnsupported;
  bool add_pipeline_struct = false, add_reorder_struct = false;   // chain our own struct (the app has none)
};
inline FeatureDecision decide_features(const DeviceQuery& q) {
  FeatureDecision d;
  if (!q.pipeline_extension || !q.reorder_extension || !q.pipeline_feature || !q.reorder_feature) return d;
  if ((q.app_pipeline_struct && !q.app_pipeline_value) || (q.app_reorder_struct && !q.app_reorder_value)) {
    d.result = Feature::kAppDisabled;
    return d;
  }
  d.result = Feature::kRequest;
  d.add_pipeline_struct = !q.app_pipeline_struct;
  d.add_reorder_struct = !q.app_reorder_struct;
  return d;
}

// ---- shader binding table (core) ----
// One raygen record per slot, no miss / hit / callable records. Slots never move once written, so a slot a
// recorded frame still reads is never rewritten (the core assigns one slot per pipeline, round robin).
struct SbtLayout {
  uint32_t stride = 0;       // VkStridedDeviceAddressRegionKHR stride and size of the raygen region
  uint32_t slot_bytes = 0;   // distance between slots (a multiple of the base alignment)
  bool ok = false;
};
inline bool power_of_two(uint32_t v) { return v && !(v & (v - 1)); }
inline uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }
// From VkPhysicalDeviceRayTracingPipelinePropertiesKHR: the raygen region's size must equal its stride, the stride
// a multiple of shaderGroupHandleAlignment, and its address a multiple of shaderGroupBaseAlignment.
inline SbtLayout sbt_layout(uint32_t handle_size, uint32_t handle_alignment, uint32_t base_alignment) {
  SbtLayout l;
  if (!handle_size || !power_of_two(handle_alignment) || !power_of_two(base_alignment)) return l;
  l.stride = uint32_t(align_up(handle_size, handle_alignment));
  l.slot_bytes = uint32_t(align_up(l.stride, base_alignment));
  l.ok = true;
  return l;
}
// Bytes of a buffer holding `slots` records at any buffer address (the first record is aligned up inside it).
inline uint64_t sbt_buffer_bytes(const SbtLayout& l, uint32_t slots, uint32_t base_alignment) {
  return l.ok ? uint64_t(l.slot_bytes) * slots + base_alignment : 0;
}
// Device address and mapped byte offset of slot `slot` in a buffer at `address`.
inline uint64_t sbt_slot_address(uint64_t address, const SbtLayout& l, uint32_t slot, uint32_t base_alignment) {
  return align_up(address, base_alignment) + uint64_t(slot) * l.slot_bytes;
}

// ---- the dispatch path (core) ----
// Why the hit-GI trace runs as compute this frame (kRaygen: it runs as a ray tracing pipeline).
enum class Path : uint32_t {
  kRaygen = 0,
  kKeyOff = 1,          // gi_rt_pipeline=0
  kNoDevice = 2,        // the layer did not enable the features (Bg3RtPipelineInfo::available == 0)
  kNoEntryPoints = 3,   // vkCreateRayTracingPipelinesKHR / handles / trace rays missing
  kVariant = 4,         // a variant without a raygen request (SHaRC, the direct-only files)
  kNotReady = 5,        // the pipeline or its SBT slot is not built yet (it builds in the background)
};
inline Path choose_path(bool key, bool device, bool entry_points, bool raygen_variant, bool ready) {
  if (!key) return Path::kKeyOff;
  if (!device) return Path::kNoDevice;
  if (!entry_points) return Path::kNoEntryPoints;
  if (!raygen_variant) return Path::kVariant;
  if (!ready) return Path::kNotReady;
  return Path::kRaygen;
}
inline const char* path_name(Path p) {
  switch (p) {
    case Path::kRaygen: return "ray tracing pipeline";
    case Path::kKeyOff: return "key off";
    case Path::kNoDevice: return "no ray tracing pipeline + reorder features on the device";
    case Path::kNoEntryPoints: return "ray tracing pipeline entry points missing";
    case Path::kVariant: return "this variant has no raygen request (SHaRC or direct-only)";
    case Path::kNotReady: return "the raygen pipeline is still building";
  }
  return "?";
}
// gi_rt_pipeline_reorder -> specialization constant 1050 (kSpecIdGiSer): 0 no reorder (no prepass), 1 hit kind,
// 2 hit kind + instance. The shader's values are 0 none, 1 kind + instance, 2 kind.
inline uint32_t reorder_spec(int key) { return key == 1 ? 2u : key == 2 ? 1u : 0u; }

}  // namespace bg3::rt_pipeline
