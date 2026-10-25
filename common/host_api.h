// ABI between the stable host layer (VkLayer_bg3rtx.dll) and the hot-reloadable
// core (bg3rtx_core.dll). Plain C structs and function pointers only: the core is
// unloaded and replaced while the game runs, so nothing here may own C++ objects
// that cross the boundary.
#pragma once
#include <cstddef>
#include <cstdint>
#include "vk_funcs.h"

#define BG3RTX_ABI_VERSION 1

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
};

enum Bg3DeferKind : uint32_t {
  BG3_DEFER_PIPELINE, BG3_DEFER_PIPELINE_LAYOUT, BG3_DEFER_DESCRIPTOR_SET_LAYOUT,
  BG3_DEFER_DESCRIPTOR_POOL, BG3_DEFER_SHADER_MODULE, BG3_DEFER_IMAGE,
  BG3_DEFER_IMAGE_VIEW, BG3_DEFER_BUFFER, BG3_DEFER_MEMORY, BG3_DEFER_SAMPLER,
  BG3_DEFER_COMMAND_POOL, BG3_DEFER_FENCE, BG3_DEFER_SEMAPHORE,
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
};

typedef const Bg3CoreApi* (*PFN_bg3rtx_core_load)(const Bg3HostApi* host);
#define BG3RTX_CORE_ENTRY "bg3rtx_core_load"
