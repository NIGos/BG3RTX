// gi_rt_pipeline (docs/TRACK_C_GI.md "Ray tracing pipeline GI trace"): VK_KHR_ray_tracing_pipeline and
// VK_EXT_ray_tracing_invocation_reorder at device creation, and what the core needs to build ray tracing
// pipelines (Bg3HostApi::rt_pipeline, ABI 29). Only when the device supports both; never overrides the
// application's chain; dropped first when device creation fails (layer.cpp's optional retry).
#pragma once
#include "host.h"
#include "../common/host_api.h"

namespace bg3::rt_pipeline {
const void* device_features(InstanceData*, VkPhysicalDevice, const void* next, std::vector<const char*>& extensions);
bool device_requested();
void disable_device_request();   // optional-device retry: ordinary RT is retained
void device_created(DeviceData*, bool rt_enabled);
// The device has both features (the descriptor set layouts the GI raygen binds add the raygen stage then).
bool enabled();
// Stage flags for the descriptor set layouts the raygen GI trace shares with compute passes.
VkShaderStageFlags shared_stages();
const Bg3RtPipelineInfo& info();
}  // namespace bg3::rt_pipeline
