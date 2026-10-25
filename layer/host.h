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
};

DeviceData* main_device();             // the first device created; BG3 makes one
DeviceData* device_of(const void* dispatchable);   // device, queue or command buffer

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
  struct Subpass { std::vector<uint32_t> color; uint32_t depth = VK_ATTACHMENT_UNUSED; };
  std::vector<Subpass> subpasses;
};
struct DescEntry {
  uint32_t binding, element;
  VkDescriptorType type;
  VkImageView view;                    // image descriptors
  VkBuffer buffer;                     // buffer descriptors
  VkDeviceSize offset, range;
};

struct Tracker {
  std::shared_mutex mu;
  std::unordered_map<VkShaderModule, uint64_t> modules;
  std::unordered_map<VkImage, ImageRec> images;
  std::unordered_map<VkImageView, ViewRec> views;
  std::unordered_map<VkFramebuffer, std::vector<VkImageView>> framebuffers;
  std::unordered_map<VkRenderPass, RenderPassRec> render_passes;
  std::unordered_map<VkPipeline, PipelineRec> pipelines;
  std::unordered_map<VkDescriptorSet, std::vector<DescEntry>> sets;
  std::unordered_map<VkDescriptorUpdateTemplate, std::vector<VkDescriptorUpdateTemplateEntry>> templates;
  std::atomic<uint32_t> next_image_id{1}, next_pipeline_id{1};
};
Tracker& tracker();

void track_set_write(const VkWriteDescriptorSet& w);
void track_set_template(VkDescriptorSet set, VkDescriptorUpdateTemplate tpl, const void* data);
std::vector<DescEntry> decode_push(const VkWriteDescriptorSet* writes, uint32_t count);
std::vector<DescEntry> decode_push_template(VkDescriptorUpdateTemplate tpl, const void* data);

// ---- command buffer state (cmd.cpp) ----
struct CmdState {
  bool in_pass = false;
  Bg3PassInfo pass{};
  uint32_t pass_ordinal = 0;
  VkPipeline pipe[2] = {};             // graphics, compute
  uint32_t pipe_id[2] = {};
  uint64_t vs = 0, fs = 0, cs = 0;     // stage hashes of the bound pipelines
  std::array<VkDescriptorSet, 8> sets[2] = {};
  std::vector<DescEntry> push[2];
  bool tracing = false;                // began while a trace was recording
  std::vector<std::string> events;     // pre-rendered JSON objects, one per event
  // segment aggregation for the trace
  std::string seg_key;
  uint32_t seg_count = 0;
  std::string seg_json;
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

// ---- hot reload + shader compiler (hotreload.cpp, shaders.cpp) ----
void core_init(DeviceData* dd);
void core_shutdown();
void core_poll(VkQueue present_queue); // at present: reload if the DLL changed
void core_on_frame(uint64_t frame);
void core_on_pass_end(VkCommandBuffer cmd, const Bg3PassInfo* pass);
void defer_destroy(Bg3DeferKind kind, uint64_t handle);
void defer_tick(bool flush_all);

bool shader_get(const char* file, const char* entry, const char* profile, Bg3ShaderBlob* out);
void shader_poll();

uint64_t frame_index();
void* persist(const char* key, size_t size);

}  // namespace bg3
