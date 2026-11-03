// Track A: BG3's DLSS Super Resolution evaluate -> DLSS Ray Reconstruction.
//
// BG3 links the NGX client statically (bg3.exe exports NVSDK_NGX_*), and that
// client calls into the driver's NGX loader, _nvngx.dll. We hook the loader's
// Vulkan exports (CreateFeature, CreateFeature1, EvaluateFeature,
// ReleaseFeature) the moment the DLL is mapped, with a 14-byte absolute jump
// and a relocated-prologue trampoline. Never the game executable, never the
// feature snippets.
//
// With rr=1 the game's SR evaluate is not forwarded. Instead, in the same
// command buffer:
//   1. rr_guides.hlsl converts the G-buffer (RT0 normals, RT1 base colour,
//      RT2 roughness/specular/metal) into RR's guide formats;
//   2. a RayReconstruction feature, created lazily with the SR feature's own
//      create contract, is evaluated with the game's parameter block (colour,
//      depth, MV, output, jitter, MV scale, reset...) plus the guide keys.
// rr is read on every evaluate, so it switches without a restart; the first
// evaluate after a switch carries Reset=1.
#include "host.h"

#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_defs_dlssd.h>

#include <tlhelp32.h>

#include <atomic>
#include <cmath>
#include <cstring>

#pragma comment(lib, "version.lib")

namespace bg3 {

void ngx_note_pass(const Bg3PassInfo& p, const VkImageLayout* layouts);

// ---------------------------------------------------------------------------
// Inline hooks
// ---------------------------------------------------------------------------

struct InlineHook {
  uint8_t* target = nullptr;
  void* trampoline = nullptr;  // relocated prologue + jump back
  bool active = false;
};

static uint8_t* g_tramp_page = nullptr;
static size_t g_tramp_used = 0;

// Length of one prologue instruction, or -1 for anything we do not relocate
// (RIP-relative operands, branches). Covers what MSVC emits at function entry.
static int insn_len(const uint8_t* p) {
  int i = 0;
  if ((p[0] & 0xF0) == 0x40) i = 1;  // REX
  uint8_t op = p[i];
  if (op >= 0x50 && op <= 0x57) return i + 1;  // push r
  auto modrm_len = [&](int at) -> int {
    uint8_t m = p[at];
    int mod = m >> 6, rm = m & 7, len = at + 1;
    if (mod == 3) return len;
    if (mod == 0 && rm == 5) return -1;  // RIP-relative
    if (rm == 4) {
      if (mod == 0 && (p[len] & 7) == 5) len += 4;
      ++len;  // SIB
    }
    if (mod == 1) len += 1;
    else if (mod == 2) len += 4;
    return len;
  };
  switch (op) {
    case 0x89: case 0x8B: case 0x8D: case 0x85: case 0x33: case 0x2B: case 0x03: return modrm_len(i + 1);
    case 0x83: { int l = modrm_len(i + 1); return l < 0 ? -1 : l + 1; }
    case 0x81: { int l = modrm_len(i + 1); return l < 0 ? -1 : l + 4; }
    default: return -1;
  }
}

static uint8_t* follow_jumps(uint8_t* p) {
  for (int hop = 0; hop < 4; ++hop) {
    if (p[0] == 0xE9) p = p + 5 + *reinterpret_cast<int32_t*>(p + 1);
    else if (p[0] == 0xEB) p = p + 2 + static_cast<int8_t>(p[1]);
    else if (p[0] == 0xFF && p[1] == 0x25) p = *reinterpret_cast<uint8_t**>(p + 6 + *reinterpret_cast<int32_t*>(p + 2));
    else break;
  }
  return p;
}

static void write_abs_jump(uint8_t* at, const void* to) {
  at[0] = 0xFF; at[1] = 0x25;
  memset(at + 2, 0, 4);
  memcpy(at + 6, &to, 8);
}

static bool hook_install(InlineHook& h, void* fn, void* detour, const char* name) {
  uint8_t* t = follow_jumps(static_cast<uint8_t*>(fn));
  int n = 0;
  while (n < 14) {
    int l = insn_len(t + n);
    if (l <= 0) {
      char hex[64] = {};
      for (int i = 0; i < 16; ++i) snprintf(hex + i * 3, 4, "%02X ", t[i]);
      log("[ngx] cannot relocate the prologue of %s at %p (+%d): %s", name, t, n, hex);
      return false;
    }
    n += l;
  }
  if (!g_tramp_page) {
    g_tramp_page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!g_tramp_page) return false;
  }
  if (g_tramp_used + 64 > 4096) return false;
  uint8_t* tr = g_tramp_page + g_tramp_used;
  g_tramp_used += 64;
  memcpy(tr, t, n);
  write_abs_jump(tr + n, t + n);
  FlushInstructionCache(GetCurrentProcess(), tr, 64);

  uint8_t patch[14];
  write_abs_jump(patch, detour);
  DWORD old = 0;
  if (!VirtualProtect(t, 14, PAGE_EXECUTE_READWRITE, &old)) return false;
  memcpy(t, patch, 14);
  VirtualProtect(t, 14, old, &old);
  FlushInstructionCache(GetCurrentProcess(), t, 14);
  h.target = t;
  h.trampoline = tr;
  h.active = true;
  log("[ngx] hooked %s at %p (prologue %d bytes relocated)", name, t, n);
  return true;
}

// ---------------------------------------------------------------------------
// NGX entry points
// ---------------------------------------------------------------------------

using PFN_Create = NVSDK_NGX_Result (*)(VkCommandBuffer, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using PFN_Create1 = NVSDK_NGX_Result (*)(VkDevice, VkCommandBuffer, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*,
                                         NVSDK_NGX_Handle**);
using PFN_Eval = NVSDK_NGX_Result (*)(VkCommandBuffer, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
                                      PFN_NVSDK_NGX_ProgressCallback);
using PFN_Release = NVSDK_NGX_Result (*)(NVSDK_NGX_Handle*);
using PFN_AllocParams = NVSDK_NGX_Result (*)(NVSDK_NGX_Parameter**);

static HMODULE g_ngx_mod = nullptr;
static InlineHook g_h_create, g_h_create1, g_h_eval, g_h_release;
static PFN_AllocParams g_alloc_params = nullptr;

static PFN_Create orig_create() { return reinterpret_cast<PFN_Create>(g_h_create.trampoline); }
static PFN_Create1 orig_create1() { return reinterpret_cast<PFN_Create1>(g_h_create1.trampoline); }
static PFN_Eval orig_eval() { return reinterpret_cast<PFN_Eval>(g_h_eval.trampoline); }
static PFN_Release orig_release() { return reinterpret_cast<PFN_Release>(g_h_release.trampoline); }

static thread_local int t_nest = 0;
struct Nest {
  Nest() { ++t_nest; }
  ~Nest() { --t_nest; }
};

static const char* result_name(NVSDK_NGX_Result r) {
  switch (r) {
    case NVSDK_NGX_Result_Success: return "Success";
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported: return "FeatureNotSupported";
    case NVSDK_NGX_Result_FAIL_PlatformError: return "PlatformError";
    case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists: return "FeatureAlreadyExists";
    case NVSDK_NGX_Result_FAIL_FeatureNotFound: return "FeatureNotFound";
    case NVSDK_NGX_Result_FAIL_InvalidParameter: return "InvalidParameter";
    case NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall: return "ScratchBufferTooSmall";
    case NVSDK_NGX_Result_FAIL_NotInitialized: return "NotInitialized";
    case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat: return "UnsupportedInputFormat";
    case NVSDK_NGX_Result_FAIL_RWFlagMissing: return "RWFlagMissing";
    case NVSDK_NGX_Result_FAIL_MissingInput: return "MissingInput";
    case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature: return "UnableToInitializeFeature";
    case NVSDK_NGX_Result_FAIL_OutOfDate: return "OutOfDate";
    case NVSDK_NGX_Result_FAIL_OutOfGPUMemory: return "OutOfGPUMemory";
    case NVSDK_NGX_Result_FAIL_UnsupportedFormat: return "UnsupportedFormat";
    case NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath: return "UnableToWriteToAppDataPath";
    case NVSDK_NGX_Result_FAIL_UnsupportedParameter: return "UnsupportedParameter";
    case NVSDK_NGX_Result_FAIL_Denied: return "Denied";
    case NVSDK_NGX_Result_FAIL_NotImplemented: return "NotImplemented";
    default: return "?";
  }
}

static uint32_t image_id(VkImage img) {
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  auto it = tracker().images.find(img);
  return it == tracker().images.end() ? 0 : it->second.id;
}

static NVSDK_NGX_Resource_VK* get_res(const NVSDK_NGX_Parameter* p, const char* key) {
  void* v = nullptr;
  if (p->Get(key, &v) != NVSDK_NGX_Result_Success) return nullptr;
  return static_cast<NVSDK_NGX_Resource_VK*>(v);
}
static bool get_ui(const NVSDK_NGX_Parameter* p, const char* key, unsigned& v) {
  return p->Get(key, &v) == NVSDK_NGX_Result_Success;
}
static bool get_i(const NVSDK_NGX_Parameter* p, const char* key, int& v) {
  return p->Get(key, &v) == NVSDK_NGX_Result_Success;
}
static bool get_f(const NVSDK_NGX_Parameter* p, const char* key, float& v) {
  return p->Get(key, &v) == NVSDK_NGX_Result_Success;
}

static void log_res(const char* key, const NVSDK_NGX_Resource_VK* r) {
  if (!r) {
    log("[ngx]   %-16s (not set)", key);
    return;
  }
  if (r->Type != NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW) {
    log("[ngx]   %-16s buffer %p", key, (void*)r->Resource.BufferInfo.Buffer);
    return;
  }
  const auto& iv = r->Resource.ImageViewInfo;
  ImageRec rec{};
  bool known = false;
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    auto it = tracker().images.find(iv.Image);
    if (it != tracker().images.end()) rec = it->second, known = true;
  }
  log("[ngx]   %-16s i%u  %ux%u fmt %d aspect 0x%x rw %d  (image %ux%u usage 0x%x)", key, known ? rec.id : 0, iv.Width,
      iv.Height, iv.Format, iv.SubresourceRange.aspectMask, r->ReadWrite ? 1 : 0, known ? rec.ci.extent.width : 0,
      known ? rec.ci.extent.height : 0, known ? rec.ci.usage : 0);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct SrFeature {
  NVSDK_NGX_Handle* sr = nullptr;
  unsigned w = 0, h = 0, ow = 0, oh = 0;
  int pq = 0, flags = 0, subrects = 0;
  NVSDK_NGX_Handle* rr = nullptr;
  bool rr_failed = false;
  int last_mode = -1;
  uint64_t evals = 0;
};

struct GBuffer {
  VkImage rt[3] = {};
  VkImageLayout layout[3] = {};
  uint32_t w = 0, h = 0;
  uint64_t seen_frame = 0;
};

struct Guide {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  bool initialized = false;
  NVSDK_NGX_Resource_VK res{};
};

struct SetKey {
  VkImage rt0, rt1, rt2, depth, out;
  VkImageLayout rt_layout, depth_layout;
  bool operator==(const SetKey& o) const {
    return rt0 == o.rt0 && rt1 == o.rt1 && rt2 == o.rt2 && depth == o.depth && out == o.out &&
           rt_layout == o.rt_layout && depth_layout == o.depth_layout;
  }
};

static std::mutex g_mu;
static std::vector<SrFeature> g_feats;
static GBuffer g_gbuf;
static std::mutex g_gbuf_mu;

static Guide g_guide[3];  // diffuse, specular, normal+roughness
static uint32_t g_guide_w = 0, g_guide_h = 0;

static VkDescriptorSetLayout g_dsl = VK_NULL_HANDLE;
static VkPipelineLayout g_pl = VK_NULL_HANDLE;
static VkPipeline g_pipe = VK_NULL_HANDLE, g_pipe_show = VK_NULL_HANDLE;
static uint64_t g_pipe_gen = 0, g_show_gen = 0;
static VkDescriptorPool g_pool = VK_NULL_HANDLE;
static std::vector<std::pair<SetKey, VkDescriptorSet>> g_sets;
static std::unordered_map<VkImage, VkImageView> g_views;  // our views of game images

// Recording threads must never idle the device to reclaim RR descriptors:
// DLSS-G's pacer can still submit to its queues. Keep old sets alive behind
// the existing GPU-completion retirement fence and allocate a fresh pool.
static void retire_guide_pool() {
  g_sets.clear();
  if (g_pool) defer_destroy(BG3_DEFER_DESCRIPTOR_POOL, uint64_t(g_pool));
  g_pool = VK_NULL_HANDLE;
}

static bool create_guide_pool(DeviceData* dd) {
  VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4 * 64}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 * 64}};
  VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  ci.maxSets = 64; ci.poolSizeCount = 2; ci.pPoolSizes = sizes;
  return dd->vk.CreateDescriptorPool(dd->device, &ci, nullptr, &g_pool) == VK_SUCCESS;
}

// GPU timing: per evaluate, 3 timestamps (start, after guides, after evaluate).
static const uint32_t kTsSlots = 32;
static VkQueryPool g_qpool = VK_NULL_HANDLE;
static float g_ts_period = 1.0f;
static uint32_t g_ts_next = 0;
static int g_ts_mode[kTsSlots];
static bool g_ts_pending[kTsSlots];
static double g_acc_guides[2], g_acc_eval[2];
static uint32_t g_acc_n[2];
static ULONGLONG g_acc_tick = 0;

static bool g_logged_eval_contract = false;

// Last motion vectors the game gave DLSS, for the core's temporal passes.
static std::mutex g_mv_mu;
static VkImage g_mv_image = VK_NULL_HANDLE;
static VkImageView g_mv_view = VK_NULL_HANDLE;
static VkFormat g_mv_format = VK_FORMAT_UNDEFINED;
static float g_mv_scale[2] = {1.0f, 1.0f};

// The game recreates its render targets on level loads: drop a destroyed MV image.
void ngx_forget_image(VkImage image) {
  std::lock_guard<std::mutex> lock(g_mv_mu);
  if (image == g_mv_image) {
    g_mv_image = VK_NULL_HANDLE;
    g_mv_view = VK_NULL_HANDLE;
  }
}

bool ngx_motion_vectors(VkImage* image, VkImageView* view, VkFormat* format, float scale[2]) {
  std::lock_guard<std::mutex> lock(g_mv_mu);
  if (!g_mv_image) return false;
  *image = g_mv_image;
  *view = g_mv_view;
  *format = g_mv_format;
  scale[0] = g_mv_scale[0];
  scale[1] = g_mv_scale[1];
  return true;
}
static int g_jitter_logs = 0;

// Track D reflections (D-RF): the core's specular hit distance image, handed to
// RR as DLSSD.SpecularHitDistance while it is fresh (written this or last frame).
static std::mutex g_shd_mu;
static NVSDK_NGX_Resource_VK g_shd_res{};
static uint64_t g_shd_frame = 0;
static bool g_shd_logged = false;

// ABI 7: whether Ray Reconstruction actually denoises the frame. The core's
// GI, local-light shadows and reflections pick their 1-spp RR modes from this,
// not from the rr key: with FSR/TAA, an SR fallback or a failed RR creation
// they must not leave raw 1-spp noise in the frame.
static std::atomic<uint64_t> g_rr_frame{0};
bool ngx_rr_active() {
  uint64_t f = g_rr_frame.load();
  return f != 0 && f + 3 >= frame_index();
}

// The nvngx_dlssd snippet NGX actually loaded (the game's 310.7.129, or
// Streamline's 310.9.16 from bin/sl with fg>=2, or an OTA copy).
static void log_dlssd_module() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
  if (snap == INVALID_HANDLE_VALUE) return;
  MODULEENTRY32W me{};
  me.dwSize = sizeof me;
  bool found = false;
  for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
    if (_wcsnicmp(me.szModule, L"nvngx_dlssd", 11) != 0) continue;
    found = true;
    DWORD dummy = 0, size = GetFileVersionInfoSizeW(me.szExePath, &dummy);
    std::vector<uint8_t> buf(size ? size : 1);
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (size && GetFileVersionInfoW(me.szExePath, 0, size, buf.data()) &&
        VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&fi), &len) && fi)
      log("[ngx] RR snippet loaded: %ls, version %u.%u.%u.%u", me.szExePath, HIWORD(fi->dwFileVersionMS),
          LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
    else
      log("[ngx] RR snippet loaded: %ls (no version resource)", me.szExePath);
  }
  CloseHandle(snap);
  if (!found) log("[ngx] RR snippet: no nvngx_dlssd module found in the process");
}

void ngx_set_spec_hit_distance(VkImage image, VkImageView view, VkFormat format, uint32_t w, uint32_t h) {
  std::lock_guard<std::mutex> lock(g_shd_mu);
  g_shd_res = {};
  g_shd_frame = 0;
  if (!image || !view) return;
  g_shd_res.Resource.ImageViewInfo.ImageView = view;
  g_shd_res.Resource.ImageViewInfo.Image = image;
  g_shd_res.Resource.ImageViewInfo.SubresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  g_shd_res.Resource.ImageViewInfo.Format = format;
  g_shd_res.Resource.ImageViewInfo.Width = w;
  g_shd_res.Resource.ImageViewInfo.Height = h;
  g_shd_res.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
  g_shd_res.ReadWrite = true;
  g_shd_frame = frame_index();
}

// ABI 26 (rt_water_rr_mv): the core's motion vectors (the game's with the water surface's on the
// water pixels), handed to RR instead of the game's while fresh. FG keeps the game's vectors.
static std::mutex g_rmv_mu;
static NVSDK_NGX_Resource_VK g_rmv_res{};
static uint64_t g_rmv_frame = 0;
static bool g_rmv_logged = false;

void ngx_set_rr_motion_vectors(VkImage image, VkImageView view, VkFormat format, uint32_t w, uint32_t h) {
  std::lock_guard<std::mutex> lock(g_rmv_mu);
  g_rmv_res = {};
  g_rmv_frame = 0;
  if (!image || !view) return;
  g_rmv_res.Resource.ImageViewInfo.ImageView = view;
  g_rmv_res.Resource.ImageViewInfo.Image = image;
  g_rmv_res.Resource.ImageViewInfo.SubresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  g_rmv_res.Resource.ImageViewInfo.Format = format;
  g_rmv_res.Resource.ImageViewInfo.Width = w;
  g_rmv_res.Resource.ImageViewInfo.Height = h;
  g_rmv_res.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
  g_rmv_res.ReadWrite = false;
  g_rmv_frame = frame_index();
}

// rt_water_fg_mv (layer/streamline.cpp): the same core vectors for DLSS-G, while fresh.
bool ngx_core_motion_vectors(VkImage* image, VkImageView* view, VkFormat* format, uint32_t* w, uint32_t* h) {
  std::lock_guard<std::mutex> lock(g_rmv_mu);
  if (!g_rmv_frame || g_rmv_frame + 1 < frame_index()) return false;
  const auto& im = g_rmv_res.Resource.ImageViewInfo;
  *image = im.Image, *view = im.ImageView, *format = VkFormat(im.Format), *w = im.Width, *h = im.Height;
  return true;
}

// ---------------------------------------------------------------------------
// G-buffer discovery (called at every render pass end, on recording threads)
// ---------------------------------------------------------------------------

static void record_guides_at_gbuffer(VkCommandBuffer cmd, const Bg3PassInfo& p, const VkImageLayout* layouts,
                                     VkImageLayout depth_layout);

void ngx_note_pass(VkCommandBuffer cmd, const Bg3PassInfo& p, const VkImageLayout* layouts,
                   VkImageLayout depth_layout) {
  if (p.color_count < 4 || p.draw_count == 0) return;
  if (p.color_formats[0] != VK_FORMAT_R16G16_SFLOAT || p.color_formats[1] != VK_FORMAT_R8G8B8A8_UNORM ||
      p.color_formats[2] != VK_FORMAT_R8G8B8A8_UNORM || p.color_formats[3] != VK_FORMAT_R8G8B8A8_UNORM)
    return;
  if (p.depth_format != VK_FORMAT_D24_UNORM_S8_UINT) return;
  {
    // A smaller pass with the G-buffer's formats (UI portraits: the party screen) is not
    // the scene's: it must not replace the main G-buffer (RR would refuse the frame) or
    // record the guides. The main one is the largest seen in the last few frames.
    std::lock_guard<std::mutex> lock(g_gbuf_mu);
    if (g_gbuf.rt[0] && uint64_t(p.width) * p.height < uint64_t(g_gbuf.w) * g_gbuf.h &&
        frame_index() <= g_gbuf.seen_frame + 4) {
      static int said = 0;
      if (said++ < 4)
        log("[ngx] %ux%u pass with the G-buffer formats ignored (the scene's is %ux%u)", p.width, p.height, g_gbuf.w,
            g_gbuf.h);
      return;
    }
  }
  record_guides_at_gbuffer(cmd, p, layouts, depth_layout);
  std::lock_guard<std::mutex> lock(g_gbuf_mu);
  bool changed = g_gbuf.rt[0] != p.color_images[0] || g_gbuf.w != p.width;
  for (int i = 0; i < 3; ++i) {
    g_gbuf.rt[i] = p.color_images[i];
    if (layouts[i] != VK_IMAGE_LAYOUT_UNDEFINED) g_gbuf.layout[i] = layouts[i];
  }
  g_gbuf.w = p.width;
  g_gbuf.h = p.height;
  g_gbuf.seen_frame = frame_index();
  if (changed)
    log("[ngx] G-buffer pass %ux%u: RT0 i%u RT1 i%u RT2 i%u (final layouts %d %d %d, fs %016llx)", p.width, p.height,
        image_id(p.color_images[0]), image_id(p.color_images[1]), image_id(p.color_images[2]), layouts[0], layouts[1],
        layouts[2], (unsigned long long)p.first_fs_hash);
}

// ---------------------------------------------------------------------------
// GPU resources
// ---------------------------------------------------------------------------

static uint32_t device_memory_type(DeviceData* dd, uint32_t bits) {
  VkPhysicalDeviceMemoryProperties props{};
  dd->inst->GetPhysicalDeviceMemoryProperties(dd->physical, &props);
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return i;
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
    if (bits & (1u << i)) return i;
  return UINT32_MAX;
}

static void guide_destroy(DeviceData* dd, Guide& g) {
  if (g.view) defer_destroy(BG3_DEFER_IMAGE_VIEW, (uint64_t)g.view);
  if (g.image) {
    defer_destroy(BG3_DEFER_IMAGE, (uint64_t)g.image);
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().images.erase(g.image);
  }
  if (g.memory) defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g.memory);
  g = Guide{};
}

static bool guide_create(DeviceData* dd, Guide& g, VkFormat fmt, uint32_t w, uint32_t h) {
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.imageType = VK_IMAGE_TYPE_2D;
  ci.format = fmt;
  ci.extent = {w, h, 1};
  ci.mipLevels = 1;
  ci.arrayLayers = 1;
  ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (dd->vk.CreateImage(dd->device, &ci, nullptr, &g.image) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetImageMemoryRequirements(dd->device, g.image, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = device_memory_type(dd, req.memoryTypeBits);
  if (dd->vk.AllocateMemory(dd->device, &mai, nullptr, &g.memory) != VK_SUCCESS) return false;
  dd->vk.BindImageMemory(dd->device, g.image, g.memory, 0);
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = g.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = fmt;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (dd->vk.CreateImageView(dd->device, &vi, nullptr, &g.view) != VK_SUCCESS) return false;
  g.format = fmt;
  g.initialized = false;
  g.res = {};
  g.res.Resource.ImageViewInfo.ImageView = g.view;
  g.res.Resource.ImageViewInfo.Image = g.image;
  g.res.Resource.ImageViewInfo.SubresourceRange = vi.subresourceRange;
  g.res.Resource.ImageViewInfo.Format = fmt;
  g.res.Resource.ImageViewInfo.Width = w;
  g.res.Resource.ImageViewInfo.Height = h;
  g.res.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
  g.res.ReadWrite = true;
  // Visible to the tracer, so guide buffers can be dumped like render targets.
  ImageRec rec{tracker().next_image_id++, ci, false};
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  tracker().images[g.image] = rec;
  tracker().views[g.view] = ViewRec{g.image, fmt, vi.subresourceRange};
  return true;
}

bool ngx_diffuse_albedo(uint32_t w, uint32_t h, VkImageView* view) {
  if (!g_guide[0].view || g_guide_w != w || g_guide_h != h || !g_guide[0].initialized) return false;
  *view = g_guide[0].view;
  return true;
}

static bool ensure_guides(DeviceData* dd, uint32_t w, uint32_t h) {
  if (g_guide_w == w && g_guide_h == h && g_guide[0].image) return true;
  for (auto& g : g_guide) guide_destroy(dd, g);
  retire_guide_pool();
  const VkFormat fmts[3] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT};
  for (int i = 0; i < 3; ++i)
    if (!guide_create(dd, g_guide[i], fmts[i], w, h)) {
      log("[ngx] could not create guide image %d (%ux%u)", i, w, h);
      return false;
    }
  g_guide_w = w;
  g_guide_h = h;
  log("[ngx] guide buffers %ux%u: diffuse i%u, specular i%u, normal+roughness i%u", w, h, image_id(g_guide[0].image),
      image_id(g_guide[1].image), image_id(g_guide[2].image));
  return true;
}

static bool ensure_layouts(DeviceData* dd) {
  if (g_dsl) return true;
  VkDescriptorSetLayoutBinding b[8] = {};
  for (uint32_t i = 0; i < 8; ++i) {
    b[i].binding = i;
    b[i].descriptorType = i < 4 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[i].descriptorCount = 1;
    b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dci.bindingCount = 8;
  dci.pBindings = b;
  if (dd->vk.CreateDescriptorSetLayout(dd->device, &dci, nullptr, &g_dsl) != VK_SUCCESS) return false;
  VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 96};  // sizeof(GuidePush)
  VkPipelineLayoutCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pci.setLayoutCount = 1;
  pci.pSetLayouts = &g_dsl;
  pci.pushConstantRangeCount = 1;
  pci.pPushConstantRanges = &pcr;
  if (dd->vk.CreatePipelineLayout(dd->device, &pci, nullptr, &g_pl) != VK_SUCCESS) return false;
  return true;
}

static VkPipeline build_pipeline(DeviceData* dd, const char* entry, uint64_t& gen, VkPipeline current) {
  Bg3ShaderBlob blob{};
  if (!shader_get("rr_guides.hlsl", entry, "cs_6_0", &blob)) return current;
  if (current && blob.generation == gen) return current;
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  VkShaderModule mod = VK_NULL_HANDLE;
  if (dd->vk.CreateShaderModule(dd->device, &mci, nullptr, &mod) != VK_SUCCESS) return current;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, entry};
  ci.layout = g_pl;
  VkPipeline p = VK_NULL_HANDLE;
  VkResult r = dd->vk.CreateComputePipelines(dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &p);
  dd->vk.DestroyShaderModule(dd->device, mod, nullptr);
  if (r != VK_SUCCESS) {
    log("[ngx] rr_guides.hlsl:%s pipeline creation failed (%d)", entry, r);
    return current;
  }
  if (current) defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)current);
  gen = blob.generation;
  log("[ngx] rr_guides.hlsl:%s pipeline built (generation %llu)", entry, (unsigned long long)gen);
  return p;
}

static VkImageView view_of(DeviceData* dd, VkImage img, bool depth) {
  auto it = g_views.find(img);
  if (it != g_views.end()) return it->second;
  VkFormat fmt;
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    auto rec = tracker().images.find(img);
    if (rec == tracker().images.end()) return VK_NULL_HANDLE;
    fmt = rec->second.ci.format;
  }
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = img;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = fmt;
  vi.subresourceRange = {VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1};
  VkImageView v = VK_NULL_HANDLE;
  if (dd->vk.CreateImageView(dd->device, &vi, nullptr, &v) != VK_SUCCESS) return VK_NULL_HANDLE;
  g_views[img] = v;
  return v;
}

void ngx_on_destroy_image(VkImage img) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_views.find(img);
  if (it == g_views.end()) return;
  DeviceData* dd = main_device();
  if (dd) dd->vk.DestroyImageView(dd->device, it->second, nullptr);
  g_views.erase(it);
  for (size_t i = 0; i < g_sets.size();) {
    const SetKey& k = g_sets[i].first;
    if (k.rt0 == img || k.rt1 == img || k.rt2 == img || k.depth == img || k.out == img) {
      g_sets.erase(g_sets.begin() + i);  // the set is simply abandoned to the pool
    } else ++i;
  }
}

static VkDescriptorSet get_set(DeviceData* dd, const SetKey& key, VkImageView out_view) {
  for (auto& [k, s] : g_sets)
    if (k == key) return s;
  if (g_sets.size() >= 48) {
    log("[ngx] descriptor set cache full; retiring pool without device idle");
    retire_guide_pool();
  }
  if (!g_pool && !create_guide_pool(dd)) return VK_NULL_HANDLE;
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = g_pool;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &g_dsl;
  VkDescriptorSet set = VK_NULL_HANDLE;
  VkResult allocated = dd->vk.AllocateDescriptorSets(dd->device, &ai, &set);
  if (allocated != VK_SUCCESS) {
    if (allocated != VK_ERROR_OUT_OF_POOL_MEMORY && allocated != VK_ERROR_FRAGMENTED_POOL) return VK_NULL_HANDLE;
    // Sets abandoned by destroyed G-buffers (one per level load) fill the pool.
    log("[ngx] descriptor pool exhausted; retiring pool without device idle");
    retire_guide_pool();
    if (!create_guide_pool(dd)) return VK_NULL_HANDLE;
    ai.descriptorPool = g_pool;
    if (dd->vk.AllocateDescriptorSets(dd->device, &ai, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
  }
  VkDescriptorImageInfo ii[8] = {};
  VkImage srcs[3] = {key.rt0, key.rt1, key.rt2};
  for (int i = 0; i < 3; ++i) ii[i] = {VK_NULL_HANDLE, view_of(dd, srcs[i], false), key.rt_layout};
  ii[3] = {VK_NULL_HANDLE, view_of(dd, key.depth, true), key.depth_layout};
  for (int i = 0; i < 3; ++i) ii[4 + i] = {VK_NULL_HANDLE, g_guide[i].view, VK_IMAGE_LAYOUT_GENERAL};
  ii[7] = {VK_NULL_HANDLE, out_view, VK_IMAGE_LAYOUT_GENERAL};
  for (int i = 0; i < 4; ++i)
    if (!ii[i].imageView) return VK_NULL_HANDLE;
  VkWriteDescriptorSet w[8] = {};
  uint32_t n = 0;
  for (uint32_t i = 0; i < 8; ++i) {
    if (i == 7 && !out_view) continue;
    w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w[n].dstSet = set;
    w[n].dstBinding = i;
    w[n].descriptorCount = 1;
    w[n].descriptorType = i < 4 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[n].pImageInfo = &ii[i];
    ++n;
  }
  dd->vk.UpdateDescriptorSets(dd->device, n, w, 0, nullptr);
  g_sets.push_back({key, set});
  return set;
}

static void memory_barrier(DeviceData* dd, VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                           VkAccessFlags sa, VkAccessFlags da) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = sa;
  mb.dstAccessMask = da;
  dd->vk.CmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

// ---------------------------------------------------------------------------
// Guide dispatch
// ---------------------------------------------------------------------------

// Matches rr_guides.hlsl's Push (96 bytes).
struct GuidePush {
  uint32_t size[2], out_size[2];
  float nov;
  uint32_t v2;
  uint32_t depth_inverted, show;
  float rot[3][4];   // rows of inverse(mat3(view)): n_world = n_view * inverse(R)
  float proj[4];     // P00, P11, P20, P21 of the jittered projection
};
static_assert(sizeof(GuidePush) == 96, "GuidePush must match rr_guides.hlsl");

// The camera of the G-buffer pass: set 0 binding 2 of the G-buffer vertex
// shaders (224 bytes, std140, row-major, used as v * M; FRAME_MAP/TRACK_B):
// _m0 (floats 0-15) world-to-view, _m1 (16-31) the jittered reversed-Z
// projection with the jitter in row 2, _m2 the camera-relative view-projection,
// floats 48/49 near/far, 52-54 the camera position. Read on the CPU from the
// game's mapped memory when the pass ends, the same way layer/streamline.cpp
// reads it at the velocity pass.
struct GuideCamera {
  bool ok = false;
  float raw[56] = {};
  float view[16] = {};       // world-to-view as given to RR (row-major, v * M)
  float proj[16] = {};       // view-to-clip, jitter removed (row-major, v * M)
  float inv_rot[3][4] = {};  // inverse(mat3(_m0)), rows
  float pj[4] = {};          // jittered P00, P11, P20, P21
  uint64_t frame = 0;
};
static GuideCamera g_gcam;   // under g_mu
static std::string g_gcam_err;

static bool finite_all(const float* f, int n) {
  for (int i = 0; i < n; ++i)
    if (!std::isfinite(f[i])) return false;
  return true;
}

static bool read_guide_camera(VkCommandBuffer cmd, GuideCamera& c, std::string& why) {
  Bg3Binding b{};
  if (!query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 2, 0, &b) || !b.buffer)
    return why = "set 0 binding 2 not bound", false;
  if (b.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && b.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
    return why = "set 0 binding 2 is not a uniform buffer", false;
  if (b.range != VK_WHOLE_SIZE && b.range < sizeof c.raw) return why = "uniform range < 224 bytes", false;
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    auto bm = tracker().buffer_mem.find(b.buffer);
    if (bm == tracker().buffer_mem.end()) return why = "buffer memory unknown", false;
    auto mp = tracker().mapped.find(bm->second.first);
    if (mp == tracker().mapped.end() || !mp->second) return why = "memory not mapped", false;
    memcpy(c.raw, mp->second + bm->second.second + b.offset, sizeof c.raw);
  }
  const float* v = &c.raw[0];
  const float* p = &c.raw[16];
  if (!finite_all(c.raw, 32)) return why = "non-finite matrices", false;
  // A perspective projection in the row-vector convention: clip.w = view.z.
  if (std::fabs(p[11] - 1.0f) > 1e-3f || std::fabs(p[15]) > 1e-3f || std::fabs(p[0]) < 1e-3f ||
      std::fabs(p[5]) < 1e-3f || std::fabs(p[1]) > 1e-4f || std::fabs(p[4]) > 1e-4f)
    return why = "_m1 is not the expected projection", false;
  if (std::fabs(v[3]) > 1e-4f || std::fabs(v[7]) > 1e-4f || std::fabs(v[11]) > 1e-4f || std::fabs(v[15] - 1.0f) > 1e-4f)
    return why = "_m0 is not an affine view matrix", false;
  // inverse(R) by the adjugate; R = rows 0..2, columns 0..2 of _m0.
  double r[3][3];
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) r[i][j] = v[i * 4 + j];
  double det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
               r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
  if (!(std::fabs(det) > 1e-6)) return why = "_m0 rotation is singular", false;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      // inv[i][j] = cofactor[j][i] / det
      int a0 = (j + 1) % 3, a1 = (j + 2) % 3, b0 = (i + 1) % 3, b1 = (i + 2) % 3;
      c.inv_rot[i][j] = float((r[a0][b0] * r[a1][b1] - r[a0][b1] * r[a1][b0]) / det);
    }
  for (auto& row : c.inv_rot) row[3] = 0.0f;
  memcpy(c.view, v, sizeof c.view);
  memcpy(c.proj, p, sizeof c.proj);
  c.pj[0] = p[0];
  c.pj[1] = p[5];
  c.pj[2] = p[8];
  c.pj[3] = p[9];
  c.proj[8] = 0.0f;  // unjittered: the jitter is P[2][0..1] (TRACK_B_FG.md)
  c.proj[9] = 0.0f;
  c.ok = true;
  return true;
}

static GuidePush guide_push(const SrFeature& f, uint32_t w, uint32_t h, const GuideCamera* cam = nullptr) {
  GuidePush pc{};
  pc.size[0] = w;
  pc.size[1] = h;
  pc.nov = cfg_float("rr_nov", 0.5f);
  pc.depth_inverted = (f.flags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) ? 1 : 0;
  pc.show = uint32_t(cfg_int("rr_show", 0));
  if (cam && cam->ok) {
    pc.v2 = 1;
    memcpy(pc.rot, cam->inv_rot, sizeof pc.rot);
    memcpy(pc.proj, cam->pj, sizeof pc.proj);
  }
  return pc;
}

// Guide images go UNDEFINED -> GENERAL once, then stay GENERAL.
static void guides_init_barrier(DeviceData* dd, VkCommandBuffer cmd) {
  VkImageMemoryBarrier init[3];
  uint32_t n = 0;
  for (auto& g : g_guide)
    if (!g.initialized) {
      init[n] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      init[n].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      init[n].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      init[n].newLayout = VK_IMAGE_LAYOUT_GENERAL;
      init[n].srcQueueFamilyIndex = init[n].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      init[n].image = g.image;
      init[n].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      ++n;
      g.initialized = true;
    }
  if (n)
    dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                              nullptr, 0, nullptr, n, init);
}

static void dispatch_guides(DeviceData* dd, VkCommandBuffer cmd, VkDescriptorSet set, const GuidePush& pc) {
  guides_init_barrier(dd, cmd);
  // Earlier reads of the guides (last frame's NGX) and writes of the inputs.
  memory_barrier(dd, cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
  dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe);
  dd->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl, 0, 1, &set, 0, nullptr);
  dd->vk.CmdPushConstants(cmd, g_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  dd->vk.CmdDispatch(cmd, (pc.size[0] + 7) / 8, (pc.size[1] + 7) / 8, 1);
  memory_barrier(dd, cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
  restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);  // the game's compute state survives us
}

static uint64_t g_guides_frame = 0;

// Called right after a G-buffer pass ends, in the game's command buffer and
// outside any render pass: the only moment RT0-RT2 are known to hold the
// G-buffer (their memory is reused before the DLSS evaluate). The RTs are left
// in COLOR_ATTACHMENT_OPTIMAL by the pass, depth in its final layout; both are
// moved to read-only layouts for the dispatch and back afterwards.
static void record_guides_at_gbuffer(VkCommandBuffer cmd, const Bg3PassInfo& p, const VkImageLayout* layouts,
                                     VkImageLayout depth_layout) {
  if (!cfg_int("rr", 1) || cfg_int("rr_guides_at", 1) != 1) return;
  DeviceData* dd = device_of(cmd);
  if (!dd || !p.depth_image) return;
  std::lock_guard<std::mutex> lock(g_mu);
  const SrFeature* f = nullptr;
  for (auto& x : g_feats)
    if (x.w == p.width && x.h == p.height && !x.rr_failed) f = &x;
  if (!f) return;
  if (!ensure_layouts(dd) || !ensure_guides(dd, p.width, p.height)) return;
  g_pipe = build_pipeline(dd, "main", g_pipe_gen, g_pipe);
  if (!g_pipe) return;

  bool known = depth_layout != VK_IMAGE_LAYOUT_UNDEFINED;
  for (int i = 0; i < 3; ++i) known &= layouts[i] != VK_IMAGE_LAYOUT_UNDEFINED;
  const VkImageLayout rt_read = known ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
  const VkImageLayout depth_read = known ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
  SetKey key{p.color_images[0], p.color_images[1], p.color_images[2], p.depth_image, VK_NULL_HANDLE, rt_read,
             depth_read};
  VkDescriptorSet set = get_set(dd, key, VK_NULL_HANDLE);
  if (!set) return;

  VkImageMemoryBarrier b[4] = {};
  for (int i = 0; i < 4; ++i) {
    b[i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b[i].srcQueueFamilyIndex = b[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b[i].image = i < 3 ? p.color_images[i] : p.depth_image;
    b[i].oldLayout = i < 3 ? layouts[i] : depth_layout;
    b[i].newLayout = i < 3 ? rt_read : depth_read;
    b[i].srcAccessMask = i < 3 ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    b[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b[i].subresourceRange = {VkImageAspectFlags(i < 3 ? VK_IMAGE_ASPECT_COLOR_BIT
                                                      : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT),
                             0, 1, 0, 1};
  }
  const VkPipelineStageFlags att = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  if (known)
    dd->vk.CmdPipelineBarrier(cmd, att, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 4, b);
  const GuideCamera* cam = nullptr;
  if (cfg_int("rr_guides_v2", 1)) {
    GuideCamera c;
    std::string why;
    if (read_guide_camera(cmd, c, why)) {
      c.frame = frame_index();
      g_gcam = c;
      cam = &g_gcam;
      static int logged = 0;
      static uint64_t traced_frame = 0;
      CmdState* cs = cmd_state(cmd);
      const bool tracing = cs && cs->tracing && traced_frame != frame_index();   // the camera of every trace
      if (tracing) traced_frame = frame_index();
      if (logged++ < 2 || tracing || (cfg_int("rr_log_camera", 0) && frame_index() % 600 == 0)) {
        const float* v = c.raw;
        const float* q = c.raw + 16;
        log("[ngx] guide camera _m0 [%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f "
            "%.4f]",
            v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15]);
        log("[ngx] guide camera _m1 [%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f "
            "%.4f], near/far %g %g, pos %g %g %g",
            q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10], q[11], q[12], q[13], q[14], q[15],
            c.raw[48], c.raw[49], c.raw[52], c.raw[53], c.raw[54]);
        log("[ngx] guide camera inverse(R) rows [%.4f %.4f %.4f | %.4f %.4f %.4f | %.4f %.4f %.4f]", c.inv_rot[0][0],
            c.inv_rot[0][1], c.inv_rot[0][2], c.inv_rot[1][0], c.inv_rot[1][1], c.inv_rot[1][2], c.inv_rot[2][0],
            c.inv_rot[2][1], c.inv_rot[2][2]);
      }
      g_gcam_err.clear();
    } else if (why != g_gcam_err) {
      g_gcam_err = why;
      log("[ngx] guide camera unavailable (%s): this frame uses the v1 guides", why.c_str());
    }
  }
  dispatch_guides(dd, cmd, set, guide_push(*f, p.width, p.height, cam));
  if (known) {
    for (auto& x : b) {
      std::swap(x.oldLayout, x.newLayout);
      x.srcAccessMask = 0;
      x.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }
    dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                              nullptr, 0, nullptr, 4, b);
  }
  g_guides_frame = frame_index();
  static bool said = false;
  if (!said) {
    said = true;
    log("[ngx] guides recorded at the G-buffer pass end (RT layouts %d %d %d -> %d, depth %d -> %d)", layouts[0],
        layouts[1], layouts[2], rt_read, depth_layout, depth_read);
  }
}

// ---------------------------------------------------------------------------
// GPU timing
// ---------------------------------------------------------------------------

static void ts_init(DeviceData* dd) {
  if (g_qpool || !dd->vk.CreateQueryPool) return;
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qi.queryCount = kTsSlots * 3;
  if (dd->vk.CreateQueryPool(dd->device, &qi, nullptr, &g_qpool) != VK_SUCCESS) g_qpool = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties props;
  dd->inst->GetPhysicalDeviceProperties(dd->physical, &props);
  g_ts_period = props.limits.timestampPeriod;
}

// Harvests a finished slot, then resets it for this evaluate. Returns the slot.
static uint32_t ts_begin(DeviceData* dd, VkCommandBuffer cmd, int mode) {
  uint32_t slot = g_ts_next++ % kTsSlots;
  if (g_ts_pending[slot]) {
    uint64_t r[6] = {};
    VkResult res = dd->vk.GetQueryPoolResults(dd->device, g_qpool, slot * 3, 3, sizeof r, r, 2 * sizeof(uint64_t),
                                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (res == VK_SUCCESS && r[1] && r[3] && r[5]) {
      int m = g_ts_mode[slot];
      g_acc_guides[m] += double(r[2] - r[0]) * g_ts_period * 1e-6;
      g_acc_eval[m] += double(r[4] - r[2]) * g_ts_period * 1e-6;
      ++g_acc_n[m];
    }
  }
  dd->vk.CmdResetQueryPool(cmd, g_qpool, slot * 3, 3);
  dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qpool, slot * 3);
  g_ts_mode[slot] = mode;
  g_ts_pending[slot] = true;
  return slot;
}

static void ts_mark(DeviceData* dd, VkCommandBuffer cmd, uint32_t slot, uint32_t i) {
  dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qpool, slot * 3 + i);
}

void ngx_on_present() {
  rrpost_on_present();
  std::lock_guard<std::mutex> lock(g_mu);
  ULONGLONG now = GetTickCount64();
  if (!g_acc_tick) g_acc_tick = now;
  if (now - g_acc_tick < 5000) return;
  g_acc_tick = now;
  static const char* names[2] = {"SR", "RR"};
  for (int m = 0; m < 2; ++m) {
    if (!g_acc_n[m]) continue;
    log("[ngx] gpu %s: guides %.3f ms + evaluate %.3f ms = %.3f ms (avg of %u)", names[m], g_acc_guides[m] / g_acc_n[m],
        g_acc_eval[m] / g_acc_n[m], (g_acc_guides[m] + g_acc_eval[m]) / g_acc_n[m], g_acc_n[m]);
    g_acc_guides[m] = g_acc_eval[m] = 0;
    g_acc_n[m] = 0;
  }
  if (g_feats.empty() && g_ngx_mod && frame_index() > 2000) {
    static bool said = false;
    if (!said) log("[ngx] no DLSS feature has been created through _nvngx.dll after %llu presents",
                   (unsigned long long)frame_index());
    said = true;
  }
}

// ---------------------------------------------------------------------------
// Contract logging
// ---------------------------------------------------------------------------

static void log_create(const char* via, NVSDK_NGX_Feature f, const NVSDK_NGX_Parameter* p, NVSDK_NGX_Result r,
                       NVSDK_NGX_Handle* h) {
  log("[ngx] %s feature %d -> %s (0x%08x) handle %p", via, int(f), result_name(r), unsigned(r), (void*)h);
  if (!p) return;
  unsigned u;
  int i;
  const char* ukeys[] = {NVSDK_NGX_Parameter_Width, NVSDK_NGX_Parameter_Height, NVSDK_NGX_Parameter_OutWidth,
                         NVSDK_NGX_Parameter_OutHeight, NVSDK_NGX_Parameter_CreationNodeMask,
                         NVSDK_NGX_Parameter_VisibilityNodeMask,
                         NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
                         NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                         NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
                         NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                         NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
                         NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality};
  for (const char* k : ukeys)
    if (get_ui(p, k, u)) log("[ngx]   %-40s = %u", k, u);
  const char* ikeys[] = {NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
                         NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects};
  for (const char* k : ikeys)
    if (get_i(p, k, i)) log("[ngx]   %-40s = %d", k, i);
  if (get_i(p, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, i))
    log("[ngx]   flags:%s%s%s%s%s%s", (i & NVSDK_NGX_DLSS_Feature_Flags_IsHDR) ? " IsHDR" : "",
        (i & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) ? " MVLowRes" : "",
        (i & NVSDK_NGX_DLSS_Feature_Flags_MVJittered) ? " MVJittered" : "",
        (i & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) ? " DepthInverted" : "",
        (i & NVSDK_NGX_DLSS_Feature_Flags_AutoExposure) ? " AutoExposure" : "",
        (i & NVSDK_NGX_DLSS_Feature_Flags_AlphaUpscaling) ? " AlphaUpscaling" : "");
}

static void log_eval(const SrFeature& f, const NVSDK_NGX_Parameter* p) {
  log("[ngx] SR evaluate contract (handle %p, created %ux%u -> %ux%u, pq %d, flags 0x%x):", (void*)f.sr, f.w, f.h,
      f.ow, f.oh, f.pq, f.flags);
  log_res("Color", get_res(p, NVSDK_NGX_Parameter_Color));
  log_res("Depth", get_res(p, NVSDK_NGX_Parameter_Depth));
  log_res("MotionVectors", get_res(p, NVSDK_NGX_Parameter_MotionVectors));
  log_res("Output", get_res(p, NVSDK_NGX_Parameter_Output));
  log_res("ExposureTexture", get_res(p, NVSDK_NGX_Parameter_ExposureTexture));
  log_res("BiasColorMask", get_res(p, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask));
  log_res("TransparencyMask", get_res(p, NVSDK_NGX_Parameter_TransparencyMask));
  float v;
  const char* fkeys[] = {NVSDK_NGX_Parameter_Jitter_Offset_X, NVSDK_NGX_Parameter_Jitter_Offset_Y,
                         NVSDK_NGX_Parameter_MV_Scale_X, NVSDK_NGX_Parameter_MV_Scale_Y,
                         NVSDK_NGX_Parameter_DLSS_Pre_Exposure, NVSDK_NGX_Parameter_DLSS_Exposure_Scale,
                         NVSDK_NGX_Parameter_Sharpness, NVSDK_NGX_Parameter_FrameTimeDeltaInMsec};
  for (const char* k : fkeys)
    if (get_f(p, k, v)) log("[ngx]   %-40s = %g", k, v);
  int i;
  unsigned u;
  if (get_i(p, NVSDK_NGX_Parameter_Reset, i)) log("[ngx]   %-40s = %d", NVSDK_NGX_Parameter_Reset, i);
  const char* ukeys[] = {NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,
                         NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,
                         NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,
                         NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,
                         NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y,
                         NVSDK_NGX_Parameter_Width, NVSDK_NGX_Parameter_Height, NVSDK_NGX_Parameter_OutWidth,
                         NVSDK_NGX_Parameter_OutHeight};
  for (const char* k : ukeys)
    if (get_ui(p, k, u)) log("[ngx]   %-40s = %u", k, u);
  GBuffer gb;
  {
    std::lock_guard<std::mutex> lock(g_gbuf_mu);
    gb = g_gbuf;
  }
  log("[ngx]   G-buffer seen: RT0 i%u RT1 i%u RT2 i%u %ux%u (frame %llu)", image_id(gb.rt[0]), image_id(gb.rt[1]),
      image_id(gb.rt[2]), gb.w, gb.h, (unsigned long long)gb.seen_frame);
}

// ---------------------------------------------------------------------------
// Ray Reconstruction
// ---------------------------------------------------------------------------

static bool rr_create(DeviceData* dd, VkCommandBuffer cmd, SrFeature& f) {
  if (!g_alloc_params) {
    log("[ngx] RR: NVSDK_NGX_VULKAN_AllocateParameters not found");
    return false;
  }
  // Test switch for the fallback: RR creation "fails", SR runs, ngx_rr_active()
  // stays false and the core's effects must use their non-RR modes.
  if (cfg_int("rr_test_fail", 0)) {
    log("[ngx] rr_test_fail=1: RR creation refused (test)");
    return false;
  }
  NVSDK_NGX_Parameter* p = nullptr;
  if (g_alloc_params(&p) != NVSDK_NGX_Result_Success || !p) {
    log("[ngx] RR: AllocateParameters failed");
    return false;
  }
  unsigned preset = unsigned(cfg_int("rr_preset", 6));
  p->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
  p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
  p->Set(NVSDK_NGX_Parameter_Width, f.w);
  p->Set(NVSDK_NGX_Parameter_Height, f.h);
  p->Set(NVSDK_NGX_Parameter_OutWidth, f.ow);
  p->Set(NVSDK_NGX_Parameter_OutHeight, f.oh);
  p->Set(NVSDK_NGX_Parameter_PerfQualityValue, f.pq);
  int flags = f.flags & ~int(NVSDK_NGX_DLSS_Feature_Flags_AutoExposure);  // not supported by RR
  p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, flags);
  p->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, f.subrects ? 1 : 0);
  p->Set(NVSDK_NGX_Parameter_DLSS_Denoise_Mode, int(NVSDK_NGX_DLSS_Denoise_Mode_DLUnified));
  p->Set(NVSDK_NGX_Parameter_DLSS_Roughness_Mode, unsigned(NVSDK_NGX_DLSS_Roughness_Mode_Packed));
  p->Set(NVSDK_NGX_Parameter_Use_HW_Depth, unsigned(NVSDK_NGX_DLSS_Depth_Type_HW));
  const char* presets[] = {NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA,
                           NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality,
                           NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced,
                           NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance,
                           NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance,
                           NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraQuality};
  for (const char* k : presets) p->Set(k, preset);

  NVSDK_NGX_Result r;
  {
    Nest n;
    if (g_h_create1.active) r = orig_create1()(dd->device, cmd, NVSDK_NGX_Feature_RayReconstruction, p, &f.rr);
    else r = orig_create()(cmd, NVSDK_NGX_Feature_RayReconstruction, p, &f.rr);
  }
  log("[ngx] RR create %ux%u -> %ux%u pq %d flags 0x%x preset %u: %s (0x%08x) handle %p", f.w, f.h, f.ow, f.oh, f.pq,
      flags, preset, result_name(r), unsigned(r), (void*)f.rr);
  if (r != NVSDK_NGX_Result_Success) {
    f.rr = nullptr;
    return false;
  }
  log_dlssd_module();
  return true;
}

// Fake pass around our guide images so the tracer dumps them (PNG + stats).
// gbuf (optional): the G-buffer RT0-RT2 as they are at evaluate time.
static void trace_images(VkCommandBuffer cmd, const VkImage* imgs, int count, const char* tag);
static void trace_guides(VkCommandBuffer cmd, VkImage extra, const VkImage* gbuf, const char* tag,
                         const VkImage* more = nullptr, int more_n = 0) {
  VkImage imgs[7] = {g_guide[0].image, g_guide[1].image, g_guide[2].image, extra,
                     gbuf ? gbuf[0] : VK_NULL_HANDLE, gbuf ? gbuf[1] : VK_NULL_HANDLE, gbuf ? gbuf[2] : VK_NULL_HANDLE};
  trace_images(cmd, imgs, 7, tag);
  if (more_n) trace_images(cmd, more, more_n, "rrpost");
}

static void trace_images(VkCommandBuffer cmd, const VkImage* all, int count, const char* tag) {
  CmdState* s = cmd_state(cmd);
  if (!s || !s->tracing) return;
  Bg3PassInfo saved = s->pass;
  VkImageLayout saved_layouts[8];
  memcpy(saved_layouts, s->color_layouts, sizeof saved_layouts);
  VkImageLayout saved_depth = s->depth_layout;
  Bg3PassInfo p{};
  p.ordinal = s->pass_ordinal++;
  p.draw_count = 1;
  for (int i = 0; i < count; ++i) {
    VkImage img = all[i];
    if (!img || p.color_count >= 8) continue;
    ImageRec rec{};
    {
      std::shared_lock<std::shared_mutex> lock(tracker().mu);
      auto it = tracker().images.find(img);
      if (it == tracker().images.end()) continue;
      rec = it->second;
    }
    p.color_images[p.color_count] = img;
    p.color_formats[p.color_count] = rec.ci.format;
    s->color_layouts[p.color_count] = VK_IMAGE_LAYOUT_GENERAL;
    ++p.color_count;
    p.width = (std::max)(p.width, rec.ci.extent.width);
    p.height = (std::max)(p.height, rec.ci.extent.height);
  }
  s->pass = p;
  s->depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  trace_flush_segment(s);
  std::string j = "{\"op\":\"pass\",\"ordinal\":" + std::to_string(p.ordinal) + ",\"w\":" + std::to_string(p.width) +
                  ",\"h\":" + std::to_string(p.height) + ",\"tag\":\"" + tag + "\",\"color\":[";
  for (uint32_t i = 0; i < p.color_count; ++i) j += (i ? ",\"" : "\"") + image_ref(p.color_images[i]) + "\"";
  j += "],\"depth\":\"\"}";
  trace_event(s, j);
  std::string dumps = trace_dump_pass(cmd, s);
  trace_event(s, "{\"op\":\"end\",\"draws\":1,\"dumps\":[" + dumps + "]}");
  s->pass = saved;
  memcpy(s->color_layouts, saved_layouts, sizeof saved_layouts);
  s->depth_layout = saved_depth;
}

// Records guides + RR evaluate. Returns false when RR cannot run this frame
// (the caller then forwards the game's SR evaluate unchanged).
// mode: in, the feature's last mode (-1 none, 0 SR, 1 RR on the graded colour,
// 2 RR before post); out, the mode this frame ran in.
static bool rr_evaluate(DeviceData* dd, VkCommandBuffer cmd, SrFeature& f, NVSDK_NGX_Parameter* p,
                        PFN_NVSDK_NGX_ProgressCallback cb, int& mode, uint32_t ts_slot, NVSDK_NGX_Result& result) {
  const int last_mode = mode;
  static int refusals = 0;
  auto refuse = [&](const char* why) {
    if (refusals++ < 8) log("[ngx] RR skipped this frame: %s", why);
    return false;
  };
  NVSDK_NGX_Resource_VK* color = get_res(p, NVSDK_NGX_Parameter_Color);
  NVSDK_NGX_Resource_VK* depth = get_res(p, NVSDK_NGX_Parameter_Depth);
  NVSDK_NGX_Resource_VK* out = get_res(p, NVSDK_NGX_Parameter_Output);
  if (!color || !depth || !out || color->Type != NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW ||
      depth->Type != NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)
    return refuse("colour, depth or output missing from the parameter block");
  GBuffer gb;
  {
    std::lock_guard<std::mutex> lock(g_gbuf_mu);
    gb = g_gbuf;
  }
  const uint32_t w = f.w, h = f.h;
  if (!gb.rt[0] || gb.w != w || gb.h != h) return refuse("no G-buffer pass at the render resolution seen yet");
  if (!ensure_layouts(dd) || !ensure_guides(dd, w, h)) return refuse("guide resources unavailable");
  g_pipe = build_pipeline(dd, "main", g_pipe_gen, g_pipe);
  if (!g_pipe) return refuse("rr_guides.hlsl has not compiled");

  int show = cfg_int("rr_show", 0);
  VkImage out_img = out->Resource.ImageViewInfo.Image;
  VkImageView out_view = VK_NULL_HANDLE;
  if (show) {
    VkFormat of = VkFormat(out->Resource.ImageViewInfo.Format);
    if (of == VK_FORMAT_B10G11R11_UFLOAT_PACK32 && out->ReadWrite) {
      g_pipe_show = build_pipeline(dd, "show", g_show_gen, g_pipe_show);
      out_view = view_of(dd, out_img, false);
    }
    if (!out_view || !g_pipe_show) show = 0;
  }
  // Guides were normally built at the end of the G-buffer pass (rr_guides_at=1,
  // see record_guides_at_gbuffer). At evaluate time the G-buffer memory has
  // already been reused by later passes, so rr_guides_at=0 is a debug mode.
  const bool late = cfg_int("rr_guides_at", 1) == 0;
  const bool fresh = g_guides_frame + 2 >= frame_index() && g_guides_frame != 0;
  if (!late && !fresh) return refuse("no guides recorded at the G-buffer pass this frame");
  SetKey key{gb.rt[0], gb.rt[1], gb.rt[2], depth->Resource.ImageViewInfo.Image, out_view ? out_img : VK_NULL_HANDLE,
             VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL};
  VkDescriptorSet set = (late || show) ? get_set(dd, key, out_view) : VK_NULL_HANDLE;
  if ((late || show) && !set) return refuse("descriptor set for the G-buffer could not be built");

  if (!f.rr && !f.rr_failed) {
    if (!rr_create(dd, cmd, f)) {
      f.rr_failed = true;
      log("[ngx] RR creation failed; staying on SR for this feature");
      return false;
    }
  }
  if (!f.rr) return false;

  // RR at the start of post (rr_post=1, layer/rrpost.cpp): RR denoises the
  // ungraded, pre-DoF colour and the game's post chain is replayed after it.
  // Otherwise (rr_post=0, rr_show, or a frame whose chain was not followed) RR
  // gets the game's graded colour, as before.
  RrPostInputs post{};
  const uint32_t ow = out->Resource.ImageViewInfo.Width, oh = out->Resource.ImageViewInfo.Height;
  bool use_post = false;
  if (cfg_int("rr_post", 1) && !show) {
    use_post = rrpost_take(cmd, w, h, ow, oh, &post);
    static int post_refusals = 0;
    if (!use_post && post_refusals++ < 8) log("[ngx] RR before post unavailable this frame (%s)", post.why);
  }
  mode = use_post ? 2 : 1;
  const bool reset = last_mode != -1 && last_mode != mode;
  if (CmdState* ts = cmd_state(cmd); ts && ts->tracing)
    log("[ngx] traced frame %llu: %s%s%s", (unsigned long long)frame_index(),
        use_post ? "RR before post" : "RR on the graded colour", use_post ? "" : " - ", use_post ? "" : post.why);

  GuidePush pc = guide_push(f, w, h);
  pc.out_size[0] = out->Resource.ImageViewInfo.Width;
  pc.out_size[1] = out->Resource.ImageViewInfo.Height;
  pc.show = uint32_t(show);
  if (late) dispatch_guides(dd, cmd, set, pc);
  // Makes the guide writes (this or an earlier command buffer) visible to NGX.
  memory_barrier(dd, cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                 VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
  if (g_qpool) ts_mark(dd, cmd, ts_slot, 1);

  // ---- evaluate ----
  p->Set(NVSDK_NGX_Parameter_DiffuseAlbedo, static_cast<void*>(&g_guide[0].res));
  p->Set(NVSDK_NGX_Parameter_SpecularAlbedo, static_cast<void*>(&g_guide[1].res));
  p->Set(NVSDK_NGX_Parameter_GBuffer_Normals, static_cast<void*>(&g_guide[2].res));
  p->Set(NVSDK_NGX_Parameter_GBuffer_Roughness, static_cast<void*>(&g_guide[2].res));
  const char* zero_keys[] = {NVSDK_NGX_Parameter_DLSS_Input_DiffuseAlbedo_Subrect_Base_X,
                             NVSDK_NGX_Parameter_DLSS_Input_DiffuseAlbedo_Subrect_Base_Y,
                             NVSDK_NGX_Parameter_DLSS_Input_SpecularAlbedo_Subrect_Base_X,
                             NVSDK_NGX_Parameter_DLSS_Input_SpecularAlbedo_Subrect_Base_Y,
                             NVSDK_NGX_Parameter_DLSS_Input_Normals_Subrect_Base_X,
                             NVSDK_NGX_Parameter_DLSS_Input_Normals_Subrect_Base_Y,
                             NVSDK_NGX_Parameter_DLSS_Input_Roughness_Subrect_Base_X,
                             NVSDK_NGX_Parameter_DLSS_Input_Roughness_Subrect_Base_Y};
  for (const char* k : zero_keys) p->Set(k, 0u);
  // Camera matrices (row-major, left multiplication, as the RR guide 3.4.9
  // asks), unjittered, from the G-buffer pass of this frame. They go with the
  // world-space normals; without them (v1, or no camera read) the keys are
  // cleared, since the game's parameter block is reused between frames.
  static float s_w2v[16], s_v2c[16];
  const bool cam_ok = cfg_int("rr_guides_v2", 1) && g_gcam.ok && g_gcam.frame + 2 >= frame_index();
  if (cam_ok) {
    memcpy(s_w2v, g_gcam.view, sizeof s_w2v);
    memcpy(s_v2c, g_gcam.proj, sizeof s_v2c);
  }
  p->Set(NVSDK_NGX_Parameter_DLSS_WORLD_TO_VIEW_MATRIX, cam_ok ? static_cast<void*>(s_w2v) : nullptr);
  p->Set(NVSDK_NGX_Parameter_DLSS_VIEW_TO_CLIP_MATRIX, cam_ok ? static_cast<void*>(s_v2c) : nullptr);
  // D-RF: the specular hit distance of the reflection rays (with the matrices,
  // what RR needs to reproject reflections). Only while fresh and at the render
  // resolution; cleared otherwise, since the parameter block is reused.
  static NVSDK_NGX_Resource_VK s_shd;
  bool shd_ok = false;
  {
    std::lock_guard<std::mutex> lock(g_shd_mu);
    shd_ok = cfg_int("rr_spec_hit_distance", 1) && g_shd_frame && g_shd_frame + 2 >= frame_index() &&
             g_shd_res.Resource.ImageViewInfo.Width == w && g_shd_res.Resource.ImageViewInfo.Height == h;
    if (shd_ok) s_shd = g_shd_res;
  }
  p->Set(NVSDK_NGX_Parameter_DLSSD_SpecularHitDistance, shd_ok ? static_cast<void*>(&s_shd) : nullptr);
  if (shd_ok) {
    p->Set(NVSDK_NGX_Parameter_DLSSD_SpecularHitDistance_Subrect_Base_X, 0u);
    p->Set(NVSDK_NGX_Parameter_DLSSD_SpecularHitDistance_Subrect_Base_Y, 0u);
    if (!g_shd_logged) {
      g_shd_logged = true;
      log("[ngx] RR gets the specular hit distance (D-RF), %ux%u format %d", w, h,
          int(s_shd.Resource.ImageViewInfo.Format));
    }
  }
  // RR before post: our colour, output and colour-before-transparency guide
  // (RR guide 3.4.11; same format as the colour, both pre-exposed). The game's
  // resources are put back after the evaluate (the block is reused).
  auto res_of = [](const RrPostImage& im, bool rw) {
    NVSDK_NGX_Resource_VK r{};
    r.Resource.ImageViewInfo.ImageView = im.view;
    r.Resource.ImageViewInfo.Image = im.image;
    r.Resource.ImageViewInfo.SubresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    r.Resource.ImageViewInfo.Format = im.format;
    r.Resource.ImageViewInfo.Width = im.w;
    r.Resource.ImageViewInfo.Height = im.h;
    r.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    r.ReadWrite = rw;
    return r;
  };
  static NVSDK_NGX_Resource_VK s_color, s_out, s_cbt;
  const bool cbt_ok = use_post && post.cbt.image && cfg_int("rr_cbt", 1);
  if (use_post) {
    s_color = res_of(post.color, false);
    s_out = res_of(post.output, true);
    p->Set(NVSDK_NGX_Parameter_Color, static_cast<void*>(&s_color));
    p->Set(NVSDK_NGX_Parameter_Output, static_cast<void*>(&s_out));
  }
  if (cbt_ok) {
    s_cbt = res_of(post.cbt, false);
    p->Set(NVSDK_NGX_Parameter_DLSSD_ColorBeforeTransparency_Subrect_Base_X, 0u);
    p->Set(NVSDK_NGX_Parameter_DLSSD_ColorBeforeTransparency_Subrect_Base_Y, 0u);
  }
  p->Set(NVSDK_NGX_Parameter_DLSSD_ColorBeforeTransparency, cbt_ok ? static_cast<void*>(&s_cbt) : nullptr);
  // Screen-space SSS guide (RR guide 3.4.12): lum(after - before * albedo) of
  // BG3's skin blur (6f535075/d1669194), 0 elsewhere. Same space as the colour
  // only with rr_post=1 (before the grade), so only then.
  static NVSDK_NGX_Resource_VK s_sss;
  const bool sss_ok = use_post && post.sss.image && cfg_int("rr_sss", 1);
  if (sss_ok) {
    s_sss = res_of(post.sss, false);
    p->Set(NVSDK_NGX_Parameter_DLSSD_ScreenSpaceSubsurfaceScatteringGuide_Subrect_Base_X, 0u);
    p->Set(NVSDK_NGX_Parameter_DLSSD_ScreenSpaceSubsurfaceScatteringGuide_Subrect_Base_Y, 0u);
  }
  p->Set(NVSDK_NGX_Parameter_DLSSD_ScreenSpaceSubsurfaceScatteringGuide, sss_ok ? static_cast<void*>(&s_sss) : nullptr);
  // Pre-exposure: the RR guide 3.7 says RR does not support the exposure
  // parameters, so the game's own value (1) stays.
  static bool said_post = false;
  if (use_post && !said_post) {
    said_post = true;
    log("[ngx] RR before post: colour %ux%u fmt %d (pre-DoF, pre-grade), output %ux%u RGBA16F, CBT %s, SSS guide %s, "
        "pre-exposure game's (1)",
        post.color.w, post.color.h, int(post.color.format), ow, oh, cbt_ok ? "yes" : "no", sss_ok ? "yes" : "no");
  }

  int old_reset = 0;
  if (cfg_int("pan_inputs", 0) && cfg_int("pan_capture", 0) > 0) {
    const char* keys[] = {NVSDK_NGX_Parameter_Color, NVSDK_NGX_Parameter_Depth,
                          NVSDK_NGX_Parameter_MotionVectors};
    const char* names[] = {"rr_color", "rr_depth", "rr_mvec"};
    for (unsigned i = 0; i < 3; ++i) {
      auto* r = get_res(p, keys[i]);
      if (!r || r->Type != NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW) continue;
      const auto& im = r->Resource.ImageViewInfo;
      Bg3Binding b{};
      b.image = im.Image; b.view = im.ImageView; b.format = VkFormat(im.Format);
      b.width = im.Width; b.height = im.Height;
      b.layout = i == 0 && use_post ? VK_IMAGE_LAYOUT_GENERAL : sl_input_layout(cmd, b.image,
          i == 1 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      pan_capture(cmd, names[i], &b);
    }
  }
  bool had_reset = get_i(p, NVSDK_NGX_Parameter_Reset, old_reset);
  auto dump_mv = [&](const char* name) {
    if (!cfg_int("pan_inputs", 0) || cfg_int("pan_capture", 0) <= 0) return;
    auto* mv = get_res(p, NVSDK_NGX_Parameter_MotionVectors);
    if (!mv || mv->Type != NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW) return;
    const auto& im = mv->Resource.ImageViewInfo;
    Bg3Binding b{};
    b.image = im.Image; b.view = im.ImageView; b.format = VkFormat(im.Format);
    b.width = im.Width; b.height = im.Height;
    b.layout = sl_input_layout(cmd, b.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    pan_capture(cmd, name, &b);
  };
  if (reset) p->Set(NVSDK_NGX_Parameter_Reset, 1);
  {
    Nest n;
    result = orig_eval()(cmd, f.rr, p, cb);
  }
  dump_mv("rr_mvec_after");
  if (reset) p->Set(NVSDK_NGX_Parameter_Reset, had_reset ? old_reset : 0);
  if (use_post) {
    p->Set(NVSDK_NGX_Parameter_Color, static_cast<void*>(color));
    p->Set(NVSDK_NGX_Parameter_Output, static_cast<void*>(out));
  }
  static int eval_logs = 0;
  if (result != NVSDK_NGX_Result_Success && eval_logs++ < 8)
    log("[ngx] RR evaluate failed: %s (0x%08x)", result_name(result), unsigned(result));
  if (g_qpool) ts_mark(dd, cmd, ts_slot, 2);
  // The game's post chain after RR, at output resolution, into its DLSS output.
  if (use_post && result == NVSDK_NGX_Result_Success && !rrpost_final(cmd, out->Resource.ImageViewInfo.Image, VkFormat(out->Resource.ImageViewInfo.Format), ow, oh))
    log("[ngx] RR before post: the output-resolution grade could not be recorded");
  dump_mv("rr_mvec_after_post");

  if (show && result == NVSDK_NGX_Result_Success) {
    memory_barrier(dd, cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe_show);
    dd->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl, 0, 1, &set, 0, nullptr);
    dd->vk.CmdPushConstants(cmd, g_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
    dd->vk.CmdDispatch(cmd, (pc.out_size[0] + 7) / 8, (pc.out_size[1] + 7) / 8, 1);
    memory_barrier(dd, cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT);
    restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  }
  VkImage more[8];
  int more_n = use_post ? rrpost_trace_images(more, 8) : 0;
  trace_guides(cmd, out_img, cfg_int("rr_dump_gbuffer", 0) ? gb.rt : nullptr, "rr", more, more_n);
  return result == NVSDK_NGX_Result_Success;
}

// ---------------------------------------------------------------------------
// Detours
// ---------------------------------------------------------------------------

static SrFeature* find_feature(const NVSDK_NGX_Handle* h) {
  for (auto& f : g_feats)
    if (f.sr == h) return &f;
  return nullptr;
}

static void note_create(NVSDK_NGX_Feature feature, NVSDK_NGX_Parameter* p, NVSDK_NGX_Result r, NVSDK_NGX_Handle* h,
                        const char* via) {
  log_create(via, feature, p, r, h);
  if (feature != NVSDK_NGX_Feature_SuperSampling || r != NVSDK_NGX_Result_Success || !h || !p) return;
  std::lock_guard<std::mutex> lock(g_mu);
  SrFeature f;
  f.sr = h;
  get_ui(p, NVSDK_NGX_Parameter_Width, f.w);
  get_ui(p, NVSDK_NGX_Parameter_Height, f.h);
  get_ui(p, NVSDK_NGX_Parameter_OutWidth, f.ow);
  get_ui(p, NVSDK_NGX_Parameter_OutHeight, f.oh);
  get_i(p, NVSDK_NGX_Parameter_PerfQualityValue, f.pq);
  get_i(p, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, f.flags);
  get_i(p, NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, f.subrects);
  g_feats.push_back(f);
}

static NVSDK_NGX_Result Detour_Create(VkCommandBuffer cmd, NVSDK_NGX_Feature feature, NVSDK_NGX_Parameter* p,
                                      NVSDK_NGX_Handle** out) {
  if (t_nest) return orig_create()(cmd, feature, p, out);
  NVSDK_NGX_Result r;
  {
    Nest n;
    r = orig_create()(cmd, feature, p, out);
  }
  note_create(feature, p, r, out ? *out : nullptr, "CreateFeature");
  return r;
}

static NVSDK_NGX_Result Detour_Create1(VkDevice dev, VkCommandBuffer cmd, NVSDK_NGX_Feature feature,
                                       NVSDK_NGX_Parameter* p, NVSDK_NGX_Handle** out) {
  if (t_nest) return orig_create1()(dev, cmd, feature, p, out);
  NVSDK_NGX_Result r;
  {
    Nest n;
    r = orig_create1()(dev, cmd, feature, p, out);
  }
  note_create(feature, p, r, out ? *out : nullptr, "CreateFeature1");
  return r;
}

static NVSDK_NGX_Result Detour_Release(NVSDK_NGX_Handle* h) {
  if (t_nest) return orig_release()(h);
  NVSDK_NGX_Handle* rr = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    for (size_t i = 0; i < g_feats.size(); ++i)
      if (g_feats[i].sr == h) {
        rr = g_feats[i].rr;
        g_feats.erase(g_feats.begin() + i);
        break;
      }
  }
  Nest n;
  if (rr) {
    NVSDK_NGX_Result rr_r = orig_release()(rr);
    log("[ngx] RR feature %p released with its SR feature %p: %s", (void*)rr, (void*)h, result_name(rr_r));
  }
  NVSDK_NGX_Result r = orig_release()(h);
  log("[ngx] ReleaseFeature %p -> %s", (void*)h, result_name(r));
  return r;
}

static NVSDK_NGX_Result Detour_Eval(VkCommandBuffer cmd, const NVSDK_NGX_Handle* h, const NVSDK_NGX_Parameter* cp,
                                    PFN_NVSDK_NGX_ProgressCallback cb) {
  if (t_nest) return orig_eval()(cmd, h, cp, cb);
  Nest n;
  DeviceData* dd = device_of(cmd);
  std::lock_guard<std::mutex> lock(g_mu);
  SrFeature* f = find_feature(h);
  if (!f || !dd || !cp) {
    static int unknown = 0;
    if (unknown++ < 4) log("[ngx] evaluate of an untracked feature %p; forwarded", (void*)h);
    return orig_eval()(cmd, h, cp, cb);
  }
  auto* p = const_cast<NVSDK_NGX_Parameter*>(cp);
  ++f->evals;
  if (NVSDK_NGX_Resource_VK* mv = get_res(p, NVSDK_NGX_Parameter_MotionVectors)) {
    if (cfg_int("fg_input_copy", 0)) {
      const auto& im = mv->Resource.ImageViewInfo;
      Bg3Binding b{};
      b.image = im.Image; b.view = im.ImageView; b.format = VkFormat(im.Format);
      b.width = im.Width; b.height = im.Height;
      b.layout = sl_input_layout(cmd, b.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      auto* dep = get_res(p, NVSDK_NGX_Parameter_Depth);
      if (dep && dep->Type == NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW) {
        const auto& di = dep->Resource.ImageViewInfo;
        Bg3Binding db{};
        db.image = di.Image; db.view = di.ImageView; db.format = VkFormat(di.Format);
        db.width = di.Width; db.height = di.Height;
        db.layout = sl_input_layout(cmd, db.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sl_preserve_inputs(cmd, b, db);
      }
    }
    std::lock_guard<std::mutex> mvl(g_mv_mu);
    g_mv_image = mv->Resource.ImageViewInfo.Image;
    g_mv_view = mv->Resource.ImageViewInfo.ImageView;
    g_mv_format = mv->Resource.ImageViewInfo.Format;
    // Keep the last valid scale unless both parameters are read and finite: a NaN/Inf scale
    // turns every reprojection of the core's temporal passes non-finite.
    float sx = g_mv_scale[0], sy = g_mv_scale[1];
    if (get_f(p, NVSDK_NGX_Parameter_MV_Scale_X, sx) && get_f(p, NVSDK_NGX_Parameter_MV_Scale_Y, sy) &&
        std::isfinite(sx) && std::isfinite(sy)) {
      g_mv_scale[0] = sx;
      g_mv_scale[1] = sy;
    }
  }
  if (!g_logged_eval_contract) {
    g_logged_eval_contract = true;
    log_eval(*f, p);
  }
  if (g_jitter_logs < 16) {
    float jx = 0, jy = 0;
    get_f(p, NVSDK_NGX_Parameter_Jitter_Offset_X, jx);
    get_f(p, NVSDK_NGX_Parameter_Jitter_Offset_Y, jy);
    log("[ngx] jitter #%d: %+.4f %+.4f", g_jitter_logs, jx, jy);
    ++g_jitter_logs;
  }

  int want = cfg_int("rr", 1) && !f->rr_failed ? 1 : 0;

  ts_init(dd);
  uint32_t slot = g_qpool ? ts_begin(dd, cmd, want) : 0;
  NVSDK_NGX_Result r = NVSDK_NGX_Result_Fail;
  bool done = false;
  int mode = f->last_mode;   // in: last mode; out: the RR mode run (1 graded input, 2 before post)
  if (want) done = rr_evaluate(dd, cmd, *f, p, cb, mode, slot, r);
  if (!done) {
    mode = 0;  // the game's own SR evaluate (RR off, declined or failed)
    if (g_qpool) {
      g_ts_mode[slot] = 0;
      ts_mark(dd, cmd, slot, 1);
    }
    const bool reset = f->last_mode != -1 && f->last_mode != 0;
    int old_reset = 0;
    bool had_reset = get_i(p, NVSDK_NGX_Parameter_Reset, old_reset);
    if (reset) p->Set(NVSDK_NGX_Parameter_Reset, 1);
    r = orig_eval()(cmd, h, cp, cb);
    if (reset) p->Set(NVSDK_NGX_Parameter_Reset, had_reset ? old_reset : 0);
    if (g_qpool) ts_mark(dd, cmd, slot, 2);
    NVSDK_NGX_Resource_VK* out = get_res(p, NVSDK_NGX_Parameter_Output);
    trace_guides(cmd, out ? out->Resource.ImageViewInfo.Image : VK_NULL_HANDLE, nullptr, "sr");
  } else {
    g_rr_frame.store(frame_index());
  }
  static const char* names[3] = {"DLSS SR", "Ray Reconstruction (graded input)", "Ray Reconstruction before post"};
  if (f->last_mode != mode) log("[ngx] upscaler for feature %p: %s", (void*)h, names[mode]);
  f->last_mode = mode;
  return r;
}

// ---------------------------------------------------------------------------
// Hook installation
// ---------------------------------------------------------------------------

static void hook_module(HMODULE mod) {
  if (g_ngx_mod) return;
  auto proc = [&](const char* n) { return reinterpret_cast<void*>(GetProcAddress(mod, n)); };
  void* create = proc("NVSDK_NGX_VULKAN_CreateFeature");
  void* create1 = proc("NVSDK_NGX_VULKAN_CreateFeature1");
  void* eval = proc("NVSDK_NGX_VULKAN_EvaluateFeature");
  void* release = proc("NVSDK_NGX_VULKAN_ReleaseFeature");
  if (!create || !eval || !release) {
    log("[ngx] _nvngx.dll at %p lacks the Vulkan exports", (void*)mod);
    return;
  }
  g_ngx_mod = mod;
  char path[MAX_PATH] = {};
  GetModuleFileNameA(mod, path, MAX_PATH);
  log("[ngx] NGX loader %s at %p", path, (void*)mod);
  g_alloc_params = reinterpret_cast<PFN_AllocParams>(proc("NVSDK_NGX_VULKAN_AllocateParameters"));
  if (!cfg_int("ngx_hook", 1)) {
    log("[ngx] ngx_hook=0: not hooking");
    return;
  }
  bool ok = hook_install(g_h_create, create, reinterpret_cast<void*>(&Detour_Create), "VULKAN_CreateFeature");
  if (create1) ok &= hook_install(g_h_create1, create1, reinterpret_cast<void*>(&Detour_Create1), "VULKAN_CreateFeature1");
  ok &= hook_install(g_h_eval, eval, reinterpret_cast<void*>(&Detour_Eval), "VULKAN_EvaluateFeature");
  ok &= hook_install(g_h_release, release, reinterpret_cast<void*>(&Detour_Release), "VULKAN_ReleaseFeature");
  if (!ok) log("[ngx] some NGX hooks failed; RR will not be available");
}

struct UnicodeStr {
  USHORT Length, MaximumLength;
  PWSTR Buffer;
};
struct DllNotificationData {
  ULONG Flags;
  const UnicodeStr* FullDllName;
  const UnicodeStr* BaseDllName;
  PVOID DllBase;
  ULONG SizeOfImage;
};
using PFN_DllNotification = VOID(CALLBACK*)(ULONG reason, const DllNotificationData* data, PVOID ctx);
using PFN_LdrRegisterDllNotification = LONG(NTAPI*)(ULONG, PFN_DllNotification, PVOID, PVOID*);

static VOID CALLBACK on_dll(ULONG reason, const DllNotificationData* data, PVOID) {
  if (reason != 1 || !data || !data->BaseDllName || !data->BaseDllName->Buffer) return;
  const UnicodeStr* n = data->BaseDllName;
  std::wstring name(n->Buffer, n->Length / sizeof(wchar_t));
  if (cfg_int("ngx_log_dlls", 0)) log("[ngx] dll loaded: %ls", name.c_str());
  if (_wcsicmp(name.c_str(), L"_nvngx.dll") == 0) hook_module(static_cast<HMODULE>(data->DllBase));
}

void ngx_init() {
  static std::once_flag once;
  std::call_once(once, [] {
    if (!cfg_int("ngx_hook", 1)) {
      log("[ngx] ngx_hook=0: NGX is left alone");
      return;
    }
    // The loader unloads layers with their last VkInstance (BG3 creates and
    // destroys a probe instance for XeSS first). The DLL notification and the
    // inline hooks point into this module, so it must never be unmapped.
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCSTR>(&ngx_init), &self);
    if (HMODULE m = GetModuleHandleW(L"_nvngx.dll")) hook_module(m);
    auto reg = reinterpret_cast<PFN_LdrRegisterDllNotification>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));
    static PVOID cookie = nullptr;
    if (reg && reg(0, &on_dll, nullptr, &cookie) == 0) log("[ngx] waiting for _nvngx.dll");
    else log("[ngx] LdrRegisterDllNotification unavailable");
  });
}

}  // namespace bg3
