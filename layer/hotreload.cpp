// Hot-reloadable core: bin/bg3rtx_core.dll is copied to bin/hot/ and loaded
// from there, so the build can overwrite the original while the game runs.
// When the original changes and has settled, the old copy is unloaded and the
// new one loaded at the next present. A fault inside the core disables it
// instead of taking the game down.
#include "host.h"
#include "assetdb.h"

#include <cstdarg>
#include <cstdio>

namespace bg3 {

static std::shared_mutex g_core_mu;
static HMODULE g_mod = nullptr;
static const Bg3CoreApi* g_core = nullptr;
static Bg3HostApi g_host{};
static uint64_t g_loaded_mtime = 0, g_seen_mtime = 0;
static ULONGLONG g_seen_tick = 0, g_last_check = 0;
static uint32_t g_generation = 0;
static bool g_faulted = false;

static std::string core_path() { return bin_dir() + "\\bg3rtx_core.dll"; }

static void host_log(const char* fmt, ...) {
  char msg[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  log("[core] %s", msg);
}

static uint64_t host_frame_index() { return frame_index(); }

// SEH wrappers: no C++ objects with destructors may live in these frames.
static bool seh_load(PFN_bg3rtx_core_load fn, const Bg3HostApi* host, const Bg3CoreApi** out) {
  __try {
    *out = fn(host);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
static bool seh_unload(const Bg3CoreApi* c) {
  __try {
    if (c->unload) c->unload();
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
static bool seh_frame(const Bg3CoreApi* c, uint64_t frame) {
  __try {
    c->on_frame(frame);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
static bool seh_pass(const Bg3CoreApi* c, VkCommandBuffer cmd, const Bg3PassInfo* p) {
  __try {
    c->on_pass_end(cmd, p);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool seh_pass_begin(const Bg3CoreApi* c, VkCommandBuffer cmd, const Bg3PassInfo* p) {
  __try {
    c->on_pass_begin(cmd, p);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
static bool seh_dispatch(const Bg3CoreApi* c, VkCommandBuffer cmd, const Bg3DispatchInfo* d) {
  __try {
    c->on_dispatch(cmd, d);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool seh_dispatch_pre(const Bg3CoreApi* c, VkCommandBuffer cmd, const Bg3DispatchInfo* d) {
  __try {
    c->on_dispatch_pre(cmd, d);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool seh_cmd_end(const Bg3CoreApi* c, VkCommandBuffer cmd) {
  __try {
    c->on_cmd_end(cmd);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static void unload_locked() {
  if (!g_core) return;
  if (!seh_unload(g_core)) log("[hot] core faulted inside unload(); its GPU objects may leak");
  g_core = nullptr;
  FreeLibrary(g_mod);
  g_mod = nullptr;
}

static bool load_locked() {
  std::string src = core_path();
  uint64_t mt = file_mtime(src);
  if (!mt) {
    log("[hot] %s not found; running without a core", src.c_str());
    return false;
  }
  std::string dir = bin_dir() + "\\hot";
  make_dirs(dir);
  char name[64];
  snprintf(name, sizeof name, "\\bg3rtx_core.%lu.%u.dll", GetCurrentProcessId(), ++g_generation);
  std::string dst = dir + name;
  if (!CopyFileA(src.c_str(), dst.c_str(), FALSE)) {
    log("[hot] copy to %s failed (%lu)", dst.c_str(), GetLastError());
    return false;
  }
  HMODULE mod = LoadLibraryA(dst.c_str());
  if (!mod) {
    log("[hot] LoadLibrary(%s) failed (%lu)", dst.c_str(), GetLastError());
    return false;
  }
  auto entry = reinterpret_cast<PFN_bg3rtx_core_load>(GetProcAddress(mod, BG3RTX_CORE_ENTRY));
  const Bg3CoreApi* api = nullptr;
  if (!entry || !seh_load(entry, &g_host, &api) || !api || api->abi_version != BG3RTX_ABI_VERSION) {
    log("[hot] core rejected: entry %s, abi %u (want %u)", entry ? "ok" : "missing", api ? api->abi_version : 0,
        BG3RTX_ABI_VERSION);
    FreeLibrary(mod);
    return false;
  }
  g_mod = mod;
  g_core = api;
  g_loaded_mtime = mt;
  g_faulted = false;
  log("[hot] core generation %u loaded", g_generation);
  return true;
}

static void clean_hot_dir() {
  WIN32_FIND_DATAA fd;
  std::string dir = bin_dir() + "\\hot\\";
  HANDLE h = FindFirstFileA((dir + "*.*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    // bg3rtx_core.<pid>.<generation>.dll: another game running from this bin (a
    // second test run) may have copied its core but not loaded it yet; leave it.
    unsigned long pid = 0;
    if (sscanf_s(fd.cFileName, "bg3rtx_core.%lu.", &pid) == 1 && pid != GetCurrentProcessId()) {
      HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
      bool alive = p && WaitForSingleObject(p, 0) == WAIT_TIMEOUT;
      if (p) CloseHandle(p);
      if (alive) continue;
    }
    DeleteFileA((dir + fd.cFileName).c_str());  // fails if in use
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}

void core_init(DeviceData* dd) {
  g_host.abi_version = BG3RTX_ABI_VERSION;
  g_host.log = &host_log;
  g_host.instance = dd->inst ? dd->inst->instance : VK_NULL_HANDLE;
  g_host.physical_device = dd->physical;
  g_host.device = dd->device;
  g_host.graphics_family = dd->graphics_family;
  g_host.graphics_queue = dd->graphics_queue;
  g_host.vk = &dd->vk;
  g_host.get_shader = &shader_get;
  g_host.cfg_int = &cfg_int;
  g_host.cfg_float = &cfg_float;
  g_host.persist = &persist;
  g_host.defer_destroy = &defer_destroy;
  g_host.frame_index = &host_frame_index;
  g_host.root_dir = root_dir().c_str();
  g_host.query_binding = &query_binding;
  g_host.alloc_image = &alloc_image;
  g_host.restore_bindings = &restore_bindings;
  g_host.dlss_motion_vectors = &ngx_motion_vectors;
  g_host.rt_scene = &rt_scene;
  g_host.rt_alpha = &rt_alpha_set;
  g_host.alloc_host_buffer = &alloc_host_buffer;
  g_host.query_set = &query_set;
  g_host.set_spec_hit_distance = &ngx_set_spec_hit_distance;
  g_host.assetdb = assetdb_api();
  g_host.rr_active = &ngx_rr_active;

  clean_hot_dir();
  std::unique_lock<std::shared_mutex> lock(g_core_mu);
  if (cfg_int("core", 1)) load_locked();
}

void core_shutdown() {
  std::unique_lock<std::shared_mutex> lock(g_core_mu);
  unload_locked();
}

void core_poll(VkQueue present_queue) {
  ULONGLONG now = GetTickCount64();
  if (now - g_last_check < 250) return;
  g_last_check = now;

  uint64_t mt = file_mtime(core_path());
  bool want = cfg_int("core", 1) != 0;
  bool changed = mt && mt != g_loaded_mtime;
  if (mt != g_seen_mtime) {  // still being written: wait until it settles
    g_seen_mtime = mt;
    g_seen_tick = now;
    return;
  }
  bool settled = now - g_seen_tick >= 400;
  bool toggle = want != (g_core != nullptr) && !(want && g_faulted && !changed);
  if (!toggle && !(changed && settled && want)) return;

  ULONGLONG t0 = GetTickCount64();
  DeviceData* dd = main_device();
  // Only this queue is ours to wait on here; deferred destruction covers command
  // buffers the game recorded but has not submitted yet.
  dd->vk.QueueWaitIdle(present_queue);
  std::unique_lock<std::shared_mutex> lock(g_core_mu);
  unload_locked();
  if (want) load_locked();
  else g_loaded_mtime = 0;
  log("[hot] reload took %llu ms", static_cast<unsigned long long>(GetTickCount64() - t0));
}

static void fault(const char* where) {
  if (g_faulted) return;
  g_faulted = true;
  log("[hot] core faulted in %s; disabled until the next build", where);
}

void core_on_frame(uint64_t frame) {
  std::shared_lock<std::shared_mutex> lock(g_core_mu);
  if (!g_core || g_faulted || !g_core->on_frame) return;
  if (!seh_frame(g_core, frame)) fault("on_frame");
}

void core_on_pass_end(VkCommandBuffer cmd, const Bg3PassInfo* pass) {
  std::shared_lock<std::shared_mutex> lock(g_core_mu);
  if (!g_core || g_faulted || !g_core->on_pass_end) return;
  if (!seh_pass(g_core, cmd, pass)) fault("on_pass_end");
}

void core_on_pass_begin(VkCommandBuffer cmd, const Bg3PassInfo* pass) {
  std::shared_lock<std::shared_mutex> lock(g_core_mu);
  if (!g_core || g_faulted || !g_core->on_pass_begin) return;
  if (!seh_pass_begin(g_core, cmd, pass)) fault("on_pass_begin");
}

void core_on_dispatch(VkCommandBuffer cmd, const Bg3DispatchInfo* info) {
  std::shared_lock<std::shared_mutex> lock(g_core_mu);
  if (!g_core || g_faulted || !g_core->on_dispatch) return;
  if (!seh_dispatch(g_core, cmd, info)) fault("on_dispatch");
}

void core_on_dispatch_pre(VkCommandBuffer cmd, const Bg3DispatchInfo* info) {
  std::shared_lock<std::shared_mutex> lock(g_core_mu);
  if (!g_core || g_faulted || !g_core->on_dispatch_pre) return;
  if (!seh_dispatch_pre(g_core, cmd, info)) fault("on_dispatch_pre");
}

void core_on_cmd_end(VkCommandBuffer cmd) {
  std::shared_lock<std::shared_mutex> lock(g_core_mu);
  if (!g_core || g_faulted || !g_core->on_cmd_end) return;
  if (!seh_cmd_end(g_core, cmd)) fault("on_cmd_end");
}

bool alloc_host_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* buffer, VkDeviceMemory* memory,
                       void** mapped) {
  DeviceData* dd = main_device();
  *buffer = VK_NULL_HANDLE;
  *memory = VK_NULL_HANDLE;
  *mapped = nullptr;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  if (dd->vk.CreateBuffer(dd->device, &bci, nullptr, buffer) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetBufferMemoryRequirements(dd->device, *buffer, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex =
      find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, memory) != VK_SUCCESS ||
      dd->vk.BindBufferMemory(dd->device, *buffer, *memory, 0) != VK_SUCCESS ||
      dd->vk.MapMemory(dd->device, *memory, 0, VK_WHOLE_SIZE, 0, mapped) != VK_SUCCESS) {
    dd->vk.DestroyBuffer(dd->device, *buffer, nullptr);
    if (*memory) dd->vk.FreeMemory(dd->device, *memory, nullptr);
    *buffer = VK_NULL_HANDLE;
    *memory = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

bool alloc_image(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage, Bg3Image* out) {
  DeviceData* dd = main_device();
  *out = Bg3Image{};
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.imageType = VK_IMAGE_TYPE_2D;
  ci.format = format;
  ci.extent = {w, h, 1};
  ci.mipLevels = 1;
  ci.arrayLayers = 1;
  ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = usage;
  ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (dd->vk.CreateImage(dd->device, &ci, nullptr, &out->image) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetImageMemoryRequirements(dd->device, out->image, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out->memory) != VK_SUCCESS ||
      dd->vk.BindImageMemory(dd->device, out->image, out->memory, 0) != VK_SUCCESS) {
    dd->vk.DestroyImage(dd->device, out->image, nullptr);
    if (out->memory) dd->vk.FreeMemory(dd->device, out->memory, nullptr);
    *out = Bg3Image{};
    return false;
  }
  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = out->image;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = format;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  dd->vk.CreateImageView(dd->device, &vci, nullptr, &out->view);
  return true;
}

// ---- deferred destruction ----

struct Deferred {
  Bg3DeferKind kind;
  uint64_t handle;
  uint64_t frame;
};
static std::mutex g_def_mu;
static std::vector<Deferred> g_deferred;

void defer_destroy(Bg3DeferKind kind, uint64_t handle) {
  if (!handle) return;
  std::lock_guard<std::mutex> lock(g_def_mu);
  g_deferred.push_back({kind, handle, frame_index()});
}

void defer_tick(bool flush_all) {
  DeviceData* dd = main_device();
  if (!dd) return;
  std::vector<Deferred> due;
  {
    std::lock_guard<std::mutex> lock(g_def_mu);
    uint64_t now = frame_index();
    // Retire only once the GPU has finished every frame that could still use the
    // object: frame counting alone is not enough, since the CPU can run many
    // presents ahead of the GPU (loading, headless), and a freed acceleration
    // structure still read by a trace is a GPU page fault (device lost). Covered
    // by the present fence of frame + 2 (the RT passes use the previous frame's
    // TLAS); 240 frames is a fallback if no present fence is being tracked.
    const uint64_t gpu_done = life_gpu_done();
    // One pass (the BVH split can retire thousands of objects at a level change).
    size_t w = 0;
    for (size_t i = 0; i < g_deferred.size(); ++i) {
      const uint64_t f = g_deferred[i].frame;
      if (flush_all || (now >= f + 4 && (f + 2 <= gpu_done || now >= f + 240))) due.push_back(g_deferred[i]);
      else g_deferred[w++] = g_deferred[i];
    }
    g_deferred.resize(w);
  }
  if (flush_all && !due.empty()) dd->vk.DeviceWaitIdle(dd->device);
  VkDevice d = dd->device;
  auto& vk = dd->vk;
  for (auto& x : due) {
    switch (x.kind) {
      case BG3_DEFER_PIPELINE: vk.DestroyPipeline(d, reinterpret_cast<VkPipeline>(x.handle), nullptr); break;
      case BG3_DEFER_PIPELINE_LAYOUT: vk.DestroyPipelineLayout(d, reinterpret_cast<VkPipelineLayout>(x.handle), nullptr); break;
      case BG3_DEFER_DESCRIPTOR_SET_LAYOUT: vk.DestroyDescriptorSetLayout(d, reinterpret_cast<VkDescriptorSetLayout>(x.handle), nullptr); break;
      case BG3_DEFER_DESCRIPTOR_POOL: vk.DestroyDescriptorPool(d, reinterpret_cast<VkDescriptorPool>(x.handle), nullptr); break;
      case BG3_DEFER_SHADER_MODULE: vk.DestroyShaderModule(d, reinterpret_cast<VkShaderModule>(x.handle), nullptr); break;
      case BG3_DEFER_IMAGE: vk.DestroyImage(d, reinterpret_cast<VkImage>(x.handle), nullptr); break;
      case BG3_DEFER_IMAGE_VIEW: vk.DestroyImageView(d, reinterpret_cast<VkImageView>(x.handle), nullptr); break;
      case BG3_DEFER_BUFFER: vk.DestroyBuffer(d, reinterpret_cast<VkBuffer>(x.handle), nullptr); break;
      case BG3_DEFER_MEMORY: vk.FreeMemory(d, reinterpret_cast<VkDeviceMemory>(x.handle), nullptr); break;
      case BG3_DEFER_SAMPLER: vk.DestroySampler(d, reinterpret_cast<VkSampler>(x.handle), nullptr); break;
      case BG3_DEFER_COMMAND_POOL: vk.DestroyCommandPool(d, reinterpret_cast<VkCommandPool>(x.handle), nullptr); break;
      case BG3_DEFER_FENCE: vk.DestroyFence(d, reinterpret_cast<VkFence>(x.handle), nullptr); break;
      case BG3_DEFER_SEMAPHORE: vk.DestroySemaphore(d, reinterpret_cast<VkSemaphore>(x.handle), nullptr); break;
      case BG3_DEFER_QUERY_POOL: vk.DestroyQueryPool(d, reinterpret_cast<VkQueryPool>(x.handle), nullptr); break;
      case BG3_DEFER_ACCELERATION_STRUCTURE:
        if (vk.DestroyAccelerationStructureKHR)
          vk.DestroyAccelerationStructureKHR(d, reinterpret_cast<VkAccelerationStructureKHR>(x.handle), nullptr);
        break;
    }
  }
}

}  // namespace bg3
