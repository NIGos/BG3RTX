// Track D phase 3: what the ray queries need to alpha-test BG3's cutout
// geometry (chains, grates, foliage), per captured draw (docs/TRACK_D_RT.md).
//
// With rt_alpha=1 the scene is captured from the depth prepass instead of the
// G-buffer pass: its fragment shaders are the ones that discard (the G-buffer
// pass then tests depth against it and never discards). Which of them alpha
// test, and where their texture comes from, is in shaders/rt/alpha_fs.txt,
// generated offline from the shader dump by scripts/alpha_table.py:
//   vt   BG3's virtual texturing (Granite): the albedo tile's alpha, read through
//        an indirection texture from the tile cache array, with per-draw
//        constants from a UBO (uv transform, tile-set index) and an SSBO entry
//        (two row-major mat4 of tile-set constants);
//   tex  a plain texture's alpha (or red), optionally with a UBO uv scale.
// At each such draw the layer reads those constants from the game's mapped
// memory (the same way Streamline reads the camera), and records the views it
// sampled. After the pass the records go into a buffer, and the views into one
// of a ring of descriptor sets (set 1 of the core's ray query passes,
// shaders/rt/rt_common.hlsli): a sampled-image array for indirection/plain
// textures, one for tile caches, the captured UV soup, and two samplers.
// Every view in a set was bound by the game in the frame the set was written.
#include "host.h"
#include "hit_material.h"
#include "../common/native_hit_capability.h"
#include "../common/mapped_copy.h"
#include "../common/native_scalp_coverage.h"
#include "../common/native_scalp_capability.h"
#include "../common/native_hair.h"
#include "../common/omm_scene_capability.h"
#include "omm_backend.h"
#include "omm_content.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bg3 {

namespace {
bool g_stream_reads = false;

// Must match shaders/rt/rt_common.hlsli. Outdoors (foliage) a frame binds far
// more than the 256 views of the first version; a full list turns draws opaque
// (logged at once, "views full").
constexpr uint32_t kTextures = 4096;  // Texture2D slots (0 = dummy)
constexpr uint32_t kCaches = 16;      // Texture2DArray slots (0 = dummy)
constexpr uint32_t kRing = kSceneSlots;   // descriptor sets / record regions: one per scene slot (rtscene.cpp)
constexpr uint32_t kMaxRecords = 8192;
constexpr VkDeviceSize kRegion = 16 + VkDeviceSize(kMaxRecords) * sizeof(AlphaRec);

DeviceData* g_dd = nullptr;
bool g_on = false;
bool g_omm_layout = false;
std::atomic<uint32_t> g_stale_views{0};   // held alpha records dropped: a view was destroyed/reused
std::mutex g_mu;
std::unordered_map<uint64_t, AlphaFs> g_table;
uint64_t g_table_mtime = 0;

VkDescriptorSetLayout g_dsl = VK_NULL_HANDLE;
VkDescriptorPool g_pool = VK_NULL_HANDLE;
VkDescriptorSet g_sets[kRing] = {};
VkSampler g_wrap = VK_NULL_HANDLE, g_clamp = VK_NULL_HANDLE;
VkBuffer g_rec_buf = VK_NULL_HANDLE;
VkDeviceMemory g_rec_mem = VK_NULL_HANDLE;
struct Img { VkImage image = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; };
Img g_dummy2d, g_dummy_arr;
bool g_dummy_ready = false;
VkBuffer g_dummy_buf = VK_NULL_HANDLE;
VkDeviceMemory g_dummy_mem = VK_NULL_HANDLE;
// Scene stamps (set 1 binding 9, 512 bytes per slot, host-visible): uint 0 the
// generation the CPU expects (written at commit), uint 1 the one the build's
// command buffer wrote on the GPU after its TLAS. The ray queries trace only when
// they match (rt_common.hlsli rt_scene_ok): a TLAS whose build never ran (its
// command buffer dropped, or submitted after the pass) is not traced.
VkBuffer g_stamp_buf = VK_NULL_HANDLE;
VkDeviceMemory g_stamp_mem = VK_NULL_HANDLE;
uint32_t* g_stamp_map = nullptr;
uint32_t g_stamp_gen = 0;
constexpr VkDeviceSize kStampStride = 512, kStampCameraOffset = 256;
struct SunAlphaSlot {
  VkDescriptorPool pool = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
  VkBuffer stamp = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  uint32_t* mapped = nullptr;
  VkBuffer records=VK_NULL_HANDLE, dispatch=VK_NULL_HANDLE, parameters=VK_NULL_HANDLE;
  VkDeviceMemory records_memory=VK_NULL_HANDLE, dispatch_memory=VK_NULL_HANDLE, parameters_memory=VK_NULL_HANDLE;
  VkDeviceSize record_bytes=0, dispatch_bytes=0, parameter_bytes=0;
};
SunAlphaSlot g_sun_alpha[kRing];
VkDescriptorSetLayout g_sun_dsl=VK_NULL_HANDLE;
uint32_t g_set_records[kRing]={};
VkBuffer g_legacy_buf = VK_NULL_HANDLE;   // alpha_legacy_info
VkDeviceMemory g_legacy_mem = VK_NULL_HANDLE;
uint32_t g_legacy_stride = 0;
uint32_t g_set_tex[kRing] = {};     // per set: texture slots holding real views (the rest: dummies)
uint32_t g_set_cache[kRing] = {};
bool g_set_init[kRing] = {};
// The game's views (and their images) each set references, for the life system
// whenever the set is handed out (alpha_slot_in_use).
std::vector<uint64_t> g_set_handles[kRing];
uint64_t g_set_extended[kRing] = {};
bool g_full_logged = false;
int g_current = -1;             // slot of the last commit
bool g_logged = false;

// This capture's records and views.
std::vector<AlphaRec> g_recs;
std::vector<std::pair<VkImageView, VkImageLayout>> g_tex, g_cache;
// Why draws stayed opaque, for the log.
uint32_t g_fail[6] = {};
uint64_t g_frames = 0, g_draws = 0, g_tris = 0;
std::atomic<uint32_t> g_scalp_hard{0}, g_scalp_dither{0}, g_scalp_invalid{0};
std::atomic<uint32_t> g_hair_records{0}, g_hair_invalid{0};
std::atomic<uint32_t> g_hair_attributes{0};
std::atomic<uint32_t> g_hair_bounds_invalid{0};

bool unit_mask_format(VkFormat f) {
  switch(f) {
    case VK_FORMAT_R8_UNORM:case VK_FORMAT_R8G8_UNORM:case VK_FORMAT_R8G8B8_UNORM:
    case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_R16_UNORM:case VK_FORMAT_R16G16_UNORM:case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC2_UNORM_BLOCK:case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC4_UNORM_BLOCK:case VK_FORMAT_BC5_UNORM_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:case VK_FORMAT_BC7_SRGB_BLOCK:return true;
    default:return false;
  }
}

bool make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& b, VkDeviceMemory& m, bool address = false) {
  DeviceData* dd = g_dd;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage | (address ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0u);
  if (dd->vk.CreateBuffer(dd->device, &bci, nullptr, &b) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetBufferMemoryRequirements(dd->device, b, &req);
  VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
  fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  if (address && rt_alloc_device_address()) mai.pNext = &fi;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &m) != VK_SUCCESS) return false;
  return dd->vk.BindBufferMemory(dd->device, b, m, 0) == VK_SUCCESS;
}

bool make_image(bool array, Img& out) {
  DeviceData* dd = g_dd;
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.imageType = VK_IMAGE_TYPE_2D;
  ci.format = VK_FORMAT_R8G8B8A8_UNORM;
  ci.extent = {4, 4, 1};
  ci.mipLevels = 1;
  ci.arrayLayers = 1;
  ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (dd->vk.CreateImage(dd->device, &ci, nullptr, &out.image) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetImageMemoryRequirements(dd->device, out.image, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.mem) != VK_SUCCESS ||
      dd->vk.BindImageMemory(dd->device, out.image, out.mem, 0) != VK_SUCCESS)
    return false;
  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = out.image;
  vci.viewType = array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  vci.format = ci.format;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  return dd->vk.CreateImageView(dd->device, &vci, nullptr, &out.view) == VK_SUCCESS;
}

// "<hash> vt uv=0 cut=0.5 [cut0=0.001] ubo=1.0 p0=0 idx=32 ssbo=0.12 ind=1.3 cache=1.4 col=0"
void load_table() {
  std::string path = root_dir() + "\\shaders\\rt\\alpha_fs.txt";
  uint64_t mt = file_mtime(path);
  if (!mt || mt == g_table_mtime) return;
  g_table_mtime = mt;
  std::ifstream f(path);
  std::unordered_map<uint64_t, AlphaFs> t;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream in(line);
    std::string hash, kind, kv;
    in >> hash >> kind;
    AlphaFs a{};
    a.kind = kind == "vt" ? 1 : kind == "tex" ? 2 : 0;
    a.cut = 0.5f;
    a.cut_first = -1.0f;
    a.k = 1.0f;
    a.hash = std::stoull(hash, nullptr, 16);
    if (!a.kind) continue;
    auto sb = [](const std::string& v, uint8_t& s, uint8_t& b) {
      size_t d = v.find('.');
      s = uint8_t(std::stoul(v.substr(0, d)));
      b = uint8_t(std::stoul(v.substr(d + 1)));
    };
    while (in >> kv) {
      size_t e = kv.find('=');
      if (e == std::string::npos) continue;
      std::string k = kv.substr(0, e), v = kv.substr(e + 1);
      if (k == "uv") a.uv = int8_t(std::stoi(v));
      else if (k == "cut") a.cut = std::stof(v);
      else if (k == "cut0") a.cut_first = std::stof(v);
      else if (k == "ubo") sb(v, a.ubo_set, a.ubo_bind), a.has_ubo = true;
      else if (k == "p0") a.p0 = uint32_t(std::stoul(v));
      else if (k == "idx") a.idx = uint32_t(std::stoul(v));
      else if (k == "ssbo") sb(v, a.ssbo_set, a.ssbo_bind);
      else if (k == "ind" || k == "tex") sb(v, a.tex_set, a.tex_bind);
      else if (k == "cache") sb(v, a.cache_set, a.cache_bind);
      else if (k == "col" || k == "ch") a.channel = uint8_t(std::stoul(v));
      else if (k == "scale") a.scale = uint32_t(std::stoul(v)), a.has_scale = true;
      else if (k == "k") a.k = std::stof(v);
    }
    if (a.cut_first < 0.0f) a.cut_first = a.cut;
    t[a.hash] = a;
  }
  g_table.swap(t);
  log("[alpha] %zu alpha-tested fragment shaders in %s", g_table.size(), path.c_str());
}

// Reads `n` bytes at `extra` into the buffer bound at (set, binding) of the
// graphics bind point, from the game's mapped memory.
bool read_bound(VkCommandBuffer cmd, uint32_t set, uint32_t binding, VkDeviceSize extra, void* dst, size_t n) {
  Bg3Binding b;
  if (!query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, set, binding, 0, &b) || !b.buffer) return false;
  if (b.range != VK_WHOLE_SIZE && extra + n > b.range) return false;
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  auto bm = tracker().buffer_mem.find(b.buffer);
  if (bm == tracker().buffer_mem.end()) return false;
  auto mp = tracker().mapped.find(bm->second.first);
  if (mp == tracker().mapped.end() || !mp->second) return false;
  const auto* src = mp->second + bm->second.second + b.offset + extra;
  if (g_stream_reads) copy_mapped_stream(dst, src, n);
  else std::memcpy(dst, src, n);
  return true;
}

// Slot of `view` in `list` (appended if new); 0 if the list is full.
uint32_t slot_of(std::vector<std::pair<VkImageView, VkImageLayout>>& list, uint32_t cap, VkImageView v,
                 VkImageLayout l) {
  for (size_t i = 0; i < list.size(); ++i)
    if (list[i].first == v) return uint32_t(i + 1);
  if (list.size() + 1 >= cap) return 0;
  list.push_back({v, l});
  return uint32_t(list.size());
}

// Tracker id of the image behind `v` (0 if the view or image is unknown). Caller holds no tracker lock.
uint32_t view_image_id(VkImageView v) {
  if (!v) return 0;
  std::shared_lock<std::shared_mutex> tl(tracker().mu);
  auto it = tracker().views.find(v);
  if (it == tracker().views.end()) return 0;
  auto im = tracker().images.find(it->second.image);
  return im == tracker().images.end() ? 0 : im->second.id;
}

bool finite(const float* f, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (!(f[i] > -1e30f && f[i] < 1e30f)) return false;
  return true;
}

}  // namespace

void alpha_device_created(DeviceData* dd) {
  if (g_dd || !cfg_int("rt", 1)) return;
  g_dd = dd;
  const VkDeviceFuncs& vk = dd->vk;
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = si.minFilter = VK_FILTER_LINEAR;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  si.maxLod = 16.0f;
  bool ok = vk.CreateSampler(dd->device, &si, nullptr, &g_wrap) == VK_SUCCESS;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  ok = ok && vk.CreateSampler(dd->device, &si, nullptr, &g_clamp) == VK_SUCCESS;
  g_omm_layout=omm::available_for(dd) || omm::scene_capability(static_cast<uint32_t*>(persist(omm::scene_capability_key,sizeof(uint32_t))));
  VkDescriptorSetLayoutBinding b[16] = {
      {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},   // scene stamp
      {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},   // InstInfo per TLAS instance
      {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},   // the split's mesh pool
      {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kTextures, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCaches, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {4, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &g_wrap},
      {5, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &g_clamp},
      {10,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {11,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {12,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {13,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {14,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,native_hit::cache_slots,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {15,VK_DESCRIPTOR_TYPE_SAMPLER,native_hit::sampler_slots,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
  std::vector<VkDescriptorSetLayoutBinding> bindings(b,b+10);
  if(g_omm_layout)bindings.insert(bindings.end(),b+10,b+12);
  if(native_hit::enabled())bindings.insert(bindings.end(),b+12,b+16);
  VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dci.bindingCount = uint32_t(bindings.size());
  dci.pBindings = bindings.data();
  ok = ok && vk.CreateDescriptorSetLayout(dd->device, &dci, nullptr, &g_dsl) == VK_SUCCESS;
  VkDescriptorPoolSize ps[4] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ((g_omm_layout?7u:6u)+(native_hit::enabled()?2u:0u)) * kRing},
                                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (kTextures + kCaches+(native_hit::enabled()?native_hit::cache_slots:0u)) * kRing},
                                {VK_DESCRIPTOR_TYPE_SAMPLER, (2+(native_hit::enabled()?native_hit::sampler_slots:0u)) * kRing},
                                {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,kRing}};
  VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pci.maxSets = kRing;
  pci.poolSizeCount = g_omm_layout?4u:3u;
  pci.pPoolSizes = ps;
  ok = ok && vk.CreateDescriptorPool(dd->device, &pci, nullptr, &g_pool) == VK_SUCCESS;
  VkDescriptorSetLayout layouts[kRing];
  for (auto& l : layouts) l = g_dsl;
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = g_pool;
  ai.descriptorSetCount = kRing;
  ai.pSetLayouts = layouts;
  ok = ok && vk.AllocateDescriptorSets(dd->device, &ai, g_sets) == VK_SUCCESS;
  ok = ok && make_buffer(kRegion * kRing, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         g_rec_buf, g_rec_mem, true);
  if (ok) fault_track("alpha records (8 slot regions)", rt_buffer_address(g_rec_buf), kRegion * kRing);
  ok = ok && make_buffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, g_dummy_buf,
                         g_dummy_mem);
  ok = ok && make_image(false, g_dummy2d) && make_image(true, g_dummy_arr);
  if (ok) {   // the scene stamps: host-visible, persistently mapped, zeroed
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = kStampStride * kRing;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    ok = vk.CreateBuffer(dd->device, &bci, nullptr, &g_stamp_buf) == VK_SUCCESS;
    VkMemoryRequirements req{};
    if (ok) vk.GetBufferMemoryRequirements(dd->device, g_stamp_buf, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = ok ? find_memory_type(dd, req.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                             : UINT32_MAX;
    void* p = nullptr;
    ok = ok && mai.memoryTypeIndex != UINT32_MAX &&
         vk.AllocateMemory(dd->device, &mai, nullptr, &g_stamp_mem) == VK_SUCCESS &&
         vk.BindBufferMemory(dd->device, g_stamp_buf, g_stamp_mem, 0) == VK_SUCCESS &&
         vk.MapMemory(dd->device, g_stamp_mem, 0, VK_WHOLE_SIZE, 0, &p) == VK_SUCCESS;
    g_stamp_map = static_cast<uint32_t*>(p);
    if (ok) std::memset(g_stamp_map, 0, size_t(kStampStride * kRing));
  }
  if (!ok) {
    log("[alpha] descriptor/resource setup failed: alpha testing off");
    return;
  }
  load_table();
  g_on = true;
  native_hit::capability(static_cast<uint32_t*>(persist(native_hit::capability_key,sizeof(uint32_t))),native_hit::enabled());
}

void alpha_device_destroyed(DeviceData* dd) {
  if (dd != g_dd) return;
  native_hit::capability(static_cast<uint32_t*>(persist(native_hit::capability_key,sizeof(uint32_t))),false);
  const VkDeviceFuncs& vk = dd->vk;
  for (SunAlphaSlot& s : g_sun_alpha) {
    if (s.pool) vk.DestroyDescriptorPool(dd->device,s.pool,nullptr);
    if (s.stamp) vk.DestroyBuffer(dd->device,s.stamp,nullptr);
    if (s.memory) vk.FreeMemory(dd->device,s.memory,nullptr);
    for(auto [b,m]:{std::pair{s.records,s.records_memory},std::pair{s.dispatch,s.dispatch_memory},std::pair{s.parameters,s.parameters_memory}}) {
      if(b) vk.DestroyBuffer(dd->device,b,nullptr);if(m) vk.FreeMemory(dd->device,m,nullptr);
    }
    s = SunAlphaSlot{};
  }
  if(g_sun_dsl) vk.DestroyDescriptorSetLayout(dd->device,g_sun_dsl,nullptr);
  g_sun_dsl=VK_NULL_HANDLE;
  if (g_pool) vk.DestroyDescriptorPool(dd->device, g_pool, nullptr);
  if (g_dsl) vk.DestroyDescriptorSetLayout(dd->device, g_dsl, nullptr);
  for (VkSampler s : {g_wrap, g_clamp})
    if (s) vk.DestroySampler(dd->device, s, nullptr);
  for (Img* i : {&g_dummy2d, &g_dummy_arr}) {
    if (i->view) vk.DestroyImageView(dd->device, i->view, nullptr);
    if (i->image) vk.DestroyImage(dd->device, i->image, nullptr);
    if (i->mem) vk.FreeMemory(dd->device, i->mem, nullptr);
    *i = Img{};
  }
  for (auto [b, m] : {std::pair{g_rec_buf, g_rec_mem}, std::pair{g_dummy_buf, g_dummy_mem},
                      std::pair{g_legacy_buf, g_legacy_mem}, std::pair{g_stamp_buf, g_stamp_mem}}) {
    if (b) vk.DestroyBuffer(dd->device, b, nullptr);
    if (m) vk.FreeMemory(dd->device, m, nullptr);
  }
  g_rec_buf = g_dummy_buf = g_legacy_buf = g_stamp_buf = VK_NULL_HANDLE;
  g_rec_mem = g_dummy_mem = g_legacy_mem = g_stamp_mem = VK_NULL_HANDLE;
  g_stamp_map = nullptr;
  g_legacy_stride = 0;
  g_pool = VK_NULL_HANDLE;
  g_dsl = VK_NULL_HANDLE;
  g_wrap = g_clamp = VK_NULL_HANDLE;
  g_on = false;
  g_current = -1;
  g_dd = nullptr;
}

bool alpha_lookup(uint64_t fs, AlphaFs* out) {
  if (!g_on) return false;
  if (cfg_int("rt_native_hair", 0)) {
    if (const auto* recipe = native_hair::lookup(fs)) {
      if (!geo_native_hair_enabled()) {
        static std::atomic<bool> warned{false};
        if (!warned.exchange(true)) log("[alpha hair] rt_native_hair requires restart with rt_alpha=1; no mixed 24/88-byte scene");
        return false;
      }
      native_scalp::publish_capability(static_cast<uint32_t*>(persist(native_scalp::capability_key, sizeof(uint32_t))));
      *out = AlphaFs{};
      out->kind=4;out->hash=fs;out->uv=0;out->tex_set=out->cache_set=out->ubo_set=1;
      out->tex_bind=recipe->mask;out->cache_bind=recipe->noise;out->ubo_bind=recipe->material;
      out->channel=recipe->camera?1:0;out->cut=out->cut_first=0.3333f;out->k=1;
      return true;
    }
  }
  if (cfg_int("rt_native_scalp", 0)) {
    if (const auto* recipe = native_scalp::lookup(fs)) {
      // Before any possible kind3 publication. The core selects after acquiring
      // its actual scene; acquire of this sticky word survives core reloads.
      native_scalp::publish_capability(static_cast<uint32_t*>(persist(native_scalp::capability_key, sizeof(uint32_t))));
      *out = AlphaFs{};
      out->kind = 3;
      out->hash = fs;
      out->uv = 0;
      out->tex_set = out->ubo_set = 1;
      out->tex_bind = recipe->mask_binding;
      out->ubo_bind = recipe->material_binding;
      out->cut = out->cut_first = 0.3333f;
      out->channel = 3;
      out->k = 1.0f;
      return true;
    }
  }
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_table.find(fs);
  if (it == g_table.end()) return false;
  *out = it->second;
  return true;
}

bool alpha_native_hair_record(uint32_t index,AlphaRec* out) {
  std::lock_guard<std::mutex> lock(g_mu);
  if(index>=g_recs.size() || g_recs[index].kind!=4)return false;
  *out=g_recs[index];return true;
}
bool alpha_native_hair_bounds(uint32_t index,uint32_t pool_offset) {
  std::lock_guard<std::mutex> lock(g_mu);
  if(index>=g_recs.size() || g_recs[index].kind!=4)return false;
  std::memcpy(reinterpret_cast<uint8_t*>(&g_recs[index])+192,&pool_offset,4);return true;
}
bool alpha_hair_build_status(uint32_t slot,VkBuffer* buffer,VkDeviceSize* offset) {
  if(!geo_native_hair_enabled() || !g_stamp_buf || slot>=kRing)return false;
  *buffer=g_stamp_buf;*offset=VkDeviceSize(slot)*kStampStride;return true;
}

void alpha_native_hair_missing_attributes(uint64_t fs,uint64_t vs,uint32_t flags) {
  const auto n=g_hair_attributes.fetch_add(1);
  if(n<8) log("[alpha hair] unsupported native interface FS %016llx VS %016llx flags %u (strand=1, camera fade=2)",
             (unsigned long long)fs,(unsigned long long)vs,flags);
}

void alpha_begin_capture() {
  std::lock_guard<std::mutex> lock(g_mu);
  if (cfg_int("rt_native_scalp", 0) || geo_native_hair_enabled())
    native_scalp::publish_capability(static_cast<uint32_t*>(persist(native_scalp::capability_key, sizeof(uint32_t))));
  g_stream_reads = cfg_int("rt_alpha_stream", 0) != 0;
  g_recs.clear();
  g_tex.clear();
  g_cache.clear();
}

// Shared canonical capture: never touches the camera capture's mutable record
// or descriptor-index arrays. Ordinary camera adds use the same raw bindings.
static bool capture_source_material(VkCommandBuffer cmd,const AlphaFs& a,uint32_t first_tri,uint32_t tris,
                                    AlphaRec& r,Bg3Binding& t,Bg3Binding& c) {
  r={};t={};c={};
  r.first_tri = first_tri;
  r.tris = tris;
  r.kind = a.kind;
  r.channel = a.channel;
  r.cut = a.cut;
  r.cut_first = a.cut_first;
  r.fs_lo = uint32_t(a.hash);
  r.fs_hi = uint32_t(a.hash >> 32);
  r.scale[0] = r.scale[1] = a.k;
  if (!query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a.tex_set, a.tex_bind, 0, &t) || !t.view) {
    ++g_fail[0];
    return false;
  }
  if (a.kind == 1) {
    // UV transform (two vec4) and the tile-set index from the material UBO,
    // the tile-set constants (two row-major mat4) from the SSBO entry.
    uint32_t idx = 0;
    if (!read_bound(cmd, a.ubo_set, a.ubo_bind, a.p0, r.p0, 32) || !read_bound(cmd, a.ubo_set, a.ubo_bind, a.idx, &idx, 4)) {
      ++g_fail[1];
      return false;
    }
    if (idx > (1u << 20) || !read_bound(cmd, a.ssbo_set, a.ssbo_bind, VkDeviceSize(idx) * 128, r.t, 128)) {
      ++g_fail[2];
      return false;
    }
    if (!query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a.cache_set, a.cache_bind, 0, &c) || !c.view) {
      ++g_fail[0];
      return false;
    }
  } else if (a.kind == 3) {
    native_scalp::Captured captured;
    if (!native_scalp::capture(a.hash, [&](unsigned set, unsigned binding, unsigned off, void* dst, size_t bytes) {
          return read_bound(cmd, set, binding, off, dst, bytes);
        }, captured)) {
      ++g_scalp_invalid;
      return false;
    }
    // Validate the material independently of original raster coordinates.
    auto hard_frame=captured.frame;hard_frame.dither_mode=0.0f;
    const auto probe = native_scalp::evaluate(a.hash, captured.material, {1, 1, true}, hard_frame);
    if (probe.status != native_scalp::Status::CompleteScalar) {
      ++g_scalp_invalid;
      return false;
    }
    r.p0[0] = captured.material.edge;
    r.p0[1] = captured.material.root;
    if(captured.frame.available && captured.frame.dither_mode!=0.0f) {
      auto* state=cmd_state(cmd);
      if(!captured.projection_available || !state || !state->has_viewport) {++g_scalp_invalid;return false;}
      const float viewport[4]={state->viewport.x,state->viewport.y,state->viewport.width,state->viewport.height};
      if(!finite(viewport,4)||viewport[2]==0||viewport[3]==0) {++g_scalp_invalid;return false;}
      auto* bytes=reinterpret_cast<uint8_t*>(&r);
      std::memcpy(bytes+96,captured.projection.data(),64);
      std::memcpy(bytes+160,viewport,16);
      r.p0[2]=1.0f; // Explicit camera-dither packet. Old kind3 hard records stay zero.
      r.scale[0]=captured.frame.phase;r.scale[1]=captured.frame.dither_mode;
    }

  } else if (a.kind == 4) {
    native_hair::Captured captured;
    if (!native_hair::capture(a.hash, [&](unsigned set,unsigned binding,unsigned off,void* dst,size_t bytes) {
          return read_bound(cmd,set,binding,off,dst,bytes);
        },captured) || !query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,a.cache_set,a.cache_bind,0,&c) || !c.view) {
      ++g_hair_invalid; return false;
    }
    // Fixed208B payload: material48B, VP64B, viewport16B, near/far8B,
    // projection flag4B. Header scale.xy holds native frame phase/mode.
    auto* bytes = reinterpret_cast<uint8_t*>(&r);
    std::memcpy(bytes+48,captured.material.data(),48);
    std::memcpy(bytes+96,captured.view_projection.data(),64);
    std::memcpy(bytes+176,captured.near_far.data(),8);
    std::memcpy(bytes+184,&captured.orthographic,4);
    std::memcpy(bytes+196,&captured.subtype,4); // 0 Hair, 1 Pubes; native dither FMA grouping.
    const float width=std::clamp(captured.material[7],0.0f,1.0f);
    const float edge0=std::fma(-width,0.5f,captured.material[6]),edge1=std::fma(width,0.5f,captured.material[6]);
    const bool zero_height=captured.material[4]==0.0f && captured.material[5]==0.0f;
    const bool defined_height=!a.channel || edge0<edge1 || zero_height;
    const float bounded_mask=(!a.channel || (unit_mask_format(t.format) && defined_height))?1.0f:0.0f;
    std::memcpy(bytes+188,&bounded_mask,4);
    r.scale[0]=captured.frame[0];r.scale[1]=captured.frame[1];
    if (a.channel) {
      auto* state=cmd_state(cmd);
      if (!state || !state->has_viewport) {++g_hair_invalid;return false;}
      const float viewport[4]={state->viewport.x,state->viewport.y,state->viewport.width,state->viewport.height};
      if (!finite(viewport,4) || viewport[2]==0 || viewport[3]==0) {++g_hair_invalid;return false;}
      std::memcpy(bytes+160,viewport,16);
    }
  } else if (a.has_scale) {
    if (!read_bound(cmd, a.ubo_set, a.ubo_bind, a.scale, r.scale, 8)) {
      ++g_fail[1];
      return false;
    }
  }
  if (!finite(r.p0, 8) || !finite(r.t, 32) || !finite(r.scale, 2)) {
    ++g_fail[3];
    return false;
  }
  return true;
}

bool alpha_capture_source(VkCommandBuffer cmd,const AlphaFs& a,uint32_t first_tri,uint32_t tris,AlphaSnapshot* out) {
  if(!out) return false;
  *out={};
  Bg3Binding t{},c{};
  if(!capture_source_material(cmd,a,first_tri,tris,out->rec,t,c)) return false;
  out->tex=t.view;out->tex_layout=t.layout;out->tex_id=view_image_id(t.view);
  if(a.kind==1 || a.kind==4) {
    out->cache=c.view;out->cache_layout=c.layout;out->cache_id=view_image_id(c.view);
  }
  // Reject unidentified/recycled resources before a private descriptor tuple can
  // be published. Camera ordinary adds retain their previous lookup/cost path.
  return out->tex_id && ((a.kind!=1 && a.kind!=4) || out->cache_id);
}

bool alpha_add_draw(VkCommandBuffer cmd,const AlphaFs& a,uint32_t first_tri,uint32_t tris,uint32_t* rec_index) {
  AlphaRec r{};Bg3Binding t{},c{};
  if(!capture_source_material(cmd,a,first_tri,tris,r,t,c)) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_recs.size() >= kMaxRecords) {
    ++g_fail[4];
    if (!g_full_logged) {
      g_full_logged = true;
      log("[alpha] WARNING record list full (%u draws): further alpha-tested draws are made OPAQUE", kMaxRecords);
    }
    return false;
  }
  r.tex = slot_of(g_tex, kTextures, t.view, t.layout);
  r.cache = a.kind == 1 ? slot_of(g_cache, kCaches, c.view, c.layout) :
            a.kind == 4 ? slot_of(g_tex, kTextures, c.view, c.layout) : 0;
  if (!r.tex || ((a.kind == 1 || a.kind == 4) && !r.cache)) {
    ++g_fail[5];
    if (!g_full_logged) {
      g_full_logged = true;
      log("[alpha] WARNING view list full (%zu textures of %u, %zu tile caches of %u): further alpha-tested draws are "
          "made OPAQUE", g_tex.size(), kTextures - 1, g_cache.size(), kCaches - 1);
    }
    return false;
  }
  if (rec_index) *rec_index = uint32_t(g_recs.size());
  g_recs.push_back(r);
  if (r.kind == 3) {if(r.p0[2]!=0.0f) ++g_scalp_dither; else ++g_scalp_hard;}
  if (r.kind == 4) ++g_hair_records;
  return true;
}

// The legacy scene's InstInfo (shaders/rt/rt_common.hlsli): its one TLAS instance
// is the 2-geometry soup BLAS, looked up the old way (kInfoLegacy).
bool alpha_legacy_info(VkCommandBuffer cmd, uint32_t stride, VkBuffer* buf, VkDeviceSize* size) {
  if (!g_on) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_legacy_buf) {
    if (!make_buffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, g_legacy_buf, g_legacy_mem))
      return false;
    g_legacy_stride = 0;
  }
  if (g_legacy_stride != stride) {
    const VkDeviceFuncs& vk = g_dd->vk;
    uint32_t info[4] = {0, (stride & 0xffu) | (1u << 9), 0, 0};   // kInfoLegacy
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0,
                          nullptr);
    vk.CmdUpdateBuffer(cmd, g_legacy_buf, 0, sizeof info, info);
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr,
                          0, nullptr);
    g_legacy_stride = stride;
  }
  *buf = g_legacy_buf;
  *size = 256;
  return true;
}

uint32_t alpha_record_count() {
  std::lock_guard<std::mutex> lock(g_mu);
  return uint32_t(g_recs.size());
}

bool alpha_snapshot(uint32_t index, AlphaSnapshot* out) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (index >= g_recs.size()) return false;
  const AlphaRec& r = g_recs[index];
  if (!r.tex || r.tex > g_tex.size() || (r.kind == 1 && (!r.cache || r.cache > g_cache.size())) ||
      (r.kind == 4 && (!r.cache || r.cache > g_tex.size()))) return false;
  out->rec = r;
  const auto t = g_tex[r.tex - 1];
  out->tex = t.first; out->tex_layout = t.second;
  if (r.kind == 1 || r.kind == 4) {
    const auto c = r.kind == 1 ? g_cache[r.cache - 1] : g_tex[r.cache - 1];
    out->cache = c.first; out->cache_layout = c.second;
  }
  // Keep both the view and its image alive until the last possible held use.
  std::vector<uint64_t> handles;
  {
    std::shared_lock<std::shared_mutex> tl(tracker().mu);
    for (VkImageView v : {out->tex, out->cache}) if (v) {
      handles.push_back(uint64_t(v));
      auto it = tracker().views.find(v);
      if (it != tracker().views.end()) handles.push_back(uint64_t(it->second.image));
    }
  }
  out->tex_id = view_image_id(out->tex);
  out->cache_id = view_image_id(out->cache);
  const uint64_t until = frame_index() + uint64_t((std::clamp)(cfg_int("rt_dyn_hold", 0), 0, 120));
  for (uint64_t h : handles) life_extend(h, until);
  return true;
}

uint32_t alpha_restore(const AlphaSnapshot& saved) {
  // A view destroyed during the hold (its handle possibly reused for another image,
  // e.g. a glyph or UI texture) must not be bound: the held draw is dropped instead.
  if (view_image_id(saved.tex) != saved.tex_id || !saved.tex_id ||
      ((saved.rec.kind == 1 || saved.rec.kind == 4) && (view_image_id(saved.cache) != saved.cache_id || !saved.cache_id))) {
    ++g_stale_views;
    return UINT32_MAX;
  }
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_recs.size() >= kMaxRecords) return UINT32_MAX;
  AlphaRec r = saved.rec;
  r.tex = slot_of(g_tex, kTextures, saved.tex, saved.tex_layout);
  r.cache = r.kind == 1 ? slot_of(g_cache, kCaches, saved.cache, saved.cache_layout) :
            r.kind == 4 ? slot_of(g_tex, kTextures, saved.cache, saved.cache_layout) : 0;
  if (!r.tex || ((r.kind == 1 || r.kind == 4) && !r.cache)) return UINT32_MAX;
  const uint32_t index = uint32_t(g_recs.size());
  g_recs.push_back(r);
  return index;
}

VkDescriptorSet alpha_commit(VkCommandBuffer cmd, const AlphaBindings& ab, uint32_t slot) {
  if (!g_on || slot >= kRing) return VK_NULL_HANDLE;
  const VkBuffer soup = ab.soup;
  const VkDeviceSize alpha_base = ab.alpha_base, alpha_bytes = ab.alpha_bytes, opaque_bytes = ab.opaque_bytes;
  const uint32_t stride = ab.stride;
  const VkDeviceFuncs& vk = g_dd->vk;
  std::lock_guard<std::mutex> lock(g_mu);
  load_table();
  if (!g_dummy_ready) {
    VkImageMemoryBarrier ib[2];
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (int i = 0; i < 2; ++i) {
      ib[i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      ib[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      ib[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      ib[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      ib[i].srcQueueFamilyIndex = ib[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      ib[i].image = i ? g_dummy_arr.image : g_dummy2d.image;
      ib[i].subresourceRange = range;
    }
    vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                          2, ib);
    VkClearColorValue one{{1, 1, 1, 1}};
    for (int i = 0; i < 2; ++i)
      vk.CmdClearColorImage(cmd, ib[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &one, 1, &range);
    vk.CmdFillBuffer(cmd, g_dummy_buf, 0, 256, 0);
    for (int i = 0; i < 2; ++i) {
      ib[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      ib[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      ib[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      ib[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0,
                          nullptr, 2, ib);
    g_dummy_ready = true;
  }
  // Records: a 16-byte header (count) and the records, in this slot's region.
  // The scene slot is retired: the GPU finished every frame that read it
  // (rtscene.cpp). The update is still ordered after earlier reads.
  const VkDeviceSize base = VkDeviceSize(slot) * kRegion;
  // header: record count, the opaque soup's vertex stride (0: not bound), the
  // alpha-test policy bits (rt_common.hlsli kAlpha*; live switches)
  const uint32_t policy = (cfg_int("rt_alpha_cut", 1) ? 1u : 0u) | (cfg_int("rt_alpha_stochastic", 1) ? 2u : 0u) |
                          (cfg_int("rt_alpha_unknown", 1) ? 4u : 0u) |
                          (geo_native_hair_enabled() && !cfg_int("rt_alpha_test",1) ? 8u : 0u);
  g_set_records[slot]=uint32_t(g_recs.size());
  uint32_t header[4] = {uint32_t(g_recs.size()), geo_native_hair_enabled() ? stride : (soup && opaque_bytes ? stride : 0u), policy, 0};
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  vk.CmdUpdateBuffer(cmd, g_rec_buf, base, sizeof header, header);
  const uint8_t* src = reinterpret_cast<const uint8_t*>(g_recs.data());
  VkDeviceSize left = VkDeviceSize(g_recs.size()) * sizeof(AlphaRec), off = 0;
  while (left) {
    VkDeviceSize n = (std::min)(left, VkDeviceSize(65536 / sizeof(AlphaRec) * sizeof(AlphaRec)));
    vk.CmdUpdateBuffer(cmd, g_rec_buf, base + 16 + off, n, src + off);
    off += n;
    left -= n;
  }
  // The scene stamp: the CPU expects `gen`, the GPU confirms it once this command
  // buffer runs (after the scene build recorded before this call).
  if (++g_stamp_gen == 0) g_stamp_gen = 1;
  const VkDeviceSize stamp_base = VkDeviceSize(slot) * kStampStride;
  if(geo_native_hair_enabled())g_hair_bounds_invalid.fetch_add(g_stamp_map[stamp_base/4+3]);
  g_stamp_map[stamp_base / 4] = g_stamp_gen;
  // Snapshot the very same native UBO used by XFB conversion. This read is
  // executed on the GPU with the build, not a CPU observation at record time.
  const bool rebase = cfg_int("rt_scene_rebase", 0) != 0 || cfg_int("rt_primary_epoch", 0) != 0;
  const bool pose_ok = rebase && geo_copy_camera(cmd, ab.camera, ab.camera_offset, g_stamp_buf,
                                               stamp_base + kStampCameraOffset);
  const uint32_t gpu_stamp[2] = {g_stamp_gen, pose_ok ? g_stamp_gen : 0};
  vk.CmdUpdateBuffer(cmd, g_stamp_buf, stamp_base + 4, sizeof gpu_stamp, gpu_stamp);
  if(g_omm_layout)vk.CmdFillBuffer(cmd,g_stamp_buf,stamp_base+128u,32u,0u);
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr,
                        0, nullptr);

  // The set: records, UV soup, views (unused slots: the dummies).
  VkDescriptorBufferInfo rec{g_rec_buf, base, kRegion};
  VkDescriptorBufferInfo soupi = soup && alpha_bytes ? VkDescriptorBufferInfo{soup, alpha_base, alpha_bytes}
                                                     : VkDescriptorBufferInfo{g_dummy_buf, 0, 256};
  VkDescriptorBufferInfo opaquei = soup && opaque_bytes ? VkDescriptorBufferInfo{soup, 0, opaque_bytes}
                                                        : VkDescriptorBufferInfo{g_dummy_buf, 0, 256};
  if (ab.split && soup && ab.soup_size) {
    // Split: per-instance byte offsets into the whole soup (InstInfo.base).
    soupi = opaquei = VkDescriptorBufferInfo{soup, 0, ab.soup_size};
  }
  VkDescriptorBufferInfo infoi = ab.info && ab.info_bytes ? VkDescriptorBufferInfo{ab.info, ab.info_off, ab.info_bytes}
                                                          : VkDescriptorBufferInfo{g_dummy_buf, 0, 256};
  VkDescriptorBufferInfo pooli = ab.pool && ab.pool_bytes ? VkDescriptorBufferInfo{ab.pool, 0, ab.pool_bytes}
                                                          : VkDescriptorBufferInfo{g_dummy_buf, 0, 256};
  // Every slot must hold a live view: this frame's views, dummies after them.
  // Only the slots a set ever held real views in need rewriting (all of them the
  // first time a set is used): writing 4096 descriptors per frame is avoided.
  const uint32_t ntex = g_set_init[slot] ? (std::max)(g_set_tex[slot], uint32_t(g_tex.size())) + 1 : kTextures;
  const uint32_t ncache = g_set_init[slot] ? (std::max)(g_set_cache[slot], uint32_t(g_cache.size())) + 1 : kCaches;
  g_set_init[slot] = true;
  {
    std::vector<uint64_t>& hs = g_set_handles[slot];
    hs.clear();
    std::shared_lock<std::shared_mutex> tl(tracker().mu);
    for (const auto* list : {&g_tex, &g_cache})
      for (const auto& [v, l] : *list) {
        hs.push_back(uint64_t(v));
        auto it = tracker().views.find(v);
        if (it != tracker().views.end()) hs.push_back(uint64_t(it->second.image));
      }
    g_set_extended[slot] = 0;
  }
  g_set_tex[slot] = uint32_t(g_tex.size());
  g_set_cache[slot] = uint32_t(g_cache.size());
  std::vector<VkDescriptorImageInfo> tex(ntex), cache(ncache);
  for (uint32_t i = 0; i < ntex; ++i)
    tex[i] = i && i <= g_tex.size() ? VkDescriptorImageInfo{VK_NULL_HANDLE, g_tex[i - 1].first, g_tex[i - 1].second}
                                    : VkDescriptorImageInfo{VK_NULL_HANDLE, g_dummy2d.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  for (uint32_t i = 0; i < ncache; ++i)
    cache[i] = i && i <= g_cache.size()
                   ? VkDescriptorImageInfo{VK_NULL_HANDLE, g_cache[i - 1].first, g_cache[i - 1].second}
                   : VkDescriptorImageInfo{VK_NULL_HANDLE, g_dummy_arr.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorBufferInfo stampi{g_stamp_buf, stamp_base, kStampStride};
  VkWriteDescriptorSet w[8] = {};
  for (int i = 0; i < 8; ++i) {
    w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w[i].dstSet = g_sets[slot];
    w[i].dstBinding = i < 4 ? uint32_t(i) : uint32_t(i + 2);   // 0-3, then 6, 7, 8, 9
  }
  w[7].descriptorCount = 1, w[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[7].pBufferInfo = &stampi;
  w[4].descriptorCount = 1, w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[4].pBufferInfo = &opaquei;
  w[5].descriptorCount = 1, w[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[5].pBufferInfo = &infoi;
  w[6].descriptorCount = 1, w[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[6].pBufferInfo = &pooli;
  w[0].descriptorCount = 1, w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[0].pBufferInfo = &rec;
  w[1].descriptorCount = 1, w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[1].pBufferInfo = &soupi;
  w[2].descriptorCount = ntex, w[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, w[2].pImageInfo = tex.data();
  w[3].descriptorCount = ncache, w[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, w[3].pImageInfo = cache.data();
  vk.UpdateDescriptorSets(g_dd->device, 8, w, 0, nullptr);
  if(g_omm_layout){
    // No capable shader may execute before both additional bindings are valid.
    if(!ab.reference_tlas)return VK_NULL_HANDLE;
    VkBuffer epochs{};VkDeviceSize epoch_bytes{};
    omm::content_buffer(g_dd,&epochs,&epoch_bytes);
    VkDescriptorBufferInfo epoch{epochs?epochs:g_dummy_buf,0,epochs?epoch_bytes:256};
    VkWriteDescriptorSetAccelerationStructureKHR as{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    as.accelerationStructureCount=1;as.pAccelerationStructures=&ab.reference_tlas;
    VkWriteDescriptorSet extra[2]{};
    extra[0]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,&as};extra[0].dstSet=g_sets[slot];extra[0].dstBinding=10;
    extra[0].descriptorCount=1;extra[0].descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    extra[1]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};extra[1].dstSet=g_sets[slot];extra[1].dstBinding=11;
    extra[1].descriptorCount=1;extra[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;extra[1].pBufferInfo=&epoch;
    vk.UpdateDescriptorSets(g_dd->device,2,extra,0,nullptr);
    omm::publish_scene_capability(static_cast<uint32_t*>(persist(omm::scene_capability_key,sizeof(uint32_t))));
  }
  g_current = int(slot);
  if(native_hit::enabled() && !native_hit::attach(cmd,slot,g_sets[slot],g_dummy_buf,g_dummy_arr.view,g_clamp,g_stamp_gen))return VK_NULL_HANDLE;

  ++g_frames;
  g_draws += g_recs.size();
  for (const auto& r : g_recs) g_tris += r.tris;
  if (!g_logged && !g_recs.empty()) {
    g_logged = true;
    log("[alpha] first alpha-tested capture: %zu draws, %zu textures, %zu tile caches", g_recs.size(), g_tex.size(),
        g_cache.size());
  }
  int every = (std::max)(cfg_int("rt_timing_frames", 300), 30);
  if (g_frames >= uint64_t(every)) {
    log("[alpha] last %llu frames: %.1f alpha-tested draws, %.0f triangles, %zu textures, %zu tile caches per frame "
        "(capacity %u, %u, %u records); opaque fallbacks: no view %u, UBO %u, SSBO %u, non-finite %u, records full %u, "
        "views full %u; held records dropped (view destroyed/reused) %u",
        (unsigned long long)g_frames, double(g_draws) / g_frames, double(g_tris) / g_frames, g_tex.size(),
        g_cache.size(), kTextures - 1, kCaches - 1, kMaxRecords, g_fail[0], g_fail[1], g_fail[2], g_fail[3], g_fail[4],
        g_fail[5], g_stale_views.load());
    if (native_scalp::has_capability(static_cast<uint32_t*>(persist(native_scalp::capability_key, sizeof(uint32_t)))))
      log("[alpha scalp] native hard records %u; native camera dither records %u; invalid/missing native state %u; "
          "RT cone footprint, not native raster sample identity",
          g_scalp_hard.exchange(0), g_scalp_dither.exchange(0), g_scalp_invalid.exchange(0));
    if (geo_native_hair_enabled()) log("[alpha hair] native coverage records %u; invalid/missing native state %u; unsupported native interfaces total %u; retired bounds-invalid primitives %u; "
        "88-byte strand capture; procedural camera depth and physical Shadow ribbons",
        g_hair_records.exchange(0),g_hair_invalid.exchange(0),g_hair_attributes.load(),g_hair_bounds_invalid.exchange(0));
    // The alpha-tested fragment shaders of this frame by triangles, with their
    // cut-offs (identifies the families on screen: grass, foliage, chains).
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> per;
    for (const auto& r : g_recs) {
      auto& e = per[(uint64_t(r.fs_hi) << 32) | r.fs_lo];
      ++e.first;
      e.second += r.tris;
    }
    std::vector<std::pair<uint64_t, std::pair<uint32_t, uint32_t>>> top(per.begin(), per.end());
    std::sort(top.begin(), top.end(), [](const auto& x, const auto& y) { return x.second.second > y.second.second; });
    std::string list;
    for (size_t i = 0; i < top.size() && i < 12; ++i) {
      AlphaFs f{};
      auto it = g_table.find(top[i].first);
      if (it != g_table.end()) f = it->second;
      char b[96];
      snprintf(b, sizeof b, "%s%016llx %s cut %g/%g: %u draws %u tris", i ? "; " : "", (unsigned long long)top[i].first,
               f.kind == 1 ? "vt" : "tex", f.cut_first, f.cut, top[i].second.first, top[i].second.second);
      list += b;
    }
    log("[alpha] this frame's shaders by triangles (cut first/effective): %s", list.c_str());
    g_frames = g_draws = g_tris = 0;
    std::memset(g_fail, 0, sizeof g_fail);
  }
  return g_sets[slot];
}

// The scene slot `slot` was handed out in `frame`: the passes that bind its set are
// recorded then, in other command buffers than the build's (which noted the views
// for its own submission only). Noted as submitted in `frame`: a destroy waits for
// that frame's present fence. (A destroy before any fence covers it cannot wait;
// tracker.cpp counts it as "no fence yet".)
void alpha_slot_in_use(uint32_t slot, uint64_t frame) {
  native_hit::slot_in_use(slot,frame);
  if (!g_on || slot >= kRing) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_set_extended[slot] >= frame) return;
  g_set_extended[slot] = frame;
  for (uint64_t h : g_set_handles[slot]) life_extend(h, frame);
}

// The set layout is fixed for the device's lifetime; the set is the one of the
// latest scene build (VK_NULL_HANDLE before the first), from the same scene
// slot as rt_scene's TLAS (rt_scene_set returns both as one pair).
bool rt_alpha_set(VkDescriptorSetLayout* layout, VkDescriptorSet* set) {
  if (!g_on) return false;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    *layout = g_dsl;
  }
  // rtscene's lock after ours is released (rt_after_capture takes it before ours).
  VkAccelerationStructureKHR tlas;
  uint64_t built;
  if (!rt_scene_set(&tlas, set, &built)) *set = VK_NULL_HANDLE;
  return true;
}

// Called after alpha_commit releases its mutex: BVH capture uses the opposite
// BVH -> alpha lock order. The retired SceneSlot gives exclusive stamp ownership.
bool alpha_omm_commit(VkCommandBuffer cmd,uint32_t slot){
  VkBuffer stamp{};const uint32_t* mapped{};uint32_t generation{};
  {
    std::lock_guard lock(g_mu);
    if(!g_on || !g_omm_layout || slot>=kRing || !g_stamp_map)return false;
    stamp=g_stamp_buf;mapped=g_stamp_map+VkDeviceSize(slot)*kStampStride/4;generation=mapped[0];
  }
  return bvh_omm_commit(cmd,slot,stamp,VkDeviceSize(slot)*kStampStride,generation,mapped);
}

bool alpha_prepare_sun_clone(VkCommandBuffer cmd, uint32_t slot, VkDescriptorSet selected_alpha,
                             std::span<const SunCascadeCapture> cascades, AlphaSunClone* out) {
  if (!out) return false;
  *out = {};
  if (!g_on || !cmd || slot >= kRing || !selected_alpha || cascades.size()!=4) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  if (selected_alpha != g_sets[slot] || !g_set_init[slot] || !g_stamp_map) return false;
  const uint32_t expected=g_stamp_map[VkDeviceSize(slot)*kStampStride/4];
  if (!expected) return false;
  SunAlphaSlot& s=g_sun_alpha[slot];
  const auto& vk=g_dd->vk;
  if(!g_sun_dsl) {
    VkDescriptorSetLayoutBinding bindings[18]={
      {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {2,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,kTextures,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {3,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,kCaches,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {4,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,&g_wrap},
      {5,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,&g_clamp},
      {6,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {7,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {8,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {9,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {12,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {13,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {16,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {17,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {18,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {14,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {10,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {11,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount=18;ci.pBindings=bindings;
    if(vk.CreateDescriptorSetLayout(g_dd->device,&ci,nullptr,&g_sun_dsl)!=VK_SUCCESS) return false;
  }
  std::vector<AlphaRec> extra;
  std::vector<std::pair<VkImageView,VkImageLayout>> textures,caches;
  std::vector<uint32_t> dispatch(16,0);
  for(unsigned layer=0;layer<4;++layer) {
  const auto& cascade=cascades[layer];
  if(cascade.layer!=layer || !sun_coverage::contiguous_triangle_ranges(std::span<const SunCascadeDraw>(cascade.draws),cascade.soup.tris)) return false;
  dispatch[4*layer]=uint32_t(dispatch.size()*4);
  dispatch[4*layer+1]=uint32_t(cascade.draws.size());dispatch[4*layer+2]=cascade.soup.stride;dispatch[4*layer+3]=cascade.soup.tris;
  uint32_t next=0;
  for(const auto& draw:cascade.draws) {
    if(draw.first_triangle!=next || draw.triangles>cascade.soup.tris-next) return false;
    next+=draw.triangles;
    uint32_t record=0,state=draw.material==0?0u:2u;
    if(draw.material && draw.material!=UINT32_MAX && draw.material<=cascade.materials.size()) {
      const auto& snapshot=cascade.materials[draw.material-1];
      const auto& source=snapshot.rec;
      if(snapshot.tex_id && snapshot.tex_id==view_image_id(snapshot.tex) &&
         ((source.kind!=1 && source.kind!=4) || (snapshot.cache_id && snapshot.cache_id==view_image_id(snapshot.cache)))) {
        AlphaRec r=source;
        const uint32_t t=slot_of(textures,kTextures-g_set_tex[slot],snapshot.tex,snapshot.tex_layout);
        uint32_t c=0;
        if(source.kind==1) c=slot_of(caches,kCaches-g_set_cache[slot],snapshot.cache,snapshot.cache_layout);
        if(source.kind==4) c=slot_of(textures,kTextures-g_set_tex[slot],snapshot.cache,snapshot.cache_layout);
        if(t && ((source.kind!=1 && source.kind!=4) || c)) {
          r.tex=g_set_tex[slot]+t;
          r.cache=source.kind==1?g_set_cache[slot]+c:source.kind==4?g_set_tex[slot]+c:0;
          record=16u+uint32_t((uint64_t(g_set_records[slot])+extra.size())*sizeof(AlphaRec));
          extra.push_back(r);state=1;
          const uint64_t until=frame_index()+kRing+uint64_t((std::clamp)(cfg_int("rt_dyn_hold",0),0,120));
          for(auto view:{snapshot.tex,snapshot.cache}) if(view) {
            uint64_t image_handle=0;
            {std::shared_lock<std::shared_mutex> lock(tracker().mu);
             auto image=tracker().views.find(view);
             if(image!=tracker().views.end()) image_handle=uint64_t(image->second.image);}
            life_extend(uint64_t(view),until);if(image_handle) life_extend(image_handle,until);
          }
        }
      }
    }
    dispatch.insert(dispatch.end(),{draw.first_triangle,draw.triangles,record,state});
  }
  if(next!=cascade.soup.tris) return false;
  }
  if(uint64_t(g_set_records[slot])+extra.size()>kMaxRecords) return false;
  const uint32_t total_records=uint32_t(g_set_records[slot]+extra.size());
  auto ensure=[&](VkBuffer& buffer,VkDeviceMemory& memory,VkDeviceSize& capacity,VkDeviceSize bytes) {
    if(buffer && capacity>=bytes) return true;
    if(buffer) defer_destroy(BG3_DEFER_BUFFER,uint64_t(buffer));
    if(memory) defer_destroy(BG3_DEFER_MEMORY,uint64_t(memory));
    buffer=VK_NULL_HANDLE;memory=VK_NULL_HANDLE;capacity=0;
    if(!make_buffer(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,buffer,memory)) return false;
    capacity=bytes;return true;
  };
  const VkDeviceSize record_bytes=16+VkDeviceSize(total_records)*sizeof(AlphaRec);
  if(!ensure(s.records,s.records_memory,s.record_bytes,record_bytes) ||
     !ensure(s.dispatch,s.dispatch_memory,s.dispatch_bytes,dispatch.size()*4ull) ||
     !ensure(s.parameters,s.parameters_memory,s.parameter_bytes,1232)) return false;
  if (!s.pool) {
    VkDescriptorPoolSize sizes[4]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,13},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,kTextures+kCaches},{VK_DESCRIPTOR_TYPE_SAMPLER,2},
      {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets=1;pi.poolSizeCount=4;pi.pPoolSizes=sizes;
    if (vk.CreateDescriptorPool(g_dd->device,&pi,nullptr,&s.pool)!=VK_SUCCESS) return false;
  }
  if(!s.set) {
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool=s.pool;ai.descriptorSetCount=1;ai.pSetLayouts=&g_sun_dsl;
    if (vk.AllocateDescriptorSets(g_dd->device,&ai,&s.set)!=VK_SUCCESS) return false;
  }
  if (!s.set) return false;
  if (!s.stamp) {
    void* mapped=nullptr;
    if (!alloc_host_buffer(kStampStride,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           &s.stamp,&s.memory,&mapped)) return false;
    s.mapped=static_cast<uint32_t*>(mapped);
  }
  if (!s.mapped) return false;
  // This slot's previous consumer is retired. Without an approved private build
  // none of these zero words can authorize traversal of an unbuilt clone TLAS.
  std::memset(s.mapped,0,size_t(kStampStride));
  VkCopyDescriptorSet copies[6]{};
  const uint32_t bindings[6]={1,2,3,6,7,8};
  for(unsigned i=0;i<6;++i) {
    copies[i]={VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
    copies[i].srcSet=selected_alpha;copies[i].srcBinding=bindings[i];
    copies[i].dstSet=s.set;copies[i].dstBinding=bindings[i];
    copies[i].descriptorCount=bindings[i]==2?kTextures:bindings[i]==3?kCaches:1u;
  }
  // Copy first: Vulkan applies descriptor copies AFTER writes in one call.
  vk.UpdateDescriptorSets(g_dd->device,0,nullptr,6,copies);
  VkDescriptorBufferInfo buffers[9]={{s.stamp,0,kStampStride},{s.records,0,record_bytes},{s.dispatch,0,dispatch.size()*4ull}};
  for(unsigned layer=0;layer<4;++layer) {
    const auto& soup=cascades[layer].soup;buffers[3+layer]={soup.xfb,0,VkDeviceSize(soup.tris)*3*soup.stride};
  }
  buffers[7]={s.parameters,0,1232};
  buffers[8]={g_dummy_buf,0,256};
  const uint32_t bindings_out[9]={9,0,12,13,16,17,18,14,11};
  VkWriteDescriptorSet writes[9]{};
  for(unsigned i=0;i<9;++i) {
    writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=s.set;writes[i].dstBinding=bindings_out[i];
    writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&buffers[i];
  }
  vk.UpdateDescriptorSets(g_dd->device,9,writes,0,nullptr);
  auto images=[&](uint32_t binding,uint32_t base,const auto& views) {
    std::vector<VkDescriptorImageInfo> info;
    for(auto [view,layout]:views) info.push_back({VK_NULL_HANDLE,view,layout});
    if(info.empty()) return;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=s.set;write.dstBinding=binding;
    write.dstArrayElement=base+1;write.descriptorCount=uint32_t(info.size());write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo=info.data();vk.UpdateDescriptorSets(g_dd->device,1,&write,0,nullptr);
  };
  images(2,g_set_tex[slot],textures);images(3,g_set_cache[slot],caches);
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT;
  barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
  vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
  VkBufferCopy cp{VkDeviceSize(slot)*kStampStride,0,kStampStride};
  vk.CmdCopyBuffer(cmd,g_stamp_buf,s.stamp,1,&cp);
  VkBufferCopy records{VkDeviceSize(slot)*kRegion,0,16+VkDeviceSize(g_set_records[slot])*sizeof(AlphaRec)};
  vk.CmdCopyBuffer(cmd,g_rec_buf,s.records,1,&records);
  barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
  vk.CmdUpdateBuffer(cmd,s.records,0,4,&total_records);
  auto upload=[&](VkBuffer buffer,VkDeviceSize offset,const void* data,VkDeviceSize bytes) {
    const auto* src=static_cast<const uint8_t*>(data);
    for(VkDeviceSize copied=0;copied<bytes;) {
      const VkDeviceSize count=(std::min)(VkDeviceSize(65536),bytes-copied);
      vk.CmdUpdateBuffer(cmd,buffer,offset+copied,count,src+copied);copied+=count;
    }
  };
  if(!extra.empty()) upload(s.records,16+VkDeviceSize(g_set_records[slot])*sizeof(AlphaRec),extra.data(),extra.size()*sizeof(AlphaRec));
  upload(s.dispatch,0,dispatch.data(),dispatch.size()*4ull);
  // byte12 belongs to Hair, and must survive the source stamp copy.
  vk.CmdFillBuffer(cmd,s.stamp,16,8,0);
  vk.CmdFillBuffer(cmd,s.stamp,128,32,0); // no micromaps in this dedicated Sun scene
  out->set=s.set;out->layout=g_sun_dsl;out->stamp=s.stamp;out->parameters=s.parameters;out->expected_generation=expected;
  return true;
}

bool alpha_sun_bind_scene(uint32_t slot,VkAccelerationStructureKHR tlas,VkBuffer epochs,VkDeviceSize bytes) {
  if(!g_on || slot>=kRing || !tlas) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  auto& s=g_sun_alpha[slot];if(!s.set) return false;
  VkWriteDescriptorSetAccelerationStructureKHR as{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
  as.accelerationStructureCount=1;as.pAccelerationStructures=&tlas;
  VkWriteDescriptorSet writes[2]{};
  writes[0]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[0].pNext=&as;writes[0].dstSet=s.set;
  writes[0].dstBinding=10;writes[0].descriptorCount=1;writes[0].descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  VkDescriptorBufferInfo epoch{epochs?epochs:g_dummy_buf,0,epochs?bytes:256};
  if(epochs && !bytes) return false;
  writes[1]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[1].dstSet=s.set;writes[1].dstBinding=11;
  writes[1].descriptorCount=1;writes[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[1].pBufferInfo=&epoch;
  g_dd->vk.UpdateDescriptorSets(g_dd->device,2,writes,0,nullptr);return true;
}

}  // namespace bg3
