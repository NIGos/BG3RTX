// Water reflections and composition (docs/PLAN_M5_WATER.md M5.2-M5.5).
// Enabled by the release presets together with rt_water_geometry. Reflections are
// composed into the forward water passes as described below:
//
//  - water_rf (core/gi.cpp run_post, after the GI composite and the debug views): two
//    dispatches of shaders/rt/water_reflect.hlsl. water_primary traces the water TLAS that
//    layer/water.cpp builds per submission and publishes through persist memory
//    (common/water_scene.h: no ABI change) against the bed depth; water_reflect traces one
//    mirror ray per visible water pixel into the scene TLAS (the lighting's TLAS + alpha set,
//    the pair every RT pass uses) and resolves it from the screen (tier (a)), writing
//    wOut = (L, E) for the composition and counting everything into a statistics ring.
//  - water_on_pass_end: rt_debug 21-24 (shaders/rt/water_view.hlsl) over the forward water
//    pass's own target, so the view shows on the water and not under it.
//  - every rt_timing_frames frames: "[water] reflections" (coverage, tiers, means, skips)
//    and "[water] reflections GPU" (the two dispatches, median and p90).
//
// M5.3 water composition (rt_water_compose; needs rt_water_reflections=1 and the key
// on when the water pipelines are created: layer/water.cpp patches the audited water FS then).
// The patched FS stores its probe-term factors per pixel (common/water_compose.h). Here:
//  - compose_at_lighting (in water_rf, after the reflection dispatches): the record buffer's
//    header (pitch), this frame's lit radiance / depth / class and camera constants copied
//    (water_copy, into one image pair and ONE camera block the GPU overwrites each frame), and
//    the stash the water passes use (push constants, statistics slot);
//  - compose_at_pass_end (water_on_pass_end, every forward pass whose first or last FS is
//    audited water or that recorded a supported water draw, with this frame's lighting stash or
//    the previous one's: the pass is mostly recorded before the lighting is flushed, and the GPU
//    runs the lighting first either way; common/water_compose.h lighting_use): water_compose
//    replaces the FS's probe term with the traced reflection where it resolves,
//    C' = max(C + A L - B, 0); rt_debug 25 the delta view;
//  - "[water] compose", "compose energy", "compose ratio", "compose GPU" every rt_timing_frames.
// Key off: nothing is allocated, dispatched or bound; core/gi.cpp only tests the key.
//
// M5.4 water shadows (rt_water_shadows; needs rt_water_reflections=1 and the key on when
// the water pipelines are created: the layer then patches the FS with the sun words, 40-byte
// records). The same lighting copy, pass and device-local counters as M5.3, with the WATER_SHADOWS
// variant of the compose shader: one sun ray per record with a sun term, C' = max(C - (1 - V) D, 0)
// (plus the reflection term when rt_water_compose is on too). "[water] shadows" every
// rt_timing_frames; rt_debug 26 the shadow view.
//
// M5.4b water local-light shadows (rt_water_ls_shadows; the same needs, 96-byte records with
// the sun's and the local-light words). The WATER_SHADOWS;WATER_LS compose walks each record's froxel
// list in the first water draw's own set 2 light buffers (captured at that draw, core/em_policy.cpp,
// pushed at 22..28), checks the recomputed sum against the FS's, and traces the lights the game leaves
// unshadowed: C' = max(C - F sum (1 - V_k) c_k, 0). A pass without the list gets kFlagLs cleared.
// "[water] local shadows" every rt_timing_frames; rt_debug 27 the view.
//
// M5.5 step 1 water refraction (rt_water_refraction; the same needs, 104-byte records: the
// local-light record plus the FS's refraction sample and its weight 1 - a). The WATER_SHADOWS;WATER_LS;
// WATER_REFR compose traces one refracted ray per transmissive record (Snell at the FS's N, n from its F0)
// and, where the hit resolves on screen, reads the water draw's own colour copy (its set 2 b4, captured at
// the draw, core/em_policy.cpp, pushed at 68) there: C' = max(C + (1 - a) (Copy(hit) - Refr), 0). A pass
// without the copy gets kFlagRefr cleared. "[water] refraction" every rt_timing_frames; rt_debug 28 the view.
//
// M5.4c the game's sun shadow on the water (rt_water_game_shadow; with rt_water_shadows, and both on
// when the water pipelines are created: 8 more bytes per record, common/water_compose.h kGshBytes). The FS
// that sample the game's cascade shadow map (PL_noCaustics_Shadows, huge_PL_Shadow) record the sun light that
// shadow removed (Dg) and its Vg; the ;WATER_GSH=1 compose variant gives a record lit by the traced sun ray
// C + Dg (occluded R and unknown C as M5.4): the game's Vg replaced by V. Without the words those FS record no
// sun term (the sun step skips them). "[water] game shadow" every rt_timing_frames; rt_debug 26 shows it.
#include "../common/water_build_camera.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "completion_lease.h"
#include <mutex>
#include <string>
#include <vector>

#include "core.h"
#include "../common/pipeline_async.h"
#include "../common/native_tileset_capability.h"
#include "../common/water_compose.h"
#include "../common/water_policy.h"
#include "../common/water_scene.h"
#include "../common/shader_variant.h"
#include "../common/native_scalp_capability.h"

namespace {

constexpr uint32_t kRing = 8;           // statistics / timestamp slots, read kRing frames later
constexpr uint32_t kStatWords = 32;
constexpr VkDeviceSize kStatBytes = 256;
constexpr VkDeviceSize kParamBytes = 256;
constexpr VkDeviceSize kRtStatsBytes = 64 * 1024;   // rt_common.hlsli guard counters (never read)
constexpr VkPipelineStageFlags kCS = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags kAll = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
constexpr uint32_t kGroup = 16;         // water_reflect.hlsl kGroup (numthreads kGroup x kGroup)

struct Params {   // shaders/rt/water_reflect.hlsl WaterParams
  float cam_build[3];
  float f0;
  uint32_t flags[4];
};
static_assert(sizeof(Params) == 32, "WaterParams is two float4");

std::mutex g_mu;
VkDescriptorSetLayout g_push = VK_NULL_HANDLE, g_alpha_layout = VK_NULL_HANDLE, g_view_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE, g_view_layout = VK_NULL_HANDLE;
uint32_t g_push_bytes = 0;
struct Pipe {
  struct Variant { VkPipeline p; uint64_t gen; std::string req; };
  VkPipeline p = VK_NULL_HANDLE;
  uint64_t gen = 0;
  std::string req;
  std::vector<Variant> cached;
};
void release_pipe(Pipe& p) {
  host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)p.p);
  for (const auto& v : p.cached) host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)v.p);
  p = Pipe{};
}
// rt_cascade_casters (live, as core/gi.cpp refresh): the water requests trace the cascade's masks.
bool cascade_on() { return host->cfg_int("rt_cascade_casters", 0) != 0; }
// Native scalp/hair records in the scene (the layer's capability word, as core/gi.cpp refresh): the
// requests that trace get RT_NATIVE_SCALP;RT_NATIVE_HAIR (shader_variant::water_request).
bool scalp_on() {
  return bg3::native_scalp::has_capability(
      static_cast<uint32_t*>(host->persist(bg3::native_scalp::capability_key, sizeof(uint32_t))));
}
Pipe g_primary, g_reflect, g_view;
bool g_building = false;   // a make_pipe this frame waits for its background build (not "unavailable")

Bg3Image g_out{}, g_t{};
uint32_t g_w = 0, g_h = 0;
bool g_img_init = false;
uint64_t g_out_frame = 0;      // frame whose lighting wrote wOut / wT (views need this frame's)
uint32_t g_out_w = 0, g_out_h = 0;

VkBuffer g_host = VK_NULL_HANDLE, g_rts = VK_NULL_HANDLE, g_dev = VK_NULL_HANDLE;
VkDeviceMemory g_host_mem = VK_NULL_HANDLE, g_rts_mem = VK_NULL_HANDLE, g_dev_mem = VK_NULL_HANDLE;
uint8_t* g_map = nullptr;
VkQueryPool g_qp = VK_NULL_HANDLE;
uint32_t g_slot = 0;
bool g_written[kRing] = {}, g_timed[kRing] = {};
CoreCompletionLease g_lease[kRing];
bool g_busy_logged = false;
uint64_t g_last_frame = UINT64_MAX;
bool g_failed_logged = false;

// The log window.
struct Window {
  uint64_t frames = 0, ran = 0;
  uint64_t skip[bg3::water::kUsableCount] = {};
  uint64_t skip_variant = 0, skip_layout = 0, skip_size = 0;
  uint64_t w[kStatWords] = {};
  uint64_t views = 0, view_passes = 0, view_passes_inner = 0;   // inner: water neither first nor last FS
  double sumE = 0, sumL = 0, sumEL = 0;
  std::vector<float> ms;
  uint64_t tris = 0, draws = 0, age = 0;
  float refl = 0, rough = 0, f0 = 0;
  uint32_t family = UINT32_MAX;
};
Window g_win;

void release_images() {
  for (Bg3Image* im : {&g_out, &g_t}) {
    host->defer_destroy(BG3_DEFER_IMAGE_VIEW, (uint64_t)im->view);
    host->defer_destroy(BG3_DEFER_IMAGE, (uint64_t)im->image);
    host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)im->memory);
    *im = Bg3Image{};
  }
  g_w = g_h = 0;
  g_img_init = false;
}

// A device-local buffer (the core/sharc.cpp pattern: the memory types from BG3_DEVICE_INFO_KEY).
// The shaders' atomics go here, never to host-visible memory: in the m5w2 live run the
// statistics' per-wave atomics into the host buffer (PCIe) cost ~15 ms per frame.
bool make_device_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& b, VkDeviceMemory& m) {
  const VkDeviceFuncs& vk = *host->vk;
  const auto* d = static_cast<const Bg3DeviceInfo*>(host->persist(BG3_DEVICE_INFO_KEY, sizeof(Bg3DeviceInfo)));
  if (!d || d->version < 1) return false;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  if (vk.CreateBuffer(host->device, &bci, nullptr, &b) != VK_SUCCESS) return b = VK_NULL_HANDLE, false;
  VkMemoryRequirements req;
  vk.GetBufferMemoryRequirements(host->device, b, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = UINT32_MAX;
  for (uint32_t i = 0; i < d->memory.memoryTypeCount && mai.memoryTypeIndex == UINT32_MAX; ++i)
    if ((req.memoryTypeBits & (1u << i)) && (d->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      mai.memoryTypeIndex = i;
  if (mai.memoryTypeIndex == UINT32_MAX || vk.AllocateMemory(host->device, &mai, nullptr, &m) != VK_SUCCESS ||
      vk.BindBufferMemory(host->device, b, m, 0) != VK_SUCCESS) {
    vk.DestroyBuffer(host->device, b, nullptr);
    if (m) vk.FreeMemory(host->device, m, nullptr);
    b = VK_NULL_HANDLE, m = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

bool ensure_buffers() {
  if (g_host) return true;
  void* m = nullptr;
  if (!host->alloc_host_buffer ||
      !make_device_buffer(kStatBytes,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          g_dev, g_dev_mem) ||
      !make_device_buffer(kRtStatsBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, g_rts, g_rts_mem) ||
      !host->alloc_host_buffer(kRing * (kStatBytes + kParamBytes),
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               &g_host, &g_host_mem, &m)) {
    for (VkBuffer* b : {&g_dev, &g_rts, &g_host}) host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)*b), *b = VK_NULL_HANDLE;
    for (VkDeviceMemory* dm : {&g_dev_mem, &g_rts_mem, &g_host_mem})
      host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)*dm), *dm = VK_NULL_HANDLE;
    return false;
  }
  g_map = static_cast<uint8_t*>(m);
  std::memset(g_map, 0, size_t(kRing * (kStatBytes + kParamBytes)));
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qi.queryCount = kRing * 2;
  if (host->vk->CreateQueryPool(host->device, &qi, nullptr, &g_qp) != VK_SUCCESS) g_qp = VK_NULL_HANDLE;
  return true;
}
VkDeviceSize stat_off(uint32_t slot) { return VkDeviceSize(slot) * kStatBytes; }
VkDeviceSize param_off(uint32_t slot) { return kRing * kStatBytes + VkDeviceSize(slot) * kParamBytes; }

bool make_pipe(Pipe& p, const char* base, const char* entry, VkPipelineLayout layout) {
  const std::string req = bg3::shader_variant::water_request(base, cascade_on(), scalp_on());
  const bool cascade_spec = req.find("WATER_SPEC_CASCADE=1") != std::string::npos;
  const uint32_t cascade_value = cascade_spec && cascade_on() ? 1u : 0u;
  const char* file = req.c_str();
  const bool native_spec = req.find("RT_SPEC_NATIVE=1") != std::string::npos;
  const bool edge_spec = req.find("WATER_SPEC_RF_EDGE=1") != std::string::npos;
  const bool shift_spec = cascade_spec && (req.find("rt/water_compose.hlsl") == 0);
  const uint32_t values[] = {native_spec && bg3::native_tileset::enabled(static_cast<uint32_t*>(host->persist(bg3::native_tileset::capability_key, sizeof(uint32_t)))) ? 1u : 0u, native_spec && scalp_on() ? 1u : 0u,
                            edge_spec && host->cfg_int("rt_water_rf_edge", 0) != 0 ? 1u : 0u,
                            cascade_value,
                            shift_spec && host->cfg_int("rt_water_compose_shift", 0) != 0 ? 1u : 0u,
                            edge_spec && host->cfg_int("rt_water_surface_fresnel",0) != 0 ? 1u : 0u,
                            native_spec && host->cfg_int("rt_hair_groom_shadow_t",0) != 0 ? 1u : 0u};
  VkSpecializationMapEntry entries[7]{};
  uint32_t count = 0;
  if (native_spec) {
    entries[count++] = {bg3::shader_variant::kSpecIdTileset, 0, sizeof(uint32_t)};
    entries[count++] = {bg3::shader_variant::kSpecIdScalpHair, sizeof(uint32_t), sizeof(uint32_t)};
    entries[count++] = {bg3::shader_variant::kSpecIdHairGroomShadowT, 6 * sizeof(uint32_t), sizeof(uint32_t)};
  }
  if (edge_spec) entries[count++] = {bg3::shader_variant::kSpecIdWaterRfEdge, 2 * sizeof(uint32_t), sizeof(uint32_t)};
  if (cascade_spec) entries[count++] = {bg3::shader_variant::kSpecIdCascade, 3 * sizeof(uint32_t), sizeof(uint32_t)};
  if (shift_spec) entries[count++] = {bg3::shader_variant::kSpecIdWaterComposeShift, 4 * sizeof(uint32_t), sizeof(uint32_t)};
  if(edge_spec) entries[count++] = {bg3::shader_variant::kSpecIdWaterSurfaceFresnel,5*sizeof(uint32_t),sizeof(uint32_t)};
  const VkSpecializationInfo info{count, entries, sizeof(values), values};
  const std::string pipe_key = req + "#groomshadow=" + std::to_string(values[6]) + "#tileset=" + std::to_string(values[0]) + "#hair=" + std::to_string(values[1]) + "#edge=" + std::to_string(values[2])
      + "#cascade=" + std::to_string(values[3]) + "#shift=" + std::to_string(values[4])
      + "#surface=" + std::to_string(values[5]);
  // Specialization toggles retain their pipelines until the owning layout is retired.
  if (p.req != pipe_key) {
    for (auto& v : p.cached) if (v.req == pipe_key) {
      std::swap(p.p, v.p); std::swap(p.gen, v.gen); std::swap(p.req, v.req);
      break;
    }
  }
  // A built request asks the layer for its generation (in memory, no compile: hot reload rebuilt it);
  // a replacement builds in the background while the previous specialization keeps running.
  const bool known = p.p != VK_NULL_HANDLE && p.req == pipe_key;
  if (known) {
    Bg3ShaderBlob blob{};
    if (!host->get_shader(file, entry, "cs_6_6", &blob) || blob.generation == p.gen) return true;
  }
  VkPipeline np = VK_NULL_HANDLE;
  uint64_t gen = 0;
  const int st = core_pipeline_async(file, entry, layout, &np, &gen, &info);
  if (st <= 0) {
    if (st == 0 && !known) g_building = true;   // a failure is logged by the build (and the layer's compile)
    p.p = bg3::pipe_async::select_pipeline(st < 0 ? bg3::pipe_async::State::kFailed : bg3::pipe_async::State::kBuilding, VkPipeline{}, p.p);
    return p.p != VK_NULL_HANDLE;
  }
  if (known) host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)p.p);
  else if (p.p) p.cached.push_back({p.p, p.gen, p.req});
  if (p.req != pipe_key) host->log("[water] %s %s: built (spec hair=%u edge=%u)", file, entry, values[1], values[2]);
  p.p = np;
  p.gen = gen;
  p.req = pipe_key;
  return true;
}

// The push set of water_reflect.hlsl (both entries; spirv-cross --reflect lists the union).
constexpr struct { uint32_t binding; VkDescriptorType type; } kBind[] = {
    {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},   {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
    {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},    {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
    {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},    {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    {18, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR}, {20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    {60, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},  {61, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    {62, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},   {63, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    {64, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR}, {65, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
    // rt_water_rr_hitdist (WATER_RR_HIT): RR's specular hit distance (core/gi.cpp g_rf_hit); a stand-in otherwise.
    {30, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
};
constexpr uint32_t kNumBind = uint32_t(sizeof kBind / sizeof kBind[0]);

bool ensure_layouts(VkDescriptorSetLayout alpha_layout, uint32_t push_bytes, bool rr_hit) {
  const VkDeviceFuncs& vk = *host->vk;
  VkDevice dev = host->device;
  if (g_layout && (g_alpha_layout != alpha_layout || g_push_bytes != push_bytes)) {
    release_pipe(g_primary);
    release_pipe(g_reflect);
    core_defer_layout_destroy(g_layout);
    g_primary = g_reflect = Pipe{};
    g_layout = VK_NULL_HANDLE;
    host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT,(uint64_t)g_push);g_push=VK_NULL_HANDLE;
  }
  if (!g_push) {
    std::vector<VkDescriptorSetLayoutBinding> b;
    for (const auto& x:kBind) b.push_back({x.binding,x.type,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    ci.bindingCount = uint32_t(b.size());
    ci.pBindings = b.data();
    if (vk.CreateDescriptorSetLayout(dev, &ci, nullptr, &g_push) != VK_SUCCESS) return g_push = VK_NULL_HANDLE, false;
  }
  if (!g_layout) {
    VkDescriptorSetLayout sets[2] = {g_push, alpha_layout};
    VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, push_bytes};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 2u;
    li.pSetLayouts = sets;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    if (vk.CreatePipelineLayout(dev, &li, nullptr, &g_layout) != VK_SUCCESS) return g_layout = VK_NULL_HANDLE, false;
    g_alpha_layout = alpha_layout;
    g_push_bytes = push_bytes;
  }
  return make_pipe(g_primary, "rt/water_reflect.hlsl", "water_primary", g_layout) &   // &: both builds start
         make_pipe(g_reflect, rr_hit ? "rt/water_reflect.hlsl?WATER_RR_HIT=1" : "rt/water_reflect.hlsl", "water_reflect", g_layout);
}


bool ensure_view() {
  const VkDeviceFuncs& vk = *host->vk;
  if (!g_view_dsl) {
    VkDescriptorSetLayoutBinding b[3];
    for (uint32_t i = 0; i < 3; ++i) b[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    ci.bindingCount = 3;
    ci.pBindings = b;
    if (vk.CreateDescriptorSetLayout(host->device, &ci, nullptr, &g_view_dsl) != VK_SUCCESS) return g_view_dsl = VK_NULL_HANDLE, false;
  }
  if (!g_view_layout) {
    VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &g_view_dsl;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    if (vk.CreatePipelineLayout(host->device, &li, nullptr, &g_view_layout) != VK_SUCCESS)
      return g_view_layout = VK_NULL_HANDLE, false;
  }
  return make_pipe(g_view, "rt/water_view.hlsl", "main", g_view_layout);
}

bool ensure_images(uint32_t w, uint32_t h) {
  if (g_out.image && g_w == w && g_h == h) return true;
  release_images();
  const VkImageUsageFlags use = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (!host->alloc_image(w, h, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_out) ||
      !host->alloc_image(w, h, VK_FORMAT_R32G32_SFLOAT, use, &g_t)) {
    release_images();
    return false;
  }
  g_w = w, g_h = h;
  host->log("[water] reflection images %ux%u (wOut RGBA16F, wT RG32F: %llu MB)", w, h,
            (unsigned long long)((uint64_t(w) * h * 16) >> 20));
  return true;
}

void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags src,
                   VkPipelineStageFlags dst) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  host->vk->CmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void memory_barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  host->vk->CmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

double u64(const uint32_t* w, uint32_t i) { return double(uint64_t(w[i]) | (uint64_t(w[i + 1]) << 32)); }

// Reads the slot about to be reused (written kRing frames ago) into the window.
void collect(uint32_t slot) {
  if (!g_written[slot] || !g_lease[slot].readable()) return;
  g_written[slot] = false;
  const uint32_t* w = reinterpret_cast<const uint32_t*>(g_map + stat_off(slot));
  if (w[0]) {
    ++g_win.ran;
    for (uint32_t i = 0; i < kStatWords; ++i)
      if (i < 16 || i > 21) g_win.w[i] += w[i];
    g_win.sumE += u64(w, 16) / 65536.0;
    g_win.sumL += u64(w, 18) / 4096.0;
    g_win.sumEL += u64(w, 20) / 4096.0;
  }
  if (g_timed[slot] && g_qp && bg3::logging::stats_enabled(host->cfg_int)) {
    uint64_t t[4] = {};
    if (host->vk->GetQueryPoolResults(host->device, g_qp, slot * 2, 2, sizeof t, t, 16,
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) == VK_SUCCESS &&
        t[1] && t[3] && t[2] >= t[0]) {
      const double ms = double(t[2] - t[0]) * host->cfg_float("gpu_timestamp_period_ns", 1.0f) * 1e-6;
      if (ms >= 0 && ms < 1000) g_win.ms.push_back(float(ms));
    }
  }
}

double pct(double a, double b) { return b > 0 ? 100.0 * a / b : 0.0; }

void maybe_log() {
  const uint64_t every = uint64_t(stats_period(unsigned((std::max)(host->cfg_int("rt_timing_frames", 300), 30))));
  if (++g_win.frames < every) return;
  Window& W = g_win;
  const uint64_t* w = W.w;
  const double px = double(w[1]), vis = double(w[4] + w[5]), rays = double(w[8]);
  static const char* kWhy[] = {"-", "no scene", "stale", "torn", "no TLAS"};
  log_stats("[water] reflections (%llu frames, %llu traced): water px %.2f%% of %.0f px/frame (in front %.2f%%, empty "
            "G-buffer %.2f%%, at bed %.2f%%), behind bed %llu (%.4f%% of water hits); rays %.0f/frame: screen hit %.1f%%, "
            "hidden %.1f%% (skin %llu), off screen %.1f%%, miss %.1f%%, aborted %llu, invalid %llu; normal fallback "
            "%.2f%%; mean E %.4f, mean screen L %.4f, mean E*L %.5f; shift %s; skipped frames: %s %llu, %s %llu, %s %llu, "
            "%s %llu, variant %llu, layout %llu, size %llu",
            (unsigned long long)W.frames, (unsigned long long)W.ran, pct(double(w[3]), px),
            W.ran ? px / double(W.ran) : 0.0, pct(double(w[4]), px), pct(double(w[5]), px), pct(double(w[6]), px),
            (unsigned long long)w[7], pct(double(w[7]), double(w[3])), W.ran ? rays / double(W.ran) : 0.0,
            pct(double(w[9]), rays), pct(double(w[10]), rays), (unsigned long long)w[22], pct(double(w[11]), rays),
            pct(double(w[12]), rays), (unsigned long long)w[13], (unsigned long long)w[14], pct(double(w[15]), vis),
            rays > 0 ? W.sumE / rays : 0.0, w[9] ? W.sumL / double(w[9]) : 0.0, w[9] ? W.sumEL / double(w[9]) : 0.0,
            W.ran && w[23] == W.ran ? "on" : w[23] ? "partial" : "off", kWhy[1], (unsigned long long)W.skip[1], kWhy[2],
            (unsigned long long)W.skip[2], kWhy[3], (unsigned long long)W.skip[3], kWhy[4], (unsigned long long)W.skip[4],
            (unsigned long long)W.skip_variant, (unsigned long long)W.skip_layout, (unsigned long long)W.skip_size);
  std::vector<float> ms = W.ms;
  std::sort(ms.begin(), ms.end());
  const double med = ms.empty() ? 0.0 : double(ms[ms.size() / 2]);
  const double p90 = ms.empty() ? 0.0 : double(ms[(std::min)(ms.size() - 1, ms.size() * 9 / 10)]);
  log_stats("[water] reflections GPU %.3f ms median, p90 %.3f (%zu timed; water primary + reflection dispatches); scene "
            "%.1f draws, %.0f triangles, age %.2f frames; F0 %.4f (Reflectance_Base %.3f, Roughness_Base %.3f, family %d); "
            "views %llu over %llu water passes (%llu with water between other draws)",
            med, p90, ms.size(), W.ran ? double(W.draws) / double(W.ran) : 0.0,
            W.ran ? double(W.tris) / double(W.ran) : 0.0, W.ran ? double(W.age) / double(W.ran) : 0.0, double(W.f0),
            double(W.refl), double(W.rough), W.family == UINT32_MAX ? -1 : int(W.family), (unsigned long long)W.views,
            (unsigned long long)W.view_passes, (unsigned long long)W.view_passes_inner);
  g_win = Window{};
}

// ---- M5.3 composition (rt_water_compose; shaders/rt/water_compose.hlsl, common/water_compose.h) ----
constexpr uint32_t kMaxComposePasses = 4;                 // timed compose dispatches per frame
constexpr uint32_t kCQ = 2 + 2 * kMaxComposePasses;      // timestamps per frame slot
// 32 M5.3 words + the M5.4 sun words + the M5.4b local-light words (64..90) + the M5.5 refraction words
// (96..119) of a 128-word slot.
constexpr uint32_t kCStatWords = 128; // M5.6 counters after the existing refraction table
constexpr VkDeviceSize kCStatBytes = 512, kCParamBytes = 256, kCCamBytes = 768;   // cam at 0, vp at 512
static_assert(kCStatBytes == bg3::water::kCsSlotWordsLs * 4 && kCStatWords * 4 <= kCStatBytes, "the slot holds every word");
constexpr VkDeviceSize kCVpOff = 512, kCSlotBytes = kCStatBytes + kCParamBytes;
// ONE camera block after the slots (common/water_compose.h lighting_use): every frame's copy
// overwrites it, the compose reads the camera of the lighting the GPU ran last, as wRad / wDep.
// Layout of g_cdev (DEVICE-LOCAL: the shaders' statistics atomics, the params and the camera all
// live here; m5w3-fg2 had them in host-visible memory and the compose's per-wave atomics over PCIe
// cost 7.2 ms at 275k records). g_chost (host-visible) only receives each slot's statistics,
// copied after every compose dispatch, outside the timed region; it uses the same slot offsets.
constexpr VkDeviceSize kCCamOff = kRing * kCSlotBytes, kCDevBytes = kCCamOff + kCCamBytes;
constexpr VkDeviceSize kCHostBytes = kRing * kCSlotBytes;

struct ComposeParams {   // shaders/rt/water_compose.hlsl ComposeParams
  uint32_t a[4];         // pitch, capacity, rt_debug, flags (bit 0 apply)
  float b[4];            // rough threshold
};
static_assert(sizeof(ComposeParams) == 32, "ComposeParams is two 16-byte vectors");

VkDescriptorSetLayout g_cpush = VK_NULL_HANDLE, g_calpha = VK_NULL_HANDLE;
VkPipelineLayout g_clayout = VK_NULL_HANDLE;
uint32_t g_cpush_bytes = 0;
Pipe g_copy, g_compose;
Bg3Image g_rad{}, g_dep{};
uint32_t g_cw = 0, g_ch = 0;
bool g_cimg_init = false, g_cbuf_zeroed = false;
uint64_t g_cbuf_seen = 0;   // the record buffer the zero fill was for
VkBuffer g_chost = VK_NULL_HANDLE, g_cdev = VK_NULL_HANDLE;
VkDeviceMemory g_chost_mem = VK_NULL_HANDLE, g_cdev_mem = VK_NULL_HANDLE;
uint8_t* g_cmap = nullptr;
bool g_csun = false;   // M5.4: the WATER_SHADOWS compose (40-byte records)
bool g_cls = false;    // M5.4b: the WATER_SHADOWS;WATER_LS compose (96-byte records, bindings 22..28)
bool g_crr = false;    // M5.5: the WATER_SHADOWS;WATER_LS;WATER_REFR compose (104-byte records, binding 68)
bool g_cgsh = false;   // M5.4c: the ;WATER_GSH=1 variant of the above (the records + kGshBytes)
VkQueryPool g_cqp = VK_NULL_HANDLE;
uint32_t g_cslot = 0;
bool g_cwritten[kRing] = {}, g_ctimed[kRing] = {};
CoreCompletionLease g_clease[kRing];
bool g_cbusy_logged = false;
uint32_t g_cpasses[kRing] = {};
// What the composition at the end of the forward water passes needs from this frame's lighting.
struct Stash {
  uint64_t hit_identity=0;
  uint64_t frame = 0, build_frame = 0;
  VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
  VkDescriptorSet alpha = VK_NULL_HANDLE;
  std::vector<uint8_t> push;
  uint32_t w = 0, h = 0, slot = 0;
  uint32_t flags = 0;   // ComposeParams.a.w as the lighting wrote it
  bg3::water::ComposeBuffer buf{};
};
Stash g_st;
struct CWindow {
  uint64_t frames = 0, ran = 0, passes = 0;
  uint64_t w[kCStatWords] = {};
  double sumC = 0, sumB = 0, sumAL = 0, sumPos = 0, sumNeg = 0, sumBown = 0;
  double sumD = 0, sumRm = 0;   // M5.4: lum(D) and the removed lum((1 - V) D) over sun records
  uint64_t sun_passes = 0, no_sun_records = 0;
  // M5.4b: lum(F L), removed, lum(F sum) and lum(F L) over checked records, the projector share;
  // passes with the light list (and without: the flag cleared for that pass), buffers without the words.
  double sumLsL = 0, sumLsRm = 0, sumLsChk = 0, sumLsChkL = 0, sumLsBox = 0;
  uint64_t ls_passes = 0, ls_no_lights = 0, no_ls_records = 0;
  // M5.5: lum((1 - a) Refr) and lum((1 - a) Copy(hit)) over composed, lum((1 - a) Refr) over transmissive
  // records, the hit's screen shift (px) over composed, (1 - a) over hidden and over traced rays; passes
  // with the water draw's colour copy (and without: the flag cleared for that pass), buffers without the words.
  double sumRrGame = 0, sumRrNew = 0, sumRrAll = 0, sumRrShift = 0, sumRrWHidden = 0, sumRrWTraced = 0;
  uint64_t rr_passes = 0, rr_no_copy = 0, no_rr_records = 0;
  // M5.4c: lum(Dg) over game-shadowed records, the restored lum(Dg), Vg over game-shadowed records; buffers
  // without the words while the key is on.
  double sumGshDg = 0, sumGshRestored = 0, sumGshVg = 0;
  uint64_t no_gsh_records = 0;
  std::vector<float> ms, ms_copy, ms_comp;   // per timed frame: total, the lighting copy, the compose dispatches
  uint64_t no_buffer = 0, layout = 0, size = 0, untimed = 0;
  // Passes: on this frame's lighting stash, on the previous lighting's (the pass recorded first),
  // refused (no recent lighting, no scene to stamp), and a newer scene than the stash's taken.
  uint64_t this_frame = 0, earlier = 0, no_lighting = 0, no_scene = 0, newer_scene = 0;
  // The "[water] compose stability" line: the no-lighting refusals by cause (lighting_refusal), why
  // an earlier lighting's pass did not find the stash's pair current (the current slot stale, a newer
  // build older than this frame, the newer pair unavailable), and the stash's pair kept (pick_scene).
  uint64_t nl_cleared = 0, nl_old = 0, nl_ahead = 0;
  uint64_t cur_stale = 0, newer_old = 0, newer_failed = 0, stash_kept = 0;
  uint64_t camera_samples = 0, camera_missing = 0, tlas_age_sum = 0, tlas_age_max = 0, tlas_age_samples = 0;
  double camera_delta_sum = 0, camera_delta_max = 0;
};
CWindow g_cwin;
bg3::water::ComposeContinuity g_ccont;   // across windows (the counts reset with each line)

VkDeviceSize cslot_off(uint32_t slot) { return VkDeviceSize(slot) * kCSlotBytes; }

bool compose_wanted() { return host->cfg_int("rt_water_compose", 0) != 0 && water_rf_wanted(); }
bool shadows_wanted() { return host->cfg_int("rt_water_shadows", 0) != 0 && water_rf_wanted(); }
bool ls_wanted() { return host->cfg_int("rt_water_ls_shadows", 0) != 0 && water_rf_wanted(); }
bool refr_wanted() { return host->cfg_int("rt_water_refraction", 0) != 0 && water_rf_wanted(); }
bool gsh_wanted() { return host->cfg_int("rt_water_game_shadow", 0) != 0 && shadows_wanted(); }

void release_compose_images() {
  for (Bg3Image* im : {&g_rad, &g_dep}) {
    host->defer_destroy(BG3_DEFER_IMAGE_VIEW, (uint64_t)im->view);
    host->defer_destroy(BG3_DEFER_IMAGE, (uint64_t)im->image);
    host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)im->memory);
    *im = Bg3Image{};
  }
  g_cw = g_ch = 0;
  g_cimg_init = false;
}

bool ensure_compose_images(uint32_t w, uint32_t h) {
  if (g_rad.image && g_cw == w && g_ch == h) return true;
  release_compose_images();
  const VkImageUsageFlags use = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (!host->alloc_image(w, h, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_rad) ||
      !host->alloc_image(w, h, VK_FORMAT_R32_SFLOAT, use | VK_IMAGE_USAGE_SAMPLED_BIT, &g_dep)) {
    release_compose_images();
    return false;
  }
  g_cw = w, g_ch = h;
  host->log("[water] compose images %ux%u (radiance RGBA16F, depth R32F: %llu MB)", w, h,
            (unsigned long long)((uint64_t(w) * h * 12) >> 20));
  return true;
}

// The push set of water_compose.hlsl (both entries; spirv-cross --reflect lists the union).
constexpr struct { uint32_t binding; VkDescriptorType type; } kCBind[] = {
    {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},   {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
    {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},    {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
    {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},    {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    {18, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR}, {20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    {60, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},  {61, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    {62, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},   {63, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    {64, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},  {66, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    {67, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    // Reserved descriptor: valid stand-in, not read by the shader.
    {69, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    // M5.4b (WATER_LS only, the layout's tail): the water draw's set 2 light list (bg3_lights.hlsli 22..28).
    {22, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},  {23, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    {24, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},  {25, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    {26, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},  {27, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    {28, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
    // M5.5 (WATER_REFR only, after the light list): the water draw's colour copy (its set 2 b4).
    {68, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
};
constexpr uint32_t kNumCBindRefr = uint32_t(sizeof kCBind / sizeof kCBind[0]);
constexpr uint32_t kNumCBindLs = kNumCBindRefr - 1;   // M5.4b: the layout with the light list
constexpr uint32_t kNumCBind = kNumCBindLs - 7;       // M5.3 / M5.4: the layout without the light list
uint32_t g_cpush_nb = 0;   // the binding count g_cpush was made with

bool ensure_compose(VkDescriptorSetLayout alpha_layout, uint32_t push_bytes, bool sun, bool ls, bool refr, bool gsh) {
  const VkDeviceFuncs& vk = *host->vk;
  VkDevice dev = host->device;
  if (g_compose.p && (g_csun != sun || g_cls != ls || g_crr != refr || g_cgsh != gsh)) {   // the record layout of the buffer decides the variant
    release_pipe(g_compose);
    g_compose = Pipe{};
  }
  const uint32_t nb = refr ? kNumCBindRefr : ls ? kNumCBindLs : kNumCBind;
  if (g_cpush && nb != g_cpush_nb) {   // the push layout differs by the light list and the copy
    release_pipe(g_copy);
    release_pipe(g_compose);
    core_defer_layout_destroy(g_clayout);
    host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_cpush);
    g_copy = g_compose = Pipe{};
    g_clayout = VK_NULL_HANDLE;
    g_cpush = VK_NULL_HANDLE;
    g_cpush_nb = 0;
  }
  g_csun = sun;
  g_cls = ls;
  g_crr = refr;
  g_cgsh = gsh;
  if (!g_chost) {
    void* m = nullptr;
    if (!make_device_buffer(kCDevBytes,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            g_cdev, g_cdev_mem))
      return false;
    if (!host->alloc_host_buffer(kCHostBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &g_chost, &g_chost_mem, &m)) {
      host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)g_cdev);
      host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_cdev_mem);
      g_cdev = VK_NULL_HANDLE, g_cdev_mem = VK_NULL_HANDLE;
      g_chost = VK_NULL_HANDLE, g_chost_mem = VK_NULL_HANDLE;
      return false;
    }
    g_cmap = static_cast<uint8_t*>(m);
    std::memset(g_cmap, 0, size_t(kCHostBytes));
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kRing * kCQ;
    if (vk.CreateQueryPool(dev, &qi, nullptr, &g_cqp) != VK_SUCCESS) g_cqp = VK_NULL_HANDLE;
  }
  if (g_clayout && (g_calpha != alpha_layout || g_cpush_bytes != push_bytes)) {
    release_pipe(g_copy);
    release_pipe(g_compose);
    core_defer_layout_destroy(g_clayout);
    g_copy = g_compose = Pipe{};
    g_clayout = VK_NULL_HANDLE;
    host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT,(uint64_t)g_cpush);g_cpush=VK_NULL_HANDLE;
  }
  if (!g_cpush) {
    std::vector<VkDescriptorSetLayoutBinding> b;
    for(uint32_t i=0;i<nb;++i) b.push_back({kCBind[i].binding,kCBind[i].type,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    ci.bindingCount = uint32_t(b.size());
    ci.pBindings = b.data();
    if (vk.CreateDescriptorSetLayout(dev, &ci, nullptr, &g_cpush) != VK_SUCCESS) return g_cpush = VK_NULL_HANDLE, false;
    g_cpush_nb = nb;
  }
  if (!g_clayout) {
    VkDescriptorSetLayout sets[2] = {g_cpush, alpha_layout};
    VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, push_bytes};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 2u;
    li.pSetLayouts = sets;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    if (vk.CreatePipelineLayout(dev, &li, nullptr, &g_clayout) != VK_SUCCESS) return g_clayout = VK_NULL_HANDLE, false;
    g_calpha = alpha_layout;
    g_cpush_bytes = push_bytes;
  }
  // rt_water_rf_edge: the screen-edge fade constant of the same request.
  const char* form =
                   refr && gsh ? "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1;WATER_GSH=1"
                   : ls && gsh ? "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_GSH=1"
                   : gsh       ? "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_GSH=1"
                   : refr      ? "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1"
                   : ls        ? "rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1"
                   : sun       ? "rt/water_compose.hlsl?WATER_SHADOWS=1"
                               : "rt/water_compose.hlsl";
  const std::string creq = bg3::shader_variant::water_rf_edge_request(form, host->cfg_int("rt_water_rf_edge", 0) != 0);

  return make_pipe(g_copy, "rt/water_compose.hlsl", "water_copy", g_clayout) &
         make_pipe(g_compose, creq.c_str(), "water_compose", g_clayout);
}


// Reads the compose slot about to be reused (kRing frames later) into the window.
void collect_compose(uint32_t slot) {
  if (!g_cwritten[slot] || !g_clease[slot].readable()) return;
  g_cwritten[slot] = false;
  const uint32_t* w = reinterpret_cast<const uint32_t*>(g_cmap + cslot_off(slot));
  if (w[0]) {
    float camera[4]{};
    std::memcpy(camera, g_cmap + cslot_off(slot) + kCStatBytes, sizeof camera);
    if (camera[3] != 0) {
      const double delta = std::sqrt(double(camera[0])*camera[0] + double(camera[1])*camera[1] + double(camera[2])*camera[2]);
      ++g_cwin.camera_samples; g_cwin.camera_delta_sum += delta;
      g_cwin.camera_delta_max = (std::max)(g_cwin.camera_delta_max, delta);
    }
    ++g_cwin.ran;
    g_cwin.passes += w[0];
    using bg3::water::kCsLsRec, bg3::water::kCsLsSumL, bg3::water::kCsLsChkShWithin;
    using bg3::water::kCsRrRec, bg3::water::kCsRrSumGame;
    for (uint32_t i = 0; i < kCStatWords; ++i)   // the counters (the 64-bit sums below)
      if (bg3::water::compose_plain_counter(i))
        g_cwin.w[i] += w[i];
    g_cwin.sumC += u64(w, 16) / 4096.0;
    g_cwin.sumB += u64(w, 18) / 4096.0;
    g_cwin.sumAL += u64(w, 20) / 4096.0;
    g_cwin.sumPos += u64(w, 22) / 4096.0;
    g_cwin.sumNeg += u64(w, 24) / 4096.0;
    g_cwin.sumBown += u64(w, 26) / 4096.0;
    g_cwin.sumD += u64(w, bg3::water::kCsSunSumD) / 4096.0;
    g_cwin.sumRm += u64(w, bg3::water::kCsSunSumRemoved) / 4096.0;
    g_cwin.sumLsL += u64(w, bg3::water::kCsLsSumL) / 4096.0;
    g_cwin.sumLsRm += u64(w, bg3::water::kCsLsSumRemoved) / 4096.0;
    g_cwin.sumLsChk += u64(w, bg3::water::kCsLsSumChk) / 4096.0;
    g_cwin.sumLsChkL += u64(w, bg3::water::kCsLsSumChkL) / 4096.0;
    g_cwin.sumLsBox += u64(w, bg3::water::kCsLsSumBox) / 4096.0;
    g_cwin.sumRrGame += u64(w, bg3::water::kCsRrSumGame) / 4096.0;
    g_cwin.sumRrNew += u64(w, bg3::water::kCsRrSumNew) / 4096.0;
    g_cwin.sumRrAll += u64(w, bg3::water::kCsRrSumAll) / 4096.0;
    g_cwin.sumRrShift += u64(w, bg3::water::kCsRrSumShift) / 4096.0;
    g_cwin.sumRrWHidden += u64(w, bg3::water::kCsRrSumRwHidden) / 4096.0;
    g_cwin.sumRrWTraced += u64(w, bg3::water::kCsRrSumRwTraced) / 4096.0;
    g_cwin.sumGshDg += u64(w, bg3::water::kCsGshSumDg) / 4096.0;
    g_cwin.sumGshRestored += u64(w, bg3::water::kCsGshSumRestored) / 4096.0;
    g_cwin.sumGshVg += u64(w, bg3::water::kCsGshSumVg) / 4096.0;
  }
  if (g_ctimed[slot] && g_cqp && bg3::logging::stats_enabled(host->cfg_int)) {
    const uint32_t n = 1 + (std::min)(g_cpasses[slot], kMaxComposePasses);
    uint64_t t[kCQ * 2] = {};
    bg3::water::ComposeMs ms;
    if (host->vk->GetQueryPoolResults(host->device, g_cqp, slot * kCQ, n * 2, sizeof t, t, 16,
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) == VK_SUCCESS &&
        bg3::water::compose_ms(t, n, host->cfg_float("gpu_timestamp_period_ns", 1.0f) * 1e-6, ms)) {
      g_cwin.ms.push_back(float(ms.copy + ms.compose));
      g_cwin.ms_copy.push_back(float(ms.copy));
      g_cwin.ms_comp.push_back(float(ms.compose));
    }
  }
  g_cpasses[slot] = 0;
}

void maybe_log_compose() {
  const uint64_t every = uint64_t(stats_period(unsigned((std::max)(host->cfg_int("rt_timing_frames", 300), 30))));
  if (++g_cwin.frames < every) return;
  CWindow& W = g_cwin;
  const uint64_t* w = W.w;
  using namespace bg3::water;
  const double rec = double(w[1]), own = double(w[2]), rays = double(w[kCsRays]), comp = double(w[kCsComposed]);
  const uint32_t t1 = kCsTier1;
  log_stats("[water] compose (%llu frames, %llu composed, %.2f passes/frame): records %.0f/frame, owned %.2f%% (not owned "
            "%.2f%%), invalid %llu, kept: rough %.2f%%, below the surface %.2f%%; rays %.0f/frame: screen hit (composed) "
            "%.1f%%, hidden %.1f%% (skin %llu), off screen %.1f%%, miss %.1f%%, aborted %llu, invalid %llu; unresolved "
            "(probe kept) %.2f%% of owned; clamped %llu; skipped frames: no record buffer %llu, layout %llu, size %llu; "
            "passes on this frame's lighting %llu, on the previous lighting %llu, refused: no recent lighting %llu, no "
            "scene %llu; newer scene taken %llu; TLAS age avg/max %.2f/%llu frames (%llu samples, camera missing %llu); build-camera delta avg/max %.6f/%.6f m (%llu last-pass samples)",
            (unsigned long long)W.frames, (unsigned long long)W.ran, W.ran ? double(W.passes) / double(W.ran) : 0.0,
            W.ran ? rec / double(W.ran) : 0.0, pct(own, rec), pct(rec - own, rec), (unsigned long long)w[4],
            pct(double(w[5]), own), pct(double(w[6]), own), W.ran ? rays / double(W.ran) : 0.0,
            pct(double(w[t1]), rays), pct(double(w[t1 + 1]), rays), (unsigned long long)w[kCsSkin],
            pct(double(w[t1 + 2]), rays), pct(double(w[t1 + 3]), rays), (unsigned long long)w[t1 + 4],
            (unsigned long long)w[t1 + 5], pct(own - comp, own), (unsigned long long)w[kCsClamped],
            (unsigned long long)W.no_buffer, (unsigned long long)W.layout, (unsigned long long)W.size,
            (unsigned long long)W.this_frame, (unsigned long long)W.earlier, (unsigned long long)W.no_lighting,
            (unsigned long long)W.no_scene, (unsigned long long)W.newer_scene,
            W.tlas_age_samples ? double(W.tlas_age_sum)/W.tlas_age_samples : 0.0,
            (unsigned long long)W.tlas_age_max, (unsigned long long)W.tlas_age_samples, (unsigned long long)W.camera_missing,
            W.camera_samples ? W.camera_delta_sum/W.camera_samples : 0.0, W.camera_delta_max, (unsigned long long)W.camera_samples);
  // Temporal stability (common/water_compose.h ComposeContinuity, pick_scene): the frames whose water
  // passes were all refused show the game's water for one frame (gate: uncomposed 0).
  log_stats("[water] compose stability (%llu frames): frames with a water pass %llu, composed %llu, uncomposed %llu "
            "(on/off flips %llu); refused passes: no recent lighting (cleared %llu, older than %llu frames %llu, ahead "
            "%llu), no scene %llu; earlier lighting: current slot stale %llu, newer build before this frame %llu, newer "
            "pair unavailable %llu, lighting's pair kept %llu; non-finite terms %llu (the game's colour kept: "
            "water_compose.h kCsNonFinite)",
            (unsigned long long)W.frames, (unsigned long long)g_ccont.frames, (unsigned long long)g_ccont.composed_frames,
            (unsigned long long)g_ccont.uncomposed, (unsigned long long)g_ccont.flips, (unsigned long long)W.nl_cleared,
            (unsigned long long)kLightingLagFrames, (unsigned long long)W.nl_old, (unsigned long long)W.nl_ahead,
            (unsigned long long)W.no_scene, (unsigned long long)W.cur_stale, (unsigned long long)W.newer_old,
            (unsigned long long)W.newer_failed, (unsigned long long)W.stash_kept,
            (unsigned long long)w[kCsNonFinite]);
  log_stats("[water] surface partition: coverage-weighted owned %llu, invalid %llu; coverage zero %llu, partial %llu, full %llu, native/missing FS marker %llu, unmarked/layout %llu, FS spec off %llu, compose spec off %llu; marked owned %llu, foam texture source %llu, edge/flow mask source %llu; histogram ceil(255c), weighted count wave-rounded; energy partition checked (8 float eps), model mean may change",
            (unsigned long long)w[kCsSurfaceClean], (unsigned long long)w[kCsSurfaceInvalid],
            (unsigned long long)w[kCsSurfaceFoamColour], (unsigned long long)w[kCsSurfaceFoamF0],
            (unsigned long long)w[kCsSurfaceFoamRough], (unsigned long long)w[kCsSurfaceNative],
            (unsigned long long)w[kCsSurfaceLayout], (unsigned long long)w[kCsSurfaceFsOff],
            (unsigned long long)w[kCsSurfaceComposeOff], (unsigned long long)w[kCsSurfaceMarked],
            (unsigned long long)w[kCsSurfaceFlow], (unsigned long long)w[kCsSurfaceEdge]);
  g_ccont.reset_counts();
  const double meanC = own > 0 ? W.sumC / own : 0.0, delta = W.sumPos - W.sumNeg;
  log_stats("[water] compose energy: mean C %.5f over owned; composed %.2f%% of owned: probe removed (B) %.5f, traced "
            "added (A L) %.5f, delta %+.5f (+%.5f / -%.5f) per composed pixel; mean delta over owned %+.6f = %+.3f%% of mean "
            "C; probe term B over owned %.5f",
            meanC, pct(comp, own), comp > 0 ? W.sumB / comp : 0.0, comp > 0 ? W.sumAL / comp : 0.0,
            comp > 0 ? delta / comp : 0.0, comp > 0 ? W.sumPos / comp : 0.0, comp > 0 ? W.sumNeg / comp : 0.0,
            own > 0 ? delta / own : 0.0, meanC > 0 && own > 0 ? 100.0 * (delta / own) / meanC : 0.0,
            own > 0 ? W.sumBown / own : 0.0);
  // L / (k Sp) per composed pixel (water_compose.h ratio_bin): one bin = a unit factor; a spread
  // with mass near 1 = the probe's error (sky where the hit geometry is), the physical case.
  const double rbins = double(w[kCsRatioLow] + w[kCsRatioMid] + w[kCsRatioHigh]);
  log_stats("[water] compose ratio A L / B over %.0f composed px: below %.1f %.1f%%, %.1f..%.1f %.1f%%, above %.1f %.1f%%; "
            "ratio of the means %.3f",
            rbins, double(kRatioLow), pct(double(w[kCsRatioLow]), rbins), double(kRatioLow), double(kRatioHigh),
            pct(double(w[kCsRatioMid]), rbins), double(kRatioHigh), pct(double(w[kCsRatioHigh]), rbins),
            W.sumB > 0 ? W.sumAL / W.sumB : 0.0);
  auto quant = [](std::vector<float> v, double& med, double& p90) {
    std::sort(v.begin(), v.end());
    med = v.empty() ? 0.0 : double(v[v.size() / 2]);
    p90 = v.empty() ? 0.0 : double(v[(std::min)(v.size() - 1, v.size() * 9 / 10)]);
  };
  double med, p90, cmed, cp90, dmed, dp90;
  quant(W.ms, med, p90);
  quant(W.ms_copy, cmed, cp90);
  quant(W.ms_comp, dmed, dp90);
  log_stats("[water] compose GPU %.3f ms median, p90 %.3f (%zu timed frames: the lighting copy + the compose dispatches; "
            "%llu passes beyond %u untimed); copy %.3f ms median, p90 %.3f; compose dispatches %.3f ms median, p90 %.3f",
            med, p90, W.ms.size(), (unsigned long long)W.untimed, kMaxComposePasses, cmed, cp90, dmed, dp90);
  if (g_csun || W.no_sun_records || host->cfg_int("rt_water_shadows", 0)) {
    // M5.4: the traced sun visibility on the water (WATER_SHADOWS words, common/water_compose.h).
    const double srec = double(w[kCsSunRec]), straced = double(w[kCsSunTraced]), socc = double(w[kCsSunOccluded]);
    const double meanD = srec > 0 ? W.sumD / srec : 0.0, meanRm = srec > 0 ? W.sumRm / srec : 0.0;
    const double removed_own = own > 0 ? W.sumRm / own : 0.0;
    log_stats("[water] shadows (%llu frames, %llu passes, variant %s): sun records %.0f/frame = %.2f%% of owned (no sun "
              "term %.2f%%, invalid %llu); traced %.2f%% of sun records, occluded %.2f%% of traced (traced shadow share "
              "%.2f%% of sun records), unknown %llu (kept); sun term D %.5f per sun record, removed %.5f = %.2f%% of D; "
              "mean removed over owned %.6f = %.3f%% of mean C; clamped %llu; GPU %.3f ms median, p90 %.3f (the "
              "compose dispatches with the sun ray + the lighting copy); skipped: records without the sun words %llu",
              (unsigned long long)W.frames, (unsigned long long)W.sun_passes, g_csun ? "sun" : "none",
              W.ran ? srec / double(W.ran) : 0.0, pct(srec, own), pct(double(w[kCsSunNone]), own),
              (unsigned long long)w[kCsSunInvalid], pct(straced, srec), pct(socc, straced), pct(socc, srec),
              (unsigned long long)w[kCsSunUnknown], meanD, meanRm, meanD > 0 ? 100.0 * meanRm / meanD : 0.0, removed_own,
              meanC > 0 ? 100.0 * removed_own / meanC : 0.0, (unsigned long long)w[kCsSunClamped], med, p90,
              (unsigned long long)W.no_sun_records);
  }
  if (g_cgsh || W.no_gsh_records || host->cfg_int("rt_water_game_shadow", 0)) {
    // M5.4c: the game's sun shadow replaced on the FS that sample it (WATER_GSH words, common/water_compose.h).
    const double grec = double(w[kCsGshRec]), grest = double(w[kCsGshRestored]);
    const double meanDg = grec > 0 ? W.sumGshDg / grec : 0.0, meanRest = grest > 0 ? W.sumGshRestored / grest : 0.0;
    const double restored_own = own > 0 ? W.sumGshRestored / own : 0.0;
    log_stats("[water] game shadow (%llu frames, variant %s): game-shadowed records %.0f/frame = %.2f%% of owned (invalid "
              "%llu), mean Vg %.4f; lit by the traced ray (restored C + Dg) %.2f%% of them; occluded where the game was "
              "lit %llu; Dg %.5f per game-shadowed record, restored %.5f per restored record; mean restored over owned "
              "%.6f = %.3f%% of mean C; skipped: records without the words %llu",
              (unsigned long long)W.frames, g_cgsh ? "gsh" : "none", W.ran ? grec / double(W.ran) : 0.0, pct(grec, own),
              (unsigned long long)w[kCsGshInvalid], grec > 0 ? W.sumGshVg / grec : 0.0, pct(grest, grec),
              (unsigned long long)w[kCsGshOccLit], meanDg, meanRest, restored_own,
              meanC > 0 ? 100.0 * restored_own / meanC : 0.0, (unsigned long long)W.no_gsh_records);
  }
  if (g_cls || W.no_ls_records || host->cfg_int("rt_water_ls_shadows", 0)) {
    // M5.4b: the traced local-light visibility on the water (WATER_LS words, common/water_compose.h).
    const double lrec = double(w[kCsLsRec]), lev = double(w[kCsLsLights]), ltr = double(w[kCsLsTraced]);
    const double locc = double(w[kCsLsOccluded]), chk = double(w[kCsLsCheck]), chs = double(w[kCsLsChkSh]);
    const double meanL = lrec > 0 ? W.sumLsL / lrec : 0.0, meanRm = lrec > 0 ? W.sumLsRm / lrec : 0.0;
    const double removed_own = own > 0 ? W.sumLsRm / own : 0.0;
    log_stats("[water] local shadows (%llu frames, %llu passes with the light list, %llu without): local records %.0f/frame "
              "= %.2f%% of owned (no local term %.2f%%, invalid %llu, list cut %llu); lights evaluated %.2f/record, with a "
              "game shadow map %.2f/record (kept), traced %.2f/record, occluded %.2f%% of traced, unknown %llu (kept); "
              "records with an occluded light %.2f%% of local records; sum check over %.0f records: within %.3f%% %.2f%%, "
              "to %.0f%% %.2f%%, beyond %.2f%% (with a shadow-mapped light %.0f, within %.2f%%); ratio of the means sum / "
              "L %.5f; local term F L %.5f per local record (projector share %.5f, kept), removed %.5f = %.2f%% of F L; "
              "mean removed over owned %.6f = %.3f%% of mean C; clamped %llu; GPU %.3f ms median, p90 %.3f (the compose "
              "dispatches with the light walk + the lighting copy); skipped: records without the local words %llu",
              (unsigned long long)W.frames, (unsigned long long)W.ls_passes, (unsigned long long)W.ls_no_lights,
              W.ran ? lrec / double(W.ran) : 0.0, pct(lrec, own), pct(double(w[kCsLsNone]), own),
              (unsigned long long)w[kCsLsInvalid], (unsigned long long)w[kCsLsCut], lrec > 0 ? lev / lrec : 0.0,
              lrec > 0 ? double(w[kCsLsShadowed]) / lrec : 0.0, lrec > 0 ? ltr / lrec : 0.0, pct(locc, ltr),
              (unsigned long long)w[kCsLsUnknown], pct(double(w[kCsLsOccRec]), lrec), chk, 100.0 * double(kLsSumTol),
              pct(double(w[kCsLsWithin]), chk), 100.0 * double(kLsSumFar), pct(double(w[kCsLsMid]), chk),
              pct(double(w[kCsLsFar]), chk), chs, pct(double(w[kCsLsChkShWithin]), chs),
              W.sumLsChkL > 0 ? W.sumLsChk / W.sumLsChkL : 0.0, meanL, lrec > 0 ? W.sumLsBox / lrec : 0.0, meanRm,
              meanL > 0 ? 100.0 * meanRm / meanL : 0.0, removed_own, meanC > 0 ? 100.0 * removed_own / meanC : 0.0,
              (unsigned long long)w[kCsLsClamped], med, p90, (unsigned long long)W.no_ls_records);
  }
  if (g_crr || W.no_rr_records || host->cfg_int("rt_water_refraction", 0)) {
    // M5.5: the traced refraction (WATER_REFR words, common/water_compose.h kCsRr*).
    const double rrec = double(w[kCsRrRec]), rtr = double(w[kCsRrTraced]), rhit = double(w[kCsRrHit]);
    const double game = rhit > 0 ? W.sumRrGame / rhit : 0.0, nw = rhit > 0 ? W.sumRrNew / rhit : 0.0;
    const double delta_own = own > 0 ? (W.sumRrNew - W.sumRrGame) / own : 0.0;
    log_stats("[water] refraction (%llu frames, %llu passes with the colour copy, %llu without): transmissive records "
              "%.0f/frame = %.2f%% of owned (opaque %.2f%%, invalid %llu, no usable copy %llu); traced %.2f%% of "
              "transmissive (not traced %llu): screen hit (composed) %.1f%%, hidden %.1f%%, off screen %.1f%%, miss "
              "%.1f%%, aborted %llu; transmission-weighted hidden share %.2f%%; screen shift %.1f px per composed record; "
              "refraction term (1 - a) Refr %.5f per transmissive record; composed: game %.5f -> traced %.5f per record "
              "(ratio of the means %.4f); mean delta over owned %+.6f = %+.3f%% of mean C; clamped %llu; GPU %.3f ms "
              "median, p90 %.3f (the compose dispatches with the refraction ray + the lighting copy); skipped: records "
              "without the refraction words %llu",
              (unsigned long long)W.frames, (unsigned long long)W.rr_passes, (unsigned long long)W.rr_no_copy,
              W.ran ? rrec / double(W.ran) : 0.0, pct(rrec, own), pct(double(w[kCsRrOpaque]), own),
              (unsigned long long)w[kCsRrInvalid], (unsigned long long)w[kCsRrNoCopy], pct(rtr, rrec),
              (unsigned long long)w[kCsRrNotTraced], pct(rhit, rtr), pct(double(w[kCsRrHidden]), rtr),
              pct(double(w[kCsRrOff]), rtr), pct(double(w[kCsRrMiss]), rtr), (unsigned long long)w[kCsRrAborted],
              W.sumRrWTraced > 0 ? 100.0 * W.sumRrWHidden / W.sumRrWTraced : 0.0, rhit > 0 ? W.sumRrShift / rhit : 0.0,
              rrec > 0 ? W.sumRrAll / rrec : 0.0, game, nw, W.sumRrGame > 0 ? W.sumRrNew / W.sumRrGame : 0.0, delta_own,
              meanC > 0 ? 100.0 * delta_own / meanC : 0.0, (unsigned long long)w[kCsRrClamped], med, p90,
              (unsigned long long)W.no_rr_records);
  }
  g_cwin = CWindow{};
}

// At the lighting (water_rf, after the reflection dispatches): the header, this frame's
// radiance / depth / camera copies, the stash for the water passes.
void compose_at_lighting(VkCommandBuffer cmd, const WaterRfIn& in, VkDescriptorSetLayout alpha_layout) {
  using namespace bg3::water;
  const VkDeviceFuncs& vk = *host->vk;
  ComposeBuffer cb{};
  if (!read_buffer(static_cast<const ComposeBuffer*>(host->persist(kComposeKey, sizeof(ComposeBuffer))), cb)) {
    ++g_cwin.no_buffer;
    g_st.frame = 0;
    return maybe_log_compose();
  }
  // M5.4: the sun words exist only if rt_water_shadows was on when the water FS were patched.
  // M5.4b: the local-light words (with the sun's) only if rt_water_ls_shadows was on then.
  // M5.5: the refraction words (with the local-light and sun words) only if rt_water_refraction was on then.
  const bool refr = record_bytes_of(cb) == kRecordBytesRefr;
  const bool ls = refr || record_bytes_of(cb) == kRecordBytesLs;
  const bool sun = ls || record_bytes_of(cb) == kRecordBytesSun;
  const bool want_rf = compose_wanted(), want_sun = shadows_wanted() && sun, want_ls = ls_wanted() && ls;
  const bool want_rr = refr_wanted() && refr;
  const bool gsh = sun && gsh_of(cb), want_gsh = gsh_wanted() && gsh;   // M5.4c: the game-shadow words
  if (shadows_wanted() && !sun) ++g_cwin.no_sun_records;
  if (gsh_wanted() && !gsh) ++g_cwin.no_gsh_records;
  if (ls_wanted() && !ls) ++g_cwin.no_ls_records;
  if (refr_wanted() && !refr) ++g_cwin.no_rr_records;
  if (!want_rf && !want_sun && !want_ls && !want_rr) {
    g_st.frame = 0;
    return maybe_log_compose();
  }
  if (!ensure_compose(alpha_layout, in.push_bytes, sun, ls, refr, gsh)) {
    ++g_cwin.layout;
    g_st.frame = 0;
    return maybe_log_compose();
  }
  if (!ensure_compose_images(in.w, in.h)) {
    ++g_cwin.size;
    g_st.frame = 0;
    return maybe_log_compose();
  }
  const uint32_t slot = g_cslot;
  if (!g_clease[slot].reusable()) {
    if (!g_cbusy_logged) host->log("[water] compose slot still owned by recording/GPU; update skipped"), g_cbusy_logged = true;
    g_st.frame = 0;
    return maybe_log_compose();
  }
  g_cslot = (g_cslot + 1) % kRing;
  collect_compose(slot);
  g_clease[slot].clear();
  g_clease[slot].record(cmd);
  const VkBuffer rb = VkBuffer(cb.buffer);
  const VkPipelineStageFlags kXfer = VK_PIPELINE_STAGE_TRANSFER_BIT;
  // The earlier frames' readers of the record buffer, the images and the slot.
  memory_barrier(cmd, kAll, kXfer | kCS);
  if (!g_cbuf_zeroed || g_cbuf_seen != cb.buffer) {   // once: uninitialised memory holds no record
    vk.CmdFillBuffer(cmd, rb, 0, VK_WHOLE_SIZE, 0);
    g_cbuf_zeroed = true;
    g_cbuf_seen = cb.buffer;
  }
  const uint32_t header[4] = {in.w, cb.capacity, 0, 0};
  vk.CmdUpdateBuffer(cmd, rb, 0, sizeof header, header);
  vk.CmdFillBuffer(cmd, g_cdev, cslot_off(slot), kCStatBytes, 0);    // the shader's counters
  vk.CmdFillBuffer(cmd, g_chost, cslot_off(slot), kCStatBytes + 16, 0);   // the readback, if no pass copies
  // Keep the scene camera with the lighting camera copied by water_copy. The
  // latter is a different 400 B layout and cannot supply the TLAS translation.
  if (host->cfg_int("rt_water_compose_shift", 0) != 0) {
    const uint32_t valid = in.cam2.buffer && in.cam2.range >= 224 ? 1u : 0u;
    if (valid) {
      const VkBufferCopy camera{in.cam2.offset + WATER_SCENE_CAMERA_ORIGIN_OFFSET,
                               kCCamOff + WATER_COMPOSE_CURRENT_CAMERA_OFFSET, 16};
      vk.CmdCopyBuffer(cmd, in.cam2.buffer, g_cdev, 1, &camera);
    } else vk.CmdFillBuffer(cmd, g_cdev, kCCamOff + WATER_COMPOSE_CURRENT_CAMERA_OFFSET, 16, 0);
    vk.CmdUpdateBuffer(cmd, g_cdev, kCCamOff + WATER_COMPOSE_CURRENT_CAMERA_VALID_OFFSET, 4, &valid);
  } else vk.CmdFillBuffer(cmd, g_cdev, kCCamOff + WATER_COMPOSE_CURRENT_CAMERA_VALID_OFFSET, 4, 0);
  ComposeParams prm{};
  prm.a[0] = in.w;
  prm.a[1] = cb.capacity;
  prm.a[2] = uint32_t((std::max)(host->cfg_int("rt_debug", 0), 0));
  // M5.3's variant: bit 0 always (the pass runs only with the compose key). The WATER_SHADOWS one:
  // each term by its key, live (common/water_compose.h kFlagRf, kFlagSun).
  // M5.4b: bit 2 by its key (each pass clears it again without its light list, compose_at_pass_end).
  // M5.5: bit 3 by its key (each pass clears it again without its colour copy, compose_at_pass_end).
  // M5.4c: bit 4 by its key (the WATER_GSH variant only).
  prm.a[3] = sun ? (want_rf ? kFlagRf : 0u) | (want_sun ? kFlagSun : 0u) | (want_ls ? kFlagLs : 0u) | (want_rr ? kFlagRefr : 0u) |
                       (want_gsh ? kFlagGsh : 0u)
                 : 1u;
  if(host->cfg_int("rt_water_surface_fresnel",0) != 0) prm.a[3]|=kFlagSurfaceDiagnostics;
  g_st.hit_identity=in.hit_identity;
  g_st.flags = prm.a[3];
  prm.b[0] = host->cfg_float("rf_mirror_rough", 0.1f);
  vk.CmdUpdateBuffer(cmd, g_cdev, cslot_off(slot) + kCStatBytes, sizeof prm, &prm);
  if (!g_cimg_init) {
    image_barrier(cmd, g_rad.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    image_barrier(cmd, g_dep.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    g_cimg_init = true;
  }
  memory_barrier(cmd, kXfer, kCS | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  g_ctimed[slot] = g_cqp && bg3::logging::stats_enabled(host->cfg_int);
  if (g_ctimed[slot]) {
    vk.CmdResetQueryPool(cmd, g_cqp, slot * kCQ, kCQ);
    vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_cqp, slot * kCQ);
  }
  const VkImageLayout G = VK_IMAGE_LAYOUT_GENERAL;
  VkDescriptorBufferInfo bufs[3] = {{in.cam.buffer, in.cam.offset, in.cam.range},
                                    {in.vp.buffer, in.vp.offset, in.vp.range},
                                    {g_cdev, kCCamOff, kCCamBytes}};
  VkDescriptorImageInfo imgs[6] = {{VK_NULL_HANDLE, in.mat.view, in.mat.layout}, {VK_NULL_HANDLE, in.dep.view, in.dep.layout},
                                   {VK_NULL_HANDLE, in.dif.view, G},              {VK_NULL_HANDLE, in.spe.view, G},
                                   {VK_NULL_HANDLE, g_rad.view, G},               {VK_NULL_HANDLE, g_dep.view, G}};
  VkWriteDescriptorSet wr[9];
  const struct { uint32_t b; VkDescriptorType t; const VkDescriptorBufferInfo* bi; const VkDescriptorImageInfo* ii; } src[9] = {
      {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufs[0], nullptr}, {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufs[1], nullptr},
      {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &imgs[0]},  {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &imgs[1]},
      {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &imgs[2]},  {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &imgs[3]},
      {62, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &imgs[4]}, {63, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &imgs[5]},
      {64, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bufs[2], nullptr}};
  for (uint32_t i = 0; i < 9; ++i) {
    wr[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr[i].dstBinding = src[i].b;
    wr[i].descriptorCount = 1;
    wr[i].descriptorType = src[i].t;
    wr[i].pBufferInfo = src[i].bi;
    wr[i].pImageInfo = src[i].ii;
  }
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_copy.p);
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_clayout, 0, 9, wr);
  vk.CmdPushConstants(cmd, g_clayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, in.push_bytes, in.push);
  vk.CmdDispatch(cmd, (in.w + 7) / 8, (in.h + 7) / 8, 1);
  if (g_ctimed[slot]) vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_cqp, slot * kCQ + 1);
  // The compose dispatches (later passes, other command buffers) and the host read.
  memory_barrier(cmd, kCS, kCS | VK_PIPELINE_STAGE_HOST_BIT);
  g_cwritten[slot] = true;
  g_cpasses[slot] = 0;
  g_st.frame = host->frame_index();
  g_st.tlas = in.scene_tlas;
  g_st.build_frame = 0;
  if (auto* ring = static_cast<bg3::water::BuildCameraRing*>(host->persist(bg3::water::kBuildCameraKey, sizeof(bg3::water::BuildCameraRing)))) {
    bg3::water::BuildCameraLock lock(*ring);
    g_st.build_frame = bg3::water::find_build_camera(*ring, uint64_t(in.scene_tlas), g_st.frame).frame;
  }
  g_st.alpha = in.alpha;
  g_st.push.assign(static_cast<const uint8_t*>(in.push), static_cast<const uint8_t*>(in.push) + in.push_bytes);
  g_st.w = in.w, g_st.h = in.h, g_st.slot = slot;
  g_st.buf = cb;
  maybe_log_compose();
}

// At the end of a forward water pass (outside it): C' = max(C + A L - B, 0) where resolved.
// The pass is most often recorded before this frame's lighting is flushed (core/gi.cpp: run_post
// at the end of the lighting's command buffer): it binds what the GPU makes current, not this
// frame's stash (common/water_compose.h lighting_use).
bool compose_at_pass_end(VkCommandBuffer cmd, const Bg3PassInfo* p, const Bg3Binding* lights, bool lights_ok,
                         const Bg3Binding* copy, bool copy_ok) {
  using namespace bg3::water;
  const VkDeviceFuncs& vk = *host->vk;
  const uint64_t now = host->frame_index();
  const LightingUse use = lighting_use(g_st.frame, now);
  if (use == LightingUse::kNone) {
    ++g_cwin.no_lighting;
    const LightingRefusal r = lighting_refusal(g_st.frame, now);
    ++(r == LightingRefusal::kCleared ? g_cwin.nl_cleared : r == LightingRefusal::kOld ? g_cwin.nl_old : g_cwin.nl_ahead);
    g_ccont.note(now, false);
    return false;
  }
  if (p->width != g_st.w || p->height != g_st.h || !g_compose.p || !g_rad.view || !g_dep.view) return false;
  // The scene pair (common/water_compose.h pick_scene). The stash's was handed out (stamped) for the
  // frame the lighting recorded in; a pass of a later frame stamps the pair it binds, or the slot
  // could be rebuilt before this command buffer runs. rt_scene never waits; rt_scene_set waits only
  // for a scene older than this frame, so it is called only when the current build is this frame's.
  VkAccelerationStructureKHR tlas = g_st.tlas;
  VkDescriptorSet alpha = g_st.alpha;
  uint64_t built = 0;
  VkAccelerationStructureKHR cur = VK_NULL_HANDLE;
  const bool cur_ok = use != LightingUse::kThisFrame && host->rt_scene && host->rt_scene(&cur, &built) && cur;
  if (use != LightingUse::kThisFrame) {
    if (!cur_ok) ++g_cwin.cur_stale;
    else if (cur != g_st.tlas && built < now) ++g_cwin.newer_old;
  }
  auto take_newer = [&] {
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!host->rt_scene_set || !host->rt_scene_set(&cur, &set, &built) || !set || !cur) {
      ++g_cwin.newer_failed;
      return false;
    }
    VkAccelerationStructureKHR ref=VK_NULL_HANDLE;
    uint64_t id=0;
    if(!reference_scene(cur,0,ref,id,[&](auto selected,auto* reference,auto* identity){
         if(host->rt_omm_scene_reference)host->rt_omm_scene_reference(cmd,selected,reference,identity);
       })) {++g_cwin.newer_failed;return false;}
    tlas = ref, alpha = set;
    return true;
  };
  // The lighting's own pair, kept alive through this frame: rt_omm_scene_reference finds the slot of
  // that TLAS, refuses a retired one and stamps it (last_use = now, its set's views with it). With
  // either OMM state it may return false with a valid baseline TLAS.
  auto keep_stash = [&] {
    if (!host->rt_omm_scene_reference || !g_st.tlas || !g_st.alpha) return false;
    VkAccelerationStructureKHR ref = VK_NULL_HANDLE;
    uint64_t id = 0;
    if(!reference_scene(g_st.tlas,g_st.hit_identity,ref,id,[&](auto selected,auto* reference,auto* identity){
         host->rt_omm_scene_reference(cmd,selected,reference,identity);
       }))return false;
    tlas = ref, alpha = g_st.alpha;
    return true;
  };
  ScenePick pick = pick_scene(use, cur_ok, cur == g_st.tlas, built, now, take_newer, keep_stash);
  if (pick == ScenePick::kNone || !tlas || !alpha) {
    ++g_cwin.no_scene;
    g_ccont.note(now, false);
    return false;
  }
  const bool ls_list = g_cls && lights_ok && lights;
  const bool shift_on=host->cfg_int("rt_water_compose_shift",0) != 0;
  BuildCameraEntry build_camera{};
  uint64_t camera_buffer = 0;
  {
    if (auto* ring = static_cast<BuildCameraRing*>(host->persist(kBuildCameraKey, sizeof(BuildCameraRing)))) {
      BuildCameraLock lock(*ring);
      const uint64_t selected_build = pick == ScenePick::kNewer ? built : g_st.build_frame;
      if (selected_build) build_camera = find_build_camera(*ring, uint64_t(tlas), now, selected_build);
      camera_buffer = ring->buffer;
    }
    if (!camera_buffer || !build_camera.frame) { if (shift_on) ++g_cwin.camera_missing; }
    else {
      const uint64_t age = now - build_camera.frame;
      ++g_cwin.tlas_age_samples; g_cwin.tlas_age_sum += age;
      g_cwin.tlas_age_max = (std::max)(g_cwin.tlas_age_max, age);
    }
  }
  const bool shifted = shift_on && camera_buffer && build_camera.frame && compose_shift_needed(use,pick);
  const bool apply_ls=ls_list;
  if (pick == ScenePick::kNewer) ++g_cwin.newer_scene;
  if (pick == ScenePick::kStashKept) ++g_cwin.stash_kept;
  if(!g_compose.p) {++g_cwin.layout;return false;}
  g_ccont.note(now, true);
  ++(use == LightingUse::kThisFrame ? g_cwin.this_frame : g_cwin.earlier);
  const uint32_t slot = g_st.slot;
  g_clease[slot].record(cmd);
  const uint32_t pass = g_cpasses[slot]++;
  const VkImageLayout from = p->color_layouts[0] == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                                              : p->color_layouts[0];
  const VkImageLayout G = VK_IMAGE_LAYOUT_GENERAL;
  // The pass's colour writes and the FS's record stores (fragment stage) before our reads.
  // M5.4b: this pass's flags word first (kFlagLs only with its water draw's light list): the earlier
  // dispatches' reads of the params, then the update, then this dispatch's read.
  // M5.5: likewise kFlagRefr only with its water draw's colour copy.
  const bool rr_copy = g_crr && copy_ok && copy && copy->view;
  if (g_cls || host->cfg_int("rt_water_compose_shift", 0) != 0) {
    memory_barrier(cmd, kAll, VK_PIPELINE_STAGE_TRANSFER_BIT | kCS);
    uint32_t flags = (apply_ls ? g_st.flags : (g_st.flags & ~kFlagLs)) & (rr_copy ? ~0u : ~kFlagRefr);
    flags |= shifted ? kFlagSceneShift : 0u;
    if (shift_on) {
      if (camera_buffer && build_camera.frame) {
        const VkBufferCopy camera{build_camera_index(build_camera.frame) * kBuildCameraBytes,
                                 kCCamOff + WATER_COMPOSE_BUILD_CAMERA_OFFSET, 16};
        vk.CmdCopyBuffer(cmd, VkBuffer(camera_buffer), g_cdev, 1, &camera);
      } else vk.CmdFillBuffer(cmd, g_cdev, kCCamOff + WATER_COMPOSE_BUILD_CAMERA_OFFSET, 16, 0);
    }
    vk.CmdUpdateBuffer(cmd, g_cdev, cslot_off(slot) + kCStatBytes + 12, 4, &flags);
    if (g_st.flags & kFlagLs) ++(apply_ls ? g_cwin.ls_passes : g_cwin.ls_no_lights);
    if (g_st.flags & kFlagRefr) ++(rr_copy ? g_cwin.rr_passes : g_cwin.rr_no_copy);
  }
  memory_barrier(cmd, kAll, kCS);
  if (from != G) image_barrier(cmd, p->color_images[0], from, G, kAll, kCS);
  if (g_ctimed[slot] && pass < kMaxComposePasses) vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_cqp, slot * kCQ + 2 + pass * 2);
  else ++g_cwin.untimed;
  VkDescriptorBufferInfo bufs[7] = {{g_cdev, kCCamOff, 400},
                                    {g_cdev, kCCamOff + kCVpOff, 16},
                                    {g_rts, 0, kRtStatsBytes},
                                    {g_cdev, cslot_off(slot) + kCStatBytes, sizeof(ComposeParams)},
                                    {g_cdev, cslot_off(slot), kCStatBytes},
                                    {VkBuffer(g_st.buf.buffer), 0, g_st.buf.bytes},
                                    {g_cdev, kCCamOff, kCCamBytes}};
  VkDescriptorImageInfo imgs[4] = {{VK_NULL_HANDLE, g_dep.view, G}, {VK_NULL_HANDLE, g_rad.view, G},
                                   {VK_NULL_HANDLE, p->color_views[0], G},
                                   {VK_NULL_HANDLE, g_rad.view, G}};
  VkWriteDescriptorSetAccelerationStructureKHR as{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
  as.accelerationStructureCount = 1;
  as.pAccelerationStructures = &tlas;
  // M5.4b: the water draw's light list at 22..28 (kWaterLsBindings order), or, without it, valid
  // stand-ins the shader never reads (the pass's flags word has kFlagLs cleared).
  VkDescriptorBufferInfo lb[6];
  VkDescriptorImageInfo li{VK_NULL_HANDLE, g_dep.view, G};
  for (uint32_t i = 0; i < 6; ++i) {
    const uint32_t k = i < 4 ? i : i + 1;   // b0 b10 b11 b12 | b26 (i = 4, the UBO below) | b27
    lb[i] = ls_list ? VkDescriptorBufferInfo{lights[k].buffer, lights[k].offset, lights[k].range}
                    : VkDescriptorBufferInfo{g_cdev, 0, kCStatBytes};
  }
  const VkDescriptorBufferInfo lubo = ls_list ? VkDescriptorBufferInfo{lights[4].buffer, lights[4].offset, 544}
                                              : VkDescriptorBufferInfo{g_cdev, 0, 544};
  if (ls_list) li = {VK_NULL_HANDLE, lights[6].view, lights[6].layout};
  // M5.5: the water draw's colour copy at 68 in the layout it was bound with (read-only during the pass),
  // or, without it, a valid stand-in the shader never reads (kFlagRefr cleared for this pass).
  const VkDescriptorImageInfo ci = rr_copy ? VkDescriptorImageInfo{VK_NULL_HANDLE, copy->view, copy->layout}
                                           : VkDescriptorImageInfo{VK_NULL_HANDLE, g_dep.view, G};
  const struct { uint32_t b; VkDescriptorType t; const VkDescriptorBufferInfo* bi; const VkDescriptorImageInfo* ii; } src[20] = {
      {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufs[0], nullptr}, {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufs[1], nullptr},
      {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &imgs[0]},  {18, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, nullptr, nullptr},
      {20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bufs[2], nullptr}, {60, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufs[3], nullptr},
      {61, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bufs[4], nullptr}, {62, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &imgs[1]},
      {66, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bufs[5], nullptr}, {67, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &imgs[2]},
      {69, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &imgs[3]},
      {64, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bufs[6], nullptr},
      {22, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lb[0], nullptr},   {23, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lb[1], nullptr},
      {24, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lb[2], nullptr},   {25, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lb[3], nullptr},
      {26, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &lubo, nullptr},    {27, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lb[5], nullptr},
      {28, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &li},
      {68, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &ci}};
  const uint32_t nw = g_crr ? 20u : g_cls ? 19u : 12u;
  VkWriteDescriptorSet wr[20];
  for (uint32_t i = 0; i < nw; ++i) {
    wr[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr[i].dstBinding = src[i].b;
    wr[i].descriptorCount = 1;
    wr[i].descriptorType = src[i].t;
    wr[i].pBufferInfo = src[i].bi;
    wr[i].pImageInfo = src[i].ii;
    if (src[i].b == 18) wr[i].pNext = &as;
  }
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_clayout, 0, nw, wr);
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_clayout, 1, 1, &alpha, 0, nullptr);
  vk.CmdPushConstants(cmd, g_clayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, uint32_t(g_st.push.size()), g_st.push.data());
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_compose.p);
  vk.CmdDispatch(cmd, (p->width + 7) / 8, (p->height + 7) / 8, 1);
  if (g_ctimed[slot] && pass < kMaxComposePasses)
    vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_cqp, slot * kCQ + 3 + pass * 2);
  // The consumed records, the statistics and the target before the later passes and the copy.
  memory_barrier(cmd, kCS, kAll);
  // The slot's cumulative statistics to the host slot (256 B, after the end timestamp: outside the
  // timed region); the last pass's copy holds every pass's counts when the host reads it kRing later.
  const VkBufferCopy sc{cslot_off(slot), cslot_off(slot), kCStatBytes};
  vk.CmdCopyBuffer(cmd, g_cdev, g_chost, 1, &sc);
  if (shift_on) {
    const VkBufferCopy camera{kCCamOff + WATER_COMPOSE_CAMERA_DELTA_OFFSET, cslot_off(slot) + kCStatBytes, 16};
    vk.CmdCopyBuffer(cmd, g_cdev, g_chost, 1, &camera);
  }
  memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kAll | VK_PIPELINE_STAGE_HOST_BIT);
  if (g_csun) ++g_cwin.sun_passes;
  if (from != G) image_barrier(cmd, p->color_images[0], G, from, kCS, kAll);
  host->restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  return true;
}

void release_compose() {
  release_compose_images();
  for (Pipe* p : {&g_copy, &g_compose}) release_pipe(*p);
  core_defer_layout_destroy(g_clayout);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_cpush);
  g_clayout = VK_NULL_HANDLE;
  g_cpush = g_calpha = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)g_chost);
  host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_chost_mem);
  host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)g_cdev);
  host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_cdev_mem);
  g_chost = g_cdev = VK_NULL_HANDLE;
  g_chost_mem = g_cdev_mem = VK_NULL_HANDLE;
  g_csun = false;
  g_cls = false;
  g_crr = false;
  g_cgsh = false;
  g_cpush_nb = 0;
  g_cmap = nullptr;
  if (g_cqp) host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_cqp);
  g_cqp = VK_NULL_HANDLE;
  for (bool& w : g_cwritten) w = false;
  for (auto& lease : g_clease) lease.clear();
  g_cbusy_logged = false;
  for (uint32_t& n : g_cpasses) n = 0;
  g_cslot = 0;
  g_cbuf_zeroed = false;
  g_cbuf_seen = 0;
  g_st = Stash{};
  g_cwin = CWindow{};
  g_ccont = bg3::water::ComposeContinuity{};
}

}  // namespace


bool water_rf_wanted() { return host->cfg_int("rt_water_reflections", 0) != 0; }

void water_rf(VkCommandBuffer cmd, const WaterRfIn& input) {
  if (!water_rf_wanted()) return;
  std::lock_guard<std::mutex> lock(g_mu);
  const uint64_t now = host->frame_index();
  if (now == g_last_frame) return;   // once per frame (the first lighting)
  g_last_frame = now;
  // A lighting that does not copy leaves the GPU copy a frame stale: no pass composes with it
  // (common/water_compose.h lighting_use).
  auto skip = [] { g_st.frame = 0; maybe_log(); };
  using namespace bg3::water;
  const Scene* pub = static_cast<const Scene*>(host->persist(kSceneKey, sizeof(Scene)));
  Scene sc{};
  const bool rd = read(pub, sc);
  const Usable u = usable(rd, sc, now);
  if (u != kUsable) {
    ++g_win.skip[u];
    return skip();
  }
  // Native tilesets use constant 1008 in the existing RT_SPEC_NATIVE water module.
  // Use the immutable same-generation reference for primary/reflection rays
  // and the lighting stash. The host AlphaDSL is a compatible superset: OMM
  // bindings 10/11 remain unused by these ordinary water shaders.
  WaterRfIn in=input;
  if(!reference_scene(input.scene_tlas,input.hit_identity,in.scene_tlas,in.hit_identity,
       [&](auto selected,auto* reference,auto* identity){
         if(host->rt_omm_scene_reference)host->rt_omm_scene_reference(cmd,selected,reference,identity);
       })) {++g_win.skip_layout;return skip();}
  VkDescriptorSetLayout alpha_layout = VK_NULL_HANDLE;
  VkDescriptorSet alpha_any = VK_NULL_HANDLE;
  g_building = false;
  bool ready = false;
  if (in.scene_tlas && in.alpha && host->rt_alpha && host->rt_alpha(&alpha_layout, &alpha_any) && alpha_layout && ensure_buffers()) {
    ready = ensure_layouts(alpha_layout, in.push_bytes, in.rf_hit != VK_NULL_HANDLE);
  }
  if (!ready) {
    ++g_win.skip_layout;
    if (!g_failed_logged && !g_building)
      g_failed_logged = true, host->log("[water] reflections unavailable (layout, buffers or pipelines)");
    return skip();
  }
  if (!in.w || !in.h || !ensure_images(in.w, in.h)) {
    ++g_win.skip_size;
    return skip();
  }
  const VkDeviceFuncs& vk = *host->vk;
  const uint32_t slot = g_slot;
  if (!g_lease[slot].reusable()) {
    if (!g_busy_logged) host->log("[water] reflection slot still owned by recording/GPU; update skipped"), g_busy_logged = true;
    return skip();
  }
  g_slot = (g_slot + 1) % kRing;
  collect(slot);
  g_lease[slot].clear();
  g_lease[slot].record(cmd);
  Params prm{};
  std::memcpy(prm.cam_build, sc.cam, sizeof prm.cam_build);
  prm.f0 = f0_of(sc.reflectance);
  const bool cam2 = in.cam2.buffer && in.cam2.range >= 224;
  prm.flags[0] = (sc.cam_ok ? 1u : 0u) | (cam2 ? 2u : 0u);
  std::memcpy(g_map + param_off(slot), &prm, sizeof prm);
  g_win.draws += sc.draws;
  g_win.tris += sc.triangles;
  g_win.age += now - sc.built_frame;
  g_win.refl = sc.reflectance, g_win.rough = sc.roughness, g_win.f0 = prm.f0, g_win.family = sc.family;

  // The previous frame's copy out of g_dev (an earlier command buffer) before this fill.
  memory_barrier(cmd, kAll, VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk.CmdFillBuffer(cmd, g_dev, 0, kStatBytes, 0);   // the device-local counters (copied to the slot below)
  if (!g_img_init) {
    image_barrier(cmd, g_out.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    image_barrier(cmd, g_t.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    g_img_init = true;
  }
  // The fill, the earlier views' reads of wOut / wT (other command buffers, earlier in the
  // queue), and the water build (the layer's own submission: AS build stage).
  memory_barrier(cmd, kAll | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, kCS);
  g_timed[slot] = g_qp && bg3::logging::stats_enabled(host->cfg_int);
  if (g_timed[slot]) {
    vk.CmdResetQueryPool(cmd, g_qp, slot * 2, 2);
    vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_qp, slot * 2);
  }
  const VkAccelerationStructureKHR wt = VkAccelerationStructureKHR(sc.tlas);
  VkWriteDescriptorSetAccelerationStructureKHR as[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR},
                                                        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR}};
  as[0].accelerationStructureCount = 1;
  as[0].pAccelerationStructures = &in.scene_tlas;
  as[1].accelerationStructureCount = 1;
  as[1].pAccelerationStructures = &wt;
  const VkImageLayout G = VK_IMAGE_LAYOUT_GENERAL;
  VkDescriptorBufferInfo bufs[7] = {
      {in.cam.buffer, in.cam.offset, in.cam.range},
      {in.vp.buffer, in.vp.offset, in.vp.range},
      {g_rts, 0, kRtStatsBytes},
      {g_host, param_off(slot), sizeof(Params)},
      {g_dev, 0, kStatBytes},
      cam2 ? VkDescriptorBufferInfo{in.cam2.buffer, in.cam2.offset, 224} : VkDescriptorBufferInfo{in.cam.buffer, in.cam.offset, in.cam.range},
      {}};
  VkDescriptorImageInfo imgs[7] = {{VK_NULL_HANDLE, in.mat.view, in.mat.layout}, {VK_NULL_HANDLE, in.dep.view, in.dep.layout},
                                   {VK_NULL_HANDLE, in.dif.view, G},              {VK_NULL_HANDLE, in.spe.view, G},
                                   {VK_NULL_HANDLE, g_out.view, G},               {VK_NULL_HANDLE, g_t.view, G},
                                   // 30: RR's hit distance with WATER_RR_HIT, else a stand-in the shader never reads
                                   {VK_NULL_HANDLE, in.rf_hit ? in.rf_hit : g_t.view, G}};
  VkWriteDescriptorSet wr[kNumBind];
  for (uint32_t i = 0; i < kNumBind; ++i) {
    wr[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr[i].dstBinding = kBind[i].binding;
    wr[i].descriptorCount = 1;
    wr[i].descriptorType = kBind[i].type;
    switch (kBind[i].binding) {
      case 0: wr[i].pBufferInfo = &bufs[0]; break;
      case 1: wr[i].pBufferInfo = &bufs[1]; break;
      case 4: wr[i].pImageInfo = &imgs[0]; break;
      case 5: wr[i].pImageInfo = &imgs[1]; break;
      case 6: wr[i].pImageInfo = &imgs[2]; break;
      case 7: wr[i].pImageInfo = &imgs[3]; break;
      case 18: wr[i].pNext = &as[0]; break;
      case 20: wr[i].pBufferInfo = &bufs[2]; break;
      case 60: wr[i].pBufferInfo = &bufs[3]; break;
      case 61: wr[i].pBufferInfo = &bufs[4]; break;
      case 62: wr[i].pImageInfo = &imgs[4]; break;
      case 63: wr[i].pImageInfo = &imgs[5]; break;
      case 64: wr[i].pNext = &as[1]; break;
      case 65: wr[i].pBufferInfo = &bufs[5]; break;
      case 30: wr[i].pImageInfo = &imgs[6]; break;
    }
  }
  const uint32_t gx = (in.w + kGroup - 1) / kGroup, gy = (in.h + kGroup - 1) / kGroup;
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_primary.p);
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_layout, 0, kNumBind, wr);
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_layout, 1, 1, &in.alpha, 0, nullptr);
  vk.CmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, in.push_bytes, in.push);
  vk.CmdDispatch(cmd, gx, gy, 1);
  memory_barrier(cmd, kCS, kCS);
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_reflect.p);
  vk.CmdDispatch(cmd, gx, gy, 1);
  if (g_timed[slot]) vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_qp, slot * 2 + 1);
  // The views (later passes, other command buffers), the copy of the counters to this slot and
  // its host read kRing frames later.
  memory_barrier(cmd, kCS, kCS | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
  const VkBufferCopy cp{0, stat_off(slot), kStatBytes};
  vk.CmdCopyBuffer(cmd, g_dev, g_host, 1, &cp);
  memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
  g_written[slot] = true;
  g_out_frame = now;
  g_out_w = in.w, g_out_h = in.h;
  if (compose_wanted() || shadows_wanted() || ls_wanted() || refr_wanted()) {
    compose_at_lighting(cmd, in, alpha_layout);
  }   // M5.3, M5.4, M5.4b, M5.5
  maybe_log();
}

void water_on_pass_end(VkCommandBuffer cmd, const Bg3PassInfo* p) {
  // A supported water draw anywhere in the pass (S8: the water is drawn between other forward
  // draws, so neither the first nor the last FS is water); always taken, so no note outlives it.
  Bg3Binding lights[7] = {};   // M5.4b: the first water draw's light list (core/em_policy.cpp)
  bool lights_ok = false;
  Bg3Binding copy = {};        // M5.5: the first water draw's colour copy (its set 2 b4)
  bool copy_ok = false;
  const bool drawn = core_water_pass_take(cmd, lights, &lights_ok, &copy, &copy_ok);
  const int view = host->cfg_int("rt_debug", 0);
  const bool views = view >= 21 && view <= 24 && water_rf_wanted();
  std::lock_guard<std::mutex> lock(g_mu);
  // M5.3, or M5.4 / M5.4b alone on a buffer with their words (else compose_at_lighting stashed nothing).
  const bool comp = compose_wanted() || (shadows_wanted() && g_csun) || (ls_wanted() && g_cls) || (refr_wanted() && g_crr);
  const bool pan_water = host->pan_capture && host->cfg_int("pan_water", 0) && host->cfg_int("pan_capture", 0) > 0;
  if (!views && !comp && !pan_water) return;
  if (p->color_count != 1 || p->color_formats[0] != VK_FORMAT_B10G11R11_UFLOAT_PACK32 ||
      p->depth_format != VK_FORMAT_D24_UNORM_S8_UINT || !p->color_views[0] || !p->color_images[0])
    return;
  using bg3::water::kAudited;
  bool ends = false;
  for (const auto& a : kAudited) ends |= a.fs == p->first_fs_hash || a.fs == p->last_fs_hash;
  if (!ends && !drawn) return;
  // Raw diagnostic capture, before post/RR. No dispatch or shader changes.
  Bg3Binding pan_target{};
  if (pan_water) {
    pan_target.image=p->color_images[0]; pan_target.view=p->color_views[0];
    pan_target.format=p->color_formats[0]; pan_target.width=p->width; pan_target.height=p->height;
    pan_target.layout=p->color_layouts[0]==VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : p->color_layouts[0];
    host->pan_capture(cmd,"water_game",&pan_target);
    bg3::water::ComposeBuffer records{};
    if (bg3::water::read_buffer(static_cast<const bg3::water::ComposeBuffer*>(host->persist(bg3::water::kComposeKey,sizeof records)),records) &&
        uint64_t(p->width)*p->height<=records.capacity &&
        bg3::water::buffer_bytes(records.capacity,bg3::water::stride_of(records))<=records.bytes) {
      Bg3Binding coverage{};
      coverage.buffer=VkBuffer(records.buffer); coverage.offset=bg3::water::kHeaderBytes;
      coverage.range=uint64_t(p->width)*p->height*bg3::water::stride_of(records);
      coverage.width=p->width; coverage.height=p->height; coverage.format=VK_FORMAT_R8_UINT;
      host->pan_capture(cmd,"water_coverage",&coverage);
      if (!comp) {
        // Consume after copying even with composition disabled. Preserve the header/pitch.
        memory_barrier(cmd,kAll,VK_PIPELINE_STAGE_TRANSFER_BIT);
        host->vk->CmdFillBuffer(cmd,coverage.buffer,bg3::water::kHeaderBytes,records.bytes-bg3::water::kHeaderBytes,0);
        memory_barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,kAll);
      }
    } else host->log("[pan] water capture REFUSED: coverage requires complete records covering the render target");
  }
  const bool composed = comp && compose_at_pass_end(cmd, p, lights, lights_ok, &copy, copy_ok);
  if (pan_water && composed && view==25) host->pan_capture(cmd,"water_compose_debug",&pan_target);
  if (!views) return;
  ++g_win.view_passes;
  if (!ends) ++g_win.view_passes_inner;
  // This frame's or the previous one's (the lighting may be recorded after this pass on another thread).
  if (g_out_frame + 2 < host->frame_index() || !g_out_frame || p->width != g_out_w || p->height != g_out_h || !g_out.view || !ensure_view())
    return;
  const VkDeviceFuncs& vk = *host->vk;
  const VkImageLayout from = p->color_layouts[0] == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                                              : p->color_layouts[0];
  const VkImageLayout G = VK_IMAGE_LAYOUT_GENERAL;
  if (from != G) image_barrier(cmd, p->color_images[0], from, G, kAll, kCS);
  else memory_barrier(cmd, kAll, kCS);
  VkDescriptorImageInfo im[3] = {{VK_NULL_HANDLE, g_out.view, G}, {VK_NULL_HANDLE, g_t.view, G},
                                 {VK_NULL_HANDLE, p->color_views[0], G}};
  VkWriteDescriptorSet wr[3];
  for (uint32_t i = 0; i < 3; ++i) {
    wr[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr[i].dstBinding = i;
    wr[i].descriptorCount = 1;
    wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    wr[i].pImageInfo = &im[i];
  }
  const uint32_t pc[4] = {p->width, p->height, uint32_t(view), 0};
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_view.p);
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_view_layout, 0, 3, wr);
  vk.CmdPushConstants(cmd, g_view_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
  vk.CmdDispatch(cmd, (p->width + 7) / 8, (p->height + 7) / 8, 1);
  if (from != G) image_barrier(cmd, p->color_images[0], G, from, kCS, kAll);
  else memory_barrier(cmd, kCS, kAll);
  host->restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  ++g_win.views;
}

void water_release() {
  std::lock_guard<std::mutex> lock(g_mu);
  release_compose();
  release_images();
  for (Pipe* p : {&g_primary, &g_reflect, &g_view}) release_pipe(*p);
  core_defer_layout_destroy(g_layout);
  core_defer_layout_destroy(g_view_layout);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_push);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_view_dsl);
  g_layout = g_view_layout = VK_NULL_HANDLE;
  g_push = g_view_dsl = g_alpha_layout = VK_NULL_HANDLE;
  for (VkBuffer* b : {&g_host, &g_rts, &g_dev}) host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)*b), *b = VK_NULL_HANDLE;
  for (VkDeviceMemory* m : {&g_host_mem, &g_rts_mem, &g_dev_mem})
    host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)*m), *m = VK_NULL_HANDLE;
  g_map = nullptr;
  if (g_qp) host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_qp);
  g_qp = VK_NULL_HANDLE;
  for (bool& w : g_written) w = false;
  for (auto& lease : g_lease) lease.clear();
  g_busy_logged = false;
  g_slot = 0;
  g_last_frame = UINT64_MAX;
  g_out_frame = 0;
  g_failed_logged = false;
  g_win = Window{};
}

void water_stats_reset() {
  std::lock_guard<std::mutex> lock(g_mu);
  g_win = Window{}; g_cwin = CWindow{};
  std::fill_n(g_written, kRing, false); std::fill_n(g_cwritten, kRing, false);
  std::fill_n(g_cpasses, kRing, uint32_t(0));
}
