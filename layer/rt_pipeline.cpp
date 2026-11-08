#include "rt_pipeline.h"
#include "../common/rt_pipeline.h"
#include <cstring>

namespace bg3::rt_pipeline {
namespace {
bool g_requested = false, g_suppressed = false, g_enabled = false;
VkPhysicalDeviceRayTracingPipelineFeaturesKHR g_pipeline{};
VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT g_reorder{};
Bg3RtPipelineInfo g_info{};
const VkBaseInStructure* find_struct(const void* next, VkStructureType type) {
  for (auto p = static_cast<const VkBaseInStructure*>(next); p; p = p->pNext)
    if (p->sType == type) return p;
  return nullptr;
}
}  // namespace

const void* device_features(InstanceData* id, VkPhysicalDevice phys, const void* next, std::vector<const char*>& extensions) {
  g_requested = false;
  if (g_suppressed || !id || !id->GetPhysicalDeviceFeatures2) return next;
  DeviceQuery q;
  uint32_t n = 0;
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> avail(n);
  if (id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, avail.data()) != VK_SUCCESS) return next;
  for (const auto& a : avail) {
    q.pipeline_extension |= !strcmp(a.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    q.reorder_extension |= !strcmp(a.extensionName, VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME);
  }
  VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT sr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_EXT};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR sp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, &sr};
  VkPhysicalDeviceFeatures2 all{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &sp};
  if (q.pipeline_extension && q.reorder_extension) id->GetPhysicalDeviceFeatures2(phys, &all);
  q.pipeline_feature = sp.rayTracingPipeline != VK_FALSE;
  q.reorder_feature = sr.rayTracingInvocationReorder != VK_FALSE;
  const auto* ap = reinterpret_cast<const VkPhysicalDeviceRayTracingPipelineFeaturesKHR*>(find_struct(next, sp.sType));
  const auto* ar = reinterpret_cast<const VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT*>(find_struct(next, sr.sType));
  q.app_pipeline_struct = ap != nullptr; q.app_pipeline_value = ap && ap->rayTracingPipeline;
  q.app_reorder_struct = ar != nullptr; q.app_reorder_value = ar && ar->rayTracingInvocationReorder;
  const FeatureDecision d = decide_features(q);
  if (d.result == Feature::kUnsupported) {
    log("[rt pipeline] VK_KHR_ray_tracing_pipeline + VK_EXT_ray_tracing_invocation_reorder unavailable "
        "(extensions %d/%d, features %d/%d): the GI trace stays compute",
        q.pipeline_extension, q.reorder_extension, q.pipeline_feature, q.reorder_feature);
    return next;
  }
  if (d.result == Feature::kAppDisabled) {
    log("[rt pipeline] the application chains rayTracingPipeline or rayTracingInvocationReorder off: left alone");
    return next;
  }
  if (d.add_pipeline_struct) {
    g_pipeline = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    g_pipeline.rayTracingPipeline = VK_TRUE;
    g_pipeline.pNext = const_cast<void*>(next);
    next = &g_pipeline;
  }
  if (d.add_reorder_struct) {
    g_reorder = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_EXT};
    g_reorder.rayTracingInvocationReorder = VK_TRUE;
    g_reorder.pNext = const_cast<void*>(next);
    next = &g_reorder;
  }
  for (const char* e : {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME}) {
    bool listed = false;   // hair_groom may list the pipeline extension already (as an extension only)
    for (auto x : extensions) listed |= !strcmp(x, e);
    if (!listed) extensions.push_back(e);
  }
  g_requested = true;
  log("[rt pipeline] + VK_KHR_ray_tracing_pipeline (rayTracingPipeline), VK_EXT_ray_tracing_invocation_reorder "
      "(rayTracingInvocationReorder)%s", d.add_pipeline_struct ? "" : "; the application's pipeline feature kept");
  return next;
}
bool device_requested() { return g_requested; }
void disable_device_request() { g_requested = false; g_suppressed = true; }

void device_created(DeviceData* dd, bool rt_enabled) {
  g_info = Bg3RtPipelineInfo{};
  g_enabled = false;
  if (!g_requested || !rt_enabled || !dd) {
    if (g_requested) log("[rt pipeline] ray tracing is off on the device: the GI trace stays compute");
    return;
  }
  const bool fns = dd->vk.CreateRayTracingPipelinesKHR && dd->vk.GetRayTracingShaderGroupHandlesKHR && dd->vk.CmdTraceRaysKHR;
  auto props2 = dd->inst ? reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
                               dd->inst->gipa(dd->inst->instance, "vkGetPhysicalDeviceProperties2")) : nullptr;
  if (!fns || !props2) {
    log("[rt pipeline] entry points missing (pipelines %d, handles %d, trace %d, properties2 %d): the GI trace stays compute",
        dd->vk.CreateRayTracingPipelinesKHR != nullptr, dd->vk.GetRayTracingShaderGroupHandlesKHR != nullptr,
        dd->vk.CmdTraceRaysKHR != nullptr, props2 != nullptr);
    return;
  }
  VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT rp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_EXT};
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR pp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR, &rp};
  VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &pp};
  props2(dd->physical, &p2);
  const SbtLayout l = sbt_layout(pp.shaderGroupHandleSize, pp.shaderGroupHandleAlignment, pp.shaderGroupBaseAlignment);
  if (!l.ok || pp.maxRayRecursionDepth < 1) {
    log("[rt pipeline] unusable properties (handle %u, alignment %u, base %u, recursion %u): the GI trace stays compute",
        pp.shaderGroupHandleSize, pp.shaderGroupHandleAlignment, pp.shaderGroupBaseAlignment, pp.maxRayRecursionDepth);
    return;
  }
  g_info.available = 1;
  g_info.reorder_hint = rp.rayTracingInvocationReorderReorderingHint == VK_RAY_TRACING_INVOCATION_REORDER_MODE_REORDER_EXT ? 1u : 0u;
  g_info.handle_size = pp.shaderGroupHandleSize;
  g_info.handle_alignment = pp.shaderGroupHandleAlignment;
  g_info.base_alignment = pp.shaderGroupBaseAlignment;
  g_enabled = true;
  log("[rt pipeline] on the device: handle %u B, alignment %u, base alignment %u, reorder %s",
      g_info.handle_size, g_info.handle_alignment, g_info.base_alignment, g_info.reorder_hint ? "REORDER" : "NONE (no reordering)");
}
bool enabled() { return g_enabled; }
VkShaderStageFlags shared_stages() {
  return VK_SHADER_STAGE_COMPUTE_BIT | (g_enabled ? VK_SHADER_STAGE_RAYGEN_BIT_KHR : 0u);
}
const Bg3RtPipelineInfo& info() { return g_info; }
}  // namespace bg3::rt_pipeline
