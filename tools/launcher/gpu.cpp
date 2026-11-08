// GPU capabilities through Vulkan in the launcher's own process: vendor,
// device id, name and ray-query support (SETTINGS_DESIGN 4, 5.3).
#include "launcher.h"

#include <vulkan/vulkan.h>

#include <cstring>
#include <cstdio>
#include <mutex>
#include <filesystem>
#include "../../common/hair_groom_asset.h"

namespace launcher {

namespace st = bg3::settings;

// Read only the same index headers as the renderer; an empty/corrupt directory
// is not local groom availability. The caller supplies the resolved database folder.
bool local_grooms_available(const std::wstring& directory) {
  WIN32_FIND_DATAW entry{};
  HANDLE search = FindFirstFileW((directory + L"\\hair_grooms\\*.hgrm").c_str(), &entry);
  if (search == INVALID_HANDLE_VALUE) return false;
  bool available = false;
  do {
    if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    FILE* file = nullptr;
    auto path = directory + L"\\hair_grooms\\" + entry.cFileName;
    if (_wfopen_s(&file, path.c_str(), L"rb") || !file) continue;
    uint8_t header[40 + bg3::hair_groom::kSamples * 8]{};
    const size_t bytes = fread(header, 1, sizeof header, file);
    bg3::hair_groom::Asset asset;
    uint64_t at = 0;
    bool valid = bg3::hair_groom::parse_header(header, bytes, asset, &at);
    for (uint32_t lod = 0; valid && lod < asset.lod_count; ++lod) {
      uint8_t h16[16]; uint32_t points = 0, segments = 0;
      uint64_t next = 0;
      valid = _fseeki64(file, int64_t(at), SEEK_SET) == 0 && fread(h16, 1, 16, file) == 16 &&
              bg3::hair_groom::lod_header(h16, at, &points, &segments, &next);
      at = asset.version == 3 ? at + 16 : next;   // v3: one table of LOD headers (common/hair_groom_v3.h)
    }
    fclose(file);
    available = valid;
  } while (!available && FindNextFileW(search, &entry));
  FindClose(search);
  return available;
}

GraphicsInspection inspect_graphics() {
  static std::mutex probe_mutex;
  std::lock_guard<std::mutex> guard(probe_mutex);
  GraphicsInspection result;
  auto& out = result.adapters;
  // Restore the caller's environment exactly on every exit. No Vulkan device
  // is created; the loader handle and instance are owned by this inspection.
  struct Scope {
    HMODULE library = nullptr;
    std::wstring previous;
    bool existed = false;
    Scope() {
      DWORD n = GetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", nullptr, 0);
      existed = n != 0;
      if (n) { previous.resize(n); GetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", previous.data(), n); previous.resize(n - 1); }
    }
    ~Scope() {
      SetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", existed ? previous.c_str() : nullptr);
      if (library) FreeLibrary(library);
    }
  } scope;
  // No implicit layers (overlays, and never our own) in the probe instance.
  SetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", L"~all~");
  HMODULE vk = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  scope.library = vk;
  auto fail = [&](const wchar_t* why) {
    result.error.clear();
    for (const wchar_t* c = why; *c; ++c) result.error += char(*c); // All messages below are ASCII.
    result.adapters.clear();
    return result;
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
  LOAD(vkGetPhysicalDeviceMemoryProperties);
  LOAD(vkGetPhysicalDeviceMemoryProperties2);
#undef LOAD
  if (!vkDestroyInstance || !vkEnumeratePhysicalDevices || !vkGetPhysicalDeviceProperties ||
      !vkGetPhysicalDeviceFeatures2 || !vkEnumerateDeviceExtensionProperties || !vkGetPhysicalDeviceMemoryProperties) {
    if (vkDestroyInstance) vkDestroyInstance(inst, nullptr);
    return fail(L"Vulkan inspection functions unavailable. Recheck the driver.");
  }
  wchar_t location[32768]{};
  std::filesystem::path database_directory;
  DWORD length = GetEnvironmentVariableW(L"BG3RTX_ASSETDB", location, DWORD(std::size(location)));
  if (length && length < std::size(location)) database_directory = std::filesystem::path(location).parent_path();
  else {
    GetModuleFileNameW(nullptr, location, DWORD(std::size(location)));
    database_directory = std::filesystem::path(location).parent_path().parent_path() / L"data" / L"assetdb";
  }
  const bool grooms = local_grooms_available(database_directory.wstring());
  uint32_t n = 0;
  if (vkEnumeratePhysicalDevices(inst, &n, nullptr) != VK_SUCCESS || n > 64) {
    vkDestroyInstance(inst, nullptr); return fail(L"Could not enumerate Vulkan adapters.");
  }
  std::vector<VkPhysicalDevice> devs(n);
  if (n && vkEnumeratePhysicalDevices(inst, &n, devs.data()) != VK_SUCCESS) {
    vkDestroyInstance(inst, nullptr); return fail(L"Adapter list changed. Retry inspection.");
  }
  devs.resize(n);
  for (VkPhysicalDevice d : devs) {
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(d, &p);
    uint32_t ne = 0;
    if (vkEnumerateDeviceExtensionProperties(d, nullptr, &ne, nullptr) != VK_SUCCESS || ne > 4096) {
      vkDestroyInstance(inst, nullptr); return fail(L"Could not inspect adapter extensions.");
    }
    std::vector<VkExtensionProperties> ext(ne);
    if (ne && vkEnumerateDeviceExtensionProperties(d, nullptr, &ne, ext.data()) != VK_SUCCESS) {
      vkDestroyInstance(inst, nullptr); return fail(L"Extension list changed. Retry inspection.");
    }
    ext.resize(ne);
    bool rq = false, as = false, omm = false, budget = false, bda = false, lss = false, pipeline = false;
    for (auto& e : ext) {
      if (!strcmp(e.extensionName, VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME)) lss = true;
      if (!strcmp(e.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) pipeline = true;
      if (!strcmp(e.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME)) rq = true;
      if (!strcmp(e.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)) as = true;
      if (!strcmp(e.extensionName, VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME)) omm = true;
      if (!strcmp(e.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) budget = true;
      if (!strcmp(e.extensionName, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)) bda = true;
    }
    st::Gpu g;
    g.vendor = p.vendorID;
    g.device = p.deviceID;
    g.name = p.deviceName;
    g.hair_grooms = grooms;
    VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV spheres{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV};
    VkPhysicalDeviceFeatures2 basics{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    if (lss && pipeline) basics.pNext = &spheres;
    vkGetPhysicalDeviceFeatures2(d, &basics);
    g.linear_swept_spheres = lss && pipeline && spheres.linearSweptSpheres;
    g.shader_int64 = basics.features.shaderInt64 != VK_FALSE;
    g.fragment_stores = basics.features.fragmentStoresAndAtomics != VK_FALSE;
    if (rq && as && (bda || p.apiVersion >= VK_API_VERSION_1_2)) {
      VkPhysicalDeviceBufferDeviceAddressFeatures address{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
      VkPhysicalDeviceRayQueryFeaturesKHR ray{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &address};
      VkPhysicalDeviceAccelerationStructureFeaturesKHR accel{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &ray};
      VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &accel};
      vkGetPhysicalDeviceFeatures2(d, &features);
      g.ray_query = ray.rayQuery && accel.accelerationStructure && address.bufferDeviceAddress;
    }
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
    GraphicsAdapter a;
    a.gpu = g; a.api_version = p.apiVersion; a.driver_raw = p.driverVersion;
    if (g.vendor == 0x10DE) {
      char driver[64];
      snprintf(driver, sizeof(driver), "%u.%02u", p.driverVersion >> 22, (p.driverVersion >> 14) & 255);
      a.driver_version = driver;
    } // Other vendors' encodings are not assumed to be Vulkan version numbers.
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDeviceMemoryBudgetPropertiesEXT mb{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    if (budget && vkGetPhysicalDeviceMemoryProperties2) {
      VkPhysicalDeviceMemoryProperties2 mp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, &mb};
      vkGetPhysicalDeviceMemoryProperties2(d, &mp); memory = mp.memoryProperties; a.budget_known = true;
    } else vkGetPhysicalDeviceMemoryProperties(d, &memory);
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i) if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
      if (g.discrete) a.dedicated_bytes += memory.memoryHeaps[i].size; // UMA is not dedicated VRAM.
      if (a.budget_known) { a.budget_bytes += mb.heapBudget[i]; a.usage_bytes += mb.heapUsage[i]; }
    }
    out.push_back(a);
  }
  vkDestroyInstance(inst, nullptr);
  if (out.empty()) result.error = "No Vulkan GPU found. Install or recheck the graphics driver.";
  return result;
}

std::vector<st::Gpu> probe_gpus(std::wstring* err) {
  auto result = inspect_graphics();
  if (err) err->assign(result.error.begin(), result.error.end());
  std::vector<st::Gpu> out;
  for (const auto& a : result.adapters) out.push_back(a.gpu);
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
