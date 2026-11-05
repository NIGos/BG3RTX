// When slSetVulkanInfo may run (layer/streamline.cpp sl_device_ready). No Vulkan.
//
// NGX, initialised inside slSetVulkanInfo, resolves its device functions through
// vulkan-1's vkGetDeviceProcAddr. The loader fills the device's dispatch table only
// after the whole layer chain's vkCreateDevice has returned. A layer above ours
// (ReShade) calls vkGetDeviceQueue inside its own vkCreateDevice, so "the first
// vkGetDeviceQueue" can arrive before that: NGX then gets null for every device
// function and Streamline crashes on vkGetBufferDeviceAddress (2026-11-05).
// The game's NGX Init is the first point that is surely after vkCreateDevice
// returned to the game, and still before the game uses NGX.
#pragma once

namespace bg3::sl_init {

enum class Trigger {
  DeviceQueue,   // the first vkGetDeviceQueue/2 on the device
  GameNgxInit,   // the game calls NVSDK_NGX_VULKAN_Init* (stubbed while NGX is shared)
  Swapchain,     // the first vkCreateSwapchainKHR
};

// at: sl_init_at (0 = early, before the game's NGX; 1 = at the first swapchain).
// after_create: sl_init_after_create (move the early point from the first
// vkGetDeviceQueue to the game's NGX Init).
inline bool init_now(int at, bool after_create, Trigger t) {
  if (t == Trigger::Swapchain) return true;   // always after vkCreateDevice returned
  if (at != 0) return false;
  return after_create ? t == Trigger::GameNgxInit : t == Trigger::DeviceQueue;
}

}  // namespace bg3::sl_init
