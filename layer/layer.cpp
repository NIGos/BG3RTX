// Vulkan layer entry points: loader negotiation, instance/device creation,
// dispatch, and the frame boundary (submit/present).
#include "host.h"

#include <cstring>
#include <unordered_set>

namespace bg3 {

using HookMap = std::unordered_map<std::string, PFN_vkVoidFunction>;
void register_tracker_hooks(HookMap& m);
void register_cmd_hooks(HookMap& m);
void register_geo_hooks(HookMap& m);

static std::shared_mutex g_mu;
static std::unordered_map<void*, InstanceData*> g_instances;
static std::unordered_map<void*, DeviceData*> g_devices;
static DeviceData* g_main = nullptr;
static std::atomic<uint64_t> g_frame{0};
static bool g_core_started = false;

static void* dkey(const void* handle) { return *static_cast<void* const*>(handle); }

DeviceData* main_device() { return g_main; }
uint64_t frame_index() { return g_frame.load(); }

static InstanceData* instance_of(const void* handle) {
  std::shared_lock<std::shared_mutex> lock(g_mu);
  auto it = g_instances.find(dkey(handle));
  return it == g_instances.end() ? nullptr : it->second;
}

DeviceData* device_of(const void* handle) {
  std::shared_lock<std::shared_mutex> lock(g_mu);
  auto it = g_devices.find(dkey(handle));
  return it == g_devices.end() ? nullptr : it->second;
}

// ---- instance ----

static VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* alloc,
                                                     VkInstance* out) {
  cfg_poll();  // settings that matter before the first present (dump_shaders)
  ngx_init();  // BG3 loads _nvngx.dll after device creation; hook it as it maps
  auto* link = static_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(ci->pNext));
  while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
    link = static_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(link->pNext));
  if (!link) return VK_ERROR_INITIALIZATION_FAILED;

  PFN_vkGetInstanceProcAddr next = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  link->u.pLayerInfo = link->u.pLayerInfo->pNext;
  auto create = reinterpret_cast<PFN_vkCreateInstance>(next(VK_NULL_HANDLE, "vkCreateInstance"));
  VkResult r = create(ci, alloc, out);
  if (r != VK_SUCCESS) return r;

  auto* id = new InstanceData;
  id->instance = *out;
  id->gipa = next;
#define LOADI(n) id->n = reinterpret_cast<PFN_vk##n>(next(*out, "vk" #n))
  LOADI(DestroyInstance);
  LOADI(GetPhysicalDeviceQueueFamilyProperties);
  LOADI(GetPhysicalDeviceProperties);
  LOADI(EnumerateDeviceExtensionProperties);
  LOADI(GetPhysicalDeviceMemoryProperties);
#undef LOADI
  {
    std::unique_lock<std::shared_mutex> lock(g_mu);
    g_instances[dkey(*out)] = id;
  }
  const VkApplicationInfo* app = ci->pApplicationInfo;
  log("[layer] instance created: app '%s' engine '%s' api %u.%u, %u extensions",
      app && app->pApplicationName ? app->pApplicationName : "?", app && app->pEngineName ? app->pEngineName : "?",
      app ? VK_API_VERSION_MAJOR(app->apiVersion) : 0, app ? VK_API_VERSION_MINOR(app->apiVersion) : 0,
      ci->enabledExtensionCount);
  return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc) {
  InstanceData* id = instance_of(instance);
  if (!id) return;
  id->DestroyInstance(instance, alloc);
  std::unique_lock<std::shared_mutex> lock(g_mu);
  g_instances.erase(dkey(instance));
  delete id;
}

// ---- device ----

static VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                   const VkAllocationCallbacks* alloc, VkDevice* out) {
  const VkDeviceCreateInfo* const ci_orig = ci;
  auto* link = static_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(ci->pNext));
  while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
    link = static_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(link->pNext));
  if (!link) return VK_ERROR_INITIALIZATION_FAILED;

  InstanceData* id = instance_of(phys);
  PFN_vkGetInstanceProcAddr gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  PFN_vkGetDeviceProcAddr gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
  link->u.pLayerInfo = link->u.pLayerInfo->pNext;

  // Future: Streamline's DLSS-G device requirements (extensions, extra queues)
  // get merged into a copy of `ci` here.
  // Track D: VK_EXT_transform_feedback for the geometry capture (geo_capture=1).
  VkDeviceCreateInfo geo_ci;
  const VkDeviceCreateInfo* use_ci = geo_device_create_info(id, phys, ci, &geo_ci);
  auto create = reinterpret_cast<PFN_vkCreateDevice>(gipa(id ? id->instance : VK_NULL_HANDLE, "vkCreateDevice"));
  VkResult r = create(phys, use_ci, alloc, out);
  if (r != VK_SUCCESS && use_ci != ci) {
    log("[layer] device creation with the geometry capture extensions failed (%d); retrying without", r);
    use_ci = ci;
    r = create(phys, ci, alloc, out);
  }
  if (r != VK_SUCCESS) return r;
  ci = use_ci;

  auto* dd = new DeviceData;
  dd->device = *out;
  dd->physical = phys;
  dd->inst = id;
  dd->gdpa = gdpa;
#define LOADD(n)                                                                             \
  dd->vk.n = reinterpret_cast<PFN_vk##n>(gdpa(*out, "vk" #n));                              \
  if (!dd->vk.n) dd->vk.n = reinterpret_cast<PFN_vk##n>(gdpa(*out, "vk" #n "KHR"));
  BG3RTX_DEVICE_FUNCS(LOADD)
#undef LOADD

  for (uint32_t i = 0; i < ci->enabledExtensionCount; ++i) dd->extensions.emplace_back(ci->ppEnabledExtensionNames[i]);

  if (id && id->GetPhysicalDeviceQueueFamilyProperties) {
    uint32_t n = 0;
    id->GetPhysicalDeviceQueueFamilyProperties(phys, &n, nullptr);
    std::vector<VkQueueFamilyProperties> fam(n);
    id->GetPhysicalDeviceQueueFamilyProperties(phys, &n, fam.data());
    for (uint32_t i = 0; i < ci->queueCreateInfoCount && dd->graphics_family == UINT32_MAX; ++i) {
      uint32_t f = ci->pQueueCreateInfos[i].queueFamilyIndex;
      if (f < n && (fam[f].queueFlags & VK_QUEUE_GRAPHICS_BIT)) dd->graphics_family = f;
    }
    std::string queues;
    for (uint32_t i = 0; i < ci->queueCreateInfoCount; ++i) {
      const auto& q = ci->pQueueCreateInfos[i];
      char b[96];
      snprintf(b, sizeof b, " family%u x%u (flags 0x%x)", q.queueFamilyIndex, q.queueCount,
               q.queueFamilyIndex < n ? fam[q.queueFamilyIndex].queueFlags : 0);
      queues += b;
    }
    log("[layer] device created, queues:%s", queues.c_str());
  }
  if (id && id->GetPhysicalDeviceProperties) {
    VkPhysicalDeviceProperties p;
    id->GetPhysicalDeviceProperties(phys, &p);
    log("[layer] GPU '%s' api %u.%u.%u driver 0x%08x", p.deviceName, VK_API_VERSION_MAJOR(p.apiVersion),
        VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion), p.driverVersion);
  }
  std::string exts;
  for (auto& e : dd->extensions) exts += " " + e;
  log("[layer] device extensions:%s", exts.c_str());

  {
    std::unique_lock<std::shared_mutex> lock(g_mu);
    g_devices[dkey(*out)] = dd;
    if (!g_main) g_main = dd;
  }
  if (dd == g_main) geo_device_created(dd, use_ci != ci_orig);
  watchdog_start();
  return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
  DeviceData* dd = device_of(device);
  if (!dd) return;
  if (dd == g_main) {
    core_shutdown();
    geo_device_destroyed(dd);
    defer_tick(true);
    g_core_started = false;
  }
  dd->vk.DestroyDevice(device, alloc);
  std::unique_lock<std::shared_mutex> lock(g_mu);
  g_devices.erase(dkey(device));
  if (g_main == dd) g_main = nullptr;
  delete dd;
}

static VKAPI_ATTR void VKAPI_CALL GetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue* out) {
  DeviceData* dd = device_of(device);
  dd->vk.GetDeviceQueue(device, family, index, out);
  if (family == dd->graphics_family && index == 0 && !dd->graphics_queue) dd->graphics_queue = *out;
}

static VKAPI_ATTR void VKAPI_CALL GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* out) {
  DeviceData* dd = device_of(device);
  dd->vk.GetDeviceQueue2(device, info, out);
  if (info->queueFamilyIndex == dd->graphics_family && info->queueIndex == 0 && !dd->graphics_queue)
    dd->graphics_queue = *out;
}

// ---- frame boundary ----

// Headless mode (headless=1 in bg3rtx.ini, read when a swapchain is created):
// nothing reaches the display. Acquire hands out images round-robin and
// signals its semaphore/fence through an empty submit; present only consumes
// its wait semaphores. The game then renders as fast as the GPU allows even
// with the display asleep, which otherwise throttles presentation. All queue
// submissions are serialised while it is on, since the empty submits share
// the game's queues.
static std::atomic<bool> g_headless{false};
static std::mutex g_queue_mu;
static std::mutex g_sc_mu;
static std::unordered_map<VkSwapchainKHR, std::pair<uint32_t, uint32_t>> g_sc_images;  // count, next

static std::unique_lock<std::mutex> queue_lock() {
  return g_headless ? std::unique_lock<std::mutex>(g_queue_mu) : std::unique_lock<std::mutex>();
}

void headless_on_swapchain() {
  bool on = cfg_int("headless", 0) != 0;
  if (on != g_headless.exchange(on)) log("[layer] headless presentation %s", on ? "ON" : "off");
}

static VkResult fake_acquire(VkDevice device, VkSwapchainKHR sc, VkSemaphore sem, VkFence fence, uint32_t* index) {
  DeviceData* dd = device_of(device);
  {
    std::lock_guard<std::mutex> lock(g_sc_mu);
    auto& e = g_sc_images[sc];
    if (!e.first) dd->vk.GetSwapchainImagesKHR(device, sc, &e.first, nullptr);
    *index = e.second++ % (std::max)(e.first, 1u);
  }
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.signalSemaphoreCount = sem ? 1 : 0;
  si.pSignalSemaphores = &sem;
  std::lock_guard<std::mutex> lock(g_queue_mu);
  return dd->vk.QueueSubmit(dd->graphics_queue, 1, &si, fence);
}

static VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImageKHR(VkDevice device, VkSwapchainKHR sc, uint64_t timeout,
                                                          VkSemaphore sem, VkFence fence, uint32_t* index) {
  if (g_headless) return fake_acquire(device, sc, sem, fence, index);
  return device_of(device)->vk.AcquireNextImageKHR(device, sc, timeout, sem, fence, index);
}

static VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* info,
                                                           uint32_t* index) {
  if (g_headless) return fake_acquire(device, info->swapchain, info->semaphore, info->fence, index);
  return device_of(device)->vk.AcquireNextImage2KHR(device, info, index);
}

static VKAPI_ATTR VkResult VKAPI_CALL WaitForPresentKHR(VkDevice device, VkSwapchainKHR sc, uint64_t id,
                                                        uint64_t timeout) {
  if (g_headless) return VK_SUCCESS;
  return device_of(device)->vk.WaitForPresentKHR(device, sc, id, timeout);
}

static VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo* submits,
                                                  VkFence fence) {
  DeviceData* dd = device_of(queue);
  diag(DIAG_SUBMIT);
  if (trace_recording()) {
    std::vector<VkCommandBuffer> cmds;
    for (uint32_t i = 0; i < count; ++i)
      cmds.insert(cmds.end(), submits[i].pCommandBuffers, submits[i].pCommandBuffers + submits[i].commandBufferCount);
    trace_on_submit(cmds);
  }
  if (geo_submit_wanted()) {
    std::vector<VkCommandBuffer> cmds;
    for (uint32_t i = 0; i < count; ++i)
      cmds.insert(cmds.end(), submits[i].pCommandBuffers, submits[i].pCommandBuffers + submits[i].commandBufferCount);
    geo_on_submit(cmds);
  }
  auto lock = queue_lock();
  return dd->vk.QueueSubmit(queue, count, submits, fence);
}

static VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits,
                                                   VkFence fence) {
  DeviceData* dd = device_of(queue);
  diag(DIAG_SUBMIT);
  if (trace_recording()) {
    std::vector<VkCommandBuffer> cmds;
    for (uint32_t i = 0; i < count; ++i)
      for (uint32_t j = 0; j < submits[i].commandBufferInfoCount; ++j)
        cmds.push_back(submits[i].pCommandBufferInfos[j].commandBuffer);
    trace_on_submit(cmds);
  }
  if (geo_submit_wanted()) {
    std::vector<VkCommandBuffer> cmds;
    for (uint32_t i = 0; i < count; ++i)
      for (uint32_t j = 0; j < submits[i].commandBufferInfoCount; ++j)
        cmds.push_back(submits[i].pCommandBufferInfos[j].commandBuffer);
    geo_on_submit(cmds);
  }
  auto lock = queue_lock();
  return dd->vk.QueueSubmit2(queue, count, submits, fence);
}

static VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
  DeviceData* dd = device_of(queue);
  uint64_t frame = ++g_frame;
  diag(DIAG_PRESENT);
  static int presents_logged = 0;
  if (frame > 600 && presents_logged < cfg_int("log_presents", 0)) {
    ++presents_logged;
    std::string chain;
    for (auto p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext) chain += " " + std::to_string(p->sType);
    log("[present] #%llu thread %lu queue %p swapchains %u first %p image %u waits %u pNext:%s",
        (unsigned long long)frame, GetCurrentThreadId(), (void*)queue, info->swapchainCount,
        (void*)info->pSwapchains[0], info->pImageIndices[0], info->waitSemaphoreCount, chain.c_str());
  }
  cfg_poll();
  if (dd == g_main) {
    if (!g_core_started) {
      g_core_started = true;
      if (!dd->graphics_queue) dd->graphics_queue = queue;
      core_init(dd);
    }
    shader_poll();
    defer_tick(false);
    core_poll(queue);
    core_on_frame(frame);
    ngx_on_present();
    geo_on_present(queue);
  }
  trace_on_present(queue, info);
  if (g_headless) {
    std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = info->waitSemaphoreCount;
    si.pWaitSemaphores = info->pWaitSemaphores;
    si.pWaitDstStageMask = stages.data();
    std::lock_guard<std::mutex> lock(g_queue_mu);
    VkResult r = dd->vk.QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    if (info->pResults)
      for (uint32_t i = 0; i < info->swapchainCount; ++i) info->pResults[i] = r;
    return r;
  }
  auto lock = queue_lock();
  return dd->vk.QueuePresentKHR(queue, info);
}

static void register_device_hooks(HookMap& m) {
  auto add = [&](const char* name, PFN_vkVoidFunction fn) {
    m[std::string("vk") + name] = fn;
    m[std::string("vk") + name + "KHR"] = fn;
  };
  add("DestroyDevice", reinterpret_cast<PFN_vkVoidFunction>(&DestroyDevice));
  add("GetDeviceQueue", reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceQueue));
  add("GetDeviceQueue2", reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceQueue2));
  add("QueueSubmit", reinterpret_cast<PFN_vkVoidFunction>(&QueueSubmit));
  add("QueueSubmit2", reinterpret_cast<PFN_vkVoidFunction>(&QueueSubmit2));
  m["vkQueuePresentKHR"] = reinterpret_cast<PFN_vkVoidFunction>(&QueuePresentKHR);
  m["vkAcquireNextImageKHR"] = reinterpret_cast<PFN_vkVoidFunction>(&AcquireNextImageKHR);
  m["vkAcquireNextImage2KHR"] = reinterpret_cast<PFN_vkVoidFunction>(&AcquireNextImage2KHR);
  m["vkWaitForPresentKHR"] = reinterpret_cast<PFN_vkVoidFunction>(&WaitForPresentKHR);
}

static const HookMap& hooks() {
  static const HookMap m = [] {
    HookMap h;
    register_device_hooks(h);
    register_tracker_hooks(h);
    register_cmd_hooks(h);
    register_geo_hooks(h);
    return h;
  }();
  return m;
}

}  // namespace bg3

using namespace bg3;

static void note_proc(const char* via, const char* name, bool hooked, bool exists);

extern "C" PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name);

extern "C" PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,
                                                                                     const char* name) {
  if (!strcmp(name, "vkGetInstanceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(&vkGetInstanceProcAddr);
  if (!strcmp(name, "vkGetDeviceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(&vkGetDeviceProcAddr);
  if (!strcmp(name, "vkCreateInstance")) return reinterpret_cast<PFN_vkVoidFunction>(&CreateInstance);
  if (!strcmp(name, "vkDestroyInstance")) return reinterpret_cast<PFN_vkVoidFunction>(&DestroyInstance);
  if (!strcmp(name, "vkCreateDevice")) return reinterpret_cast<PFN_vkVoidFunction>(&CreateDevice);
  if (instance) {
    auto it = hooks().find(name);
    note_proc("GIPA", name, it != hooks().end(), true);
    if (it != hooks().end()) return it->second;
  }
  InstanceData* id = instance ? instance_of(instance) : nullptr;
  return id ? id->gipa(instance, name) : nullptr;
}

static void note_proc(const char* via, const char* name, bool hooked, bool exists) {
  static std::mutex mu;
  static std::unordered_set<std::string> seen;
  if (!cfg_int("log_procs", 0)) return;
  std::lock_guard<std::mutex> lock(mu);
  if (seen.insert(std::string(via) + name).second)
    log("[procs] %s %s%s%s", via, name, hooked ? " (hooked)" : "", exists ? "" : " (missing)");
}

extern "C" PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
  if (!strcmp(name, "vkGetDeviceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(&vkGetDeviceProcAddr);
  DeviceData* dd = device_of(device);
  if (!dd) {
    note_proc("GDPA(unknown device)", name, false, false);
    return nullptr;
  }
  PFN_vkVoidFunction next = dd->gdpa(device, name);
  auto it = hooks().find(name);
  note_proc("GDPA", name, it != hooks().end(), next != nullptr);
  if (!next) return nullptr;  // never claim what the device does not have
  return it != hooks().end() ? it->second : next;
}

extern "C" VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* p) {
  if (p->loaderLayerInterfaceVersion >= 2) {
    p->pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    p->pfnGetDeviceProcAddr = &vkGetDeviceProcAddr;
    p->pfnGetPhysicalDeviceProcAddr = nullptr;
    p->loaderLayerInterfaceVersion = 2;
  }
  return VK_SUCCESS;
}
