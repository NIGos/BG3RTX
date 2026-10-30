// GPU capabilities through Vulkan in the launcher's own process: vendor,
// device id, name and ray-query support (SETTINGS_DESIGN 4, 5.3).
#include "launcher.h"

#include <vulkan/vulkan.h>

#include <cstring>

namespace launcher {

namespace st = bg3::settings;

std::vector<st::Gpu> probe_gpus(std::wstring* err) {
  std::vector<st::Gpu> out;
  // No implicit layers (overlays, and never our own) in the probe instance.
  SetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", L"~implicit~");
  HMODULE vk = LoadLibraryW(L"vulkan-1.dll");
  auto fail = [&](const wchar_t* why) {
    if (err) *err = why;
    SetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", nullptr);
    return out;
  };
  if (!vk) return fail(L"vulkan-1.dll not found (no Vulkan driver)");
  auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vk, "vkGetInstanceProcAddr"));
  auto create = gipa ? reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance")) : nullptr;
  if (!create) return fail(L"vkGetInstanceProcAddr missing");

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "BG3RTX launcher";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo = &app;
  VkInstance inst = VK_NULL_HANDLE;
  if (create(&ci, nullptr, &inst) != VK_SUCCESS) return fail(L"vkCreateInstance failed");

#define LOAD(name) auto name = reinterpret_cast<PFN_##name>(gipa(inst, #name))
  LOAD(vkDestroyInstance);
  LOAD(vkEnumeratePhysicalDevices);
  LOAD(vkGetPhysicalDeviceProperties);
  LOAD(vkGetPhysicalDeviceFeatures2);
  LOAD(vkEnumerateDeviceExtensionProperties);
#undef LOAD
  uint32_t n = 0;
  vkEnumeratePhysicalDevices(inst, &n, nullptr);
  std::vector<VkPhysicalDevice> devs(n);
  if (n) vkEnumeratePhysicalDevices(inst, &n, devs.data());
  for (VkPhysicalDevice d : devs) {
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(d, &p);
    uint32_t ne = 0;
    vkEnumerateDeviceExtensionProperties(d, nullptr, &ne, nullptr);
    std::vector<VkExtensionProperties> ext(ne);
    if (ne) vkEnumerateDeviceExtensionProperties(d, nullptr, &ne, ext.data());
    bool rq = false, as = false, omm = false;
    for (auto& e : ext) {
      if (!strcmp(e.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME)) rq = true;
      if (!strcmp(e.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)) as = true;
      if (!strcmp(e.extensionName, VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME)) omm = true;
    }
    st::Gpu g;
    g.vendor = p.vendorID;
    g.device = p.deviceID;
    g.name = p.deviceName;
    g.ray_query = rq && as;
    g.discrete = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    if(omm && rq && as && p.apiVersion>=VK_API_VERSION_1_3 && vkGetPhysicalDeviceFeatures2){
      VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
      VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,&f13};
      VkPhysicalDeviceOpacityMicromapFeaturesEXT mm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT,&f12};
      VkPhysicalDeviceRayQueryFeaturesKHR ray{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,&mm};
      VkPhysicalDeviceAccelerationStructureFeaturesKHR accel{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,&ray};
      VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&accel};
      vkGetPhysicalDeviceFeatures2(d,&features);
      g.opacity_micromap=mm.micromap && f12.bufferDeviceAddress && f12.timelineSemaphore && f13.synchronization2 && ray.rayQuery && accel.accelerationStructure;
    }
    out.push_back(g);
  }
  vkDestroyInstance(inst, nullptr);
  SetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", nullptr);
  if (out.empty() && err) *err = L"no Vulkan GPU found";
  return out;
}

// The GPU the game will most likely use: discrete, ray tracing, NVIDIA first.
int best_gpu(const std::vector<st::Gpu>& gpus) {
  int best = -1, best_score = -1;
  for (int i = 0; i < int(gpus.size()); ++i) {
    const st::Gpu& g = gpus[i];
    int score = (g.discrete ? 4 : 0) + (g.ray_query ? 2 : 0) + (g.vendor == 0x10DE ? 1 : 0);
    if (score > best_score) best = i, best_score = score;
  }
  return best;
}

}  // namespace launcher
