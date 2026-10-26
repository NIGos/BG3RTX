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
std::string now_stamp();              // 20261025-224512
uint64_t file_mtime(const std::string& path);   // 0 if missing
bool write_file(const std::string& path, const void* data, size_t size);
void make_dirs(const std::string& path);

uint64_t hash64(const void* data, size_t size);   // FNV-1a 64

// ---- config (util.cpp) ----
void cfg_poll();                       // re-reads bg3rtx.ini when it changes
int cfg_int(const char* key, int fallback);
float cfg_float(const char* key, float fallback);

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
  bool game_alloc = false;             // allocated through this layer (not by Streamline below it)
  std::vector<std::string> events;     // pre-rendered JSON objects, one per event
  // segment aggregation for the trace
  std::string seg_key;
  uint32_t seg_count = 0;
  std::string seg_json;
  // Track D geometry capture (geocapture.cpp)
  void* geo = nullptr;                 // capture active in the current pass
  int geo_ts = -1;                     // timestamp slot of the current pass
  VkViewport viewport{};               // last vkCmdSetViewport(first = 0)
  bool has_viewport = false;
};
CmdState* cmd_state(VkCommandBuffer cmd, bool create = false);
void cmd_forget(VkCommandBuffer cmd);

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
void restore_bindings(VkCommandBuffer cmd, VkPipelineBindPoint bp);
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
void geo_pre_draw(VkCommandBuffer cmd, CmdState* s, uint32_t kind, uint32_t count, uint32_t instances);
void geo_post_draw(VkCommandBuffer cmd, CmdState* s);
void geo_pass_ending(VkCommandBuffer cmd, CmdState* s);   // inside, right before the game's end
void geo_pass_after(VkCommandBuffer cmd, CmdState* s);    // outside, right after the game's end
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
bool rt_capture_wanted();                  // build the scene this frame (rt_build=1, not yet built)
VkBufferUsageFlags rt_xfb_usage();         // extra usage the per-frame XFB buffer needs
bool rt_alloc_device_address();            // memory for BDA buffers needs VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
void rt_before_capture(VkCommandBuffer cmd);   // outside the G-buffer pass, before it begins
// After the G-buffer pass, outside it: XFB clip positions -> camera-relative
// world in place, BLAS build or refit, TLAS build.
void rt_after_capture(VkCommandBuffer cmd, VkBuffer xfb, VkDeviceSize bytes, VkBuffer ubo, VkDeviceSize ubo_offset,
                      uint64_t signature, uint32_t draws);
bool rt_scene(VkAccelerationStructureKHR* tlas, uint64_t* built_frame);
void rt_on_present();

uint64_t frame_index();
void* persist(const char* key, size_t size);

}  // namespace bg3
