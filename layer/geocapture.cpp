// Track D phase 1: capture the opaque scene geometry of BG3's G-buffer pass in
// camera-relative world space (docs/TRACK_D_RT.md).
//
// Mechanism: transform feedback of gl_Position.
// - Every vertex shader module gets three decorations and one execution mode
//   (Xfb, XfbBuffer 0 / XfbStride 16 / Offset 0 on its Position output). Nothing
//   else in the shader changes, so all ~36 G-buffer VS variants (static,
//   instanced, compute-skinned, wind, terrain) are covered the same way.
// - Every captured draw is self-contained: the XFB buffer is bound at a
//   CPU-known offset (prefix sum of indexCount/3 * instances), XFB begins,
//   the draw runs, XFB ends. No counter buffers (an End's counter is not
//   visible to the next Begin without a barrier, which a render pass cannot
//   hold), and no pipeline bind while XFB is active (VUID-02323). A transform
//   feedback stream query per draw checks the count and detects overflow.
// - gl_Position = vec4(W, 1) * ViewProj (y negated) in every G-buffer VS, with
//   ViewProj at offset 128 of the camera UBO (set 0, binding 2). W, the
//   camera-relative world position, is recovered by solving the x, y and w rows
//   of that matrix, which is exact and ignores any z tricks (the sky writes z=0).
//
// Validation (all in the capture frame, on the present thread): the clip-space
// triangles are rasterised on the CPU and compared with the frame's own depth
// buffer, and the world-space triangle normals with the G-buffer normals.
#include "host.h"
#include "spirv_xfb.h"

#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <unordered_set>

namespace bg3 {

using HookMap = std::unordered_map<std::string, PFN_vkVoidFunction>;

namespace {

// ---- device-level state ----
std::atomic<bool> g_on{false};          // XFB enabled on the device: shaders are patched
std::atomic<bool> g_timing{false};
DeviceData* g_dd = nullptr;
PFN_vkCmdBindTransformFeedbackBuffersEXT p_bind_xfb = nullptr;
PFN_vkCmdBeginTransformFeedbackEXT p_begin_xfb = nullptr;
PFN_vkCmdEndTransformFeedbackEXT p_end_xfb = nullptr;
PFN_vkCmdCopyBuffer p_copy_buffer = nullptr;
PFN_vkCmdSetViewport p_set_viewport = nullptr;
PFN_vkCmdBeginQuery p_begin_query = nullptr;
PFN_vkCmdEndQuery p_end_query = nullptr;
PFN_vkCmdDispatch p_dispatch = nullptr;
VkDeviceSize g_ubo_align = 256;
// Camera UBO copy (shaders/geo_ubo_copy.hlsl), push descriptors.
VkDescriptorSetLayout g_ubo_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_ubo_pl = VK_NULL_HANDLE;
VkPipeline g_ubo_pipe = VK_NULL_HANDLE;
float g_ts_period = 1.0f;

// Storage for the modified device create info (one device).
std::vector<const char*> g_ext_names;
VkPhysicalDeviceTransformFeedbackFeaturesEXT g_xfb_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT};

// XFB vertex layout, fixed at device creation: 16 bytes (gl_Position), or 24
// with the UV output (Track D phase 3, rt=1 and rt_alpha=1). With rt_alpha the
// per-frame scene comes from the depth prepass (its fragment shaders are the
// ones that alpha test), otherwise from the G-buffer pass.
uint32_t g_stride = 16;
bool g_alpha_mode = false;
std::atomic<uint32_t> g_gbuf_w{0}, g_gbuf_h{0};   // render resolution, from the last G-buffer pass
thread_local int t_last_uv = -1;       // UV location of the module patched last on this thread

std::mutex g_mu;  // modules, pipelines, buffers
std::unordered_map<VkShaderModule, int> g_patched;   // -> UV output location (-1 none)
struct PipeInfo {
  bool xfb;                  // VS patched, VS is the last pre-raster stage
  bool triangles;            // triangle topology (list/strip/fan, with or without adjacency)
  VkPrimitiveTopology topo;
  int uv = -1;               // VS output location captured as UV (stride 24)
  bool restart = true;       // primitive restart enabled, or dynamic (unknown)
};
std::unordered_map<VkPipeline, PipeInfo> g_pipes;
std::unordered_map<VkBuffer, VkBufferUsageFlags> g_buffer_usage;
std::atomic<uint32_t> g_patch_count{0}, g_patch_skipped{0};

// ---- buffers ----
struct Buf {
  VkBuffer b = VK_NULL_HANDLE;
  VkDeviceMemory m = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  void* map = nullptr;
};

bool make_buf(Buf& out, VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
  DeviceData* dd = g_dd;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  if (dd->vk.CreateBuffer(dd->device, &bci, nullptr, &out.b) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetBufferMemoryRequirements(dd->device, out.b, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
  flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) && rt_alloc_device_address()) mai.pNext = &flags;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex =
      host ? find_memory_type(dd, req.memoryTypeBits,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                  VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
           : find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (host && mai.memoryTypeIndex == UINT32_MAX)
    mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.m) != VK_SUCCESS) {
    dd->vk.DestroyBuffer(dd->device, out.b, nullptr);
    out.b = VK_NULL_HANDLE;
    return false;
  }
  dd->vk.BindBufferMemory(dd->device, out.b, out.m, 0);
  out.size = size;
  if (host) dd->vk.MapMemory(dd->device, out.m, 0, VK_WHOLE_SIZE, 0, &out.map);
  return true;
}

void free_buf(Buf& b) {
  if (b.map) g_dd->vk.UnmapMemory(g_dd->device, b.m);
  defer_destroy(BG3_DEFER_BUFFER, uint64_t(b.b));
  defer_destroy(BG3_DEFER_MEMORY, uint64_t(b.m));
  b = Buf{};
}

// ---- capture ----
struct DrawRec {
  uint32_t kind;             // 0 draw, 1 indexed, 2 indirect, 3 indexed indirect, 4/5 indirect count
  uint32_t pipe_id;
  uint64_t vs, fs;
  uint32_t count, instances; // direct draws: vertex/index count and instances (0 if indirect)
  int32_t slot;              // counter slot, -1 if not captured
  uint32_t skip;             // 1 no xfb pipeline, 2 not triangles, 3 out of slots
  VkBuffer ubo;
  VkDeviceSize ubo_off;
  uint64_t offset, reserved; // byte range bound for this draw's XFB output
};

struct Capture {
  bool readback = false;     // false: per-frame cost measurement into a device-local scratch buffer
  int state = 0;             // 0 free, 1 recording, 2 recorded, 3 submitted
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  uint64_t frame_tag = UINT64_MAX, record_frame = 0;
  Buf xfb, counters, depth, normals, ubo;
  VkQueryPool qp = VK_NULL_HANDLE;   // one XFB stream query per captured draw
  uint32_t max_slots = 0, slots = 0;
  int32_t active_slot = -1;
  bool xfb_active = false;
  uint64_t cursor = 0;               // next free byte of the XFB buffer
  uint32_t w = 0, h = 0;
  VkViewport viewport{};
  VkFormat depth_format = VK_FORMAT_UNDEFINED, normal_format = VK_FORMAT_UNDEFINED;
  bool have_depth = false, have_normals = false, have_ubo = false;
  uint32_t ubo_bytes = 0;
  uint32_t ubo_usage = 0;
  std::vector<DrawRec> draws;
  std::string dir;
  uint64_t wait_until = 0;
  // Per-frame capture for the acceleration structures (rt=1): only exactly
  // sized draws, so the soup is contiguous; the camera UBO of the first one.
  bool rt = false;
  VkBuffer rt_ubo = VK_NULL_HANDLE;
  VkDeviceSize rt_ubo_off = 0;
  uint64_t sig = 0;
  uint32_t rt_draws = 0;
  // rt_alpha: opaque draws fill [0, opaque_end), alpha-tested ones
  // [alpha_base, xfb.size) from alpha_cursor.
  VkDeviceSize opaque_end = 0, alpha_base = 0, alpha_cursor = 0;
  uint32_t alpha_draws = 0;
  // What this frame wanted (for the next frame's split, AUDIT_RT fix 5) and what
  // did not fit; draws left out of the RT soup (fix 7).
  VkDeviceSize alpha_demand = 0, alpha_overflow_bytes = 0, opaque_overflow_bytes = 0;
  uint32_t alpha_overflow = 0, opaque_overflow = 0;
  uint32_t drop_indirect = 0, drop_topo = 0, strips = 0;
  uint64_t drop_topo_tris = 0, strip_tris = 0;
};

// The split of the RT capture's XFB buffer between opaque and alpha-tested
// triangles, sized from the previous frame's demand (rt_alpha_auto=1).
std::atomic<uint64_t> g_alpha_region{0}, g_alpha_demand_prev{0}, g_opaque_demand_prev{0};
// Logged loudly: per-window totals, and the first occurrence at once.
struct RtDropLog {
  uint64_t frames = 0, alpha_overflow = 0, alpha_overflow_mb = 0, opaque_overflow = 0, drop_indirect = 0, drop_topo = 0,
           drop_topo_tris = 0, strips = 0, strip_tris = 0;
  uint64_t peak_alpha = 0, peak_opaque = 0;
  bool first_alpha = false, first_opaque = false, first_drop = false;
};
std::mutex g_drop_mu;
RtDropLog g_drop;

std::mutex g_cap_mu;
Capture g_cap;                 // the readback capture (one per trace)
Capture g_scratch;             // geo_xfb_always
std::atomic<bool> g_submit_wanted{false};

// ---- timing ----
constexpr uint32_t kTsSlots = 64;
VkQueryPool g_qp = VK_NULL_HANDLE;
std::mutex g_ts_mu;
uint64_t g_ts_frame[kTsSlots];        // frame the slot was written in (0 = free)
bool g_ts_xfb[kTsSlots];
struct TsAcc { double sum = 0; uint32_t n = 0; std::vector<float> v; };
TsAcc g_ts_acc[2];                    // [0] xfb inactive, [1] xfb active

bool is_gbuffer(const Bg3PassInfo& p) {
  if (p.color_count != 5 || p.depth_format != VK_FORMAT_D24_UNORM_S8_UINT) return false;
  if (p.color_formats[0] != VK_FORMAT_R16G16_SFLOAT) return false;
  for (int i = 1; i < 5; ++i)
    if (p.color_formats[i] != VK_FORMAT_R8G8B8A8_UNORM) return false;
  return true;
}

// Depth-only D24S8 at render resolution (rt_alpha's source pass).
bool is_prepass(const Bg3PassInfo& p) {
  return g_alpha_mode && p.color_count == 0 && p.depth_format == VK_FORMAT_D24_UNORM_S8_UINT && g_gbuf_w &&
         p.width == g_gbuf_w && p.height == g_gbuf_h;
}
constexpr uint32_t kPrepassMinDraws = 64;
std::atomic<int> g_prepass_ordinal{-1};   // learned: ordinal of the depth-only pass with the scene
std::atomic<uint64_t> g_prepass_frame{0}; // frame it was last captured in (relearned when it goes missing)

// ---- hooks of our own ----
VKAPI_ATTR void VKAPI_CALL CmdSetViewport(VkCommandBuffer cmd, uint32_t first, uint32_t n, const VkViewport* vps) {
  p_set_viewport(cmd, first, n, vps);
  if (first == 0 && n)
    if (CmdState* s = cmd_state(cmd)) {
      s->viewport = vps[0];
      s->has_viewport = true;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL CreateBuffer(VkDevice device, const VkBufferCreateInfo* ci,
                                            const VkAllocationCallbacks* alloc, VkBuffer* out) {
  VkResult r = device_of(device)->vk.CreateBuffer(device, ci, alloc, out);
  if (r == VK_SUCCESS) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_buffer_usage[*out] = ci->usage;
  }
  return r;
}

VKAPI_ATTR void VKAPI_CALL DestroyBuffer(VkDevice device, VkBuffer b, const VkAllocationCallbacks* alloc) {
  device_of(device)->vk.DestroyBuffer(device, b, alloc);
  std::lock_guard<std::mutex> lock(g_mu);
  g_buffer_usage.erase(b);
}

// ---- recording helpers ----
void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkAccessFlags sa, VkPipelineStageFlags dst,
             VkAccessFlags da) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = sa;
  mb.dstAccessMask = da;
  g_dd->vk.CmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

bool copy_image(VkCommandBuffer cmd, VkImage image, VkImageLayout layout, VkFormat fmt, bool depth, uint32_t w,
                uint32_t h, Buf& out) {
  if (!image || layout == VK_IMAGE_LAYOUT_UNDEFINED) return false;
  if (!make_buf(out, VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) return false;
  VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
  if (depth && fmt != VK_FORMAT_D24_UNORM_S8_UINT) aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b.oldLayout = layout;
  b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {aspect, 0, 1, 0, 1};
  g_dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                              0, nullptr, 1, &b);
  VkBufferImageCopy region{};
  region.imageSubresource = {VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 0, 1};
  region.imageExtent = {w, h, 1};
  g_dd->vk.CmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out.b, 1, &region);
  b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b.newLayout = layout;
  g_dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                              0, nullptr, 1, &b);
  return true;
}

bool start_capture(Capture& c, bool readback, uint32_t w, uint32_t h) {
  if (!c.xfb.b) {
    // The per-frame RT capture holds one frame's soup (3.2M triangles = 152 MB
    // in the test scene) and is read in place by the AS build.
    const bool rt = !readback && rt_enabled();
    VkDeviceSize mb = VkDeviceSize((std::max)(rt ? cfg_int("rt_xfb_mb", 384) : cfg_int("geo_xfb_mb", 768), 16));
    c.max_slots = uint32_t((std::max)(cfg_int("geo_max_draws", 16384), 256));
    // Readback captures write straight into host-cached memory (one frame, so
    // the PCIe traffic does not matter); the cost probe uses device memory.
    if (!make_buf(c.xfb, mb << 20, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | (rt ? rt_xfb_usage() : 0), readback) ||
        !make_buf(c.counters, VkDeviceSize(c.max_slots) * 4, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT,
                  readback)) {
      log("[geo] could not allocate %llu MB of transform feedback memory", (unsigned long long)mb);
      if (c.xfb.b) free_buf(c.xfb);
      return false;
    }
  }
  if (readback && !c.qp) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;
    qi.queryCount = c.max_slots;
    if (g_dd->vk.CreateQueryPool(g_dd->device, &qi, nullptr, &c.qp) != VK_SUCCESS) {
      log("[geo] transform feedback query pool creation failed");
      c.qp = VK_NULL_HANDLE;
      return false;
    }
  }
  c.readback = readback;
  c.state = 1;
  c.slots = 0;
  c.active_slot = -1;
  c.xfb_active = false;
  c.cursor = 0;
  c.draws.clear();
  c.w = w;
  c.h = h;
  c.frame_tag = UINT64_MAX;
  c.record_frame = frame_index();
  c.have_depth = c.have_normals = c.have_ubo = false;
  c.rt = false;
  c.rt_ubo = VK_NULL_HANDLE;
  c.rt_ubo_off = 0;
  c.sig = 1469598103934665603ull;
  c.rt_draws = 0;
  c.opaque_end = c.xfb.size;
  c.alpha_base = c.alpha_cursor = c.xfb.size;
  c.alpha_draws = 0;
  c.alpha_demand = c.alpha_overflow_bytes = c.opaque_overflow_bytes = 0;
  c.alpha_overflow = c.opaque_overflow = 0;
  c.drop_indirect = c.drop_topo = c.strips = 0;
  c.drop_topo_tris = c.strip_tris = 0;
  if (!readback && g_alpha_mode && rt_enabled()) {
    // Alpha region: rt_alpha_mb at first; with rt_alpha_auto, 1.5x the last
    // frame's demand (grown at once, shrunk only below a third of it), at least
    // rt_alpha_mb, capped by rt_alpha_max_pct of the buffer and by what the
    // opaque triangles needed last frame.
    const VkDeviceSize mb = VkDeviceSize(1) << 20, size = c.xfb.size;
    VkDeviceSize floor_b = VkDeviceSize((std::max)(cfg_int("rt_alpha_mb", 64), 1)) * mb;
    VkDeviceSize amb = g_alpha_region.load();
    if (!amb) amb = floor_b;
    if (cfg_int("rt_alpha_auto", 1)) {
      VkDeviceSize want = (std::max)(floor_b, (g_alpha_demand_prev.load() * 3 / 2 + mb - 1) / mb * mb);
      if (want > amb || want * 2 < amb) amb = want;
      VkDeviceSize cap = size / 100 * VkDeviceSize((std::min)((std::max)(cfg_int("rt_alpha_max_pct", 60), 5), 90));
      VkDeviceSize opaque = g_opaque_demand_prev.load() * 11 / 10;
      if (opaque && opaque < size) cap = (std::min)(cap, (std::max)(size - opaque, floor_b));
      amb = (std::min)(amb, cap);
    } else {
      amb = floor_b;
    }
    if (amb < size) {
      g_alpha_region = amb;
      c.alpha_base = c.alpha_cursor = c.opaque_end = (size - amb) & ~VkDeviceSize(255);
      // A new split moves the alpha geometry: the BLAS must be rebuilt, not refit.
      c.sig = (c.sig ^ c.alpha_base) * 1099511628211ull;
      alpha_begin_capture();
    }
  }
  return true;
}

// ---- analysis ----
float half_to_float(uint16_t h) {
  uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
  float v = e == 0 ? std::ldexp(float(m), -24) : e == 31 ? (m ? NAN : INFINITY) : std::ldexp(float(m | 1024), int(e) - 25);
  return s ? -v : v;
}

struct V4 { float x, y, z, w; };
struct V3 { float x, y, z; };

std::string hex64(uint64_t v) {
  char b[24];
  snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
  return b;
}

std::string jnum(double v) {
  if (!std::isfinite(v)) return "null";
  char b[48];
  snprintf(b, sizeof b, "%.6g", v);
  return b;
}

// Solves the x, y and w rows of clip = [W, 1] * M (M row-major as stored in the
// UBO) for W. Returns false if the matrix is singular.
struct Unproject {
  double inv[3][3];
  double t[3];      // m[12+j] for j = 0, 1, 3
  double m[16];
  bool ok = false;
  void init(const float* f) {
    for (int i = 0; i < 16; ++i) m[i] = f[i];
    const int js[3] = {0, 1, 3};
    double a[3][3];
    for (int r = 0; r < 3; ++r) {
      for (int k = 0; k < 3; ++k) a[r][k] = m[k * 4 + js[r]];
      t[r] = m[12 + js[r]];
    }
    double det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                 a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    if (!std::isfinite(det) || std::fabs(det) < 1e-30) return;
    double id = 1.0 / det;
    inv[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * id;
    inv[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * id;
    inv[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * id;
    inv[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * id;
    inv[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * id;
    inv[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * id;
    inv[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * id;
    inv[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * id;
    inv[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * id;
    ok = true;
  }
  // `c` is gl_Position as written (y already negated by the shader).
  V3 world(const V4& c) const {
    double b[3] = {c.x - t[0], -c.y - t[1], c.w - t[2]};
    double w[3];
    for (int r = 0; r < 3; ++r) w[r] = inv[r][0] * b[0] + inv[r][1] * b[1] + inv[r][2] * b[2];
    return {float(w[0]), float(w[1]), float(w[2])};
  }
  double clip_z(const V3& w) const { return w.x * m[2] + w.y * m[6] + w.z * m[10] + m[14]; }
  // The camera centre: the W with clip x = y = w = 0.
  V3 eye() const {
    double b[3] = {-t[0], -t[1], -t[2]};
    double w[3];
    for (int r = 0; r < 3; ++r) w[r] = inv[r][0] * b[0] + inv[r][1] * b[1] + inv[r][2] * b[2];
    return {float(w[0]), float(w[1]), float(w[2])};
  }
};

// Software rasteriser over clip-space triangles: reversed Z (larger is
// closer), pixel centres, no culling. Triangles are clipped against w > eps.
struct Raster {
  uint32_t w, h;
  VkViewport vp;
  std::vector<float> depth;     // -1 = empty
  std::vector<uint32_t> tri;    // triangle index
  uint64_t clipped = 0, degenerate = 0;

  void run(const V4* v, uint32_t ntri, unsigned threads) {
    depth.assign(size_t(w) * h, -1.0f);
    tri.assign(size_t(w) * h, UINT32_MAX);
    std::vector<std::thread> pool;
    std::atomic<uint64_t> clip_n{0}, degen_n{0};
    for (unsigned t = 0; t < threads; ++t)
      pool.emplace_back([&, t] {
        uint32_t y0 = uint32_t(uint64_t(h) * t / threads), y1 = uint32_t(uint64_t(h) * (t + 1) / threads);
        uint64_t cl = 0, dg = 0;
        for (uint32_t i = 0; i < ntri; ++i) {
          const V4* p = v + size_t(i) * 3;
          // Clip against w >= eps (Sutherland-Hodgman, one plane).
          const double eps = 1e-5;
          double in[3][4], poly[4][4];
          int n = 0;
          for (int k = 0; k < 3; ++k) {
            in[k][0] = p[k].x; in[k][1] = p[k].y; in[k][2] = p[k].z; in[k][3] = p[k].w;
          }
          bool any_out = false;
          for (int k = 0; k < 3; ++k) {
            const double* a = in[k];
            const double* b = in[(k + 1) % 3];
            bool ain = a[3] >= eps, bin = b[3] >= eps;
            if (!ain) any_out = true;
            if (ain) { std::memcpy(poly[n++], a, sizeof(double) * 4); }
            if (ain != bin && n < 4) {
              double s = (eps - a[3]) / (b[3] - a[3]);
              for (int c = 0; c < 4; ++c) poly[n][c] = a[c] + (b[c] - a[c]) * s;
              ++n;
            }
          }
          if (any_out && t == 0) ++cl;
          if (n < 3) continue;
          double sx[4], sy[4], sz[4];
          bool bad = false;
          for (int k = 0; k < n; ++k) {
            double iw = 1.0 / poly[k][3];
            sx[k] = vp.x + (poly[k][0] * iw * 0.5 + 0.5) * vp.width;
            sy[k] = vp.y + (poly[k][1] * iw * 0.5 + 0.5) * vp.height;
            sz[k] = vp.minDepth + poly[k][2] * iw * (vp.maxDepth - vp.minDepth);
            if (!std::isfinite(sx[k]) || !std::isfinite(sy[k]) || !std::isfinite(sz[k])) bad = true;
          }
          if (bad) continue;
          for (int f = 1; f + 1 < n; ++f) {
            int a = 0, b = f, c = f + 1;
            double area = (sx[b] - sx[a]) * (sy[c] - sy[a]) - (sx[c] - sx[a]) * (sy[b] - sy[a]);
            if (area == 0.0 || !std::isfinite(area)) { if (t == 0) ++dg; continue; }
            double minx = (std::min)({sx[a], sx[b], sx[c]}), maxx = (std::max)({sx[a], sx[b], sx[c]});
            double miny = (std::min)({sy[a], sy[b], sy[c]}), maxy = (std::max)({sy[a], sy[b], sy[c]});
            int ix0 = (std::max)(0, int(std::ceil(minx - 0.5))), ix1 = (std::min)(int(w) - 1, int(std::floor(maxx - 0.5)));
            int iy0 = (std::max)(int(y0), int(std::ceil(miny - 0.5)));
            int iy1 = (std::min)(int(y1) - 1, int(std::floor(maxy - 0.5)));
            if (ix0 > ix1 || iy0 > iy1) continue;
            double ia = 1.0 / area;
            for (int y = iy0; y <= iy1; ++y) {
              double py = y + 0.5;
              for (int x = ix0; x <= ix1; ++x) {
                double px = x + 0.5;
                double w0 = ((sx[b] - px) * (sy[c] - py) - (sx[c] - px) * (sy[b] - py)) * ia;
                double w1 = ((sx[c] - px) * (sy[a] - py) - (sx[a] - px) * (sy[c] - py)) * ia;
                double w2 = 1.0 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float d = float(w0 * sz[a] + w1 * sz[b] + w2 * sz[c]);
                size_t o = size_t(y) * w + x;
                if (d > depth[o]) { depth[o] = d; tri[o] = i; }
              }
            }
          }
        }
        if (t == 0) { clip_n += cl; degen_n += dg; }
      });
    for (auto& th : pool) th.join();
    clipped = clip_n;
    degenerate = degen_n;
  }
};

void png_thread(std::vector<std::pair<std::string, std::vector<uint8_t>>> imgs, uint32_t w, uint32_t h) {
  std::thread([&] {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (auto& [path, rgb] : imgs) write_png(std::wstring(path.begin(), path.end()), w, h, rgb);
    if (SUCCEEDED(co)) CoUninitialize();
  }).join();
}

void process(Capture& c) {
  ULONGLONG t0 = GetTickCount64();
  std::string dir = c.dir + "\\geo";
  make_dirs(dir);
  const uint8_t* xfb_raw = static_cast<const uint8_t*>(c.xfb.map);

  // Per-draw ranges from the stream queries: primitives written per draw, in
  // recording order; each draw's output sits at its reserved offset.
  std::vector<uint64_t> q(size_t(c.slots) * 2, 0);
  VkResult qr = c.slots ? g_dd->vk.GetQueryPoolResults(g_dd->device, c.qp, 0, c.slots, q.size() * 8, q.data(), 16,
                                                       VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT)
                        : VK_SUCCESS;
  if (qr != VK_SUCCESS) log("[geo] transform feedback query results: %d", qr);
  struct Range { uint32_t first, count; };
  std::vector<Range> ranges(c.draws.size(), Range{0, 0});
  std::vector<uint8_t> compact(size_t(c.cursor));
  uint32_t prev = 0, total_bytes = 0, mismatched = 0, overflow = 0;
  uint64_t expect_tris = 0, expect_known = 0, needed_more = 0;
  for (size_t i = 0; i < c.draws.size(); ++i) {
    const DrawRec& d = c.draws[i];
    if (d.slot < 0) continue;
    uint64_t written = q[size_t(d.slot) * 2], needed = q[size_t(d.slot) * 2 + 1];
    if (needed > written) needed_more += needed - written;
    const uint64_t tri_bytes = 3ull * g_stride;
    if (written * tri_bytes > d.reserved) { ++mismatched; written = d.reserved / tri_bytes; }
    uint64_t end = prev + written * 48;
    // Compact to 16 bytes per vertex (gl_Position): draws were written at
    // their reserved offsets, g_stride bytes per vertex.
    if (written && g_stride == 16) std::memmove(compact.data() + prev, xfb_raw + d.offset, size_t(written * 48));
    else
      for (uint64_t v = 0; v < written * 3; ++v)
        std::memcpy(compact.data() + prev + v * 16, xfb_raw + d.offset + v * g_stride, 16);
    ranges[i] = {prev / 16, uint32_t((end - prev) / 16)};
    if (d.count && d.instances && (d.kind == 0 || d.kind == 1)) {
      uint64_t want = uint64_t(d.count / 3) * d.instances * 3;
      expect_tris += want / 3;
      ++expect_known;
      if (want != ranges[i].count) ++mismatched;
    }
    prev = uint32_t(end);
  }
  total_bytes = prev;
  for (const auto& d : c.draws) overflow += d.skip == 3;
  if (needed_more) ++overflow;
  const uint32_t nvert = total_bytes / 16;
  const uint32_t ntri = nvert / 3;
  const V4* clip = reinterpret_cast<const V4*>(compact.data());

  // Camera UBO: ViewProj at 128.
  Unproject un;
  float ubo[64] = {};
  if (c.have_ubo) {
    std::memcpy(ubo, c.ubo.map, (std::min)(c.ubo_bytes, 256u));
    un.init(ubo + 32);
  }
  std::vector<V3> world;
  double zres_max = 0, zres_sum = 0;
  uint64_t zres_bad = 0, nonfinite = 0;
  float bmin[3] = {INFINITY, INFINITY, INFINITY}, bmax[3] = {-INFINITY, -INFINITY, -INFINITY};
  if (un.ok) {
    world.resize(nvert);
    for (uint32_t i = 0; i < nvert; ++i) {
      V3 p = un.world(clip[i]);
      const V4& cv = clip[i];
      if (!std::isfinite(cv.x) || !std::isfinite(cv.y) || !std::isfinite(cv.z) || !std::isfinite(cv.w) ||
          !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
        // The game parks culled vertices at NaN; keep them out of the soup.
        ++nonfinite;
        world[i] = {0, 0, 0};
        continue;
      }
      world[i] = p;
      double zr = std::fabs(un.clip_z(p) - clip[i].z) / (std::fabs(clip[i].z) + 1e-6);
      zres_sum += zr;
      zres_max = (std::max)(zres_max, zr);
      if (zr > 1e-3) ++zres_bad;
      const float* q = &p.x;
      for (int k = 0; k < 3; ++k) { bmin[k] = (std::min)(bmin[k], q[k]); bmax[k] = (std::max)(bmax[k], q[k]); }
    }
  }

  // Rasterise and compare with the game's depth.
  Raster r;
  r.w = c.w;
  r.h = c.h;
  r.vp = c.viewport;
  if (r.vp.width <= 0 || r.vp.height <= 0) r.vp = {0, 0, float(c.w), float(c.h), 0, 1};
  ULONGLONG tr = GetTickCount64();
  r.run(clip, ntri, (std::max)(1u, (std::min)(16u, std::thread::hardware_concurrency())));
  tr = GetTickCount64() - tr;

  const uint32_t* dbuf = static_cast<const uint32_t*>(c.depth.map);
  const uint32_t* nbuf = static_cast<const uint32_t*>(c.normals.map);
  const float tol_rel = cfg_float("geo_depth_tol", 2e-3f);
  uint64_t px_geo = 0, px_sky = 0, match = 0, front = 0, behind = 0, missing = 0, sky_extra = 0;
  std::vector<uint8_t> img(size_t(c.w) * c.h * 3), img_ours(size_t(c.w) * c.h * 3), img_game(size_t(c.w) * c.h * 3);
  // Depth previews share one scale: log of reversed-Z depth.
  float dmin = INFINITY, dmax = 0;
  if (c.have_depth)
    for (size_t i = 0; i < size_t(c.w) * c.h; ++i) {
      float g = (dbuf[i] & 0xFFFFFF) / 16777215.0f;
      if (g > 0) { dmin = (std::min)(dmin, g); dmax = (std::max)(dmax, g); }
    }
  auto shade = [&](float d) -> uint8_t {
    if (d <= 0 || !(dmax > dmin)) return 0;
    float t = (std::log(d) - std::log(dmin)) / (std::log(dmax) - std::log(dmin) + 1e-12f);
    return uint8_t(std::fmin(std::fmax(t, 0.0f), 1.0f) * 200.0f + 40.0f);
  };
  // Per-triangle normal agreement and per-VS pixel coverage.
  std::vector<uint32_t> tri_draw(ntri, UINT32_MAX);
  for (size_t i = 0; i < c.draws.size(); ++i)
    for (uint32_t t = ranges[i].first / 3; t < (ranges[i].first + ranges[i].count) / 3 && t < ntri; ++t) tri_draw[t] = uint32_t(i);
  std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> vs_stats;  // vs -> (tris, matched px)
  for (size_t i = 0; i < c.draws.size(); ++i) vs_stats[c.draws[i].vs].first += ranges[i].count / 3;
  std::vector<float> ndots, nsamples;
  uint64_t ndot_pos = 0;
  if (c.have_depth)
    for (uint32_t y = 0; y < c.h; ++y)
      for (uint32_t x = 0; x < c.w; ++x) {
        size_t o = size_t(y) * c.w + x;
        float g = (dbuf[o] & 0xFFFFFF) / 16777215.0f;
        float d = r.depth[o];
        uint8_t* px = &img[o * 3];
        img_game[o * 3] = img_game[o * 3 + 1] = img_game[o * 3 + 2] = shade(g);
        img_ours[o * 3] = img_ours[o * 3 + 1] = img_ours[o * 3 + 2] = shade(d);
        if (g <= 0) {
          ++px_sky;
          if (d > 0) { ++sky_extra; px[0] = 200; px[1] = 200; px[2] = 0; }
          continue;
        }
        ++px_geo;
        float tol = tol_rel * g + 3.0f / 16777215.0f;
        if (d < 0) { ++missing; px[0] = 255; px[1] = 0; px[2] = 255; continue; }
        if (d > g + tol) { ++front; px[0] = 255; px[1] = 40; px[2] = 40; continue; }
        if (d < g - tol) { ++behind; px[0] = 40; px[1] = 80; px[2] = 255; continue; }
        ++match;
        uint8_t s = shade(g);
        px[0] = uint8_t(s * 0.3f); px[1] = s; px[2] = uint8_t(s * 0.3f);
        uint32_t t = r.tri[o];
        if (t < ntri && tri_draw[t] != UINT32_MAX) vs_stats[c.draws[tri_draw[t]].vs].second++;
        // Geometric normal of the world-space triangle vs the G-buffer normal.
        if (nbuf && !world.empty() && t < ntri && ((x | y) & 3) == 0) {
          const V3& a = world[t * 3];
          const V3& b = world[t * 3 + 1];
          const V3& cc = world[t * 3 + 2];
          double e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, e2[3] = {cc.x - a.x, cc.y - a.y, cc.z - a.z};
          double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
          double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
          if (!(len > 0)) continue;
          uint32_t raw = nbuf[o];
          float ex = half_to_float(uint16_t(raw & 0xFFFF)) * 3.5554f - 1.7777f;
          float ey = half_to_float(uint16_t(raw >> 16)) * 3.5554f - 1.7777f;
          float tt = ex * ex + ey * ey + 1.0f;
          if (!std::isfinite(tt) || tt > 5.4e10f) continue;
          double g3[3] = {2.0 * ex / tt, 2.0 * ey / tt, 1.0 - 2.0 / tt};
          // The G-buffer normals are view space: rotate by the view matrix
          // (_m0, row-vector convention) before comparing. Identity gives
          // |dot| ~0.77, the view rotation ~0.998 (docs/TRACK_D_RT.md).
          double nv[3];
          for (int j = 0; j < 3; ++j) nv[j] = n[0] * ubo[j] + n[1] * ubo[4 + j] + n[2] * ubo[8 + j];
          double dot = (nv[0] * g3[0] + nv[1] * g3[1] + nv[2] * g3[2]) / len;
          ndots.push_back(float(std::fabs(dot)));
          if (dot > 0) ++ndot_pos;
          float smp[8] = {float(x), float(y), float(n[0] / len), float(n[1] / len), float(n[2] / len),
                          float(g3[0]), float(g3[1]), float(g3[2])};
          nsamples.insert(nsamples.end(), smp, smp + 8);
        }
      }
  std::sort(ndots.begin(), ndots.end());
  // (x, y, triangle normal xyz in W space, G-buffer normal xyz) per sample, for offline frame checks.
  write_file(dir + "\\normals_sample.bin", nsamples.data(), nsamples.size() * 4);
  auto pct = [&](double q) { return ndots.empty() ? NAN : double(ndots[size_t(q * (ndots.size() - 1))]); };
  uint64_t ndot_good = 0;
  for (float v : ndots) ndot_good += v > 0.9f;

  // scene.bin: header, draw table, gl_Position per vertex, world per vertex.
  {
    std::vector<uint8_t> f;
    auto put = [&](const void* p, size_t n) { f.insert(f.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
    uint32_t hdr[8] = {0x47334742u /* "BG3G" */, 1, nvert, uint32_t(c.draws.size()), c.w, c.h,
                       uint32_t(un.ok ? 1 : 0), 0};
    put(hdr, sizeof hdr);
    put(ubo, sizeof ubo);
    put(&r.vp, sizeof(VkViewport));
    uint32_t pad[2] = {};
    put(pad, sizeof pad);
    for (size_t i = 0; i < c.draws.size(); ++i) {
      const DrawRec& d = c.draws[i];
      struct { uint32_t first, count; uint64_t vs, fs; uint32_t pipe, kind, n, inst, skip, pad; } rec{
          ranges[i].first, ranges[i].count, d.vs, d.fs, d.pipe_id, d.kind, d.count, d.instances, d.skip, 0};
      put(&rec, sizeof rec);
    }
    put(clip, size_t(nvert) * 16);
    if (!world.empty()) put(world.data(), world.size() * sizeof(V3));
    write_file(dir + "\\scene.bin", f.data(), f.size());
  }
  if (cfg_int("geo_obj", 0) && !world.empty()) {
    std::string o;
    o.reserve(size_t(nvert) * 36 + size_t(ntri) * 24);
    char b[128];
    o += "# BG3RTX G-buffer geometry, camera-relative world space (one frame)\n";
    for (uint32_t i = 0; i < nvert; ++i) {
      snprintf(b, sizeof b, "v %.4f %.4f %.4f\n", world[i].x, world[i].y, world[i].z);
      o += b;
    }
    for (size_t i = 0; i < c.draws.size(); ++i) {
      if (!ranges[i].count) continue;
      snprintf(b, sizeof b, "g d%zu_vs%s\n", i, hex64(c.draws[i].vs).substr(0, 8).c_str());
      o += b;
      for (uint32_t t = ranges[i].first; t + 2 < ranges[i].first + ranges[i].count; t += 3) {
        snprintf(b, sizeof b, "f %u %u %u\n", t + 1, t + 2, t + 3);
        o += b;
      }
    }
    write_file(dir + "\\scene.obj", o.data(), o.size());
  }
  png_thread({{dir + "\\coverage.png", std::move(img)}, {dir + "\\depth_ours.png", std::move(img_ours)},
              {dir + "\\depth_game.png", std::move(img_game)}},
             c.w, c.h);

  // report.json
  uint32_t captured = 0, skipped[4] = {};
  for (const auto& d : c.draws) {
    if (d.slot >= 0) ++captured;
    else if (d.skip < 4) ++skipped[d.skip];
  }
  std::unordered_set<uint64_t> ubos;
  for (const auto& d : c.draws)
    if (d.ubo) ubos.insert(uint64_t(d.ubo) * 31 + d.ubo_off);
  double geo = double(px_geo ? px_geo : 1);
  V3 eye = un.ok ? un.eye() : V3{NAN, NAN, NAN};
  std::string j = "{\n";
  j += "\"draws\":" + std::to_string(c.draws.size()) + ",\"draws_captured\":" + std::to_string(captured) +
       ",\"draws_skipped\":{\"no_xfb_pipeline\":" + std::to_string(skipped[1]) + ",\"not_triangles\":" +
       std::to_string(skipped[2]) + ",\"out_of_slots\":" + std::to_string(skipped[3]) + "},\n";
  j += "\"triangles\":" + std::to_string(ntri) + ",\"vertices\":" + std::to_string(nvert) +
       ",\"xfb_bytes\":" + std::to_string(total_bytes) + ",\"xfb_overflow\":" + std::to_string(overflow) +
       ",\"primitives_dropped\":" + std::to_string(needed_more) + ",\"xfb_reserved_bytes\":" + std::to_string(c.cursor) +
       ",\"count_mismatches\":" + std::to_string(mismatched) + ",\"direct_draws_checked\":" +
       std::to_string(expect_known) + ",\"expected_triangles_direct\":" + std::to_string(expect_tris) + ",\n";
  j += "\"width\":" + std::to_string(c.w) + ",\"height\":" + std::to_string(c.h) + ",\"viewport\":[" + jnum(r.vp.x) +
       "," + jnum(r.vp.y) + "," + jnum(r.vp.width) + "," + jnum(r.vp.height) + "," + jnum(r.vp.minDepth) + "," +
       jnum(r.vp.maxDepth) + "],\n";
  j += "\"camera_ubo\":{\"captured\":" + std::string(c.have_ubo ? "true" : "false") + ",\"bytes\":" +
       std::to_string(c.ubo_bytes) + ",\"buffer_usage\":" + std::to_string(c.ubo_usage) + ",\"distinct_bindings\":" +
       std::to_string(ubos.size()) + ",\"floats\":[";
  for (int i = 0; i < 56; ++i) j += (i ? "," : "") + jnum(ubo[i]);
  j += "]},\n";
  j += "\"world\":{\"valid\":" + std::string(un.ok ? "true" : "false") + ",\"eye\":[" + jnum(eye.x) + "," +
       jnum(eye.y) + "," + jnum(eye.z) + "],\"bbox_min\":[" + jnum(bmin[0]) + "," + jnum(bmin[1]) + "," +
       jnum(bmin[2]) + "],\"bbox_max\":[" + jnum(bmax[0]) + "," + jnum(bmax[1]) + "," + jnum(bmax[2]) +
       "],\"z_residual_mean\":" + jnum(nvert ? zres_sum / nvert : 0) + ",\"z_residual_max\":" + jnum(zres_max) +
       ",\"z_modified_vertices\":" + std::to_string(zres_bad) + ",\"nonfinite\":" + std::to_string(nonfinite) + "},\n";
  j += "\"depth\":{\"pixels\":" + std::to_string(size_t(c.w) * c.h) + ",\"geometry\":" + std::to_string(px_geo) +
       ",\"sky\":" + std::to_string(px_sky) + ",\"match\":" + std::to_string(match) + ",\"in_front\":" +
       std::to_string(front) + ",\"behind\":" + std::to_string(behind) + ",\"missing\":" + std::to_string(missing) +
       ",\"sky_covered\":" + std::to_string(sky_extra) + ",\"coverage\":" + jnum(match / geo) +
       ",\"covered_any\":" + jnum((match + front + behind) / geo) + ",\"tolerance_rel\":" + jnum(tol_rel) +
       ",\"clipped_triangles\":" + std::to_string(r.clipped) + ",\"degenerate\":" + std::to_string(r.degenerate) + "},\n";
  j += "\"normals\":{\"samples\":" + std::to_string(ndots.size()) + ",\"abs_dot_median\":" + jnum(pct(0.5)) +
       ",\"abs_dot_p10\":" + jnum(pct(0.1)) + ",\"frac_abs_dot_gt_0_9\":" +
       jnum(ndots.empty() ? NAN : double(ndot_good) / ndots.size()) + ",\"frac_positive\":" +
       jnum(ndots.empty() ? NAN : double(ndot_pos) / ndots.size()) + "},\n";
  std::vector<std::pair<uint64_t, std::pair<uint64_t, uint64_t>>> vs(vs_stats.begin(), vs_stats.end());
  std::sort(vs.begin(), vs.end(), [](auto& a, auto& b) { return a.second.first > b.second.first; });
  j += "\"vs\":[";
  for (size_t i = 0; i < vs.size(); ++i) {
    uint32_t nd = 0, ncap = 0;
    for (const auto& d : c.draws)
      if (d.vs == vs[i].first) { ++nd; ncap += d.slot >= 0; }
    j += std::string(i ? ",\n" : "\n") + "{\"vs\":\"" + hex64(vs[i].first) + "\",\"draws\":" + std::to_string(nd) +
         ",\"captured\":" + std::to_string(ncap) + ",\"triangles\":" + std::to_string(vs[i].second.first) +
         ",\"matched_px\":" + std::to_string(vs[i].second.second) + "}";
  }
  j += "],\n\"timing_ms\":{\"raster\":" + std::to_string(tr) + ",\"total\":" + std::to_string(GetTickCount64() - t0) +
       "}\n}\n";
  write_file(dir + "\\report.json", j.data(), j.size());
  log("[geo] frame %llu: %u/%zu draws captured, %u triangles (%u MB XFB%s), coverage %.2f%% "
      "(front %.2f%%, behind %.2f%%, missing %.2f%%), normals |dot| median %.3f, world %s, %llu ms -> %s",
      (unsigned long long)c.frame_tag, captured, c.draws.size(), ntri, total_bytes >> 20, overflow ? ", OVERFLOW" : "",
      100.0 * match / geo, 100.0 * front / geo, 100.0 * behind / geo, 100.0 * missing / geo, pct(0.5),
      un.ok ? "ok" : "unavailable", (unsigned long long)(GetTickCount64() - t0), dir.c_str());
}

bool ubo_pipeline() {
  if (g_ubo_pipe) return true;
  if (!g_dd->vk.CmdPushDescriptorSet) return false;
  Bg3ShaderBlob blob{};
  if (!shader_get("geo_ubo_copy.hlsl", "main", "cs_6_0", &blob)) return false;
  VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                       {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
  VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
  dci.bindingCount = 2;
  dci.pBindings = b;
  if (g_dd->vk.CreateDescriptorSetLayout(g_dd->device, &dci, nullptr, &g_ubo_dsl) != VK_SUCCESS) return false;
  VkPipelineLayoutCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pci.setLayoutCount = 1;
  pci.pSetLayouts = &g_ubo_dsl;
  if (g_dd->vk.CreatePipelineLayout(g_dd->device, &pci, nullptr, &g_ubo_pl) != VK_SUCCESS) return false;
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  VkShaderModule mod = VK_NULL_HANDLE;
  if (g_dd->vk.CreateShaderModule(g_dd->device, &mci, nullptr, &mod) != VK_SUCCESS) return false;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main"};
  ci.layout = g_ubo_pl;
  VkResult r = g_dd->vk.CreateComputePipelines(g_dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &g_ubo_pipe);
  g_dd->vk.DestroyShaderModule(g_dd->device, mod, nullptr);
  if (r != VK_SUCCESS) g_ubo_pipe = VK_NULL_HANDLE;
  log("[geo] camera UBO copy pipeline %s", g_ubo_pipe ? "built" : "FAILED");
  return g_ubo_pipe != VK_NULL_HANDLE;
}

void release(Capture& c) {
  for (Buf* b : {&c.xfb, &c.counters, &c.depth, &c.normals, &c.ubo})
    if (b->b) free_buf(*b);
  if (c.qp) defer_destroy(BG3_DEFER_QUERY_POOL, uint64_t(c.qp));
  c.qp = VK_NULL_HANDLE;
  c.state = 0;
  c.draws.clear();
  c.draws.shrink_to_fit();
}

}  // namespace

// ================= host-facing entry points =================

const VkDeviceCreateInfo* geo_device_create_info(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                 VkDeviceCreateInfo* scratch) {
  g_timing = cfg_int("geo_timing", 0) != 0;
  if ((!cfg_int("geo_capture", 0) && !cfg_int("rt", 0)) || !id) return ci;
  uint32_t n = 0;
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> props(n);
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, props.data());
  bool have = false;
  for (auto& p : props) have |= !strcmp(p.extensionName, VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME);
  auto feats2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(id->gipa(id->instance, "vkGetPhysicalDeviceFeatures2"));
  if (!feats2) feats2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(id->gipa(id->instance, "vkGetPhysicalDeviceFeatures2KHR"));
  VkPhysicalDeviceTransformFeedbackFeaturesEXT xf{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT};
  VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &xf};
  if (feats2) feats2(phys, &f2);
  if (!have || !xf.transformFeedback) {
    log("[geo] VK_EXT_transform_feedback not available (ext %d, feature %d): geometry capture off", have,
        xf.transformFeedback);
    return ci;
  }
  for (auto p = static_cast<const VkBaseInStructure*>(ci->pNext); p; p = p->pNext)
    if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT) {
      log("[geo] the application already chains transform feedback features; leaving the device alone");
      return ci;
    }
  g_ext_names.assign(ci->ppEnabledExtensionNames, ci->ppEnabledExtensionNames + ci->enabledExtensionCount);
  bool listed = false;
  for (auto e : g_ext_names) listed |= !strcmp(e, VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME);
  if (!listed) g_ext_names.push_back(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME);
  g_xfb_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT};
  g_xfb_features.transformFeedback = VK_TRUE;
  g_xfb_features.pNext = const_cast<void*>(ci->pNext);
  *scratch = *ci;
  scratch->pNext = &g_xfb_features;
  scratch->enabledExtensionCount = uint32_t(g_ext_names.size());
  scratch->ppEnabledExtensionNames = g_ext_names.data();
  return scratch;
}

void geo_device_created(DeviceData* dd, bool xfb_enabled) {
  if (g_dd) return;
  g_dd = dd;
  p_set_viewport = reinterpret_cast<PFN_vkCmdSetViewport>(dd->gdpa(dd->device, "vkCmdSetViewport"));
  p_copy_buffer = reinterpret_cast<PFN_vkCmdCopyBuffer>(dd->gdpa(dd->device, "vkCmdCopyBuffer"));
  p_begin_query = reinterpret_cast<PFN_vkCmdBeginQuery>(dd->gdpa(dd->device, "vkCmdBeginQuery"));
  p_end_query = reinterpret_cast<PFN_vkCmdEndQuery>(dd->gdpa(dd->device, "vkCmdEndQuery"));
  p_dispatch = dd->vk.CmdDispatch;
  if (dd->inst && dd->inst->GetPhysicalDeviceProperties) {
    VkPhysicalDeviceProperties p;
    dd->inst->GetPhysicalDeviceProperties(dd->physical, &p);
    g_ts_period = p.limits.timestampPeriod;
    g_ubo_align = p.limits.minUniformBufferOffsetAlignment;
  }
  if (xfb_enabled) {
    p_bind_xfb = reinterpret_cast<PFN_vkCmdBindTransformFeedbackBuffersEXT>(
        dd->gdpa(dd->device, "vkCmdBindTransformFeedbackBuffersEXT"));
    p_begin_xfb = reinterpret_cast<PFN_vkCmdBeginTransformFeedbackEXT>(dd->gdpa(dd->device, "vkCmdBeginTransformFeedbackEXT"));
    p_end_xfb = reinterpret_cast<PFN_vkCmdEndTransformFeedbackEXT>(dd->gdpa(dd->device, "vkCmdEndTransformFeedbackEXT"));
    g_on = p_bind_xfb && p_begin_xfb && p_end_xfb && p_begin_query && p_end_query;
    // Track D phase 3: the UV goes along with the position, and the scene
    // comes from the depth prepass (rt_alpha needs rt=1; both are read here, once).
    g_alpha_mode = cfg_int("rt", 0) && cfg_int("rt_alpha", 0);
    g_stride = g_alpha_mode ? 24 : 16;
    log("[geo] transform feedback %s: vertex shaders get Position%s XFB decorations (stride %u)%s",
        g_on ? "enabled" : "FAILED to load", g_alpha_mode ? " + UV" : "", g_stride,
        g_alpha_mode ? "; ray tracing scene from the depth prepass" : "");
  }
  if (g_timing) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kTsSlots * 2;
    if (dd->vk.CreateQueryPool(dd->device, &qi, nullptr, &g_qp) != VK_SUCCESS) g_qp = VK_NULL_HANDLE;
    log("[geo] G-buffer pass timing %s", g_qp ? "on" : "unavailable");
  }
}

bool geo_patch_shader(const VkShaderModuleCreateInfo* ci, std::vector<uint32_t>& out) {
  t_last_uv = -1;
  if (!g_on) return false;
  bool ok = patch_vs(ci->pCode, ci->codeSize / 4, out, g_stride, &t_last_uv);
  if (!ok) return false;
  // geo_patch_dump=N writes the first N patched modules next to the shader
  // dumps, for spirv-val.
  if (int(g_patch_count) < cfg_int("geo_patch_dump", 0)) {
    char name[64];
    snprintf(name, sizeof name, "\\captures\\shaders\\%016llx.xfb.spv", (unsigned long long)hash64(ci->pCode, ci->codeSize));
    write_file(root_dir() + name, out.data(), out.size() * 4);
  }
  ++g_patch_count;
  return true;
}

void geo_note_module(VkShaderModule m, bool patched) {
  if (!patched) return;
  std::lock_guard<std::mutex> lock(g_mu);
  g_patched[m] = t_last_uv;   // same thread, right after geo_patch_shader
}

void geo_forget_module(VkShaderModule m) {
  if (!g_on) return;
  std::lock_guard<std::mutex> lock(g_mu);
  g_patched.erase(m);
}

void geo_note_pipelines(uint32_t n, const VkGraphicsPipelineCreateInfo* cis, const VkPipeline* out) {
  if (!g_on) return;
  std::lock_guard<std::mutex> lock(g_mu);
  for (uint32_t i = 0; i < n; ++i) {
    if (!out[i]) continue;
    PipeInfo pi{false, false, VK_PRIMITIVE_TOPOLOGY_MAX_ENUM};
    bool vs_patched = false, other = false;
    for (uint32_t s = 0; s < cis[i].stageCount; ++s) {
      const auto& st = cis[i].pStages[s];
      if (st.stage == VK_SHADER_STAGE_VERTEX_BIT) {
        auto it = st.module ? g_patched.find(st.module) : g_patched.end();
        vs_patched = it != g_patched.end();
        if (vs_patched) pi.uv = it->second;
      } else if (st.stage != VK_SHADER_STAGE_FRAGMENT_BIT) {
        other = true;
      }
    }
    pi.xfb = vs_patched && !other;
    bool dyn_topo = false;
    if (cis[i].pDynamicState)
      for (uint32_t d = 0; d < cis[i].pDynamicState->dynamicStateCount; ++d)
        dyn_topo |= cis[i].pDynamicState->pDynamicStates[d] == VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
    bool dyn_restart = false;
    if (cis[i].pDynamicState)
      for (uint32_t d = 0; d < cis[i].pDynamicState->dynamicStateCount; ++d)
        dyn_restart |= cis[i].pDynamicState->pDynamicStates[d] == VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE;
    if (cis[i].pInputAssemblyState && !dyn_topo) {
      VkPrimitiveTopology t = cis[i].pInputAssemblyState->topology;
      pi.topo = t;
      pi.restart = dyn_restart || cis[i].pInputAssemblyState->primitiveRestartEnable;
      pi.triangles = t == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST || t == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
                     t == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN || t == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY ||
                     t == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
    }
    g_pipes[out[i]] = pi;
  }
}

void geo_forget_pipeline(VkPipeline p) {
  if (!g_on) return;
  std::lock_guard<std::mutex> lock(g_mu);
  g_pipes.erase(p);
}

// Before the game's vkCmdBeginRenderPass (outside the pass).
void geo_pass_before(VkCommandBuffer cmd, CmdState* s) {
  s->geo = nullptr;
  s->geo_ts = -1;
  if (!g_dd) return;
  const bool gbuf = is_gbuffer(s->pass);
  if (gbuf) g_gbuf_w = s->pass.width, g_gbuf_h = s->pass.height;
  // The ray tracing scene's source pass: with rt_alpha the depth prepass
  // (depth-only D24S8 at render resolution), otherwise the G-buffer.
  // BG3 draws a few small depth-only passes before the big one: once seen, only
  // the big one's ordinal (its position in the command buffer) is captured.
  if (g_prepass_ordinal >= 0 && frame_index() > g_prepass_frame + 8) g_prepass_ordinal = -1;
  const bool prepass = is_prepass(s->pass) && (g_prepass_ordinal < 0 || int(s->pass.ordinal) == g_prepass_ordinal);
  const bool rt_src = g_alpha_mode ? prepass : gbuf;
  if (!gbuf && !rt_src) return;
  bool xfb = false;
  if (g_on) {
    std::lock_guard<std::mutex> lock(g_cap_mu);
    if (gbuf && trace_recording() && s->tracing && g_cap.state == 0 && frame_index() >= trace_start_frame() + 1 &&
        cfg_int("geo_capture", 0)) {
      if (start_capture(g_cap, true, s->pass.width, s->pass.height)) {
        g_cap.cmd = cmd;
        g_cap.viewport = {};
        s->geo = &g_cap;
        g_submit_wanted = true;
        g_dd->vk.CmdResetQueryPool(cmd, g_cap.qp, 0, g_cap.max_slots);
      }
    } else if (g_scratch.state != 1 && ((rt_src && rt_capture_wanted()) || (gbuf && cfg_int("geo_xfb_always", 0)))) {
      if (start_capture(g_scratch, false, s->pass.width, s->pass.height)) {
        g_scratch.cmd = cmd;
        g_scratch.rt = rt_src && rt_capture_wanted();
        s->geo = &g_scratch;
        // The previous frame's AS build read this buffer.
        if (g_scratch.rt) rt_before_capture(cmd);
      }
    }
    xfb = s->geo != nullptr;
  }
  // Timed: the ray tracing source pass (the G-buffer without rt_alpha).
  if (g_timing && g_qp && rt_src) {
    std::lock_guard<std::mutex> lock(g_ts_mu);
    uint32_t slot = uint32_t(frame_index() % kTsSlots);
    if (!g_ts_frame[slot]) {
      g_ts_frame[slot] = frame_index() + 1;
      g_ts_xfb[slot] = xfb;
      s->geo_ts = int(slot);
      g_dd->vk.CmdResetQueryPool(cmd, g_qp, slot * 2, 2);
      g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_qp, slot * 2);
    }
  }
}

// Right after the game's vkCmdBeginRenderPass (inside the pass).
void geo_pass_begun(VkCommandBuffer cmd, CmdState* s) {
  auto* c = static_cast<Capture*>(s->geo);
  if (!c) return;
  // Buffers are bound per draw (geo_pre_draw).
  (void)cmd;
}

// Right before the game's vkCmdEndRenderPass / vkCmdEndRendering (inside the pass).
void geo_pass_ending(VkCommandBuffer cmd, CmdState* s) {
  auto* c = static_cast<Capture*>(s->geo);
  if (!c || !c->xfb_active) return;
  p_end_xfb(cmd, 0, 0, nullptr, nullptr);
  c->xfb_active = false;
}

// The demand of this frame (next frame's alpha/opaque split) and the loud log of
// everything that did not make it into the RT soup as it should.
void rt_capture_stats(const Capture& c) {
  g_alpha_demand_prev = c.alpha_demand;
  g_opaque_demand_prev = c.cursor + c.opaque_overflow_bytes;
  std::lock_guard<std::mutex> lock(g_drop_mu);
  RtDropLog& l = g_drop;
  ++l.frames;
  l.alpha_overflow += c.alpha_overflow;
  l.alpha_overflow_mb += c.alpha_overflow_bytes >> 20;
  l.opaque_overflow += c.opaque_overflow;
  l.drop_indirect += c.drop_indirect;
  l.drop_topo += c.drop_topo;
  l.drop_topo_tris += c.drop_topo_tris;
  l.strips += c.strips;
  l.strip_tris += c.strip_tris;
  l.peak_alpha = (std::max)(l.peak_alpha, uint64_t(c.alpha_demand));
  l.peak_opaque = (std::max)(l.peak_opaque, uint64_t(c.cursor + c.opaque_overflow_bytes));
  const double mb = 1.0 / (1 << 20);
  if (c.alpha_overflow && !l.first_alpha) {
    l.first_alpha = true;
    log("[geo] WARNING alpha-tested region full (%.1f MB of %.1f MB wanted): %u alpha-tested draws (%.1f MB) made "
        "OPAQUE this frame (foliage and cutouts turn solid in RT). Raise rt_alpha_max_pct or rt_xfb_mb.",
        double(c.xfb.size - c.alpha_base) * mb, double(c.alpha_demand) * mb, c.alpha_overflow,
        double(c.alpha_overflow_bytes) * mb);
  }
  if (c.opaque_overflow && !l.first_opaque) {
    l.first_opaque = true;
    log("[geo] WARNING opaque region full (%.1f MB): %u draws (%.1f MB) left out of the RT scene this frame. Raise "
        "rt_xfb_mb.", double(c.opaque_end) * mb, c.opaque_overflow, double(c.opaque_overflow_bytes) * mb);
  }
  if ((c.drop_indirect || c.drop_topo) && !l.first_drop) {
    l.first_drop = true;
    log("[geo] WARNING draws left out of the RT scene this frame: %u indirect, %u with a topology that cannot be "
        "sized exactly (%llu triangles)", c.drop_indirect, c.drop_topo, (unsigned long long)c.drop_topo_tris);
  }
  int every = (std::max)(cfg_int("rt_timing_frames", 300), 30);
  if (l.frames >= uint64_t(every)) {
    log("[geo] RT capture, last %llu frames: alpha region %.0f MB (peak demand %.1f MB), opaque peak %.1f MB of a "
        "%.0f MB buffer; per frame: alpha draws made opaque (region full) %.2f, opaque draws dropped (buffer full) %.2f, "
        "indirect draws dropped %.2f, non-list draws dropped %.2f (%.0f triangles), strips/fans captured %.2f (%.0f "
        "triangles)",
        (unsigned long long)l.frames, double(g_alpha_region.load()) * mb, double(l.peak_alpha) * mb,
        double(l.peak_opaque) * mb, double(c.xfb.size) * mb, double(l.alpha_overflow) / l.frames,
        double(l.opaque_overflow) / l.frames, double(l.drop_indirect) / l.frames, double(l.drop_topo) / l.frames,
        double(l.drop_topo_tris) / l.frames, double(l.strips) / l.frames, double(l.strip_tris) / l.frames);
    l = RtDropLog{};
    l.first_alpha = l.first_opaque = l.first_drop = true;   // only the first occurrence is logged at once
  }
}

void geo_pre_draw(VkCommandBuffer cmd, CmdState* s, uint32_t kind, uint32_t count, uint32_t instances) {
  auto* c = static_cast<Capture*>(s->geo);
  if (!c) return;
  DrawRec d{kind, s->pipe_id[0], s->vs, s->fs, count, instances, -1, 0, VK_NULL_HANDLE, 0, 0, 0};
  PipeInfo pi{false, false, VK_PRIMITIVE_TOPOLOGY_MAX_ENUM};
  {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_pipes.find(s->pipe[0]);
    if (it != g_pipes.end()) pi = it->second;
  }
  if (c->rt && !c->rt_ubo) {
    Bg3Binding b;
    if (query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 2, 0, &b) &&
        (b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) &&
        b.offset % g_ubo_align == 0) {
      c->rt_ubo = b.buffer;
      c->rt_ubo_off = b.offset;
    }
  }
  if (c->readback && c->draws.size() < 65536) {
    Bg3Binding b;
    if (query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 2, 0, &b) &&
        (b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)) {
      d.ubo = b.buffer;
      d.ubo_off = b.offset;
    }
  }
  // Output size: exact for direct lists, an upper bound for strips/fans, a
  // fixed slab for indirect draws (the stream query tells what was written).
  uint64_t prims = 0;
  if (kind <= 1) {
    switch (pi.topo) {
      case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: prims = count / 3; break;
      case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: prims = count / 6; break;
      case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY: prims = count >= 6 ? (count - 4) / 2 : 0; break;
      default: prims = count > 2 ? count - 2 : 0; break;
    }
    prims *= instances;
  } else {
    prims = (uint64_t((std::max)(cfg_int("geo_indirect_mb", 8), 1)) << 20) / (3ull * g_stride);
  }
  const uint64_t tri_bytes = 3ull * g_stride;
  d.reserved = prims * tri_bytes;
  d.offset = c->cursor;
  if (!pi.xfb) d.skip = 1;
  else if (!pi.triangles) d.skip = 2;
  // The RT soup must be gap-free: only draws whose output size is exact. Strips
  // and fans are (count - 2 triangles per instance, each written as 3 vertices)
  // for non-indexed draws, and for indexed ones without primitive restart.
  // Indirect draws are not (the count is on the GPU): counted and logged.
  else if (c->rt && kind > 1) d.skip = 4, ++c->drop_indirect;
  else if (c->rt && pi.topo != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST &&
           !((pi.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP || pi.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) &&
             (kind == 0 || !pi.restart) && cfg_int("rt_strips", 1)))
    d.skip = 4, ++c->drop_topo, c->drop_topo_tris += prims;
  else if ((c->readback && c->slots >= c->max_slots) || c->cursor + d.reserved > c->opaque_end) d.skip = 3;
  bool capture = d.skip == 0 && d.reserved > 0;
  if (c->rt && d.skip == 0 && pi.topo != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) ++c->strips, c->strip_tris += prims;
  // Track D phase 3: an alpha-tested draw (its fragment shader is in the
  // table, the VS captured the UV it samples with) goes into the alpha region.
  bool alpha = false, alpha_wanted = false;
  if (c->rt && g_alpha_mode && d.skip != 1 && d.skip != 2 && d.skip != 4 && d.reserved > 0) {
    AlphaFs a;
    if (alpha_lookup(s->fs, &a) && a.uv == pi.uv && pi.uv >= 0 && cfg_int("rt_alpha_capture", 1)) {
      alpha_wanted = true;
      c->alpha_demand += d.reserved;
      if (c->alpha_cursor + d.reserved > c->xfb.size) {
        // Region full: the draw would silently turn opaque. Counted and logged.
        ++c->alpha_overflow;
        c->alpha_overflow_bytes += d.reserved;
      } else if (alpha_add_draw(cmd, a, uint32_t((c->alpha_cursor - c->alpha_base) / tri_bytes), uint32_t(prims))) {
        alpha = true;
        capture = true;
        d.offset = c->alpha_cursor;
      }
    }
  }
  if (c->rt && !alpha && (d.skip == 3 || (d.skip == 0 && c->cursor + d.reserved > c->opaque_end)))
    ++c->opaque_overflow, c->opaque_overflow_bytes += d.reserved;
  (void)alpha_wanted;
  if (capture && c->rt) {
    // Draw identity for the refit decision: same draws in the same order give
    // the same triangle order.
    for (uint64_t v : {uint64_t(d.pipe_id), uint64_t(count), uint64_t(instances), uint64_t(alpha)})
      c->sig = (c->sig ^ v) * 1099511628211ull;
    ++c->rt_draws;
    c->alpha_draws += alpha;
  }
  if (capture) {
    VkDeviceSize off = d.offset, size = d.reserved;
    p_bind_xfb(cmd, 0, 1, &c->xfb.b, &off, &size);
    p_begin_xfb(cmd, 0, 0, nullptr, nullptr);
    c->xfb_active = true;
    (alpha ? c->alpha_cursor : c->cursor) += d.reserved;
  }
  if (capture && c->readback) {
    d.slot = int32_t(c->slots);
    p_begin_query(cmd, c->qp, uint32_t(d.slot), 0);
    c->active_slot = d.slot;
  }
  if (c->readback) c->draws.push_back(d);
}

void geo_post_draw(VkCommandBuffer cmd, CmdState* s) {
  auto* c = static_cast<Capture*>(s->geo);
  if (!c) return;
  if (c->active_slot >= 0) {
    p_end_query(cmd, c->qp, uint32_t(c->active_slot));
    c->active_slot = -1;
    ++c->slots;
  }
  if (c->xfb_active) {
    p_end_xfb(cmd, 0, 0, nullptr, nullptr);
    c->xfb_active = false;
  }
}

// After the game's vkCmdEndRenderPass (outside the pass).
void geo_pass_after(VkCommandBuffer cmd, CmdState* s) {
  if (s->geo_ts >= 0 && g_qp) g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qp, s->geo_ts * 2 + 1);
  s->geo_ts = -1;
  auto* c = static_cast<Capture*>(s->geo);
  s->geo = nullptr;
  if (!c) return;
  if (!c->readback) {
    // rt_alpha: a small depth-only pass (not the scene's) is captured once
    // until the big one is known, and dropped.
    const bool minor_pass = g_alpha_mode && s->pass.draw_count < kPrepassMinDraws;
    if (g_alpha_mode && !minor_pass) {
      g_prepass_ordinal = int(s->pass.ordinal);
      g_prepass_frame = frame_index();
    }
    if (c->rt && !minor_pass) {
      rt_capture_stats(*c);
      RtCapture rc{};
      rc.xfb = c->xfb.b;
      rc.opaque_bytes = c->cursor;
      rc.alpha_base = c->alpha_base;
      rc.alpha_bytes = c->alpha_cursor - c->alpha_base;
      rc.stride = g_stride;
      rc.alpha_region = c->alpha_base < c->xfb.size;
      rc.ubo = c->rt_ubo;
      rc.ubo_offset = c->rt_ubo_off;
      rc.signature = c->sig;
      rc.draws = c->rt_draws;
      rc.alpha_draws = c->alpha_draws;
      rt_after_capture(cmd, rc);
    }
    // Next frame's capture into the same scratch memory comes after this one.
    barrier(cmd, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
            VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT | VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
            VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT | VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT |
                VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT);
    std::lock_guard<std::mutex> lock(g_cap_mu);
    c->state = 0;
    return;
  }
  if (s->has_viewport) c->viewport = s->viewport;
  barrier(cmd, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
          VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT | VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,
          VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
  c->depth_format = s->pass.depth_format;
  c->have_depth = copy_image(cmd, s->pass.depth_image, s->depth_layout, s->pass.depth_format, true, c->w, c->h, c->depth);
  c->normal_format = s->pass.color_formats[0];
  c->have_normals =
      copy_image(cmd, s->pass.color_images[0], s->color_layouts[0], s->pass.color_formats[0], false, c->w, c->h, c->normals);
  // Camera UBO of the first captured draw: the game's buffer must allow copies.
  for (const auto& d : c->draws) {
    if (!d.ubo) continue;
    VkBufferUsageFlags usage = 0;
    {
      std::lock_guard<std::mutex> lock(g_mu);
      auto it = g_buffer_usage.find(d.ubo);
      if (it != g_buffer_usage.end()) usage = it->second;
    }
    c->ubo_usage = usage;
    if ((usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) && make_buf(c->ubo, 256, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
      VkBufferCopy region{d.ubo_off, 0, 224};
      p_copy_buffer(cmd, d.ubo, c->ubo.b, 1, &region);
      c->ubo_bytes = 224;
      c->have_ubo = true;
    } else if ((usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) && d.ubo_off % g_ubo_align == 0 && ubo_pipeline() &&
               make_buf(c->ubo, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)) {
      // Shader read of the game's UBO; the game's compute bindings are restored after.
      VkDescriptorBufferInfo in{d.ubo, d.ubo_off, 224}, out{c->ubo.b, 0, 256};
      VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
      w[0].dstBinding = 0;
      w[0].descriptorCount = 1;
      w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      w[0].pBufferInfo = &in;
      w[1].dstBinding = 1;
      w[1].descriptorCount = 1;
      w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w[1].pBufferInfo = &out;
      g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_ubo_pipe);
      g_dd->vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_ubo_pl, 0, 2, w);
      p_dispatch(cmd, 1, 1, 1);
      barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
              VK_ACCESS_HOST_READ_BIT);
      restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
      c->ubo_bytes = 224;
      c->have_ubo = true;
    }
    break;
  }
  barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
          VK_ACCESS_HOST_READ_BIT);
  std::lock_guard<std::mutex> lock(g_cap_mu);
  c->state = 2;
}

bool geo_submit_wanted() { return g_submit_wanted.load(std::memory_order_relaxed); }

void geo_on_submit(const std::vector<VkCommandBuffer>& cmds) {
  std::lock_guard<std::mutex> lock(g_cap_mu);
  if (g_cap.state != 2) return;
  for (VkCommandBuffer c : cmds)
    if (c == g_cap.cmd) {
      g_cap.frame_tag = frame_index();
      g_cap.state = 3;
      g_submit_wanted = false;
    }
}

static void try_process(VkQueue queue) {
  std::unique_lock<std::mutex> lock(g_cap_mu);
  if (g_cap.state == 0 || g_cap.dir.empty()) return;
  if (g_cap.state != 3 || frame_index() <= g_cap.frame_tag) {
    if (frame_index() > g_cap.wait_until) {
      log("[geo] capture never submitted (state %d); dropped", g_cap.state);
      g_submit_wanted = false;
      release(g_cap);
      g_cap.dir.clear();
    }
    return;
  }
  lock.unlock();
  g_dd->vk.QueueWaitIdle(queue);
  if (g_dd->graphics_queue && g_dd->graphics_queue != queue) g_dd->vk.QueueWaitIdle(g_dd->graphics_queue);
  process(g_cap);
  lock.lock();
  release(g_cap);
  g_cap.dir.clear();
}

void geo_finish(uint64_t frame, const std::string& dir, VkQueue queue) {
  {
    std::lock_guard<std::mutex> lock(g_cap_mu);
    if (g_cap.state == 0) {
      if (g_on && cfg_int("geo_capture", 0)) log("[geo] no G-buffer pass was captured in frame %llu", (unsigned long long)frame);
      return;
    }
    g_cap.dir = dir;
    g_cap.wait_until = frame_index() + 120;
  }
  try_process(queue);
}

void geo_on_present(VkQueue queue) {
  if (g_cap.state != 0) try_process(queue);
  if (!g_timing || !g_qp) return;
  std::lock_guard<std::mutex> lock(g_ts_mu);
  uint64_t now = frame_index();
  for (uint32_t s = 0; s < kTsSlots; ++s) {
    if (!g_ts_frame[s] || g_ts_frame[s] + 6 > now) continue;
    uint64_t r[4] = {};
    VkResult res = g_dd->vk.GetQueryPoolResults(g_dd->device, g_qp, s * 2, 2, sizeof r, r, 16,
                                                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    bool done = res == VK_SUCCESS && r[1] && r[3];
    if (!done && g_ts_frame[s] + 40 > now) continue;  // not executed yet
    g_ts_frame[s] = 0;
    if (!done || r[2] < r[0]) continue;
    float ms = float(double(r[2] - r[0]) * g_ts_period * 1e-6);
    TsAcc& a = g_ts_acc[g_ts_xfb[s] ? 1 : 0];
    a.v.push_back(ms);
  }
  int every = (std::max)(cfg_int("geo_timing_frames", 300), 30);
  for (int k = 0; k < 2; ++k) {
    TsAcc& a = g_ts_acc[k];
    if (int(a.v.size()) < every) continue;
    std::sort(a.v.begin(), a.v.end());
    double sum = 0;
    for (float v : a.v) sum += v;
    log("[geo] G-buffer pass GPU time (%s, %zu frames): median %.3f ms, mean %.3f, p10 %.3f, p90 %.3f",
        k ? "XFB capture active" : g_on ? "shaders patched, XFB idle" : "unpatched", a.v.size(), a.v[a.v.size() / 2],
        sum / a.v.size(), a.v[a.v.size() / 10], a.v[a.v.size() * 9 / 10]);
    a.v.clear();
  }
}

void geo_device_destroyed(DeviceData* dd) {
  if (dd != g_dd) return;
  log("[geo] %u vertex shader modules were patched for transform feedback", g_patch_count.load());
  {
    std::lock_guard<std::mutex> lock(g_cap_mu);
    release(g_cap);
    release(g_scratch);
  }
  if (g_qp) dd->vk.DestroyQueryPool(dd->device, g_qp, nullptr);
  if (g_ubo_pipe) dd->vk.DestroyPipeline(dd->device, g_ubo_pipe, nullptr);
  if (g_ubo_pl) dd->vk.DestroyPipelineLayout(dd->device, g_ubo_pl, nullptr);
  if (g_ubo_dsl) dd->vk.DestroyDescriptorSetLayout(dd->device, g_ubo_dsl, nullptr);
  g_ubo_pipe = VK_NULL_HANDLE;
  g_ubo_pl = VK_NULL_HANDLE;
  g_ubo_dsl = VK_NULL_HANDLE;
  g_qp = VK_NULL_HANDLE;
  g_on = false;
  g_dd = nullptr;
}

void register_geo_hooks(HookMap& m) {
  m["vkCmdSetViewport"] = reinterpret_cast<PFN_vkVoidFunction>(&CmdSetViewport);
  m["vkCreateBuffer"] = reinterpret_cast<PFN_vkVoidFunction>(&CreateBuffer);
  m["vkDestroyBuffer"] = reinterpret_cast<PFN_vkVoidFunction>(&DestroyBuffer);
}

}  // namespace bg3
