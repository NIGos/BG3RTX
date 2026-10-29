// ABI between the stable host layer (VkLayer_bg3rtx.dll) and the hot-reloadable
// core (bg3rtx_core.dll). Plain C structs and function pointers only: the core is
// unloaded and replaced while the game runs, so nothing here may own C++ objects
// that cross the boundary.
#pragma once
#include <cstddef>
#include <cstdint>
#include "vk_funcs.h"
#include "assetdb_api.h"

// 6: Bg3HostApi::assetdb (appended).
// 7: Bg3HostApi::rr_active (appended).
// 8: Bg3HostApi::rt_scene_set (appended).
// 15: named, bounded pan-burst readbacks (9-14 belong to other phase branches).
// 17: Bg3DispatchInfo::native_validation (box lights, codex/p3-box's 9, merged).
// 18: Bg3HostApi::rt_emissive (D-EM, codex/p2-emissive's 9, merged after pan_capture/rt_scene_camera).
#define BG3RTX_ABI_VERSION 18
// D-EM is in this host: Bg3HostApi::rt_emissive (ABI 9) and Bg3EmissiveInfo exist. The core's
// SHaRC emission injection (core/gi.cpp, CMake BG3RTX_SHARC_EM=AUTO) compiles on this.
#define BG3RTX_HOST_HAS_RT_EMISSIVE 1

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
  uint32_t native_validation;  // bit0: recognized native direct-light diagnostic controls
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
  BG3_DEFER_ACCELERATION_STRUCTURE,
};

struct Bg3ShaderBlob {
  const uint32_t* code;        // SPIR-V, owned by the host, valid until next compile
  size_t size_bytes;
  uint64_t generation;         // bumps every successful recompile of this entry
};

// What the game's device was created with, published by the layer through
// persist(BG3_DEVICE_INFO_KEY, sizeof(Bg3DeviceInfo)) when the device is created (no ABI
// change: the core reads the same key). All zero before that, or without rt=1.
#define BG3_DEVICE_INFO_KEY "bg3rtx.device_info"
enum Bg3DeviceFeatureBits : uint32_t {
  BG3_DEV_INT64 = 1u << 0,           // shaderInt64
  BG3_DEV_INT64_ATOMICS = 1u << 1,   // shaderBufferInt64Atomics
  BG3_DEV_FLOAT16 = 1u << 2,         // shaderFloat16
  BG3_DEV_STORAGE16 = 1u << 3,       // storageBuffer16BitAccess
};
struct Bg3DeviceInfo {
  uint32_t version;                  // 1
  uint32_t features;                 // Bg3DeviceFeatureBits enabled on the device by the layer (SHaRC)
  VkPhysicalDeviceMemoryProperties memory;   // for the core's own device-local buffers
};

// D-EM (ABI 9): the emissive scene of the latest build (docs/TRACK_D_RT.md 13). The
// descriptor set's layout is in shaders/rt/em_common.hlsli: the emissive TLAS, its
// per-instance InstInfo, the mesh pool and emissive soup, the per-draw records, the
// light list, the mask textures and a statistics buffer for debug views.
struct Bg3EmissiveInfo {
  uint64_t built_frame;        // frame_index of the build the set belongs to
  uint32_t instances;          // TLAS instances
  uint32_t records;            // emissive draws (records)
  uint32_t lights;             // triangles in the light list (capped by rt_em_max_lights)
  float pre_exposure;          // lighting s1b13[0] when known (0 otherwise): radiance / pre_exposure = lighting units
  // The game's effects target of the frame (sky, clouds, emissive draws; B10G11R11, render
  // resolution), for comparisons; image null when not seen this frame.
  VkImage fx_image;
  VkImageView fx_view;
  VkImageLayout fx_layout;     // the layout its last pass left it in
  uint32_t fx_width, fx_height;
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

  // Track D: the top-level acceleration structure over this frame's captured
  // G-buffer geometry (camera-relative world space, the lighting pass's space),
  // and the frame index it was last recorded in. False when hardware RT is
  // not enabled on the device (rt=0) or nothing has been built yet. The
  // handle is the host's; it stays valid for a few frames after it is
  // replaced, like everything handed to defer_destroy.
  bool (*rt_scene)(VkAccelerationStructureKHR* tlas, uint64_t* built_frame);

  // Track D phase 3 (ABI 4): set 1 of every ray query pass. The layout is fixed
  // for the device's lifetime; the set belongs to the latest scene build and
  // holds what alpha-tested candidates are tested with (shaders/rt/rt_common.hlsli):
  // per-draw records, the captured UV soup, the textures and tile caches the
  // game sampled in that frame, two samplers. `set` is VK_NULL_HANDLE before the
  // first build. False when hardware RT is not enabled.
  bool (*rt_alpha)(VkDescriptorSetLayout* layout, VkDescriptorSet* set);

  // A host-visible, coherent, persistently mapped buffer (statistics read back
  // by the core). Free with defer_destroy (buffer, memory).
  bool (*alloc_host_buffer)(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* buffer, VkDeviceMemory* memory,
                            void** mapped);

  // ABI 5 (Track D reflections). The descriptor set the game has bound right
  // now at (bind point, set) in `cmd`, the layout it was allocated with, and its
  // dynamic offsets (`dyn_count`: capacity in, count out). False for a push
  // descriptor set or an untracked one. Only valid inside a callback for `cmd`.
  bool (*query_set)(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, uint32_t set, VkDescriptorSet* out_set,
                    VkDescriptorSetLayout* out_layout, uint32_t* dyn, uint32_t* dyn_count);
  // ABI 5: the image Ray Reconstruction gets as DLSSD.SpecularHitDistance (render
  // resolution, GENERAL layout, written this frame). The core calls it every
  // frame it writes the image, and with VK_NULL_HANDLE before releasing it.
  void (*set_spec_hit_distance)(VkImage image, VkImageView view, VkFormat format, uint32_t width, uint32_t height);

  // ABI 6: the offline asset database (common/assetdb_api.h, docs/ASSET_DB.md): placed
  // lights, shader -> material bindings, rooms/roofs, presets, probes. Never null; the
  // table carries its own version and loads the data on first use.
  const Bg3AssetDbApi* assetdb;

  // ABI 7: true while Ray Reconstruction actually denoises the frame (an RR
  // evaluate succeeded in the last 3 frames). The core picks its 1-spp "RR
  // modes" (gi_rr, ls_soft, rf_rr = -1) from this, not from the rr key, so
  // FSR/TAA, an SR fallback or a failed RR creation get the non-RR modes.
  bool (*rr_active)();

  // ABI 8: the latest scene's TLAS and the set 1 built with it, as one pair (a
  // TLAS traced with another build's set reads InstInfo, records and vertices
  // that do not match its instances). Each build is a snapshot slot of the host;
  // a slot is reused only after the GPU finished every frame it was handed out
  // in, so call this in the frame that records the passes. False before the
  // first build, when hardware RT is off, and once the last build is older than
  // the previous frame.
  bool (*rt_scene_set)(VkAccelerationStructureKHR* tlas, VkDescriptorSet* set, uint64_t* built_frame);
  // Diagnostic only. A positive pan_capture serial arms 16 lighting frames.
  // Images require TRANSFER_SRC usage. Buffer samples are copied on the GPU.
  // Returns false outside a burst or when a resource cannot be captured.
  bool (*pan_capture)(VkCommandBuffer cmd, const char* name, const Bg3Binding* resource);
  // ABI 16: select a live scene by the lighting camera's actual native pose.
  bool (*rt_scene_camera)(VkCommandBuffer cmd, VkAccelerationStructureKHR* tlas,
                          VkDescriptorSet* set, uint64_t* built_frame);

  // ABI 9 (D-EM): the emissive scene (set 3 of shaders/rt/em_common.hlsli) of the latest
  // build (the previous submitted frame). False when RT or the capture is off; `set` is
  // VK_NULL_HANDLE before the first build. Pass the command buffer the set will be bound in:
  // the game images and buffers it reads (glow maps, the effects target) are then kept
  // alive until that command buffer has run (the layer's image lifetime tracking).
  bool (*rt_emissive)(VkCommandBuffer cmd, VkDescriptorSetLayout* layout, VkDescriptorSet* set, Bg3EmissiveInfo* info);
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
