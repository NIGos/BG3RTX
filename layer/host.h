// Internals shared by the host layer's translation units.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <array>
#include <atomic>
#include <cstdint>
#include "../common/pan_epoch.h"
#include "../common/sun_caster_coverage.h"
#include "../common/trace_sampled_view.h"
#include <mutex>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/host_api.h"

// BG3RTX_DIAGNOSTICS (CMake option, default ON): 0 compiles out diagnostic-only host
// code (terrain packet dump, pan bursts, rt_order_trace, cpu_timing hook timers, the
// ls_native_direct_validate SPIR-V patch). Its hooks below become inline no-ops.
#ifndef BG3RTX_DIAGNOSTICS
#define BG3RTX_DIAGNOSTICS 1
#endif

namespace bg3 {

// ---- logging / paths (util.cpp) ----
void log(const char* fmt, ...);
const std::string& root_dir();        // repository root (parent of bin/)
const std::string& bin_dir();
// %BG3RTX_RUN_TAG% (scripts/auto-run.ps1 -Tag): lets two games run from one checkout.
// Empty when unset; otherwise the log, the auto.done marker, trace.request and the
// capture folders carry it (bg3rtx.<tag>.log, captures/<stamp>-<tag>, ...).
const std::string& run_tag();
std::string tagged(const std::string& base, const std::string& ext);   // base + ".<tag>" + ext
std::string capture_stamp();          // now_stamp(), plus "-<tag>" when tagged
std::string now_stamp();              // 20261025-224512
uint64_t file_mtime(const std::string& path);   // 0 if missing
bool write_file(const std::string& path, const void* data, size_t size);
void make_dirs(const std::string& path);

uint64_t hash64(const void* data, size_t size);   // FNV-1a 64

// ---- config (util.cpp) ----
void cfg_poll();                       // re-reads bg3rtx.ini when it changes
int cfg_int(const char* key, int fallback);
// rt_order_trace=N: log record/submit order of the scene build and the RT passes for N
// frames every 600 frames (P1 frame-lag diagnosis).
#if BG3RTX_DIAGNOSTICS
bool order_trace_active();
#else
inline bool order_trace_active() { return false; }
#endif
float cfg_float(const char* key, float fallback);
// Hot-path settings (per draw, per pass, per submit): cfg_hot(CfgHot::k) is exactly
// cfg_int("k", fallback in util.cpp's kCfgHot), cached by every cfg_poll (once per present,
// the only place the merged settings and the A/B hotkey change), read without a lock.
enum class CfgHot : int {
  geo_indirect_mb, rt_strips, rt_alpha_capture, rt_alpha_spill, rt_native_tileset, rt_stats,
  rt_em_overlap_check, rt_cascade_casters, geo_capture, pan_capture,
  fg_input_copy, pan_inputs, rt_event_flush, sl_log_barriers, rt_build, rt_bvh_split, rt_omm,
  rt_hit_material, rt_native_hair, rt_native_scalp, rt_mesh_audit_frames, rt_mesh_audits,
  rt_emissive, rt_em_backdrop, rt_em_decal, rt_sun_cascade_current, rt_cascade_index, rt_alpha_cut,
  rt_alpha_stochastic, rt_alpha_unknown, rt_alpha_test, rt_water_geometry, rt_water_compose, rt_hit_material_instanced,
  rt_water_shadows, rt_water_ls_shadows, rt_water_refraction, rt_hit_material_recycle, rt_hit_material_capture_unlocked, rt_hit_material_lean_hooks,
  Count
};
int cfg_hot(CfgHot key);

// ---- frame cap (framecap.cpp; fps_cap) ----
void framecap_on_present();            // sleep limiter when Streamline's Reflex limiter is not active
bool sl_limits_frames();               // streamline.cpp: Reflex options are set (fps_cap goes there)

// ---- per-instance / per-device state (layer.cpp) ----
struct InstanceData {
  VkInstance instance = VK_NULL_HANDLE;
  PFN_vkGetInstanceProcAddr gipa = nullptr;
  PFN_vkDestroyInstance DestroyInstance = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
  PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
  PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties2 GetPhysicalDeviceMemoryProperties2 = nullptr;   // VK_EXT_memory_budget ([mem] line)
  PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2 = nullptr;
  PFN_vkCreateWin32SurfaceKHR CreateWin32SurfaceKHR = nullptr;
  PFN_vkDestroySurfaceKHR DestroySurfaceKHR = nullptr;
  uint32_t api_version = 0;            // as created (after our own bump for rt=1)
};

struct DeviceData {
  VkDevice device = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  InstanceData* inst = nullptr;
  PFN_vkGetDeviceProcAddr gdpa = nullptr;
  VkDeviceFuncs vk{};
  uint32_t graphics_family = UINT32_MAX;
  VkQueue graphics_queue = VK_NULL_HANDLE;
  std::vector<std::string> extensions;
  // Loader callback for dispatchable objects created below the loader's
  // trampolines (Streamline's command buffers and queues).
  PFN_vkSetDeviceLoaderData set_loader_data = nullptr;
};

void headless_on_swapchain();
DeviceData* main_device();             // the first device created; BG3 makes one
DeviceData* device_of(const void* dispatchable);   // device, queue or command buffer
InstanceData* instance_data(const void* dispatchable);   // instance or physical device

// ---- object tracking (tracker.cpp) ----
struct ImageRec {
  uint32_t id;
  VkImageCreateInfo ci;                // pNext/pQueueFamilyIndices cleared
  bool swapchain;
};
struct ViewRec { VkImage image; VkFormat format; VkImageSubresourceRange range; trace_view::Creation creation{}; };
struct PipelineRec {
  uint32_t id;
  VkPipelineBindPoint bind_point;
  uint64_t vs, fs, cs, other;          // SPIR-V hashes per stage (0 if absent)
  VkRenderPass render_pass;
  uint32_t subpass;
  uint32_t native_validation = 0;
};
struct RenderPassRec {
  std::vector<VkFormat> formats;
  std::vector<VkImageLayout> final_layouts;
  std::vector<VkAttachmentLoadOp> load_ops;
  struct Subpass { std::vector<uint32_t> color; uint32_t depth = VK_ATTACHMENT_UNUSED; };
  std::vector<Subpass> subpasses;
};
struct DescEntry {
  uint32_t binding, element;
  VkDescriptorType type;
  VkImageView view;                    // image descriptors
  VkBuffer buffer;                     // buffer descriptors
  VkDeviceSize offset, range;
  VkImageLayout layout;                // image descriptors
  VkSampler sampler = VK_NULL_HANDLE;  // OMM metadata only; public host ABI unchanged
  uint64_t sampler_id = 0;             // generation at write, never raw-handle age
};

inline uint64_t desc_key(uint32_t binding, uint32_t element) { return (uint64_t(binding) << 32) | element; }
inline bool is_dynamic_type(VkDescriptorType t) {
  return t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

struct Tracker {
  std::shared_mutex mu;
  std::unordered_map<VkShaderModule, uint64_t> modules;
  std::unordered_map<VkShaderModule, uint32_t> module_validation;
  std::unordered_map<VkImage, ImageRec> images;
  std::unordered_map<VkImageView, ViewRec> views;
  std::unordered_map<VkFramebuffer, std::vector<VkImageView>> framebuffers;
  std::unordered_map<VkRenderPass, RenderPassRec> render_passes;
  std::unordered_map<VkPipeline, PipelineRec> pipelines;
  // Keyed by (binding << 32 | element): BG3's bindless sets hold very large arrays.
  std::unordered_map<VkDescriptorSet, std::unordered_map<uint64_t, DescEntry>> sets;
  std::unordered_map<VkDescriptorSet, uint32_t> dynamic_counts;  // fallback: dynamic descriptors written per set
  // Dynamic-buffer bindings of each set layout as (binding, count), sorted by
  // binding, and the layout every set was allocated with: dynamic offsets are
  // consumed in this order.
  std::unordered_map<VkDescriptorSetLayout, std::vector<std::pair<uint32_t, uint32_t>>> layout_dynamic;
  std::unordered_map<VkDescriptorSet, VkDescriptorSetLayout> set_layouts;
  std::unordered_map<VkDescriptorUpdateTemplate, std::vector<VkDescriptorUpdateTemplateEntry>> templates;
  // Host-visible memory, so uniform buffer contents can be read on the CPU.
  std::unordered_map<VkBuffer, std::pair<VkDeviceMemory, VkDeviceSize>> buffer_mem;
  std::unordered_map<VkDeviceMemory, uint8_t*> mapped;   // base of the mapping (offset 0)
  std::atomic<uint32_t> next_image_id{1}, next_pipeline_id{1};
  std::atomic<uint64_t> next_view_id{1};
};
Tracker& tracker();
// Caller holds tracker().mu. Number of dynamic offsets a set consumes, and the
// index of (binding, element) among them (UINT32_MAX if not dynamic/unknown).
uint32_t set_dynamic_count_locked(VkDescriptorSet set);
uint32_t set_dynamic_index_locked(VkDescriptorSet set, uint32_t binding, uint32_t element);
void ngx_forget_image(VkImage image);

void track_set_write(const VkWriteDescriptorSet& w);
void track_set_template(VkDescriptorSet set, VkDescriptorUpdateTemplate tpl, const void* data);
std::vector<DescEntry> decode_push(const VkWriteDescriptorSet* writes, uint32_t count);
std::vector<DescEntry> decode_push_template(VkDescriptorUpdateTemplate tpl, const void* data);

// ---- command buffer state (cmd.cpp) ----
struct CmdState {
  VkDevice allocation_device = VK_NULL_HANDLE;
  VkCommandPool allocation_pool = VK_NULL_HANDLE;
  bool in_pass = false;
  // Work of ours (rrpost steps) recorded since the game's last vkCmdSetEvent: the
  // game's vkCmdWaitEvents does not cover it (cmd.cpp inserts a barrier first).
  bool ours_since_event = false;
  Bg3PassInfo pass{};
  VkImageLayout color_layouts[8] = {};  // layout each attachment is left in when the pass ends
  VkImageLayout depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  uint32_t pass_ordinal = 0;
  std::vector<uint32_t> dumps;         // image dumps recorded into this buffer
  VkPipeline pipe[2] = {};             // graphics, compute
  uint32_t pipe_id[2] = {};
  uint64_t vs = 0, fs = 0, cs = 0;     // stage hashes of the bound pipelines
  uint32_t cs_native_validation = 0;
  std::array<VkDescriptorSet, 8> sets[2] = {};
  std::vector<uint32_t> dyn[2][8];     // dynamic offsets of each bound set, in binding order
  VkPipelineLayout set_layout[2][8] = {};
  // The game's descriptor binds, replayed verbatim by restore_bindings.
  struct BindCall {
    VkPipelineLayout layout;
    uint32_t first;
    std::vector<VkDescriptorSet> sets;
    std::vector<uint32_t> dyn;
  };
  std::vector<BindCall> calls[2];
  // Streamline bookkeeping: layouts the watched images end this buffer in, and
  // the camera constants read at the velocity pass (applied at submit).
  std::vector<std::pair<VkImage, VkImageLayout>> sl_layouts;
  std::vector<std::pair<VkImage, VkImageLayout>> sl_input_layouts; // actual layouts for diagnostic reads
  bool sl_cam_valid = false;
  Bg3Binding sl_mvec_copy{};
  Bg3Binding sl_depth_copy{};
  Bg3Binding sl_hudless{}, sl_ui{}; // publish composite inputs in submit order
  uint32_t sl_mvec_slot = UINT32_MAX;
  uint8_t sl_cam[2][256] = {};
  std::vector<DescEntry> push[2];
  uint32_t push_set[2] = {UINT32_MAX, UINT32_MAX};
  uint32_t dispatches_since_pass = 0, indirect_since_pass = 0;
  uint64_t last_cs = 0;
  bool tracing = false;                // began while a trace was recording
  uint64_t recording = 0;              // unique per vkBeginCommandBuffer (rrpost chains)
  VkCommandBufferUsageFlags begin_flags = 0; // exact native recording contract
  VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_SECONDARY; // unknown stays unsupported
  PanEpoch pan_epoch{};                // depth-to-tonemap identity within this recording
  bool game_alloc = false;             // allocated through this layer (not by Streamline below it)
  std::vector<std::string> events;     // pre-rendered JSON objects, one per event
  // segment aggregation for the trace
  std::string seg_key;
  uint32_t seg_count = 0;
  std::string seg_json;
  // Track D geometry capture (geocapture.cpp)
  // The capture active in the current pass. Kind::Capture: `object` is geocapture.cpp's
  // Capture; Kind::Em: its EmPass (an effects pass of the emissive capture, D-EM);
  // Kind::Material: the opt-in exact deferred VT stream (no object). Only geocapture.cpp
  // reads `object`, through the kind.
  struct GeoPass {
    enum class Kind : uint8_t { None, Capture, Em, Material };
    Kind kind = Kind::None;
    void* object = nullptr;
    explicit operator bool() const { return kind != Kind::None; }
  } geo;
  bool geo_material_active = false;   // Kind::Material: its XFB is active
  bool geo_material_no_writes = false;
  int geo_ts = -1;                     // timestamp slot of the current pass
  VkViewport viewport{};               // last vkCmdSetViewport(first = 0)
  bool has_viewport = false;
  VkRect2D scissor{};
  bool has_scissor=false;
  bool depth_cleared_empty=false;
  VkRect2D render_area{};
  // Vertex and index buffer bindings (Track D BVH split: mesh keys, instance streams).
  struct VertexBinding { VkBuffer buffer = VK_NULL_HANDLE; VkDeviceSize offset = 0, stride = 0; };   // stride 0: the pipeline's
  static constexpr uint32_t kMaxVertexBindings = 16;
  std::array<VertexBinding, kMaxVertexBindings> vb{};
  VkBuffer ib = VK_NULL_HANDLE;
  VkDeviceSize ib_offset = 0;
  VkIndexType ib_type = VK_INDEX_TYPE_UINT16;
  // Game images, views and buffers this recording's work (ours or the game's
  // passes our hooks read) references; merged into the lifetime registry at
  // submit (tracker.cpp, life_*).
  std::vector<uint64_t> refs;
};
// The arguments of a draw (geo_pre_draw).
struct DrawArgs {
  uint32_t kind;             // 0 draw, 1 indexed, 2 indirect, 3 indexed indirect, 4/5 indirect count
  uint32_t count, instances; // vertex/index count and instances (0 if indirect)
  uint32_t first;            // first vertex / first index
  int32_t vertex_offset;     // indexed draws
  uint32_t first_instance;
};
CmdState* cmd_state(VkCommandBuffer cmd, bool create = false);
uint64_t cmd_recording(VkCommandBuffer cmd); // locked copy; safe across reset/free
void cmd_forget(VkCommandBuffer cmd);

// ---- lifetime of game objects our GPU work references (tracker.cpp) ----
// Every game image, view or buffer that our recorded work reads or writes is
// noted on the recording (query_binding results, pass attachments, explicit
// notes). At submit the notes become "submitted in frame F on queue Q"; every
// present appends a fence of ours to the present queue. When the game destroys
// a noted object, the destroy hook first waits for the fence that covers its
// last submission, so no pending work of ours (or of the game's own push
// descriptors in the same command buffer) can touch freed memory.
void life_note(CmdState* s, uint64_t handle);
void life_note(VkCommandBuffer cmd, uint64_t handle);
void life_extend(uint64_t handle, uint64_t frame);   // in use by work submitted up to `frame`
void alpha_slot_in_use(uint32_t slot, uint64_t frame);   // alphatest.cpp: the slot's game views, life_extend
void life_on_submit(VkQueue queue, const std::vector<VkCommandBuffer>& cmds);
void life_on_present(DeviceData* dd, VkQueue queue);   // after the present went down, queue still owned
// Around the game's vkDeviceWaitIdle: submissions before begin are complete after it.
uint64_t life_gpu_done();   // presents <= this have completed on the GPU (life_on_present fences)
uint64_t life_idle_begin();
void life_idle_end(uint64_t seq_before);
// In the destroy hooks, before anything is released: waits (bounded) for the
// last submission that referenced `handle`. `what` names it in the log.
void life_before_destroy(uint64_t handle, const char* what);
void life_device_destroyed(DeviceData* dd);

// ---- tracer (tracer.cpp) ----
bool trace_recording();
void trace_on_present(VkQueue queue, const VkPresentInfoKHR* info);
void trace_on_submit(const std::vector<VkCommandBuffer>& cmds);
void trace_flush_segment(CmdState* s);
void trace_event(CmdState* s, std::string json);
void trace_note_draw(VkCommandBuffer cmd, CmdState* s, int bind_point, const char* kind, const char* extra);
void shader_dump(uint64_t hash, const void* code, size_t size);
std::string image_ref(VkImage image);
std::string view_ref(VkImageView view);
// Records copies of the pass's attachments into readback buffers; returns
// the dump ids as a JSON array fragment for the pass's "end" event.
std::string trace_dump_pass(VkCommandBuffer cmd, CmdState* s);
// Terrain packet dump (terrain_packet_dump.inl, diagnostic only).
struct TerrainSubmission {std::vector<uint64_t> packets;uint64_t frame{};};
#if BG3RTX_DIAGNOSTICS
std::string terrain_capture_note(VkCommandBuffer cmd, CmdState* s);
void terrain_capture_pass(VkCommandBuffer cmd, CmdState* s);
void terrain_capture_secondary(VkCommandBuffer cmd, CmdState* s);
void terrain_capture_discard(VkCommandBuffer cmd);
bool terrain_capture_needs_submit_promotion(VkCommandBuffer cmd);
TerrainSubmission terrain_capture_before_submit(const std::vector<VkCommandBuffer>& cmds);
void terrain_capture_after_submit(const TerrainSubmission&, VkQueue queue, bool success);
void terrain_capture_device_destroyed(DeviceData* dd);
void terrain_capture_finish(uint64_t frame, const std::string& dir, VkQueue queue);
#else
inline std::string terrain_capture_note(VkCommandBuffer, CmdState*) { return {}; }
inline void terrain_capture_pass(VkCommandBuffer, CmdState*) {}
inline void terrain_capture_secondary(VkCommandBuffer, CmdState*) {}
inline void terrain_capture_discard(VkCommandBuffer) {}
inline bool terrain_capture_needs_submit_promotion(VkCommandBuffer) { return false; }
inline TerrainSubmission terrain_capture_before_submit(const std::vector<VkCommandBuffer>&) { return {}; }
inline void terrain_capture_after_submit(const TerrainSubmission&, VkQueue, bool) {}
inline void terrain_capture_device_destroyed(DeviceData*) {}
inline void terrain_capture_finish(uint64_t, const std::string&, VkQueue) {}
#endif
void count_pass();
// Diagnostic counters, logged with the periodic [auto] line.
enum DiagCounter { DIAG_PRESENT, DIAG_PIPELINES, DIAG_PIPELINE_US, DIAG_DESC_WRITES, DIAG_SHADER_MODULES, DIAG_SUBMIT, DIAG_BEGIN_CMD, DIAG_BEGIN_PASS, DIAG_BEGIN_RENDERING, DIAG_BIND_PIPE, DIAG_DRAW,
                   DIAG_DISPATCH, DIAG_EXEC, DIAG_DISPLAYED, DIAG_COUNT };
void diag(DiagCounter c, uint64_t n = 1);
void watchdog_start();                     // passes per frame feed the auto trigger
void dumps_on_submit(const std::vector<VkCommandBuffer>& cmds, uint64_t frame);
void dumps_finish(uint64_t frame, const std::string& capture_dir, VkQueue queue);
bool write_png_rgb(const std::string& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgb);
// Pan bursts (dump.cpp, diagnostic only). Off: Bg3HostApi::pan_capture stays a valid
// pointer that always answers false (no ABI change).
#if BG3RTX_DIAGNOSTICS
bool pan_capture(VkCommandBuffer cmd, const char* name, const Bg3Binding* resource);
void pan_on_submit(const std::vector<VkCommandBuffer>& cmds);
void pan_on_present(VkQueue queue);
void pan_pass_end(VkCommandBuffer cmd, CmdState* s);
#else
inline bool pan_capture(VkCommandBuffer, const char*, const Bg3Binding*) { return false; }
inline void pan_on_submit(const std::vector<VkCommandBuffer>&) {}
inline void pan_on_present(VkQueue) {}
inline void pan_pass_end(VkCommandBuffer, CmdState*) {}
#endif
// Offline replay pack writer (replay.cpp, diagnostics only; tools/frame_replay.md "Live writer").
// tracker.cpp's vkUpdateDescriptorSets hook sets this while it forwards the game's call: the
// replay's descriptor record (wrapping dd->vk) keeps only the layer's own material-set writes.
extern thread_local bool t_game_descriptor_update;
#if BG3RTX_DIAGNOSTICS
void replay_device_created(DeviceData* dd);   // wraps a few dd->vk entries (object and material-set records)
void replay_device_destroyed(DeviceData* dd);
void replay_note_alpha_layout(VkDescriptorSetLayout layout, VkSampler wrap, VkSampler clamp);   // alphatest.cpp
int replay_pack(VkCommandBuffer cmd, const Bg3ReplayDispatch* dispatch);   // Bg3HostApi::replay_pack (ABI 25)
void replay_on_present();
#else
inline void replay_device_created(DeviceData*) {}
inline void replay_device_destroyed(DeviceData*) {}
inline void replay_note_alpha_layout(VkDescriptorSetLayout, VkSampler, VkSampler) {}
inline void replay_on_present() {}
#endif
bool geo_copy_camera(VkCommandBuffer cmd, VkBuffer source, VkDeviceSize offset, VkBuffer destination,
                     VkDeviceSize destination_offset = 0);
VkBuffer rt_scene_pose();

// ---- Streamline DLSS-G (streamline.cpp) ----
// Instance/device creation: merge Streamline's requirements into the game's.
struct SlDeviceScratch;               // storage for the patched VkDeviceCreateInfo
void sl_instance_pre(const VkInstanceCreateInfo* in, VkInstanceCreateInfo& out);
void sl_instance_post(VkInstance instance, VkResult r);
SlDeviceScratch* sl_device_pre(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* in,
                               VkDeviceCreateInfo& out);
void sl_device_post(SlDeviceScratch* s, DeviceData* dd, VkResult r);
void sl_device_destroy(DeviceData* dd);
void sl_device_ready(DeviceData* dd, bool at_swapchain = false);   // a game call on the finished device
// Swapchain/present entry points Streamline hooks. Each returns false when
// Streamline is not active, and the caller goes straight to the next layer.
bool sl_create_swapchain(VkDevice d, const VkSwapchainCreateInfoKHR* ci, const VkAllocationCallbacks* a,
                         VkSwapchainKHR* out, VkResult* r);
bool sl_destroy_swapchain(VkDevice d, VkSwapchainKHR sc, const VkAllocationCallbacks* a);
bool sl_get_swapchain_images(VkDevice d, VkSwapchainKHR sc, uint32_t* n, VkImage* imgs, VkResult* r);
bool sl_acquire(VkDevice d, VkSwapchainKHR sc, uint64_t timeout, VkSemaphore s, VkFence f, uint32_t* idx,
                VkResult* r);
bool sl_device_wait_idle(VkDevice d, VkResult* r);
bool sl_create_surface(VkInstance i, const VkWin32SurfaceCreateInfoKHR* ci, const VkAllocationCallbacks* a,
                       VkSurfaceKHR* out, VkResult* r);
bool sl_destroy_surface(VkInstance i, VkSurfaceKHR s, const VkAllocationCallbacks* a);
void sl_note_surface(VkSurfaceKHR surface, HWND hwnd);   // hwnd null: destroyed
VkResult sl_present(DeviceData* dd, VkQueue queue, const VkPresentInfoKHR* info);
// Observers fed by the command buffer hooks.
void sl_note_draw(VkCommandBuffer cmd, CmdState* s);           // first draw of a pass
void sl_note_pass_end(VkCommandBuffer cmd, CmdState* s);
void sl_note_barrier(CmdState* s, VkImage image, VkImageLayout layout);
void sl_note_event_barrier(CmdState* s, VkImage image, VkImageLayout layout);
void sl_note_submit(const std::vector<VkCommandBuffer>& cmds);
void sl_preserve_inputs(VkCommandBuffer cmd, const Bg3Binding& motion, const Bg3Binding& depth);
VkImageLayout sl_input_layout(VkCommandBuffer cmd, VkImage image, VkImageLayout fallback);
void sl_note_image_destroyed(VkImage image);
void sl_note_view_destroyed(VkImageView view);   // a tag's view: DLSS-G may still read it
bool sl_watched(VkImage image);

// ---- hot reload + shader compiler (hotreload.cpp, shaders.cpp) ----
void core_init(DeviceData* dd);
void core_shutdown();
void core_poll(VkQueue present_queue); // at present: reload if the DLL changed
void core_on_frame(uint64_t frame);
void core_on_pass_end(VkCommandBuffer cmd, const Bg3PassInfo* pass);
void core_on_pass_begin(VkCommandBuffer cmd, const Bg3PassInfo* pass);
void core_on_dispatch(VkCommandBuffer cmd, const Bg3DispatchInfo* info);
void core_on_cmd_end(VkCommandBuffer cmd);
void core_on_dispatch_pre(VkCommandBuffer cmd, const Bg3DispatchInfo* info);
// D-EM policy in the core (ABI 24): hold core_shared_lock() around these, taken before
// emissive.cpp's own mutex (the order of em_scene, called by the core). False without a core.
std::shared_lock<std::shared_mutex> core_shared_lock();
bool em_core_record(const Bg3EmRecordIn* in, Bg3EmRecordOut* out);
bool em_core_calib_update(Bg3EmCalib* cal, bool first, double ratio, uint32_t flags, uint64_t frames, double bright);
bool em_core_calib_apply(uint32_t* rec, const Bg3EmCalib* cal);
bool query_binding(VkCommandBuffer cmd, VkPipelineBindPoint bp, uint32_t set, uint32_t binding, uint32_t element,
                   Bg3Binding* out);
bool alloc_image(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage, Bg3Image* out);
bool alloc_host_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* buffer, VkDeviceMemory* memory,
                       void** mapped);
void restore_bindings(VkCommandBuffer cmd, VkPipelineBindPoint bp);
bool query_set(VkCommandBuffer cmd, VkPipelineBindPoint bp, uint32_t set, VkDescriptorSet* out_set,
               VkDescriptorSetLayout* out_layout, uint32_t* dyn, uint32_t* dyn_count);
void ngx_set_spec_hit_distance(VkImage image, VkImageView view, VkFormat format, uint32_t w, uint32_t h);
bool ngx_motion_vectors(VkImage* image, VkImageView* view, VkFormat* format, float scale[2]);
uint32_t find_memory_type(DeviceData* dd, uint32_t bits, VkMemoryPropertyFlags want);
void defer_destroy(Bg3DeferKind kind, uint64_t handle);
void defer_tick(bool flush_all);

bool shader_get(const char* file, const char* entry, const char* profile, Bg3ShaderBlob* out);
void shader_poll();

// ---- NGX: SR -> Ray Reconstruction (ngx.cpp) ----
void ngx_init();                       // hooks _nvngx.dll now or when it loads
// G-buffer discovery; with rr=1 also records the RR guide dispatch into `cmd`.
void ngx_note_pass(VkCommandBuffer cmd, const Bg3PassInfo& p, const VkImageLayout* layouts, VkImageLayout depth_layout);
void ngx_on_destroy_image(VkImage img);
void ngx_on_present();
// ABI 7: Ray Reconstruction evaluated in the last few frames (not the rr key):
// false with FSR/TAA, SR fallback, or a failed RR creation.
bool ngx_rr_active();
// RR's diffuse albedo guide (RGBA8, GENERAL) when it has this size.
bool ngx_diffuse_albedo(uint32_t w, uint32_t h, VkImageView* view);

// ---- RR at the start of post-processing (rrpost.cpp, rr_post=1) ----
struct RrPostImage {
  VkImage image;
  VkImageView view;
  VkFormat format;
  uint32_t w, h;
};
struct RrPostInputs {
  RrPostImage color;    // RR colour: the dither-resolved pre-post scene colour
  RrPostImage output;   // RR writes here (RGBA16F, output resolution)
  RrPostImage cbt;      // colour before transparency (image null when stale)
  RrPostImage sss;      // screen-space SSS guide, R16F (image null when stale)
  bool dof;             // the game's DoF composite ran this frame
  char why[96];         // why not, when rrpost_take fails
};
void rrpost_pass_end(VkCommandBuffer cmd, const Bg3PassInfo& p, const VkImageLayout* layouts);
void rrpost_dispatch_pre(VkCommandBuffer cmd, uint64_t cs);
void rrpost_dispatch_post(VkCommandBuffer cmd, uint64_t cs);   // QA exposure hold
bool rrpost_take(VkCommandBuffer cmd, uint32_t w, uint32_t h, uint32_t ow, uint32_t oh, RrPostInputs* out);
bool rrpost_final(VkCommandBuffer cmd, VkImage out_image, VkFormat out_fmt, uint32_t ow, uint32_t oh);
void rrpost_forget_image(VkImage image);   // the game destroys an image
int rrpost_trace_images(VkImage* out, int max);
float rrpost_pre_exposure();   // lighting s1b13[0] as last read back (0 if unknown)
void rrpost_on_present();

// ---- Track D: G-buffer geometry capture via transform feedback (geocapture.cpp) ----
const VkDeviceCreateInfo* geo_device_create_info(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                 VkDeviceCreateInfo* scratch);
void geo_device_created(DeviceData* dd, bool xfb_enabled);
void geo_device_destroyed(DeviceData* dd);
bool geo_patch_shader(const VkShaderModuleCreateInfo* ci, std::vector<uint32_t>& out);
void geo_note_module(VkShaderModule m, bool patched);
void geo_forget_module(VkShaderModule m);
void geo_note_pipelines(uint32_t n, const VkGraphicsPipelineCreateInfo* cis, const VkPipeline* out);
void geo_forget_pipeline(VkPipeline p);
void geo_pass_before(VkCommandBuffer cmd, CmdState* s);   // outside the pass, before the game's begin
void geo_pass_begun(VkCommandBuffer cmd, CmdState* s);    // inside, right after it
void geo_pre_draw(VkCommandBuffer cmd, CmdState* s, const DrawArgs& args);
void geo_post_draw(VkCommandBuffer cmd, CmdState* s);
bool geo_native_hair_enabled();
VkDeviceSize geo_ubo_align();   // minUniformBufferOffsetAlignment of the device
bool geo_native_tileset_enabled();
void geo_native_tileset_disable();
bool geo_native_tileset_inputs(VkPipeline,uint32_t subtype=0);
void alpha_tileset_incomplete(); // Startup-fixed XFB/soup stride88; never hot-enable 24-byte scenes.
void alpha_native_hair_missing_attributes(uint64_t fs,uint64_t vs,uint32_t flags);
void geo_pass_ending(VkCommandBuffer cmd, CmdState* s);   // inside, right before the game's end
void geo_pass_after(VkCommandBuffer cmd, CmdState* s);    // outside, right after the game's end
void geo_cmd_discard(CmdState* s);   // begin/reset/free with a pass still open: frees its EmPass
void geo_forget_buffer(VkBuffer b);
VkBufferUsageFlags geo_buffer_usage(VkBuffer b); // readonly tracked create usage; 0 if unknown
VkDeviceSize geo_buffer_size(VkBuffer b);                   // 0 if unknown
namespace native_hit {struct StaticInputs;struct TilesetInputs;}
bool geo_native_hit_inputs(VkPipeline,native_hit::StaticInputs*);
bool geo_native_tileset_material_inputs(VkPipeline,native_hit::TilesetInputs*);
bool geo_uniform_buffer_range(VkBuffer buffer,VkDeviceSize offset,VkDeviceSize bytes);
bool geo_submit_wanted();
void geo_on_submit(const std::vector<VkCommandBuffer>& cmds);
void geo_on_present(VkQueue queue);
void geo_finish(uint64_t frame, const std::string& capture_dir, VkQueue queue);
uint64_t trace_start_frame();
bool write_png(const std::wstring& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgb);  // dump.cpp

// ---- Track D phase 2: acceleration structures + ray query on the game's device (rtscene.cpp) ----
// Instance API version the layer asks for (rt=1 needs 1.2+ for the AS/ray query
// dependencies; the game asks for 1.1).
uint32_t rt_instance_api(uint32_t requested);
// `sharc`: also the SHaRC radiance cache's device features (64-bit integers and buffer
// atomics, float16, 16-bit storage) where the physical device supports them. The
// layer retries without them (rt_device_features_undo first) if the driver refuses.
const VkDeviceCreateInfo* rt_device_create_info(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                VkDeviceCreateInfo* scratch, bool sharc = true);
bool rt_device_features_added();           // the last create info has SHaRC features of ours
void rt_device_features_undo();            // restores the game's feature structs we edited
void rt_device_created(DeviceData* dd, bool enabled);
void rt_device_destroyed(DeviceData* dd);
bool rt_enabled();                         // AS + ray query are on the device
// Build the scene this frame: rt_build (after the settings layer resolves
// rt_build=auto from the RT consumers, common/settings.h) and not yet built.
bool rt_capture_wanted();
bool rt_camera_moving();                   // the camera moved in the last scene build's frame
VkBufferUsageFlags rt_xfb_usage();         // extra usage the per-frame XFB buffer needs
bool rt_alloc_device_address();            // memory for BDA buffers needs VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
void rt_before_capture(VkCommandBuffer cmd);   // outside the G-buffer pass, before it begins
// After the G-buffer pass, outside it: XFB clip positions -> camera-relative
// world in place, BLAS build or refit, TLAS build.
// The soup in `xfb`: opaque triangles from 0, alpha-tested ones (rt_alpha=1)
// from alpha_base; `stride` bytes per vertex (16 position, 24 with the UV).
struct RtCapture {
  VkBuffer xfb;
  VkDeviceSize opaque_bytes, alpha_base, alpha_bytes;
  uint32_t stride;
  bool alpha_region;         // the capture reserved an alpha region (rt_alpha=1)
  VkBuffer ubo;
  VkDeviceSize ubo_offset;
  uint64_t signature;
  uint32_t draws, alpha_draws;
  bool split;                // rt_bvh_split: the capture holds only the dynamic draws (bvhsplit.cpp)
  VkDeviceSize xfb_size;
  // rt_cascade_casters (set by rt_after_capture, split builds only): last frame's cascade-only
  // soup, already camera-relative world (its own frame's), and the TLAS translation that
  // moves it into this frame's camera-relative space. cascade_tris 0: none.
  VkBuffer cascade_xfb;
  uint32_t cascade_tris, cascade_stride;
  float cascade_t[3];
  uint64_t cascade_sig;   // the cascade draw list (same: the BLAS may be refitted)
  uint64_t cascade_frame = 0;   // the frame whose converted soup it is (geo_cascade_soup; replay pack)
  sun_coverage::NativeEpoch native_epoch;
  VkBuffer native_frame_buffer = VK_NULL_HANDLE;
  VkDeviceSize native_frame_offset = 0;
};
void rt_after_capture(VkCommandBuffer cmd, const RtCapture& cap);
// ---- rt_cascade_casters (docs/TRACK_D_RT.md 6.7 item 2, common/cascade_casters.h) ----
// geocapture.cpp captures one sun cascade pass (minus the camera pass's draws) and
// converts it in place after the pass; the next frame's split build adds it as one
// instance (mask 0x02). The soup converted in `frame`: 0 found, 1 none, 2 its command
// buffer not (yet) submitted (the geo_cascade_note_build reasons).
struct CascadeSoup {
  VkBuffer xfb; uint32_t tris, stride; bool has_origin; float origin[3]; uint64_t sig;
  uint64_t record = 0;
  VkCommandBuffer producer = VK_NULL_HANDLE;
};
struct AlphaSnapshot;
struct SunCascadeDraw {
  uint32_t first_triangle=0, triangles=0;
  uint64_t fragment_shader=0;
  uint32_t material=0; // 0 opaque; 1+ snapshot index; UINT32_MAX unknown
};
struct SunCascadeCapture {
  CascadeSoup soup{};
  std::vector<SunCascadeDraw> draws;
  std::vector<AlphaSnapshot> materials;
  VkImage depth_image = VK_NULL_HANDLE;
  sun_coverage::NativeEpoch native_epoch;
  VkBuffer native_frame_buffer = VK_NULL_HANDLE;
  VkDeviceSize native_frame_offset = 0;
  VkBuffer camera_buffer = VK_NULL_HANDLE;
  VkDeviceSize camera_offset = 0;
  float camera[56] = {};
  bool camera_mapped=false;
  float matrix[4][4] = {};
  uint32_t layer = 0;
  bool geometry_complete = false, materials_complete = false;
  bool finite_native_clip = false, native_raster_valid=false, fresh_depth=false;
  VkViewport native_viewport{};
};
// Exact native Frame packet, independent of presentation-frame tag. Pending
// conversions are returned for private-build scheduling, never as submitted proof.
// Four separate native draw domains; never infer near-layer membership from far.
bool geo_sun_cascades(const sun_coverage::NativeEpoch& epoch, std::array<SunCascadeCapture,4>* out);
bool geo_sun_captures_owned(std::span<const SunCascadeCapture> captures);
bool geo_native_frame_epoch(VkCommandBuffer cmd, VkPipelineBindPoint bind,
                             sun_coverage::NativeEpoch* epoch, VkBuffer* buffer, VkDeviceSize* offset);
int geo_cascade_soup(uint64_t frame, CascadeSoup* out);
// Why a build has or lacks the cascade instance (the [cascade] log line): 0 added, 1 no soup
// of the previous frame, 2 its command buffer not submitted, 3 no camera of that frame,
// 4 legacy (non-split) build, 5 BLAS allocation failed.
void geo_cascade_note_build(int reason, uint32_t tris);
// clip -> camera-relative world with the cascade's orthographic matrix (its camera-layout
// UBO), in place (shaders/rt/xfb_to_world_ortho.hlsl). Restores the game's compute bindings.
bool rt_cascade_convert(VkCommandBuffer cmd, VkBuffer xfb, uint32_t tris, uint32_t stride, VkBuffer ubo,
                        VkDeviceSize ubo_offset);
// The camera position (camera UBO byte 208, the origin of camera-relative space) of the
// scene build of `frame`, from the last few builds.
bool rt_build_camera(uint64_t frame, float pos[3]);
// Scene snapshots: each build writes one slot (its own TLAS for the split, its
// InstInfo region of the bvhsplit ring, its alpha set and record region), and a
// slot is reused only once the GPU finished every frame it was handed out in
// (life_gpu_done). The RT passes get a TLAS and the set of the same slot.
constexpr uint32_t kSceneSlots = 8;
bool rt_scene(VkAccelerationStructureKHR* tlas, uint64_t* built_frame);
// ABI 8: the latest scene's TLAS and its own set 1, as one versioned pair.
bool rt_scene_set(VkAccelerationStructureKHR* tlas, VkDescriptorSet* set, uint64_t* built_frame);
// Oldest build frame among the slots the GPU may still read (UINT64_MAX: none).
// A BVH range freed in frame F is released only once this is > F.
uint64_t rt_scene_oldest_live();
void rt_log_state(const char* prefix);     // the slots, for a device-loss report
void rt_on_present();
// Shared by the legacy build and the split (bvhsplit.cpp).
VkDeviceAddress rt_buffer_address(VkBuffer b);
VkDeviceSize rt_scratch_alignment();
// The xfb_to_world conversion of the capture's opaque and alpha regions (in place).
bool rt_convert(VkCommandBuffer cmd, const RtCapture& cap);
// Reads `n` bytes of a game buffer at `offset` from its host mapping (false if not mapped).
bool read_mapped(VkBuffer b, VkDeviceSize offset, void* dst, size_t n);
const uint8_t* mapped_ptr(VkBuffer b, VkDeviceSize offset);   // nullptr if not host-mapped

// ---- Track D BVH split (bvhsplit.cpp, docs/TRACK_D_RT.md 11) ----
// Static instanced meshes get one object-space BLAS per unique mesh, built once
// from the first transform-feedback capture of a draw (inverse instance
// matrix), compacted and cached; every frame the TLAS holds one instance per
// draw instance, with the matrix the game's instance stream gives the draw.
// Everything else (skinned, wind, per-draw matrices, terrain) is captured as
// before and gets a per-draw BLAS, refit while its triangle count holds.
struct InstPipe {
  bool ok = false;              // class-A position path and instance-rate float4 matrix rows
  uint8_t row_binding[3] = {};
  uint32_t row_offset[3] = {};
  uint32_t row_stride[3] = {};  // binding strides (0: dynamic, from the bind call)
  uint64_t vtx_sig = 0;         // VS + per-vertex input layout
  uint32_t vtx_bindings = 0;    // mask of the per-vertex bindings the attributes read
  bool uv_ok = false;           // the captured UV output is a copy of a per-vertex input
  uint8_t pos_binding = 0xff;   // the position attribute's binding (0xff: none) and offset (pool material join)
  uint32_t pos_offset = 0;
};
void bvh_device_created(DeviceData* dd);
void bvh_device_destroyed(DeviceData* dd);
bool bvh_split_wanted();                       // rt_bvh_split=1 and the split is usable
void bvh_begin_capture(VkCommandBuffer cmd);
// Per draw of the RT capture (split mode). `token` identifies the draw in later calls.
struct BvhDraw {
  const InstPipe* inst;         // nullptr: not instanceable
  uint64_t vs;
  uint32_t pipe_id;
  bool alpha;                   // alpha-tested (its FS is in the table, UV captured)
  DrawArgs args;
  const CmdState* s;
};
enum BvhVerdict { BVH_CAPTURE = 0, BVH_CACHED = 1, BVH_CACHED_AUDIT = 2 };
BvhVerdict bvh_classify(VkCommandBuffer cmd, const BvhDraw& d, uint32_t* token, uint32_t* mesh_tris);
// The draw's alpha record (cached draws; UINT32_MAX: none, made opaque).
void bvh_set_record(uint32_t token, uint32_t rec);
// The mesh pool buffer, its size and replacement epoch (hit-material pool basis jobs).
bool bvh_pool_target(VkBuffer* buffer, VkDeviceSize* bytes, uint64_t* epoch);
// The draw was captured into the soup at `offset` (`prims` triangles, all instances).
void bvh_captured(uint32_t token, VkDeviceSize offset, uint32_t prims, bool alpha, uint32_t rec, uint32_t materialRecord=UINT32_MAX);
// Uncaptured draw that is not instanceable (counted only).
void bvh_note_skipped(uint32_t token);
// rt_primary_fade_class: the draw's camera-fade cut is enabled (InstInfo kInfoCameraFade).
void bvh_set_camera_fade(uint32_t token);
// After the pass: object-space extraction, BLAS builds/refits, compaction, TLAS.
// `stamp(i)` writes the timing timestamps 1..3.
// Immutable instance input of a retired-slot build, before its optional OLD
// cascade instance. Late sun clones GPU-copy this range, so submit-time native
// transform fixups are preserved. The owning SceneSlot retains every BLAS ref.
struct BvhSunBase {
  VkBuffer instances = VK_NULL_HANDLE;
  VkDeviceSize offset = 0, buffer_bytes = 0;
  uint32_t count = 0;
  uint64_t record = 0;
  bool transfer_source = false; // Creation usage of this retained buffer, not the current ring.
};
struct BvhResult {
  VkAccelerationStructureKHR tlas;
  uint32_t instances, static_instances, dynamic_draws, cached_draws, new_meshes;
  uint64_t static_tris, dynamic_tris;
  VkBuffer info; VkDeviceSize info_off, info_bytes;   // InstInfo of this TLAS (set 1 binding 7)
  VkBuffer pool; VkDeviceSize pool_bytes;             // object-space vertices (binding 8)
  uint64_t rec;                                       // build record of the command buffer (bvh_rec_state)
  BvhSunBase sun_base;
};
struct BvhSunCloneResult { VkAccelerationStructureKHR tlas = VK_NULL_HANDLE; uint64_t record = 0; };
struct SunBaseCapture {
  BvhSunBase base;
  uint32_t slot = 0;
  VkCommandBuffer producer = VK_NULL_HANDLE;
  sun_coverage::NativeEpoch native_epoch;
  VkBuffer native_frame_buffer = VK_NULL_HANDLE;
  VkDeviceSize native_frame_offset = 0;
  VkBuffer camera_buffer = VK_NULL_HANDLE;
  VkDeviceSize camera_offset = 0;
  float camera[56] = {};
};
bool rt_sun_base_capture(VkAccelerationStructureKHR selected_tlas, VkDescriptorSet selected_alpha, SunBaseCapture* out);
bool rt_sun_scene(VkCommandBuffer cmd,VkAccelerationStructureKHR selected_tlas,VkDescriptorSet selected_alpha,
                   Bg3SunSceneInfo* out);
// Exact native Sun-mask source packets. Unmapped source UBOs remain valid GPU
// sources, not invented CPU values; a consumer must copy/validate them on GPU.
struct SunReceiverPacket {
  Bg3Binding source{};
  std::array<uint8_t,544> bytes{};
  uint32_t size=0;
  bool mapped=false;
};
struct SunReceiverCapture {
  VkCommandBuffer producer=VK_NULL_HANDLE;
  uint64_t record=0;
  sun_coverage::NativeEpoch epoch;
  SunReceiverPacket frame, camera, sun, auxiliary, ray_basis;
  Bg3Binding csm{}, auxiliary_near{}, auxiliary_far{};
  bool sources_valid=false;
};
void sun_note_native_draw(VkCommandBuffer cmd, const CmdState* state);
bool sun_native_receiver(const sun_coverage::NativeEpoch& epoch, SunReceiverCapture* out);
struct SunSubmitBatch {
  std::vector<VkSubmitInfo> legacy;
  std::vector<VkSubmitInfo2> modern;
  std::vector<std::vector<VkCommandBuffer>> legacy_commands;
  std::vector<std::vector<VkCommandBufferSubmitInfo>> modern_commands;
  std::vector<std::vector<VkSemaphoreSubmitInfo>> modern_signals;
  std::vector<VkCommandBuffer> commands;
  bool modified=false;
};
void sun_prepare_submit(DeviceData* dd,VkQueue queue,uint32_t count,const VkSubmitInfo* submits,SunSubmitBatch* out);
void sun_prepare_submit2(DeviceData* dd,VkQueue queue,uint32_t count,const VkSubmitInfo2* submits,SunSubmitBatch* out);
void sun_device_destroyed(DeviceData* dd);
// Records a NEW per-slot sun-only BLAS/TLAS after the native cascade conversion.
// Never rewrites the camera TLAS or reruns bvh_build's mutable draw collection.
// Caller owns verified command dependencies; no coverage certificate is implied.
bool bvh_build_sun_clone(VkCommandBuffer cmd, uint32_t slot, const BvhSunBase& base,
                         std::span<const SunCascadeCapture> cascades, const float camera_origin[3],
                         VkPipeline validate_pipeline, VkPipelineLayout validate_layout, VkBuffer stamp, VkBuffer parameters,
                         BvhSunCloneResult* out);
// `slot` (< kSceneSlots, retired on the GPU): the ring region and TLAS it writes.
bool bvh_build(VkCommandBuffer cmd, const RtCapture& cap, void (*stamp)(VkCommandBuffer, uint32_t), uint32_t slot,
               BvhResult* out);
VkAccelerationStructureKHR bvh_omm_scene(uint32_t slot,VkAccelerationStructureKHR reference);
bool bvh_omm_use(VkCommandBuffer cmd,uint32_t slot,VkAccelerationStructureKHR selected,uint64_t* identity);
bool rt_omm_scene_reference(VkCommandBuffer cmd,VkAccelerationStructureKHR selected,VkAccelerationStructureKHR* reference,uint64_t* identity);
bool rt_hit_material_use(VkCommandBuffer,VkAccelerationStructureKHR,VkDescriptorSet,uint64_t,VkDescriptorSet*);
bool bvh_omm_commit(VkCommandBuffer cmd,uint32_t slot,VkBuffer stamp,VkDeviceSize base,uint32_t generation,const uint32_t* mapped);
void bvh_on_submit(const std::vector<VkCommandBuffer>& cmds);
// Build records: work of ours recorded into a game command buffer counts only once
// that buffer is submitted. The game can drop a recorded buffer (vkBeginCommandBuffer
// again, or vkFreeCommandBuffers) without a submit; bvh_cmd_reset marks its records
// dropped. States: 0 pending, 1 submitted, 2 dropped, -1 unknown (never trusted).
uint64_t bvh_rec_register(VkCommandBuffer cmd);
int bvh_rec_state(uint64_t id);
bool bvh_rec_matches_command(uint64_t id, VkCommandBuffer command);
void bvh_cmd_reset(VkCommandBuffer cmd);
void bvh_log_state(const char* prefix);   // device-loss / stall report (devfault.cpp)
VkResult life_poll_fence(DeviceData* dd);  // our latest present fence (0-timeout); INITIALIZATION_FAILED: device gone
size_t life_pending_fences();
void fault_poll_start(DeviceData* dd);    // devfault.cpp: active device-loss detection
void bvh_on_present();
void bvh_forget_buffer(VkBuffer b);
std::string bvh_describe_address(VkDeviceAddress a);   // device-fault reports ("" if not the split's)
// Replay pack (layer/replay.cpp): a scene ring region by its InstInfo descriptor. rt_replay_region
// takes rtscene's lock and calls bvh_replay_region.
struct BvhReplayRegion {
  VkBuffer ring = VK_NULL_HANDLE;
  VkDeviceSize ring_bytes = 0, instances_offset = 0;
  uint32_t count = 0;
  std::vector<uint32_t> info;   // count x (base, meta, rec, tris)
  // rt_cascade_casters: the region's cascade instance (InstInfo tris 0) and the soup its BLAS was built
  // from (geo_cascade_soup of casc_frame: its buffer, triangles, stride, draw list). casc_tris 0: none.
  uint32_t casc_instance = 0, casc_tris = 0, casc_stride = 0;
  VkBuffer casc_xfb = VK_NULL_HANDLE;
  uint64_t casc_frame = 0, casc_sig = 0;
};
bool bvh_replay_region(VkBuffer info, VkDeviceSize info_offset, BvhReplayRegion* out);
bool rt_replay_region(VkBuffer info, VkDeviceSize info_offset, BvhReplayRegion* out);
void bvh_buffer_written(VkBuffer b, VkDeviceSize offset, VkDeviceSize size);

// ---- Track D D-EM: emissive geometry (emissive.cpp, docs/TRACK_D_EMISSIVE.md section 13) ----
// The game redraws its emissive meshes into the effects target (B10G11R11 + the
// G-buffer depth, before the tiled lighting). Those draws are captured (XFB) with a per-draw
// record (radiance, mask texture); at
// the first tiled-lighting dispatch the emissive TLAS and the light list are built.
void em_device_created(DeviceData* dd);
void em_device_destroyed(DeviceData* dd);
bool em_wanted();                                    // rt_emissive=1, RT and the split available
bool em_is_fx_pass(const Bg3PassInfo& p, uint32_t gbuf_w, uint32_t gbuf_h);
// Outside an effects pass, before it begins: notes the target, times it; false: no capture.
bool em_pass_begin(VkCommandBuffer cmd, const CmdState* s, int* ts);   // ts: timestamp pair (-1 none)
// Per draw of an effects pass. Not an emissive material draw (sky, clouds, decals...),
// a cached mesh (its instances come from the instance stream) or no room: false.
// Otherwise the draw is to be captured into the soup at [*off, *off + *bytes).
struct EmDrawIn {
  bool xfb;                  // the pipeline's VS is patched and last before rasterisation
  bool triangles;
  VkPrimitiveTopology topo;
  bool restart;
  const InstPipe* inst;      // class-A instancing layout, or nullptr
  uint32_t stride;           // XFB bytes per vertex
  uint8_t uv_match = 0;      // UvMatch of the VS/FS pair (spirv_xfb.h): 1 ok, 2/3 the wrong UV captured
  int uv = -1;               // captured VS output location
  int uv_fs = -1;            // the FS's float2 input location
  uint32_t uv_vs_comps = 0;  // components the VS writes at uv_fs
};
void geo_uv_summary();       // logs the UV-slot verdicts over all pipelines (geocapture.cpp)
void em_note_draw(const CmdState* s);   // an effects-pass draw while capture is off (timing only)
bool em_draw(VkCommandBuffer cmd, const CmdState* s, const DrawArgs& a, const EmDrawIn& in, VkBuffer* soup,
             VkDeviceSize* off, VkDeviceSize* bytes, uint32_t* prims);
// After the pass, outside it: the captured ranges (offset, triangles) to camera-relative
// world with the pass's camera UBO.
void em_pass_done(VkCommandBuffer cmd, const CmdState* s, VkBuffer ubo, VkDeviceSize ubo_off, uint32_t stride,
                  const std::vector<std::pair<VkDeviceSize, uint32_t>>& ranges, int ts);
// A command buffer is (re)begun: its unsubmitted draws are dropped (cmd.cpp).
void em_forget_cmd(VkCommandBuffer cmd);
// Right after the game's vkQueueSubmit(2) returned, the queue lock held (layer.cpp): if the
// batch holds effects passes, builds the emissive scene from their draws in a command
// buffer of ours and submits it on the same queue.
void em_after_submit(DeviceData* dd, VkQueue queue, const std::vector<VkCommandBuffer>& cmds);
void em_on_present();
// ABI 9: the emissive scene for the core (host_api.h Bg3HostApi::rt_emissive).
bool em_scene(VkCommandBuffer cmd, VkDescriptorSetLayout* layout, VkDescriptorSet* set, Bg3EmissiveInfo* info);

// ---- M5.1 water geometry (water.cpp, docs/PLAN_M5_WATER.md, rt_water_geometry) ----
// The forward water draws (common/water_policy.h, run by the core) are captured through the
// D-EM pass hook (geocapture.cpp) into a soup, a BLAS and a TLAS of their own per game
// submission. Data only: the water TLAS is bound nowhere (no image change).
struct WaterRange { VkBuffer soup; VkDeviceSize off, bytes; uint32_t prims; };
void water_device_created(DeviceData* dd);   // stores the device; allocates nothing
void water_device_destroyed(DeviceData* dd);
bool water_wanted();                          // rt_water_geometry=1 and RT on the device
bool water_is_pass(const Bg3PassInfo& p, uint32_t gbuf_w, uint32_t gbuf_h);   // the forward/effects signature
bool water_pass_begin(VkCommandBuffer cmd);   // outside the pass; false: no water capture in it
// Per draw of such a pass: 0 not a water draw, 1 a water draw counted but not captured,
// 2 captured: bind XFB to *out (soup, off, bytes) for this draw. depth_write: the pipeline's
// (1, 0, -1 dynamic/unknown).
// *swapped: M5.3 (rt_water_compose) bound the composition variant of the draw's pipeline in place
// of the game's (water_unswap after the draw rebinds the game's).
int water_draw(VkCommandBuffer cmd, const CmdState* s, const DrawArgs& a, const EmDrawIn& in, int depth_write,
               WaterRange* out, bool* swapped);
void water_unswap(VkCommandBuffer cmd, const CmdState* s);
// M5.3 composition variants (common/water_fs_patch.h), from the tracker's creation hooks: an
// audited water FS module is patched (rt_water_compose nonzero when it is created), and each
// pipeline using it gets a variant pipeline with the patched FS (same layout and state).
void water_note_module(VkShaderModule m, uint64_t hash, const uint32_t* code, size_t bytes);
void water_note_pipelines(VkDevice device, VkPipelineCache cache, uint32_t n, const VkGraphicsPipelineCreateInfo* cis,
                          const VkPipeline* out);
void water_forget_pipeline(VkPipeline p);
// After the pass, outside it: the captured ranges to camera-relative world (pass camera UBO).
void water_pass_done(VkCommandBuffer cmd, VkBuffer ubo, VkDeviceSize ubo_off, uint32_t stride,
                     const std::vector<WaterRange>& ranges, uint32_t pass_draws, uint32_t water_draws);
void water_forget_cmd(VkCommandBuffer cmd);   // re-begun command buffer: its draws go (cmd.cpp)
// Right after the game's submit, the queue lock held (layer.cpp): builds this batch's water scene.
void water_after_submit(DeviceData* dd, VkQueue queue, const std::vector<VkCommandBuffer>& cmds);
void water_on_present();                      // harvests builds, logs "[water]" every rt_timing_frames

// ---- Track D phase 3: alpha-tested geometry in the ray queries (alphatest.cpp) ----
// One line of shaders/rt/alpha_fs.txt (scripts/alpha_table.py).
struct AlphaFs {
  uint8_t kind;              // 1 virtual texture, 2 plain texture, 3 native scalp hard coverage
  int8_t uv;                 // FS input location of the texture coordinate
  float cut;                 // discard below this alpha (the strictest constant test on it)
  float cut_first;           // the shader's first test (an early-out where lower; rt_alpha_cut=0)
  uint64_t hash;             // the fragment shader
  uint8_t ubo_set, ubo_bind; // material UBO
  bool has_ubo, has_scale;
  uint32_t p0, idx, scale;   // UBO offsets: uv transform vec4[2], tile-set index (vt); uv scale vec2 (tex)
  uint8_t ssbo_set, ssbo_bind;   // tile-set constants (vt)
  uint8_t tex_set, tex_bind;     // indirection (vt) or the texture (tex)
  uint8_t cache_set, cache_bind; // tile cache array (vt)
  uint8_t channel;           // vt: tile-set column of the albedo layer; tex: component holding alpha
  float k;                   // tex: constant uv scale
};
// Per alpha-tested draw, as read by shaders/rt/rt_common.hlsli (208 bytes).
struct AlphaRec {
  uint32_t first_tri, tris, kind, tex;   // triangles in the alpha geometry; kind; texture slot
  uint32_t cache, channel;               // tile cache slot; column / component
  float cut, cut_first;                  // effective cut-off; the first test's (rt_alpha_cut=0)
  float scale[2];
  uint32_t fs_lo, fs_hi;                 // fragment shader hash (diagnostics)
  float p0[4], p1[4];                    // vt: uv transform; scalp: p0.xy edge/root
  float t[32];                           // vt: the SSBO entry, two row-major mat4
};
static_assert(sizeof(AlphaRec) == 208, "AlphaRec must match rt_common.hlsli");
bool alpha_native_hair_record(uint32_t index,AlphaRec* out);
bool alpha_native_hair_bounds(uint32_t index,uint32_t pool_offset);
bool alpha_hair_build_status(uint32_t slot,VkBuffer* buffer,VkDeviceSize* offset);
// Captured material plus actual views; descriptor indices are local to a frame.
struct NativeTilesetSampler;
struct AlphaSnapshot {
  std::shared_ptr<NativeTilesetSampler> native_sampler;
  AlphaRec rec{};
  VkImageView tex = VK_NULL_HANDLE, cache = VK_NULL_HANDLE;
  VkImageLayout tex_layout{}, cache_layout{};
  // Identity of each view when snapshotted (tracker image id, 0 = unknown): the game may
  // destroy a view during a hold and the driver reuse its handle for another image.
  uint32_t tex_id = 0, cache_id = 0;
};
// Canonical material capture without modifying camera descriptor indices.
// Caller retains source resources and assigns descriptor-local indices.
bool alpha_capture_source(VkCommandBuffer cmd,const AlphaFs& fs,uint32_t first_tri,uint32_t tris,AlphaSnapshot* out);
bool alpha_snapshot(uint32_t index, AlphaSnapshot* out);
bool alpha_fine_mask(uint32_t index, AlphaRec* rec, VkImageView* view, uint32_t* width, uint32_t* height);   // rt_omm_dyn
uint32_t alpha_restore(const AlphaSnapshot& saved);   // UINT32_MAX on failure
void alpha_device_created(DeviceData* dd);
void alpha_device_destroyed(DeviceData* dd);
bool alpha_lookup_native_tileset_shadow(uint64_t fs_hash,AlphaFs* out); // current-Sun per-draw material path only
bool alpha_lookup(uint64_t fs_hash, AlphaFs* out);   // copied: the table may be reloaded
// rt_primary_fade_class (diagnostics only, shaders/rt/camera_fade_fs.txt): true if `fs` has
// BG3's camera-occlusion capsule fade; *enabled: this draw's flag enables the cut. The first
// enabled draw of a capture snapshots the capsule (frame UBO set 0 binding 6) for the stamp.
bool camera_fade_draw(VkCommandBuffer cmd, uint64_t fs, uint64_t tris, bool* enabled);
void camera_fade_marked(uint32_t instances, uint64_t tris);   // TLAS instances given kInfoCameraFade
void alpha_begin_capture();
// `rec_index` (optional) receives the record's index (split mode: referenced per TLAS instance).
bool alpha_add_draw(VkCommandBuffer cmd, const AlphaFs& a, uint32_t first_tri, uint32_t tris,
                    uint32_t* rec_index = nullptr);
uint32_t alpha_record_count();
// Outside the pass: uploads the capture's records and writes its descriptor set.
//   legacy (one BLAS, 2 geometries): binding 1 the alpha soup from alpha_base, binding 6
//     geometry 0's soup at offset 0 (geometric normals of hits);
//   split: bindings 1 and 6 cover the whole soup (per-instance offsets in InstInfo).
// Binding 7: the TLAS's per-instance InstInfo; binding 8: the split's mesh pool.
struct AlphaBindings {
  VkBuffer soup;
  VkDeviceSize alpha_base, alpha_bytes, opaque_bytes, soup_size;
  uint32_t stride;
  bool split;
  VkBuffer info;
  VkDeviceSize info_off, info_bytes;
  VkBuffer pool;
  VkDeviceSize pool_bytes;
  VkBuffer camera = VK_NULL_HANDLE;
  VkDeviceSize camera_offset = 0;
  VkAccelerationStructureKHR reference_tlas = VK_NULL_HANDLE;
};
// Writes set `slot` (< kSceneSlots, retired on the GPU) and its record region;
// returns that set (VK_NULL_HANDLE when alpha testing is off).
VkDescriptorSet alpha_commit(VkCommandBuffer cmd, const AlphaBindings& b, uint32_t slot);
bool alpha_omm_commit(VkCommandBuffer cmd,uint32_t slot);
struct AlphaSunClone {
  VkDescriptorSet set = VK_NULL_HANDLE;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  VkBuffer stamp = VK_NULL_HANDLE;
  VkBuffer parameters=VK_NULL_HANDLE;
  uint32_t expected_generation = 0;
};
// Retired-slot private descriptors. Records a GPU copy of the ENTIRE original
// stamp (including Hair-invalid byte12 and camera256), then clears Sun16/20.
// CPU initialization leaves the clone unready if its private work is not submitted.
bool alpha_prepare_sun_clone(VkCommandBuffer private_cmd, uint32_t slot,
                             VkDescriptorSet selected_alpha, std::span<const SunCascadeCapture> cascades, AlphaSunClone* out);
// Sun has no micromaps: reference10 is its OWN TLAS. Optional source epoch11
// is supplied by the combined OMM path; a valid zero dummy is used otherwise.
bool alpha_sun_bind_scene(uint32_t slot,VkAccelerationStructureKHR sun_tlas,
                          VkBuffer epochs=VK_NULL_HANDLE,VkDeviceSize epoch_bytes=0);
// The legacy TLAS's InstInfo (one instance, the 2-geometry soup BLAS).
bool alpha_legacy_info(VkCommandBuffer cmd, uint32_t stride, VkBuffer* buf, VkDeviceSize* size);
bool rt_alpha_set(VkDescriptorSetLayout* layout, VkDescriptorSet* set);
// Query-owned clone of the exact main AlphaDSL; caller retires its pool.
bool alpha_clone_hit_set(VkDescriptorSet source,VkDescriptorPool* pool,VkDescriptorSet* set,std::span<const VkDescriptorImageInfo> tables={},uint32_t* firstTable=nullptr);
uint32_t alpha_hit_table_free(VkDescriptorSet source);

// ---- device-fault diagnostics (devfault.cpp, VK_EXT_device_fault) ----
const VkDeviceCreateInfo* fault_device_create_info(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                   VkDeviceCreateInfo* scratch);
void fault_device_created(DeviceData* dd, bool enabled);
// VK_EXT_debug_utils + a messenger for VK_EXT_device_address_binding_report (the
// driver's bindings, the game's included, to name a fault address).
const VkInstanceCreateInfo* fault_instance_create_info(PFN_vkGetInstanceProcAddr next, const VkInstanceCreateInfo* ci,
                                                       VkInstanceCreateInfo* scratch);
void fault_instance_created(VkInstance inst, PFN_vkGetInstanceProcAddr next);
void fault_instance_destroyed(VkInstance inst);
// Device-address ranges of our buffers, named in a fault report (live or freed).
void fault_track(const char* what, VkDeviceAddress addr, VkDeviceSize size);
void fault_untrack(VkDeviceAddress addr);
// A submit's or present's result: VK_ERROR_DEVICE_LOST logs the fault once.
void fault_on_result(DeviceData* dd, VkResult r, const char* where);
// VK_NV_device_diagnostic_checkpoints markers (no-ops without the extension).
enum CheckpointSite : uint32_t {
  kCkCmdBegin = 1, kCkPassBeginPre, kCkPassBeginPost, kCkPassEndPre, kCkPassEndPost, kCkDispPrePre, kCkDispPrePost,
  kCkDispPre, kCkDispPost, kCkCmdEndPre, kCkCmdEndPost,
  kCkBvhStart = 20, kCkBvhConverted, kCkBvhBlas, kCkBvhTlas, kCkSceneCommitted, kCkLegacyStart, kCkLegacyTlas,
};
void fault_checkpoint(VkCommandBuffer cmd, uint32_t site, uint32_t arg);
// ---- Nsight Aftermath GPU crash dumps (aftermath.cpp, aftermath=1; optional SDK) ----
void aftermath_pre_device();   // before the driver's vkCreateDevice
const VkDeviceCreateInfo* aftermath_device_create_info(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                       VkDeviceCreateInfo* scratch);   // + VK_NV_device_diagnostics_config
// A SPIR-V module the device compiles: its Aftermath hash into the shader map
// (`saved_path`: where the bytes already are; nullptr: written by Aftermath's side).
void aftermath_note_spirv(const char* what, const void* code, size_t size, const char* saved_path);
void aftermath_wait();         // after a device loss: up to 10 s for the dump
bool aftermath_enabled();
void fault_note_queue(VkQueue q);   // a queue the game submits to (checkpoints are per queue)

uint64_t frame_index();
void* persist(const char* key, size_t size);

}  // namespace bg3
