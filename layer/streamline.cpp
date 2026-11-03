// DLSS Frame Generation through NVIDIA Streamline, manually hooked from inside
// the host layer.
//
// Layering. Streamline's interposer normally sits between the application and
// vulkan-1.dll. Here it sits *below* this layer instead:
//
//   bg3.exe -> loader -> VK_LAYER_BG3RTX_host --(swapchain/present calls)--> sl.interposer proxies
//                                  |                                                |
//                                  +------------- everything else -------------+   v
//                                                                               next layer -> driver
//
// sl.interposer resolves its own Vulkan entry points with
// GetProcAddress(vulkan-1.dll, "vkGetInstanceProcAddr"/"vkGetDeviceProcAddr").
// Those would lead back to the top of the chain, so to us, and every present
// Streamline makes would recurse. Its import of GetProcAddress is patched so
// that those two names resolve to sl_next_gipa / sl_next_gdpa below, which hand
// out the *next layer's* functions. Dispatchable objects Streamline creates
// that way (command buffers, queues) bypass the loader trampolines, so their
// loader dispatch pointer is set here with the loader's SetDeviceLoaderData.
//
// What gets tagged each frame is found by pass signature (see FRAME_MAP.md):
//   depth + motion vectors   the velocity pass (first fs 788dfdf6..., RG16F + D24S8, render res)
//   hudless + UI             descriptor set 1 bindings 1 and 2 of the UI composite (fs 046c6949...)
// Camera matrices come from the velocity pass's uniform buffers (set 0
// bindings 2 and 5), read on the CPU from the game's mapped memory.
#include "host.h"
#include "../common/fg_stability.h"
#include "../common/fg_copy_lease.h"
#include "../common/fg_extent.h"
#include "../common/async_capture_queue.h"

#include <psapi.h>
#include <dbghelp.h>

#include <cmath>
#include <algorithm>
#include <cstring>
#include <set>

#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss_g.h>
#include <sl_helpers_vk.h>
#include <sl_pcl.h>
#include <sl_reflex.h>

namespace bg3 {

// ---------------------------------------------------------------------------
// state

namespace {

struct SlFns {
  PFun_slInit* Init = nullptr;
  PFun_slShutdown* Shutdown = nullptr;
  PFun_slIsFeatureSupported* IsFeatureSupported = nullptr;
  PFun_slIsFeatureLoaded* IsFeatureLoaded = nullptr;
  PFun_slGetFeatureRequirements* GetFeatureRequirements = nullptr;
  PFun_slGetFeatureFunction* GetFeatureFunction = nullptr;
  PFun_slGetNewFrameToken* GetNewFrameToken = nullptr;
  PFun_slSetTagForFrame* SetTagForFrame = nullptr;
  PFun_slSetConstants* SetConstants = nullptr;
  PFun_slSetVulkanInfo* SetVulkanInfo = nullptr;
  PFun_slDLSSGSetOptions* DLSSGSetOptions = nullptr;
  PFun_slDLSSGGetState* DLSSGGetState = nullptr;
  PFun_slReflexSetOptions* ReflexSetOptions = nullptr;
  PFun_slReflexSleep* ReflexSleep = nullptr;
  PFun_slPCLSetMarker* PCLSetMarker = nullptr;
  PFN_vkGetDeviceProcAddr gdpa = nullptr;     // interposer's exports: hand out its proxies
  PFN_vkGetInstanceProcAddr gipa = nullptr;
};

struct Proxies {
  PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
  PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
  PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
  PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;
  PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
  PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;
  PFN_vkCreateWin32SurfaceKHR CreateWin32SurfaceKHR = nullptr;
  PFN_vkDestroySurfaceKHR DestroySurfaceKHR = nullptr;
};

SlFns g_sl;
Proxies g_px;
HMODULE g_interposer = nullptr;
HMODULE g_vulkan1 = nullptr;
bool g_sl_init = false;          // slInit succeeded
bool g_in_sl_init = false;       // slInit is creating its own probe instance through us
VkInstance g_sl_instance = VK_NULL_HANDLE;
DeviceData* g_dd = nullptr;      // the device Streamline runs on
std::atomic<bool> g_active{false};
bool g_have_of_queue = false;

// Requirements gathered from slGetFeatureRequirements after slInit.
struct Reqs {
  std::vector<std::string> inst_ext, dev_ext, feat12, feat13;
  uint32_t gfx_queues = 0, compute_queues = 0, of_queues = 0;
} g_req;

void sl_logcb(sl::LogType type, const char* msg) {
  size_t n = strlen(msg);
  while (n && (msg[n - 1] == '\n' || msg[n - 1] == '\r')) --n;
  const char* t = type == sl::LogType::eError ? "ERROR " : type == sl::LogType::eWarn ? "warn " : "";
  log("[sl] %s%.*s", t, int(n), msg);
}

const char* res_str(sl::Result r) {
  switch (r) {
    case sl::Result::eOk: return "ok";
    case sl::Result::eErrorIO: return "eErrorIO";
    case sl::Result::eErrorDriverOutOfDate: return "eErrorDriverOutOfDate";
    case sl::Result::eErrorOSOutOfDate: return "eErrorOSOutOfDate";
    case sl::Result::eErrorOSDisabledHWS: return "eErrorOSDisabledHWS";
    case sl::Result::eErrorDeviceNotCreated: return "eErrorDeviceNotCreated";
    case sl::Result::eErrorNoSupportedAdapterFound: return "eErrorNoSupportedAdapterFound";
    case sl::Result::eErrorAdapterNotSupported: return "eErrorAdapterNotSupported";
    case sl::Result::eErrorNoPlugins: return "eErrorNoPlugins";
    case sl::Result::eErrorVulkanAPI: return "eErrorVulkanAPI";
    case sl::Result::eErrorDXGIAPI: return "eErrorDXGIAPI";
    case sl::Result::eErrorD3DAPI: return "eErrorD3DAPI";
    case sl::Result::eErrorNRDAPI: return "eErrorNRDAPI";
    case sl::Result::eErrorNVAPI: return "eErrorNVAPI";
    case sl::Result::eErrorReflexAPI: return "eErrorReflexAPI";
    case sl::Result::eErrorNGXFailed: return "eErrorNGXFailed";
    case sl::Result::eErrorJSONParsing: return "eErrorJSONParsing";
    case sl::Result::eErrorMissingProxy: return "eErrorMissingProxy";
    case sl::Result::eErrorMissingResourceState: return "eErrorMissingResourceState";
    case sl::Result::eErrorInvalidIntegration: return "eErrorInvalidIntegration";
    case sl::Result::eErrorMissingInputParameter: return "eErrorMissingInputParameter";
    case sl::Result::eErrorNotInitialized: return "eErrorNotInitialized";
    case sl::Result::eErrorComputeFailed: return "eErrorComputeFailed";
    case sl::Result::eErrorInitNotCalled: return "eErrorInitNotCalled";
    case sl::Result::eErrorExceptionHandler: return "eErrorExceptionHandler";
    case sl::Result::eErrorInvalidParameter: return "eErrorInvalidParameter";
    case sl::Result::eErrorMissingConstants: return "eErrorMissingConstants";
    case sl::Result::eErrorDuplicatedConstants: return "eErrorDuplicatedConstants";
    case sl::Result::eErrorMissingOrInvalidAPI: return "eErrorMissingOrInvalidAPI";
    case sl::Result::eErrorCommonConstantsMissing: return "eErrorCommonConstantsMissing";
    case sl::Result::eErrorUnsupportedInterface: return "eErrorUnsupportedInterface";
    case sl::Result::eErrorFeatureMissing: return "eErrorFeatureMissing";
    case sl::Result::eErrorFeatureNotSupported: return "eErrorFeatureNotSupported";
    case sl::Result::eErrorFeatureMissingHooks: return "eErrorFeatureMissingHooks";
    case sl::Result::eErrorFeatureFailedToLoad: return "eErrorFeatureFailedToLoad";
    case sl::Result::eErrorFeatureWrongPriority: return "eErrorFeatureWrongPriority";
    case sl::Result::eErrorFeatureMissingDependency: return "eErrorFeatureMissingDependency";
    case sl::Result::eErrorFeatureManagerInvalidState: return "eErrorFeatureManagerInvalidState";
    case sl::Result::eErrorInvalidState: return "eErrorInvalidState";
    case sl::Result::eWarnOutOfVRAM: return "eWarnOutOfVRAM";
    default: return "?";
  }
}

std::wstring widen(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(size_t(n > 0 ? n - 1 : 0), L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

// ---------------------------------------------------------------------------
// the layer Streamline sees below itself

PFN_vkVoidFunction VKAPI_CALL sl_next_gdpa(VkDevice device, const char* name);
PFN_vkVoidFunction VKAPI_CALL sl_next_gipa(VkInstance instance, const char* name);
PFN_vkGetInstanceProcAddr loader_gipa();
thread_local int t_in_sl = 0;          // inside a call into a Streamline proxy on this thread
struct InSl {
  InSl() { ++t_in_sl; }
  ~InSl() { --t_in_sl; }
};

std::atomic<uint64_t> g_displayed{0};
std::atomic<uint64_t> g_batched{0};   // presents carrying VkSetPresentConfigNV with more than one frame per batch
std::mutex g_queue_mu;
std::unordered_map<VkQueue, uint32_t> g_queue_family;   // queues Streamline took
bool snap_present(DeviceData* dd, VkQueue queue, const VkPresentInfoKHR* info, VkPresentInfoKHR& out,
                  VkSemaphore& wait);
void snap_finish(DeviceData* dd);
bool snap_last();   // the capture's last present is in flight
bool g_fg_running_flag();

// Every present Streamline makes (real and generated frames) passes here.
VKAPI_ATTR VkResult VKAPI_CALL w_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
  diag(DIAG_DISPLAYED);
  g_displayed.fetch_add(1, std::memory_order_relaxed);
  for (auto p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext)
    if (p->sType == VK_STRUCTURE_TYPE_SET_PRESENT_CONFIG_NV &&
        reinterpret_cast<const VkSetPresentConfigNV*>(p)->numFramesPerBatch > 1)
      g_batched.fetch_add(1, std::memory_order_relaxed);
  DeviceData* dd = device_of(queue);
  static std::atomic<int> logged{0};
  if (g_fg_running_flag() && logged < cfg_int("fg_log_presents", 6)) {
    ++logged;
    std::string chain;
    for (auto p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext) {
      chain += " " + std::to_string(p->sType);
      if (p->sType == VK_STRUCTURE_TYPE_SET_PRESENT_CONFIG_NV) {
        auto c = reinterpret_cast<const VkSetPresentConfigNV*>(p);
        chain += "(frames per batch " + std::to_string(c->numFramesPerBatch) + ")";
      }
    }
    log("[sl] Streamline present: thread %lu queue %p swapchains %u image %u waits %u pNext:%s", GetCurrentThreadId(),
        (void*)queue, info->swapchainCount, info->pImageIndices[0], info->waitSemaphoreCount, chain.c_str());
  }
  VkPresentInfoKHR copy;
  VkSemaphore wait = VK_NULL_HANDLE;
  if (snap_present(dd, queue, info, copy, wait)) {
    VkResult r = dd->vk.QueuePresentKHR(queue, &copy);
    // The readback is finished at the NEXT present (snap_present), when the GPU is usually done with it: waiting
    // here stalled every captured present (wf5-s8-fg2 160335: one present per ~180 ms, 16-19 of 32 captured).
    // The last one of a capture is finished here.
    if (snap_last()) snap_finish(dd);
    return r;
  }
  return dd->vk.QueuePresentKHR(queue, info);
}

VKAPI_ATTR VkResult VKAPI_CALL w_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* ai,
                                                        VkCommandBuffer* out) {
  DeviceData* dd = device_of(device);
  VkResult r = dd->vk.AllocateCommandBuffers(device, ai, out);
  if (r == VK_SUCCESS && dd->set_loader_data)
    for (uint32_t i = 0; i < ai->commandBufferCount; ++i) dd->set_loader_data(device, out[i]);
  return r;
}

VKAPI_ATTR void VKAPI_CALL w_GetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue* out) {
  DeviceData* dd = device_of(device);
  dd->vk.GetDeviceQueue(device, family, index, out);
  if (*out && dd->set_loader_data) dd->set_loader_data(device, *out);
  std::lock_guard<std::mutex> lock(g_queue_mu);
  if (*out) g_queue_family[*out] = family;
}

VKAPI_ATTR void VKAPI_CALL w_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* out) {
  DeviceData* dd = device_of(device);
  dd->vk.GetDeviceQueue2(device, info, out);
  if (*out && dd->set_loader_data) dd->set_loader_data(device, *out);
  std::lock_guard<std::mutex> lock(g_queue_mu);
  if (*out) g_queue_family[*out] = info->queueFamilyIndex;
}

// ---- snapshots of what is actually presented (fg_snap=N) ----
// Once frame generation has run for fg_snap_after frames, the next N presents
// are copied to captures/fg_<stamp>/pNN.png: alternating real and generated
// frames, for checking UI and motion artifacts.
VkExtent2D g_sc_extent{};
VkFormat g_sc_format = VK_FORMAT_UNDEFINED;
uint64_t g_fg_on_frame = 0;
int g_fg_enables = 0;   // times frame generation went on (the first is usually the loading screen)
int g_snaps_left = -1;
std::string g_snap_dir;
struct Snap {
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkBuffer buf = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkSemaphore sem = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  uint32_t family = UINT32_MAX;
  bool pending = false;
  int index = 0;
} g_snap;
bool fg_running_now();

uint32_t snap_memory_type(DeviceData* dd, uint32_t bits) {
  VkPhysicalDeviceMemoryProperties props{};
  dd->inst->GetPhysicalDeviceMemoryProperties(dd->physical, &props);
  // Host-cached first: the CPU reads the readback (uncached write-combined reads made each capture ~180 ms).
  const VkMemoryPropertyFlags cached =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & cached) == cached) return i;
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                                  (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
      return i;
  return UINT32_MAX;
}

bool snap_present(DeviceData* dd, VkQueue queue, const VkPresentInfoKHR* info, VkPresentInfoKHR& out,
                  VkSemaphore& wait) {
  // The previous present's readback (deferred from w_QueuePresentKHR) before anything else.
  snap_finish(dd);
  // Allow separate off/on snapshot windows in one live A/B session.
  const int requested = cfg_int("fg_snap", 0);
  static uint64_t s_requested_at = 0;   // fg_snap_delay: the frame the request was first seen
  if (requested <= 0) { g_snaps_left = -1; s_requested_at = 0; return false; }
  if (g_snaps_left == 0 || info->swapchainCount != 1 || !g_sc_extent.width) return false;
  if (g_snaps_left < 0) {
    int n = requested;
    if (cfg_int("pan_edge",0) && (n>16 || g_sc_extent.width>3840 || g_sc_extent.height>1600)) {
      log("[sl] edge capture REFUSED: presented extent/count budget");
      g_snaps_left=0; return false;
    }
    // fg_snap_delay=N: start N rendered frames after the request (a plan step sets fg_snap at its start; the
    // capture then lands inside the camera turn that starts later in the step).
    // fg_snap_delay_ms=N (when > 0, instead): N wall-clock ms, so launches at different frame rates (rt=1 / rt=0)
    // capture at the same point of the same turn.
    static uint64_t s_requested_ms = 0;
    if (!s_requested_at) s_requested_at = frame_index(), s_requested_ms = GetTickCount64();
    if (const int ms = cfg_int("fg_snap_delay_ms", 0); ms > 0) {
      if (GetTickCount64() < s_requested_ms + uint64_t(ms)) return false;
    } else if (frame_index() < s_requested_at + uint64_t(std::max(cfg_int("fg_snap_delay", 0), 0))) {
      return false;
    }
    // fg_snap_any=1: also while frame generation is off (fg=0 live), so an FG on / off A/B scores the
    // presented frames of both states with the same tool.
    const bool any = cfg_int("fg_snap_any", 0) != 0 && !fg_running_now();
    if (n <= 0 || (!any && (!fg_running_now() || g_fg_enables < cfg_int("fg_snap_on", 1) ||
                            frame_index() < g_fg_on_frame + uint64_t(cfg_int("fg_snap_after", 60)))))
      return false;
    g_snaps_left = n;
    g_snap_dir = root_dir() + "\\captures\\fg_" + capture_stamp();
    make_dirs(g_snap_dir);
    log("[sl] capturing %d presented frames to %s", n, g_snap_dir.c_str());
    if(cfg_int("pan_edge",0)) log("[sl] edge capture format %d, extent %ux%u",int(g_sc_format),g_sc_extent.width,g_sc_extent.height);
  }
  uint32_t family = dd->graphics_family;
  {
    std::lock_guard<std::mutex> lock(g_queue_mu);
    auto it = g_queue_family.find(queue);
    if (it != g_queue_family.end()) family = it->second;
  }
  Snap& S = g_snap;
  VkDevice dev = dd->device;
  if (!S.pool || S.family != family) {
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = family;
    if (dd->vk.CreateCommandPool(dev, &pci, nullptr, &S.pool) != VK_SUCCESS) return g_snaps_left = 0, false;
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = S.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    dd->vk.AllocateCommandBuffers(dev, &ai, &S.cmd);
    if (dd->set_loader_data) dd->set_loader_data(dev, S.cmd);
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = VkDeviceSize(g_sc_extent.width) * g_sc_extent.height * 4;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    dd->vk.CreateBuffer(dev, &bci, nullptr, &S.buf);
    VkMemoryRequirements req;
    dd->vk.GetBufferMemoryRequirements(dev, S.buf, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = snap_memory_type(dd, req.memoryTypeBits);
    if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dev, &mai, nullptr, &S.mem) != VK_SUCCESS)
      return g_snaps_left = 0, false;
    dd->vk.BindBufferMemory(dev, S.buf, S.mem, 0);
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    dd->vk.CreateSemaphore(dev, &sci, nullptr, &S.sem);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    dd->vk.CreateFence(dev, &fci, nullptr, &S.fence);
    S.family = family;
  }
  uint32_t n = 0;
  dd->vk.GetSwapchainImagesKHR(dev, info->pSwapchains[0], &n, nullptr);
  std::vector<VkImage> imgs(n);
  dd->vk.GetSwapchainImagesKHR(dev, info->pSwapchains[0], &n, imgs.data());
  uint32_t idx = info->pImageIndices[0];
  if (idx >= n) return false;
  VkImage img = imgs[idx];

  dd->vk.ResetCommandBuffer(S.cmd, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  dd->vk.BeginCommandBuffer(S.cmd, &bi);
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = img;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  dd->vk.CmdPipelineBarrier(S.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                            nullptr, 1, &b);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {g_sc_extent.width, g_sc_extent.height, 1};
  dd->vk.CmdCopyImageToBuffer(S.cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, S.buf, 1, &region);
  b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b.dstAccessMask = 0;
  b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  dd->vk.CmdPipelineBarrier(S.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                            nullptr, 1, &b);
  dd->vk.EndCommandBuffer(S.cmd);

  std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.waitSemaphoreCount = info->waitSemaphoreCount;
  si.pWaitSemaphores = info->pWaitSemaphores;
  si.pWaitDstStageMask = stages.data();
  si.commandBufferCount = 1;
  si.pCommandBuffers = &S.cmd;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &S.sem;
  dd->vk.ResetFences(dev, 1, &S.fence);
  if (dd->vk.QueueSubmit(queue, 1, &si, S.fence) != VK_SUCCESS) return g_snaps_left = 0, false;
  out = *info;
  wait = S.sem;
  out.waitSemaphoreCount = 1;
  out.pWaitSemaphores = &S.sem;
  S.pending = true;
  return true;
}

bool snap_last() { return g_snaps_left == 1; }

void snap_finish(DeviceData* dd) {
  Snap& S = g_snap;
  if (!S.pending) return;
  S.pending = false;
  dd->vk.WaitForFences(dd->device, 1, &S.fence, VK_TRUE, 2000000000ull);
  void* p = nullptr;
  if (dd->vk.MapMemory(dd->device, S.mem, 0, VK_WHOLE_SIZE, 0, &p) == VK_SUCCESS && p) {
    uint32_t w = g_sc_extent.width, h = g_sc_extent.height;
    // Keep P16 compact; optionally retain its first present at native resolution.
    const uint32_t stride = 2u;
    const bool native = cfg_int("fg_snap_full", 0) && g_snaps_left == cfg_int("fg_snap", 0);
    std::vector<uint8_t> native_rgb;
    if (native) native_rgb.resize(size_t(w) * h * 3);
    uint32_t ow = w / stride, oh = h / stride;
    std::vector<uint8_t> rgb(size_t(ow) * oh * 3);
    const uint32_t* px = static_cast<const uint32_t*>(p);
    for (uint32_t y = 0; y < oh; ++y)
      for (uint32_t x = 0; x < ow; ++x) {
        uint32_t v = px[size_t(y * stride) * w + x * stride];
        uint8_t r, g, bl;
        if (g_sc_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) {
          r = uint8_t((v & 1023) >> 2), g = uint8_t(((v >> 10) & 1023) >> 2), bl = uint8_t(((v >> 20) & 1023) >> 2);
        } else if (g_sc_format == VK_FORMAT_B8G8R8A8_UNORM || g_sc_format == VK_FORMAT_B8G8R8A8_SRGB) {
          bl = uint8_t(v), g = uint8_t(v >> 8), r = uint8_t(v >> 16);
        } else {
          r = uint8_t(v), g = uint8_t(v >> 8), bl = uint8_t(v >> 16);
        }
        uint8_t* o = &rgb[(size_t(y) * ow + x) * 3];
        o[0] = r, o[1] = g, o[2] = bl;
      }
    if (native) {
      for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
        uint32_t v = px[size_t(y) * w + x];
        uint8_t* o = &native_rgb[(size_t(y) * w + x) * 3];
        if (g_sc_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) {
          o[0] = uint8_t((v & 1023) >> 2); o[1] = uint8_t(((v >> 10) & 1023) >> 2); o[2] = uint8_t(((v >> 20) & 1023) >> 2);
        } else if (g_sc_format == VK_FORMAT_B8G8R8A8_UNORM || g_sc_format == VK_FORMAT_B8G8R8A8_SRGB) {
          o[0] = uint8_t(v >> 16); o[1] = uint8_t(v >> 8); o[2] = uint8_t(v);
        } else {
          o[0] = uint8_t(v); o[1] = uint8_t(v >> 8); o[2] = uint8_t(v >> 16);
        }
      }
    }
    dd->vk.UnmapMemory(dd->device, S.mem);
    char name[64];
    snprintf(name, sizeof name, "\\p%02d.png", S.index++);
    struct Png { std::string path; uint32_t w,h; int index; std::vector<uint8_t> rgb; };
    static AsyncCaptureQueue<Png> writer(32,[](Png&& p) {
      bool ok=write_png_rgb(p.path,p.w,p.h,p.rgb);
      log("[sl] presented frame %d -> %s (%s)",p.index,p.path.c_str(),ok ? "ok" : "PNG write failed");
    });
    if (native && !writer.submit({g_snap_dir+"\\native.png",w,h,S.index-1,std::move(native_rgb)}))
      log("[sl] PNG write failed: native capture writer backlog full");
    if (!writer.submit({g_snap_dir+name,ow,oh,S.index-1,std::move(rgb)}))
      log("[sl] PNG write failed: capture writer backlog full");
  }
  if (--g_snaps_left == 0) log("[sl] present capture done");
}

PFN_vkVoidFunction wrapped(const char* name) {
  if (!strcmp(name, "vkGetDeviceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(&sl_next_gdpa);
  if (!strcmp(name, "vkGetInstanceProcAddr"))
    return cfg_int("sl_redirect_gipa", 0) ? reinterpret_cast<PFN_vkVoidFunction>(&sl_next_gipa)
                                          : reinterpret_cast<PFN_vkVoidFunction>(loader_gipa());
  if (!strcmp(name, "vkQueuePresentKHR")) return reinterpret_cast<PFN_vkVoidFunction>(&w_QueuePresentKHR);
  if (!strcmp(name, "vkAllocateCommandBuffers")) return reinterpret_cast<PFN_vkVoidFunction>(&w_AllocateCommandBuffers);
  if (!strcmp(name, "vkGetDeviceQueue")) return reinterpret_cast<PFN_vkVoidFunction>(&w_GetDeviceQueue);
  if (!strcmp(name, "vkGetDeviceQueue2")) return reinterpret_cast<PFN_vkVoidFunction>(&w_GetDeviceQueue2);
  return nullptr;
}

PFN_vkGetInstanceProcAddr loader_gipa() {
  static auto p = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(GetModuleHandleA("vulkan-1.dll"),
                                                                             "vkGetInstanceProcAddr"));
  return p;
}
PFN_vkGetDeviceProcAddr loader_gdpa() {
  static auto p = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(GetModuleHandleA("vulkan-1.dll"),
                                                                           "vkGetDeviceProcAddr"));
  return p;
}

std::atomic<int> g_gdpa_log{0};

PFN_vkVoidFunction VKAPI_CALL sl_next_gdpa(VkDevice device, const char* name) {
  DeviceData* dd = device ? device_of(device) : nullptr;
  if (!dd) {   // a device we do not know: use the full chain
    PFN_vkVoidFunction f = loader_gdpa()(device, name);
    if (g_gdpa_log++ < 20) log("[sl] gdpa(unknown device %p, %s) -> %p", (void*)device, name, (void*)f);
    return f;
  }
  PFN_vkVoidFunction next = dd->gdpa(device, name);
  if (!next) {
    if (g_gdpa_log++ < 40) log("[sl] gdpa(%s) -> null below us", name);
    return nullptr;
  }
  if (auto w = wrapped(name)) return w;
  return next;
}

// Functions that must keep going through the loader's trampolines.
bool loader_only(const char* name) {
  return !strcmp(name, "vkCreateDevice") || !strcmp(name, "vkDestroyDevice") || !strcmp(name, "vkDestroyInstance") ||
         !strcmp(name, "vkCreateInstance") || !strncmp(name, "vkEnumerateInstance", 19);
}

PFN_vkVoidFunction VKAPI_CALL sl_next_gipa(VkInstance instance, const char* name) {
  if (!instance || loader_only(name)) return loader_gipa()(instance, name);
  InstanceData* id = instance_data(instance);
  if (!id) return loader_gipa()(instance, name);   // not ours: use the full chain
  PFN_vkVoidFunction next = id->gipa(instance, name);
  if (!next) return nullptr;
  if (auto w = wrapped(name)) return w;
  return next;
}

// ---------------------------------------------------------------------------
// GetProcAddress patch in Streamline's modules

std::mutex g_patch_mu;
std::set<HMODULE> g_patched;
std::atomic<uint32_t> g_gpa_redirects{0};

// bg3.exe's own NGX (DLSS SR) initialises a second NGX instance on the same
// VkDevice. Whichever NVSDK_NGX_VULKAN_Init runs last owns the process-wide
// NGX state: with Streamline first, DLSS-G feature creation fails
// (0xbad0000b); with the game first, DLSS-G evaluation crashes the driver.
// fg_game_ngx selects what the game's NGX init does:
//   0  pass through (the conflict above)
//   1  blocked: "not supported", the game falls back to TAA (no DLSS SR)
//   2  shared (default): Streamline initialises NGX first; the game's Init*
//      returns success without re-initialising and its Shutdown* is a no-op,
//      so its DLSS SR features are created on Streamline's NGX instance.
int game_ngx_mode() { return g_sl_init ? cfg_int("fg_game_ngx", 2) : 0; }
int64_t ngx_unsupported() { return int64_t(int32_t(0xBAD00001)); }   // NVSDK_NGX_Result_FAIL_FeatureNotSupported
int64_t ngx_success() { return 1; }                                  // NVSDK_NGX_Result_Success

FARPROC WINAPI hk_GetProcAddress_game(HMODULE mod, LPCSTR name) {
  if (reinterpret_cast<uintptr_t>(name) > 0xFFFF && !strncmp(name, "NVSDK_NGX_", 10)) {
    int mode = game_ngx_mode();
    bool init = !strncmp(name, "NVSDK_NGX_VULKAN_Init", 21);
    bool shutdown = !strncmp(name, "NVSDK_NGX_VULKAN_Shutdown", 25);
    FARPROC repl = nullptr;
    if (mode == 1 && init) repl = reinterpret_cast<FARPROC>(&ngx_unsupported);
    if (mode == 2 && (init || shutdown)) repl = reinterpret_cast<FARPROC>(&ngx_success);
    static std::mutex mu;
    static std::set<std::string> seen;
    {
      std::lock_guard<std::mutex> lock(mu);
      if (seen.insert(name).second)
        log("[sl] the game resolves %s%s", name, !repl ? "" : mode == 1 ? " (blocked)" : " (shared with Streamline)");
    }
    if (repl) return repl;
  }
  return GetProcAddress(mod, name);
}

FARPROC WINAPI hk_GetProcAddress(HMODULE mod, LPCSTR name) {
  if (mod && g_vulkan1 && mod == g_vulkan1 && reinterpret_cast<uintptr_t>(name) > 0xFFFF) {
    // Instance-level functions stay on the loader's trampolines: NGX is
    // initialised by sl.common without proc-addr callbacks, loads vulkan-1
    // itself and needs the application-level VkPhysicalDevice, so Streamline
    // is handed that one and must use matching (trampoline) instance functions.
    if (!strcmp(name, "vkGetInstanceProcAddr") && cfg_int("sl_redirect_gipa", 0)) {
      g_gpa_redirects++;
      return reinterpret_cast<FARPROC>(&sl_next_gipa);
    }
    if (!strcmp(name, "vkGetDeviceProcAddr")) {
      g_gpa_redirects++;
      return reinterpret_cast<FARPROC>(&sl_next_gdpa);
    }
  }
  return GetProcAddress(mod, name);
}

void patch_iat(HMODULE mod, const char* label, void* hook = nullptr) {
  if (!hook) hook = reinterpret_cast<void*>(&hk_GetProcAddress);
  auto base = reinterpret_cast<uint8_t*>(mod);
  auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress) return;
  int patched = 0;
  for (auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
    if (!imp->OriginalFirstThunk || !imp->FirstThunk) continue;
    auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
    auto iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++iat) {
      if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
      auto ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
      if (strcmp(reinterpret_cast<const char*>(ibn->Name), "GetProcAddress")) continue;
      DWORD old = 0;
      VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
      iat->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
      VirtualProtect(&iat->u1.Function, sizeof(void*), old, &old);
      ++patched;
    }
  }
  log("[sl] patched GetProcAddress in %s (%d slot%s)", label, patched, patched == 1 ? "" : "s");
}

// Patches sl.interposer only: it builds the Vulkan table every plugin uses.
// (Plugins' own lookups of vulkan-1 exports stay on the trampolines; they use
// them only for the probe instance slInit creates.)
void patch_modules() {
  std::lock_guard<std::mutex> lock(g_patch_mu);
  HMODULE mods[1024];
  DWORD need = 0;
  if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return;
  int extra = cfg_int("sl_patch_plugins", 0);
  for (DWORD i = 0; i < need / sizeof(HMODULE); ++i) {
    char nm[MAX_PATH] = {};
    K32GetModuleBaseNameA(GetCurrentProcess(), mods[i], nm, MAX_PATH);
    bool want = !_stricmp(nm, "sl.interposer.dll") || (extra && !_strnicmp(nm, "sl.", 3));
    if (!want || g_patched.count(mods[i])) continue;
    g_patched.insert(mods[i]);
    patch_iat(mods[i], nm);
  }
}

// ---------------------------------------------------------------------------
// what gets tagged

struct Img {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t w = 0, h = 0;
  VkImageUsageFlags usage = 0;
  uint64_t seen = 0;          // frame_index() when last identified
};

std::mutex g_mu;
Img g_depth, g_mvec, g_hudless, g_ui;
// fg_letterbox=1 (common/fg_extent.h): where the game's output lands in a larger swapchain (the
// DLSS-G backbuffer subrect), from the last UI composite; w 0: the hudless extent at 0,0 (as before).
fg::Rect g_bb_rect{};
VkImageView g_depth_only_view = VK_NULL_HANDLE;   // ours: depth aspect only
VkImage g_depth_only_for = VK_NULL_HANDLE;
std::unordered_map<VkImage, VkImageLayout> g_layout;   // queue-order layout at the last submit
std::unordered_map<VkImage, VkImageLayout> g_input_layout; // observations, including event waits
// Every image that has been a tag (the game rotates several per kind), so
// their layouts are tracked before they are identified in a given frame.
constexpr int kWatch = 32;
std::atomic<VkImage> g_watch[kWatch] = {};
// Legacy cooldown covers all watched images; the keyed policy scopes it to
// current inputs. Real target recreation still waits for stable inputs.
FgStability g_fg_stability;
struct TagDestroyEvent {
  uint64_t frame = 0;
  VkImage image = VK_NULL_HANDLE;
  uint32_t current_roles = 0;  // depth=1, MV=2, hudless=4, UI=8
  bool reset = false;
};
TagDestroyEvent g_last_destroy_event;  // g_mu
std::atomic<bool> g_fg_running{false};
bool g_fg_running_flag() { return g_fg_running.load(); }
// DLSS-G's inputs-processing completion fence (a timeline semaphore on
// Vulkan) and the value that marks the last presented frame's inputs consumed,
// read on the present thread after every present while frame generation runs.
std::mutex g_fence_mu;
VkSemaphore g_inputs_fence = VK_NULL_HANDLE;
uint64_t g_inputs_value = 0;
PFN_vkWaitSemaphores g_wait_semaphores = nullptr;
int g_watch_next = 0;

// Private allocations: MV contents observed at UI composite differ from NGX's inputs.
// Reuse a slot only after the SDK's inputs-consumed fence for its tagged present.
struct MvCopySlot {
  Img image{};
  Img depth{};
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceMemory depth_memory = VK_NULL_HANDLE;
  VkSemaphore fence = VK_NULL_HANDLE;
  FgCopyLease lease;
  uint64_t submitted = UINT64_MAX;
};
std::mutex g_mv_copy_mu;
MvCopySlot g_mv_copies[8]{};
Img g_preserved_mvec; // selected in submit order, under g_mu
Img g_preserved_depth;
uint64_t g_copy_record = 0, g_color_record = 0, g_color_submit = 0;
VkQueryPool g_mv_copy_queries = VK_NULL_HANDLE;
float g_mv_copy_period = 1;
std::vector<double> g_mv_copy_ms;

void preserve_inputs(VkCommandBuffer cmd, const Bg3Binding& source, const Bg3Binding& depth) {
  if (!g_active || !g_fg_running || !source.image || source.format != VK_FORMAT_R16G16_SFLOAT ||
      !depth.image || depth.format != VK_FORMAT_D24_UNORM_S8_UINT ||
      depth.width != source.width || depth.height != source.height) return;
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    auto it = tracker().images.find(source.image);
    if (it == tracker().images.end() || !(it->second.ci.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) return;
    it = tracker().images.find(depth.image);
    if (it == tracker().images.end() || !(it->second.ci.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) return;
  }
  CmdState* state = cmd_state(cmd);
  if (!state || state->sl_mvec_copy.image) return;
  std::lock_guard<std::mutex> lock(g_mv_copy_mu);
  auto* dd = g_dd;
  auto& vk = dd->vk;
  for (unsigned index = 0; index < 8; ++index) {
    auto& slot = g_mv_copies[index];
    if (slot.lease.written) {
      if (!slot.fence || !slot.lease.value || !g_wait_semaphores) continue;
      VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
      wait.semaphoreCount = 1; wait.pSemaphores = &slot.fence; wait.pValues = &slot.lease.value;
      if (!slot.lease.reusable(g_wait_semaphores(dd->device, &wait, 0) == VK_SUCCESS)) continue;
    }
    if (slot.image.image && (slot.image.w != source.width || slot.image.h != source.height)) {
      vk.DestroyImageView(dd->device, slot.image.view, nullptr);
      vk.DestroyImage(dd->device, slot.image.image, nullptr);
      vk.FreeMemory(dd->device, slot.memory, nullptr);
      if (slot.depth.view) vk.DestroyImageView(dd->device, slot.depth.view, nullptr);
      if (slot.depth.image) vk.DestroyImage(dd->device, slot.depth.image, nullptr);
      if (slot.depth_memory) vk.FreeMemory(dd->device, slot.depth_memory, nullptr);
      slot = {};
    }
    auto allocate = [&](const Bg3Binding& src, Img& target, VkDeviceMemory& target_memory, VkImageAspectFlags aspect) {
      if (target.image) return true;
      VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      ci.imageType = VK_IMAGE_TYPE_2D; ci.format = src.format; ci.extent = {src.width, src.height, 1};
      ci.mipLevels = ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
      ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      VkImage image = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE;
      if (vk.CreateImage(dd->device, &ci, nullptr, &image) != VK_SUCCESS) return false;
      VkMemoryRequirements req{}; vk.GetImageMemoryRequirements(dd->device, image, &req);
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.allocationSize = req.size; ai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      bool ok = ai.memoryTypeIndex != UINT32_MAX && vk.AllocateMemory(dd->device, &ai, nullptr, &memory) == VK_SUCCESS &&
                vk.BindImageMemory(dd->device, image, memory, 0) == VK_SUCCESS;
      VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      vi.image = image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = src.format;
      vi.subresourceRange = {aspect, 0, 1, 0, 1};
      ok = ok && vk.CreateImageView(dd->device, &vi, nullptr, &view) == VK_SUCCESS;
      if (!ok) {
        if (view) vk.DestroyImageView(dd->device, view, nullptr);
        vk.DestroyImage(dd->device, image, nullptr);
        if (memory) vk.FreeMemory(dd->device, memory, nullptr);
        return false;
      }
      target = {image, view, src.format, src.width, src.height, ci.usage, 0};
      target_memory = memory;
      log("[sl] input copy slot %u: %ux%u private fmt %d image %p", index, src.width, src.height, int(src.format), (void*)image);
      return true;
    };
    if (!allocate(source,slot.image,slot.memory,VK_IMAGE_ASPECT_COLOR_BIT) ||
        !allocate(depth,slot.depth,slot.depth_memory,VK_IMAGE_ASPECT_DEPTH_BIT)) return;
    if (!g_mv_copy_queries) {
      VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = 16;
      vk.CreateQueryPool(dd->device, &qi, nullptr, &g_mv_copy_queries);
      VkPhysicalDeviceProperties props{};
      dd->inst->GetPhysicalDeviceProperties(dd->physical, &props);
      g_mv_copy_period = props.limits.timestampPeriod;
    }
    if (g_mv_copy_queries) {
      if (slot.lease.written) {
        uint64_t result[4]{};
        if (vk.GetQueryPoolResults(dd->device, g_mv_copy_queries, index * 2, 2, sizeof result, result, 16,
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) == VK_SUCCESS && result[1] && result[3]) {
          g_mv_copy_ms.push_back(double(result[2] - result[0]) * g_mv_copy_period / 1e6);
          if (g_mv_copy_ms.size() == 120) {
            std::sort(g_mv_copy_ms.begin(), g_mv_copy_ms.end());
            log("[sl] gpu FG input copy: median %.3f ms, p99 %.3f ms (120 samples)", g_mv_copy_ms[60], g_mv_copy_ms[118]);
            g_mv_copy_ms.clear();
          }
        }
      }
      vk.CmdResetQueryPool(cmd, g_mv_copy_queries, index * 2, 2);
      vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_mv_copy_queries, index * 2);
    }
    VkImageMemoryBarrier barriers[4]{};
    for (auto& b : barriers) {
      b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    const Bg3Binding* inputs[] = {&source,&depth};
    const Img* targets[] = {&slot.image,&slot.depth};
    for (unsigned i=0;i<2;++i) {
      auto& src = barriers[i*2]; auto& dst = barriers[i*2+1];
      src.image = inputs[i]->image; src.oldLayout = inputs[i]->layout;
      src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      src.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      dst.image = targets[i]->image;
      dst.oldLayout = slot.lease.written ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
      dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      dst.srcAccessMask = slot.lease.written ? VK_ACCESS_MEMORY_READ_BIT : 0; dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      if(i) src.subresourceRange.aspectMask = dst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 4, barriers);
    for (unsigned i=0;i<2;++i) {
      VkImageCopy copy{};
      copy.srcSubresource = copy.dstSubresource = {VkImageAspectFlags(i ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 0, 1};
      copy.extent = {source.width, source.height, 1};
      vk.CmdCopyImage(cmd, inputs[i]->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, targets[i]->image,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }
    for (auto& b : barriers) { std::swap(b.oldLayout, b.newLayout); b.srcAccessMask = b.dstAccessMask; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT; }
    barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[3].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 4, barriers);
    if (g_mv_copy_queries) vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_mv_copy_queries, index * 2 + 1);
    state->sl_mvec_copy = source;
    state->sl_mvec_copy.image = slot.image.image; state->sl_mvec_copy.view = slot.image.view;
    state->sl_mvec_copy.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    state->sl_depth_copy = depth;
    state->sl_depth_copy.image = slot.depth.image; state->sl_depth_copy.view = slot.depth.view;
    state->sl_depth_copy.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    state->sl_mvec_slot = index;
    slot.lease.write(); slot.fence = VK_NULL_HANDLE; slot.submitted = UINT64_MAX;
    if (cfg_int("pan_inputs", 0)) pan_capture(cmd, "rr_mvec_copy", &state->sl_mvec_copy);
    if (cfg_int("pan_inputs", 0)) pan_capture(cmd, "rr_depth_copy", &state->sl_depth_copy);
    return;
  }
  static unsigned failures = 0;
  if (failures++ < 8) log("[sl] input copy unavailable: all slots pending SDK input consumption or different extent");
}

// Camera constants from the last submitted velocity pass.
struct CamRaw {
  float cam[56];              // set 0 binding 2 (224 bytes)
  float prev[56];             // set 0 binding 5 (224 bytes)
};
CamRaw g_cam{};
bool g_cam_valid = false;
uint64_t g_cam_frame = 0;
uint32_t g_cam_log = 0;
std::string g_cam_err;

constexpr uint64_t kVelocityFs = 0x788dfdf60d8ed4cdull;    // camera velocity pass (first draw)
constexpr uint64_t kTonemapFs = 0xd25b8086de1734d5ull;     // tonemap (hudless fallback)

void fill_img(Img& out, VkImage image, VkImageView view) {
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  auto it = tracker().images.find(image);
  if (it == tracker().images.end()) return;
  out.image = image;
  out.view = view;
  out.format = it->second.ci.format;
  out.w = it->second.ci.extent.width;
  out.h = it->second.ci.extent.height;
  out.usage = it->second.ci.usage;
  out.seen = frame_index();
}

void set_watch() {   // caller holds g_mu
  for (VkImage img : {g_depth.image, g_mvec.image, g_hudless.image, g_ui.image}) {
    if (!img) continue;
    bool have = false;
    for (auto& w : g_watch) have |= w.load(std::memory_order_relaxed) == img;
    if (have) continue;
    g_watch[g_watch_next] = img;
    g_watch_next = (g_watch_next + 1) % kWatch;
  }
}

// The uniform buffer behind (set, binding) of the bound graphics sets, read
// from the game's mapped memory.
bool read_ubo(CmdState* s, uint32_t set, uint32_t binding, void* dst, size_t size, std::string& err) {
  VkDescriptorSet ds = s->sets[0][set];
  if (!ds) return err = "set not bound", false;
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  auto si = tracker().sets.find(ds);
  if (si == tracker().sets.end()) return err = "set unknown", false;
  auto ei = si->second.find(desc_key(binding, 0));
  if (ei == si->second.end()) return err = "binding not written", false;
  const DescEntry& e = ei->second;
  VkDeviceSize off = e.offset;
  if (e.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || e.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
    // dynamic offsets of a set come in binding order
    uint32_t idx = 0;
    for (auto& [k, o] : si->second)
      if ((o.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || o.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) &&
          (o.binding < binding || (o.binding == binding && o.element < e.element)))
        ++idx;
    if (idx >= s->dyn[0][set].size()) return err = "dynamic offset missing", false;
    off += s->dyn[0][set][idx];
  }
  auto bm = tracker().buffer_mem.find(e.buffer);
  if (bm == tracker().buffer_mem.end()) return err = "buffer memory unknown", false;
  auto mp = tracker().mapped.find(bm->second.first);
  if (mp == tracker().mapped.end() || !mp->second) return err = "memory not mapped", false;
  memcpy(dst, mp->second + bm->second.second + off, size);
  return true;
}

// ---- small row-major matrix helpers (row-vector convention: v * M) ----
struct M4 { float m[4][4]; };
M4 mul(const M4& a, const M4& b) {
  M4 r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      for (int k = 0; k < 4; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
  return r;
}
bool inv(const M4& in, M4& out) {
  double a[4][8];
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 8; ++j) a[i][j] = j < 4 ? in.m[i][j] : (j - 4 == i ? 1.0 : 0.0);
  for (int c = 0; c < 4; ++c) {
    int p = c;
    for (int r = c + 1; r < 4; ++r)
      if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
    if (std::fabs(a[p][c]) < 1e-20) return false;
    if (p != c)
      for (int j = 0; j < 8; ++j) std::swap(a[p][j], a[c][j]);
    double d = a[c][c];
    for (int j = 0; j < 8; ++j) a[c][j] /= d;
    for (int r = 0; r < 4; ++r)
      if (r != c) {
        double f = a[r][c];
        for (int j = 0; j < 8; ++j) a[r][j] -= f * a[c][j];
      }
  }
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) out.m[i][j] = float(a[i][j + 4]);
  return true;
}
M4 ident() {
  M4 r{};
  for (int i = 0; i < 4; ++i) r.m[i][i] = 1;
  return r;
}
M4 load(const float* f) {
  M4 r;
  memcpy(r.m, f, sizeof r.m);
  return r;
}
void store(sl::float4x4& d, const M4& s) {
  for (int i = 0; i < 4; ++i) d.setRow(i, sl::float4(s.m[i][0], s.m[i][1], s.m[i][2], s.m[i][3]));
}
std::string mstr(const M4& m) {
  char b[512];
  snprintf(b, sizeof b, "[%.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g]",
           m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3], m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3], m.m[2][0],
           m.m[2][1], m.m[2][2], m.m[2][3], m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]);
  return b;
}

// ---------------------------------------------------------------------------
// frame protocol

sl::FrameToken* g_token = nullptr;
uint32_t g_token_index = 0;
int g_fg_applied = -1;                 // fg value last passed to slDLSSGSetOptions
uint32_t g_flags_applied = 0xffffffff;
uint64_t g_last_state_log = 0;
std::atomic<uint32_t> g_api_errors{0};
std::atomic<int> g_last_api_error{0};
uint64_t g_prev_displayed = 0, g_prev_frame = 0;

void api_error_cb(const sl::APIError& e) {
  g_api_errors++;
  g_last_api_error = int(e.vkRes);
}

// Reflex mode and frame limiter (reflex, fps_cap: live). fps_cap caps the
// rendered frame rate; with DLSS-G the display shows a multiple of it.
int g_reflex_applied = -1, g_cap_applied = -1;
void reflex_apply() {
  if (!g_sl.ReflexSetOptions) return;
  int reflex = cfg_int("reflex", 1), cap = cfg_int("fps_cap", 0);
  if (cap < 0) cap = 0;
  if (reflex == g_reflex_applied && cap == g_cap_applied) return;
  g_reflex_applied = reflex;
  g_cap_applied = cap;
  sl::ReflexOptions ro{};
  ro.mode = reflex == 2 ? sl::ReflexMode::eLowLatencyWithBoost : reflex ? sl::ReflexMode::eLowLatency : sl::ReflexMode::eOff;
  ro.frameLimitUs = cap > 0 ? uint32_t(1000000 / cap) : 0;
  sl::Result rr = g_sl.ReflexSetOptions(ro);
  log("[sl] slReflexSetOptions(mode %d, frame limit %u us = fps_cap %d): %s", int(ro.mode), ro.frameLimitUs, cap,
      res_str(rr));
}

void new_frame() {
  if (!g_sl.GetNewFrameToken) return;
  reflex_apply();
  ++g_token_index;
  if (g_sl.GetNewFrameToken(g_token, &g_token_index) != sl::Result::eOk) g_token = nullptr;
  if (!g_token) return;
  if (g_sl.ReflexSleep) g_sl.ReflexSleep(*g_token);
  if (g_sl.PCLSetMarker) {
    g_sl.PCLSetMarker(sl::PCLMarker::eSimulationStart, *g_token);
    g_sl.PCLSetMarker(sl::PCLMarker::eSimulationEnd, *g_token);
    g_sl.PCLSetMarker(sl::PCLMarker::eRenderSubmitStart, *g_token);
  }
}

sl::Resource make_res(const Img& i, VkImageView view, VkImageLayout layout) {
  sl::Resource r(sl::ResourceType::eTex2d, reinterpret_cast<void*>(i.image), nullptr, reinterpret_cast<void*>(view),
                 uint32_t(layout));
  r.width = i.w;
  r.height = i.h;
  r.nativeFormat = uint32_t(i.format);
  r.mipLevels = 1;
  r.arrayLayers = 1;
  r.usage = i.usage;
  return r;
}

VkImageLayout layout_of(VkImage img, VkImageLayout fallback) {
  auto it = g_layout.find(img);
  return it == g_layout.end() || it->second == VK_IMAGE_LAYOUT_UNDEFINED ? fallback : it->second;
}

VkImageLayout recorded_layout(VkCommandBuffer cmd, VkImage image, VkImageLayout fallback) {
  if (auto* state = cmd_state(cmd))
    for (const auto& [img, layout] : state->sl_input_layouts)
      if (img == image) return layout;
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_input_layout.find(image);
  if (it != g_input_layout.end()) return it->second;
  return layout_of(image, fallback);
}

void ensure_depth_view() {
  if (g_depth.image == g_depth_only_for && g_depth_only_view) return;
  if (g_depth_only_view) defer_destroy(BG3_DEFER_IMAGE_VIEW, uint64_t(g_depth_only_view));
  g_depth_only_view = VK_NULL_HANDLE;
  g_depth_only_for = g_depth.image;
  if (!g_depth.image) return;
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = g_depth.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = g_depth.format;
  vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
  if (g_dd->vk.CreateImageView(g_dd->device, &vi, nullptr, &g_depth_only_view) != VK_SUCCESS)
    g_depth_only_view = VK_NULL_HANDLE;
  log("[sl] depth-only view %p for depth image %p (%ux%u fmt %d)", (void*)g_depth_only_view, (void*)g_depth.image,
      g_depth.w, g_depth.h, g_depth.format);
}

// Builds sl::Constants from the camera uniform buffers (see FRAME_MAP.md).
// Layout (std140, row-major, v * M):
//   cam  (set0 b2): mat4 m0, m1, m2 = view-projection relative to the camera; float near?, far?; vec2; vec3 camPos @208
//   prev (set0 b5): mat4 m0, m1, m2 = previous view-projection relative to the previous camera; vec3 prevCamPos @192;
//                   float; vec2 jitter @208; vec2 prevJitter @216
void fill_constants(sl::Constants& c, uint32_t rw, uint32_t rh, const CamRaw* snapshot = nullptr, bool snapshot_valid = false) {
  const CamRaw& cam = snapshot ? *snapshot : g_cam;
  const bool cam_valid = snapshot ? snapshot_valid : g_cam_valid;
  float sx = cfg_float("fg_mvec_scale_x", -1.0f), sy = cfg_float("fg_mvec_scale_y", -1.0f);
  c.mvecScale = sl::float2(sx / float(rw ? rw : 1), sy / float(rh ? rh : 1));
  c.jitterOffset = sl::float2(0, 0);
  c.depthInverted = sl::Boolean::eTrue;
  c.cameraMotionIncluded = sl::Boolean::eTrue;
  c.motionVectors3D = sl::Boolean::eFalse;
  c.orthographicProjection = sl::Boolean::eFalse;
  c.motionVectorsDilated = sl::Boolean::eFalse;
  c.motionVectorsJittered = cfg_int("fg_mvec_jittered", 0) ? sl::Boolean::eTrue : sl::Boolean::eFalse;
  c.reset = sl::Boolean::eFalse;
  c.cameraPinholeOffset = sl::float2(0, 0);
  M4 I = ident();
  store(c.clipToLensClip, I);
  c.cameraNear = cfg_float("fg_near", 0.1f);
  c.cameraFar = cfg_float("fg_far", 1000.0f);
  c.cameraAspectRatio = float(rw) / float(rh ? rh : 1);
  c.cameraFOV = cfg_float("fg_fov", 0.785f);
  c.cameraPos = sl::float3(0, 0, 0);
  c.cameraUp = sl::float3(0, 1, 0);
  c.cameraRight = sl::float3(1, 0, 0);
  c.cameraFwd = sl::float3(0, 0, 1);

  M4 vp_c = I, vp_p = I, proj = I, c2p_log = I;
  bool ok = false;
  float jx = 0, jy = 0;
  if (cam_valid && cfg_int("fg_camera", 1)) {
    // m1 is the jittered reversed-Z projection (row-vector, w = view z): the
    // jitter sits in row 2, columns 0 and 1. clip.xy_unjittered = clip.xy -
    // P[2][0..1] * w, i.e. post-multiplying by S with S[3][0..1] = -P[2][0..1].
    proj = load(&cam.cam[16]);
    M4 pprev = load(&cam.prev[16]);
    M4 s_c = I, s_p = I;
    s_c.m[3][0] = -proj.m[2][0];
    s_c.m[3][1] = -proj.m[2][1];
    bool prev_is_proj = std::fabs(pprev.m[2][3] - 1.0f) < 1e-3f && std::fabs(pprev.m[0][1]) < 1e-6f;
    if (prev_is_proj) {
      s_p.m[3][0] = -pprev.m[2][0];
      s_p.m[3][1] = -pprev.m[2][1];
    } else {
      s_p = s_c;
    }
    float pjx = proj.m[2][0], pjy = proj.m[2][1];
    vp_c = mul(load(&cam.cam[32]), s_c);
    vp_p = mul(load(&cam.prev[32]), s_p);
    proj.m[2][0] = 0;
    proj.m[2][1] = 0;
    if (cam.cam[48] > 0 && cam.cam[49] > cam.cam[48]) {
      c.cameraNear = cam.cam[48];
      c.cameraFar = cam.cam[49];
    }
    const float* cp = &cam.cam[52];
    const float* pp = &cam.prev[48];
    // jitter in pixels, from the projection: x = P20 * W / 2, y = -P21 * H / 2 (y down)
    jx = pjx * float(rw) * 0.5f;
    jy = -pjy * float(rh) * 0.5f;
    M4 t = I;
    t.m[3][0] = cp[0] - pp[0];
    t.m[3][1] = cp[1] - pp[1];
    t.m[3][2] = cp[2] - pp[2];
    M4 ivp;
    if (inv(vp_c, ivp)) {
      M4 c2p = mul(mul(ivp, t), vp_p);
      M4 p2c;
      if (inv(c2p, p2c)) {
        store(c.clipToPrevClip, c2p);
        store(c.prevClipToClip, p2c);
        c2p_log = c2p;
        ok = true;
      }
    }
    // View = VP * inv(P) (both camera-relative): its rows give the camera basis.
    M4 iproj;
    if (inv(proj, iproj)) {
      store(c.cameraViewToClip, proj);
      store(c.clipToCameraView, iproj);
      M4 view = mul(vp_c, iproj);
      M4 iview;
      if (inv(view, iview)) {   // camera-to-world (rotation only): rows are right, up, forward
        c.cameraRight = sl::float3(iview.m[0][0], iview.m[0][1], iview.m[0][2]);
        c.cameraUp = sl::float3(iview.m[1][0], iview.m[1][1], iview.m[1][2]);
        c.cameraFwd = sl::float3(iview.m[2][0], iview.m[2][1], iview.m[2][2]);
      }
      // perspective: P[1][1] = cot(fov/2)
      if (std::fabs(proj.m[1][1]) > 1e-6f) c.cameraFOV = 2.0f * std::atan(1.0f / std::fabs(proj.m[1][1]));
    }
    c.cameraPos = sl::float3(cp[0], cp[1], cp[2]);
    if (cfg_int("fg_jitter", 1)) c.jitterOffset = sl::float2(jx * cfg_float("fg_jitter_sx", 1.0f), jy * cfg_float("fg_jitter_sy", 1.0f));
    if (g_cam_log < 3 || (cfg_int("fg_log_camera", 0) && frame_index() % 600 == 0)) {
      ++g_cam_log;
      log("[sl] camera b2.m0 %s", mstr(load(&cam.cam[0])).c_str());
      log("[sl] camera b2.m1 %s", mstr(load(&cam.cam[16])).c_str());
      log("[sl] camera b2.m2 %s", mstr(vp_c).c_str());
      log("[sl] camera b2 floats %g %g | %g %g | pos %g %g %g | %d", cam.cam[48], cam.cam[49], cam.cam[50],
          cam.cam[51], cp[0], cp[1], cp[2], *reinterpret_cast<const int*>(&cam.cam[55]));
      log("[sl] camera b5.m2 %s", mstr(vp_p).c_str());
      log("[sl] camera b5 prevpos %g %g %g | %g | jitter %g %g | prev jitter %g %g", pp[0], pp[1], pp[2],
          cam.prev[51], cam.prev[52], cam.prev[53], cam.prev[54], cam.prev[55]);
      log("[sl] clipToPrevClip %s (ok %d) fov %.3f", mstr(c2p_log).c_str(), ok,
          c.cameraFOV);
    }
  }
  if (!ok) {
    store(c.cameraViewToClip, I);
    store(c.clipToCameraView, I);
    store(c.clipToPrevClip, I);
    store(c.prevClipToClip, I);
  }
}

}  // namespace

void sl_preserve_inputs(VkCommandBuffer cmd, const Bg3Binding& source, const Bg3Binding& depth) { preserve_inputs(cmd, source, depth); }
VkImageLayout sl_input_layout(VkCommandBuffer cmd, VkImage image, VkImageLayout fallback) {
  return recorded_layout(cmd, image, fallback);
}

// ---------------------------------------------------------------------------
// crash diagnostics: the game's own crash handler exits silently, so fatal
// exceptions are logged (module + offset, and the frames above them) first.

namespace {
std::string where(void* addr) {
  HMODULE mod = nullptr;
  char name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(addr), &mod) && mod) {
    GetModuleFileNameA(mod, name, MAX_PATH);
    const char* base = strrchr(name, '\\');
    char b[MAX_PATH + 32];
    snprintf(b, sizeof b, "%s+0x%llx", base ? base + 1 : name,
             (unsigned long long)(reinterpret_cast<uint8_t*>(addr) - reinterpret_cast<uint8_t*>(mod)));
    return b;
  }
  char b[32];
  snprintf(b, sizeof b, "%p", addr);
  return b;
}

std::atomic<int> g_veh_logged{0};
using WriteMiniDump = decltype(&MiniDumpWriteDump);
WriteMiniDump g_write_minidump = nullptr;
std::string g_crash_dir;

LONG CALLBACK veh(EXCEPTION_POINTERS* ep) {
  DWORD code = ep->ExceptionRecord->ExceptionCode;
  bool fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
               code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
               code == EXCEPTION_PRIV_INSTRUCTION || code == 0xC0000409 /* fast fail / GS */ ||
               code == 0xC0000374 /* heap corruption */ || code == 0x40000015 /* abort */;
  if (!fatal) return EXCEPTION_CONTINUE_SEARCH;
  const int ordinal = g_veh_logged.fetch_add(1);
  if (ordinal >= cfg_int("sl_veh_max", 8)) return EXCEPTION_CONTINUE_SEARCH;
  void* frames[24];
  USHORT n = CaptureStackBackTrace(0, 24, frames, nullptr);
  std::string st;
  for (USHORT i = 0; i < n; ++i) st += " " + where(frames[i]);
  ULONG_PTR info0 = ep->ExceptionRecord->NumberParameters > 0 ? ep->ExceptionRecord->ExceptionInformation[0] : 0;
  ULONG_PTR info1 = ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : 0;
  log("[crash] exception 0x%08lx at %s (thread %lu, info %llu %p) stack:%s", code,
      where(ep->ExceptionRecord->ExceptionAddress).c_str(), GetCurrentThreadId(), (unsigned long long)info0,
      (void*)info1, st.c_str());
  if (g_write_minidump && ordinal == 0) {
    const std::string path = g_crash_dir + "\\bg3-" + std::to_string(GetCurrentProcessId()) + "-" +
                             std::to_string(GetTickCount64()) + ".dmp";
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), ep, FALSE};
      const BOOL saved = g_write_minidump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal,
                                        &exception, nullptr, nullptr);
      const DWORD error = saved ? 0 : GetLastError();
      CloseHandle(file);
      log("[crash] first-chance fatal minidump %s: %s (error %lu); a dump alone does not prove an unhandled crash",
          saved ? "saved" : "FAILED", path.c_str(), error);
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

void install_exception_diagnostics() {
  static std::once_flag once;
  std::call_once(once, [] {
    if (!cfg_int("sl_veh", 1)) return;
    if (cfg_int("crash_dump", 0)) {
      g_crash_dir = root_dir() + "\\captures\\crash";
      make_dirs(g_crash_dir);
      // Load outside the exception path; do not acquire the loader lock there.
      HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
      if (dbghelp) g_write_minidump = reinterpret_cast<WriteMiniDump>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    }
    AddVectoredExceptionHandler(1, veh);
    log("[crash] exception diagnostics active independently of FG; minidump %s", g_write_minidump ? "armed" : "off");
  });
}
}  // namespace

namespace {
bool fg_running_now() { return g_fg_running.load(); }
}  // namespace

// ---------------------------------------------------------------------------
// instance

void sl_instance_pre(const VkInstanceCreateInfo* in, VkInstanceCreateInfo& out) {
  static std::vector<const char*> exts;
  static VkApplicationInfo app;
  out = *in;
  // Streamline's own probe instance, or an instance after the device exists.
  // BG3 may create (and destroy) a probe instance of its own first: Streamline
  // is initialised once, and binds to the last instance created before the device.
  if (g_in_sl_init || g_dd) return;
  install_exception_diagnostics();
  if (!g_sl_init) {
  int fg = cfg_int("fg", 2);
  if (fg < 2 && !cfg_int("sl", 0)) {
    log("[sl] fg=%d: Streamline not loaded", fg);
    return;
  }
  std::string dir = bin_dir() + (cfg_int("sl_dev", 0) ? "\\sl_dev" : "\\sl");
  std::wstring wdir = widen(dir);
  g_vulkan1 = GetModuleHandleA("vulkan-1.dll");
  g_interposer = LoadLibraryW((wdir + L"\\sl.interposer.dll").c_str());
  if (!g_interposer) {
    log("[sl] cannot load %s\\sl.interposer.dll (error %lu)", dir.c_str(), GetLastError());
    return;
  }
  patch_modules();
  if (cfg_int("fg_watch_game_ngx", 1)) patch_iat(GetModuleHandleA(nullptr), "bg3.exe", reinterpret_cast<void*>(&hk_GetProcAddress_game));
#define SLF(n) g_sl.n = reinterpret_cast<PFun_sl##n*>(GetProcAddress(g_interposer, "sl" #n))
  SLF(Init);
  SLF(Shutdown);
  SLF(IsFeatureSupported);
  SLF(IsFeatureLoaded);
  SLF(GetFeatureRequirements);
  SLF(GetFeatureFunction);
  SLF(GetNewFrameToken);
  SLF(SetTagForFrame);
  SLF(SetConstants);
  SLF(SetVulkanInfo);
#undef SLF
  g_sl.gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(g_interposer, "vkGetDeviceProcAddr"));
  g_sl.gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(g_interposer, "vkGetInstanceProcAddr"));
  if (!g_sl.Init || !g_sl.SetVulkanInfo || !g_sl.gdpa) {
    log("[sl] sl.interposer.dll lacks expected exports");
    return;
  }

  static std::wstring s_dir = wdir;
  static std::wstring s_logs = widen(root_dir() + "\\sl_logs");
  make_dirs(root_dir() + "\\sl_logs");
  static const wchar_t* paths[] = {s_dir.c_str()};
  static const sl::Feature features[] = {sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
  sl::Preferences pref{};
  pref.showConsole = false;
  pref.logLevel = cfg_int("sl_verbose", 0) ? sl::LogLevel::eVerbose : sl::LogLevel::eDefault;
  pref.pathsToPlugins = paths;
  pref.numPathsToPlugins = 1;
  pref.pathToLogsAndData = s_logs.c_str();
  pref.logMessageCallback = &sl_logcb;
  pref.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseManualHooking |
               sl::PreferenceFlags::eDisableDebugText | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
  pref.featuresToLoad = features;
  pref.numFeaturesToLoad = 3;
  pref.applicationId = uint32_t(cfg_int("sl_app_id", 0));
  pref.engine = sl::EngineType::eCustom;
  pref.engineVersion = "4.0";
  pref.projectId = "6c0e5a3b-2f4d-4b8e-9a17-3c5d8e1f0b42";
  pref.renderAPI = sl::RenderAPI::eVulkan;
  g_in_sl_init = true;
  sl::Result r = g_sl.Init(pref, sl::kSDKVersion);
  g_in_sl_init = false;
  log("[sl] slInit (plugins from %s): %s", dir.c_str(), res_str(r));
  if (r != sl::Result::eOk) return;
  g_sl_init = true;
  patch_modules();

  for (sl::Feature f : features) {
    bool loaded = false;
    if (g_sl.IsFeatureLoaded) g_sl.IsFeatureLoaded(f, loaded);
    sl::FeatureRequirements q{};
    sl::Result rr = g_sl.GetFeatureRequirements(f, q);
    std::string s;
    for (uint32_t i = 0; i < q.vkNumInstanceExtensions; ++i) {
      g_req.inst_ext.push_back(q.vkInstanceExtensions[i]);
      s += std::string(" i:") + q.vkInstanceExtensions[i];
    }
    for (uint32_t i = 0; i < q.vkNumDeviceExtensions; ++i) {
      g_req.dev_ext.push_back(q.vkDeviceExtensions[i]);
      s += std::string(" d:") + q.vkDeviceExtensions[i];
    }
    for (uint32_t i = 0; i < q.vkNumFeatures12; ++i) {
      g_req.feat12.push_back(q.vkFeatures12[i]);
      s += std::string(" f12:") + q.vkFeatures12[i];
    }
    for (uint32_t i = 0; i < q.vkNumFeatures13; ++i) {
      g_req.feat13.push_back(q.vkFeatures13[i]);
      s += std::string(" f13:") + q.vkFeatures13[i];
    }
    g_req.gfx_queues += q.vkNumGraphicsQueuesRequired;
    g_req.compute_queues += q.vkNumComputeQueuesRequired;
    g_req.of_queues += q.vkNumOpticalFlowQueuesRequired;
    log("[sl] feature %u loaded %d reqs %s flags 0x%x queues gfx %u compute %u of %u driver %u.%u.%u (need %u.%u.%u) "
        "os %u.%u.%u%s",
        f, loaded, res_str(rr), uint32_t(q.flags), q.vkNumGraphicsQueuesRequired, q.vkNumComputeQueuesRequired,
        q.vkNumOpticalFlowQueuesRequired, q.driverVersionDetected.major, q.driverVersionDetected.minor,
        q.driverVersionDetected.build, q.driverVersionRequired.major, q.driverVersionRequired.minor,
        q.driverVersionRequired.build, q.osVersionDetected.major, q.osVersionDetected.minor,
        q.osVersionDetected.build, s.c_str());
  }

  }  // !g_sl_init
  if (!g_sl_init) return;
  // instance: Streamline's extensions, API 1.3 (what the interposer does in its own vkCreateInstance)
  exts.assign(in->ppEnabledExtensionNames, in->ppEnabledExtensionNames + in->enabledExtensionCount);
  for (auto& e : g_req.inst_ext) {
    bool have = false;
    for (auto* x : exts) have |= !strcmp(x, e.c_str());
    if (!have) {
      exts.push_back(e.c_str());
      log("[sl] + instance extension %s", e.c_str());
    }
  }
  out.enabledExtensionCount = uint32_t(exts.size());
  out.ppEnabledExtensionNames = exts.data();
  app = in->pApplicationInfo ? *in->pApplicationInfo : VkApplicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  if (app.apiVersion < VK_API_VERSION_1_3) {
    log("[sl] instance API %u.%u -> 1.3", VK_API_VERSION_MAJOR(app.apiVersion), VK_API_VERSION_MINOR(app.apiVersion));
    app.apiVersion = VK_API_VERSION_1_3;
  }
  out.pApplicationInfo = &app;
}

void sl_instance_post(VkInstance instance, VkResult r) {
  if (g_in_sl_init || g_dd || !g_sl_init) return;
  if (r != VK_SUCCESS) {
    log("[sl] vkCreateInstance failed (%d) with Streamline's extensions", r);
    return;
  }
  g_sl_instance = instance;
  if (g_sl.gipa) {
    g_px.CreateWin32SurfaceKHR =
        reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(g_sl.gipa(instance, "vkCreateWin32SurfaceKHR"));
    g_px.DestroySurfaceKHR = reinterpret_cast<PFN_vkDestroySurfaceKHR>(g_sl.gipa(instance, "vkDestroySurfaceKHR"));
  }
}

// DLSS-G finds the game window through the interposer's surface -> HWND map,
// which only its vkCreateWin32SurfaceKHR proxy fills. The game creates its
// surface before Streamline has a device (and so an instance table), so the
// surface is created normally and remembered here; just before the swapchain
// is created the proxy is called once for that window, and this layer answers
// the nested vkCreateWin32SurfaceKHR with the game's existing surface.
namespace {
std::mutex g_surf_mu;
std::unordered_map<VkSurfaceKHR, HWND> g_surfaces;
std::set<VkSurfaceKHR> g_registered;
thread_local VkSurfaceKHR t_fake_surface = VK_NULL_HANDLE;

void register_surface(VkInstance inst, VkSurfaceKHR surface) {
  HWND hwnd = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_surf_mu);
    if (g_registered.count(surface)) return;
    auto it = g_surfaces.find(surface);
    if (it != g_surfaces.end()) hwnd = it->second;
  }
  if (!hwnd || !g_sl.gipa) {
    log("[sl] surface %p: no window known", (void*)surface);
    return;
  }
  auto proxy = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(g_sl.gipa(inst, "vkCreateWin32SurfaceKHR"));
  if (!proxy) return;
  VkWin32SurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
  ci.hinstance = GetModuleHandleA(nullptr);
  ci.hwnd = hwnd;
  VkSurfaceKHR out = VK_NULL_HANDLE;
  t_fake_surface = surface;
  VkResult r = proxy(inst, &ci, nullptr, &out);
  t_fake_surface = VK_NULL_HANDLE;
  log("[sl] surface %p -> window %p registered with Streamline (%d, %p)", (void*)surface, (void*)hwnd, r, (void*)out);
  std::lock_guard<std::mutex> lock(g_surf_mu);
  g_registered.insert(surface);
}
}  // namespace

bool sl_create_surface(VkInstance i, const VkWin32SurfaceCreateInfoKHR* ci, const VkAllocationCallbacks* a,
                       VkSurfaceKHR* out, VkResult* r) {
  (void)i, (void)a;
  if (t_fake_surface) {   // the interposer's proxy, called by register_surface
    *out = t_fake_surface;
    *r = VK_SUCCESS;
    return true;
  }
  (void)ci;
  return false;
}

void sl_note_surface(VkSurfaceKHR surface, HWND hwnd) {
  std::lock_guard<std::mutex> lock(g_surf_mu);
  if (hwnd) g_surfaces[surface] = hwnd;
  else {
    g_surfaces.erase(surface);
    g_registered.erase(surface);
  }
}

bool sl_destroy_surface(VkInstance i, VkSurfaceKHR s, const VkAllocationCallbacks* a) {
  (void)i, (void)s, (void)a;
  return false;
}

// ---------------------------------------------------------------------------
// device

struct SlDeviceScratch {
  std::vector<const char*> exts;
  std::vector<std::string> ext_store;
  std::vector<VkDeviceQueueCreateInfo> queues;
  std::vector<std::vector<float>> prios;
  VkPhysicalDeviceOpticalFlowFeaturesNV of{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV};
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
  VkPhysicalDeviceSynchronization2Features sync2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
  VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
  VkPhysicalDeviceShaderFloat16Int8Features f16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
  VkPhysicalDeviceDynamicRenderingFeatures dynr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
  VkPhysicalDeviceMaintenance4Features m4{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES};
  VkPhysicalDeviceHostQueryResetFeatures hqr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES};
  uint32_t gfx_family = UINT32_MAX, gfx_index = 0, compute_family = UINT32_MAX, compute_index = 0;
  uint32_t of_family = UINT32_MAX;
  bool active = false;
};

namespace {

VkBaseOutStructure* find_struct(const void* chain, VkStructureType t) {
  for (auto p = static_cast<VkBaseOutStructure*>(const_cast<void*>(chain)); p; p = p->pNext)
    if (p->sType == t) return p;
  return nullptr;
}

// Turns a VkBool32 feature on in the game's own struct when it chains one,
// otherwise chains `ours` (with the field set) in front.
template <class T>
void want_feature(VkDeviceCreateInfo& ci, std::set<const void*>& chained, T& ours, VkBool32 T::*field,
                  VkStructureType alt = VkStructureType(0)) {
  VkBaseOutStructure* s = find_struct(ci.pNext, ours.sType);
  if (!s && alt) s = find_struct(ci.pNext, alt);
  if (s) {
    reinterpret_cast<T*>(s)->*field = VK_TRUE;
    return;
  }
  ours.*field = VK_TRUE;
  if (chained.insert(&ours).second) {
    ours.pNext = const_cast<void*>(ci.pNext);
    ci.pNext = &ours;
  }
}

}  // namespace

SlDeviceScratch* sl_device_pre(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* in,
                               VkDeviceCreateInfo& out) {
  out = *in;
  if (!g_sl_init || !id || id->instance != g_sl_instance || g_dd) return nullptr;
  auto* s = new SlDeviceScratch;
  s->active = true;

  // queue families
  uint32_t nf = 0;
  id->GetPhysicalDeviceQueueFamilyProperties(phys, &nf, nullptr);
  std::vector<VkQueueFamilyProperties> fam(nf);
  id->GetPhysicalDeviceQueueFamilyProperties(phys, &nf, fam.data());
  for (uint32_t i = 0; i < nf; ++i) {
    log("[sl] queue family %u: flags 0x%x count %u", i, fam[i].queueFlags, fam[i].queueCount);
    if ((fam[i].queueFlags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) && s->of_family == UINT32_MAX) s->of_family = i;
    if ((fam[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && s->gfx_family == UINT32_MAX) s->gfx_family = i;
    if ((fam[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(fam[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
        s->compute_family == UINT32_MAX)
      s->compute_family = i;
  }
  if (s->compute_family == UINT32_MAX) s->compute_family = s->gfx_family;

  // supported features
  VkPhysicalDeviceOpticalFlowFeaturesNV sup_of{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV};
  VkPhysicalDeviceSynchronization2Features sup_s2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES, &sup_of};
  VkPhysicalDeviceFeatures2 sup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &sup_s2};
  if (id->GetPhysicalDeviceFeatures2) id->GetPhysicalDeviceFeatures2(phys, &sup);
  bool native_of = sup_of.opticalFlow && sup_s2.synchronization2 && s->of_family != UINT32_MAX &&
                   cfg_int("fg_native_of", 1);

  // extensions
  uint32_t ne = 0;
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &ne, nullptr);
  std::vector<VkExtensionProperties> avail(ne);
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &ne, avail.data());
  auto available = [&](const std::string& e) {
    for (auto& a : avail)
      if (e == a.extensionName) return true;
    return false;
  };
  std::vector<std::string> names(in->ppEnabledExtensionNames, in->ppEnabledExtensionNames + in->enabledExtensionCount);
  auto has = [&](const std::string& e) {
    for (auto& n : names)
      if (n == e) return true;
    return false;
  };
  std::vector<std::string> want = g_req.dev_ext;
  want.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
  want.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
  if (native_of) {
    want.push_back(VK_NV_OPTICAL_FLOW_EXTENSION_NAME);
    want.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
  }
  for (auto& e : want) {
    if (has(e)) continue;
    if (e == VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME && has(VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)) {
      log("[sl] keeping the game's VK_EXT_buffer_device_address instead of adding the KHR one (VUID-03328)");
      continue;
    }
    if (!available(e)) {
      log("[sl] device extension %s requested by Streamline is not available", e.c_str());
      continue;
    }
    names.push_back(e);
    log("[sl] + device extension %s", e.c_str());
  }
  s->ext_store = names;
  for (auto& n : s->ext_store) s->exts.push_back(n.c_str());
  out.enabledExtensionCount = uint32_t(s->exts.size());
  out.ppEnabledExtensionNames = s->exts.data();

  // features
  bool game12 = find_struct(in->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) != nullptr;
  bool game13 = find_struct(in->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES) != nullptr;
  if (game12 || game13) log("[sl] the game chains VkPhysicalDeviceVulkan1%sFeatures: not merged", game12 ? "2" : "3");
  std::vector<std::string> feats = g_req.feat12;
  feats.insert(feats.end(), g_req.feat13.begin(), g_req.feat13.end());
  feats.push_back("timelineSemaphore");
  std::set<const void*> chained;
  if (native_of) feats.push_back("synchronization2");
  for (auto& f : feats) {
    if (f == "timelineSemaphore")
      want_feature(out, chained, s->timeline, &VkPhysicalDeviceTimelineSemaphoreFeatures::timelineSemaphore);
    else if (f == "synchronization2")
      want_feature(out, chained, s->sync2, &VkPhysicalDeviceSynchronization2Features::synchronization2);
    else if (f == "bufferDeviceAddress")
      want_feature(out, chained, s->bda, &VkPhysicalDeviceBufferDeviceAddressFeatures::bufferDeviceAddress,
                   VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT);
    else if (f == "shaderFloat16")
      want_feature(out, chained, s->f16, &VkPhysicalDeviceShaderFloat16Int8Features::shaderFloat16);
    else if (f == "shaderInt8")
      want_feature(out, chained, s->f16, &VkPhysicalDeviceShaderFloat16Int8Features::shaderInt8);
    else if (f == "dynamicRendering")
      want_feature(out, chained, s->dynr, &VkPhysicalDeviceDynamicRenderingFeatures::dynamicRendering);
    else if (f == "maintenance4")
      want_feature(out, chained, s->m4, &VkPhysicalDeviceMaintenance4Features::maintenance4);
    else if (f == "hostQueryReset")
      want_feature(out, chained, s->hqr, &VkPhysicalDeviceHostQueryResetFeatures::hostQueryReset);
    else if (f == "descriptorIndexing")
      ;  // enabled through VK_EXT_descriptor_indexing, which the game already has
    else
      log("[sl] feature %s requested by Streamline: no mapping, not enabled", f.c_str());
    log("[sl] + feature %s", f.c_str());
  }
  if (native_of) want_feature(out, chained, s->of, &VkPhysicalDeviceOpticalFlowFeaturesNV::opticalFlow);

  // queues: Streamline's graphics/compute queues go after the game's in the
  // same families; the optical-flow queue is index 0 of its own family.
  s->queues.assign(in->pQueueCreateInfos, in->pQueueCreateInfos + in->queueCreateInfoCount);
  s->prios.resize(s->queues.size() + 2);
  auto add_to = [&](uint32_t family, uint32_t extra, uint32_t& start) {
    if (!extra) return;
    for (size_t i = 0; i < s->queues.size(); ++i) {
      auto& q = s->queues[i];
      if (q.queueFamilyIndex != family) continue;
      if (q.queueCount + extra > fam[family].queueCount) {
        log("[sl] family %u has no room for %u more queues", family, extra);
        return;
      }
      start = q.queueCount;
      s->prios[i].assign(q.pQueuePriorities, q.pQueuePriorities + q.queueCount);
      s->prios[i].resize(q.queueCount + extra, 0.0f);
      q.pQueuePriorities = s->prios[i].data();
      q.queueCount += extra;
      return;
    }
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = family;
    q.queueCount = extra;
    s->prios[s->queues.size()].assign(extra, 0.0f);
    q.pQueuePriorities = s->prios[s->queues.size()].data();
    start = 0;
    s->queues.push_back(q);
  };
  add_to(s->gfx_family, g_req.gfx_queues, s->gfx_index);
  add_to(s->compute_family, g_req.compute_queues, s->compute_index);
  if (native_of) {
    bool already = false;
    for (auto& q : s->queues) already |= q.queueFamilyIndex == s->of_family;
    if (!already) {
      uint32_t of_start = 0;
      add_to(s->of_family, std::max(1u, g_req.of_queues), of_start);
    }
    g_have_of_queue = true;
  }
  out.queueCreateInfoCount = uint32_t(s->queues.size());
  out.pQueueCreateInfos = s->queues.data();
  std::string qs;
  for (auto& q : s->queues) qs += " family" + std::to_string(q.queueFamilyIndex) + " x" + std::to_string(q.queueCount);
  log("[sl] device queues:%s (Streamline: graphics %u@%u, compute %u@%u, optical flow %s family %u)", qs.c_str(),
      s->gfx_family, s->gfx_index, s->compute_family, s->compute_index, native_of ? "native" : "interop",
      s->of_family);
  return s;
}

// The application-level handle of the layer-level physical device: the loader
// wraps physical devices above the top layer, and NGX (initialised through
// vulkan-1's trampolines) needs that one.
static VkPhysicalDevice app_physical(DeviceData* dd) {
  if (cfg_int("sl_redirect_gipa", 0)) return dd->physical;
  VkInstance inst = dd->inst->instance;
  auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(loader_gipa()(inst, "vkEnumeratePhysicalDevices"));
  auto props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(loader_gipa()(inst, "vkGetPhysicalDeviceProperties"));
  if (!enumerate || !props) return dd->physical;
  uint32_t n = 0;
  enumerate(inst, &n, nullptr);
  std::vector<VkPhysicalDevice> pds(n);
  enumerate(inst, &n, pds.data());
  VkPhysicalDeviceProperties mine{};
  dd->inst->GetPhysicalDeviceProperties(dd->physical, &mine);
  for (VkPhysicalDevice pd : pds) {
    VkPhysicalDeviceProperties p{};
    props(pd, &p);
    if (p.deviceID == mine.deviceID && p.vendorID == mine.vendorID &&
        !memcmp(p.pipelineCacheUUID, mine.pipelineCacheUUID, VK_UUID_SIZE)) {
      log("[sl] application-level physical device %p (layer-level %p) '%s'", (void*)pd, (void*)dd->physical, p.deviceName);
      return pd;
    }
  }
  log("[sl] no application-level physical device matched; using the layer-level one");
  return dd->physical;
}

// slSetVulkanInfo must wait until vkCreateDevice has returned to the game:
// the loader fills the new device's trampoline dispatch table only after the
// layer chain returns, and NGX (inside slSetVulkanInfo) resolves its device
// functions through vulkan-1's vkGetDeviceProcAddr. Called from the first
// device-level call the game makes afterwards.
std::unique_ptr<SlDeviceScratch> g_pending;
std::mutex g_pending_mu;
static void finish_device(SlDeviceScratch* s, DeviceData* dd);

void sl_device_post(SlDeviceScratch* s, DeviceData* dd, VkResult r) {
  if (!s) return;
  std::unique_ptr<SlDeviceScratch> own(s);
  if (r != VK_SUCCESS) {
    log("[sl] vkCreateDevice failed (%d) with Streamline's requirements", r);
    return;
  }
  g_dd = dd;
  std::lock_guard<std::mutex> lock(g_pending_mu);
  g_pending = std::move(own);
}

void sl_device_ready(DeviceData* dd, bool at_swapchain) {
  if (!g_pending || dd != g_dd) return;
  // sl_init_at=1 (default): at the first swapchain creation, which BG3 does
  // after initialising its own NGX instance (for DLSS SR). 0: at the first
  // vkGetDeviceQueue.
  // Sharing the game's NGX needs Streamline's NGX to exist before the game's init.
  int at = cfg_int("sl_init_at", game_ngx_mode() == 2 ? 0 : 1);
  if (!at_swapchain && at != 0) return;
  std::unique_ptr<SlDeviceScratch> s;
  {
    std::lock_guard<std::mutex> lock(g_pending_mu);
    s = std::move(g_pending);
  }
  if (s) finish_device(s.get(), dd);
}

static void finish_device(SlDeviceScratch* s, DeviceData* dd) {
  sl::VulkanInfo vi{};
  vi.device = dd->device;
  vi.instance = dd->inst->instance;
  vi.physicalDevice = app_physical(dd);
  vi.graphicsQueueFamily = s->gfx_family;
  vi.graphicsQueueIndex = s->gfx_index;
  vi.computeQueueFamily = s->compute_family;
  vi.computeQueueIndex = s->compute_index;
  vi.useNativeOpticalFlowMode = g_have_of_queue;
  vi.opticalFlowQueueFamily = s->of_family == UINT32_MAX ? 0 : s->of_family;
  vi.opticalFlowQueueIndex = 0;
  sl::Result sr = g_sl.SetVulkanInfo(vi);
  log("[sl] slSetVulkanInfo: %s (interposer Vulkan lookups redirected below us: %u)", res_str(sr),
      g_gpa_redirects.load());
  if (sr != sl::Result::eOk) return;
  patch_modules();

  sl::AdapterInfo ai{};
  ai.vkPhysicalDevice = dd->physical;
  sl::Result sup = g_sl.IsFeatureSupported(sl::kFeatureDLSS_G, ai);
  log("[sl] DLSS-G supported on this adapter: %s", res_str(sup));
  if (sup != sl::Result::eOk) return;

  auto ff = [](sl::Feature f, const char* n) {
    void* p = nullptr;
    g_sl.GetFeatureFunction(f, n, p);
    if (!p) log("[sl] missing feature function %s", n);
    return p;
  };
  g_sl.DLSSGSetOptions = reinterpret_cast<PFun_slDLSSGSetOptions*>(ff(sl::kFeatureDLSS_G, "slDLSSGSetOptions"));
  g_sl.DLSSGGetState = reinterpret_cast<PFun_slDLSSGGetState*>(ff(sl::kFeatureDLSS_G, "slDLSSGGetState"));
  g_sl.ReflexSetOptions = reinterpret_cast<PFun_slReflexSetOptions*>(ff(sl::kFeatureReflex, "slReflexSetOptions"));
  g_sl.ReflexSleep = reinterpret_cast<PFun_slReflexSleep*>(ff(sl::kFeatureReflex, "slReflexSleep"));
  g_sl.PCLSetMarker = reinterpret_cast<PFun_slPCLSetMarker*>(ff(sl::kFeaturePCL, "slPCLSetMarker"));

  g_wait_semaphores = reinterpret_cast<PFN_vkWaitSemaphores>(dd->gdpa(dd->device, "vkWaitSemaphores"));
  if (!g_wait_semaphores)
    g_wait_semaphores = reinterpret_cast<PFN_vkWaitSemaphores>(dd->gdpa(dd->device, "vkWaitSemaphoresKHR"));
#define PX(n) g_px.n = reinterpret_cast<PFN_vk##n>(g_sl.gdpa(dd->device, "vk" #n))
  PX(CreateSwapchainKHR);
  PX(DestroySwapchainKHR);
  PX(GetSwapchainImagesKHR);
  PX(AcquireNextImageKHR);
  PX(QueuePresentKHR);
  PX(DeviceWaitIdle);
#undef PX
  if (!g_px.CreateSwapchainKHR || !g_px.QueuePresentKHR || !g_px.AcquireNextImageKHR || !g_px.GetSwapchainImagesKHR ||
      !g_px.DestroySwapchainKHR || !g_px.DeviceWaitIdle) {
    log("[sl] missing Streamline proxies");
    return;
  }
  g_reflex_applied = g_cap_applied = -1;
  reflex_apply();
  if (g_sl.DLSSGGetState) {
    sl::DLSSGState st{};
    sl::Result gr = g_sl.DLSSGGetState(0, st, nullptr);
    log("[sl] DLSS-G state: %s, max frames to generate %u, status 0x%x, min size %u, vsync support %d", res_str(gr),
        st.numFramesToGenerateMax, uint32_t(st.status), st.minWidthOrHeight, int(st.bIsVsyncSupportAvailable));
  }
  new_frame();
  g_active = true;
  log("[sl] Streamline active: swapchain and present go through its proxies");
}

void sl_device_destroy(DeviceData* dd) {
  if (dd != g_dd) return;
  g_active = false;
  if (g_depth_only_view) {
    dd->vk.DestroyImageView(dd->device, g_depth_only_view, nullptr);
    g_depth_only_view = VK_NULL_HANDLE;
  }
  if (g_sl.Shutdown) {
    sl::Result r = g_sl.Shutdown();
    log("[sl] slShutdown: %s", res_str(r));
  }
  bool have_copies = g_mv_copy_queries != VK_NULL_HANDLE;
  for (const auto& slot : g_mv_copies) have_copies |= slot.image.image != VK_NULL_HANDLE;
  if (have_copies) dd->vk.DeviceWaitIdle(dd->device);
  if (g_mv_copy_queries) dd->vk.DestroyQueryPool(dd->device, g_mv_copy_queries, nullptr);
  g_mv_copy_queries = VK_NULL_HANDLE;
  g_preserved_mvec = {};
  g_preserved_depth = {};
  g_mv_copy_ms.clear();
  for (auto& slot : g_mv_copies) {
    if (slot.image.view) dd->vk.DestroyImageView(dd->device, slot.image.view, nullptr);
    if (slot.image.image) dd->vk.DestroyImage(dd->device, slot.image.image, nullptr);
    if (slot.memory) dd->vk.FreeMemory(dd->device, slot.memory, nullptr);
    if (slot.depth.view) dd->vk.DestroyImageView(dd->device, slot.depth.view, nullptr);
    if (slot.depth.image) dd->vk.DestroyImage(dd->device, slot.depth.image, nullptr);
    if (slot.depth_memory) dd->vk.FreeMemory(dd->device, slot.depth_memory, nullptr);
    slot = {};
  }
  g_sl = {};
  g_px = {};
  g_dd = nullptr;
}

// ---------------------------------------------------------------------------
// swapchain entry points

bool sl_create_swapchain(VkDevice d, const VkSwapchainCreateInfoKHR* ci, const VkAllocationCallbacks* a,
                         VkSwapchainKHR* out, VkResult* r) {
  if (g_dd && d == g_dd->device) sl_device_ready(g_dd, true);
  if (g_active && g_dd && d == g_dd->device) register_surface(g_dd->inst->instance, ci->surface);
  if (!g_active || !g_dd || d != g_dd->device) return false;
  g_sc_extent = ci->imageExtent;
  g_sc_format = ci->imageFormat;
  InSl guard;
  *r = g_px.CreateSwapchainKHR(d, ci, a, out);
  log("[sl] swapchain created through Streamline: %d", *r);
  return true;
}
bool sl_destroy_swapchain(VkDevice d, VkSwapchainKHR sc, const VkAllocationCallbacks* a) {
  if (!g_active || !g_dd || d != g_dd->device) return false;
  InSl guard;
  g_px.DestroySwapchainKHR(d, sc, a);
  return true;
}
bool sl_get_swapchain_images(VkDevice d, VkSwapchainKHR sc, uint32_t* n, VkImage* imgs, VkResult* r) {
  if (!g_active || !g_dd || d != g_dd->device) return false;
  InSl guard;
  *r = g_px.GetSwapchainImagesKHR(d, sc, n, imgs);
  return true;
}
bool sl_acquire(VkDevice d, VkSwapchainKHR sc, uint64_t timeout, VkSemaphore s, VkFence f, uint32_t* idx,
                VkResult* r) {
  if (!g_active || !g_dd || d != g_dd->device) return false;
  InSl guard;
  *r = g_px.AcquireNextImageKHR(d, sc, timeout, s, f, idx);
  return true;
}
bool sl_device_wait_idle(VkDevice d, VkResult* r) {
  // NGX and Streamline's own calls come back through the chain: go straight down.
  if (!g_active || !g_dd || d != g_dd->device || t_in_sl) return false;
  InSl guard;
  *r = g_px.DeviceWaitIdle(d);
  return true;
}

// ---------------------------------------------------------------------------
// resource discovery (called from the command buffer hooks)

bool sl_watched(VkImage image) {
  if (!image) return false;
  for (auto& w : g_watch)
    if (w.load(std::memory_order_relaxed) == image) return true;
  return false;
}

static void note_input_layout(CmdState* s, VkImage image, VkImageLayout layout) {
  for (auto& p : s->sl_input_layouts)
    if (p.first == image) { p.second = layout; return; }
  s->sl_input_layouts.emplace_back(image, layout);
}

void sl_note_barrier(CmdState* s, VkImage image, VkImageLayout layout) {
  if (!sl_watched(image)) return;
  if (cfg_int("fg_input_copy", 0) || cfg_int("pan_inputs", 0)) note_input_layout(s, image, layout);
  for (auto& p : s->sl_layouts)
    if (p.first == image) {
      p.second = layout;
      return;
    }
  s->sl_layouts.emplace_back(image, layout);
}

void sl_note_event_barrier(CmdState* s, VkImage image, VkImageLayout layout) {
  if (!sl_watched(image)) return;
  if (cfg_int("fg_input_copy", 0)) sl_note_barrier(s, image, layout);
  else note_input_layout(s, image, layout);
}


// ---- fg_ui_pq=1: a UI tag consistent with the game's HDR10 composite (shaders/fg/fg_ui_pq.hlsl) ----
// Our own persistent RGBA16F image: ui' = final - hudless * (1 - a), alpha a. Recorded at the end
// of the UI composite pass, in the same command buffer, from the views that pass sampled.
struct UiPqPending {
  VkImage final_img; VkImageView final_view, hud_view, ui_view; VkImage hud_img, ui_img; uint32_t w, h;
};
std::unordered_map<VkCommandBuffer, UiPqPending> g_uipq_pending;   // g_mu
struct UiPq {
  VkImage img = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE;
  uint32_t w = 0, h = 0;
  VkDescriptorSetLayout dsl = VK_NULL_HANDLE; VkPipelineLayout pl = VK_NULL_HANDLE; VkPipeline pipe = VK_NULL_HANDLE;
  uint64_t gen = 0; VkDescriptorPool pool = VK_NULL_HANDLE; VkDescriptorSet sets[8] = {}; uint32_t next = 0;
  bool failed = false; bool layout_known = false;
} g_uipq;   // under g_uipq_mu
std::mutex g_uipq_mu;

bool uipq_ensure(uint32_t w, uint32_t h) {
  DeviceData* dd = g_dd;
  if (!dd || g_uipq.failed) return false;
  const VkDeviceFuncs& vk = dd->vk;
  if (!g_uipq.dsl) {
    VkDescriptorSetLayoutBinding b[4] = {};
    for (uint32_t i = 0; i < 4; ++i) {
      b[i].binding = i; b[i].descriptorCount = 1; b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
      b[i].descriptorType = i < 3 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dci.bindingCount = 4; dci.pBindings = b;
    bool ok = vk.CreateDescriptorSetLayout(dd->device, &dci, nullptr, &g_uipq.dsl) == VK_SUCCESS;
    VkPipelineLayoutCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pci.setLayoutCount = 1; pci.pSetLayouts = &g_uipq.dsl;
    ok = ok && vk.CreatePipelineLayout(dd->device, &pci, nullptr, &g_uipq.pl) == VK_SUCCESS;
    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 3 * 8}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8}};
    VkDescriptorPoolCreateInfo ppi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    ppi.maxSets = 8; ppi.poolSizeCount = 2; ppi.pPoolSizes = sizes;
    ok = ok && vk.CreateDescriptorPool(dd->device, &ppi, nullptr, &g_uipq.pool) == VK_SUCCESS;
    if (ok) {
      VkDescriptorSetLayout ls[8];
      for (auto& l : ls) l = g_uipq.dsl;
      VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      ai.descriptorPool = g_uipq.pool; ai.descriptorSetCount = 8; ai.pSetLayouts = ls;
      ok = vk.AllocateDescriptorSets(dd->device, &ai, g_uipq.sets) == VK_SUCCESS;
    }
    if (!ok) {
      g_uipq.failed = true;
      log("[sl] fg_ui_pq: descriptor setup failed (off)");
      return false;
    }
  }
  Bg3ShaderBlob blob{};
  if (shader_get("fg/fg_ui_pq.hlsl", "main", "cs_6_0", &blob) && (!g_uipq.pipe || blob.generation != g_uipq.gen)) {
    VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mci.codeSize = blob.size_bytes; mci.pCode = blob.code;
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vk.CreateShaderModule(dd->device, &mci, nullptr, &mod) == VK_SUCCESS) {
      VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
      ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main"};
      ci.layout = g_uipq.pl;
      VkPipeline np = VK_NULL_HANDLE;
      if (vk.CreateComputePipelines(dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &np) == VK_SUCCESS) {
        if (g_uipq.pipe) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_uipq.pipe));
        g_uipq.pipe = np; g_uipq.gen = blob.generation;
        log("[sl] fg_ui_pq: pipeline built (generation %llu)", (unsigned long long)blob.generation);
      }
      vk.DestroyShaderModule(dd->device, mod, nullptr);
    }
  }
  if (!g_uipq.pipe) return false;
  if (g_uipq.img && g_uipq.w == w && g_uipq.h == h) return true;
  if (g_uipq.view) defer_destroy(BG3_DEFER_IMAGE_VIEW, uint64_t(g_uipq.view));
  if (g_uipq.img) defer_destroy(BG3_DEFER_IMAGE, uint64_t(g_uipq.img));
  if (g_uipq.mem) defer_destroy(BG3_DEFER_MEMORY, uint64_t(g_uipq.mem));
  g_uipq.img = VK_NULL_HANDLE; g_uipq.view = VK_NULL_HANDLE; g_uipq.mem = VK_NULL_HANDLE;
  g_uipq.layout_known = false;
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D; ici.format = VK_FORMAT_R16G16B16A16_SFLOAT; ici.extent = {w, h, 1};
  ici.mipLevels = 1; ici.arrayLayers = 1; ici.samples = VK_SAMPLE_COUNT_1_BIT; ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (vk.CreateImage(dd->device, &ici, nullptr, &g_uipq.img) != VK_SUCCESS) { g_uipq.failed = true; return false; }
  VkMemoryRequirements req{};
  vk.GetImageMemoryRequirements(dd->device, g_uipq.img, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || vk.AllocateMemory(dd->device, &mai, nullptr, &g_uipq.mem) != VK_SUCCESS ||
      vk.BindImageMemory(dd->device, g_uipq.img, g_uipq.mem, 0) != VK_SUCCESS) {
    g_uipq.failed = true;
    return false;
  }
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = g_uipq.img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = ici.format;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (vk.CreateImageView(dd->device, &vi, nullptr, &g_uipq.view) != VK_SUCCESS) { g_uipq.failed = true; return false; }
  g_uipq.w = w; g_uipq.h = h;
  log("[sl] fg_ui_pq: UI tag image %ux%u RGBA16F", w, h);
  return true;
}

// Records the UI tag image after the composite pass; true if it was written.
bool uipq_record(VkCommandBuffer cmd, const UiPqPending& p, VkImageLayout final_layout) {
  std::lock_guard<std::mutex> lock(g_uipq_mu);
  if (final_layout == VK_IMAGE_LAYOUT_UNDEFINED || !uipq_ensure(p.w, p.h)) return false;
  const VkDeviceFuncs& vk = g_dd->vk;
  VkDescriptorSet set = g_uipq.sets[g_uipq.next++ % 8];
  VkDescriptorImageInfo ii[4] = {{VK_NULL_HANDLE, p.final_view, VK_IMAGE_LAYOUT_GENERAL},
                                 {VK_NULL_HANDLE, p.hud_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                 {VK_NULL_HANDLE, p.ui_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                 {VK_NULL_HANDLE, g_uipq.view, VK_IMAGE_LAYOUT_GENERAL}};
  VkWriteDescriptorSet wr[4] = {};
  for (uint32_t i = 0; i < 4; ++i) {
    wr[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr[i].dstSet = set; wr[i].dstBinding = i; wr[i].descriptorCount = 1; wr[i].pImageInfo = &ii[i];
    wr[i].descriptorType = i < 3 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  }
  vk.UpdateDescriptorSets(g_dd->device, 4, wr, 0, nullptr);
  VkImageMemoryBarrier b[2] = {};
  for (auto& x : b) {
    x = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    x.srcQueueFamilyIndex = x.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  }
  b[0].image = p.final_img; b[0].oldLayout = final_layout; b[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
  b[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; b[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  b[1].image = g_uipq.img;
  b[1].oldLayout = g_uipq.layout_known ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
  b[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
  b[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; b[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                        nullptr, 2, b);
  g_uipq.layout_known = true;
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_uipq.pipe);
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_uipq.pl, 0, 1, &set, 0, nullptr);
  vk.CmdDispatch(cmd, (p.w + 7) / 8, (p.h + 7) / 8, 1);
  b[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL; b[0].newLayout = final_layout;
  b[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; b[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  b[1].oldLayout = b[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
  b[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; b[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                        nullptr, 2, b);
  return true;
}

void sl_note_draw(VkCommandBuffer cmd, CmdState* s) {
  (void)cmd;
  if (!g_active) return;
  const Bg3PassInfo& p = s->pass;
  if (p.first_fs_hash == kVelocityFs && p.color_count >= 1 && p.depth_image) {
    std::string e1, e2;
    bool a = read_ubo(s, 0, uint32_t(cfg_int("fg_cam_binding", 2)), s->sl_cam[0], 224, e1);
    bool b = read_ubo(s, 0, uint32_t(cfg_int("fg_prev_binding", 5)), s->sl_cam[1], 224, e2);
    s->sl_cam_valid = a && b;
    if (!s->sl_cam_valid) {
      std::lock_guard<std::mutex> lock(g_mu);
      std::string err = "b2: " + (a ? std::string("ok") : e1) + ", b5: " + (b ? std::string("ok") : e2);
      if (err != g_cam_err) {
        g_cam_err = err;
        log("[sl] camera uniforms not readable (%s)", err.c_str());
      }
    }
  } else if (p.color_count >= 1 && p.color_images[0]) {
    // UI composite: the pass that draws into a swapchain image and samples two
    // output-sized images, the scene (hudless) and the UI with alpha (RGBA8).
    // HDR10 output uses fs 046c6949 with set 1 bindings 1 and 2; SDR output
    // uses a different shader, so the images are classified, not the shader.
    bool to_swapchain = false;
    {
      std::shared_lock<std::shared_mutex> lock(tracker().mu);
      auto it = tracker().images.find(p.color_images[0]);
      to_swapchain = it != tracker().images.end() && it->second.swapchain;
    }
    if (!to_swapchain) return;
    VkDescriptorSet ds = s->sets[0][1];
    VkImage i_h = VK_NULL_HANDLE, i_u = VK_NULL_HANDLE;
    VkImageView v_h = VK_NULL_HANDLE, v_u = VK_NULL_HANDLE;
    fg::Extent out_ext{};
    {
      std::shared_lock<std::shared_mutex> lock(tracker().mu);
      auto si = tracker().sets.find(ds);
      if (si != tracker().sets.end()) {
        // The game's output extent: the pass extent, or (fg_letterbox=1) the largest sampled image
        // inside a larger swapchain pass (borderless / fake fullscreen on a bigger desktop).
        std::vector<fg::Extent> sampled;
        for (auto& [key, e] : si->second) {
          if (!e.view) continue;
          auto vi = tracker().views.find(e.view);
          if (vi == tracker().views.end()) continue;
          auto ii = tracker().images.find(vi->second.image);
          if (ii != tracker().images.end()) sampled.push_back({ii->second.ci.extent.width, ii->second.ci.extent.height});
        }
        const int letterbox_key = cfg_int("fg_letterbox", 1) != 0 ? 1 : 0;
        static std::atomic<int> letterbox_logged{-1};   // activation line on every change (lean jobs expect it)
        if (letterbox_logged.exchange(letterbox_key) != letterbox_key)
          log("[sl] fg_letterbox=%d: UI composite pass %ux%u", letterbox_key, p.width, p.height);
        out_ext = fg::output_extent(sampled.data(), uint32_t(sampled.size()), {p.width, p.height}, letterbox_key != 0);
        for (auto& [key, e] : si->second) {
          if (!e.view) continue;
          auto vi = tracker().views.find(e.view);
          if (vi == tracker().views.end()) continue;
          auto ii = tracker().images.find(vi->second.image);
          if (ii == tracker().images.end()) continue;
          const auto& ci = ii->second.ci;
          if (!out_ext.w || ci.extent.width != out_ext.w || ci.extent.height != out_ext.h) continue;
          bool rgba8 = ci.format == VK_FORMAT_R8G8B8A8_UNORM || ci.format == VK_FORMAT_B8G8R8A8_UNORM ||
                       ci.format == VK_FORMAT_R8G8B8A8_SRGB || ci.format == VK_FORMAT_B8G8R8A8_SRGB;
          if (rgba8 && !i_u) {
            i_u = vi->second.image;
            v_u = e.view;
          } else if (!rgba8 && !i_h) {
            i_h = vi->second.image;
            v_h = e.view;
          }
        }
      }
    }
    std::lock_guard<std::mutex> lock(g_mu);
    const bool letterboxed = out_ext.w && (out_ext.w != p.width || out_ext.h != p.height);
    if (i_h) {
      const fg::Rect r = letterboxed ? fg::backbuffer_rect(out_ext, {p.width, p.height}, s->has_viewport, s->viewport.x,
                                                           s->viewport.y, s->viewport.width, s->viewport.height)
                                     : fg::Rect{};
      if (letterboxed && (r.x != g_bb_rect.x || r.y != g_bb_rect.y || r.w != g_bb_rect.w || r.h != g_bb_rect.h))
        log("[sl] game output %ux%u in a %ux%u swapchain pass (fg_letterbox): backbuffer subrect %d,%d %ux%u", out_ext.w,
            out_ext.h, p.width, p.height, r.x, r.y, r.w, r.h);
      g_bb_rect = r;
    }
    if (i_h && i_h != g_hudless.image)
      log("[sl] hudless = image %p (composite fs %016llx)", (void*)i_h, (unsigned long long)p.first_fs_hash);
    if (i_u && i_u != g_ui.image)
      log("[sl] UI color+alpha = image %p (composite fs %016llx)", (void*)i_u, (unsigned long long)p.first_fs_hash);
    auto remember = [&](Bg3Binding& b, VkImage image, VkImageView view) {
      Img im{}; fill_img(im, image, view);
      b.image = im.image; b.view = im.view; b.format = im.format; b.width = im.w; b.height = im.h;
    };
    if (i_h) remember(s->sl_hudless, i_h, v_h);
    if (i_u) remember(s->sl_ui, i_u, v_u);
    if (!cfg_int("fg_input_copy", 0)) {
      if (i_h) fill_img(g_hudless, i_h, v_h);
      if (i_u) fill_img(g_ui, i_u, v_u);
    }
    // The PQ UI record writes the pass extent: only when the output is the pass size.
    if (i_h && i_u && !letterboxed && p.color_views[0] && (cfg_int("fg_ui_pq", 0) || cfg_int("pan_inputs", 0)))
      g_uipq_pending[cmd] = UiPqPending{p.color_images[0], p.color_views[0], v_h, v_u, i_h, i_u, p.width, p.height};
    set_watch();
  }
}

void sl_note_pass_end(VkCommandBuffer cmd, CmdState* s) {
  (void)cmd;
  if (!g_active) return;
  const Bg3PassInfo& p = s->pass;
  {
    UiPqPending pend{};
    bool have = false;
    {
      std::lock_guard<std::mutex> lock(g_mu);
      auto it = g_uipq_pending.find(cmd);
      if (it != g_uipq_pending.end()) {
        pend = it->second;
        have = true;
        g_uipq_pending.erase(it);
      }
    }
    if (have && cfg_int("fg_ui_pq", 0) && uipq_record(cmd, pend, s->color_layouts[0])) {
      std::lock_guard<std::mutex> lock(g_mu);
      if (!cfg_int("fg_input_copy", 0))
        g_ui = {g_uipq.img, g_uipq.view, VK_FORMAT_R16G16B16A16_SFLOAT, g_uipq.w, g_uipq.h,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, frame_index()};
      g_layout[g_uipq.img] = VK_IMAGE_LAYOUT_GENERAL;
      s->sl_ui = {};
      s->sl_ui.image = g_uipq.img; s->sl_ui.view = g_uipq.view; s->sl_ui.format = VK_FORMAT_R16G16B16A16_SFLOAT;
      s->sl_ui.width = g_uipq.w; s->sl_ui.height = g_uipq.h;
    }
    if (have && cfg_int("pan_inputs", 0) && cfg_int("pan_capture", 0) > 0) {
      Img inputs[4];
      VkImageLayout layouts[4];
      {
        std::lock_guard<std::mutex> lock(g_mu);
        inputs[0] = g_hudless; inputs[1] = g_ui;
        fill_img(inputs[0], pend.hud_img, pend.hud_view);
        if (cfg_int("fg_ui_pq", 0))
          inputs[1] = {g_uipq.img,g_uipq.view,VK_FORMAT_R16G16B16A16_SFLOAT,g_uipq.w,g_uipq.h,
                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,frame_index()};
        else fill_img(inputs[1], pend.ui_img, pend.ui_view);
        inputs[2] = g_depth; inputs[3] = g_mvec;
        for (unsigned i = 0; i < 4; ++i) {
          layouts[i] = layout_of(inputs[i].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
          auto observed = g_input_layout.find(inputs[i].image);
          if (observed != g_input_layout.end()) layouts[i] = observed->second;
          for (auto& [image, layout] : s->sl_input_layouts)
            if (image == inputs[i].image) layouts[i] = layout;
        }
      }
      const char* names[] = {"fg_hudless", "fg_ui", "fg_depth", "fg_mvec"};
      for (unsigned i = 0; i < 4; ++i) {
        Bg3Binding b{};
        b.image = inputs[i].image; b.view = inputs[i].view; b.format = inputs[i].format;
        b.width = inputs[i].w; b.height = inputs[i].h; b.layout = layouts[i];
        pan_capture(cmd, names[i], &b);
      }
      Bg3Binding b{};
      b.image = pend.final_img; b.view = pend.final_view;
      b.width = pend.w; b.height = pend.h; b.format = p.color_formats[0]; b.layout = s->color_layouts[0];
      if (b.layout != VK_IMAGE_LAYOUT_UNDEFINED) pan_capture(cmd, "fg_final", &b);
    }
  }
  if (p.first_fs_hash == kVelocityFs && p.color_count >= 1 && p.depth_image) {
    std::lock_guard<std::mutex> lock(g_mu);
    static int logged = 0;   // the game rotates several motion-vector images
    if (p.color_images[0] != g_mvec.image && logged++ < 6)
      log("[sl] motion vectors = image %p fmt %d, depth = image %p fmt %d (%ux%u velocity pass)",
          (void*)p.color_images[0], p.color_formats[0], (void*)p.depth_image, p.depth_format, p.width, p.height);
    fill_img(g_mvec, p.color_images[0], p.color_views[0]);
    fill_img(g_depth, p.depth_image, p.depth_view);
    set_watch();
  } else if (p.first_fs_hash == kTonemapFs && p.color_count == 1) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_hudless.image || frame_index() > g_hudless.seen + 120) {
      fill_img(g_hudless, p.color_images[0], p.color_views[0]);
      set_watch();
    }
  }
  // final layouts of watched attachments
  for (uint32_t i = 0; i < p.color_count; ++i)
    if (s->color_layouts[i] != VK_IMAGE_LAYOUT_UNDEFINED) sl_note_barrier(s, p.color_images[i], s->color_layouts[i]);
  if (p.depth_image && s->depth_layout != VK_IMAGE_LAYOUT_UNDEFINED) sl_note_barrier(s, p.depth_image, s->depth_layout);
}

void sl_note_submit(const std::vector<VkCommandBuffer>& cmds) {
  if (!g_active) return;
  std::lock_guard<std::mutex> lock(g_mu);
  for (VkCommandBuffer c : cmds) {
    CmdState* s = cmd_state(c);
    if (!s) continue;
    for (auto& [img, lay] : s->sl_layouts) g_layout[img] = lay;
    for (auto& [img, lay] : s->sl_input_layouts) g_input_layout[img] = lay;
    if (s->sl_mvec_copy.image) {
      g_copy_record = s->recording;
      {
        std::lock_guard<std::mutex> copy_lock(g_mv_copy_mu);
        if (s->sl_mvec_slot < 8) g_mv_copies[s->sl_mvec_slot].submitted = frame_index();
      }
      const auto& b = s->sl_mvec_copy;
      if (cfg_int("fg_input_copy", 0)) g_preserved_mvec = {b.image, b.view, b.format, b.width, b.height,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          frame_index()};
      const auto& d = s->sl_depth_copy;
      if (cfg_int("fg_input_copy", 0)) g_preserved_depth = {d.image, d.view, d.format, d.width, d.height,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          frame_index()};
    }
    if (s->sl_cam_valid) {
      memcpy(g_cam.cam, s->sl_cam[0], sizeof g_cam.cam);
      memcpy(g_cam.prev, s->sl_cam[1], sizeof g_cam.prev);
      g_cam_valid = true;
      g_cam_frame = frame_index();
    }
    if (s->sl_hudless.image) {
      g_color_record = s->recording; g_color_submit = frame_index();
      if (cfg_int("fg_input_copy", 0)) {
        const auto& h = s->sl_hudless; const auto& u = s->sl_ui;
        fill_img(g_hudless, h.image, h.view);
        if (u.image && u.image == g_uipq.img) {
          g_ui = {u.image, u.view, u.format, u.width, u.height,
                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, frame_index()};
        } else if (u.image) fill_img(g_ui, u.image, u.view);
        set_watch();
      }
    }
  }
}

// Streamline does not keep tagged Vulkan images (or the views it was given)
// alive: wait until DLSS-G has consumed the last presented frame's inputs (its
// documented fence), so its queued work never touches a destroyed object.
static void wait_inputs_consumed(const char* what, uint64_t handle) {
  VkSemaphore fence;
  uint64_t value;
  {
    std::lock_guard<std::mutex> lock(g_fence_mu);
    fence = g_inputs_fence;
    value = g_inputs_value;
  }
  if (!g_fg_running || !fence || !value || !g_wait_semaphores || !g_dd || t_in_sl) return;
  VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
  wi.semaphoreCount = 1;
  wi.pSemaphores = &fence;
  wi.pValues = &value;
  VkResult r = g_wait_semaphores(g_dd->device, &wi, 1000000000ull);
  log("[sl] tagged %s %p destroyed while frame generation runs: waited for DLSS-G inputs fence value %llu (%d)", what,
      (void*)handle, (unsigned long long)value, r);
}

// The game usually destroys a view before its image; the tag hands DLSS-G
// that view, so the wait must happen here already.
void sl_note_view_destroyed(VkImageView view) {
  if (!g_active || !view) return;
  bool current = false;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    for (Img* i : {&g_depth, &g_mvec, &g_hudless, &g_ui}) current |= i->view == view;
  }
  bool retired = false;
  if (!current && cfg_int("fg_current_destroy_only",1)) {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    auto it=tracker().views.find(view);
    retired=it!=tracker().views.end() && sl_watched(it->second.image);
  }
  // A rotated-out tag can still belong to the last presented GPU frame.
  // Ignoring its stability reset must never skip the consumption fence.
  if (current || retired) wait_inputs_consumed("view", (uint64_t)view);
}

void sl_note_image_destroyed(VkImage image) {
  if (!g_active || !sl_watched(image)) return;
  uint32_t current_roles = 0;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    const Img* inputs[] = {&g_depth, &g_mvec, &g_hudless, &g_ui};
    for (uint32_t i=0; i<4; ++i)
      if (inputs[i]->image == image) current_roles |= 1u << i;
  }
  // Legacy policy treats every historical watched image as a target reset.
  // The keyed policy only invalidates current inputs; retiring a previous
  // rotation of the same target does not invalidate its live replacement.
  const uint64_t frame = frame_index();
  const bool current_only = cfg_int("fg_current_destroy_only",1) != 0;
  const bool reset = g_fg_stability.destroyed(frame,current_roles,current_only);
  if (current_roles || current_only) wait_inputs_consumed("image", (uint64_t)image);
  std::lock_guard<std::mutex> lock(g_mu);
  g_last_destroy_event = {frame,image,current_roles,reset};
  for (Img* i : {&g_depth, &g_mvec, &g_hudless, &g_ui})
    if (i->image == image) *i = Img{};
  g_layout.erase(image);
  g_input_layout.erase(image);
  for (auto& w : g_watch)
    if (w.load() == image) w = VK_NULL_HANDLE;
  log("[sl] a tagged image was destroyed; image %p frame %llu current roles 0x%x "
      "(depth=1 MV=2 hudless=4 UI=8), stability reset %u, current-only %u",
      (void*)image,(unsigned long long)frame,current_roles,unsigned(reset),unsigned(current_only));
}

// ---------------------------------------------------------------------------
// present

VkResult sl_present(DeviceData* dd, VkQueue queue, const VkPresentInfoKHR* info) {
  if (!g_active || dd != g_dd) {
    diag(DIAG_DISPLAYED);
    return dd->vk.QueuePresentKHR(queue, info);
  }
  uint64_t frame = frame_index();
  int fg = cfg_int("fg", 2);
  uint32_t gen = fg >= 2 ? uint32_t(std::min(fg, 4) - 1) : 0;

  bool ready = false;
  Img input_snapshot[4]{};
  CamRaw camera_snapshot{};
  bool camera_snapshot_valid = false, use_camera_snapshot = false;
  TagDestroyEvent destroy_snapshot{};
  VkImageLayout input_layouts[4]{};
  sl::Result tag_result = sl::Result::eOk, constants_result = sl::Result::eOk;
  bool tagged = false;
  if (g_token) {
    // Snapshot under the lock; Streamline is called without it (it may wait on
    // GPU work that the game's recording threads, which take g_mu, still have to submit).
    Img d, m, h, u;
    VkImageView dview = VK_NULL_HANDLE;
    VkImageLayout ld{}, lm{}, lh{}, lu{};
    {
      std::lock_guard<std::mutex> lock(g_mu);
      input_snapshot[0]=g_depth; input_snapshot[1]=g_mvec;
      const bool copied = cfg_int("fg_input_copy", 0) && g_preserved_mvec.image && g_preserved_depth.image &&
                          fg_copy_pair_for_present(g_preserved_mvec.seen, g_cam_frame, g_color_submit, frame) &&
                          g_preserved_depth.seen == g_preserved_mvec.seen;
      if (copied) { input_snapshot[1] = g_preserved_mvec; input_snapshot[0] = g_preserved_depth; }
      if (cfg_int("fg_input_copy", 0) && frame % 30 == 0)
        log("[sl] input copy frame %llu: selected %u, submit age %llu, image %p",
            (unsigned long long)frame, unsigned(copied), (unsigned long long)(frame - g_preserved_mvec.seen),
            (void*)input_snapshot[1].image);
      if (cfg_int("pan_inputs", 0) && frame % 30 == 0)
        log("[sl] pair stamp present %llu token %u: copy submit %llu record %llu, camera submit %llu, color submit %llu record %llu, hud seen %llu UI seen %llu",
            (unsigned long long)frame, uint32_t(*g_token), (unsigned long long)g_preserved_mvec.seen,
            (unsigned long long)g_copy_record, (unsigned long long)g_cam_frame,
            (unsigned long long)g_color_submit, (unsigned long long)g_color_record,
            (unsigned long long)g_hudless.seen, (unsigned long long)g_ui.seen);
      if (cfg_int("pan_inputs", 0) && cfg_int("fg_snap", 0) > 0 && frame % 2 == 0)
        log("[sl] pair camera present %llu: forward %.8g %.8g %.8g",
            (unsigned long long)frame,g_cam.cam[35],g_cam.cam[39],g_cam.cam[43]);
      use_camera_snapshot = cfg_int("fg_input_copy", 0) != 0;
      if (use_camera_snapshot) { camera_snapshot = g_cam; camera_snapshot_valid = g_cam_valid; }
      input_snapshot[2]=g_hudless; input_snapshot[3]=g_ui;
      destroy_snapshot=g_last_destroy_event;
      // everything identified within the last couple of frames (menus have no velocity pass)
      ready = g_depth.image && g_mvec.image && g_hudless.image && g_ui.image && frame <= g_mvec.seen + 3 &&
              frame <= g_ui.seen + 3;      if (ready) {
        if (!copied) ensure_depth_view();
        d = g_depth, m = g_mvec, h = g_hudless, u = g_ui;
        if (copied) { m = g_preserved_mvec; d = g_preserved_depth; }
        dview = copied ? g_preserved_depth.view : g_depth_only_view;
        ld = copied ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : layout_of(d.image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        lm = layout_of(m.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        lh = layout_of(h.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        lu = layout_of(u.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        input_layouts[0]=ld; input_layouts[1]=lm; input_layouts[2]=lh; input_layouts[3]=lu;
        if (cfg_int("pan_inputs", 0) && frame % 30 == 0) {
          auto observed = [&](VkImage image, VkImageLayout fallback) {
            auto it = g_input_layout.find(image);
            return it == g_input_layout.end() ? fallback : it->second;
          };
          log("[sl] input layouts frame %llu: copy %u depth tag %d observed %d, MV tag %d observed %d",
              (unsigned long long)frame, unsigned(copied), int(ld), int(observed(d.image,ld)),
              int(lm), int(observed(m.image,lm)));
        }
      }
    }
    if (ready) {
      const Img &g_depth = d, &g_mvec = m, &g_hudless = h, &g_ui = u;
      sl::Resource depth = make_res(g_depth, dview, ld);
      sl::Resource mvec = make_res(g_mvec, g_mvec.view, lm);
      sl::Resource hud = make_res(g_hudless, g_hudless.view, lh);
      sl::Resource ui = make_res(g_ui, g_ui.view, lu);
      sl::Extent rx{0, 0, g_mvec.w, g_mvec.h};
      sl::Extent dx{0, 0, g_depth.w, g_depth.h};
      sl::Extent ox{0, 0, g_hudless.w, g_hudless.h};
      sl::Extent ux{0, 0, g_ui.w, g_ui.h};
      // fg_letterbox: the game's output region of the larger swapchain; else the hudless extent at 0,0.
      sl::Extent bx = g_bb_rect.w ? sl::Extent{uint32_t(g_bb_rect.y), uint32_t(g_bb_rect.x), g_bb_rect.w, g_bb_rect.h} : ox;
      auto life = sl::ResourceLifecycle(cfg_int("fg_lifecycle", int(sl::ResourceLifecycle::eValidUntilPresent)));
      std::vector<sl::ResourceTag> tags = {
          sl::ResourceTag(&depth, sl::kBufferTypeDepth, life, &dx),
          sl::ResourceTag(&mvec, sl::kBufferTypeMotionVectors, life, &rx),
      };
      // Omission can retain a previously tagged input. Live A/B must explicitly
      // remove the disabled tag (SL's resource-cache contract).
      tags.emplace_back(cfg_int("fg_tag_hudless", 1) ? &hud : nullptr, sl::kBufferTypeHUDLessColor, life, &ox);
      tags.emplace_back(cfg_int("fg_tag_ui", 1) ? &ui : nullptr, sl::kBufferTypeUIColorAndAlpha, life, &ux);
      if (cfg_int("fg_tag_backbuffer", 1))
        tags.emplace_back(nullptr, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle::eValidUntilPresent, &bx);
      sl::Result tr = g_sl.SetTagForFrame(*g_token, 0, tags.data(), uint32_t(tags.size()), nullptr);
      sl::Constants c{};
      fill_constants(c, g_mvec.w, g_mvec.h, use_camera_snapshot ? &camera_snapshot : nullptr, camera_snapshot_valid);
      sl::Result cr = g_sl.SetConstants(c, *g_token, 0);
      tag_result=tr; constants_result=cr; tagged=true;
      static uint32_t logged = 0;
      if (((tr != sl::Result::eOk || cr != sl::Result::eOk) && logged < 20) || frame % 600 == 0) {
        ++logged;
        log("[sl] frame %u: tags %s, constants %s | layouts depth %d mvec %d hudless %d ui %d", uint32_t(*g_token),
            res_str(tr), res_str(cr), int(depth.state), int(mvec.state), int(hud.state), int(ui.state));
      }
    }
  }

  // DLSS-G options: on only while every input is valid this frame
  uint32_t flags = uint32_t(sl::DLSSGFlags::eRetainResourcesWhenOff);
  if (cfg_int("fg_only_interpolated", 0)) flags |= uint32_t(sl::DLSSGFlags::eShowOnlyInterpolatedFrame);
  if (cfg_int("fg_menu_detect", 1)) flags |= uint32_t(sl::DLSSGFlags::eEnableFullscreenMenuDetection);
  // Frame generation only once the inputs have been stable for a while.
  uint64_t stable_n = uint64_t(cfg_int("fg_stable_frames", 30));
  bool stable = g_fg_stability.update(frame,ready,stable_n);
  int want = stable ? fg : 0;
  if (g_sl.DLSSGSetOptions && (want != g_fg_applied || flags != g_flags_applied)) {
    uint32_t missing=0, stale=0;
    for (uint32_t i=0; i<4; ++i) {
      if (!input_snapshot[i].image) missing |= 1u << i;
      if ((i==1 || i==3) && frame>input_snapshot[i].seen+3) stale |= 1u << i;
    }
    const char* reason = fg<2 ? "requested-off" : !g_token ? "no-token" : !ready ? "missing-or-stale-input" :
                         !stable ? "stability-cooldown" : flags!=g_flags_applied && want==g_fg_applied ? "flags-only" : "stable-inputs";
    log("[sl] FG decision frame %llu: applied %d -> want %d requested %d; reason %s; ready %u stable %u "
        "missing 0x%x stale 0x%x (depth=1 MV=2 hudless=4 UI=8); tag API %s constants %s; "
        "last destroy image %p frame %llu current roles 0x%x reset %u; cooldown destroy frame %llu; current-only %d",
        (unsigned long long)frame,g_fg_applied,want,fg,reason,unsigned(ready),unsigned(stable),missing,stale,
        tagged ? res_str(tag_result) : "not-called",tagged ? res_str(constants_result) : "not-called",
        (void*)destroy_snapshot.image,(unsigned long long)destroy_snapshot.frame,destroy_snapshot.current_roles,
        unsigned(destroy_snapshot.reset),(unsigned long long)g_fg_stability.last_destroy.load(),cfg_int("fg_current_destroy_only",1));
    const char* names[] = {"depth","MV","hudless","UI"};
    for (uint32_t i=0; i<4; ++i) {
      const Img& input=input_snapshot[i];
      log("[sl] FG input %s: image %p view %p extent %ux%u age %llu layout %d",
          names[i],(void*)input.image,(void*)input.view,input.w,input.h,
          (unsigned long long)(frame>=input.seen ? frame-input.seen : 0),int(input_layouts[i]));
    }
    sl::DLSSGOptions o{};
    o.mode = want >= 2 ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
    o.numFramesToGenerate = std::max(1u, gen);
    o.flags = sl::DLSSGFlags(flags);
    o.onErrorCallback = &api_error_cb;
    o.enableUserInterfaceRecomposition = cfg_int("fg_ui_recompose", 0) ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    sl::Result r = g_sl.DLSSGSetOptions(0, o);
    log("[sl] slDLSSGSetOptions(mode %s, frames to generate %u, flags 0x%x): %s", want >= 2 ? "on" : "off",
        o.numFramesToGenerate, flags, res_str(r));
    g_fg_applied = want;
    g_flags_applied = flags;
    if (want >= 2 && !g_fg_running) {
      g_fg_on_frame = frame;
      ++g_fg_enables;
    }
    g_fg_running = want >= 2;
  }

  if (g_token && g_sl.PCLSetMarker) {
    g_sl.PCLSetMarker(sl::PCLMarker::eRenderSubmitEnd, *g_token);
    g_sl.PCLSetMarker(sl::PCLMarker::ePresentStart, *g_token);
  }
  VkResult r;
  {
    InSl guard;
    r = g_px.QueuePresentKHR(queue, info);
  }
  if (g_token && g_sl.PCLSetMarker) g_sl.PCLSetMarker(sl::PCLMarker::ePresentEnd, *g_token);
  if (g_fg_running && g_sl.DLSSGGetState) {
    sl::DLSSGState st{};
    if (g_sl.DLSSGGetState(0, st, nullptr) == sl::Result::eOk) {
      std::lock_guard<std::mutex> lock(g_fence_mu);
      g_inputs_fence = reinterpret_cast<VkSemaphore>(st.inputsProcessingCompletionFence);
      g_inputs_value = st.lastPresentInputsProcessingCompletionFenceValue;
      if (tagged) {
        std::lock_guard<std::mutex> copy_lock(g_mv_copy_mu);
        for (auto& slot : g_mv_copies)
          if (slot.image.image == input_snapshot[1].image ||
              (slot.lease.written && !slot.lease.value && fg_copy_precedes_present(slot.submitted,frame))) {
            slot.fence = g_inputs_fence; slot.lease.presented(g_inputs_value);
          }
      }
    }
  }

  if (g_sl.DLSSGGetState && frame >= g_last_state_log + uint64_t(g_fg_running ? cfg_int("fg_state_every", 60) : 600)) {
    sl::DLSSGState st{};
    g_sl.DLSSGGetState(0, st, nullptr);
    uint64_t shown = g_displayed.load();
    uint64_t batched = g_batched.load();
    static uint64_t prev_batched = 0;
    double ratio = frame > g_prev_frame ? double(shown - g_prev_displayed) / double(frame - g_prev_frame) : 0.0;
    log("[sl] DLSS-G frame %llu: status 0x%x, presented per app frame %u, displayed/rendered over the last %llu frames "
        "%.2f (batch-of-2 presents %llu), api errors %u (last %d), camera %s",
        (unsigned long long)frame, uint32_t(st.status), st.numFramesActuallyPresented,
        (unsigned long long)(frame - g_prev_frame), ratio, (unsigned long long)(batched - prev_batched),
        g_api_errors.load(), g_last_api_error.load(), g_cam_valid ? "from uniforms" : "identity");
    prev_batched = batched;
    g_prev_displayed = shown;
    g_prev_frame = frame;
    g_last_state_log = frame;
  }
  new_frame();
  return r;
}

bool sl_limits_frames() { return g_active && g_sl.ReflexSetOptions != nullptr; }

}  // namespace bg3
