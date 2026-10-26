// ABI between the stable host layer (VkLayer_bg3rtx.dll) and the hot-reloadable
// core (bg3rtx_core.dll). Plain C structs and function pointers only: the core is
// unloaded and replaced while the game runs, so nothing here may own C++ objects
// that cross the boundary.
#pragma once
#include <cstddef>
#include <cstdint>
#include "vk_funcs.h"

#define BG3RTX_ABI_VERSION 2

// One render pass instance (classic render pass or dynamic rendering) as the
// game recorded it. Identified across frames by its signature, not by position:
// command buffers are recorded on several threads, so ordinals are per buffer.
struct Bg3PassInfo {
  uint32_t ordinal;            // index of this pass inside its command buffer
  uint32_t width, height;
  uint32_t color_count;
  VkFormat color_formats[8];
  VkImage color_images[8];
  VkImageView color_views[8];
  VkFormat depth_format;
  VkImage depth_image;
  VkImageView depth_view;
  uint32_t draw_count;
  uint64_t first_vs_hash;      // SPIR-V hash of the first draw's vertex shader
  uint64_t first_fs_hash;      // and fragment shader: the pass's practical name
  uint64_t last_fs_hash;
  // Classic render passes: each attachment's finalLayout. Dynamic rendering:
  // the layout it is rendered in. UNDEFINED when unknown.
  VkImageLayout color_layouts[8];
  VkImageLayout depth_layout;
  // What ran in this command buffer since the previous render pass ended.
  uint32_t dispatches_before;
  uint32_t indirect_dispatches_before;
  uint64_t last_cs_hash_before;
};

struct Bg3DispatchInfo {
  uint64_t cs_hash;
  bool indirect;
  uint32_t groups[3];          // zero for indirect dispatches
};

// A resource the game has bound at the moment of the query.
struct Bg3Binding {
  VkDescriptorType type;
  VkImageView view;            // image descriptors
  VkImage image;
  VkFormat format;             // of the view
  VkImageLayout layout;        // as written into the descriptor
  uint32_t width, height;      // of the image's mip 0
  VkBuffer buffer;             // buffer descriptors
  VkDeviceSize offset;         // dynamic offset already applied
  VkDeviceSize range;          // VK_WHOLE_SIZE resolved to the remaining size when known
};

struct Bg3Image {
  VkImage image;
  VkImageView view;
  VkDeviceMemory memory;
};

enum Bg3DeferKind : uint32_t {
  BG3_DEFER_PIPELINE, BG3_DEFER_PIPELINE_LAYOUT, BG3_DEFER_DESCRIPTOR_SET_LAYOUT,
  BG3_DEFER_DESCRIPTOR_POOL, BG3_DEFER_SHADER_MODULE, BG3_DEFER_IMAGE,
  BG3_DEFER_IMAGE_VIEW, BG3_DEFER_BUFFER, BG3_DEFER_MEMORY, BG3_DEFER_SAMPLER,
  BG3_DEFER_COMMAND_POOL, BG3_DEFER_FENCE, BG3_DEFER_SEMAPHORE, BG3_DEFER_QUERY_POOL,
};

struct Bg3ShaderBlob {
  const uint32_t* code;        // SPIR-V, owned by the host, valid until next compile
  size_t size_bytes;
  uint64_t generation;         // bumps every successful recompile of this entry
};

struct Bg3HostApi {
  uint32_t abi_version;
  void (*log)(const char* fmt, ...);

  VkInstance instance;
  VkPhysicalDevice physical_device;
  VkDevice device;
  uint32_t graphics_family;
  VkQueue graphics_queue;
  const VkDeviceFuncs* vk;     // next-layer entry points: the core's own calls skip our hooks

  // Compiles shaders/<file> with DXC to SPIR-V on first request and again whenever
  // anything under shaders/ changes. Returns false if it has never compiled.
  bool (*get_shader)(const char* file, const char* entry, const char* profile, Bg3ShaderBlob* out);

  // bg3rtx.ini, re-read when it changes on disk.
  int (*cfg_int)(const char* key, int fallback);
  float (*cfg_float)(const char* key, float fallback);

  // Memory that survives core reloads, zeroed on first request for a key.
  void* (*persist)(const char* key, size_t size);

  // Hands a GPU object to the host to destroy a few frames later, so command
  // buffers the game already recorded never reference a destroyed object.
  void (*defer_destroy)(Bg3DeferKind kind, uint64_t handle);

  uint64_t (*frame_index)();
  const char* root_dir;        // repository root: shaders/, captures/, bg3rtx.ini

  // Looks up what the game has bound right now in `cmd` at (bind point, set,
  // binding, array element), including push descriptors. Only valid inside a
  // callback for that command buffer.
  bool (*query_binding)(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, uint32_t set, uint32_t binding,
                        uint32_t element, Bg3Binding* out);

  // Device-local 2D image plus a full view. Free with defer_destroy (image,
  // view, memory).
  bool (*alloc_image)(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Bg3Image* out);

  // Re-binds the game's pipeline and descriptor sets (with their dynamic
  // offsets) at this bind point, after the core recorded its own work.
  // Push descriptors and push constants are not restored.
  void (*restore_bindings)(VkCommandBuffer cmd, VkPipelineBindPoint bind_point);

  // The motion vectors the game last passed to DLSS (seen by the NGX hook):
  // image, a view of it, its format and the game's MV scale. In render
  // pixels, so that previous = current + mv * scale. False until the first
  // DLSS evaluate.
  bool (*dlss_motion_vectors)(VkImage* image, VkImageView* view, VkFormat* format, float scale[2]);
};

struct Bg3CoreApi {
  uint32_t abi_version;
  // Called before FreeLibrary. Hand every GPU object to defer_destroy.
  void (*unload)();
  // Once per present, before it is forwarded.
  void (*on_frame)(uint64_t frame);
  // After the game's render pass ends, outside any render pass, in the game's
  // command buffer. The core may record its own work here.
  void (*on_pass_end)(VkCommandBuffer cmd, const Bg3PassInfo* pass);
  // Right before the game's render pass begins (the pass is not open yet, so
  // compute work may be recorded). Attachments are filled; draw fields are zero.
  void (*on_pass_begin)(VkCommandBuffer cmd, const Bg3PassInfo* pass);
  // After each game dispatch, outside any render pass.
  void (*on_dispatch)(VkCommandBuffer cmd, const Bg3DispatchInfo* dispatch);
  // Right before the game ends a command buffer (outside any render pass).
  void (*on_cmd_end)(VkCommandBuffer cmd);
  // Right before each game dispatch is recorded; its pipeline and descriptors
  // are already bound, so query_binding sees what it will read and write.
  void (*on_dispatch_pre)(VkCommandBuffer cmd, const Bg3DispatchInfo* dispatch);
};

typedef const Bg3CoreApi* (*PFN_bg3rtx_core_load)(const Bg3HostApi* host);
#define BG3RTX_CORE_ENTRY "bg3rtx_core_load"
