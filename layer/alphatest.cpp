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

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bg3 {

namespace {

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
  std::memcpy(dst, mp->second + bm->second.second + b.offset + extra, n);
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
  VkDescriptorSetLayoutBinding b[10] = {
      {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},   // scene stamp
      {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},   // InstInfo per TLAS instance
      {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},   // the split's mesh pool
      {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kTextures, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCaches, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {4, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &g_wrap},
      {5, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &g_clamp}};
  VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dci.bindingCount = 10;
  dci.pBindings = b;
  ok = ok && vk.CreateDescriptorSetLayout(dd->device, &dci, nullptr, &g_dsl) == VK_SUCCESS;
  VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6 * kRing},
                                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (kTextures + kCaches) * kRing},
                                {VK_DESCRIPTOR_TYPE_SAMPLER, 2 * kRing}};
  VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pci.maxSets = kRing;
  pci.poolSizeCount = 3;
  pci.pPoolSizes = ps;
  ok = ok && vk.CreateDescriptorPool(dd->device, &pci, nullptr, &g_pool) == VK_SUCCESS;
  VkDescriptorSetLayout layouts[kRing];
  for (auto& l : layouts) l = g_dsl;
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = g_pool;
  ai.descriptorSetCount = kRing;
  ai.pSetLayouts = layouts;
  ok = ok && vk.AllocateDescriptorSets(dd->device, &ai, g_sets) == VK_SUCCESS;
  ok = ok && make_buffer(kRegion * kRing, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         g_rec_buf, g_rec_mem, true);
  if (ok) fault_track("alpha records (8 slot regions)", rt_buffer_address(g_rec_buf), kRegion * kRing);
  ok = ok && make_buffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, g_dummy_buf,
                         g_dummy_mem);
  ok = ok && make_image(false, g_dummy2d) && make_image(true, g_dummy_arr);
  if (ok) {   // the scene stamps: host-visible, persistently mapped, zeroed
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = kStampStride * kRing;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
}

void alpha_device_destroyed(DeviceData* dd) {
  if (dd != g_dd) return;
  const VkDeviceFuncs& vk = dd->vk;
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
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_table.find(fs);
  if (it == g_table.end()) return false;
  *out = it->second;
  return true;
}

void alpha_begin_capture() {
  std::lock_guard<std::mutex> lock(g_mu);
  g_recs.clear();
  g_tex.clear();
  g_cache.clear();
}

bool alpha_add_draw(VkCommandBuffer cmd, const AlphaFs& a, uint32_t first_tri, uint32_t tris, uint32_t* rec_index) {
  AlphaRec r{};
  r.first_tri = first_tri;
  r.tris = tris;
  r.kind = a.kind;
  r.channel = a.channel;
  r.cut = a.cut;
  r.cut_first = a.cut_first;
  r.fs_lo = uint32_t(a.hash);
  r.fs_hi = uint32_t(a.hash >> 32);
  r.scale[0] = r.scale[1] = a.k;
  Bg3Binding t;
  if (!query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, a.tex_set, a.tex_bind, 0, &t) || !t.view) {
    ++g_fail[0];
    return false;
  }
  Bg3Binding c{};
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
  r.cache = a.kind == 1 ? slot_of(g_cache, kCaches, c.view, c.layout) : 0;
  if (!r.tex || (a.kind == 1 && !r.cache)) {
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
                          (cfg_int("rt_alpha_unknown", 1) ? 4u : 0u);
  uint32_t header[4] = {uint32_t(g_recs.size()), soup && opaque_bytes ? stride : 0u, policy, 0};
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
  g_stamp_map[stamp_base / 4] = g_stamp_gen;
  // Snapshot the very same native UBO used by XFB conversion. This read is
  // executed on the GPU with the build, not a CPU observation at record time.
  const bool rebase = cfg_int("rt_scene_rebase", 0) != 0 || cfg_int("rt_primary_epoch", 0) != 0;
  const bool pose_ok = rebase && geo_copy_camera(cmd, ab.camera, ab.camera_offset, g_stamp_buf,
                                               stamp_base + kStampCameraOffset);
  const uint32_t gpu_stamp[2] = {g_stamp_gen, pose_ok ? g_stamp_gen : 0};
  vk.CmdUpdateBuffer(cmd, g_stamp_buf, stamp_base + 4, sizeof gpu_stamp, gpu_stamp);
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
  g_current = int(slot);

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
        "views full %u",
        (unsigned long long)g_frames, double(g_draws) / g_frames, double(g_tris) / g_frames, g_tex.size(),
        g_cache.size(), kTextures - 1, kCaches - 1, kMaxRecords, g_fail[0], g_fail[1], g_fail[2], g_fail[3], g_fail[4],
        g_fail[5]);
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

}  // namespace bg3
