// Vulkan layer entry points: loader negotiation, instance/device creation,
// dispatch, and the frame boundary (submit/present).
#include <algorithm>
#include <cstring>
#include <vector>
#include "host.h"
#include "dlss5.h"
#include "perf_submit.h"
#include "../common/submit_batch.h"
#include "omm_submit.h"
#include "omm_backend.h"
#include "hair_groom.h"
#include "omm_content.h"
#include "hit_material_submit.h"
#include "hit_material.h"
#include "../common/vram_guard.h"
#include "../common/material_submit_stages.h"
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
void cmd_cpu_report(uint64_t frames);

#if BG3RTX_DIAGNOSTICS
bool order_trace_active() {
  const int n = cfg_int("rt_order_trace", 0);
  return n > 0 && frame_index() > 600 && frame_index() % 600 < uint64_t(n);
}
#endif

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
  if(cfg_int("dlss5_nr",0))want_api=(std::max)(want_api,uint32_t(VK_API_VERSION_1_2));
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
  LOADI(GetPhysicalDeviceMemoryProperties2);
  if (!id->GetPhysicalDeviceMemoryProperties2)
    id->GetPhysicalDeviceMemoryProperties2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(next(*out, "vkGetPhysicalDeviceMemoryProperties2KHR"));
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
  // A layer above ours changes when the device is complete (common/sl_init_trigger.h):
  // say once whether ReShade's implicit layer is in the process.
  static std::atomic<bool> overlay_logged{false};
  if (!overlay_logged.exchange(true)) {
    HMODULE rs = GetModuleHandleA("ReShade64.dll");
    char path[MAX_PATH] = {};
    if (rs) GetModuleFileNameA(rs, path, MAX_PATH);
    log("[compat] ReShade %s%s", rs ? "loaded: " : "not loaded", path);
  }
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
  auto raw_create = reinterpret_cast<PFN_vkCreateDevice>(gipa(id ? id->instance : VK_NULL_HANDLE, "vkCreateDevice"));
  auto create = [raw_create,id](VkPhysicalDevice physical,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* callbacks,VkDevice* output) {
    return dlss5_create_device(id,raw_create,physical,info,callbacks,output);
  };
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
  if (r != VK_SUCCESS && rt_on && hair_groom::device_requested()) {
    log("[layer] optional linear swept spheres device request failed (%d); retrying without (card hair path)", r);
    sl_device_post(sl_scratch, nullptr, r);
    rt_device_features_undo();
    hair_groom::disable_device_request();
    rt_use = rt_device_create_info(id, phys, geo_use, &rt_ci);
    rt_on = rt_use != geo_use;
    use_ci = rt_use;
    sl_scratch = sl_device_pre(id, phys, use_ci, patched);
    r = create(phys, &patched, alloc, out);
  }
  if (r != VK_SUCCESS && rt_on && omm::device_requested()) {
    log("[layer] optional EXT opacity micromap device request failed (%d); retrying ordinary RT",r);
    sl_device_post(sl_scratch,nullptr,r);
    rt_device_features_undo();
    omm::disable_device_request();
    rt_use=rt_device_create_info(id,phys,geo_use,&rt_ci);
    rt_on=rt_use!=geo_use;use_ci=rt_use;
    sl_scratch=sl_device_pre(id,phys,use_ci,patched);
    r=create(phys,&patched,alloc,out);
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
  if (!g_main) replay_device_created(dd);   // before anything of ours creates objects on it

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
    hair_groom::device_created(dd, rt_on);
    fault_device_created(dd, rt_on && fault_on);
    if (rt_on && rt_enabled()) alpha_device_created(dd);
    if (rt_on && rt_enabled()) bvh_device_created(dd);
    if (rt_on && rt_enabled()) em_device_created(dd);
    if (rt_on && rt_enabled()) water_device_created(dd);   // M5.1: allocates only once rt_water_geometry=1
    if (rt_on && rt_enabled()) hair_primary_device_created(dd, &patched);   // allocates with the first hair pipeline
  }
  sl_device_post(sl_scratch, dd, VK_SUCCESS);
  watchdog_start();
  return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
  DeviceData* dd = device_of(device);
  if (!dd) return;
  dlss5_device_destroy(dd);
  sl_device_destroy(dd);
  if (dd == g_main) {
    rrpost_join_prewarm();
    core_shutdown();
    replay_device_destroyed(dd);
    terrain_capture_device_destroyed(dd);
    em_device_destroyed(dd);
    water_device_destroyed(dd);
    hair_primary_device_destroyed(dd);
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
  const VkResult r = dd->vk.QueueSubmit(dd->graphics_queue, 1, &si, fence);
  fault_on_result(dd, r, "headless acquire submission");
  return r;
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

struct SubmitScratch {
  std::vector<VkCommandBuffer> commands, entry;
  std::vector<VkSubmitInfo> legacy;
  std::vector<VkSubmitInfo2> modern;
};
static thread_local submit_batch::Scratch<SubmitScratch> t_submit_scratch;

static VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo* submits,
                                                  VkFence fence) {
  DeviceData* dd = device_of(queue);
  diag(DIAG_SUBMIT);
  SubmitCpuTimer timer(DIAG_SUBMIT_US);
  const bool batch=cfg_int("perf_submit_batch",0)!=0;
  submit_batch::Scratch<SubmitScratch>::Lease scratch(t_submit_scratch,batch);
  auto& work=scratch.get();
  auto& cmds=work.commands;cmds.clear();
  for (uint32_t i = 0; i < count; ++i)
    if(submits[i].commandBufferCount) cmds.insert(cmds.end(), submits[i].pCommandBuffers, submits[i].pCommandBuffers + submits[i].commandBufferCount);
  if (trace_recording()) trace_on_submit(cmds);
  order_log_submit("submit", cmds);
  if (geo_submit_wanted()) geo_on_submit(cmds);
  if(!batch || !cmds.empty()) bvh_on_submit(cmds);   // the TLAS instances' matrices, read as the game submits
  if(!batch || !cmds.empty()) pan_on_submit(cmds);
  life_on_submit(queue, cmds);   // before it goes down: a destroy may follow on another thread at once
  fault_note_queue(queue);
  auto lock = queue_lock();
  const auto terrainWork=terrain_capture_before_submit(cmds);
  const auto materialWork=native_hit::before_submit(queue,cmds);
  const auto ommWork=omm::before_submit(dd,cmds);
  omm::CompletionBatch completion;
  if(batch && submit_batch::appendable(submits,count)) completion=omm::prepare_completion(dd,queue,ommWork);
  submit_batch::PrivateSignal signal(completion.semaphore,completion.value);
  if(completion.attached) {
    signal.append(work.legacy,submits,count);submits=work.legacy.data();++count;
  }
  const auto hairSubmit = hair_primary_submit_begin(cmds);
  VkResult r = dd->vk.QueueSubmit(queue, count, submits, fence);
  hair_primary_submit_end(hairSubmit, r == VK_SUCCESS);
  dlss5_note_submit(queue,cmds,r==VK_SUCCESS);
  if (r == VK_SUCCESS) {
    if (!batch || !cmds.empty()) sl_note_submit(cmds);
    alpha_submit_recordings(cmds);
  }
  omm::finish_completion(ommWork,completion,r==VK_SUCCESS);
  if(completion.attached && r==VK_SUCCESS) diag(DIAG_AUX_COALESCED);
  fault_on_result(dd, r, "vkQueueSubmit");
  if (r == VK_SUCCESS) em_after_submit(dd, queue, cmds);   // D-EM: the emissive scene of this batch's effects passes
  if (r == VK_SUCCESS) water_after_submit(dd, queue, cmds);   // M5.1: the water scene of this batch's forward passes
  omm::after_submit(dd, queue, ommWork, r==VK_SUCCESS, completion.attached);
  native_hit::after_submit(materialWork,queue,r==VK_SUCCESS);
  terrain_capture_after_submit(terrainWork,queue,r==VK_SUCCESS);
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits,
                                                   VkFence fence) {
  DeviceData* dd = device_of(queue);
  diag(DIAG_SUBMIT);
  SubmitCpuTimer timer(DIAG_SUBMIT_US);
  const bool batch=cfg_int("perf_submit_batch",0)!=0;
  submit_batch::Scratch<SubmitScratch>::Lease scratch(t_submit_scratch,batch);
  auto& work=scratch.get();
  auto& cmds=work.commands;cmds.clear();
  for (uint32_t i = 0; i < count; ++i)
    for (uint32_t j = 0; j < submits[i].commandBufferInfoCount; ++j)
      cmds.push_back(submits[i].pCommandBufferInfos[j].commandBuffer);
  if (trace_recording()) trace_on_submit(cmds);
  // Native material gathers add COMPUTE reads beyond native graphics. Waits
  // must cover those reads, and signals must not release resources before they
  // finish. Keep the existing OMM signal-only policy for entries without them.
  native_hit::SubmitStages content;
  auto& entry=work.entry;entry.clear();
  for(uint32_t i=0;i<count;++i) {
    const auto& s=submits[i];entry.clear();bool material=false,terrain=false;
    for(uint32_t j=0;j<s.commandBufferInfoCount;++j){
      const auto cmd=s.pCommandBufferInfos[j].commandBuffer;entry.push_back(cmd);
      material=material || native_hit::recording_needs_submit_promotion(cmd);
      terrain=terrain || terrain_capture_needs_submit_promotion(cmd);
    }
    const bool omm_content=omm::content_dispatch_in_submit(entry);
    content.promote(submits,count,i,material || terrain,material || terrain || omm_content);
  }
  submits=content.data_or(submits);
  order_log_submit("submit", cmds);
  if (geo_submit_wanted()) geo_on_submit(cmds);
  if(!batch || !cmds.empty()) bvh_on_submit(cmds);   // the TLAS instances' matrices, read as the game submits
  if(!batch || !cmds.empty()) pan_on_submit(cmds);
  life_on_submit(queue, cmds);
  fault_note_queue(queue);
  auto lock = queue_lock();
  const auto terrainWork=terrain_capture_before_submit(cmds);
  const auto materialWork=native_hit::before_submit(queue,cmds);
  const auto ommWork=omm::before_submit(dd,cmds);
  omm::CompletionBatch completion;
  if(batch && submit_batch::appendable(submits,count)) completion=omm::prepare_completion(dd,queue,ommWork);
  submit_batch::PrivateSignal signal(completion.semaphore,completion.value);
  if(completion.attached) {
    signal.append(work.modern,submits,count);submits=work.modern.data();++count;
  }
  const auto hairSubmit = hair_primary_submit_begin(cmds);
  VkResult r = dd->vk.QueueSubmit2(queue, count, submits, fence);
  hair_primary_submit_end(hairSubmit, r == VK_SUCCESS);
  dlss5_note_submit(queue,cmds,r==VK_SUCCESS);
  if (r == VK_SUCCESS) {
    if (!batch || !cmds.empty()) sl_note_submit(cmds);
    alpha_submit_recordings(cmds);
  }
  omm::finish_completion(ommWork,completion,r==VK_SUCCESS);
  if(completion.attached && r==VK_SUCCESS) diag(DIAG_AUX_COALESCED);
  fault_on_result(dd, r, "vkQueueSubmit2");
  if (r == VK_SUCCESS) em_after_submit(dd, queue, cmds);   // D-EM (our build goes through vkQueueSubmit)
  if (r == VK_SUCCESS) water_after_submit(dd, queue, cmds);   // M5.1 (likewise)
  omm::after_submit(dd, queue, ommWork, r==VK_SUCCESS, completion.attached);
  native_hit::after_submit(materialWork,queue,r==VK_SUCCESS);
  terrain_capture_after_submit(terrainWork,queue,r==VK_SUCCESS);
  return r;
}

// VRAM of this process on the device-local heaps (VK_EXT_memory_budget), next to each [pace] line: the
// long-session growth gate (tools/live_batches/long_session). Physical-device query only; nothing is enabled.
static bool query_memory_budget(DeviceData* dd, uint64_t* used, uint64_t* avail, uint64_t* size) {
  InstanceData* id = dd ? dd->inst : nullptr;
  if (!id || !dd->physical || !id->GetPhysicalDeviceMemoryProperties2 || !id->EnumerateDeviceExtensionProperties) return false;
  static int supported = -1;
  if (supported < 0) {
    uint32_t n = 0;
    id->EnumerateDeviceExtensionProperties(dd->physical, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> ext(n);
    if (n) id->EnumerateDeviceExtensionProperties(dd->physical, nullptr, &n, ext.data());
    supported = 0;
    for (const VkExtensionProperties& e : ext) supported |= strcmp(e.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0;
    if (!supported) log("[mem] VK_EXT_memory_budget not supported: no VRAM lines");
  }
  if (!supported) return false;
  VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
  VkPhysicalDeviceMemoryProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, &budget};
  id->GetPhysicalDeviceMemoryProperties2(dd->physical, &props);
  *used = *avail = *size = 0;
  for (uint32_t h = 0; h < props.memoryProperties.memoryHeapCount; ++h) {
    if (!(props.memoryProperties.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) continue;
    *used += budget.heapUsage[h];
    *avail += budget.heapBudget[h];
    *size += props.memoryProperties.memoryHeaps[h].size;
  }
  return true;
}

static void log_memory_budget(DeviceData* dd) {
  uint64_t used = 0, avail = 0, size = 0;
  if (!query_memory_budget(dd, &used, &avail, &size)) return;
  static ULONGLONG last_summary = 0;
  const ULONGLONG now = GetTickCount64();
  if (!logging::stats_enabled(cfg_int) && last_summary && now - last_summary < 60000) return;
  last_summary = now;
  log("[mem] device-local VRAM of this process: used %.0f MB, budget %.0f MB, heaps %.0f MB", used / 1048576.0,
      avail / 1048576.0, size / 1048576.0);
}

// ---- rt_vram_guard (common/vram_guard.h): headroom state, refusals, yields, the [mem] ours line ----
// Lock order: a module's own lock (bvhsplit g_mu, ...) -> g_vg_mu. The tick reads the modules' breakdown
// before it takes g_vg_mu, never inside it.
static std::mutex g_vg_mu;
static vram_guard::Guard g_vg;                               // under g_vg_mu
static uint64_t g_vg_real_budget = 0;                       // same sample as g_vg.used, before test override
static std::atomic<bool> g_vg_on{false};
static std::atomic<uint64_t> g_vg_budget{0};
static std::atomic<uint64_t> g_vg_ask[vram_guard::kYieldCount];
struct VgRefusal : vram_guard::RefusedRequests { const char* first = nullptr; bool logged = false; };
static VgRefusal g_vg_ref[vram_guard::kConsumerCount];      // this window, under g_vg_mu
static std::atomic<bool> g_vg_refused_new{false};           // a first refusal this window: breakdown at the next tick

bool vram_guard_on() { return g_vg_on.load(std::memory_order_relaxed); }

uint64_t vram_cap(int consumer, uint64_t fallback) {
  if (!vram_guard_on() || consumer < 0 || consumer >= vram_guard::kConsumerCount) return fallback;
  return vram_guard::cap(vram_guard::Consumer(consumer), g_vg_budget.load(std::memory_order_relaxed));
}

bool vram_allow(int consumer, uint64_t bytes, const char* what, bool over_cap) {
  if (!vram_guard_on() || consumer < 0 || consumer >= vram_guard::kConsumerCount) return true;
  std::lock_guard<std::mutex> lock(g_vg_mu);
  const bool capture = consumer == vram_guard::kRtCapture;
  if (capture ? g_vg.allow_capture(bytes, g_vg_real_budget) : !over_cap && g_vg.allow(bytes)) return true;
  VgRefusal& r = g_vg_ref[consumer];
  r.note(bytes);
  if (!r.first) r.first = what;
  if (!r.logged) {   // once per window and consumer; the window line has the totals
    r.logged = true;
    g_vg_refused_new = true;
    log("[vram] guard: refused %s, %.0f MB (%s): used %.0f MB, budget %.0f MB, margin %.0f MB%s",
        vram_guard::kConsumerName[consumer], double(bytes) / 1048576.0, what ? what : "?", double(g_vg.used) / 1048576.0,
        double(capture ? g_vg_real_budget : g_vg.budget) / 1048576.0, double(g_vg.margin_now()) / 1048576.0,
        capture ? "; full RT replacement exceeds real budget (including outstanding grants)" :
        over_cap ? "; over this consumer's cap for the budget" : g_vg.low ? "; low headroom" : "; would not fit");
  }
  return false;
}

uint64_t vram_take_ask(int yield) {
  if (yield < 0 || yield >= vram_guard::kYieldCount) return 0;
  return g_vg_ask[yield].exchange(0, std::memory_order_relaxed);
}

static void log_vram_ours(uint64_t used, uint64_t budget, const BvhVram& b, uint64_t rtcap, uint64_t alpha,
                          uint64_t cascade, uint64_t recycled, uint64_t em, uint64_t water, bool low, const char* why) {
  const double mb = 1.0 / 1048576.0;
  const uint64_t ours = b.as_reserved + b.pool + b.pool_pending + rtcap + cascade + recycled + em + water;
  log("[mem] ours (%s): BLAS chunks %.0f MB reserved in %u chunks (%.0f live, %.0f empty), mesh pool %.0f MB "
      "(%.0f used, %.0f copy/retirement, %llu repacks; %llu cached BLAS relocated), "
      "RT capture %.0f MB (alpha region %.0f), cascade capture %.0f MB, hit-material recycled %.0f MB, em soup %.0f MB, "
      "water soup %.0f MB; tracked %.0f MB of used %.0f MB (rest: game, core GI/RR images, untracked %.0f MB), "
      "budget %.0f MB (%s); guard %s",
      why, double(b.as_reserved) * mb, b.chunks, double(b.as_live) * mb, double(b.as_empty) * mb, double(b.pool) * mb,
      double(b.pool_used) * mb, double(b.pool_pending) * mb, (unsigned long long)b.pool_repacks, (unsigned long long)b.blas_relocated,
      double(rtcap) * mb, double(alpha) * mb, double(cascade) * mb, double(recycled) * mb, double(em) * mb,
      double(water) * mb, double(ours) * mb, double(used) * mb, double(used > ours ? used - ours : 0) * mb,
      double(budget) * mb, used <= budget ? "within budget" : "OVER budget",
      !vram_guard_on() ? "off" : low ? "LOW (cache growth refused, yielding)" : "normal");
}

// Present thread (g_main), every frame: sample every 16 frames (one physical-device query), the
// breakdown line every rt_timing window, on a state change and after a window's first refusal. With the
// key off only the window breakdown runs (our share vs the game's, for the guard-off A/B).
static void vram_guard_tick(DeviceData* dd, uint64_t frame) {
  static uint64_t s_window_start = 0;
  static int stats_reset_token = 0;
  if (stats_window::reset(cfg_int, stats_reset_token)) s_window_start = frame;
  const int every = stats_period(unsigned((std::max)(cfg_int("rt_timing_frames", 300), 30)));
  const bool window_end = frame >= s_window_start + uint64_t(every);
  if ((frame & 15) != 0 && !window_end) return;
  const bool on = cfg_int("rt_vram_guard", 0) != 0;
  const bool was_on = g_vg_on.exchange(on);
  if (was_on != on) log("[vram] guard %s (rt_vram_guard=%d)", on ? "on" : "off", on ? 1 : 0);
  if (!on) for (auto& a : g_vg_ask) a = 0;
  if (!on && !window_end) return;
  uint64_t used = 0, avail = 0, size = 0;
  if (!query_memory_budget(dd, &used, &avail, &size)) {
    if (window_end) s_window_start = frame;
    return;
  }
  const uint64_t real_budget = avail;
  // Test budget (rt_vram_guard_budget_mb > 0, a test key): the guard, its caps and the [mem] ours line use
  // min(driver budget, this), so a fresh launch reproduces the pressure of a long session or a smaller card
  // (vg1 2026-11-02: 18.6 of 31.4 GB, the user's 31.7 GB event did not reproduce in a 4-minute run).
  if (const int test_mb = cfg_int("rt_vram_guard_budget_mb", 0); test_mb > 0)
    avail = (std::min)(avail, uint64_t(test_mb) << 20);
  // Breakdown and yield sizes first (each module's own lock; not under g_vg_mu).
  BvhVram b;
  bvh_vram(&b);
  const uint64_t recycled = native_hit::recycled_bytes();
  bool changed = false, low = false;
  uint64_t need = 0, margin = 0;
  if (on) {
    std::lock_guard<std::mutex> lock(g_vg_mu);
    g_vg_real_budget = real_budget;
    changed = g_vg.sample(used, avail);
    low = g_vg.low;
    need = g_vg.trim_need();
    margin = g_vg.margin_now();
    g_vg_budget = avail;
    const uint64_t have[vram_guard::kYieldCount] = {b.as_empty, recycled, b.lru};
    uint64_t ask[vram_guard::kYieldCount] = {};
    vram_guard::plan_trim(need, have, ask);
    for (int i = 0; i < vram_guard::kYieldCount; ++i) g_vg_ask[i] = ask[i];
    if (changed)
      log("[vram] guard: headroom %s: used %.0f MB, budget %.0f MB, margin %.0f MB; asking %.0f MB (%s %.0f, %s %.0f, "
          "%s %.0f)",
          low ? "LOW, cache growth refused" : "back, growth allowed", used / 1048576.0, avail / 1048576.0,
          double(margin) / 1048576.0, double(need) / 1048576.0, vram_guard::kYieldName[0], ask[0] / 1048576.0,
          vram_guard::kYieldName[1], ask[1] / 1048576.0, vram_guard::kYieldName[2], ask[2] / 1048576.0);
  }
  const bool refused = g_vg_refused_new.exchange(false);
  if (changed || refused || (window_end && logging::stats_enabled(cfg_int))) {
    uint64_t rtcap = 0, alpha = 0, cascade = 0;
    geo_vram(&rtcap, &alpha, &cascade);
    log_vram_ours(used, avail, b, rtcap, alpha, cascade, recycled, em_vram_bytes(), water_vram_bytes(), low,
                  changed ? "guard state" : refused ? "refusal" : "window");
  }
  if (window_end) {
    s_window_start = frame;
    std::lock_guard<std::mutex> lock(g_vg_mu);
    for (int c = 0; c < vram_guard::kConsumerCount; ++c) {
      VgRefusal& r = g_vg_ref[c];
      if (r.count)
        log("[vram] guard window: %llu growth attempts of %s refused (peak allocation %.0f MB; first: %s)", (unsigned long long)r.count,
            vram_guard::kConsumerName[c], double(r.peak) / 1048576.0, r.first ? r.first : "?");
      r = VgRefusal{};
    }
    g_vg.new_window();
  }
}

static VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
  DeviceData* dd = device_of(queue);
  uint64_t frame = ++g_frame;
  cmd_cpu_report(frame);
  native_hit::cpu_report(frame);
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
    vram_guard_tick(dd, frame);
    // Rendered-frame pacing (the game's own presents, before frame generation):
    // median and 99th-percentile frame time and the 1% low, every 600 frames.
    static LARGE_INTEGER freq{}, last{};
    static std::vector<float> ft;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    if (last.QuadPart) ft.push_back(float(double(now.QuadPart - last.QuadPart) * 1000.0 / double(freq.QuadPart)));
    last = now;
    static int pace_reset_token = 0;
    if (stats_window::reset(cfg_int, pace_reset_token)) { ft.clear(); last = now; }
    if (ft.size() >= stats_period(600)) {
      std::vector<float> s = ft;
      std::sort(s.begin(), s.end());
      float med = s[s.size() / 2], p99 = s[s.size() * 99 / 100];
      double worst = 0;
      for (size_t i = s.size() * 99 / 100; i < s.size(); ++i) worst += s[i];
      worst /= double(s.size() - s.size() * 99 / 100);
      log_stats("[pace] rendered frame time over %zu frames: median %.2f ms (%.1f fps), p99 %.2f ms, 1%% low %.1f fps, max %.2f ms",
          s.size(), med, 1000.0 / med, p99, 1000.0 / worst, s.back());
      ft.clear();
      log_memory_budget(dd);
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
    static int stats_reset_token = 0;
    const bool stats_reset = stats_window::reset(cfg_int, stats_reset_token);
    if (stats_reset) { hair_primary_stats_reset(); alpha_stats_reset(); }
    ngx_on_present();
    geo_on_present(queue);
    rt_on_present();
    em_on_present();
    water_on_present();
    assetdb_on_present(frame);
    watchdog_stats_tick(frame);
    if (stats_reset) log("[stats] reset token=%d frame=%llu", stats_reset_token, (unsigned long long)frame);
    if (cfg_int("rt_stats_period_frames", 0) > 0) log("[stats] frame=%llu", (unsigned long long)frame);
  }
  trace_on_present(queue, info);
  pan_on_present(queue);
  replay_on_present();
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
