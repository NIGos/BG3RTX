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

#include <psapi.h>

#include <cmath>
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

VKAPI_ATTR VkResult VKAPI_CALL w_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
  diag(DIAG_DISPLAYED);
  g_displayed.fetch_add(1, std::memory_order_relaxed);
  return device_of(queue)->vk.QueuePresentKHR(queue, info);
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
}

VKAPI_ATTR void VKAPI_CALL w_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* out) {
  DeviceData* dd = device_of(device);
  dd->vk.GetDeviceQueue2(device, info, out);
  if (*out && dd->set_loader_data) dd->set_loader_data(device, *out);
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

void patch_iat(HMODULE mod, const char* label) {
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
      iat->u1.Function = reinterpret_cast<ULONG_PTR>(&hk_GetProcAddress);
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
VkImageView g_depth_only_view = VK_NULL_HANDLE;   // ours: depth aspect only
VkImage g_depth_only_for = VK_NULL_HANDLE;
std::unordered_map<VkImage, VkImageLayout> g_layout;   // queue-order layout at the last submit
std::atomic<VkImage> g_watch[4] = {};

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
constexpr uint64_t kCompositeFs = 0x046c6949b0e5297bull;   // UI composite -> swapchain
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

void set_watch() {
  g_watch[0] = g_depth.image;
  g_watch[1] = g_mvec.image;
  g_watch[2] = g_hudless.image;
  g_watch[3] = g_ui.image;
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
    if (idx >= s->dyn[set].size()) return err = "dynamic offset missing", false;
    off += s->dyn[set][idx];
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

void new_frame() {
  if (!g_sl.GetNewFrameToken) return;
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
void fill_constants(sl::Constants& c, uint32_t rw, uint32_t rh) {
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
  if (g_cam_valid && cfg_int("fg_camera", 1)) {
    vp_c = load(&g_cam.cam[32]);
    vp_p = load(&g_cam.prev[32]);
    proj = load(&g_cam.cam[16]);
    const float* cp = &g_cam.cam[52];
    const float* pp = &g_cam.prev[48];
    jx = g_cam.prev[52];
    jy = g_cam.prev[53];
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
      log("[sl] camera b2.m0 %s", mstr(load(&g_cam.cam[0])).c_str());
      log("[sl] camera b2.m1 %s", mstr(load(&g_cam.cam[16])).c_str());
      log("[sl] camera b2.m2 %s", mstr(vp_c).c_str());
      log("[sl] camera b2 floats %g %g | %g %g | pos %g %g %g | %d", g_cam.cam[48], g_cam.cam[49], g_cam.cam[50],
          g_cam.cam[51], cp[0], cp[1], cp[2], *reinterpret_cast<const int*>(&g_cam.cam[55]));
      log("[sl] camera b5.m2 %s", mstr(vp_p).c_str());
      log("[sl] camera b5 prevpos %g %g %g | %g | jitter %g %g | prev jitter %g %g", pp[0], pp[1], pp[2],
          g_cam.prev[51], g_cam.prev[52], g_cam.prev[53], g_cam.prev[54], g_cam.prev[55]);
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

// ---------------------------------------------------------------------------
// instance

void sl_instance_pre(const VkInstanceCreateInfo* in, VkInstanceCreateInfo& out) {
  static std::vector<const char*> exts;
  static VkApplicationInfo app;
  out = *in;
  if (g_in_sl_init || g_sl_instance) return;   // Streamline's probe instance, or a later instance
  int fg = cfg_int("fg", 0);
  if (fg < 2 && !cfg_int("sl", 0)) {
    log("[sl] fg=%d: Streamline not loaded", fg);
    return;
  }
  std::string dir = bin_dir() + (cfg_int("sl_dev", 1) ? "\\sl_dev" : "\\sl");
  std::wstring wdir = widen(dir);
  g_vulkan1 = GetModuleHandleA("vulkan-1.dll");
  g_interposer = LoadLibraryW((wdir + L"\\sl.interposer.dll").c_str());
  if (!g_interposer) {
    log("[sl] cannot load %s\\sl.interposer.dll (error %lu)", dir.c_str(), GetLastError());
    return;
  }
  patch_modules();
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
               sl::PreferenceFlags::eDisableDebugText;
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
  if (g_in_sl_init || g_sl_instance || !g_sl_init) return;
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

void sl_device_ready(DeviceData* dd) {
  if (!g_pending || dd != g_dd) return;
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
  if (g_sl.ReflexSetOptions) {
    sl::ReflexOptions ro{};
    int reflex = cfg_int("reflex", 1);
    ro.mode = reflex == 2 ? sl::ReflexMode::eLowLatencyWithBoost : reflex ? sl::ReflexMode::eLowLatency : sl::ReflexMode::eOff;
    sl::Result rr = g_sl.ReflexSetOptions(ro);
    log("[sl] slReflexSetOptions(mode %d): %s", int(ro.mode), res_str(rr));
  }
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
  g_sl = {};
  g_px = {};
  g_dd = nullptr;
}

// ---------------------------------------------------------------------------
// swapchain entry points

bool sl_create_swapchain(VkDevice d, const VkSwapchainCreateInfoKHR* ci, const VkAllocationCallbacks* a,
                         VkSwapchainKHR* out, VkResult* r) {
  if (g_dd && d == g_dd->device) sl_device_ready(g_dd);
  if (g_active && g_dd && d == g_dd->device) register_surface(g_dd->inst->instance, ci->surface);
  if (!g_active || !g_dd || d != g_dd->device) return false;
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

void sl_note_barrier(CmdState* s, VkImage image, VkImageLayout layout) {
  if (!sl_watched(image)) return;
  for (auto& p : s->sl_layouts)
    if (p.first == image) {
      p.second = layout;
      return;
    }
  s->sl_layouts.emplace_back(image, layout);
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
  } else if (p.first_fs_hash == kCompositeFs) {
    // UI composite: set 1 binding 1 = tonemapped scene (hudless), binding 2 = UI with alpha
    VkDescriptorSet ds = s->sets[0][1];
    VkImageView v_h = VK_NULL_HANDLE, v_u = VK_NULL_HANDLE;
    {
      std::shared_lock<std::shared_mutex> lock(tracker().mu);
      auto si = tracker().sets.find(ds);
      if (si != tracker().sets.end()) {
        auto h = si->second.find(desc_key(1, 0));
        auto u = si->second.find(desc_key(2, 0));
        if (h != si->second.end()) v_h = h->second.view;
        if (u != si->second.end()) v_u = u->second.view;
      }
    }
    VkImage i_h = VK_NULL_HANDLE, i_u = VK_NULL_HANDLE;
    {
      std::shared_lock<std::shared_mutex> lock(tracker().mu);
      auto a = tracker().views.find(v_h);
      auto b = tracker().views.find(v_u);
      if (a != tracker().views.end()) i_h = a->second.image;
      if (b != tracker().views.end()) i_u = b->second.image;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    if (i_h && i_h != g_hudless.image)
      log("[sl] hudless = image %p (composite set 1 binding 1)", (void*)i_h);
    if (i_u && i_u != g_ui.image) log("[sl] UI color+alpha = image %p (composite set 1 binding 2)", (void*)i_u);
    if (i_h) fill_img(g_hudless, i_h, v_h);
    if (i_u) fill_img(g_ui, i_u, v_u);
    set_watch();
  }
}

void sl_note_pass_end(VkCommandBuffer cmd, CmdState* s) {
  (void)cmd;
  if (!g_active) return;
  const Bg3PassInfo& p = s->pass;
  if (p.first_fs_hash == kVelocityFs && p.color_count >= 1 && p.depth_image) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (p.color_images[0] != g_mvec.image)
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
    if (s->sl_cam_valid) {
      memcpy(g_cam.cam, s->sl_cam[0], sizeof g_cam.cam);
      memcpy(g_cam.prev, s->sl_cam[1], sizeof g_cam.prev);
      g_cam_valid = true;
      g_cam_frame = frame_index();
    }
  }
}

void sl_note_image_destroyed(VkImage image) {
  if (!g_active || !sl_watched(image)) return;
  std::lock_guard<std::mutex> lock(g_mu);
  for (Img* i : {&g_depth, &g_mvec, &g_hudless, &g_ui})
    if (i->image == image) *i = Img{};
  g_layout.erase(image);
  set_watch();
  log("[sl] a tagged image was destroyed; waiting for the passes to show it again");
}

// ---------------------------------------------------------------------------
// present

VkResult sl_present(DeviceData* dd, VkQueue queue, const VkPresentInfoKHR* info) {
  if (!g_active || dd != g_dd) {
    diag(DIAG_DISPLAYED);
    return dd->vk.QueuePresentKHR(queue, info);
  }
  uint64_t frame = frame_index();
  int fg = cfg_int("fg", 0);
  uint32_t gen = fg >= 2 ? uint32_t(std::min(fg, 4) - 1) : 0;

  bool ready = false;
  if (g_token) {
    std::lock_guard<std::mutex> lock(g_mu);
    // everything identified within the last couple of frames (menus have no velocity pass)
    ready = g_depth.image && g_mvec.image && g_hudless.image && g_ui.image && frame <= g_mvec.seen + 3 &&
            frame <= g_ui.seen + 3;
    if (ready) {
      ensure_depth_view();
      sl::Resource depth = make_res(g_depth, g_depth_only_view,
                                    layout_of(g_depth.image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL));
      sl::Resource mvec = make_res(g_mvec, g_mvec.view, layout_of(g_mvec.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
      sl::Resource hud = make_res(g_hudless, g_hudless.view,
                                  layout_of(g_hudless.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
      sl::Resource ui = make_res(g_ui, g_ui.view, layout_of(g_ui.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
      sl::Extent rx{0, 0, g_mvec.w, g_mvec.h};
      sl::Extent dx{0, 0, g_depth.w, g_depth.h};
      sl::Extent ox{0, 0, g_hudless.w, g_hudless.h};
      sl::Extent ux{0, 0, g_ui.w, g_ui.h};
      auto life = sl::ResourceLifecycle(cfg_int("fg_lifecycle", int(sl::ResourceLifecycle::eValidUntilPresent)));
      std::vector<sl::ResourceTag> tags = {
          sl::ResourceTag(&depth, sl::kBufferTypeDepth, life, &dx),
          sl::ResourceTag(&mvec, sl::kBufferTypeMotionVectors, life, &rx),
      };
      if (cfg_int("fg_tag_hudless", 1)) tags.emplace_back(&hud, sl::kBufferTypeHUDLessColor, life, &ox);
      if (cfg_int("fg_tag_ui", 1)) tags.emplace_back(&ui, sl::kBufferTypeUIColorAndAlpha, life, &ux);
      if (cfg_int("fg_tag_backbuffer", 1))
        tags.emplace_back(nullptr, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle::eValidUntilPresent, &ox);
      sl::Result tr = g_sl.SetTagForFrame(*g_token, 0, tags.data(), uint32_t(tags.size()), nullptr);
      sl::Constants c{};
      fill_constants(c, g_mvec.w, g_mvec.h);
      sl::Result cr = g_sl.SetConstants(c, *g_token, 0);
      static uint32_t logged = 0;
      if ((tr != sl::Result::eOk || cr != sl::Result::eOk || logged < 1) && logged < 20) {
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
  int want = ready ? fg : 0;
  if (g_sl.DLSSGSetOptions && (want != g_fg_applied || flags != g_flags_applied)) {
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

  if (g_sl.DLSSGGetState && frame >= g_last_state_log + 600) {
    sl::DLSSGState st{};
    g_sl.DLSSGGetState(0, st, nullptr);
    uint64_t shown = g_displayed.load();
    double ratio = frame > g_prev_frame ? double(shown - g_prev_displayed) / double(frame - g_prev_frame) : 0.0;
    log("[sl] DLSS-G frame %llu: status 0x%x, presented per app frame %u, displayed/rendered over the last %llu frames "
        "%.2f, api errors %u (last %d), camera %s",
        (unsigned long long)frame, uint32_t(st.status), st.numFramesActuallyPresented,
        (unsigned long long)(frame - g_prev_frame), ratio, g_api_errors.load(), g_last_api_error.load(),
        g_cam_valid ? "from uniforms" : "identity");
    g_prev_displayed = shown;
    g_prev_frame = frame;
    g_last_state_log = frame;
  }
  new_frame();
  return r;
}

}  // namespace bg3
