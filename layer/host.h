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
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/host_api.h"

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
float cfg_float(const char* key, float fallback);

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
struct ViewRec { VkImage image; VkFormat format; VkImageSubresourceRange range; };
struct PipelineRec {
  uint32_t id;
  VkPipelineBindPoint bind_point;
  uint64_t vs, fs, cs, other;          // SPIR-V hashes per stage (0 if absent)
  VkRenderPass render_pass;
  uint32_t subpass;
};
struct RenderPassRec {
  std::vector<VkFormat> formats;
  std::vector<VkImageLayout> final_layouts;
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
};

inline uint64_t desc_key(uint32_t binding, uint32_t element) { return (uint64_t(binding) << 32) | element; }
inline bool is_dynamic_type(VkDescriptorType t) {
  return t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

struct Tracker {
  std::shared_mutex mu;
  std::unordered_map<VkShaderModule, uint64_t> modules;
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
  bool in_pass = false;
  Bg3PassInfo pass{};
  VkImageLayout color_layouts[8] = {};  // layout each attachment is left in when the pass ends
  VkImageLayout depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  uint32_t pass_ordinal = 0;
  std::vector<uint32_t> dumps;         // image dumps recorded into this buffer
  VkPipeline pipe[2] = {};             // graphics, compute
  uint32_t pipe_id[2] = {};
  uint64_t vs = 0, fs = 0, cs = 0;     // stage hashes of the bound pipelines
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
  bool sl_cam_valid = false;
  uint8_t sl_cam[2][256] = {};
  std::vector<DescEntry> push[2];
  uint32_t push_set[2] = {UINT32_MAX, UINT32_MAX};
  uint32_t dispatches_since_pass = 0, indirect_since_pass = 0;
  uint64_t last_cs = 0;
  bool tracing = false;                // began while a trace was recording
  uint64_t recording = 0;              // unique per vkBeginCommandBuffer (rrpost chains)
  bool game_alloc = false;             // allocated through this layer (not by Streamline below it)
  std::vector<std::string> events;     // pre-rendered JSON objects, one per event
  // segment aggregation for the trace
  std::string seg_key;
  uint32_t seg_count = 0;
  std::string seg_json;
  // Track D geometry capture (geocapture.cpp)
  void* geo = nullptr;                 // capture active in the current pass
  bool geo_em = false;                 // ... and it is an effects pass of the emissive capture (D-EM)
  int geo_ts = -1;                     // timestamp slot of the current pass
  VkViewport viewport{};               // last vkCmdSetViewport(first = 0)
  bool has_viewport = false;
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
void trace_note_draw(CmdState* s, int bind_point, const char* kind, const char* extra);
void shader_dump(uint64_t hash, const void* code, size_t size);
std::string image_ref(VkImage image);
std::string view_ref(VkImageView view);
// Records copies of the pass's attachments into readback buffers; returns
// the dump ids as a JSON array fragment for the pass's "end" event.
std::string trace_dump_pass(VkCommandBuffer cmd, CmdState* s);
void count_pass();
// Diagnostic counters, logged with the periodic [auto] line.
enum DiagCounter { DIAG_PRESENT, DIAG_PIPELINES, DIAG_PIPELINE_US, DIAG_DESC_WRITES, DIAG_SHADER_MODULES, DIAG_SUBMIT, DIAG_BEGIN_CMD, DIAG_BEGIN_PASS, DIAG_BEGIN_RENDERING, DIAG_BIND_PIPE, DIAG_DRAW,
                   DIAG_DISPATCH, DIAG_EXEC, DIAG_DISPLAYED, DIAG_COUNT };
void diag(DiagCounter c, uint64_t n = 1);
void watchdog_start();                     // passes per frame feed the auto trigger
void dumps_on_submit(const std::vector<VkCommandBuffer>& cmds, uint64_t frame);
void dumps_finish(uint64_t frame, const std::string& capture_dir, VkQueue queue);
bool write_png_rgb(const std::string& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgb);

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
void sl_note_submit(const std::vector<VkCommandBuffer>& cmds);
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
void geo_pass_ending(VkCommandBuffer cmd, CmdState* s);   // inside, right before the game's end
void geo_pass_after(VkCommandBuffer cmd, CmdState* s);    // outside, right after the game's end
void geo_forget_buffer(VkBuffer b);
VkDeviceSize geo_buffer_size(VkBuffer b);                   // 0 if unknown
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
const VkDeviceCreateInfo* rt_device_create_info(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                VkDeviceCreateInfo* scratch);
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
  int scene = 0;             // bvhsplit scene: 0 main, 1/2 emissive (D-EM, emissive.cpp)
  bool converted = false;    // the soup is already camera-relative world (the emissive scene)
};
void rt_after_capture(VkCommandBuffer cmd, const RtCapture& cap);
bool rt_scene(VkAccelerationStructureKHR* tlas, uint64_t* built_frame);
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
};
void bvh_device_created(DeviceData* dd);
void bvh_device_destroyed(DeviceData* dd);
bool bvh_split_wanted();                       // rt_bvh_split=1 and the split is usable
void bvh_begin_capture(VkCommandBuffer cmd);
void bvh_discard_scene(int scene);             // drops a scene's draws recorded so far (the emissive scene)
// Per draw of the RT capture (split mode). `token` identifies the draw in later calls.
struct BvhDraw {
  const InstPipe* inst;         // nullptr: not instanceable
  uint64_t vs;
  uint32_t pipe_id;
  bool alpha;                   // alpha-tested (its FS is in the table, UV captured)
  DrawArgs args;
  const CmdState* s;
  int scene = 0;                // 0 main scene, 1/2 the emissive scene of even/odd frames (D-EM)
};
enum BvhVerdict { BVH_CAPTURE = 0, BVH_CACHED = 1, BVH_CACHED_AUDIT = 2 };
BvhVerdict bvh_classify(VkCommandBuffer cmd, const BvhDraw& d, uint32_t* token, uint32_t* mesh_tris);
// The draw's alpha record (cached draws; UINT32_MAX: none, made opaque).
void bvh_set_record(uint32_t token, uint32_t rec);
// The draw was captured into the soup at `offset` (`prims` triangles, all instances).
void bvh_captured(uint32_t token, VkDeviceSize offset, uint32_t prims, bool alpha, uint32_t rec);
// Uncaptured draw that is not instanceable (counted only).
void bvh_note_skipped(uint32_t token);
// After the pass: object-space extraction, BLAS builds/refits, compaction, TLAS.
// `stamp(i)` writes the timing timestamps 1..3.
struct BvhResult {
  VkAccelerationStructureKHR tlas;
  uint32_t instances, static_instances, dynamic_draws, cached_draws, new_meshes;
  uint64_t static_tris, dynamic_tris;
  VkBuffer info; VkDeviceSize info_off, info_bytes;   // InstInfo of this TLAS (set 1 binding 7)
  VkBuffer pool; VkDeviceSize pool_bytes;             // object-space vertices (binding 8)
  VkBuffer ring; VkDeviceSize inst_off;               // the TLAS's VkAccelerationStructureInstanceKHR array
  const uint32_t* info_host;                          // InstInfo, host-visible (4 words per instance)
};
bool bvh_build(VkCommandBuffer cmd, const RtCapture& cap, void (*stamp)(VkCommandBuffer, uint32_t), BvhResult* out);
void bvh_on_submit(const std::vector<VkCommandBuffer>& cmds);
void bvh_on_present();
void bvh_forget_buffer(VkBuffer b);
void bvh_buffer_written(VkBuffer b, VkDeviceSize offset, VkDeviceSize size);

// ---- Track D D-EM: emissive geometry (emissive.cpp, docs/TRACK_D_RT.md 12) ----
// The game redraws its emissive meshes into the effects target (B10G11R11 + the
// G-buffer depth, before the tiled lighting). Those draws are captured (XFB, the
// BVH split's emissive scene), with a per-draw record (radiance, mask texture); at
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
};
bool em_draw(VkCommandBuffer cmd, const CmdState* s, const DrawArgs& a, const EmDrawIn& in, VkBuffer* soup,
             VkDeviceSize* off, VkDeviceSize* bytes, uint32_t* prims);
// After the pass, outside it: the captured ranges (offset, triangles) to camera-relative
// world with the pass's camera UBO.
void em_pass_done(VkCommandBuffer cmd, const CmdState* s, VkBuffer ubo, VkDeviceSize ubo_off, uint32_t stride,
                  const std::vector<std::pair<VkDeviceSize, uint32_t>>& ranges, int ts);
void em_dispatch_pre(VkCommandBuffer cmd, uint64_t cs, bool indirect);   // cmd.cpp: builds at the lighting
void em_on_present();
// ABI 8: the emissive scene for the core (host_api.h Bg3HostApi::rt_emissive).
bool em_scene(VkDescriptorSetLayout* layout, VkDescriptorSet* set, Bg3EmissiveInfo* info);

// ---- Track D phase 3: alpha-tested geometry in the ray queries (alphatest.cpp) ----
// One line of shaders/rt/alpha_fs.txt (scripts/alpha_table.py).
struct AlphaFs {
  uint8_t kind;              // 1 virtual texture, 2 plain texture
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
  float p0[4], p1[4];                    // vt: uv transform
  float t[32];                           // vt: the SSBO entry, two row-major mat4
};
static_assert(sizeof(AlphaRec) == 208, "AlphaRec must match rt_common.hlsli");
void alpha_device_created(DeviceData* dd);
void alpha_device_destroyed(DeviceData* dd);
bool alpha_lookup(uint64_t fs_hash, AlphaFs* out);   // copied: the table may be reloaded
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
};
void alpha_commit(VkCommandBuffer cmd, const AlphaBindings& b);
// The legacy TLAS's InstInfo (one instance, the 2-geometry soup BLAS).
bool alpha_legacy_info(VkCommandBuffer cmd, uint32_t stride, VkBuffer* buf, VkDeviceSize* size);
bool rt_alpha_set(VkDescriptorSetLayout* layout, VkDescriptorSet* set);

uint64_t frame_index();
void* persist(const char* key, size_t size);

}  // namespace bg3
