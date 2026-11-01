// Offline replay pack writer (tools/frame_replay.md "Live writer", diagnostics only): one
// production compute dispatch (gi_hit_direct) with everything it reads, as the pack of
// common/replay_pack.h, so tools/frame_replay re-runs it without the game.
//
// The core calls Bg3HostApi::replay_pack (ABI 25) right after it recorded the dispatch, with the
// push constants and the push descriptor set 0 it used and the material set 1 it bound. The host
// records, in that command buffer, copies of every resource into host-visible staging chunks:
//   set 0: the descriptors as pushed (UBOs from the game's host mapping or a transfer copy, SSBOs
//          and images by GPU copies; the pass's storage outputs after it ran = the live reference);
//   set 1: what the layer wrote into that material set (recorded here: the layer writes its sets
//          through dd->vk, which this file wraps), whole buffers except the soup (b6) and the mesh
//          pool (b8): only the spans the frame's InstInfo names unless the whole binding fits;
//          the sampled images the set binds (gAlphaTex, gAlphaCache, gHitCache: all mips of the
//          view) within the cap (replay_pack_cap_mb; rp::fill_budget: the hit material's bindings b14/b2 first,
//          smallest first, each image that still fits; "[replay] pack N textures: ..." lists what
//          was kept per binding and the 10 largest);
//          the samplers' states; the TLAS instance array of the scene ring region (bvhsplit.cpp)
//          whose InstInfo set 1 binds; with rt_cascade_casters the cascade instance's triangles
//          (casters.bin, the "casters" directive: the converted cascade soup its BLAS was built
//          from, bvh_replay_region; its InstInfo has none), so the replay TLAS has the cut-away
//          roofs ("[replay] pack N casters: ..."; left out with a note if the soup's ring slot moved on);
//          the variant's defines the pass contract carries (rp::contract_defines: RT_CASCADE_MASKS);
//   sets 2-3: the deferred lighting's inputs as the game bound them for the tiled lighting
//          (add_lighting, rp::kLightingSources): G-buffer albedo/material, AO, sky irradiance and
//          specular cubes, DFG LUT, local probe records and the cubes they name, the atmosphere
//          constants, transmittance LUT and cloud shadows; all droppable ("[replay] pack N lighting
//          inputs: ..." says what was bound, kept and its MB).
// Buffers without TRANSFER_SRC usage are read by a small compute shader (shaders/replay_copy.hlsl).
// Images without TRANSFER_SRC (most game textures and the VT caches) are read through a sampled
// 2D-array view by shaders/replay_image.hlsl and stored decoded (rp::shader_read_format: BCn as
// 8-bit or half texels; `image ... SOURCE` names the game format). Images in pieces of at most a
// staging chunk. Integer formats without TRANSFER_SRC stay "omitted" (replayed as NaN).
// Once the GPU has run the frame (life_gpu_done, no wait), a worker thread writes the pack folder
// and logs "[replay] pack <serial> written: <MB> MB (<n> images omitted, <r> read by shader), frame
// <f>, pass gi_hit_direct, instances <n> -> <dir>". Rendering is unchanged: copies and reads only,
// layouts restored.
#include "host.h"
#include "../common/replay_pack.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <thread>
#include <tuple>

namespace bg3 {

thread_local bool t_game_descriptor_update = false;

#if BG3RTX_DIAGNOSTICS
namespace {
namespace rp = bg3::replay;

// ---- object records (every image, view, sampler and buffer created on the device) ----
struct ImgRec { VkImageType type; VkFormat format; uint32_t w, h, d, mips, layers; VkImageUsageFlags usage; };
struct ViewInfo { VkImage image; VkFormat format; VkImageViewType type; VkImageSubresourceRange range; VkComponentMapping comp; };
struct SamplerRec { uint32_t mag, min, mip, au, av, aw; float aniso, max_lod; };
struct BufRec { VkDeviceSize size; VkBufferUsageFlags usage; };
struct Desc {
  VkDescriptorType type;
  VkImageView view;
  VkImageLayout layout;
  VkSampler sampler;
  VkBuffer buffer;
  VkDeviceSize offset, range;
};
struct SetRec { VkDescriptorPool pool; std::unordered_map<uint64_t, Desc> d; };

DeviceData* g_dd = nullptr;
VkDeviceFuncs g_next{};   // the entries this file wraps, as loaded
std::shared_mutex g_obj_mu;
std::unordered_map<VkImage, ImgRec> g_imgs;
std::unordered_map<VkImageView, ViewInfo> g_views;
std::unordered_map<VkSampler, SamplerRec> g_samplers;
std::unordered_map<VkBuffer, BufRec> g_bufs;
// The material sets (layout g_alpha_layout): the layer's own writes and copies into them.
std::mutex g_set_mu;
std::unordered_map<VkDescriptorSet, SetRec> g_sets;
std::atomic<bool> g_any_set{false};
std::atomic<VkDescriptorSetLayout> g_alpha_layout{VK_NULL_HANDLE};
VkSampler g_wrap = VK_NULL_HANDLE, g_clamp = VK_NULL_HANDLE;

bool is_image_desc(VkDescriptorType t) {
  return t == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || t == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
         t == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || t == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
}
bool is_buffer_desc(VkDescriptorType t) {
  return t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
         t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateImage(VkDevice d, const VkImageCreateInfo* ci, const VkAllocationCallbacks* a, VkImage* out) {
  const VkResult r = g_next.CreateImage(d, ci, a, out);
  if (r == VK_SUCCESS) {
    std::unique_lock<std::shared_mutex> lock(g_obj_mu);
    g_imgs[*out] = {ci->imageType, ci->format, ci->extent.width, ci->extent.height, ci->extent.depth, ci->mipLevels, ci->arrayLayers, ci->usage};
  }
  return r;
}
VKAPI_ATTR void VKAPI_CALL DestroyImage(VkDevice d, VkImage image, const VkAllocationCallbacks* a) {
  { std::unique_lock<std::shared_mutex> lock(g_obj_mu); g_imgs.erase(image); }
  g_next.DestroyImage(d, image, a);
}
VKAPI_ATTR VkResult VKAPI_CALL CreateImageView(VkDevice d, const VkImageViewCreateInfo* ci, const VkAllocationCallbacks* a, VkImageView* out) {
  const VkResult r = g_next.CreateImageView(d, ci, a, out);
  if (r == VK_SUCCESS) {
    std::unique_lock<std::shared_mutex> lock(g_obj_mu);
    g_views[*out] = {ci->image, ci->format, ci->viewType, ci->subresourceRange, ci->components};
  }
  return r;
}
VKAPI_ATTR void VKAPI_CALL DestroyImageView(VkDevice d, VkImageView view, const VkAllocationCallbacks* a) {
  { std::unique_lock<std::shared_mutex> lock(g_obj_mu); g_views.erase(view); }
  g_next.DestroyImageView(d, view, a);
}
VKAPI_ATTR VkResult VKAPI_CALL CreateSampler(VkDevice d, const VkSamplerCreateInfo* ci, const VkAllocationCallbacks* a, VkSampler* out) {
  const VkResult r = g_next.CreateSampler(d, ci, a, out);
  if (r == VK_SUCCESS) {
    std::unique_lock<std::shared_mutex> lock(g_obj_mu);
    g_samplers[*out] = {uint32_t(ci->magFilter), uint32_t(ci->minFilter), uint32_t(ci->mipmapMode), uint32_t(ci->addressModeU),
                        uint32_t(ci->addressModeV), uint32_t(ci->addressModeW), ci->anisotropyEnable ? ci->maxAnisotropy : 0.0f, ci->maxLod};
  }
  return r;
}
VKAPI_ATTR void VKAPI_CALL DestroySampler(VkDevice d, VkSampler s, const VkAllocationCallbacks* a) {
  { std::unique_lock<std::shared_mutex> lock(g_obj_mu); g_samplers.erase(s); }
  g_next.DestroySampler(d, s, a);
}
VKAPI_ATTR VkResult VKAPI_CALL CreateBuffer(VkDevice d, const VkBufferCreateInfo* ci, const VkAllocationCallbacks* a, VkBuffer* out) {
  const VkResult r = g_next.CreateBuffer(d, ci, a, out);
  if (r == VK_SUCCESS) {
    std::unique_lock<std::shared_mutex> lock(g_obj_mu);
    g_bufs[*out] = {ci->size, ci->usage};
  }
  return r;
}
VKAPI_ATTR void VKAPI_CALL DestroyBuffer(VkDevice d, VkBuffer b, const VkAllocationCallbacks* a) {
  { std::unique_lock<std::shared_mutex> lock(g_obj_mu); g_bufs.erase(b); }
  g_next.DestroyBuffer(d, b, a);
}
VKAPI_ATTR VkResult VKAPI_CALL AllocateDescriptorSets(VkDevice d, const VkDescriptorSetAllocateInfo* ai, VkDescriptorSet* out) {
  const VkResult r = g_next.AllocateDescriptorSets(d, ai, out);
  const VkDescriptorSetLayout alpha = g_alpha_layout.load();
  if (r == VK_SUCCESS && alpha) {
    for (uint32_t i = 0; i < ai->descriptorSetCount; ++i)
      if (ai->pSetLayouts[i] == alpha) {
        std::lock_guard<std::mutex> lock(g_set_mu);
        g_sets[out[i]] = SetRec{ai->descriptorPool, {}};
        g_any_set = true;
      }
  }
  return r;
}
void forget_pool(VkDescriptorPool pool) {
  if (!g_any_set) return;
  std::lock_guard<std::mutex> lock(g_set_mu);
  for (auto it = g_sets.begin(); it != g_sets.end();) it = it->second.pool == pool ? g_sets.erase(it) : std::next(it);
}
VKAPI_ATTR VkResult VKAPI_CALL FreeDescriptorSets(VkDevice d, VkDescriptorPool pool, uint32_t n, const VkDescriptorSet* sets) {
  if (g_any_set) {
    std::lock_guard<std::mutex> lock(g_set_mu);
    for (uint32_t i = 0; i < n; ++i) g_sets.erase(sets[i]);
  }
  return g_next.FreeDescriptorSets(d, pool, n, sets);
}
VKAPI_ATTR void VKAPI_CALL DestroyDescriptorPool(VkDevice d, VkDescriptorPool pool, const VkAllocationCallbacks* a) {
  forget_pool(pool);
  g_next.DestroyDescriptorPool(d, pool, a);
}
VKAPI_ATTR VkResult VKAPI_CALL ResetDescriptorPool(VkDevice d, VkDescriptorPool pool, VkDescriptorPoolResetFlags f) {
  forget_pool(pool);
  return g_next.ResetDescriptorPool(d, pool, f);
}
VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSets(VkDevice d, uint32_t wc, const VkWriteDescriptorSet* w, uint32_t cc,
                                                const VkCopyDescriptorSet* c) {
  g_next.UpdateDescriptorSets(d, wc, w, cc, c);
  if (t_game_descriptor_update || !g_any_set) return;
  std::lock_guard<std::mutex> lock(g_set_mu);
  for (uint32_t i = 0; i < wc; ++i) {
    auto it = g_sets.find(w[i].dstSet);
    if (it == g_sets.end()) continue;
    for (uint32_t k = 0; k < w[i].descriptorCount; ++k) {
      Desc e{w[i].descriptorType, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0};
      if (is_image_desc(e.type) && w[i].pImageInfo) {
        e.view = w[i].pImageInfo[k].imageView; e.layout = w[i].pImageInfo[k].imageLayout; e.sampler = w[i].pImageInfo[k].sampler;
      } else if (e.type == VK_DESCRIPTOR_TYPE_SAMPLER && w[i].pImageInfo) {
        e.sampler = w[i].pImageInfo[k].sampler;
      } else if (is_buffer_desc(e.type) && w[i].pBufferInfo) {
        e.buffer = w[i].pBufferInfo[k].buffer; e.offset = w[i].pBufferInfo[k].offset; e.range = w[i].pBufferInfo[k].range;
      } else {
        continue;
      }
      it->second.d[desc_key(w[i].dstBinding, w[i].dstArrayElement + k)] = e;
    }
  }
  for (uint32_t i = 0; i < cc; ++i) {
    auto src = g_sets.find(c[i].srcSet), dst = g_sets.find(c[i].dstSet);
    if (src == g_sets.end() || dst == g_sets.end()) continue;
    for (uint32_t k = 0; k < c[i].descriptorCount; ++k) {
      auto e = src->second.d.find(desc_key(c[i].srcBinding, c[i].srcArrayElement + k));
      if (e != src->second.d.end()) dst->second.d[desc_key(c[i].dstBinding, c[i].dstArrayElement + k)] = e->second;
      else dst->second.d.erase(desc_key(c[i].dstBinding, c[i].dstArrayElement + k));
    }
  }
}

// ---- the pack being captured / written ----
constexpr uint32_t kMagic = 0x52504B31u;   // "RPK1": the staging's first 16 bytes once the copies ran
constexpr VkDeviceSize kAlign = 16;        // bufferOffset of image copies: texel block (<= 16) and 4

// One captured image (all its descriptor uses share it): a transfer copy, or a shader read.
struct ImageCopy {
  VkImage image = VK_NULL_HANDLE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageAspectFlags copy_aspect = 0, barrier_aspect = 0;
  uint32_t pack_format = 0;               // the file's texel layout (depth as R32F / R16)
  uint32_t base_mip = 0, mips = 1, base_layer = 0, layers = 1, w = 1, h = 1;
  bool shader = false;                    // read by replay_image.hlsl (no TRANSFER_SRC, or a swizzled view)
  rp::ShaderRead read{};
  VkFormat view_format = VK_FORMAT_UNDEFINED;   // shader read: the game view's format and mapping
  VkComponentMapping comp{};
  VkImageView read_view = VK_NULL_HANDLE; // shader read: created after the budget
  size_t first_copy = 0, copies = 0;      // its pieces in Plan::copies
  uint64_t bytes = 0;
  uint32_t priority = 0;                  // rp::texture_priority, the highest of its uses
  bool droppable = true, dropped = false, view_failed = false;
};
struct Copy {
  enum Kind : uint8_t { kTransfer, kShader, kCpu, kImage } kind = kCpu;
  VkBuffer src = VK_NULL_HANDLE;
  VkDeviceSize src_offset = 0;            // kTransfer: in `src`; kShader: in `bound`
  VkDescriptorBufferInfo bound{};         // kShader: the source as bound by the copy shader
  std::vector<uint8_t> cpu;               // kCpu
  size_t image = SIZE_MAX;                // kImage: Plan::images, one piece of it:
  uint32_t mip = 0, layer = 0, layers = 0, row = 0, rows = 0;   //   transfer (rp::CopyPiece)
  uint64_t first = 0, count = 0;          //   shader read (rp::ReadPiece)
  uint64_t bytes = 0;
  uint32_t chunk = 0;
  VkDeviceSize offset = 0;                // in the chunk
  std::string file;
  uint64_t file_offset = 0;
  bool droppable = false, dropped = false;
  int set = -1;                           // the descriptor it fills (per-slot sizes); -1: by file
  uint32_t binding = 0;
};
struct Chunk { VkBuffer b = VK_NULL_HANDLE; VkDeviceMemory m = VK_NULL_HANDLE; uint8_t* map = nullptr; VkDeviceSize size = 0; };

struct Job {
  std::string label, dir;
  uint64_t record_frame = 0;
  rp::Manifest m;
  std::vector<Copy> copies;   // in file order (a file's pieces contiguous, rising file offsets)
  std::vector<Chunk> chunks;
  std::vector<VkImageView> views;   // the shader reads' views, released with the chunks
  uint64_t bytes = 0;
  uint32_t omitted = 0, instances = 0, read_images = 0;
  bool writing = false;
  std::atomic<bool> done{false};
  std::thread worker;
};
std::mutex g_job_mu;
std::unique_ptr<Job> g_job;
uint64_t g_last_trace = UINT64_MAX;

// The copy shader (shaders/replay_copy.hlsl), built on first use.
VkDescriptorSetLayout g_copy_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_copy_pl = VK_NULL_HANDLE;
VkPipeline g_copy_pipe = VK_NULL_HANDLE;

bool copy_pipeline() {
  if (g_copy_pipe) return true;
  const VkDeviceFuncs& vk = g_dd->vk;
  if (!vk.CmdPushDescriptorSet) return false;
  Bg3ShaderBlob blob{};
  if (!shader_get("replay_copy.hlsl", "main", "cs_6_6", &blob)) return false;
  if (!g_copy_dsl) {
    VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                         {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dci.bindingCount = 2;
    dci.pBindings = b;
    if (vk.CreateDescriptorSetLayout(g_dd->device, &dci, nullptr, &g_copy_dsl) != VK_SUCCESS) return false;
  }
  if (!g_copy_pl) {
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pci.setLayoutCount = 1; pci.pSetLayouts = &g_copy_dsl; pci.pushConstantRangeCount = 1; pci.pPushConstantRanges = &push;
    if (vk.CreatePipelineLayout(g_dd->device, &pci, nullptr, &g_copy_pl) != VK_SUCCESS) return false;
  }
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes; mci.pCode = blob.code;
  VkShaderModule mod = VK_NULL_HANDLE;
  if (vk.CreateShaderModule(g_dd->device, &mci, nullptr, &mod) != VK_SUCCESS) return false;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main"};
  ci.layout = g_copy_pl;
  const VkResult r = vk.CreateComputePipelines(g_dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &g_copy_pipe);
  vk.DestroyShaderModule(g_dd->device, mod, nullptr);
  if (r != VK_SUCCESS) g_copy_pipe = VK_NULL_HANDLE;
  return g_copy_pipe != VK_NULL_HANDLE;
}

// The image read shader (shaders/replay_image.hlsl): binding 0 the sampled 2D-array view, 1 the
// staging chunk; 32 bytes of push constants. Built on first use; without it such images are omitted.
VkDescriptorSetLayout g_read_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_read_pl = VK_NULL_HANDLE;
VkPipeline g_read_pipe = VK_NULL_HANDLE;
bool g_read_failed = false;

bool read_pipeline() {
  if (g_read_pipe) return true;
  if (g_read_failed) return false;
  const VkDeviceFuncs& vk = g_dd->vk;
  Bg3ShaderBlob blob{};
  bool ok = vk.CmdPushDescriptorSet && shader_get("replay_image.hlsl", "main", "cs_6_6", &blob);
  if (ok && !g_read_dsl) {
    VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                         {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dci.bindingCount = 2;
    dci.pBindings = b;
    ok = vk.CreateDescriptorSetLayout(g_dd->device, &dci, nullptr, &g_read_dsl) == VK_SUCCESS;
  }
  if (ok && !g_read_pl) {
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
    VkPipelineLayoutCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pci.setLayoutCount = 1; pci.pSetLayouts = &g_read_dsl; pci.pushConstantRangeCount = 1; pci.pPushConstantRanges = &push;
    ok = vk.CreatePipelineLayout(g_dd->device, &pci, nullptr, &g_read_pl) == VK_SUCCESS;
  }
  if (ok) {
    VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mci.codeSize = blob.size_bytes; mci.pCode = blob.code;
    VkShaderModule mod = VK_NULL_HANDLE;
    ok = vk.CreateShaderModule(g_dd->device, &mci, nullptr, &mod) == VK_SUCCESS;
    if (ok) {
      VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
      ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main"};
      ci.layout = g_read_pl;
      ok = vk.CreateComputePipelines(g_dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &g_read_pipe) == VK_SUCCESS;
      vk.DestroyShaderModule(g_dd->device, mod, nullptr);
    }
  }
  if (!ok) { g_read_pipe = VK_NULL_HANDLE; g_read_failed = true; log("[replay] the image read shader (shaders/replay_image.hlsl) did not build: images without TRANSFER_SRC are omitted"); }
  return ok;
}

void release_chunks(std::vector<Chunk>& chunks) {
  for (Chunk& c : chunks) {
    defer_destroy(BG3_DEFER_BUFFER, uint64_t(c.b));
    defer_destroy(BG3_DEFER_MEMORY, uint64_t(c.m));
  }
  chunks.clear();
}
void release_views(std::vector<VkImageView>& views) {
  for (VkImageView v : views) defer_destroy(BG3_DEFER_IMAGE_VIEW, uint64_t(v));
  views.clear();
}

bool depth_format(VkFormat f) {
  return f == VK_FORMAT_D16_UNORM || f == VK_FORMAT_X8_D24_UNORM_PACK32 || f == VK_FORMAT_D24_UNORM_S8_UINT ||
         f == VK_FORMAT_D32_SFLOAT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT;
}
bool stencil_format(VkFormat f) {
  return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT;
}

// Everything one pack copies, before the staging is allocated.
struct Plan {
  rp::Manifest m;
  std::vector<Copy> copies;
  std::vector<ImageCopy> images;
  std::map<VkImage, std::vector<size_t>> image_index;   // image -> its captures in Plan::images
  struct Use { uint32_t set, binding, element; size_t image; bool output; uint32_t format; };   // format: of this use's view
  std::vector<Use> uses;
  uint32_t omit_view = 0, omit_format = 0, omit_transfer = 0, omit_layout = 0, omit_size = 0, omit_budget = 0;
  std::map<uint32_t, uint32_t> omit_formats;   // VkFormat -> descriptor uses omitted for "format"
  std::vector<std::string> conflicts;          // "two views of one image": which fields differ (first few)
  bool can_read = false;                       // the image read shader built
  VkDeviceSize chunk_bytes = 0;
  std::string why;   // the failure
};

// One buffer descriptor's bytes [rel, rel + bytes) into `file` at `file_offset`, split into pieces
// a chunk holds. UBOs: a transfer copy or the host mapping; SSBOs: a transfer or shader copy.
bool add_buffer_bytes(Plan& p, const VkDescriptorBufferInfo& bi, bool ubo, VkDeviceSize rel, VkDeviceSize bytes,
                      const std::string& file, uint64_t file_offset, const char* what) {
  BufRec rec{};
  {
    std::shared_lock<std::shared_mutex> lock(g_obj_mu);
    auto it = g_bufs.find(bi.buffer);
    if (it != g_bufs.end()) rec = it->second;
  }
  const bool transfer = (rec.usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0;
  if (ubo && !transfer) {
    Copy c;
    c.kind = Copy::kCpu;
    c.cpu.resize(size_t(bytes));
    if (!read_mapped(bi.buffer, bi.offset + rel, c.cpu.data(), c.cpu.size())) {
      p.why = std::string(what) + " is a uniform buffer neither host-mapped nor a transfer source";
      return false;
    }
    c.bytes = bytes; c.file = file; c.file_offset = file_offset;
    p.copies.push_back(std::move(c));
    return true;
  }
  if (!transfer && ((rel | bytes) & 3)) {   // the shader copies words
    VkDeviceSize end = (rel + bytes) & ~VkDeviceSize(3);
    rel = (rel + 3) & ~VkDeviceSize(3);
    bytes = end > rel ? end - rel : 0;
  }
  for (VkDeviceSize done = 0; done < bytes;) {
    const VkDeviceSize n = (std::min)(bytes - done, p.chunk_bytes - kAlign);
    Copy c;
    c.kind = transfer ? Copy::kTransfer : Copy::kShader;
    c.src = bi.buffer;
    c.bound = bi;
    c.src_offset = transfer ? bi.offset + rel + done : rel + done;
    c.bytes = n; c.file = file; c.file_offset = file_offset + done;
    p.copies.push_back(std::move(c));
    done += n;
  }
  return true;
}

// The descriptor's bound bytes (rp::bound_bytes): only those are captured, never the whole VkBuffer.
VkDeviceSize resolve_range(const VkDescriptorBufferInfo& bi) {
  std::shared_lock<std::shared_mutex> lock(g_obj_mu);
  auto it = g_bufs.find(bi.buffer);
  if (it == g_bufs.end()) return bi.range == VK_WHOLE_SIZE ? 0 : bi.range;
  return rp::bound_bytes(it->second.size, bi.offset, bi.range == VK_WHOLE_SIZE ? rp::kWholeSize : bi.range);
}

void tag(Plan& p, size_t from, uint32_t set, uint32_t binding) {
  for (size_t k = from; k < p.copies.size(); ++k) { p.copies[k].set = int(set); p.copies[k].binding = binding; }
}

std::string slot_file(uint32_t set, uint32_t binding, const char* ext) {
  return "s" + std::to_string(set) + "b" + std::to_string(binding) + ext;
}

// A whole buffer descriptor: a `buffer` line with its file.
bool add_buffer(Plan& p, uint32_t set, uint32_t binding, VkDescriptorBufferInfo bi, bool ubo, const char* what) {
  bi.range = resolve_range(bi);
  if (!bi.buffer || !bi.range) { p.m.notes.push_back(" s" + std::to_string(set) + " b" + std::to_string(binding) + " (" + what + "): no buffer bound"); return true; }
  const std::string file = slot_file(set, binding, ".bin");
  p.m.buffers.push_back({set, binding, uint64_t(bi.range), file});
  const size_t before = p.copies.size();
  const bool ok = add_buffer_bytes(p, bi, ubo, 0, bi.range, file, 0, what);
  tag(p, before, set, binding);
  return ok;
}

bool readable_layout(VkImageLayout l) {
  return l == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || l == VK_IMAGE_LAYOUT_GENERAL || l == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
}
bool identity(const VkComponentMapping& c) {
  auto id = [](VkComponentSwizzle s, VkComponentSwizzle own) { return s == VK_COMPONENT_SWIZZLE_IDENTITY || s == own; };
  return id(c.r, VK_COMPONENT_SWIZZLE_R) && id(c.g, VK_COMPONENT_SWIZZLE_G) && id(c.b, VK_COMPONENT_SWIZZLE_B) && id(c.a, VK_COMPONENT_SWIZZLE_A);
}

// One image descriptor element. Returns false only for a hard failure (set 0); an image set 1
// cannot carry is "omitted". A transfer copy when the image has TRANSFER_SRC and the view shows
// its texels as stored; else (sampled colour views) a shader read, stored decoded.
bool add_image(Plan& p, uint32_t set, uint32_t binding, uint32_t element, VkImageView view, VkImageLayout layout, const rp::Slot& slot,
               bool output, bool droppable) {
  const std::string name = std::string(slot.name) + " (s" + std::to_string(set) + " b" + std::to_string(binding) + ")";
  auto omit = [&](uint32_t& counter, const char* why) {
    if (!droppable) { p.why = name + ": " + why; return false; }
    ++counter;
    p.m.omitted.push_back({set, binding, element});
    return true;
  };
  ViewInfo v{};
  ImgRec im{};
  {
    std::shared_lock<std::shared_mutex> lock(g_obj_mu);
    auto vi = view ? g_views.find(view) : g_views.end();
    if (vi == g_views.end()) return omit(p.omit_view, "view not recorded");
    v = vi->second;
    auto ii = g_imgs.find(v.image);
    if (ii == g_imgs.end()) return omit(p.omit_view, "image not recorded");
    im = ii->second;
  }
  auto omit_format = [&](const char* why) { ++p.omit_formats[uint32_t(v.format)]; return omit(p.omit_format, why); };
  if (im.type != VK_IMAGE_TYPE_2D || im.d != 1) return omit_format("not a 2D image");
  const bool depth = depth_format(im.format), transfer = (im.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
  uint32_t pack_format = uint32_t(v.format);
  bool shader = false;
  rp::ShaderRead read{};
  if (depth) {
    if (im.format == VK_FORMAT_D32_SFLOAT || im.format == VK_FORMAT_D32_SFLOAT_S8_UINT) pack_format = rp::kR32Sfloat;
    else if (im.format == VK_FORMAT_D16_UNORM || im.format == VK_FORMAT_D16_UNORM_S8_UINT) pack_format = 70;   // R16_UNORM
    else return omit_format("24-bit depth");
    if (!transfer) return omit(p.omit_transfer, "depth without TRANSFER_SRC usage");
  } else {
    uint32_t block = 1, copy_block = 1;
    const uint32_t vb = rp::block_bytes(uint32_t(v.format), block), ib = rp::block_bytes(uint32_t(im.format), copy_block);
    const bool swizzled = !identity(v.comp);
    if (!transfer || swizzled || !vb || vb != ib || block != copy_block) {
      // Storage images (set 0 outputs) are only ever copied: the read needs SAMPLED usage.
      if (slot.kind == rp::kStorage)
        return transfer ? omit_format("storage format the pack does not carry") : omit(p.omit_transfer, "no TRANSFER_SRC usage");
      if (!(im.usage & VK_IMAGE_USAGE_SAMPLED_BIT)) return omit(p.omit_transfer, "neither TRANSFER_SRC nor SAMPLED usage");
      if (!p.can_read) return omit(p.omit_transfer, "no TRANSFER_SRC usage and no image read shader");
      if (!rp::shader_read_format(uint32_t(v.format), swizzled, read)) return omit_format("format the shader read does not carry");
      if (!readable_layout(layout)) return omit(p.omit_layout, "not in a shader-readable layout");
      shader = true;
      pack_format = read.pack_format;
    }
  }
  if (slot.kind == rp::kStorage && pack_format != slot.storage_format) return omit_format("storage format differs from the shader's");
  const uint32_t base_mip = v.range.baseMipLevel, base_layer = v.range.baseArrayLayer;
  if (base_mip >= im.mips || base_layer >= im.layers) return omit(p.omit_view, "view outside its image");
  uint32_t mips = v.range.levelCount == VK_REMAINING_MIP_LEVELS ? im.mips - base_mip : (std::min)(v.range.levelCount, im.mips - base_mip);
  uint32_t layers = v.range.layerCount == VK_REMAINING_ARRAY_LAYERS ? im.layers - base_layer : (std::min)(v.range.layerCount, im.layers - base_layer);
  if (set == 0 || slot.kind == rp::kStorage) mips = 1;   // set 0 reads mip 0 (Load / SampleLevel 0)
  if (slot.kind == rp::kSampledCube) {   // lighting set 2: the cube's 6 faces (a cube view; the read uses a 2D-array view of them)
    if (v.type != VK_IMAGE_VIEW_TYPE_CUBE || layers < 6) return omit_format("not a cube view");
    layers = 6;
  } else if (slot.kind != rp::kSampled2DArray) {
    layers = 1;
  }
  const uint32_t w = (std::max)(im.w >> base_mip, 1u), h = (std::max)(im.h >> base_mip, 1u);
  const uint32_t priority = rp::texture_priority(set, binding);
  // An image already captured: the same capture when this view stores the same bytes (same range;
  // a transfer copy of a raw-compatible format, e.g. BC7 UNORM / SRGB views, each use keeps its own
  // format; a shader read of the same format and mapping). Another shader read of the same range
  // (other format or mapping) is a capture of its own. Other differences stay "omitted" (a second
  // transfer range would need its own layout transitions) and are listed in a manifest note.
  if (auto known = p.image_index.find(v.image); known != p.image_index.end()) {
    bool own_read = false;
    for (size_t idx : known->second) {
      ImageCopy& ic = p.images[idx];
      if (ic.layout != layout) return omit(p.omit_layout, "bound in two layouts");
      const bool same_range = ic.base_mip == base_mip && ic.mips == mips && ic.base_layer == base_layer && ic.layers == layers;
      const bool same_bytes = same_range && ic.shader == shader &&
                              (shader ? ic.view_format == v.format && std::memcmp(&ic.comp, &v.comp, sizeof v.comp) == 0
                                      : rp::raw_compatible(ic.pack_format, pack_format));
      if (same_bytes) {
        ic.droppable = ic.droppable && droppable;
        ic.priority = (std::max)(ic.priority, priority);
        for (size_t k = 0; k < ic.copies; ++k) p.copies[ic.first_copy + k].droppable = ic.droppable;
        p.uses.push_back({set, binding, element, idx, output, pack_format});
        return true;
      }
      if (same_range && shader && ic.shader) { own_read = true; continue; }
      if (p.conflicts.size() < 6) {
        char d[256];
        const Copy& first = p.copies[ic.first_copy];
        snprintf(d, sizeof d, "s%ub%u[%u] vs the capture for s%db%u: mips %u+%u/%u+%u layers %u+%u/%u+%u view format %u/%u %s/%s", set,
                 binding, element, first.set, first.binding, base_mip, mips, ic.base_mip, ic.mips, base_layer, layers, ic.base_layer, ic.layers,
                 uint32_t(v.format), uint32_t(ic.view_format), shader ? "read" : "copy", ic.shader ? "read" : "copy");
        p.conflicts.push_back(d);
      }
      return omit(p.omit_layout, "two views of one image");
    }
    if (!own_read) return omit(p.omit_layout, "two views of one image");
  }
  ImageCopy ic{};
  ic.image = v.image; ic.layout = layout; ic.pack_format = pack_format;
  ic.copy_aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
  const bool separate = layout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL || layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  ic.barrier_aspect = depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | (stencil_format(im.format) && !separate ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u))
                            : VK_IMAGE_ASPECT_COLOR_BIT;
  ic.base_mip = base_mip; ic.mips = mips; ic.base_layer = base_layer; ic.layers = layers; ic.w = w; ic.h = h;
  ic.shader = shader; ic.read = read; ic.view_format = v.format; ic.comp = v.comp;
  ic.bytes = rp::image_bytes(pack_format, w, h, layers, mips);
  ic.priority = priority; ic.droppable = droppable;
  // Pieces of at most a staging chunk (a shader read's last word is rounded up to 4 bytes).
  const uint64_t max_piece = p.chunk_bytes - 2 * kAlign;
  const std::string file = output ? slot_file(set, binding, ".live") : "img" + std::to_string(p.images.size()) + ".bin";
  std::vector<Copy> pieces;
  auto piece = [&](uint64_t file_offset, uint64_t bytes) {
    Copy c;
    c.kind = Copy::kImage;
    c.image = p.images.size();
    c.bytes = bytes; c.file = file; c.file_offset = file_offset;
    c.droppable = droppable;
    c.set = int(set); c.binding = binding;
    return c;
  };
  if (shader) {
    for (const rp::ReadPiece& r : rp::shader_read_pieces(read, w, h, layers, mips, max_piece)) {
      Copy c = piece(r.file_offset, r.bytes);
      c.mip = r.mip; c.first = r.first; c.count = r.count;
      pieces.push_back(std::move(c));
    }
  } else {
    for (const rp::CopyPiece& r : rp::transfer_pieces(pack_format, w, h, layers, mips, max_piece)) {
      Copy c = piece(r.file_offset, r.bytes);
      c.mip = r.mip; c.layer = r.layer; c.layers = r.layers; c.row = r.row; c.rows = r.rows;
      pieces.push_back(std::move(c));
    }
  }
  uint64_t sum = 0;
  for (const Copy& c : pieces) sum += c.bytes;
  if (!ic.bytes || pieces.empty() || sum != ic.bytes) return omit(p.omit_size, "no piece plan within a staging chunk");
  ic.first_copy = p.copies.size(); ic.copies = pieces.size();
  for (Copy& c : pieces) p.copies.push_back(std::move(c));
  p.image_index[v.image].push_back(p.images.size());
  p.images.push_back(ic);
  p.uses.push_back({set, binding, element, p.images.size() - 1, output, pack_format});
  return true;
}

bool add_sampler(Plan& p, uint32_t set, uint32_t binding, uint32_t element, VkSampler s) {
  SamplerRec r{};
  {
    std::shared_lock<std::shared_mutex> lock(g_obj_mu);
    auto it = s ? g_samplers.find(s) : g_samplers.end();
    if (it == g_samplers.end()) return false;
    r = it->second;
  }
  p.m.samplers.push_back({set, binding, element, r.mag, r.min, r.mip, r.au, r.av, r.aw, r.aniso, r.max_lod});
  return true;
}
void add_sampler(Plan& p, uint32_t binding, uint32_t element, VkSampler s) { add_sampler(p, 1, binding, element, s); }

std::string mb(uint64_t bytes) { char b[32]; snprintf(b, sizeof b, "%.1f", double(bytes) / (1024.0 * 1024.0)); return b; }

// MB per descriptor slot (all its pieces and elements), largest first: "s1b6 gOpaqueSoup 236.8, ...".
std::string slot_sizes(const Plan& p, const rp::Pass& pass, bool fixed_only) {
  std::map<std::string, uint64_t> by;
  for (const Copy& c : p.copies) {
    if (c.dropped || (fixed_only && c.droppable)) continue;
    std::string k = c.file;
    if (c.set >= 0) {
      const rp::Slot* sl = rp::find_slot(pass, uint32_t(c.set), c.binding);
      k = "s" + std::to_string(c.set) + "b" + std::to_string(c.binding) + " " + (sl ? sl->name : "?");
    }
    by[k] += c.bytes;
  }
  std::vector<std::pair<uint64_t, std::string>> v;
  for (const auto& [k, n] : by) v.push_back({n, k});
  std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::string out;
  uint64_t rest = 0;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i >= 12) { rest += v[i].first; continue; }
    out += (out.empty() ? "" : ", ") + v[i].second + " " + mb(v[i].first);
  }
  if (rest) out += ", others " + mb(rest);
  return out + " MB";
}

// ---- sets 2-3: the deferred lighting's inputs (rp::kLightingSources) ----
// The core records the pack in its pre-lighting hook (core/gi.cpp run_pre -> hit_gi_dispatch), where
// the game has bound the tiled lighting's sets on the compute bind point and the core's own binds
// are not tracked (it restores the game's afterwards), so query_binding / query_set see the lighting's
// descriptors: its set 1 (probes, cubes, DFG LUT, G-buffer, AO before the RT AO fold, which runs
// later in run_pre) and set 5 (atmosphere). Every input is optional and droppable: an absent one is a
// manifest note, never a failed pack. The local probe cubes (set 1 b45, bindless) are those the
// frame's probe records name when the records are host-readable (rp::probe_cube_elements), else every
// cube element bound (2D cookie elements of the same binding are skipped).
struct LightingStats { uint32_t slots = 0, bound = 0, cubes_named = 0, cubes_bound = 0, cube_elems = 0, beyond = 0, records = 0; bool records_read = false;
                       uint32_t samplers_game = 0, samplers_declared = 0; std::string absent; };
LightingStats add_lighting(Plan& p, VkCommandBuffer cmd, const rp::Pass& pass) {
  LightingStats st;
  rp::Manifest& m = p.m;
  auto absent = [&](const rp::Slot& s, const char* why) {
    st.absent += std::string(st.absent.empty() ? "" : ", ") + "s" + std::to_string(s.set) + "b" + std::to_string(s.binding) + " " + s.name + " (" + why + ")";
  };
  VkDescriptorSet game1 = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    uint32_t dyn[64], n = 64;
    if (!query_set(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, 1, &game1, &layout, dyn, &n)) game1 = VK_NULL_HANDLE;
  }
  std::vector<uint32_t> named;   // b45 elements the probe records name
  const rp::Slot* cube_slot = nullptr;
  for (uint32_t i = 0; i < pass.slot_count; ++i) {
    const rp::Slot& s = pass.slots[i];
    if (s.set < 2) continue;
    ++st.slots;
    if (s.set == 3 && s.binding == 6) {   // ours: linear clamp (core/gi.cpp write_sun_set)
      m.samplers.push_back({3, 6, 0, 1, 1, 1, 2, 2, 2, 0, 1000});
      ++st.bound; ++st.samplers_declared;
      continue;
    }
    const rp::LightingSource* src = rp::lighting_source(s.set, s.binding);
    if (!src) { absent(s, "no source"); continue; }
    if (s.kind == rp::kSampledCube && s.count > 1) { cube_slot = &s; continue; }   // b45 below
    if (s.kind == rp::kSampler) {
      VkSampler sampler = VK_NULL_HANDLE;
      if (game1) {
        std::shared_lock<std::shared_mutex> lock(tracker().mu);
        auto it = tracker().sets.find(game1);
        if (it != tracker().sets.end()) {
          auto e = it->second.find(desc_key(src->game_binding, 0));
          if (e != it->second.end()) sampler = e->second.sampler;
        }
      }
      if (add_sampler(p, s.set, s.binding, 0, sampler)) { ++st.bound; ++st.samplers_game; continue; }
      // Not recorded (immutable in the game's layout): declared trilinear clamp, the state the
      // probes' cube sampling and the LUT lookups assume (tools/frame_replay.md "Lighting inputs").
      m.samplers.push_back({s.set, s.binding, 0, 1, 1, 1, 2, 2, 2, 0, 1000});
      ++st.bound; ++st.samplers_declared;
      continue;
    }
    Bg3Binding b{};
    if (!query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, src->game_set, src->game_binding, 0, &b)) { absent(s, "not bound"); continue; }
    if (s.kind == rp::kUbo || s.kind == rp::kSsbo) {
      if (!b.buffer) { absent(s, "no buffer"); continue; }
      const size_t buffers = m.buffers.size(), copies = p.copies.size(), notes = m.notes.size();
      if (!add_buffer(p, s.set, s.binding, {b.buffer, b.offset, b.range}, s.kind == rp::kUbo, s.name)) {
        m.buffers.resize(buffers); p.copies.resize(copies); m.notes.resize(notes);   // nothing half-captured
        absent(s, p.why.c_str()); p.why.clear();
        continue;
      }
      ++st.bound;
      if (s.set == 2 && s.binding == 33 && b.range) {
        std::vector<uint8_t> records(size_t(b.range));
        st.records = uint32_t(b.range / rp::kProbeStride);
        if (read_mapped(b.buffer, b.offset, records.data(), records.size())) {
          st.records_read = true;
          named = rp::probe_cube_elements(records.data(), records.size(), 0xffffffffu, st.beyond);
        }
      }
      continue;
    }
    if (!b.view) { absent(s, "no view"); continue; }
    const size_t omitted = m.omitted.size();
    if (!add_image(p, s.set, s.binding, 0, b.view, b.layout, s, false, true)) { absent(s, p.why.c_str()); p.why.clear(); continue; }
    if (m.omitted.size() > omitted) absent(s, "omitted"); else ++st.bound;
  }
  // b45: the bindless probe cubes.
  if (cube_slot) {
    std::vector<std::pair<uint32_t, DescEntry>> elems;
    if (game1) {
      std::shared_lock<std::shared_mutex> lock(tracker().mu);
      auto it = tracker().sets.find(game1);
      if (it != tracker().sets.end())
        for (const auto& [k, e] : it->second)
          if (uint32_t(k >> 32) == cube_slot->binding && e.view) elems.push_back({uint32_t(k), e});
    }
    std::sort(elems.begin(), elems.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::set<uint32_t> want(named.begin(), named.end());
    const size_t omitted = m.omitted.size();
    for (const auto& [el, e] : elems) {
      bool cube = false;
      {
        std::shared_lock<std::shared_mutex> lock(g_obj_mu);
        auto v = g_views.find(e.view);
        cube = v != g_views.end() && v->second.type == VK_IMAGE_VIEW_TYPE_CUBE;
      }
      if (!cube) continue;   // a projector cookie (2D) in the same bindless binding
      ++st.cube_elems;
      if (st.records_read && !want.count(el)) continue;
      if (el >= cube_slot->count) { ++st.beyond; continue; }
      if (!add_image(p, 2, cube_slot->binding, el, e.view, e.layout, *cube_slot, false, true)) { p.why.clear(); continue; }
      ++st.cubes_bound;
    }
    st.cubes_bound -= (std::min)(st.cubes_bound, uint32_t(m.omitted.size() - omitted));
    st.cubes_named = st.records_read ? uint32_t(named.size()) : st.cube_elems;
    if (st.cubes_bound || !st.records_read) ++st.bound; else absent(*cube_slot, "no cube named by the records");
  }
  if (!game1) st.absent += std::string(st.absent.empty() ? "" : "; ") + "the lighting's set 1 is not a tracked descriptor set";
  return st;
}

// The worker: writes every file of the pack from the staging, then the manifest.
void write_pack(Job* job) {
  bool ok = !job->chunks.empty();
  if (ok) {
    const uint32_t* marker = reinterpret_cast<const uint32_t*>(job->chunks[0].map);
    ok = marker[0] == kMagic && marker[1] == kMagic && marker[2] == kMagic && marker[3] == kMagic;
    if (!ok) log("[replay] pack %s failed: the capture command buffer never ran (no marker)", job->label.c_str());
  }
  if (ok) {
    make_dirs(job->dir);
    std::ofstream f;
    std::string open_file;
    uint64_t at = 0;
    for (const Copy& c : job->copies) {
      if (c.dropped) continue;
      if (c.file != open_file) {
        if (f.is_open()) f.close();
        f.open(job->dir + "\\" + c.file, std::ios::binary | std::ios::trunc);
        open_file = c.file;
        at = 0;
      }
      if (c.file_offset != at) f.seekp(std::streamoff(c.file_offset));
      const uint8_t* src = c.kind == Copy::kCpu ? c.cpu.data() : job->chunks[c.chunk].map + c.offset;
      f.write(reinterpret_cast<const char*>(src), std::streamsize(c.bytes));
      at = c.file_offset + c.bytes;
      ok = ok && bool(f);
    }
    if (f.is_open()) f.close();
    const std::string text = rp::serialize(job->m);
    rp::Manifest back;
    std::string error;
    if (!rp::parse(text, back, error)) { ok = false; log("[replay] pack %s failed: manifest does not parse (%s)", job->label.c_str(), error.c_str()); }
    ok = ok && write_file(job->dir + "\\manifest.txt", text.data(), text.size());
    if (ok)
      log("[replay] pack %s written: %s MB (%u images omitted, %u read by shader), frame %llu, pass %s, instances %u -> %s", job->label.c_str(),
          mb(job->bytes).c_str(), job->omitted, job->read_images, (unsigned long long)job->m.frame, job->m.pass.c_str(), job->instances,
          job->dir.c_str());
    else
      log("[replay] pack %s failed: writing %s", job->label.c_str(), job->dir.c_str());
  }
  job->done = true;
}

}  // namespace

void replay_device_created(DeviceData* dd) {
  if (g_dd || !dd) return;
  g_dd = dd;
  g_next = dd->vk;
#define WRAP(n) if (g_next.n) dd->vk.n = &n;
  WRAP(CreateImage) WRAP(DestroyImage) WRAP(CreateImageView) WRAP(DestroyImageView) WRAP(CreateSampler) WRAP(DestroySampler)
  WRAP(CreateBuffer) WRAP(DestroyBuffer) WRAP(AllocateDescriptorSets) WRAP(FreeDescriptorSets) WRAP(DestroyDescriptorPool)
  WRAP(ResetDescriptorPool) WRAP(UpdateDescriptorSets)
#undef WRAP
}

void replay_note_alpha_layout(VkDescriptorSetLayout layout, VkSampler wrap, VkSampler clamp) {
  g_wrap = wrap;
  g_clamp = clamp;
  g_alpha_layout = layout;
}

void replay_device_destroyed(DeviceData* dd) {
  if (dd != g_dd) return;
  std::lock_guard<std::mutex> lock(g_job_mu);
  if (g_job) {
    if (g_job->worker.joinable()) g_job->worker.join();
    release_chunks(g_job->chunks);
    release_views(g_job->views);
    g_job.reset();
  }
  const VkDeviceFuncs& vk = g_dd->vk;
  if (g_copy_pipe) vk.DestroyPipeline(g_dd->device, g_copy_pipe, nullptr);
  if (g_copy_pl) vk.DestroyPipelineLayout(g_dd->device, g_copy_pl, nullptr);
  if (g_copy_dsl) vk.DestroyDescriptorSetLayout(g_dd->device, g_copy_dsl, nullptr);
  g_copy_pipe = VK_NULL_HANDLE; g_copy_pl = VK_NULL_HANDLE; g_copy_dsl = VK_NULL_HANDLE;
  if (g_read_pipe) vk.DestroyPipeline(g_dd->device, g_read_pipe, nullptr);
  if (g_read_pl) vk.DestroyPipelineLayout(g_dd->device, g_read_pl, nullptr);
  if (g_read_dsl) vk.DestroyDescriptorSetLayout(g_dd->device, g_read_dsl, nullptr);
  g_read_pipe = VK_NULL_HANDLE; g_read_pl = VK_NULL_HANDLE; g_read_dsl = VK_NULL_HANDLE; g_read_failed = false;
}

void replay_on_present() {
  std::lock_guard<std::mutex> lock(g_job_mu);
  if (!g_job) return;
  Job& job = *g_job;
  if (job.writing) {
    if (!job.done) return;
    if (job.worker.joinable()) job.worker.join();
    release_chunks(job.chunks);
    release_views(job.views);
    g_job.reset();
    return;
  }
  if (life_gpu_done() >= job.record_frame + 2) {   // the frame it was recorded in (or the next) has run
    job.writing = true;
    job.worker = std::thread(write_pack, &job);
  } else if (frame_index() > job.record_frame + 900) {
    log("[replay] pack %s failed: frame %llu has not completed on the GPU after 900 presents", job.label.c_str(),
        (unsigned long long)job.record_frame);
    release_chunks(job.chunks);
    release_views(job.views);
    g_job.reset();
  }
}

int replay_pack(VkCommandBuffer cmd, const Bg3ReplayDispatch* d) {
  if (!g_dd || !d || !cmd) return -1;
  std::lock_guard<std::mutex> job_lock(g_job_mu);
  if (g_job) return 0;   // the previous pack is still being written
  std::string label;
  if (d->serial > 0) {
    label = std::to_string(d->serial);
  } else {
    if (!trace_recording() || trace_start_frame() == g_last_trace) return 0;
    g_last_trace = trace_start_frame();
    label = "trace-" + std::to_string(g_last_trace);
  }
  auto fail = [&](const std::string& why) {
    log("[replay] pack %s failed: %s", label.c_str(), why.c_str());
    return -1;
  };
  const rp::Pass* pass = rp::find_pass(d->pass ? d->pass : "");
  if (!pass) return fail(std::string("no pack contract for pass '") + (d->pass ? d->pass : "") + "'");
  if (d->push_bytes != pass->push_bytes || !d->push) return fail("push constants are not " + std::to_string(pass->push_bytes) + " bytes");
  if (!copy_pipeline()) return fail("the copy shader (shaders/replay_copy.hlsl) did not build");
  Plan p;
  p.can_read = read_pipeline();
  {
    VkPhysicalDeviceProperties props{};
    if (g_dd->inst && g_dd->inst->GetPhysicalDeviceProperties) g_dd->inst->GetPhysicalDeviceProperties(g_dd->physical, &props);
    const VkDeviceSize limit = props.limits.maxStorageBufferRange ? props.limits.maxStorageBufferRange : (VkDeviceSize(1) << 27);
    p.chunk_bytes = (std::min)(VkDeviceSize(256) << 20, limit) & ~(kAlign - 1);
  }
  rp::Manifest& m = p.m;
  m.version = rp::kVersion;
  m.pass = pass->name;
  {
    std::string dropped;
    m.defines = rp::contract_defines(d->defines ? d->defines : "", &dropped);
    if (!dropped.empty()) m.notes.push_back(" live variant defines not in the pack contract (the replay compiles without them): " + dropped);
  }
  m.frame = d->frame;
  m.groups[0] = d->groups[0]; m.groups[1] = d->groups[1]; m.groups[2] = d->groups[2];
  m.push = "push.bin";
  {
    Copy c;
    c.kind = Copy::kCpu;
    c.cpu.assign(static_cast<const uint8_t*>(d->push), static_cast<const uint8_t*>(d->push) + d->push_bytes);
    c.bytes = d->push_bytes; c.file = "push.bin";
    p.copies.push_back(std::move(c));
  }
  auto is_output = [&](uint32_t b) { for (uint32_t i = 0; i < d->output_count; ++i) if (d->outputs[i] == b) return true; return false; };

  // ---- set 0, as pushed ----
  for (uint32_t i = 0; i < d->set0_count; ++i) {
    const VkWriteDescriptorSet& w = d->set0[i];
    const rp::Slot* slot = rp::find_slot(*pass, 0, w.dstBinding);
    if (!slot) { m.notes.push_back(" s0 b" + std::to_string(w.dstBinding) + ": not in the pack contract, left out"); continue; }
    if (slot->kind == rp::kTlas) continue;
    if (w.dstBinding == 20) { m.notes.push_back(" s0 b20 (rtStats) not captured: the replay counts into its own buffer"); continue; }
    if (is_buffer_desc(w.descriptorType)) {
      if (!w.pBufferInfo || !add_buffer(p, 0, w.dstBinding, w.pBufferInfo[0], w.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, slot->name))
        return fail(p.why.empty() ? std::string(slot->name) + ": no buffer info" : p.why);
    } else if (is_image_desc(w.descriptorType)) {
      if (!w.pImageInfo || !add_image(p, 0, w.dstBinding, 0, w.pImageInfo[0].imageView, w.pImageInfo[0].imageLayout, *slot,
                                      is_output(w.dstBinding), false))
        return fail(p.why.empty() ? std::string(slot->name) + ": no image info" : p.why);
    }
  }

  // ---- set 1: the material set as the layer wrote it ----
  std::unordered_map<uint64_t, Desc> s1;
  {
    std::lock_guard<std::mutex> lock(g_set_mu);
    auto it = g_sets.find(d->set1);
    if (it == g_sets.end()) return fail("set 1 is not a recorded material set");
    s1 = it->second.d;
  }
  auto entry = [&](uint32_t b, uint32_t e) -> const Desc* { auto it = s1.find(desc_key(b, e)); return it == s1.end() ? nullptr : &it->second; };
  const Desc* info = entry(7, 0);
  if (!info || !info->buffer) return fail("set 1 b7 (InstInfo) not recorded");
  BvhReplayRegion region;
  if (!rt_replay_region(info->buffer, info->offset, &region)) return fail("set 1 b7 is not a scene ring region of the split BVH");
  std::vector<rp::Span> spans[2];   // b6 soup, b8 pool
  rp::instinfo_spans(region.info.data(), region.count, spans[0], spans[1]);
  // Set-1 buffers bound into the soup or pool VkBuffer (split BVH: b1 gAlphaSoup binds the whole
  // soup, as b6) reuse the geometry capture's slices below instead of a whole copy of their own.
  const Desc* geo_desc[2] = {entry(6, 0), entry(8, 0)};
  struct Alias { uint32_t binding; const char* name; VkDescriptorBufferInfo bi; int geo; };
  std::vector<Alias> aliases;
  for (uint32_t s = 0; s < pass->slot_count; ++s) {
    const rp::Slot& slot = pass->slots[s];
    if (slot.set != 1) continue;
    if (slot.kind == rp::kSsbo && slot.binding != 6 && slot.binding != 8) {
      const Desc* e = entry(slot.binding, 0);
      if (!e || !e->buffer) { m.notes.push_back(" s1 b" + std::to_string(slot.binding) + " (" + slot.name + "): not recorded, bound zero"); continue; }
      int geo = -1;
      for (int g = 0; g < 2; ++g) if (geo_desc[g] && geo_desc[g]->buffer == e->buffer) geo = g;
      if (geo >= 0) { aliases.push_back({slot.binding, slot.name, {e->buffer, e->offset, e->range}, geo}); continue; }
      if (!add_buffer(p, 1, slot.binding, {e->buffer, e->offset, e->range}, false, slot.name)) return fail(p.why);
    } else if (slot.kind == rp::kSampled2D || slot.kind == rp::kSampled2DArray) {
      for (uint32_t el = 0; el < slot.count; ++el)
        if (const Desc* e = entry(slot.binding, el); e && e->view)
          if (!add_image(p, 1, slot.binding, el, e->view, e->layout, slot, false, true)) return fail(p.why);
    } else if (slot.kind == rp::kSampler) {
      if (slot.binding == 4) add_sampler(p, 4, 0, g_wrap);        // immutable in the layout (alphatest.cpp)
      else if (slot.binding == 5) add_sampler(p, 5, 0, g_clamp);
      else for (uint32_t el = 0; el < slot.count; ++el) if (const Desc* e = entry(slot.binding, el)) add_sampler(p, slot.binding, el, e->sampler);
    }
  }
  // ---- sets 2-3: the deferred lighting's inputs (add_lighting; tools/frame_replay.md "Lighting inputs") ----
  LightingStats lighting;
  if (pass->set_count > 2) lighting = add_lighting(p, cmd, *pass);
  // The TLAS instance array (InstInfo order) of the same ring region.
  m.instances = "instances.bin";
  m.instance_count = region.count;
  if (!add_buffer_bytes(p, {region.ring, region.instances_offset, VkDeviceSize(region.count) * 64}, false, 0,
                        VkDeviceSize(region.count) * 64, "instances.bin", 0, "the instance ring"))
    return fail(p.why);   // untagged: counted as "instances.bin"
  // rt_cascade_casters: the cascade instance's triangles (its InstInfo has none; the BLAS was built from
  // the converted cascade soup, layer/bvhsplit.cpp), so the replay's TLAS has the cut-away roofs. Only
  // while the soup's ring slot still holds that frame's conversion (geo_cascade_soup: same buffer, draw
  // list, triangles); else it is left out with a note (the replay then leaves the instance out, mask 0).
  std::string casters_note;
  if (region.casc_tris) {
    CascadeSoup now{};
    const int why = geo_cascade_soup(region.casc_frame, &now);
    const uint64_t bytes = rp::caster_bytes(region.casc_tris, region.casc_stride);
    if (why || now.xfb != region.casc_xfb || now.sig != region.casc_sig || now.tris != region.casc_tris || now.stride != region.casc_stride) {
      casters_note = " casters: instance " + std::to_string(region.casc_instance) + " (" + std::to_string(region.casc_tris) +
                     " triangles) left out: the cascade soup of frame " + std::to_string(region.casc_frame) + " was replaced (" + std::to_string(why) + ")";
    } else {
      const size_t before = p.copies.size();
      if (!add_buffer_bytes(p, {region.casc_xfb, 0, VkDeviceSize(bytes)}, false, 0, VkDeviceSize(bytes), "casters.bin", 0, "the cascade soup"))
        return fail(p.why);
      uint64_t got = 0;
      for (size_t k = before; k < p.copies.size(); ++k) got += p.copies[k].bytes;
      if (got != bytes) return fail("the cascade soup copy is not whole words");
      m.casters = {"casters.bin", region.casc_instance, region.casc_tris, region.casc_stride};
      casters_note = " casters: instance " + std::to_string(region.casc_instance) + ", " + std::to_string(region.casc_tris) +
                     " triangles of stride " + std::to_string(region.casc_stride) + ", " + mb(bytes) + " MB (soup of frame " +
                     std::to_string(region.casc_frame) + ")";
    }
    m.notes.push_back(casters_note);
    log("[replay] pack %s%s", label.c_str(), casters_note.c_str());
  }

  // ---- geometry (b6, b8): whole when it fits the budget, else the InstInfo spans ----
  uint64_t fixed = 0;
  for (const Copy& c : p.copies) if (!c.droppable) fixed += c.bytes;
  std::string geometry;
  const uint32_t geo_binding[2] = {6, 8};
  VkDescriptorBufferInfo geo_bi[2]{};
  std::vector<rp::Range> geo_slices[2];   // what each geometry binding captured, as file slices
  for (int g : {1, 0}) {   // the pool first: its basis rows (pool-material hits) live outside the InstInfo spans
    const uint32_t b = geo_binding[g];
    const Desc* e = entry(b, 0);
    if (!e || !e->buffer) { m.notes.push_back(" s1 b" + std::to_string(b) + ": not recorded, bound zero"); continue; }
    VkDescriptorBufferInfo bi{e->buffer, e->offset, e->range};
    bi.range = resolve_range(bi);
    geo_bi[g] = bi;
    auto& v = spans[g];
    for (auto& sp : v) { if (sp.offset >= bi.range) sp.bytes = 0; else sp.bytes = (std::min)(sp.bytes, bi.range - sp.offset); }
    rp::merge_spans(v, 4096);
    const uint64_t sparse = rp::span_bytes(v);
    const char* name = g ? "pool" : "soup";
    if (fixed + bi.range <= rp::kPackBudgetBytes) {
      geometry += std::string(geometry.empty() ? "" : ", ") + name + " whole " + mb(bi.range) + " MB";
      if (!add_buffer(p, 1, b, bi, false, name)) return fail(p.why);
      fixed += bi.range;
      if (bi.range) geo_slices[g].push_back({1, b, 0, slot_file(1, b, ".bin"), 0, uint64_t(bi.range)});
    } else {
      geometry += std::string(geometry.empty() ? "" : ", ") + name + " " + std::to_string(v.size()) + " spans " + mb(sparse) + " MB of " + mb(bi.range);
      m.buffers.push_back({1, b, uint64_t(bi.range), ""});
      const std::string file = slot_file(1, b, ".ranges.bin");
      uint64_t at = 0;
      for (const auto& sp : v) {
        const size_t before = p.copies.size();
        if (!add_buffer_bytes(p, bi, false, sp.offset, sp.bytes, file, at, name)) return fail(p.why);
        tag(p, before, 1, b);
        for (size_t k = before; k < p.copies.size(); ++k) {   // the word-aligned pieces actually copied
          const Copy& c = p.copies[k];
          m.ranges.push_back({1, b, uint64_t(c.kind == Copy::kTransfer ? c.src_offset - bi.offset : c.src_offset), file, c.file_offset, c.bytes});
          geo_slices[g].push_back(m.ranges.back());
          at = c.file_offset + c.bytes;
          fixed += c.bytes;
        }
      }
    }
  }
  // The aliases: the geometry's slices inside their bound range (no bytes copied twice), else whole.
  for (const Alias& a : aliases) {
    VkDescriptorBufferInfo bi = a.bi;
    bi.range = resolve_range(bi);
    const VkDescriptorBufferInfo& gb = geo_bi[a.geo];
    std::vector<rp::Range> slices;
    if (gb.buffer && rp::alias_ranges(geo_slices[a.geo], gb.offset, gb.range, 1, a.binding, bi.offset, bi.range, slices)) {
      m.buffers.push_back({1, a.binding, uint64_t(bi.range), ""});
      m.ranges.insert(m.ranges.end(), slices.begin(), slices.end());
      m.notes.push_back(" s1 b" + std::to_string(a.binding) + " (" + a.name + "): " + mb(bi.range) + " MB bound in the " +
                        (a.geo ? "pool" : "soup") + " buffer at " + std::to_string(bi.offset) + ", " + std::to_string(slices.size()) +
                        " of its captured slices reused");
      continue;
    }
    const size_t before = p.copies.size();
    if (!add_buffer(p, 1, a.binding, a.bi, false, a.name)) return fail(p.why);
    for (size_t k = before; k < p.copies.size(); ++k) fixed += p.copies[k].bytes;
  }
  // The cap: ini replay_pack_cap_mb (rp::pack_cap_bytes, default 2048 MB = rp::kPackHardCapBytes).
  const uint64_t cap = rp::pack_cap_bytes(cfg_int("replay_pack_cap_mb", int(rp::kPackHardCapBytes >> 20)));
  if (fixed + kAlign > cap) {
    log("[replay] pack %s over budget: %s MB without textures over the %s MB cap (%s); by slot: %s", label.c_str(), mb(fixed).c_str(),
        mb(cap).c_str(), geometry.c_str(),
        slot_sizes(p, *pass, true).c_str());
    return -1;
  }
  // ---- the budget: droppable images up to the cap (rp::fill_budget: the hit material's
  // bindings first, smallest first, each one that still fits), and what it kept, per binding and
  // the largest images (the "textures" note and log line) ----
  std::string textures, conflicts;
  {
    std::vector<size_t> idx;
    std::vector<uint64_t> sizes;
    std::vector<uint32_t> priority;
    for (size_t i = 0; i < p.images.size(); ++i)
      if (p.images[i].droppable) { idx.push_back(i); sizes.push_back(p.images[i].bytes); priority.push_back(p.images[i].priority); }
    bool over = false;
    for (size_t k : rp::fill_budget(fixed + kAlign, sizes, cap, over, priority)) p.images[idx[k]].dropped = true;
    std::vector<std::set<uint32_t>> bound(p.images.size());   // set * 64 + binding of every use
    std::vector<const Plan::Use*> first(p.images.size(), nullptr);
    for (const auto& u : p.uses) {
      bound[u.image].insert(u.set * 64 + u.binding);
      if (!first[u.image]) first[u.image] = &u;
    }
    auto where = [&](size_t i) {
      std::string s;
      for (uint32_t k : bound[i]) s += (s.empty() ? "s" + std::to_string(k / 64) : std::string("+")) + "b" + std::to_string(k % 64);
      return s;
    };
    struct Group { uint32_t n = 0, kept = 0; uint64_t bytes = 0, kept_bytes = 0; };
    std::map<std::string, Group> groups;
    Group all;
    for (size_t i : idx) {
      const ImageCopy& ic = p.images[i];
      for (Group* g : {&groups[where(i)], &all}) {
        ++g->n; g->bytes += ic.bytes;
        if (!ic.dropped) { ++g->kept; g->kept_bytes += ic.bytes; }
      }
    }
    std::vector<size_t> largest = idx;
    std::sort(largest.begin(), largest.end(), [&](size_t a, size_t b) { return p.images[a].bytes > p.images[b].bytes; });
    if (largest.size() > 10) largest.resize(10);
    const uint64_t used = fixed + kAlign;
    textures = " textures: " + mb(used <= cap ? cap - used : 0) + " MB free under the " + mb(cap) + " MB cap for " + std::to_string(all.n) + " droppable images of " +
               mb(all.bytes) + " MB; kept " + std::to_string(all.kept) + " (" + mb(all.kept_bytes) + " MB), dropped " +
               std::to_string(all.n - all.kept) + " (" + mb(all.bytes - all.kept_bytes) + " MB); by binding (kept/images, MB kept/all):";
    for (const auto& [k, g] : groups)
      textures += " " + k + " " + std::to_string(g.kept) + "/" + std::to_string(g.n) + " " + mb(g.kept_bytes) + "/" + mb(g.bytes) + ";";
    textures += " largest:";
    for (size_t i : largest) {
      const ImageCopy& ic = p.images[i];
      char d[200];
      snprintf(d, sizeof d, " %s[%u] %u%s %ux%u x%u m%u %s %s MB p%u %s;", where(i).c_str(), first[i] ? first[i]->element : 0u,
               uint32_t(ic.view_format), ic.shader && uint32_t(ic.view_format) != ic.pack_format ? ("->" + std::to_string(ic.pack_format)).c_str() : "",
               ic.w, ic.h, ic.layers, ic.mips, ic.shader ? "read" : "copy", mb(ic.bytes).c_str(), ic.priority, ic.dropped ? "dropped" : "kept");
      textures += d;
    }
    log("[replay] pack %s%s", label.c_str(), textures.c_str());
    if (!p.conflicts.empty()) {
      conflicts = " two views of one image (omitted):";
      for (const auto& c : p.conflicts) conflicts += " " + c + ";";
      log("[replay] pack %s%s", label.c_str(), conflicts.c_str());
    }
  }
  // The shader reads' views: a 2D-array view of the game view's format, mapping and range (only
  // SAMPLED usage), so the read sees what the pass's shader sampled. Released with the staging.
  std::vector<VkImageView> views;
  uint32_t omit_view_create = 0;
  for (ImageCopy& ic : p.images) {
    if (!ic.shader || ic.dropped) continue;
    VkImageViewUsageCreateInfo usage{VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO};
    usage.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, &usage};
    vci.image = ic.image; vci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; vci.format = ic.view_format; vci.components = ic.comp;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, ic.base_mip, ic.mips, ic.base_layer, ic.layers};
    if (g_dd->vk.CreateImageView(g_dd->device, &vci, nullptr, &ic.read_view) != VK_SUCCESS) {
      ic.read_view = VK_NULL_HANDLE;
      ic.dropped = ic.view_failed = true;
      ++omit_view_create;
      continue;
    }
    views.push_back(ic.read_view);
  }
  uint32_t read_images = 0, read_bc = 0, copied_images = 0;
  uint64_t read_bytes = 0;
  for (const ImageCopy& ic : p.images) {
    for (size_t k = 0; k < ic.copies; ++k) p.copies[ic.first_copy + k].dropped = ic.dropped;
    if (ic.dropped) continue;
    if (ic.shader) { ++read_images; read_bytes += ic.bytes; read_bc += rp::block_compressed(uint32_t(ic.view_format)); }
    else ++copied_images;
  }
  // What the lighting inputs added (after the budget): kept / dropped uses, probe cubes kept, bytes.
  std::string lighting_line;
  if (pass->set_count > 2) {
    uint32_t kept = 0, dropped = 0, cubes = 0;
    for (const auto& u : p.uses) {
      if (u.set < 2) continue;
      if (p.images[u.image].dropped) { ++dropped; continue; }
      ++kept;
      cubes += u.set == 2 && u.binding == 45;
    }
    uint64_t bytes = 0;
    for (const Copy& c : p.copies) if (c.set >= 2 && !c.dropped) bytes += c.bytes;
    char d[512];
    snprintf(d, sizeof d, " lighting inputs: %u of %u slots bound; probe cubes %u named by %u records (%s), %u of %u bound cube elements captured, %u beyond the slot, kept %u; "
             "images kept %u, dropped %u (budget); samplers game %u, declared %u; %s MB", lighting.bound, lighting.slots, lighting.cubes_named, lighting.records,
             lighting.records_read ? "records read" : "records not host-readable: every cube element", lighting.cubes_bound, lighting.cube_elems,
             lighting.beyond, cubes, kept, dropped, lighting.samplers_game, lighting.samplers_declared, mb(bytes).c_str());
    lighting_line = d;
    if (!lighting.absent.empty()) lighting_line += "; absent: " + lighting.absent;
  }
  for (const auto& u : p.uses) {
    const ImageCopy& ic = p.images[u.image];
    const std::string& file = p.copies[ic.first_copy].file;
    if (ic.dropped) { ++(ic.view_failed ? p.omit_view : p.omit_budget); m.omitted.push_back({u.set, u.binding, u.element}); continue; }
    if (u.output) m.outputs.push_back({u.set, u.binding, ic.pack_format, ic.w, ic.h, file});
    else   // a transfer copy shared by raw-compatible views: each use names its own format
      m.images.push_back({u.set, u.binding, u.element, ic.shader ? ic.pack_format : u.format, ic.w, ic.h, ic.layers, ic.mips, file,
                          ic.shader && uint32_t(ic.view_format) != ic.pack_format ? uint32_t(ic.view_format) : 0u});
  }
  const uint32_t omitted = uint32_t(m.omitted.size());
  char note[640];
  snprintf(note, sizeof note,
           " live: frame_index %llu, %u instances, %s; images omitted %u (no TRANSFER_SRC or SAMPLED %u, format %u, view %u, layout %u, "
           "size %u, budget %u); images copied %u, read by shader %u (%u BCn decoded, %s MB)",
           (unsigned long long)frame_index(), region.count, geometry.c_str(), omitted, p.omit_transfer, p.omit_format, p.omit_view,
           p.omit_layout, p.omit_size, p.omit_budget, copied_images, read_images, read_bc, mb(read_bytes).c_str());
  m.notes.insert(m.notes.begin(), note);
  if (!p.omit_formats.empty()) {
    std::string f = " omitted for their format (VkFormat x uses):";
    for (const auto& [format, n] : p.omit_formats) f += " " + std::to_string(format) + " x" + std::to_string(n);
    m.notes.insert(m.notes.begin() + 1, f);
  }
  if (omit_view_create) m.notes.push_back(" " + std::to_string(omit_view_create) + " shader-read views failed to create (omitted)");
  m.notes.push_back(textures);
  if (!conflicts.empty()) m.notes.push_back(conflicts);
  if (!lighting_line.empty()) {
    m.notes.push_back(lighting_line);
    log("[replay] pack %s%s", label.c_str(), lighting_line.c_str());
  }

  // ---- staging: chunks, placement ----
  auto job = std::make_unique<Job>();
  job->views = std::move(views);
  {
    VkDeviceSize at = kAlign;   // the marker
    uint32_t chunk = 0;
    std::vector<VkDeviceSize> sizes(1, 0);
    for (Copy& c : p.copies) {
      if (c.dropped || c.kind == Copy::kCpu) continue;
      const VkDeviceSize need = (c.bytes + 3) & ~VkDeviceSize(3);   // a shader read writes whole words
      at = (at + kAlign - 1) & ~(kAlign - 1);
      if (at + need > p.chunk_bytes) { sizes[chunk] = at; ++chunk; sizes.push_back(0); at = 0; }
      c.chunk = chunk; c.offset = at; at += need;
    }
    sizes[chunk] = (std::max)(at, kAlign);
    for (VkDeviceSize s : sizes) {
      Chunk ch;
      ch.size = (s + kAlign - 1) & ~(kAlign - 1);
      VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      bci.size = ch.size;
      bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      const VkDeviceFuncs& vk = g_dd->vk;
      bool ok = vk.CreateBuffer(g_dd->device, &bci, nullptr, &ch.b) == VK_SUCCESS;
      if (ok) {
        VkMemoryRequirements req{};
        vk.GetBufferMemoryRequirements(g_dd->device, ch.b, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = find_memory_type(g_dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                                            VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX)
          ai.memoryTypeIndex = find_memory_type(g_dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* map = nullptr;
        ok = ai.memoryTypeIndex != UINT32_MAX && vk.AllocateMemory(g_dd->device, &ai, nullptr, &ch.m) == VK_SUCCESS &&
             vk.BindBufferMemory(g_dd->device, ch.b, ch.m, 0) == VK_SUCCESS &&
             vk.MapMemory(g_dd->device, ch.m, 0, VK_WHOLE_SIZE, 0, &map) == VK_SUCCESS;
        ch.map = static_cast<uint8_t*>(map);
      }
      job->chunks.push_back(ch);
      if (!ok) {
        release_chunks(job->chunks);
        release_views(job->views);
        return fail("staging allocation of " + mb(s) + " MB (host-visible) failed");
      }
    }
    std::memset(job->chunks[0].map, 0, kAlign);
  }

  // ---- record the copies and reads ----
  const VkDeviceFuncs& vk = g_dd->vk;
  std::vector<VkImageMemoryBarrier> pre, post;
  for (const ImageCopy& ic : p.images) {   // transfer copies: TRANSFER_SRC_OPTIMAL and back; reads stay in their layout
    if (ic.dropped || ic.shader || ic.layout == VK_IMAGE_LAYOUT_GENERAL) continue;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.oldLayout = ic.layout;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = ic.image;
    b.subresourceRange = {ic.barrier_aspect, ic.base_mip, ic.mips, ic.base_layer, ic.layers};
    pre.push_back(b);
    std::swap(b.oldLayout, b.newLayout);
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    post.push_back(b);
  }
  VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  before.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  before.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                        &before, 0, nullptr, uint32_t(pre.size()), pre.data());
  uint32_t n_images = 0, n_transfer = 0, n_shader = 0, n_reads = 0;
  for (const Copy& c : p.copies) {   // one region per piece (rp::transfer_pieces)
    if (c.dropped || c.kind != Copy::kImage) continue;
    const ImageCopy& ic = p.images[c.image];
    if (ic.shader) continue;
    VkBufferImageCopy r{};
    r.bufferOffset = c.offset;
    r.imageSubresource = {ic.copy_aspect, ic.base_mip + c.mip, ic.base_layer + c.layer, c.layers};
    r.imageOffset = {0, int32_t(c.row), 0};
    r.imageExtent = {(std::max)(ic.w >> c.mip, 1u), c.rows, 1};
    vk.CmdCopyImageToBuffer(cmd, ic.image, ic.layout == VK_IMAGE_LAYOUT_GENERAL ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            job->chunks[c.chunk].b, 1, &r);
    n_images += c.file_offset == 0;
  }
  for (const Copy& c : p.copies) {   // one region per call: few transfer sources, all different
    if (c.dropped || c.kind != Copy::kTransfer) continue;
    VkBufferCopy r{c.src_offset, c.offset, c.bytes};
    vk.CmdCopyBuffer(cmd, c.src, job->chunks[c.chunk].b, 1, &r);
    ++n_transfer;
  }
  {
    VkBuffer bound_src = VK_NULL_HANDLE; VkDeviceSize bound_off = 0, bound_range = 0; uint32_t bound_chunk = UINT32_MAX;
    for (const Copy& c : p.copies) {
      if (c.dropped || c.kind != Copy::kShader) continue;
      if (!n_shader) vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_copy_pipe);
      if (c.bound.buffer != bound_src || c.bound.offset != bound_off || c.bound.range != bound_range || c.chunk != bound_chunk) {
        VkDescriptorBufferInfo bi[2] = {c.bound, {job->chunks[c.chunk].b, 0, job->chunks[c.chunk].size}};
        VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
        for (uint32_t k = 0; k < 2; ++k) {
          w[k].dstBinding = k; w[k].descriptorCount = 1; w[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[k].pBufferInfo = bi + k;
        }
        vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_copy_pl, 0, 2, w);
        bound_src = c.bound.buffer; bound_off = c.bound.offset; bound_range = c.bound.range; bound_chunk = c.chunk;
      }
      constexpr uint64_t kMaxWords = 65535ull * 256;
      for (uint64_t word = 0, words = c.bytes / 4; word < words; word += kMaxWords) {
        const uint32_t n = uint32_t((std::min)(words - word, kMaxWords));
        const uint32_t push[4] = {uint32_t(c.src_offset + word * 4), uint32_t(c.offset + word * 4), n, 0};
        vk.CmdPushConstants(cmd, g_copy_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, push);
        vk.CmdDispatch(cmd, (n + 255) / 256, 1, 1);
      }
      ++n_shader;
    }
  }
  {   // image reads (shaders/replay_image.hlsl), one piece per rp::ReadPiece
    VkImageView bound_view = VK_NULL_HANDLE; uint32_t bound_chunk = UINT32_MAX;
    for (const Copy& c : p.copies) {
      if (c.dropped || c.kind != Copy::kImage) continue;
      const ImageCopy& ic = p.images[c.image];
      if (!ic.shader) continue;
      if (!n_reads) vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_read_pipe);
      if (ic.read_view != bound_view || c.chunk != bound_chunk) {
        VkDescriptorImageInfo ii{VK_NULL_HANDLE, ic.read_view, ic.layout};
        VkDescriptorBufferInfo bi{job->chunks[c.chunk].b, 0, job->chunks[c.chunk].size};
        VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
        w[0].dstBinding = 0; w[0].descriptorCount = 1; w[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w[0].pImageInfo = &ii;
        w[1].dstBinding = 1; w[1].descriptorCount = 1; w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[1].pBufferInfo = &bi;
        vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_read_pl, 0, 2, w);
        bound_view = ic.read_view; bound_chunk = c.chunk;
      }
      const uint64_t per = rp::shader_read_per_thread(ic.read), tb = rp::shader_read_texel_bytes(ic.read);
      const uint64_t thread_bytes = tb >= 4 ? tb : 4, threads = (c.count + per - 1) / per;
      constexpr uint64_t kMaxThreads = 65535ull * 256;
      for (uint64_t t = 0; t < threads; t += kMaxThreads) {
        const uint64_t n = (std::min)(threads - t, kMaxThreads);
        const uint32_t push[8] = {uint32_t(c.offset + t * thread_bytes), uint32_t(c.first + t * per), uint32_t((std::min)(n * per, c.count - t * per)),
                                  (std::max)(ic.w >> c.mip, 1u), (std::max)(ic.h >> c.mip, 1u), c.mip, ic.read.encode, ic.read.channels};
        vk.CmdPushConstants(cmd, g_read_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, push);
        vk.CmdDispatch(cmd, uint32_t((n + 255) / 256), 1, 1);
      }
      ++n_reads;
    }
  }
  vk.CmdFillBuffer(cmd, job->chunks[0].b, 0, kAlign, kMagic);
  VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  after.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &after, 0, nullptr, uint32_t(post.size()),
                        post.data());

  uint64_t total = 0;
  for (const Copy& c : p.copies) if (!c.dropped) total += c.bytes;
  const std::string by_slot = slot_sizes(p, *pass, false);
  p.m.notes.push_back(" by slot: " + by_slot);
  job->label = label;
  job->dir = root_dir() + "\\captures\\replay-" + capture_stamp() + "-" + label;
  job->record_frame = frame_index();
  job->bytes = total;
  job->omitted = omitted;
  job->read_images = read_images;
  job->instances = region.count;
  job->m = std::move(p.m);
  job->copies = std::move(p.copies);
  log("[replay] pack %s recorded at frame %llu: %u image copies, %u image reads (%u BCn decoded, %s MB in %u pieces), %u transfer and %u shader "
      "copies, %s MB in %zu staging chunks (%s); written once the GPU has run it; by slot: %s",
      label.c_str(), (unsigned long long)job->record_frame, n_images, read_images, read_bc, mb(read_bytes).c_str(), n_reads, n_transfer, n_shader,
      mb(total).c_str(), job->chunks.size(), geometry.c_str(), by_slot.c_str());
  g_job = std::move(job);
  return 1;
}
#endif  // BG3RTX_DIAGNOSTICS

}  // namespace bg3
