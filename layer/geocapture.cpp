#include "../common/rt_capture_budget.h"
#include "../common/omm_native_plain_recipes.h"
#include "../common/sun_raster_domain.h"
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
#include "hit_material.h"
#include "../common/native_hit_writes.h"
#include "../common/native_hit_pipeline.h"
#include "../common/native_hit_inputs.h"
#include "../common/native_tileset_coverage.h"
#include "../common/native_hit_tileset_inputs.h"
#include "../common/native_hit_qualify.h"
#include "../common/native_hit_pool.h"
#include "../common/spirv_visibility.h"
#include "spirv_xfb.h"
#include "../common/cascade_casters.h"

#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <condition_variable>
#include <chrono>
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
PFN_vkCmdSetScissor p_set_scissor=nullptr;
PFN_vkCmdBeginQuery p_begin_query = nullptr;
PFN_vkCmdEndQuery p_end_query = nullptr;
PFN_vkCmdDispatch p_dispatch = nullptr;
VkDeviceSize g_ubo_align = 256;
// Camera UBO copy (shaders/geo_ubo_copy.hlsl), push descriptors.
VkDescriptorSetLayout g_ubo_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_ubo_pl = VK_NULL_HANDLE;
VkPipeline g_ubo_pipe = VK_NULL_HANDLE;
float g_ts_period = 1.0f;
VkDeviceSize g_storage_range = 0;

// Storage for the modified device create info (one device).
std::vector<const char*> g_ext_names;
VkPhysicalDeviceTransformFeedbackFeaturesEXT g_xfb_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT};

// XFB vertex layout, fixed at device creation: 16 bytes (gl_Position), or 24
// with the UV output (Track D phase 3, rt=1 and rt_alpha=1). With rt_alpha the
// per-frame scene comes from the depth prepass (its fragment shaders are the
// ones that alpha test), otherwise from the G-buffer pass.
uint32_t g_stride = 16;
bool g_tileset_capture = false;
uint32_t g_stride_without_tileset=16;
bool g_alpha_mode = false;
// The effective capture sizes: a cap left at its default follows the stride (common/rt_capture_budget.h).
int xfb_mb() {
  return capture_budget::effective_mb(cfg_int("rt_xfb_mb", 384), capture_budget::kXfbDefaultMb, g_stride);
}
int cascade_mb() {
  return (std::clamp)(capture_budget::effective_mb(cfg_int("rt_cascade_mb", 160), capture_budget::kCascadeDefaultMb, g_stride),
                      4, 1024);
}
std::atomic<uint32_t> g_gbuf_w{0}, g_gbuf_h{0};   // render resolution, from the last G-buffer pass
std::atomic<uint64_t> g_gbuf_depth{0};            // the last G-buffer pass's depth image (the prepass writes it)
thread_local int t_last_uv = -1;       // UV location of the module patched last on this thread
thread_local uint32_t t_last_hair_outputs = 0;
thread_local uint32_t t_last_vt_outputs = 0;
thread_local uint32_t t_last_tileset_outputs = 0;
thread_local bool t_last_no_writes = false;
thread_local VsInstancing t_last_inst; // its instancing analysis (bvhsplit.cpp)
thread_local uint64_t t_last_hash = 0;
thread_local bool t_last_discard = false;   // the module last scanned on this thread is a fragment shader that discards
thread_local bool t_last_always_discard = false;
thread_local bool t_last_fragment_depth = false;
thread_local std::vector<StageIo> t_last_io;   // the module's user interface (stage_io)
thread_local int t_last_model = -1;

std::mutex g_mu;  // modules, pipelines, buffers
// Interface of every module (the UV-slot check): execution model, user inputs/outputs, hash.
struct ModIo { int model = -1; std::vector<StageIo> io; uint64_t hash = 0; bool no_writes=false; };
std::unordered_map<VkShaderModule, ModIo> g_mod_io;
// UV-slot verdicts over every pipeline created, and the distinct mismatching VS/FS pairs.
std::atomic<uint32_t> g_uv_count[5];
std::unordered_map<uint64_t, uint32_t> g_uv_bad_pairs;   // hash(vs, fs) -> count
struct ModInfo {
  int uv = -1;               // UV output location (-1 none)
  VsInstancing inst;         // class-A position path (spirv_xfb.h)
  uint64_t hash = 0;         // SPIR-V hash of the game's module
  uint32_t hair_outputs = 0;
  uint32_t vt_outputs = 0;
  uint32_t tileset_outputs = 0;
};
std::unordered_map<VkShaderModule, ModInfo> g_patched;
std::unordered_set<VkShaderModule> g_discard_mods;   // fragment shaders that can discard (alpha test)
std::unordered_set<VkShaderModule> g_always_discard_mods;
std::unordered_set<VkShaderModule> g_fragment_depth_mods;
struct PipeInfo {
  // Populated after aggregate initialization below, keeps legacy field order.
  bool xfb;                  // VS patched, VS is the last pre-raster stage
  bool triangles;            // triangle topology (list/strip/fan, with or without adjacency)
  VkPrimitiveTopology topo;
  int uv = -1;               // VS output location captured as UV (stride 24)
  uint32_t hair_outputs = 0;
  uint32_t vt_outputs = 0;
  uint32_t tileset_outputs = 0;
  uint8_t uv_match = 0;      // UvMatch of the VS/FS pair (spirv_xfb.h)
  int8_t uv_fs = -1;         // the FS's float2 input location
  uint8_t uv_vs_comps = 0;   // components the VS writes there
  bool restart = true;       // primitive restart enabled, or dynamic (unknown)
  InstPipe inst;             // instancing layout (Track D BVH split)
  bool discard = false;      // its fragment shader can discard (rt_cascade_casters skips it: no alpha test)
  bool always_discard = false;
  bool fragment_depth = false;
  bool fragment_known = true;
  bool no_storage_writes = false;
  native_hit::StaticInputs material_inputs{};
  native_hit::TilesetInputs tileset_material_inputs{};
  uint8_t tileset_family=0;
  bool material_state = false;
  uint32_t material_reasons=0,raster_next=0,multisample_next=0,blend_next=0;
  bool native_view_known=false,dynamic_viewport=false,dynamic_scissor=false;
  VkViewport native_viewport{};VkRect2D native_scissor{};
  bool finite_sun_clip = false; // observed Vulkan z=[0,1], clipping enabled, no clamp
  int depth_write = -1, depth_test = -1; // -1: missing or dynamic; never reject unknown state
  uint32_t color_mask = 0, blend_mask = 0;
  bool em_state = false, em_dynamic = false;
  uint32_t em_raster[9] = {}; // blend/src/dst/op/mask/cull/depth-test/write/compare
};
std::unordered_map<VkPipeline, PipeInfo> g_pipes;
std::unordered_map<VkBuffer, VkBufferUsageFlags> g_buffer_usage;
std::unordered_map<VkBuffer, VkDeviceSize> g_buffer_size;
std::atomic<uint32_t> g_patch_count{0}, g_patch_skipped{0}, g_inst_modules{0}, g_inst_pipes{0};

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
  if(dd->vk.BindBufferMemory(dd->device,out.b,out.m,0)!=VK_SUCCESS ||
     (host && dd->vk.MapMemory(dd->device,out.m,0,VK_WHOLE_SIZE,0,&out.map)!=VK_SUCCESS)){
    dd->vk.DestroyBuffer(dd->device,out.b,nullptr);
    dd->vk.FreeMemory(dd->device,out.m,nullptr);out=Buf{};return false;
  }
  out.size = size;
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
  bool split = false;        // rt_bvh_split: cached instanced meshes are not captured (bvhsplit.cpp)
  VkBuffer rt_ubo = VK_NULL_HANDLE;
  VkDeviceSize rt_ubo_off = 0;
  uint64_t sig = 0;
  uint32_t rt_draws = 0;
  bool state_log = false, state_sample = false;
  bool discard_filter = false;
  bool fade_class = false;   // rt_primary_fade_class: mark camera-fade draws (diagnostics only)
  uint64_t discard_skipped = 0;
  uint64_t source_draws = 0, no_depth_draws = 0, no_depth_tris = 0;
  struct StateCount { uint64_t fs = 0, draws = 0, tris = 0; PipeInfo pipe{}; };
  std::unordered_map<uint64_t, StateCount> states;
  // rt_alpha: opaque draws fill [0, opaque_end), alpha-tested ones
  // [alpha_base, xfb.size) from alpha_cursor.
  VkDeviceSize opaque_end = 0, alpha_base = 0, alpha_cursor = 0;
  uint32_t alpha_draws = 0;
  // What this frame wanted (for the next frame's split, AUDIT_RT fix 5) and what
  // did not fit; draws left out of the RT soup (fix 7).
  VkDeviceSize alpha_demand = 0, alpha_overflow_bytes = 0, opaque_overflow_bytes = 0;
  uint32_t alpha_overflow = 0, opaque_overflow = 0;
  VkDeviceSize alpha_spill_bytes = 0;
  uint32_t alpha_spills = 0;
  uint32_t drop_indirect = 0, drop_topo = 0, strips = 0;
  uint64_t drop_topo_tris = 0, strip_tris = 0;
  // rt_cascade_casters. Camera source pass: its draw keys are recorded (cc_keys). A cascade
  // pass (cascade): the captured one, or a probe that only reads the cascade's matrix.
  bool cc_keys = false;
  bool cascade = false, probe = false;
  int layer = -1;
  int ts = -1;                                       // timestamp pair of the captured pass
  uint64_t rec = 0;                                  // build record of its command buffer (submitted?)
  uint32_t c_seen = 0, c_dup = 0, c_alpha = 0, c_drop = 0, c_ubo = 0, c_full = 0, c_added = 0;
  uint64_t c_sig = 1469598103934665603ull;   // cascade: the added draws in order (rt_cascade_refit)
  uint64_t c_tris = 0;
  sun_coverage::NativeEpoch c_native_epoch;
  VkBuffer c_native_frame = VK_NULL_HANDLE;
  VkDeviceSize c_native_offset = 0;
  bool c_epoch_ok = false;
  uint32_t c_material_unknown = 0;
  bool c_finite_sun_clip = true,c_native_raster_valid=true,c_have_native_viewport=false;
  VkViewport c_native_viewport{};
  bool c_fresh_depth=false;
  VkImage c_depth_image = VK_NULL_HANDLE;
  std::vector<SunCascadeDraw> c_sun_draws;
  std::vector<AlphaSnapshot> c_sun_materials;
  bool c_legacy_transfer=false;
  Capture* c_legacy_target=nullptr;
  std::vector<VkBufferCopy> c_legacy_copies;
  VkDeviceSize c_legacy_bytes=0;
  uint64_t c_legacy_sig=1469598103934665603ull;
};

// The split of the RT capture's XFB buffer between opaque and alpha-tested
// triangles, sized from the previous frame's demand (rt_alpha_auto=1).
std::atomic<uint64_t> g_alpha_region{0}, g_alpha_demand_prev{0}, g_opaque_demand_prev{0};
// Logged loudly: per-window totals, and the first occurrence at once.
struct RtDropLog {
  uint64_t frames = 0, alpha_overflow = 0, alpha_overflow_mb = 0, opaque_overflow = 0, drop_indirect = 0, drop_topo = 0,
           drop_topo_tris = 0, strips = 0, strip_tris = 0;
  uint64_t peak_alpha = 0, peak_opaque = 0, alpha_spills = 0, alpha_spill_bytes = 0;
  uint64_t source_draws = 0, no_depth_draws = 0, no_depth_tris = 0;
  bool first_alpha = false, first_opaque = false, first_drop = false;
};
std::mutex g_drop_mu;
RtDropLog g_drop;

std::mutex g_cap_mu;
Capture g_cap;                 // the readback capture (one per trace)
Capture g_scratch;             // the device-memory capture (the RT scene source)
std::atomic<bool> g_submit_wanted{false};

// An effects pass of the emissive capture (D-EM, emissive.cpp): CmdState::geo holds
// one with Kind::Em. Passes are recorded on several threads.
struct EmPass {
  bool em = false;           // D-EM engaged: em_pass_begin captures or times this pass
  bool capture = false;      // rt_emissive: draws are captured (otherwise only timed)
  bool water = false;        // rt_water_geometry: water draws are captured (water.cpp)
  bool xfb_active = false;
  bool water_swap = false;   // M5.3: the composition variant is bound for this draw (water_unswap after)
  uint32_t draws = 0, water_draws = 0;      // all draws of the pass; water draws (water.cpp census)
  std::vector<WaterRange> water_ranges;     // captured water draws
  int ts = -1;
  VkBuffer ubo = VK_NULL_HANDLE;   // the pass's camera UBO (set 0 binding 2)
  VkDeviceSize ubo_off = 0;
  std::vector<std::pair<VkDeviceSize, uint32_t>> ranges;   // captured (offset, triangles)
};

// CmdState::geo, typed by its kind.
using GeoKind = CmdState::GeoPass::Kind;
bool geo_is(const CmdState* s, GeoKind k) { return s->geo.kind == k; }
Capture* geo_capture(CmdState* s) { return geo_is(s, GeoKind::Capture) ? static_cast<Capture*>(s->geo.object) : nullptr; }
EmPass* geo_empass(CmdState* s) { return geo_is(s, GeoKind::Em) ? static_cast<EmPass*>(s->geo.object) : nullptr; }
void set_geo(CmdState* s, Capture* c) { s->geo = c ? CmdState::GeoPass{GeoKind::Capture, c} : CmdState::GeoPass{}; }
void set_geo(CmdState* s, EmPass* e) { s->geo = e ? CmdState::GeoPass{GeoKind::Em, e} : CmdState::GeoPass{}; }

// ---- timing ----
constexpr uint32_t kTsSlots = 64;
VkQueryPool g_qp = VK_NULL_HANDLE;
std::mutex g_ts_mu;
uint64_t g_ts_frame[kTsSlots];        // frame the slot was written in (0 = free)
bool g_ts_xfb[kTsSlots];
bool g_ts_moving[kTsSlots];           // camera moving in that frame (rt_camera_moving)
bool g_ts_split[kTsSlots];            // rt_bvh_split
struct TsAcc { double sum = 0; uint32_t n = 0; std::vector<float> v; };
TsAcc g_ts_acc[2][2][2];              // [xfb inactive/active][static/moving][legacy/split]

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
         p.width == g_gbuf_w && p.height == g_gbuf_h &&
         (!g_gbuf_depth || uint64_t(p.depth_image) == g_gbuf_depth);
}
constexpr uint32_t kPrepassMinDraws = 64;
std::atomic<uint64_t> g_scene_frame1{0};   // frame_index()+1 of the last non-minor prepass capture
std::atomic<int> g_scene_ordinal{-1};      // its ordinal (logged when it moves)
std::atomic<uint32_t> g_scene_moves{0}, g_minor_dropped{0};

// ---- hooks of our own ----
VKAPI_ATTR void VKAPI_CALL CmdSetViewport(VkCommandBuffer cmd, uint32_t first, uint32_t n, const VkViewport* vps) {
  p_set_viewport(cmd, first, n, vps);
  if (first == 0 && n)
    if (CmdState* s = cmd_state(cmd)) {
      s->viewport = vps[0];
      s->has_viewport = true;
    }
}

VKAPI_ATTR void VKAPI_CALL CmdSetScissor(VkCommandBuffer cmd,uint32_t first,uint32_t count,const VkRect2D* scissors) {
  p_set_scissor(cmd,first,count,scissors);
  if(first==0 && count) if(auto* state=cmd_state(cmd)) {state->scissor=scissors[0];state->has_scissor=true;}
}

VKAPI_ATTR VkResult VKAPI_CALL CreateBuffer(VkDevice device, const VkBufferCreateInfo* ci,
                                            const VkAllocationCallbacks* alloc, VkBuffer* out) {
  VkResult r = device_of(device)->vk.CreateBuffer(device, ci, alloc, out);
  if (r == VK_SUCCESS) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_buffer_usage[*out] = ci->usage;
    g_buffer_size[*out] = ci->size;
  }
  return r;
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
    const int set_mb = rt ? cfg_int("rt_xfb_mb", 384) : cfg_int("geo_xfb_mb", 768);
    VkDeviceSize mb = VkDeviceSize((std::max)(rt ? xfb_mb() : set_mb, 16));
    if (rt) log("[geo] RT capture buffer %llu MB (rt_xfb_mb %d, %u-byte vertices)", (unsigned long long)mb, set_mb, g_stride);
    if(rt && !capture_budget::storage_range_supported(mb<<20,g_storage_range)){
      log("[geo] RT capture refused: %llu MB needs full SSBO range, device limit %llu bytes",
          (unsigned long long)mb,(unsigned long long)g_storage_range);return false;
    }
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
  c.split = false;
  c.rt_ubo = VK_NULL_HANDLE;
  c.rt_ubo_off = 0;
  c.sig = 1469598103934665603ull;
  c.rt_draws = 0;
  c.state_log = cfg_int("rt_capture_state_log", 0) != 0;
  c.state_sample = c.state_log && c.record_frame % (std::max)(cfg_int("rt_timing_frames", 300), 30) == 0;
  c.discard_filter = cfg_int("rt_capture_discard", 1) != 0;
  c.fade_class = cfg_int("rt_primary_fade_class", 0) != 0;
  c.discard_skipped = 0;
  c.source_draws = c.no_depth_draws = c.no_depth_tris = 0;
  c.states.clear();
  c.opaque_end = c.xfb.size;
  c.alpha_base = c.alpha_cursor = c.xfb.size;
  c.alpha_draws = 0;
  c.alpha_demand = c.alpha_overflow_bytes = c.opaque_overflow_bytes = 0;
  c.alpha_overflow = c.opaque_overflow = 0;
  c.alpha_spill_bytes = 0; c.alpha_spills = 0;
  c.drop_indirect = c.drop_topo = c.strips = 0;
  c.drop_topo_tris = c.strip_tris = 0;
  if (!readback && g_alpha_mode && rt_enabled()) {
    // Alpha region: rt_alpha_mb at first; with rt_alpha_auto, 1.5x the last
    // frame's demand (grown at once, shrunk only below a third of it), at least
    // rt_alpha_mb, capped by rt_alpha_max_pct of the buffer and by what the
    // opaque triangles needed last frame.
    const VkDeviceSize mb = VkDeviceSize(1) << 20, size = c.xfb.size;
    VkDeviceSize floor_b = VkDeviceSize((std::max)(cfg_int("rt_alpha_mb", 64), 1)) * mb;
    const VkDeviceSize amb=capture_budget::alpha_region(size,floor_b,g_alpha_region.load(),
      g_alpha_demand_prev.load(),g_opaque_demand_prev.load(),cfg_int("rt_alpha_auto",1)!=0,
      cfg_int("rt_alpha_max_pct",60));
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
  c.c_legacy_transfer=false;c.c_legacy_target=nullptr;c.c_legacy_copies.clear();
  c.draws.clear();
  c.draws.shrink_to_fit();
}

// ---- rt_cascade_casters (docs/TRACK_D_RT.md 6.7 item 2, common/cascade_casters.h) ----
// The sun cascades are separate depth-only D16 passes, one layer each of one array, recorded
// in their own command buffers (possibly on other threads) after the G-buffer. Every cascade
// VS ends like the camera's: gl_Position = vec4(W, 1) * set0.b2._m2 with y negated (shader
// dumps of captures/20261026-165848: 1cf36b97, a7bf37c6, 646a930e, dcb21688, ...), so the
// cascade's own camera-layout UBO holds its orthographic view-projection. The captured
// cascade (rt_cascade_index) goes into a buffer of its own per frame parity, is converted in
// place after its pass, and the NEXT frame's build adds it (rtscene.cpp, bvhsplit.cpp).
std::mutex g_cc_mu;                 // everything below
std::condition_variable g_cc_cv;
cascade::KeySets g_cc_keys;         // the camera source pass's draw keys (this frame and the last)
cascade::KeySets g_cc_mesh_keys;    // its mesh keys (alpha-tested cascade draws drawn by it are skipped)
Capture g_ccap[3];                  // the captured cascade pass of frame F is g_ccap[F % 3] (a build may use F-1 or F-2)
struct CcMeta {
  uint64_t frame = 0; uint32_t tris = 0; uint64_t rec = 0;
  bool has_origin = false; float origin[3] = {}; uint64_t sig = 0;
  SunCascadeCapture sun;
};
CcMeta g_cc_meta[3];                // what g_ccap[i] holds, converted (frame 0: nothing)
// Current-only complementary layers. The configured legacy layer keeps its
// existing ring; these buffers are allocated lazily only with the new key on.
Capture g_sun_ccap[4][3];
CcMeta g_sun_meta[4][3];
constexpr int kCcLayers = 8;
Capture g_cprobe[kCcLayers];        // the other cascades: their matrix is read, nothing captured
struct CcMat { uint64_t frame = 0; float v[56] = {}; };
CcMat g_cc_mat[kCcLayers];          // each cascade's camera-layout UBO (frame 0: never read)
bool g_cc_seen[kCcLayers] = {};
bool g_cc_alloc_failed = false;
VkQueryPool g_cc_qp = VK_NULL_HANDLE;   // GPU time of the captured pass (incl. the conversion)
constexpr uint32_t kCcTs = 16;
uint64_t g_cc_ts_frame[kCcTs] = {};
std::vector<float> g_cc_ms;
std::atomic<uint64_t> g_cc_alpha_known{0}, g_cc_alpha_uv{0};   // skipped alpha-tested cascade draws
struct CcStats {
  uint64_t frames = 0, seen = 0, dup = 0, alpha = 0, drop = 0, ubo = 0, full = 0, added = 0, tris = 0, tris_max = 0;
  uint64_t bytes = 0, bytes_max = 0, keys = 0, unconverted = 0;
  uint64_t builds[6] = {}, build_tris = 0;
};
CcStats g_cc_st;

// The cascade index (array layer) of a sun cascade pass, -1 for any other pass.
int cascade_layer(const Bg3PassInfo& p) {
  if (p.color_count != 0 || p.depth_format != VK_FORMAT_D16_UNORM || !p.depth_image || !p.depth_view) return -1;
  uint32_t layers = 0, base = 0;
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    auto im = tracker().images.find(p.depth_image);
    auto vw = tracker().views.find(p.depth_view);
    if (im == tracker().images.end() || vw == tracker().views.end()) return -1;
    layers = im->second.ci.arrayLayers;
    base = vw->second.range.baseArrayLayer;
  }
  return cascade::pass_layer(p.color_count, true, p.width, p.height, layers, base);
}

// The draw's identity for the camera/cascade dedupe. place 0: unknown (never deduplicated).
cascade::DrawId cascade_draw_id(VkCommandBuffer cmd, const CmdState* s, const DrawArgs& a, const PipeInfo& pi) {
  cascade::DrawId d;
  d.kind = a.kind, d.count = a.count, d.instances = a.instances, d.first = a.first;
  d.first_instance = a.first_instance, d.vertex_offset = a.vertex_offset;
  d.vb = uint64_t(s->vb[0].buffer), d.vb_offset = s->vb[0].offset;
  d.ib = uint64_t(s->ib), d.ib_offset = s->ib_offset, d.ib_type = uint32_t(s->ib_type);
  if (a.kind > 1) return d;
  if (pi.inst.ok) {   // instanced: the instance stream places it
    const CmdState::VertexBinding& vb = s->vb[pi.inst.row_binding[0]];
    d.place = uint64_t(vb.buffer), d.place_offset = vb.offset;
  } else {            // per-draw UBO (set 1 binding 0: the model matrix of the depth-only VS)
    Bg3Binding b;
    if (query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 0, 0, &b) && b.buffer)
      d.place = uint64_t(b.buffer), d.place_offset = b.offset;
  }
  return d;
}

void cascade_pass_before(VkCommandBuffer cmd, CmdState* s, int layer) {
  const uint64_t f = frame_index();
  const int want = cfg_hot(CfgHot::rt_cascade_index);
  std::lock_guard<std::mutex> lock(g_cc_mu);
  if (layer < kCcLayers && !g_cc_seen[layer]) {
    g_cc_seen[layer] = true;
    log("[cascade] sun cascade pass found: layer %d of a %ux%u D16 array; %s", layer, s->pass.width, s->pass.height,
        layer == want ? "captured (rt_cascade_index)" : "its matrix is only read");
  }
  const bool all_current=cfg_hot(CfgHot::rt_sun_cascade_current)!=0 && layer>=0 && layer<4;
  if (layer != want && !all_current) {
    if (layer < kCcLayers) {
      Capture& p = g_cprobe[layer];
      p.cascade = p.probe = true;
      p.layer = layer;
      set_geo(s, &p);
    }
    return;
  }
  Capture& c = all_current ? g_sun_ccap[layer][f % 3] : g_ccap[f % 3];
  if (c.state == 1 && c.record_frame == f) return;   // one such pass per frame: never expected
  // (state 1 from an older frame: that recording never ended its pass; start over)
  const VkDeviceSize bytes = VkDeviceSize(cascade_mb()) << 20;
  if (c.xfb.b && c.xfb.size != bytes) {free_buf(c.xfb);c.c_legacy_transfer=false;} // deferred destruction
  if (!c.xfb.b) {
    static int s_logged_mb = 0;
    if (s_logged_mb != int(bytes >> 20))
      s_logged_mb = int(bytes >> 20), log("[cascade] capture buffer %d MB (rt_cascade_mb %d, %u-byte vertices)", s_logged_mb,
                                          cfg_int("rt_cascade_mb", 160), g_stride);
  }
  if (!c.xfb.b && !make_buf(c.xfb, bytes, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | rt_xfb_usage(), false)) {
    if (!g_cc_alloc_failed)
      log("[cascade] could not allocate %llu MB for the cascade capture", (unsigned long long)(bytes >> 20));
    g_cc_alloc_failed = true;
    return;
  }
  c.c_legacy_target=nullptr;c.c_legacy_copies.clear();c.c_legacy_bytes=0;c.c_legacy_sig=1469598103934665603ull;
  if(all_current && layer==want) {
    Capture& legacy=g_ccap[f%3];
    if(legacy.xfb.b && (legacy.xfb.size!=bytes || !legacy.c_legacy_transfer)) {free_buf(legacy.xfb);legacy.c_legacy_transfer=false;}
    if(!legacy.xfb.b) make_buf(legacy.xfb,bytes,VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | rt_xfb_usage() |
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,false);
    if(legacy.xfb.b) {
      legacy.c_legacy_transfer=true;
      c.c_legacy_target=&legacy;legacy.layer=layer;legacy.cmd=cmd;legacy.record_frame=f;
    }
  }
  c.state = 1;
  c.cascade = true;
  c.probe = false;
  c.layer = layer;
  c.cmd = cmd;
  c.record_frame = f;
  c.readback = c.rt = c.split = c.cc_keys = false;
  c.cursor = 0;
  c.xfb_active = false;
  c.active_slot = -1;
  c.rt_ubo = VK_NULL_HANDLE;
  c.rt_ubo_off = 0;
  c.c_seen = c.c_dup = c.c_alpha = c.c_drop = c.c_ubo = c.c_full = c.c_added = 0;
  c.c_sig = 1469598103934665603ull;
  c.c_tris = 0;
  c.c_native_epoch = {}; c.c_native_frame = VK_NULL_HANDLE; c.c_native_offset = 0;
  c.c_epoch_ok = false; c.c_material_unknown = 0; c.c_finite_sun_clip = true;c.c_native_raster_valid=true;c.c_have_native_viewport=false;c.c_fresh_depth=s->depth_cleared_empty && s->render_area.offset.x==0 && s->render_area.offset.y==0;
  c.c_depth_image=s->pass.depth_image; c.c_sun_draws.clear();c.c_sun_materials.clear();
  if(all_current) {
    std::shared_lock<std::shared_mutex> image_lock(tracker().mu);
    auto image=tracker().images.find(s->pass.depth_image);auto view=tracker().views.find(s->pass.depth_view);
    c.c_native_raster_valid=image!=tracker().images.end() && view!=tracker().views.end() &&
      image->second.ci.extent.width==s->pass.width && image->second.ci.extent.height==s->pass.height &&
      view->second.range.baseMipLevel==0 && view->second.range.baseArrayLayer==uint32_t(layer) &&
      view->second.range.layerCount==1 && s->render_area.offset.x==0 && s->render_area.offset.y==0;
  }
  c.rec = bvh_rec_register(cmd);
  if(layer==want) g_cc_meta[f % 3] = CcMeta{};
  if(all_current) g_sun_meta[layer][f % 3]=CcMeta{};
  // The build two frames ago (BLAS input) and this buffer's last conversion are done with
  // it before the XFB writes (both earlier in submission order).
  barrier(cmd,
          VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
              VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
          VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT);
  c.ts = -1;
  if (!g_cc_qp) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kCcTs * 2;
    if (g_dd->vk.CreateQueryPool(g_dd->device, &qi, nullptr, &g_cc_qp) != VK_SUCCESS) g_cc_qp = VK_NULL_HANDLE;
  }
  const uint32_t slot = uint32_t(f % kCcTs);
  if (g_cc_qp && !g_cc_ts_frame[slot]) {
    g_cc_ts_frame[slot] = f + 1;
    c.ts = int(slot);
    g_dd->vk.CmdResetQueryPool(cmd, g_cc_qp, slot * 2, 2);
    g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_cc_qp, slot * 2);
  }
  set_geo(s, &c);
}

void cascade_pre_draw(VkCommandBuffer cmd, CmdState* s, Capture* c, const DrawArgs& args, const PipeInfo& pi) {
  Bg3Binding b;
  const bool have = query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 2, 0, &b) && b.buffer &&
                    (b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
  if (c->probe) {   // only the matrix: the first draw's UBO, then no more hooks in this pass
    CcMat m;
    if (have && read_mapped(b.buffer, b.offset, m.v, sizeof m.v)) {
      m.frame = frame_index();
      std::lock_guard<std::mutex> lock(g_cc_mu);
      g_cc_mat[c->layer] = m;
    }
    s->geo = {};
    return;
  }
  ++c->c_seen;
  const bool current_sun = cfg_hot(CfgHot::rt_sun_cascade_current) != 0;
  if(current_sun && (!have || (b.range!=VK_WHOLE_SIZE && b.range<224) ||
      !geo_uniform_buffer_range(b.buffer,b.offset,224))) {++c->c_ubo;return;}
  if (current_sun) {
    sun_coverage::NativeEpoch epoch;
    VkBuffer frame_buffer = VK_NULL_HANDLE; VkDeviceSize frame_offset = 0;
    const bool ok = geo_native_frame_epoch(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,&epoch,&frame_buffer,&frame_offset);
    if (c->c_seen == 1) {
      c->c_native_epoch=epoch;c->c_native_frame=frame_buffer;c->c_native_offset=frame_offset;c->c_epoch_ok=ok;
    } else c->c_epoch_ok = c->c_epoch_ok && ok &&
      sun_coverage::same_epoch(c->c_native_epoch,epoch) && frame_buffer==c->c_native_frame && frame_offset==c->c_native_offset;
    // A native depth-write-disabled draw contributes no caster. Dynamic or
    // unknown state is NOT interpreted as that known empty case.
    if (pi.depth_write == 0 || pi.always_discard) return;
    c->c_finite_sun_clip = c->c_finite_sun_clip && pi.finite_sun_clip;
    const bool have_view=pi.native_view_known && (!pi.dynamic_viewport || s->has_viewport) &&
      (!pi.dynamic_scissor || s->has_scissor);
    const VkViewport view=pi.dynamic_viewport?s->viewport:pi.native_viewport;
    const VkRect2D scissor=pi.dynamic_scissor?s->scissor:pi.native_scissor;
    c->c_native_raster_valid=c->c_native_raster_valid && have_view &&
      sun_coverage::full_native_view(view,scissor,s->pass.width,s->pass.height);
    if(!c->c_have_native_viewport) {c->c_native_viewport=view;c->c_have_native_viewport=true;}
    else c->c_native_raster_valid=c->c_native_raster_valid && std::memcmp(&view,&c->c_native_viewport,sizeof view)==0;
    if (pi.fragment_depth || !pi.fragment_known)
      ++c->c_material_unknown;
  }
  // One matrix converts the whole soup: every draw must use the first one's UBO.
  if (!have || b.offset % g_ubo_align) {
    ++c->c_ubo;
    return;
  }
  if (!c->rt_ubo) {
    c->rt_ubo = b.buffer;
    c->rt_ubo_off = b.offset;
  } else if (b.buffer != c->rt_ubo || b.offset != c->rt_ubo_off) {
    ++c->c_ubo;
    return;
  }
  const uint32_t kind = args.kind, count = args.count;
  if (!pi.xfb || !pi.triangles || kind > 1 ||
      !(pi.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ||
        ((pi.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP || pi.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) &&
         (kind == 0 || !pi.restart)))) {
    ++c->c_drop;   // not sizeable exactly (indirect, adjacency, restart): as for the camera soup
    return;
  }
  // rt_cascade_alpha=1 (A/B): alpha-tested draws go in as opaque (cutouts solid).
  // rt_cascade_alpha=2: only draws whose FS is a texture cutout (in the alpha table) are
  // skipped; the other discarding FS test no texture alpha (LOD dither, camera fade: 856 of the
  // 1263 depth-only discarding FS in the dump) and their geometry goes in solid.
  const int cc_alpha=cfg_int("rt_cascade_alpha",0);
  AlphaFs alpha_fs{};
  const bool cutout=pi.discard && cc_alpha!=1 && alpha_lookup(s->fs,&alpha_fs);
  const cascade::DrawId id=cascade_draw_id(cmd,s,args,pi);
  bool mesh_duplicate=false,draw_duplicate=false;
  {
    std::lock_guard<std::mutex> lock(g_cc_mu);
    mesh_duplicate=g_cc_mesh_keys.contains(c->record_frame,cascade::mesh_key(id));
    draw_duplicate=id.place && g_cc_keys.contains(c->record_frame,cascade::draw_key(id));
  }
  const auto legacy=cascade::legacy_caster(pi.discard,cc_alpha,cutout,
    cfg_int("rt_cascade_alpha_dedupe",1)!=0,mesh_duplicate,draw_duplicate);
  const bool legacy_keep=legacy==cascade::LegacyCaster::keep;
  if(legacy==cascade::LegacyCaster::alpha_skip) {
    if(cutout) {++g_cc_alpha_known;if(alpha_fs.uv==pi.uv && pi.uv>=0) ++g_cc_alpha_uv;}
    if(!current_sun) {++c->c_alpha;return;}
  } else if(legacy==cascade::LegacyCaster::duplicate && !current_sun) {++c->c_dup;return;}
  uint64_t prims = pi.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ? count / 3 : count > 2 ? count - 2 : 0;
  prims *= args.instances;
  if (current_sun && prims > UINT64_MAX / (3ull * g_stride)) { ++c->c_full; return; }
  const uint64_t reserved = prims * 3ull * g_stride;
  if (!reserved) return;
  if (c->cursor + reserved > c->xfb.size) {
    ++c->c_full;
    return;
  }
  if(current_sun && legacy_keep && c->c_legacy_target) {
    c->c_legacy_copies.push_back({c->cursor,c->c_legacy_bytes,reserved});
    c->c_legacy_bytes+=reserved;
    for(uint64_t v:{uint64_t(s->pipe_id[0]),uint64_t(args.count),uint64_t(args.first),uint64_t(args.instances),
                   uint64_t(int64_t(args.vertex_offset)),uint64_t(s->vb[0].buffer),uint64_t(s->vb[0].offset),prims})
      c->c_legacy_sig=(c->c_legacy_sig^v)*1099511628211ull;
  }
  if(current_sun) {
    SunCascadeDraw draw;
    draw.first_triangle=uint32_t(c->cursor/(3ull*g_stride));draw.triangles=uint32_t(prims);draw.fragment_shader=s->fs;
    if(pi.discard || pi.fragment_depth || !pi.fragment_known || pi.depth_write<0 || pi.depth_test<0) {
      draw.material=UINT32_MAX;
      AlphaFs material{};AlphaSnapshot snapshot;
      if(!pi.fragment_depth && pi.fragment_known && pi.depth_write>=0 && pi.depth_test>=0 && (alpha_lookup(s->fs,&material) || alpha_lookup_native_tileset_shadow(s->fs,&material)) && material.uv==pi.uv && pi.uv>=0 &&
         (material.kind!=4 || (g_stride>=88 && (pi.hair_outputs&1u)!=0u)) &&
         alpha_capture_source(cmd,material,draw.first_triangle,draw.triangles,&snapshot)) {
        // Legacy alpha-table membership is NOT proof of the native predicate:
        // helper/fade/secondary factors omitted by those recipes stay UNKNOWN.
        bool native=material.kind==3 || material.kind==4 ||
          (material.kind==5 && native_tileset_coverage::lookup_shadow(s->fs) && native_tileset_coverage::matches(s->vs,s->fs));
        if(const auto* recipe=omm::native_plain_recipe(s->fs,s->vs))
          native=omm::native_plain_record_matches(*recipe,material.kind,uint32_t(material.uv),material.tex_set,
            material.tex_bind,snapshot.rec.channel,snapshot.rec.cut,snapshot.rec.cut_first,
            snapshot.rec.scale[0],snapshot.rec.scale[1]);
        if(native) {
          draw.material=uint32_t(c->c_sun_materials.size())+1;
          c->c_sun_materials.push_back(std::move(snapshot));
        }
      }
    }
    c->c_sun_draws.push_back(std::move(draw));
  }
  VkDeviceSize off = c->cursor, size = reserved;
  p_bind_xfb(cmd, 0, 1, &c->xfb.b, &off, &size);
  p_begin_xfb(cmd, 0, 0, nullptr, nullptr);
  c->xfb_active = true;
  c->cursor += reserved;
  ++c->c_added;
  c->c_tris += prims;
  // Same draws in the same order with the same triangle counts: the BLAS can be refitted.
  for (uint64_t v : {uint64_t(s->pipe_id[0]), uint64_t(args.count), uint64_t(args.first), uint64_t(args.instances),
                     uint64_t(int64_t(args.vertex_offset)), uint64_t(s->vb[0].buffer), uint64_t(s->vb[0].offset), prims})
    c->c_sig = (c->c_sig ^ v) * 1099511628211ull;
}

void cascade_pass_after(VkCommandBuffer cmd, Capture* c) {
  if (c->probe) return;
  const uint32_t tris = uint32_t(c->cursor / (3ull * g_stride));
  bool converted = false;
  if (tris && c->rt_ubo) {
    barrier(cmd, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT, VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    converted = rt_cascade_convert(cmd, c->xfb.b, tris, g_stride, c->rt_ubo, c->rt_ubo_off);
    // -> the next frame's BLAS build (which has its own barriers too).
    barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_MEMORY_READ_BIT);
  }
  // The raw current soup retains every capturable caster, including unknown
  // cutouts. Compact ONLY legacy-approved ranges for the old F-1 path, after
  // conversion, so no raw unknown card is accidentally made opaque there.
  bool legacy_copied=false;
  if(converted && c->c_legacy_target && !c->c_legacy_copies.empty()) {
    barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    g_dd->vk.CmdCopyBuffer(cmd,c->xfb.b,c->c_legacy_target->xfb.b,uint32_t(c->c_legacy_copies.size()),c->c_legacy_copies.data());
    barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_MEMORY_READ_BIT);
    legacy_copied=true;
  }
  if (c->ts >= 0 && g_cc_qp)
    g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_cc_qp, uint32_t(c->ts) * 2 + 1);
  CcMat m;
  const bool mat_ok = c->rt_ubo && read_mapped(c->rt_ubo, c->rt_ubo_off, m.v, sizeof m.v);
  std::lock_guard<std::mutex> lock(g_cc_mu);
  const uint64_t f = c->record_frame;
  if (mat_ok && c->layer >= 0 && c->layer < kCcLayers) {
    m.frame = f;
    g_cc_mat[c->layer] = m;
  }
  CcMeta meta{};
  if (converted) {
    meta.frame = f; meta.tris = tris; meta.rec = c->rec; meta.sig = c->c_sig;
    // The cascade VS's world position is relative to its own UBO's origin (byte 208),
    // which is not the camera's (92 m away in S7): the rebase starts from it.
    if (mat_ok) { meta.has_origin = true; for (int k = 0; k < 3; ++k) meta.origin[k] = m.v[52 + k]; }
    meta.sun.soup={c->xfb.b,tris,g_stride,meta.has_origin,{meta.origin[0],meta.origin[1],meta.origin[2]},meta.sig,c->rec,c->cmd};
    meta.sun.draws=c->c_sun_draws;
    meta.sun.materials=c->c_sun_materials;
    meta.sun.depth_image=c->c_depth_image;
    meta.sun.native_epoch=c->c_native_epoch;
    meta.sun.native_frame_buffer=c->c_native_frame;meta.sun.native_frame_offset=c->c_native_offset;
    meta.sun.camera_buffer=c->rt_ubo;meta.sun.camera_offset=c->rt_ubo_off;
    meta.sun.camera_mapped=mat_ok;
    if(mat_ok) std::memcpy(meta.sun.camera,m.v,sizeof m.v);
    meta.sun.layer=uint32_t(c->layer);
    if (mat_ok) for(unsigned r=0;r<4;++r) for(unsigned k=0;k<4;++k) meta.sun.matrix[r][k]=m.v[(8+r)*4+k];
    meta.sun.geometry_complete=c->rt_ubo && c->c_epoch_ok && c->c_seen && !c->c_drop && !c->c_ubo && !c->c_full && !c->c_dup && !c->c_alpha;
    meta.sun.materials_complete=c->c_material_unknown==0;
    meta.sun.finite_native_clip=c->c_finite_sun_clip;
    meta.sun.fresh_depth=c->c_fresh_depth;meta.sun.native_raster_valid=c->c_native_raster_valid;meta.sun.native_viewport=c->c_native_viewport;
  }
  if(c==&g_ccap[f % 3]) g_cc_meta[f % 3] = meta;
  if(c->c_legacy_target) {
    CcMeta legacy=meta;
    legacy.tris=legacy_copied ? uint32_t(c->c_legacy_bytes/(3ull*g_stride)) : 0;
    legacy.sig=c->c_legacy_sig;legacy.sun={};
    g_cc_meta[f%3]=std::move(legacy);
  }
  if(cfg_hot(CfgHot::rt_sun_cascade_current) && c->layer>=0 && c->layer<4)
    g_sun_meta[c->layer][f % 3]=meta;
  g_cc_cv.notify_all();
  CcStats& st = g_cc_st;
  ++st.frames;
  st.seen += c->c_seen, st.dup += c->c_dup, st.alpha += c->c_alpha, st.drop += c->c_drop, st.ubo += c->c_ubo;
  st.full += c->c_full, st.added += c->c_added, st.tris += tris;
  st.tris_max = (std::max)(st.tris_max, uint64_t(tris));
  st.bytes += c->cursor;
  st.bytes_max = (std::max)(st.bytes_max, uint64_t(c->cursor));
  st.keys += g_cc_keys.size(f);
  st.unconverted += tris && !converted;
  c->state = 0;
}

void cc_matrix(const CcMat& m, float m4[4][4]) {
  for (int r = 0; r < 4; ++r)
    for (int k = 0; k < 4; ++k) m4[r][k] = m.v[(8 + r) * 4 + k];
}

// The [cascade] lines, on the present thread.
void cascade_on_present() {
  if (g_cc_qp) {
    std::lock_guard<std::mutex> lock(g_cc_mu);
    const uint64_t now = frame_index();
    for (uint32_t s = 0; s < kCcTs; ++s) {
      if (!g_cc_ts_frame[s] || g_cc_ts_frame[s] + 6 > now) continue;
      uint64_t r[4] = {};
      VkResult res = g_dd->vk.GetQueryPoolResults(g_dd->device, g_cc_qp, s * 2, 2, sizeof r, r, 16,
                                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
      const bool done = res == VK_SUCCESS && r[1] && r[3];
      if (!done && g_cc_ts_frame[s] + 40 > now) continue;
      g_cc_ts_frame[s] = 0;
      if (done && r[2] >= r[0]) g_cc_ms.push_back(float(double(r[2] - r[0]) * g_ts_period * 1e-6));
    }
  }
  CcStats st;
  CcMat mats[kCcLayers];
  float ms = -1;
  int layer = -1;
  {
    std::lock_guard<std::mutex> lock(g_cc_mu);
    const int every = (std::max)(cfg_int("rt_timing_frames", 300), 30);
    if (g_cc_st.frames < uint64_t(every)) return;
    st = g_cc_st;
    g_cc_st = CcStats{};
    for (int i = 0; i < kCcLayers; ++i) mats[i] = g_cc_mat[i];
    if (!g_cc_ms.empty()) {
      std::sort(g_cc_ms.begin(), g_cc_ms.end());
      ms = g_cc_ms[g_cc_ms.size() / 2];
      g_cc_ms.clear();
    }
    const uint64_t f = frame_index();
    layer = g_ccap[(f - 1) % 3].layer >= 0 ? g_ccap[(f - 1) % 3].layer : g_ccap[f % 3].layer;
  }
  const double n = double(st.frames), mb = 1.0 / (1 << 20);
  uint64_t builds = 0;
  for (uint64_t b : st.builds) builds += b;
  log("[cascade] alpha-tested cascade draws skipped %.1f per frame: FS in the alpha table %.1f, with the captured UV %.1f",
      st.alpha / double(st.frames ? st.frames : 1), g_cc_alpha_known.exchange(0) / double(st.frames ? st.frames : 1),
      g_cc_alpha_uv.exchange(0) / double(st.frames ? st.frames : 1));
  log("[cascade] casters: cascade %d, %llu frames; per frame: draws seen %.1f, skipped as duplicates %.1f, "
      "alpha-tested skipped %.1f, not sizeable %.1f, other matrix %.1f, buffer full %.1f, added %.1f; triangles %.0f "
      "(max %llu), %.2f MB (max %.2f of %d MB); camera pass keys %.0f; captures not converted %llu; next-frame builds "
      "%llu: with the instance %llu (%.0f triangles), no soup %llu, unsubmitted %llu, no camera %llu, legacy %llu, "
      "BLAS failed %llu; captured pass GPU median %.3f ms (its draws + XFB + conversion)",
      layer, (unsigned long long)st.frames, st.seen / n, st.dup / n, st.alpha / n, st.drop / n, st.ubo / n, st.full / n,
      st.added / n, st.tris / n, (unsigned long long)st.tris_max, st.bytes / n * mb, st.bytes_max * mb,
      cascade_mb(), st.keys / n, (unsigned long long)st.unconverted, (unsigned long long)builds,
      (unsigned long long)st.builds[0], st.builds[0] ? double(st.build_tris) / double(st.builds[0]) : 0.0,
      (unsigned long long)st.builds[1], (unsigned long long)st.builds[2], (unsigned long long)st.builds[3],
      (unsigned long long)st.builds[4], (unsigned long long)st.builds[5], ms);
  // The inversion's premises, from the matrix the conversion used: orthographic (w column
  // (0, 0, 0, 1)), invertible, and the UBO's origin (byte 208) = the camera's of that frame.
  std::string ext;
  for (int i = 0; i < kCcLayers; ++i) {
    if (!mats[i].frame) continue;
    float m4[4][4];
    cc_matrix(mats[i], m4);
    const cascade::Ortho o = cascade::ortho_check(m4);
    char t[96];
    snprintf(t, sizeof t, " c%d %.1f x %.1f x %.1f%s;", i, o.extent[0], o.extent[1], o.extent[2],
             o.ok ? "" : " (NOT orthographic)");
    ext += t;
  }
  if (layer >= 0 && layer < kCcLayers && mats[layer].frame) {
    const CcMat& m = mats[layer];
    float m4[4][4];
    cc_matrix(m, m4);
    const cascade::Ortho o = cascade::ortho_check(m4);
    float cam[3];
    const bool have_cam = rt_build_camera(m.frame, cam);
    double d = -1.0;
    if (have_cam) {
      const double dx = m.v[52] - cam[0], dy = m.v[53] - cam[1], dz = m.v[54] - cam[2];
      d = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    log("[cascade] matrix of cascade %d (its set 0 binding 2, _m2 at +128): w column error %.2g -> %s, det %.3g, "
        "extent %.1f x %.1f m, depth %.1f m; its UBO origin (+208) is %.3f m from the camera's of that frame%s; all "
        "cascades (m):%s",
        layer, o.w_err, o.ok ? "orthographic, inverted from its x, y, z rows" : "NOT orthographic: soup invalid",
        o.det, o.extent[0], o.extent[1], o.extent[2], d, have_cam ? "" : " (that frame's camera unknown)", ext.c_str());
  }
}

}  // namespace

bool geo_copy_camera(VkCommandBuffer cmd, VkBuffer source, VkDeviceSize offset, VkBuffer destination,
                     VkDeviceSize destination_offset) {
  if (!source || !destination || !ubo_pipeline()) return false;
  VkDescriptorBufferInfo in{source, offset, 224}, out{destination, destination_offset, 256};
  VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
  w[0].dstBinding = 0; w[0].descriptorCount = 1;
  w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w[0].pBufferInfo = &in;
  w[1].dstBinding = 1; w[1].descriptorCount = 1;
  w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[1].pBufferInfo = &out;
  g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_ubo_pipe);
  g_dd->vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_ubo_pl, 0, 2, w);
  p_dispatch(cmd, 1, 1, 1);
  barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  return true;
}

// ================= host-facing entry points =================

const VkDeviceCreateInfo* geo_device_create_info(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                 VkDeviceCreateInfo* scratch) {
  g_timing = cfg_int("geo_timing", 0) != 0;
  if ((!cfg_hot(CfgHot::geo_capture) && !cfg_int("rt", 1)) || !id) return ci;
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
  p_set_scissor=reinterpret_cast<PFN_vkCmdSetScissor>(dd->gdpa(dd->device,"vkCmdSetScissor"));
  p_copy_buffer = reinterpret_cast<PFN_vkCmdCopyBuffer>(dd->gdpa(dd->device, "vkCmdCopyBuffer"));
  p_begin_query = reinterpret_cast<PFN_vkCmdBeginQuery>(dd->gdpa(dd->device, "vkCmdBeginQuery"));
  p_end_query = reinterpret_cast<PFN_vkCmdEndQuery>(dd->gdpa(dd->device, "vkCmdEndQuery"));
  p_dispatch = dd->vk.CmdDispatch;
  if (dd->inst && dd->inst->GetPhysicalDeviceProperties) {
    VkPhysicalDeviceProperties p;
    dd->inst->GetPhysicalDeviceProperties(dd->physical, &p);
    g_ts_period = p.limits.timestampPeriod;
    g_ubo_align = p.limits.minUniformBufferOffsetAlignment;
    g_storage_range=p.limits.maxStorageBufferRange;
  }
  if (xfb_enabled) {
    p_bind_xfb = reinterpret_cast<PFN_vkCmdBindTransformFeedbackBuffersEXT>(
        dd->gdpa(dd->device, "vkCmdBindTransformFeedbackBuffersEXT"));
    p_begin_xfb = reinterpret_cast<PFN_vkCmdBeginTransformFeedbackEXT>(dd->gdpa(dd->device, "vkCmdBeginTransformFeedbackEXT"));
    p_end_xfb = reinterpret_cast<PFN_vkCmdEndTransformFeedbackEXT>(dd->gdpa(dd->device, "vkCmdEndTransformFeedbackEXT"));
    g_on = p_bind_xfb && p_begin_xfb && p_end_xfb && p_begin_query && p_end_query;
    // Track D phase 3: the UV goes along with the position, and the scene
    // comes from the depth prepass (rt_alpha needs rt=1; both are read here, once).
    g_alpha_mode = cfg_int("rt", 1) && cfg_int("rt_alpha", 1);
    const bool hair_requested=cfg_int("rt_native_hair",0)!=0;
    const bool hair_split=cfg_int("rt_bvh_split",1)!=0;
    if(hair_requested && !hair_split)
      log("[geo] rt_native_hair rejected: requires rt_bvh_split=1 at startup; legacy procedural range not implemented");
    g_stride = g_alpha_mode ? (hair_requested && hair_split ? 88 : cfg_int("rt_hit_material",0) && hair_split ? 64 : 24) : 16;
    g_stride_without_tileset=g_stride;
    g_tileset_capture=g_alpha_mode && hair_split && cfg_hot(CfgHot::rt_native_tileset)!=0;
    if(cfg_hot(CfgHot::rt_native_tileset) && !g_tileset_capture)log("[geo] rt_native_tileset rejected: restart with rt_alpha=1 rt_bvh_split=1");
    if(g_tileset_capture)g_stride=(std::max)(g_stride,cfg_int("rt_hit_material",0)?80u:40u);
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

bool geo_native_tileset_enabled() { return g_tileset_capture; }
void geo_native_tileset_disable(){g_tileset_capture=false;g_stride=g_stride_without_tileset;}

bool geo_native_hair_enabled() { return g_stride >= 88u && g_alpha_mode; }
VkDeviceSize geo_ubo_align() { return g_ubo_align ? g_ubo_align : 256; }

bool geo_patch_shader(const VkShaderModuleCreateInfo* ci, std::vector<uint32_t>& out) {
  t_last_uv = -1;
  t_last_hair_outputs = 0;
  t_last_vt_outputs = 0;
  t_last_tileset_outputs = 0;
  t_last_no_writes = false;
  t_last_inst = VsInstancing{};
  t_last_hash = 0;
  t_last_discard = false;
  t_last_always_discard = false;
  t_last_fragment_depth = false;
  t_last_io.clear();
  t_last_model = -1;
  if (!g_on) return false;
  t_last_discard = cascade::spirv_fragment_discards(ci->pCode, ci->codeSize / 4);
  t_last_always_discard = spirv_fragment_always_discards(ci->pCode, ci->codeSize / 4);
  t_last_fragment_depth = sun_coverage::fragment_depth_replacing(ci->pCode, ci->codeSize / 4);
  // Every module's interface (vertex and fragment): the UV-slot check at pipeline creation.
  t_last_model = stage_io(ci->pCode, ci->codeSize / 4, t_last_io);
  if(native_hit::enabled())t_last_no_writes=native_hit::no_graphics_storage_writes(ci->pCode,ci->codeSize/4);
  t_last_hash = hash64(ci->pCode, ci->codeSize);
  // Smooth N/B/T at locations 1/2/3 (offsets 24/36/48): the audited per-draw VS and the STI pool
  // VS 2d2ac3c3 (same output interface, native_hit_pool.h; its UV/basis capture of instance 0).
  // Class A VS 54df894a (ENV_DetailMap_VT pool pair): UV at 3, N/B/T at 4/5/6, same offsets.
  // Class A VS c32ce535 (ENV_DetailMap_Iridescent pool pair): 2d2ac3c3's interface plus W at 4.
  const bool detail_vs=t_last_hash==0x54df894ab5230cbbull;
  // rt_hit_material_instanced at startup: the instanced wind / 2S G-buffer VSs 50fbfceb and 1e9145e4
  // (2d2ac3c3's output interface; native_hit_instanced.h).
  const bool vt_frame=native_hit::enabled() && (t_last_hash==0x3812bc980438747full || t_last_hash==0x2d2ac3c38992b7f0ull || detail_vs ||
                                                t_last_hash==0xc32ce535d664fbbcull || native_hit::instanced_patch_vs(t_last_hash));
  bool ok = patch_vs(ci->pCode, ci->codeSize / 4, out, g_stride, &t_last_uv, &t_last_hair_outputs,vt_frame,&t_last_vt_outputs,g_tileset_capture && native_tileset_coverage::vertex(t_last_hash),&t_last_tileset_outputs,
                     g_tileset_capture && native_hit::enabled() && t_last_hash==native_hit::tileset_deferred_vs,detail_vs?4u:1u);
  if (!ok) return false;
  // The instancing analysis runs on the game's original SPIR-V.
  if (rt_enabled()) {
    t_last_inst = analyze_vs_instancing(ci->pCode, ci->codeSize / 4, t_last_uv);
    t_last_hash = hash64(ci->pCode, ci->codeSize);
    if (t_last_inst.ok) ++g_inst_modules;
  }
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
  if (!g_on) return;
  if (t_last_discard && !patched) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_discard_mods.insert(m);
  }
  std::lock_guard<std::mutex> lock(g_mu);
  if (t_last_always_discard && !patched) g_always_discard_mods.insert(m);
  if (t_last_fragment_depth && !patched) g_fragment_depth_mods.insert(m);
  if (t_last_model == 0 || t_last_model == 4) g_mod_io[m] = ModIo{t_last_model, std::move(t_last_io), t_last_hash,t_last_no_writes};
  if (!patched) return;
  g_patched[m] = ModInfo{t_last_uv, t_last_inst, t_last_hash, t_last_hair_outputs,t_last_vt_outputs,t_last_tileset_outputs};
}

void geo_forget_module(VkShaderModule m) {
  if (!g_on) return;
  std::lock_guard<std::mutex> lock(g_mu);
  g_patched.erase(m);
  g_discard_mods.erase(m);
  g_always_discard_mods.erase(m);
  g_fragment_depth_mods.erase(m);
  g_mod_io.erase(m);
}

// The instancing layout of a class-A pipeline (Track D BVH split): its three
// matrix rows must be R32G32B32A32_SFLOAT attributes of instance-rate bindings
// (divisor 1) and its position an attribute of a per-vertex binding.
static InstPipe make_inst_pipe(const VkGraphicsPipelineCreateInfo& ci, const ModInfo& mod) {
  InstPipe ip;
  const VsInstancing& vi = mod.inst;
  const VkPipelineVertexInputStateCreateInfo* vis = ci.pVertexInputState;
  if (!vi.ok || !vis) return ip;
  bool dyn_stride = false;
  if (ci.pDynamicState)
    for (uint32_t d = 0; d < ci.pDynamicState->dynamicStateCount; ++d) {
      VkDynamicState s = ci.pDynamicState->pDynamicStates[d];
      if (s == VK_DYNAMIC_STATE_VERTEX_INPUT_EXT) return ip;
      if (s == VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE) dyn_stride = true;
    }
  for (auto p = static_cast<const VkBaseInStructure*>(vis->pNext); p; p = p->pNext)
    if (p->sType == VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_KHR) return ip;
  auto binding = [&](uint32_t b) -> const VkVertexInputBindingDescription* {
    for (uint32_t i = 0; i < vis->vertexBindingDescriptionCount; ++i)
      if (vis->pVertexBindingDescriptions[i].binding == b) return &vis->pVertexBindingDescriptions[i];
    return nullptr;
  };
  auto attr = [&](int loc) -> const VkVertexInputAttributeDescription* {
    for (uint32_t i = 0; i < vis->vertexAttributeDescriptionCount; ++i)
      if (int(vis->pVertexAttributeDescriptions[i].location) == loc) return &vis->pVertexAttributeDescriptions[i];
    return nullptr;
  };
  for (int k = 0; k < 3; ++k) {
    const auto* a = attr(vi.row_location[k]);
    const auto* b = a ? binding(a->binding) : nullptr;
    if (!a || !b || a->format != VK_FORMAT_R32G32B32A32_SFLOAT || b->inputRate != VK_VERTEX_INPUT_RATE_INSTANCE ||
        a->binding >= CmdState::kMaxVertexBindings)
      return ip;
    ip.row_binding[k] = uint8_t(a->binding);
    ip.row_offset[k] = a->offset;
    ip.row_stride[k] = dyn_stride ? 0 : b->stride;
  }
  const auto* pa = attr(vi.pos_location);
  const auto* pb = pa ? binding(pa->binding) : nullptr;
  if (!pa || !pb || pb->inputRate != VK_VERTEX_INPUT_RATE_VERTEX) return ip;
  ip.pos_binding = pa->binding < CmdState::kMaxVertexBindings ? uint8_t(pa->binding) : uint8_t(0xff);
  ip.pos_offset = pa->offset;
  // The mesh is what the per-vertex inputs hold: the VS and their layout are part of its key.
  uint64_t h = mod.hash ^ 0x9e3779b97f4a7c15ull;
  auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
  for (uint32_t i = 0; i < vis->vertexAttributeDescriptionCount; ++i) {
    const auto& a = vis->pVertexAttributeDescriptions[i];
    const auto* b = binding(a.binding);
    if (!b || b->inputRate != VK_VERTEX_INPUT_RATE_VERTEX || a.binding >= CmdState::kMaxVertexBindings) continue;
    mix(a.location);
    mix(a.binding);
    mix(a.format);
    mix(a.offset);
    mix(dyn_stride ? 0 : b->stride);
    ip.vtx_bindings |= 1u << a.binding;
  }
  ip.vtx_sig = h;
  const auto* ua = vi.uv_input_location >= 0 ? attr(vi.uv_input_location) : nullptr;
  const auto* ub = ua ? binding(ua->binding) : nullptr;
  ip.uv_ok = ub && ub->inputRate == VK_VERTEX_INPUT_RATE_VERTEX;
  ip.ok = true;
  return ip;
}

void geo_uv_summary() {
  if (!g_on || g_stride < 24) return;
  size_t pairs;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    pairs = g_uv_bad_pairs.size();
  }
  log("[geo] UV slots of the patched pipelines so far: %u match the FS, %u the FS's UV is a wider VS output, %u the FS "
      "reads its UV elsewhere, %u FS without a float2 input, %u unknown (libraries); %zu distinct mismatching VS/FS pairs",
      g_uv_count[UV_OK].load(), g_uv_count[UV_VS_WIDER].load(), g_uv_count[UV_FS_ELSEWHERE].load(),
      g_uv_count[UV_FS_NONE].load(), g_uv_count[UV_UNKNOWN].load(), pairs);
}

void geo_note_pipelines(uint32_t n, const VkGraphicsPipelineCreateInfo* cis, const VkPipeline* out) {
  if (!g_on) return;
  std::lock_guard<std::mutex> lock(g_mu);
  for (uint32_t i = 0; i < n; ++i) {
    if (!out[i]) continue;
    PipeInfo pi{false, false, VK_PRIMITIVE_TOPOLOGY_MAX_ENUM};
    bool vs_patched = false, other = false;
    bool all_no_writes=cis[i].stageCount!=0;
    const ModIo *vio = nullptr, *fio = nullptr;
    for (uint32_t s = 0; s < cis[i].stageCount; ++s) {
      const auto& st = cis[i].pStages[s];
      auto io = st.module ? g_mod_io.find(st.module) : g_mod_io.end();
      all_no_writes&=io!=g_mod_io.end() && io->second.no_writes;
      if (io != g_mod_io.end()) (st.stage == VK_SHADER_STAGE_VERTEX_BIT ? vio : fio) = &io->second;
      if (st.stage == VK_SHADER_STAGE_VERTEX_BIT) {
        auto it = st.module ? g_patched.find(st.module) : g_patched.end();
        vs_patched = it != g_patched.end();
        if (vs_patched) {
          pi.uv = it->second.uv;
          pi.hair_outputs = it->second.hair_outputs;
          pi.vt_outputs = it->second.vt_outputs; pi.tileset_outputs=it->second.tileset_outputs;
          pi.inst = make_inst_pipe(cis[i], it->second);
          if (pi.inst.ok) ++g_inst_pipes;
        }
      } else if (st.stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
        pi.discard = st.module && g_discard_mods.count(st.module) != 0;
        pi.always_discard = st.module && g_always_discard_mods.count(st.module) != 0;
        pi.fragment_depth = st.module && g_fragment_depth_mods.count(st.module) != 0;
        pi.fragment_known = st.module && g_mod_io.count(st.module) != 0;
      } else {
        other = true;
      }
    }
    pi.xfb = vs_patched && !other;
    pi.no_storage_writes=all_no_writes;
    if(native_hit::enabled())pi.material_inputs=native_hit::static_inputs(cis[i]);
    if(native_hit::enabled() && g_tileset_capture && vio && fio){
      pi.tileset_family=native_hit::tileset_depth(vio->hash,fio->hash)?1u:native_hit::tileset_deferred(vio->hash,fio->hash)?2u:0u;
      if(pi.tileset_family)pi.tileset_material_inputs=native_hit::tileset_inputs(cis[i]);
    }
    pi.material_reasons=native_hit::material_state_reasons(cis[i]);
    pi.material_state=pi.material_reasons==0;
    auto next_type=[](const void* chain){return chain?uint32_t(static_cast<const VkBaseInStructure*>(chain)->sType):0u;};
    pi.raster_next=cis[i].pRasterizationState?next_type(cis[i].pRasterizationState->pNext):0u;
    pi.multisample_next=cis[i].pMultisampleState?next_type(cis[i].pMultisampleState->pNext):0u;
    pi.blend_next=cis[i].pColorBlendState?next_type(cis[i].pColorBlendState->pNext):0u;
    pi.finite_sun_clip = sun_coverage::finite_native_clip(cis[i]);
    if(cis[i].pViewportState) {
      const auto& v=*cis[i].pViewportState;
      pi.native_view_known=v.viewportCount==1 && v.scissorCount==1;
      if(cis[i].pDynamicState) for(uint32_t k=0;k<cis[i].pDynamicState->dynamicStateCount;++k) {
        const auto state=cis[i].pDynamicState->pDynamicStates[k];
        if(state==VK_DYNAMIC_STATE_VIEWPORT) pi.dynamic_viewport=true;
        if(state==VK_DYNAMIC_STATE_SCISSOR) pi.dynamic_scissor=true;
        if(state==VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT || state==VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT) pi.native_view_known=false;
      }
      if(!pi.dynamic_viewport) {if(v.pViewports) pi.native_viewport=v.pViewports[0];else pi.native_view_known=false;}
      if(!pi.dynamic_scissor) {if(v.pScissors) pi.native_scissor=v.pScissors[0];else pi.native_view_known=false;}
    }
    if (cis[i].pDepthStencilState) {
      pi.depth_write = cis[i].pDepthStencilState->depthWriteEnable != 0;
      pi.depth_test = cis[i].pDepthStencilState->depthTestEnable != 0;
    }
    if (cis[i].pColorBlendState)
      for (uint32_t k = 0; k < cis[i].pColorBlendState->attachmentCount; ++k) {
        const auto& b = cis[i].pColorBlendState->pAttachments[k];
        pi.color_mask |= uint32_t(b.colorWriteMask) << (4 * (std::min)(k, 7u));
        if (b.blendEnable) pi.blend_mask |= 1u << (std::min)(k, 31u);
      }
    if (cis[i].pDynamicState)
      for (uint32_t k = 0; k < cis[i].pDynamicState->dynamicStateCount; ++k) {
        const auto state = cis[i].pDynamicState->pDynamicStates[k];
        if (state == VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE) pi.depth_write = -1;
        if (state == VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE) pi.depth_test = -1;
        if(state==VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT || state==VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT ||
           state==VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT || state==VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE ||
           state==VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT || state==VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT){pi.material_state=false;pi.material_reasons|=native_hit::MaterialDynamicState;}
      }
    const uint32_t overlap_family = (uint32_t(cfg_int("rt_em_dbg_fam_hi", 0) & 0xffff) << 16) |
                                     uint32_t(cfg_int("rt_em_dbg_fam_lo", 0) & 0xffff);
    if (cfg_hot(CfgHot::rt_em_overlap_check) && fio && uint32_t(fio->hash >> 32) == overlap_family &&
        cis[i].pColorBlendState && cis[i].pColorBlendState->attachmentCount &&
        cis[i].pRasterizationState && cis[i].pDepthStencilState) {
      const auto& b = cis[i].pColorBlendState->pAttachments[0];
      const auto& r = *cis[i].pRasterizationState;
      const auto& d = *cis[i].pDepthStencilState;
      pi.em_state = true;
      const uint32_t values[9] = {b.blendEnable, uint32_t(b.srcColorBlendFactor), uint32_t(b.dstColorBlendFactor), uint32_t(b.colorBlendOp),
                                  b.colorWriteMask, r.cullMode, d.depthTestEnable, d.depthWriteEnable, uint32_t(d.depthCompareOp)};
      std::copy(std::begin(values), std::end(values), pi.em_raster);
      if (cis[i].pDynamicState)
        for (uint32_t k = 0; k < cis[i].pDynamicState->dynamicStateCount; ++k) {
          const auto state = cis[i].pDynamicState->pDynamicStates[k];
          pi.em_dynamic |= state == VK_DYNAMIC_STATE_CULL_MODE || state == VK_DYNAMIC_STATE_FRONT_FACE ||
              state == VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE || state == VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE ||
              state == VK_DYNAMIC_STATE_DEPTH_COMPARE_OP || state == VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT ||
              state == VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT || state == VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT;
        }
    }
    // The UV-slot check: does the fragment shader read its texture coordinate where the
    // captured UV came from (TRACK_D 13.19)? Every pipeline, for every user of captured UVs.
    if (pi.xfb && g_stride >= 24 && vio && fio) {
      int fs_uv = -1;
      uint32_t comps = 0;
      const UvMatch m = uv_match(pi.uv, vio->io, fio->io, &fs_uv, &comps);
      pi.uv_match = m;
      pi.uv_fs = int8_t(fs_uv);
      pi.uv_vs_comps = uint8_t(comps);
      ++g_uv_count[m];
      if (m == UV_VS_WIDER || m == UV_FS_ELSEWHERE) {
        const uint64_t key = vio->hash * 1099511628211ull ^ fio->hash;
        if (g_uv_bad_pairs[key]++ == 0 && g_uv_bad_pairs.size() <= 64)
          log("[geo] UV slot mismatch: VS %016llx captures location %d as UV, FS %016llx reads its float2 at location "
              "%d (the VS writes %u components there)",
              (unsigned long long)vio->hash, pi.uv, (unsigned long long)fio->hash, fs_uv, comps);
      }
    } else if (pi.xfb && g_stride >= 24) {
      ++g_uv_count[UV_UNKNOWN];
    }
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
  s->geo = {};
  s->geo_material_active = false;
  s->geo_ts = -1;
  if (!g_dd) return;
  // rt_cascade_casters: a sun shadow cascade pass (a depth-only D16 layer of the cascade array).
  if (g_on && s->pass.color_count == 0 && s->pass.depth_format == VK_FORMAT_D16_UNORM && rt_enabled() &&
      cfg_hot(CfgHot::rt_cascade_casters)) {
    const int layer = cascade_layer(s->pass);
    if (layer >= 0) {
      cascade_pass_before(cmd, s, layer);
      return;
    }
  }
  // The main G-buffer is the largest one seen recently: a smaller pass with its formats
  // (UI portraits) must not move the render resolution or the prepass depth image.
  static std::atomic<uint64_t> main_seen{0};
  bool gbuf = is_gbuffer(s->pass);
  if (gbuf && g_gbuf_w && uint64_t(s->pass.width) * s->pass.height < uint64_t(g_gbuf_w) * g_gbuf_h &&
      frame_index() <= main_seen.load() + 4)
    gbuf = false;
  if (gbuf) main_seen = frame_index();
  // D-EM: an effects pass (the game's emissive draws, emissive.cpp). The forward pass has the
  // same signature: M5.1 water (water.cpp) captures its water draws through the same hook.
  const bool em_fx = g_on && !gbuf && em_is_fx_pass(s->pass, g_gbuf_w, g_gbuf_h);
  const bool water_fx = g_on && !gbuf && water_is_pass(s->pass, g_gbuf_w, g_gbuf_h);
  if (em_fx || water_fx) {
    auto* e = new EmPass;
    if (em_fx) {
      e->capture = em_pass_begin(cmd, s, &e->ts);
      e->em = e->capture || e->ts >= 0;
    }
    e->water = water_fx && water_pass_begin(cmd);
    if (e->em || e->water) {
      set_geo(s, e);
    } else {
      delete e;
    }
    return;
  }
  if (gbuf) g_gbuf_w = s->pass.width, g_gbuf_h = s->pass.height, g_gbuf_depth = uint64_t(s->pass.depth_image);
  // The ray tracing scene's source pass: with rt_alpha the depth prepass
  // (depth-only D24S8 at render resolution), otherwise the G-buffer.
  // BG3 draws a few small depth-only passes before the big one.
  // The scene's prepass is the first non-minor depth-only pass of the frame, wherever
  // it sits. (A learned ordinal was fragile: the small passes before the big one come and
  // go with what is on screen, and a shifted ordinal froze the RT scene for 8 frames.)
  // Minor passes are still captured and dropped at their end.
  const bool prepass = is_prepass(s->pass) && g_scene_frame1.load() != frame_index() + 1;
  const bool rt_src = g_alpha_mode ? prepass : gbuf;
  if(g_on && gbuf && !rt_src && native_hit::enabled() && !trace_recording()) {
    s->geo={GeoKind::Material,nullptr};s->geo_material_no_writes=true;return;
  }
  if (!gbuf && !rt_src) return;
  bool xfb = false;
  if (g_on) {
    std::lock_guard<std::mutex> lock(g_cap_mu);
    if (gbuf && trace_recording() && s->tracing && g_cap.state == 0 && frame_index() >= trace_start_frame() + 1 &&
        cfg_hot(CfgHot::geo_capture)) {
      if (start_capture(g_cap, true, s->pass.width, s->pass.height)) {
        g_cap.cmd = cmd;
        g_cap.cc_keys = g_cap.cascade = false;
        g_cap.viewport = {};
        set_geo(s, &g_cap);
        g_submit_wanted = true;
        g_dd->vk.CmdResetQueryPool(cmd, g_cap.qp, 0, g_cap.max_slots);
      }
    } else if (g_scratch.state != 1 && rt_src && rt_capture_wanted()) {
      if (start_capture(g_scratch, false, s->pass.width, s->pass.height)) {
        g_scratch.cmd = cmd;
        g_scratch.rt = rt_src && rt_capture_wanted();
        g_scratch.split = g_scratch.rt && bvh_split_wanted();
        g_scratch.cascade = false;
        g_scratch.cc_keys = g_scratch.rt && cfg_hot(CfgHot::rt_cascade_casters) != 0;
        if(g_scratch.rt)native_hit::prepass_begin(cmd);
        set_geo(s, &g_scratch);
        // The previous frame's AS build read this buffer.
        if (g_scratch.rt) rt_before_capture(cmd);
        if (g_scratch.split) bvh_begin_capture(cmd);
      }
    }
    xfb = bool(s->geo);
  }
  // Timed: the ray tracing source pass (the G-buffer without rt_alpha).
  // (the small passes before the scene's are captured too: time the usual scene ordinal only)
  if (g_timing && g_qp && rt_src && (!g_alpha_mode || g_scene_ordinal < 0 || int(s->pass.ordinal) == g_scene_ordinal)) {
    std::lock_guard<std::mutex> lock(g_ts_mu);
    uint32_t slot = uint32_t(frame_index() % kTsSlots);
    if (!g_ts_frame[slot]) {
      g_ts_frame[slot] = frame_index() + 1;
      g_ts_xfb[slot] = xfb;
      g_ts_moving[slot] = false;
      g_ts_split[slot] = false;
      s->geo_ts = int(slot);
      g_dd->vk.CmdResetQueryPool(cmd, g_qp, slot * 2, 2);
      g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_qp, slot * 2);
    }
  }
}

// Right after the game's vkCmdBeginRenderPass (inside the pass).
void geo_pass_begun(VkCommandBuffer cmd, CmdState* s) {
  auto* c = geo_capture(s);
  if (!c) return;
  // Buffers are bound per draw (geo_pre_draw).
  (void)cmd;
}

// Right before the game's vkCmdEndRenderPass / vkCmdEndRendering (inside the pass).
void geo_pass_ending(VkCommandBuffer cmd, CmdState* s) {
  if(geo_is(s,GeoKind::Material)){if(s->geo_material_active)p_end_xfb(cmd,0,0,nullptr,nullptr);s->geo_material_active=false;return;}
  if (geo_is(s, GeoKind::Em)) {
    auto* e = geo_empass(s);
    if (e && e->xfb_active) p_end_xfb(cmd, 0, 0, nullptr, nullptr), e->xfb_active = false;
    if (e && e->water_swap) water_unswap(cmd, s), e->water_swap = false;
    return;
  }
  auto* c = geo_capture(s);
  if (!c || !c->xfb_active) return;
  p_end_xfb(cmd, 0, 0, nullptr, nullptr);
  c->xfb_active = false;
}

// The demand of this frame (next frame's alpha/opaque split) and the loud log of
// everything that did not make it into the RT soup as it should.
void rt_capture_stats(const Capture& c) {
  if (c.discard_filter && c.record_frame % uint64_t((std::max)(cfg_int("rt_timing_frames", 300), 30)) == 0)
    log("[rt-capture] discard filter 1 frame %llu skipped %llu always-discard draws",
        (unsigned long long)c.record_frame, (unsigned long long)c.discard_skipped);
  g_alpha_demand_prev = c.alpha_demand;
  // In the opt-in material/Hair streams, capacity-forced alpha fallbacks must not
  // reserve opaque space a second time and lock the next alpha partition down.
  // Logs below retain the total physical opaque/fallback stream and all drops.
  g_opaque_demand_prev=capture_budget::opaque_partition_demand(c.cursor+c.opaque_overflow_bytes,
    c.alpha_overflow_bytes+c.alpha_spill_bytes,capture_budget::corrected_partition_feedback(g_stride) || c.alpha_spills!=0);
  std::lock_guard<std::mutex> lock(g_drop_mu);
  RtDropLog& l = g_drop;
  ++l.frames;
  l.alpha_overflow += c.alpha_overflow;
  l.alpha_overflow_mb += c.alpha_overflow_bytes >> 20;
  l.alpha_spills += c.alpha_spills;
  l.alpha_spill_bytes += c.alpha_spill_bytes;
  l.opaque_overflow += c.opaque_overflow;
  l.drop_indirect += c.drop_indirect;
  l.drop_topo += c.drop_topo;
  l.drop_topo_tris += c.drop_topo_tris;
  l.strips += c.strips;
  l.strip_tris += c.strip_tris;
  l.source_draws += c.source_draws;
  l.no_depth_draws += c.no_depth_draws;
  l.no_depth_tris += c.no_depth_tris;
  if (c.state_sample)
    for (const auto& [id, row] : c.states)
      log("[rt-capture] frame %llu pipe %llu FS %016llx depth write/test %d/%d colorWriteMask 0x%x blendMask 0x%x draws %llu triangles %llu",
          (unsigned long long)c.record_frame, (unsigned long long)id, (unsigned long long)row.fs,
          row.pipe.depth_write, row.pipe.depth_test, row.pipe.color_mask, row.pipe.blend_mask,
          (unsigned long long)row.draws, (unsigned long long)row.tris);
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
    if (c.state_log)
      log("[rt-capture] last %llu frames per frame: source draws %.2f depth-write-off draws %.2f triangles %.2f",
          (unsigned long long)l.frames, double(l.source_draws) / l.frames, double(l.no_depth_draws) / l.frames,
          double(l.no_depth_tris) / l.frames);
    log("[geo] RT capture, last %llu frames: alpha region %.0f MB (peak demand %.1f MB), opaque peak %.1f MB of a "
        "%.0f MB buffer; per frame: alpha draws made opaque (region full) %.2f, opaque draws dropped (buffer full) %.2f, "
        "indirect draws dropped %.2f, non-list draws dropped %.2f (%.0f triangles), strips/fans captured %.2f (%.0f "
        "triangles)",
        (unsigned long long)l.frames, double(g_alpha_region.load()) * mb, double(l.peak_alpha) * mb,
        double(l.peak_opaque) * mb, double(c.xfb.size) * mb, double(l.alpha_overflow) / l.frames,
        double(l.opaque_overflow) / l.frames, double(l.drop_indirect) / l.frames, double(l.drop_topo) / l.frames,
        double(l.drop_topo_tris) / l.frames, double(l.strips) / l.frames, double(l.strip_tris) / l.frames);
    if(l.alpha_spills)log("[geo] alpha partition spill: %.2f draws/frame, %.3f MB/frame; direct alpha records retained; residual made opaque %.2f draws/frame",
      double(l.alpha_spills)/l.frames,double(l.alpha_spill_bytes)*mb/l.frames,double(l.alpha_overflow)/l.frames);
    l = RtDropLog{};
    l.first_alpha = l.first_opaque = l.first_drop = true;   // only the first occurrence is logged at once
  }
}

// The pass's camera UBO (set 0 binding 2), from its first draw that has one.
static void em_pass_ubo(VkCommandBuffer cmd, EmPass* e) {
  if (e->ubo) return;
  Bg3Binding b;
  if (query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 2, 0, &b) &&
      (b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) &&
      b.offset % g_ubo_align == 0) {
    e->ubo = b.buffer;
    e->ubo_off = b.offset;
  }
}

// M5.1: a draw of a forward/effects pass with rt_water_geometry on. True: captured as water.
static bool water_pre_draw(VkCommandBuffer cmd, CmdState* s, const DrawArgs& args, EmPass* e) {
  PipeInfo pi{false, false, VK_PRIMITIVE_TOPOLOGY_MAX_ENUM};
  {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_pipes.find(s->pipe[0]);
    if (it != g_pipes.end()) pi = it->second;
  }
  EmDrawIn in{pi.xfb, pi.triangles, pi.topo, pi.restart, pi.inst.ok ? &pi.inst : nullptr, g_stride,
              pi.uv_match, pi.uv, pi.uv_fs, pi.uv_vs_comps};
  WaterRange r{};
  const int w = water_draw(cmd, s, args, in, pi.depth_write, &r, &e->water_swap);
  if (w) ++e->water_draws;
  if (w != 2) return false;
  em_pass_ubo(cmd, e);
  p_bind_xfb(cmd, 0, 1, &r.soup, &r.off, &r.bytes);
  p_begin_xfb(cmd, 0, 0, nullptr, nullptr);
  e->xfb_active = true;
  e->water_ranges.push_back(r);
  return true;
}

// D-EM: a draw of an effects pass. Emissive material draws that are not cached meshes
// are captured into the emissive soup (emissive.cpp decides).
// With rt_water_geometry, water.cpp sees each draw first.
static void em_pre_draw(VkCommandBuffer cmd, CmdState* s, const DrawArgs& args) {
  auto* e = geo_empass(s);
  if (!e) return;
  ++e->draws;
  if (e->water && water_pre_draw(cmd, s, args, e)) return;
  if (!e->em) return;
  if (!e->capture) return em_note_draw(s);   // timed only (rt_emissive=0)
  PipeInfo pi{false, false, VK_PRIMITIVE_TOPOLOGY_MAX_ENUM};
  {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_pipes.find(s->pipe[0]);
    if (it != g_pipes.end()) pi = it->second;
  }
  if (pi.em_state && cfg_hot(CfgHot::rt_em_overlap_check)) {
    static thread_local std::unordered_set<uint64_t> seen;
    if (seen.insert(s->pipe_id[0]).second && seen.size() <= 16)
      log("[em-overlap] native draw pipe %llu FS %016llx blend/src/dst/op/mask %u/%u/%u/%u/%u cull %u depth test/write/compare %u/%u/%u dynamic %u",
          (unsigned long long)s->pipe_id[0], (unsigned long long)s->fs,
          pi.em_raster[0], pi.em_raster[1], pi.em_raster[2], pi.em_raster[3], pi.em_raster[4],
          pi.em_raster[5], pi.em_raster[6], pi.em_raster[7], pi.em_raster[8], unsigned(pi.em_dynamic));
  }
  em_pass_ubo(cmd, e);
  EmDrawIn in{pi.xfb, pi.triangles, pi.topo, pi.restart, pi.inst.ok ? &pi.inst : nullptr, g_stride,
              pi.uv_match, pi.uv, pi.uv_fs, pi.uv_vs_comps};
  VkBuffer soup = VK_NULL_HANDLE;
  VkDeviceSize off = 0, bytes = 0;
  uint32_t prims = 0;
  if (!em_draw(cmd, s, args, in, &soup, &off, &bytes, &prims)) return;
  p_bind_xfb(cmd, 0, 1, &soup, &off, &bytes);
  p_begin_xfb(cmd, 0, 0, nullptr, nullptr);
  e->xfb_active = true;
  e->ranges.push_back({off, prims});
}

void geo_pre_draw(VkCommandBuffer cmd, CmdState* s, const DrawArgs& args) {
  if(geo_is(s,GeoKind::Material)){
    bool exact=false,pool_pipeline=false,basis_pipeline=false;InstPipe pool_inst{};
    {std::lock_guard lock(g_mu);auto i=g_pipes.find(s->pipe[0]);if(i!=g_pipes.end()){
      s->geo_material_no_writes&=i->second.no_storage_writes;
      const auto& p=i->second;
      const bool tileset=native_hit::tileset_deferred(s->vs,s->fs);
      native_hit::MaterialPipelineFacts facts;
      facts.tileset=tileset;facts.tileset_capture=g_tileset_capture;facts.stride=g_stride;
      facts.tileset_outputs=p.tileset_outputs;facts.tileset_inputs_valid=p.tileset_material_inputs.valid;facts.vt_outputs=p.vt_outputs;
      facts.xfb=p.xfb;facts.material_state=p.material_state;facts.depth_test=p.depth_test;facts.blend_mask=p.blend_mask;
      facts.color_mask=p.color_mask;facts.triangle_list=p.topo==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;facts.uv_ok=p.uv_match==UV_OK;
      exact=native_hit::exact_material_pipeline(facts);
      // Class A G-buffer draws feed the pool material records (their meshes are BVH-cached).
      if(native_hit::enabled() && p.inst.ok){pool_inst=p.inst;pool_pipeline=native_hit::pool::pool_material_pipeline(facts);
        basis_pipeline=pool_pipeline && p.xfb && p.vt_outputs==1u && g_stride>=64u;}
      // One sparse, exact state record per audited native pipeline. A later
      // unsupported term is visible without another blind attribution run.
      static std::unordered_set<uint64_t> reported;
      if(native_hit::enabled() && cfg_hot(CfgHot::rt_stats) && ((s->vs==0x3812bc980438747full && s->fs==0x8995da1e4060f53bull) || (g_tileset_capture && tileset)) &&
         reported.size()<16 && reported.insert(uint64_t(s->pipe[0])).second)
        log("[hit material pipeline] pipe %u accepted %u xfb %u basis %u static %u reasons 0x%x depth test %d blend 0x%x color 0x%x topo %u uv %u; raster/multisample/blend pNext %u/%u/%u",
          s->pipe_id[0],unsigned(exact),unsigned(p.xfb),tileset?p.tileset_outputs:p.vt_outputs,unsigned(p.material_state),p.material_reasons,p.depth_test,p.blend_mask,p.color_mask,unsigned(p.topo),unsigned(p.uv_match),p.raster_next,p.multisample_next,p.blend_next);

    }else s->geo_material_no_writes=false;}
    VkBuffer buffer{};VkDeviceSize offset=0,bytes=0;
    if(pool_inst.ok && native_hit::pool_observe(cmd,s,args,pool_inst,pool_pipeline,basis_pipeline,g_stride,&buffer,&offset,&bytes)){
      p_bind_xfb(cmd,0,1,&buffer,&offset,&bytes);p_begin_xfb(cmd,0,0,nullptr,nullptr);s->geo_material_active=true;return;
    }
    if(native_hit::deferred_begin(cmd,s,args,exact,g_stride,&buffer,&offset,&bytes)){
      p_bind_xfb(cmd,0,1,&buffer,&offset,&bytes);p_begin_xfb(cmd,0,0,nullptr,nullptr);s->geo_material_active=true;
    }return;
  }
  if (geo_is(s, GeoKind::Em)) return em_pre_draw(cmd, s, args);
  auto* c = geo_capture(s);
  if (!c) return;
  const uint32_t kind = args.kind, count = args.count, instances = args.instances;
  if(c->rt)native_hit::prepass_observe(cmd,s,args);
  DrawRec d{kind, s->pipe_id[0], s->vs, s->fs, count, instances, -1, 0, VK_NULL_HANDLE, 0, 0, 0};
  PipeInfo pi{false, false, VK_PRIMITIVE_TOPOLOGY_MAX_ENUM};
  {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_pipes.find(s->pipe[0]);
    if (it != g_pipes.end()) pi = it->second;
  }
  if (c->rt && c->discard_filter && pi.always_discard) {
    ++c->discard_skipped;
    return;
  }
  if (c->cascade) {
    cascade_pre_draw(cmd, s, c, args, pi);
    return;
  }
  if (c->cc_keys) {   // every draw of the camera source pass, captured or not (rt_cascade_casters dedupe)
    const cascade::DrawId id = cascade_draw_id(cmd, s, args, pi);
    std::lock_guard<std::mutex> lock(g_cc_mu);
    g_cc_mesh_keys.insert(c->record_frame, cascade::mesh_key(id));
    if (id.place) g_cc_keys.insert(c->record_frame, cascade::draw_key(id));
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
    prims = (uint64_t((std::max)(cfg_hot(CfgHot::geo_indirect_mb), 1)) << 20) / (3ull * g_stride);
  }
  const uint64_t tri_bytes = 3ull * g_stride;
  if (c->rt && pi.xfb && pi.triangles && prims && kind <= 1) {
    if (c->state_log) {
      ++c->source_draws;
      if (pi.depth_write == 0) ++c->no_depth_draws, c->no_depth_tris += prims;
      if (c->state_sample) {
        auto& row = c->states[d.pipe_id];
        row.fs = s->fs; row.pipe = pi; ++row.draws; row.tris += prims;
      }
    }
  }
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
             (kind == 0 || !pi.restart) && cfg_hot(CfgHot::rt_strips)))
    d.skip = 4, ++c->drop_topo, c->drop_topo_tris += prims;
  else if ((c->readback && c->slots >= c->max_slots) || c->cursor + d.reserved > c->opaque_end) d.skip = 3;
  bool capture = d.skip == 0 && d.reserved > 0;
  if (c->rt && d.skip == 0 && pi.topo != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) ++c->strips, c->strip_tris += prims;
  // Track D phase 3: an alpha-tested draw (its fragment shader is in the
  // table, the VS captured the UV it samples with).
  const bool capturable = c->rt && d.skip != 1 && d.skip != 2 && d.skip != 4 && d.reserved > 0;
  AlphaFs a{};
  const bool alpha_fs = capturable && g_alpha_mode && alpha_lookup(s->fs, &a) && (a.kind==5 || (a.uv == pi.uv && pi.uv >= 0)) &&
                        (a.kind != 4 || (pi.hair_outputs & (a.channel ? 3u : 1u)) == (a.channel ? 3u : 1u)) &&
                        cfg_hot(CfgHot::rt_alpha_capture);
  if(a.kind==4 && (pi.hair_outputs & (a.channel?3u:1u)) != (a.channel?3u:1u))
    alpha_native_hair_missing_attributes(s->fs,s->vs,pi.hair_outputs);
  // Track D BVH split: a draw of a cached instanced mesh is not captured; its
  // instances go into the TLAS with the matrices of the game's instance stream.
  uint32_t token = UINT32_MAX;
  bool audit = false;
  // rt_primary_fade_class (diagnostics only): a draw whose FS has BG3's camera-occlusion
  // capsule fade with its cut enabled is marked in InstInfo (kInfoCameraFade) for the
  // primary-ray check. Its capture is unchanged: the wall still occludes every ray.
  bool camera_fade = false;
  if (c->fade_class && capturable) camera_fade_draw(cmd, s->fs, prims, &camera_fade);
  if (c->split && capturable) {
    const bool list = pi.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST && kind <= 1;
    // Native camera cutouts retain their original projection/Frame packet.
    // Keep the matching positions dynamic, never a separately transformed cache.
    BvhDraw bd{list && pi.inst.ok && (!alpha_fs || (pi.inst.uv_ok && a.kind != 4 && a.kind != 3 && a.kind != 5)) ? &pi.inst : nullptr, s->vs, d.pipe_id, alpha_fs,
               args, s};
    uint32_t mesh_tris = 0;
    BvhVerdict v = bvh_classify(cmd, bd, &token, &mesh_tris);
    if (camera_fade) bvh_set_camera_fade(token);
    if (v != BVH_CAPTURE) {
      uint32_t rec = UINT32_MAX;
      if (alpha_fs && !alpha_add_draw(cmd, a, 0, mesh_tris, &rec)) rec = UINT32_MAX;
      bvh_set_record(token, rec);
      if (v == BVH_CACHED) return;
      audit = true;   // also captured (opaque region), for the cache's position check
    }
  }
  // Alpha-tested draws go into the alpha region.
  bool alpha = false;
  bool alpha_spill = false; // Storage ownership is separate from alpha semantics.
  uint32_t rec = UINT32_MAX;
  if (alpha_fs && !audit) {
    c->alpha_demand += d.reserved;
    if (c->alpha_cursor + d.reserved > c->xfb.size) {
      uint64_t spill_offset=0;
      if(capture_budget::alpha_spill_offset(cfg_hot(CfgHot::rt_alpha_spill)!=0,c->split,
          token!=UINT32_MAX,pi.topo==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST && kind<=1,
          c->cursor,c->opaque_end,d.reserved,spill_offset) &&
          alpha_add_draw(cmd,a,0,uint32_t(prims),&rec)) {
        // Split-only direct record: first_tri is unused. Never publish this
        // draw through the legacy first_tri lookup or consume alpha_cursor.
        alpha=alpha_spill=true;capture=true;d.offset=spill_offset;
        ++c->alpha_spills;c->alpha_spill_bytes+=d.reserved;
      } else {
        // Both ranges or material/record capacity failed. Preserve the existing
        // counted fallback; this increment does not invent missing coverage.
        ++c->alpha_overflow;
        c->alpha_overflow_bytes += d.reserved;
      }
    } else if (alpha_add_draw(cmd, a, uint32_t((c->alpha_cursor - c->alpha_base) / tri_bytes), uint32_t(prims), &rec)) {
      alpha = true;
      capture = true;
      d.offset = c->alpha_cursor;
    }
  }
  if (c->rt && !alpha && (d.skip == 3 || (d.skip == 0 && c->cursor + d.reserved > c->opaque_end)))
    ++c->opaque_overflow, c->opaque_overflow_bytes += d.reserved;
  if(c->rt && g_tileset_capture && cfg_hot(CfgHot::rt_native_tileset) && native_tileset_coverage::lookup_camera(s->fs)!=nullptr && (!alpha || !capture))alpha_tileset_incomplete();
  uint32_t materialRecord=UINT32_MAX;
  if(capture && c->rt && !alpha && !audit && pi.topo==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
    materialRecord=native_hit::note_opaque_prepass(cmd,s,args,d.offset,g_stride,uint32_t(prims));
  if (token != UINT32_MAX) {
    if (capture) bvh_captured(token, d.offset, uint32_t(prims), alpha, alpha ? rec : UINT32_MAX,materialRecord);
    else bvh_note_skipped(token);
  }
  if (capture && c->rt) {
    // Draw identity for the refit decision: same draws in the same order give
    // the same triangle order.
    for (uint64_t v : {uint64_t(d.pipe_id), uint64_t(count), uint64_t(instances), uint64_t(alpha)})
      c->sig = (c->sig ^ v) * 1099511628211ull;
    ++c->rt_draws;
    c->alpha_draws += alpha;
  }
  if (capture) {
    if(c->rt && alpha && !audit && pi.topo==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
      native_hit::note_prepass(cmd,s,args,rec,d.offset,g_stride,uint32_t(prims));
    VkDeviceSize off = d.offset, size = d.reserved;
    p_bind_xfb(cmd, 0, 1, &c->xfb.b, &off, &size);
    p_begin_xfb(cmd, 0, 0, nullptr, nullptr);
    c->xfb_active = true;
    (alpha && !alpha_spill ? c->alpha_cursor : c->cursor) += d.reserved;
  }
  if (capture && c->readback) {
    d.slot = int32_t(c->slots);
    p_begin_query(cmd, c->qp, uint32_t(d.slot), 0);
    c->active_slot = d.slot;
  }
  if (c->readback) c->draws.push_back(d);
}

void geo_post_draw(VkCommandBuffer cmd, CmdState* s) {
  if(geo_is(s,GeoKind::Material)){if(s->geo_material_active)p_end_xfb(cmd,0,0,nullptr,nullptr);s->geo_material_active=false;return;}
  if (geo_is(s, GeoKind::Em)) {
    auto* e = geo_empass(s);
    if (e && e->xfb_active) p_end_xfb(cmd, 0, 0, nullptr, nullptr), e->xfb_active = false;
    if (e && e->water_swap) water_unswap(cmd, s), e->water_swap = false;
    return;
  }
  auto* c = geo_capture(s);
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

// The command buffer is begun again, reset or freed with an effects pass still open (its
// recording is never submitted): its EmPass is freed (geo_pass_after never runs for it).
void geo_cmd_discard(CmdState* s) {
  if (!geo_is(s, GeoKind::Em)) return;
  delete geo_empass(s);
  s->geo = {};
}

// After the game's vkCmdEndRenderPass (outside the pass).
void geo_pass_after(VkCommandBuffer cmd, CmdState* s) {
  if(geo_is(s,GeoKind::Material)){native_hit::deferred_finish(cmd,s->geo_material_no_writes);s->geo_material_active=false;s->geo={};return;}
  if (geo_is(s, GeoKind::Em)) {
    auto* e = geo_empass(s);
    s->geo = {};
    if (e && e->em) em_pass_done(cmd, s, e->ubo, e->ubo_off, g_stride, e->ranges, e->ts);
    if (e && e->water) water_pass_done(cmd, e->ubo, e->ubo_off, g_stride, e->water_ranges, e->draws, e->water_draws);
    delete e;
    return;
  }
  if (s->geo_ts >= 0 && g_qp) g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qp, s->geo_ts * 2 + 1);
  const int ts_slot = s->geo_ts;
  s->geo_ts = -1;
  auto* c = geo_capture(s);
  // The pass's timing sample is classified by camera motion once the scene build
  // below has looked at the camera (static vs pan medians, TRACK_D_RT.md 11).
  struct MotionTag {
    int slot;
    ~MotionTag() {
      if (slot < 0) return;
      std::lock_guard<std::mutex> lock(g_ts_mu);
      g_ts_moving[slot] = rt_camera_moving();
      g_ts_split[slot] = bvh_split_wanted();
    }
  } motion_tag{ts_slot};
  s->geo = {};
  if (!c) return;
  if (c->cascade) {
    cascade_pass_after(cmd, c);
    return;
  }
  if (!c->readback) {
    // rt_alpha: a small depth-only pass (not the scene's) is captured once
    // until the big one is known, and dropped.
    const bool minor_pass = g_alpha_mode && s->pass.draw_count < kPrepassMinDraws;
    if (g_alpha_mode && !minor_pass) {
      g_scene_frame1 = frame_index() + 1;
      const int was = g_scene_ordinal.exchange(int(s->pass.ordinal));
      if (was >= 0 && was != int(s->pass.ordinal)) {
        const uint32_t n = ++g_scene_moves;
        if (n <= 20 || n % 100 == 0)
          log("[geo] scene prepass moved: ordinal %d -> %d (frame %llu, %u moves so far, %u minor passes dropped)", was,
              int(s->pass.ordinal), (unsigned long long)frame_index(), n, g_minor_dropped.load());
      }
    } else if (minor_pass) {
      ++g_minor_dropped;
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
      rc.split = c->split;
      rc.xfb_size = c->xfb.size;
      if (cfg_hot(CfgHot::rt_sun_cascade_current))
        geo_native_frame_epoch(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,&rc.native_epoch,
                                &rc.native_frame_buffer,&rc.native_frame_offset);
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

int geo_cascade_soup(uint64_t frame, CascadeSoup* out) {
  std::lock_guard<std::mutex> lock(g_cc_mu);
  const CcMeta& m = g_cc_meta[frame % 3];
  const Capture& c = g_ccap[frame % 3];
  if (m.frame != frame || !m.tris || !c.xfb.b) return 1;
  if (bvh_rec_state(m.rec) != 1) return 2;   // its conversion has not run (yet) or never will
  *out = CascadeSoup{c.xfb.b, m.tris, g_stride, m.has_origin, {m.origin[0], m.origin[1], m.origin[2]}, m.sig,
                     m.rec, c.cmd};
  return 0;
}

bool geo_native_frame_epoch(VkCommandBuffer cmd, VkPipelineBindPoint bind,
                             sun_coverage::NativeEpoch* epoch, VkBuffer* buffer, VkDeviceSize* offset) {
  if (!epoch || !buffer || !offset) return false;
  *epoch={};*buffer=VK_NULL_HANDLE;*offset=0;
  Bg3Binding b{};
  if (!query_binding(cmd,bind,0,0,0,&b) || !b.buffer ||
      (b.type!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && b.type!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) ||
      (b.range!=VK_WHOLE_SIZE && b.range<sizeof epoch->words) ||
      !read_mapped(b.buffer,b.offset,epoch->words.data(),sizeof epoch->words)) return false;
  epoch->available=true;
  if (!sun_coverage::valid(*epoch)) { *epoch={};return false; }
  *buffer=b.buffer;*offset=b.offset;return true;
}

bool geo_sun_cascades(const sun_coverage::NativeEpoch& epoch, std::array<SunCascadeCapture,4>* out) {
  if(!out || !sun_coverage::valid(epoch)) return false;
  *out={};
  std::unique_lock<std::mutex> lock(g_cc_mu);
  auto find=[&]() {
    std::array<SunCascadeCapture,4> found{};
    VkImage image=VK_NULL_HANDLE;
    for(unsigned layer=0;layer<4;++layer) {
      const CcMeta* best=nullptr;
      for(const CcMeta& m:g_sun_meta[layer])
        if(m.tris && sun_coverage::same_epoch(m.sun.native_epoch,epoch) &&
           bvh_rec_state(m.rec)>=0 && bvh_rec_state(m.rec)!=2 && (!best || m.rec>best->rec)) best=&m;
      if(!best || !best->sun.depth_image || (image && image!=best->sun.depth_image)) return false;
      image=best->sun.depth_image;found[layer]=best->sun;
    }
    *out=std::move(found);return true;
  };
  if(find()) return true;
  // Worker recording order differs from GPU submission order. Never wait for
  // GPU completion here or for a future presentation/native epoch.
  const int ms=(std::clamp)(cfg_int("rt_scene_wait_ms",12),0,50);
  return ms>0 && g_cc_cv.wait_for(lock,std::chrono::milliseconds(ms),find);
}

bool geo_sun_captures_owned(std::span<const SunCascadeCapture> captures) {
  std::lock_guard<std::mutex> lock(g_cc_mu);
  for(const auto& capture:captures) {
    if(capture.layer>=4) return false;
    bool owned=false;
    for(const auto& current:g_sun_meta[capture.layer])
      if(current.rec==capture.soup.record && current.sun.soup.xfb==capture.soup.xfb &&
         current.sun.soup.producer==capture.soup.producer &&
         sun_coverage::same_epoch(current.sun.native_epoch,capture.native_epoch)) owned=true;
    if(!owned) return false;
  }
  return !captures.empty();
}

void geo_cascade_note_build(int reason, uint32_t tris) {
  std::lock_guard<std::mutex> lock(g_cc_mu);
  if (reason < 0 || reason > 5) return;
  ++g_cc_st.builds[reason];
  if (!reason) g_cc_st.build_tris += tris;
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
      if (g_on && cfg_hot(CfgHot::geo_capture)) log("[geo] no G-buffer pass was captured in frame %llu", (unsigned long long)frame);
      return;
    }
    g_cap.dir = dir;
    g_cap.wait_until = frame_index() + 120;
  }
  try_process(queue);
}

void geo_on_present(VkQueue queue) {
  if (g_cap.state != 0) try_process(queue);
  if (g_dd) cascade_on_present();
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
    TsAcc& a = g_ts_acc[g_ts_xfb[s] ? 1 : 0][g_ts_moving[s] ? 1 : 0][g_ts_split[s] ? 1 : 0];
    a.v.push_back(ms);
  }
  int every = (std::max)(cfg_int("geo_timing_frames", 300), 30);
  for (int k = 0; k < 2; ++k)
    for (int mv = 0; mv < 2; ++mv)
      for (int sp = 0; sp < 2; ++sp) {
        TsAcc& a = g_ts_acc[k][mv][sp];
        if (int(a.v.size()) < every) continue;
        std::sort(a.v.begin(), a.v.end());
        double sum = 0;
        for (float v : a.v) sum += v;
        log("[geo] %s pass GPU time (%s, %s%s, %zu frames): median %.3f ms, mean %.3f, p10 %.3f, p90 %.3f, p99 %.3f",
            g_alpha_mode ? "depth prepass" : "G-buffer",
            k ? "XFB capture active" : g_on ? "shaders patched, XFB idle" : "unpatched", mv ? "camera moving" : "static",
            k ? (sp ? ", BVH split" : ", legacy BVH") : "", a.v.size(), a.v[a.v.size() / 2], sum / a.v.size(),
            a.v[a.v.size() / 10], a.v[a.v.size() * 9 / 10], a.v[(std::min)(a.v.size() - 1, a.v.size() * 99 / 100)]);
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
  {
    std::lock_guard<std::mutex> lock(g_cc_mu);
    for (Capture& c : g_ccap) release(c);
    for(auto& layer:g_sun_ccap) for(Capture& c:layer) release(c);
    for(auto& layer:g_sun_meta) for(CcMeta& m:layer) m=CcMeta{};
    for (CcMeta& m : g_cc_meta) m = CcMeta{};
    if (g_cc_qp) dd->vk.DestroyQueryPool(dd->device, g_cc_qp, nullptr);
    g_cc_qp = VK_NULL_HANDLE;
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

// From the tracker's vkDestroyBuffer hook (the one hook for it).
void geo_forget_buffer(VkBuffer b) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_buffer_usage.erase(b);
  g_buffer_size.erase(b);
}

VkBufferUsageFlags geo_buffer_usage(VkBuffer b) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_buffer_usage.find(b);
  return it == g_buffer_usage.end() ? 0 : it->second;
}
VkDeviceSize geo_buffer_size(VkBuffer b) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_buffer_size.find(b);
  return it == g_buffer_size.end() ? 0 : it->second;
}

bool geo_native_tileset_inputs(VkPipeline pipeline,uint32_t subtype) {
 std::lock_guard lock(g_mu);auto p=g_pipes.find(pipeline);
 if(!g_tileset_capture || p==g_pipes.end() || !p->second.xfb)return false;
 if(subtype==1u)return p->second.uv==0; // exact VS/FS checked by canonical capture
 return subtype==0u && p->second.tileset_outputs==1u && p->second.uv==1;
}
bool geo_native_tileset_material_inputs(VkPipeline pipeline,native_hit::TilesetInputs* out) {
 if(!out)return false;*out={};std::lock_guard lock(g_mu);auto p=g_pipes.find(pipeline);
 if(!g_tileset_capture || g_stride<80u || !native_hit::enabled() || p==g_pipes.end())return false;
 const auto& info=p->second;
 if(!info.xfb || info.uv!=1 || !info.tileset_material_inputs.valid)return false;
 if((info.tileset_family==1u && info.tileset_outputs!=1u) ||
    (info.tileset_family==2u && info.tileset_outputs!=3u) || !info.tileset_family)return false;
 *out=info.tileset_material_inputs;return true;
}
bool geo_native_hit_inputs(VkPipeline pipeline,native_hit::StaticInputs* out){
 if(!out)return false;std::lock_guard lock(g_mu);auto p=g_pipes.find(pipeline);if(p==g_pipes.end())return false;
 *out=p->second.material_inputs;return out->valid;
}
bool geo_uniform_buffer_range(VkBuffer buffer,VkDeviceSize offset,VkDeviceSize bytes) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto usage=g_buffer_usage.find(buffer);auto size=g_buffer_size.find(buffer);
  return usage!=g_buffer_usage.end() && size!=g_buffer_size.end() && (usage->second&VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) &&
    offset<=size->second && bytes<=size->second-offset;
}

void register_geo_hooks(HookMap& m) {
  m["vkCmdSetViewport"] = reinterpret_cast<PFN_vkVoidFunction>(&CmdSetViewport);
  m["vkCmdSetScissor"]=reinterpret_cast<PFN_vkVoidFunction>(&CmdSetScissor);
  m["vkCreateBuffer"] = reinterpret_cast<PFN_vkVoidFunction>(&CreateBuffer);
}

}  // namespace bg3
