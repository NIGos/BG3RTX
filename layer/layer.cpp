// Vulkan layer entry points: loader negotiation, instance/device creation,
// dispatch, and the frame boundary (submit/present).
#include <algorithm>
#include <vector>
#include "host.h"
#include "assetdb.h"
#include "sebridge.h"
#include "hookguard.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <unordered_set>

namespace bg3 {

using HookMap = std::unordered_map<std::string, PFN_vkVoidFunction>;
void register_tracker_hooks(HookMap& m);
void register_cmd_hooks(HookMap& m);
void register_geo_hooks(HookMap& m);
void register_rt_hooks(HookMap& m);

static std::shared_mutex g_mu;
static std::unordered_map<void*, InstanceData*> g_instances;
static std::unordered_map<void*, DeviceData*> g_devices;
static DeviceData* g_main = nullptr;
static std::atomic<uint64_t> g_frame{0};
static bool g_core_started = false;

static void* dkey(const void* handle) { return *static_cast<void* const*>(handle); }

static void guard_init();
static void guard_check(const char* where, bool safe);

DeviceData* main_device() { return g_main; }
uint64_t frame_index() { return g_frame.load(); }

bool order_trace_active() {
  const int n = cfg_int("rt_order_trace", 0);
  return n > 0 && frame_index() > 600 && frame_index() % 600 < uint64_t(n);
}

static void order_log_submit(const char* what, const std::vector<VkCommandBuffer>& cmds) {
  if (!order_trace_active()) return;
  std::string s;
  char b[24];
  for (VkCommandBuffer c : cmds) { snprintf(b, sizeof b, " %p", (void*)c); s += b; }
  log("[order] %s frame %llu tid %lu cmds%s", what, (unsigned long long)frame_index(), GetCurrentThreadId(), s.c_str());
}

static InstanceData* instance_of(const void* handle) {
  std::shared_lock<std::shared_mutex> lock(g_mu);
  auto it = g_instances.find(dkey(handle));
  return it == g_instances.end() ? nullptr : it->second;
}

InstanceData* instance_data(const void* handle) { return instance_of(handle); }

DeviceData* device_of(const void* handle) {
  std::shared_lock<std::shared_mutex> lock(g_mu);
  auto it = g_devices.find(dkey(handle));
  return it == g_devices.end() ? nullptr : it->second;
}

// ---- instance ----

static VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* alloc,
                                                     VkInstance* out) {
  cfg_poll();  // settings that matter before the first present (dump_shaders)
  guard_init();  // before vkCreateDevice, after which the Script Extender patches our entry points
  ngx_init();  // BG3 loads _nvngx.dll after device creation; hook it as it maps
  auto* link = static_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(ci->pNext));
  while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
    link = static_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(link->pNext));
  if (!link) return VK_ERROR_INITIALIZATION_FAILED;

  PFN_vkGetInstanceProcAddr next = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  link->u.pLayerInfo = link->u.pLayerInfo->pNext;
  auto create = reinterpret_cast<PFN_vkCreateInstance>(next(VK_NULL_HANDLE, "vkCreateInstance"));
  // Streamline (fg>=2) is initialised here, before the game's instance exists,
  // and adds its instance extensions.
  VkInstanceCreateInfo patched;
  sl_instance_pre(ci, patched);
  // Hardware RT (rt=1) needs instance API 1.2+: VK_KHR_acceleration_structure
  // and VK_KHR_ray_query then depend on core features only, and the game's
  // VK_EXT_buffer_device_address can stay (the KHR one would conflict, VUID-03328).
  static VkApplicationInfo rt_app;
  uint32_t api = patched.pApplicationInfo && patched.pApplicationInfo->apiVersion ? patched.pApplicationInfo->apiVersion
                                                                                  : VK_API_VERSION_1_0;
  uint32_t want_api = rt_instance_api(api);
  if (want_api != api) {
    rt_app = patched.pApplicationInfo ? *patched.pApplicationInfo : VkApplicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    rt_app.apiVersion = want_api;
    patched.pApplicationInfo = &rt_app;
    log("[rt] instance API %u.%u -> %u.%u", VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api),
        VK_API_VERSION_MAJOR(want_api), VK_API_VERSION_MINOR(want_api));
    api = want_api;
  }
  // VK_EXT_debug_utils for the device-address binding reports (devfault.cpp).
  VkInstanceCreateInfo fault_ci;
  const VkInstanceCreateInfo* use_ci = fault_instance_create_info(next, &patched, &fault_ci);
  VkResult r = create(use_ci, alloc, out);
  if (r != VK_SUCCESS && use_ci != &patched) {
    log("[layer] instance creation with VK_EXT_debug_utils failed (%d); retrying without", r);
    fault_instance_create_info(nullptr, nullptr, nullptr);   // off
    r = create(&patched, alloc, out);
  }
  if (r != VK_SUCCESS) {
    sl_instance_post(VK_NULL_HANDLE, r);
    return r;
  }
  fault_instance_created(*out, next);

  auto* id = new InstanceData;
  id->instance = *out;
  id->gipa = next;
  id->api_version = api;
#define LOADI(n) id->n = reinterpret_cast<PFN_vk##n>(next(*out, "vk" #n))
  LOADI(DestroyInstance);
  LOADI(GetPhysicalDeviceQueueFamilyProperties);
  LOADI(GetPhysicalDeviceProperties);
  LOADI(EnumerateDeviceExtensionProperties);
  LOADI(GetPhysicalDeviceMemoryProperties);
  LOADI(GetPhysicalDeviceFeatures2);
  if (!id->GetPhysicalDeviceFeatures2)
    id->GetPhysicalDeviceFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(next(*out, "vkGetPhysicalDeviceFeatures2KHR"));
  LOADI(CreateWin32SurfaceKHR);
  LOADI(DestroySurfaceKHR);
#undef LOADI
  {
    std::unique_lock<std::shared_mutex> lock(g_mu);
    g_instances[dkey(*out)] = id;
  }
  sl_instance_post(*out, VK_SUCCESS);
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
  fault_instance_destroyed(instance);
  id->DestroyInstance(instance, alloc);
  se_stop();   // the bridge reader thread must not outlive the layer DLL
  std::unique_lock<std::shared_mutex> lock(g_mu);
  g_instances.erase(dkey(instance));
  delete id;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* ci,
                                                            const VkAllocationCallbacks* alloc, VkSurfaceKHR* out) {
  VkResult r;
  if (sl_create_surface(instance, ci, alloc, out, &r)) return r;
  r = instance_of(instance)->CreateWin32SurfaceKHR(instance, ci, alloc, out);
  if (r == VK_SUCCESS) sl_note_surface(*out, ci->hwnd);
  return r;
}

static VKAPI_ATTR void VKAPI_CALL DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                                                    const VkAllocationCallbacks* alloc) {
  sl_note_surface(surface, nullptr);
  instance_of(instance)->DestroySurfaceKHR(instance, surface, alloc);
}

// ---- device ----

static VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                   const VkAllocationCallbacks* alloc, VkDevice* out) {
  const VkDeviceCreateInfo* const ci_orig = ci;
  auto* link = static_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(ci->pNext));
  while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
    link = static_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(link->pNext));
  if (!link) return VK_ERROR_INITIALIZATION_FAILED;

  PFN_vkSetDeviceLoaderData set_loader_data = nullptr;
  for (auto p = static_cast<const VkLayerDeviceCreateInfo*>(ci->pNext); p; p = static_cast<const VkLayerDeviceCreateInfo*>(p->pNext))
    if (p->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && p->function == VK_LOADER_DATA_CALLBACK)
      set_loader_data = p->u.pfnSetDeviceLoaderData;

  InstanceData* id = instance_of(phys);
  PFN_vkGetInstanceProcAddr gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  PFN_vkGetDeviceProcAddr gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
  link->u.pLayerInfo = link->u.pLayerInfo->pNext;

  // Device-creation patches, chained: Track D's transform feedback first
  // (geo_capture=1 or rt=1), then hardware RT (rt=1: acceleration structures,
  // ray query), then Streamline's DLSS-G requirements (extensions, features,
  // extra queues including a dedicated optical-flow family). If the driver
  // refuses, RT is dropped first, then the geometry capture.
  aftermath_pre_device();   // aftermath=1: crash dumps must be on before the driver creates the device
  VkDeviceCreateInfo geo_ci, rt_ci, fault_ci, am_ci;
  const VkDeviceCreateInfo* geo_use = geo_device_create_info(id, phys, ci, &geo_ci);
  const VkDeviceCreateInfo* rt_use = rt_device_create_info(id, phys, geo_use, &rt_ci);
  bool rt_on = rt_use != geo_use;
  // VK_EXT_device_fault (devfault.cpp): fault addresses on a device loss.
  const VkDeviceCreateInfo* fault_use = rt_on ? fault_device_create_info(id, phys, rt_use, &fault_ci) : rt_use;
  bool fault_on = fault_use != rt_use;
  // Nsight Aftermath (aftermath=1): VK_NV_device_diagnostics_config on top.
  const VkDeviceCreateInfo* use_ci = aftermath_device_create_info(id, phys, fault_use, &am_ci);
  VkDeviceCreateInfo patched;
  SlDeviceScratch* sl_scratch = sl_device_pre(id, phys, use_ci, patched);
  auto create = reinterpret_cast<PFN_vkCreateDevice>(gipa(id ? id->instance : VK_NULL_HANDLE, "vkCreateDevice"));
  VkResult r = create(phys, &patched, alloc, out);
  if (r != VK_SUCCESS && use_ci != fault_use) {
    log("[layer] device creation with VK_NV_device_diagnostics_config failed (%d); retrying without", r);
    sl_device_post(sl_scratch, nullptr, r);
    use_ci = fault_use;
    sl_scratch = sl_device_pre(id, phys, use_ci, patched);
    r = create(phys, &patched, alloc, out);
  }
  if (r != VK_SUCCESS && fault_on) {
    log("[layer] device creation with VK_EXT_device_fault failed (%d); retrying without", r);
    sl_device_post(sl_scratch, nullptr, r);
    fault_on = false;
    use_ci = rt_use;
    sl_scratch = sl_device_pre(id, phys, use_ci, patched);
    r = create(phys, &patched, alloc, out);
  }
  if (r != VK_SUCCESS && rt_on && rt_device_features_added()) {
    // The SHaRC features (64-bit atomics, float16, 16-bit storage) next; ray tracing stays.
    log("[layer] device creation with the SHaRC device features failed (%d); retrying without them", r);
    sl_device_post(sl_scratch, nullptr, r);
    rt_device_features_undo();
    rt_use = rt_device_create_info(id, phys, geo_use, &rt_ci, false);
    rt_on = rt_use != geo_use;
    use_ci = rt_use;
    sl_scratch = sl_device_pre(id, phys, use_ci, patched);
    r = create(phys, &patched, alloc, out);
  }
  if (r != VK_SUCCESS && rt_on) {
    log("[layer] device creation with the ray tracing extensions failed (%d); retrying without", r);
    sl_device_post(sl_scratch, nullptr, r);
    rt_device_features_undo();
    rt_on = false;
    use_ci = geo_use;
    sl_scratch = sl_device_pre(id, phys, use_ci, patched);
    r = create(phys, &patched, alloc, out);
  }
  if (r != VK_SUCCESS && use_ci != ci) {
    log("[layer] device creation with the geometry capture extensions failed (%d); retrying without", r);
    sl_device_post(sl_scratch, nullptr, r);
    use_ci = ci;
    geo_use = ci;
    sl_scratch = sl_device_pre(id, phys, use_ci, patched);
    r = create(phys, &patched, alloc, out);
  }
  if (r != VK_SUCCESS) {
    sl_device_post(sl_scratch, nullptr, r);
    return r;
  }
  ci = &patched;

  auto* dd = new DeviceData;
  dd->device = *out;
  dd->physical = phys;
  dd->inst = id;
  dd->gdpa = gdpa;
  dd->set_loader_data = set_loader_data;
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
  if (dd == g_main) {
    geo_device_created(dd, geo_use != ci_orig);
    rt_device_created(dd, rt_on);
    fault_device_created(dd, rt_on && fault_on);
    if (rt_on && rt_enabled()) alpha_device_created(dd);
    if (rt_on && rt_enabled()) bvh_device_created(dd);
  }
  sl_device_post(sl_scratch, dd, VK_SUCCESS);
  watchdog_start();
  return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
  DeviceData* dd = device_of(device);
  if (!dd) return;
  sl_device_destroy(dd);
  if (dd == g_main) {
    core_shutdown();
    bvh_device_destroyed(dd);
    alpha_device_destroyed(dd);
    rt_device_destroyed(dd);
    assetdb_device_destroyed();
    geo_device_destroyed(dd);
    defer_tick(true);
    life_device_destroyed(dd);
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
  guard_check("vkGetDeviceQueue", frame_index() == 0);   // no present can be running yet
  sl_device_ready(dd);
  if (family == dd->graphics_family && index == 0 && !dd->graphics_queue) dd->graphics_queue = *out;
}

static VKAPI_ATTR void VKAPI_CALL GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* out) {
  DeviceData* dd = device_of(device);
  dd->vk.GetDeviceQueue2(device, info, out);
  guard_check("vkGetDeviceQueue2", frame_index() == 0);
  sl_device_ready(dd);
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
  // Streamline owns the swapchain and presentation with frame generation on.
  if (on && cfg_int("fg", 2) >= 2) {
    log("[layer] headless ignored: frame generation (fg>=2) needs real presentation");
    on = false;
  }
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
  VkResult r;
  if (sl_acquire(device, sc, timeout, sem, fence, index, &r)) return r;
  return device_of(device)->vk.AcquireNextImageKHR(device, sc, timeout, sem, fence, index);
}

static VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* info,
                                                           uint32_t* index) {
  if (g_headless) return fake_acquire(device, info->swapchain, info->semaphore, info->fence, index);
  // Streamline hooks only vkAcquireNextImageKHR.
  VkResult r;
  if (sl_acquire(device, info->swapchain, info->timeout, info->semaphore, info->fence, index, &r)) return r;
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
  std::vector<VkCommandBuffer> cmds;
  for (uint32_t i = 0; i < count; ++i)
    cmds.insert(cmds.end(), submits[i].pCommandBuffers, submits[i].pCommandBuffers + submits[i].commandBufferCount);
  if (trace_recording()) trace_on_submit(cmds);
  order_log_submit("submit", cmds);
  if (geo_submit_wanted()) geo_on_submit(cmds);
  bvh_on_submit(cmds);   // the TLAS instances' matrices, read as the game submits
  pan_on_submit(cmds);
  sl_note_submit(cmds);
  life_on_submit(queue, cmds);   // before it goes down: a destroy may follow on another thread at once
  fault_note_queue(queue);
  auto lock = queue_lock();
  VkResult r = dd->vk.QueueSubmit(queue, count, submits, fence);
  fault_on_result(dd, r, "vkQueueSubmit");
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits,
                                                   VkFence fence) {
  DeviceData* dd = device_of(queue);
  diag(DIAG_SUBMIT);
  std::vector<VkCommandBuffer> cmds;
  for (uint32_t i = 0; i < count; ++i)
    for (uint32_t j = 0; j < submits[i].commandBufferInfoCount; ++j)
      cmds.push_back(submits[i].pCommandBufferInfos[j].commandBuffer);
  if (trace_recording()) trace_on_submit(cmds);
  order_log_submit("submit", cmds);
  if (geo_submit_wanted()) geo_on_submit(cmds);
  bvh_on_submit(cmds);   // the TLAS instances' matrices, read as the game submits
  pan_on_submit(cmds);
  sl_note_submit(cmds);
  life_on_submit(queue, cmds);
  fault_note_queue(queue);
  auto lock = queue_lock();
  VkResult r = dd->vk.QueueSubmit2(queue, count, submits, fence);
  fault_on_result(dd, r, "vkQueueSubmit2");
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
  DeviceData* dd = device_of(queue);
  uint64_t frame = ++g_frame;
  if (frame <= 4 || (frame & 63) == 0) guard_check("vkQueuePresentKHR", true);   // on the presenting thread
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
    // Rendered-frame pacing (the game's own presents, before frame generation):
    // median and 99th-percentile frame time and the 1% low, every 600 frames.
    static LARGE_INTEGER freq{}, last{};
    static std::vector<float> ft;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    if (last.QuadPart) ft.push_back(float(double(now.QuadPart - last.QuadPart) * 1000.0 / double(freq.QuadPart)));
    last = now;
    if (ft.size() >= 600) {
      std::vector<float> s = ft;
      std::sort(s.begin(), s.end());
      float med = s[s.size() / 2], p99 = s[s.size() * 99 / 100];
      double worst = 0;
      for (size_t i = s.size() * 99 / 100; i < s.size(); ++i) worst += s[i];
      worst /= double(s.size() - s.size() * 99 / 100);
      log("[pace] rendered frame time over 600 frames: median %.2f ms (%.1f fps), p99 %.2f ms, 1%% low %.1f fps, max %.2f ms",
          med, 1000.0 / med, p99, 1000.0 / worst, s.back());
      ft.clear();
    }
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
    rt_on_present();
    assetdb_on_present(frame);
  }
  trace_on_present(queue, info);
  pan_on_present(queue);
  framecap_on_present();
  if (g_headless) {
    std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = info->waitSemaphoreCount;
    si.pWaitSemaphores = info->pWaitSemaphores;
    si.pWaitDstStageMask = stages.data();
    std::lock_guard<std::mutex> lock(g_queue_mu);
    VkResult r = dd->vk.QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    fault_on_result(dd, r, "vkQueuePresentKHR (headless)");
    if (dd == g_main) life_on_present(dd, queue);
    if (info->pResults)
      for (uint32_t i = 0; i < info->swapchainCount; ++i) info->pResults[i] = r;
    return r;
  }
  auto lock = queue_lock();
  VkResult r = sl_present(dd, queue, info);   // through Streamline's proxy when DLSS-G is set up
  fault_on_result(dd, r, "vkQueuePresentKHR");
  // Still inside the game's present call, so the queue is ours to submit to:
  // the fence that tells the destroy hooks when this frame's work is done.
  if (dd == g_main) life_on_present(dd, queue);
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL DeviceWaitIdle(VkDevice device) {
  VkResult r;
  const uint64_t seq = life_idle_begin();
  if (!sl_device_wait_idle(device, &r)) r = device_of(device)->vk.DeviceWaitIdle(device);
  if (r == VK_SUCCESS) life_idle_end(seq);
  return r;
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
  m["vkDeviceWaitIdle"] = reinterpret_cast<PFN_vkVoidFunction>(&DeviceWaitIdle);
  m["vkCreateWin32SurfaceKHR"] = reinterpret_cast<PFN_vkVoidFunction>(&CreateWin32SurfaceKHR);
  m["vkDestroySurfaceKHR"] = reinterpret_cast<PFN_vkVoidFunction>(&DestroySurfaceKHR);
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
    register_rt_hooks(h);
    return h;
  }();
  return m;
}

// ---- inline patches on our own entry points (hookguard.h) ----
// The loader hands the application *our* functions for vkQueuePresentKHR and
// the swapchain calls. The BG3 Script Extender Detours-patches exactly those
// right after vkCreateDevice (bg3se IMGUI/Vulkan.inl, vkCreateDeviceHooked),
// so its ImGui present hook runs in front of this layer and of Streamline's
// DLSS-G swapchain: ImGui set up on Streamline's back buffers and queue 0, a
// lock per present and, once a mod draws ImGui, a queue-0 submit that replaces
// the present's wait semaphore. With it installed DLSS-G showed no generated
// frames (docs/TRACK_B_FG.md, "Script Extender").
// se_unhook: 0 log only; 1 (default) put our original bytes back when the
// patch leads into the Script Extender (its overlay is then never set up,
// everything else in it keeps working); 2 also for any other module.
// Bytes are written back only where no thread can be running them: before the
// first present, or on the presenting thread itself.
static const char* const kGuarded[] = {"vkQueuePresentKHR", "vkCreateSwapchainKHR", "vkDestroySwapchainKHR"};
static std::mutex g_guard_mu;
static guard::Entry g_guard[std::size(kGuarded)];
static const void* g_guard_logged[std::size(kGuarded)] = {};
static bool g_guard_ready = false;

static bool script_extender_loaded() {
  wchar_t name[64];
  swprintf_s(name, L"BG3SE_Ext_%lu", GetCurrentProcessId());   // created by the extender (Shared/dllmain.cpp)
  HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, name);
  if (m) CloseHandle(m);
  return m != nullptr || GetModuleHandleW(L"BG3ScriptExtender.dll") != nullptr;
}

static void guard_init() {
  std::lock_guard<std::mutex> lock(g_guard_mu);
  if (g_guard_ready) return;
  g_guard_ready = true;
  for (size_t i = 0; i < std::size(kGuarded); ++i) {
    auto it = hooks().find(kGuarded[i]);
    if (it == hooks().end() || !guard::snapshot(g_guard[i], kGuarded[i], reinterpret_cast<const void*>(it->second)))
      log("[compat] cannot snapshot %s", kGuarded[i]);
  }
  log("[compat] entry-point guard armed (%zu functions, se_unhook=%d); BG3 Script Extender %s", std::size(kGuarded),
      cfg_int("se_unhook", 1), script_extender_loaded() ? "LOADED" : "not loaded");
}

static void guard_check(const char* where, bool safe) {
  std::lock_guard<std::mutex> lock(g_guard_mu);
  if (!g_guard_ready) return;
  int mode = cfg_int("se_unhook", 1);
  for (size_t i = 0; i < std::size(kGuarded); ++i) {
    guard::Patch p = guard::inspect(g_guard[i]);
    if (!p.patched) continue;
    bool se = guard::module_matches(p.module, L"ScriptExtender");
    bool want = mode >= 2 || (mode == 1 && se);
    if (g_guard_logged[i] != p.first) {
      g_guard_logged[i] = p.first;
      log("[compat] %s: our entry point is inline-patched (%zu bytes changed), jump %p -> detour %p in %ls%s; seen in %s",
          kGuarded[i], p.changed, p.first, p.final, p.module.empty() ? L"(no module)" : p.module.c_str(),
          se ? " (BG3 Script Extender)" : "", where);
      if (!want) log("[compat] %s: left patched (se_unhook=%d)", kGuarded[i], mode);
      else if (!safe) log("[compat] %s: restoring at the next present", kGuarded[i]);
    }
    if (want && safe) {
      bool ok = guard::restore(g_guard[i], p);
      g_guard_logged[i] = nullptr;
      log("[compat] %s: original bytes %s; the %ls hook is bypassed", kGuarded[i], ok ? "restored" : "NOT restored",
          p.module.empty() ? L"foreign" : p.module.c_str());
    }
  }
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
