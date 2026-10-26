// Track C: screen-space GI for BG3 (docs/TRACK_C_GI.md).
//
// BG3's direct lighting is a tiled compute pass (about 15 dispatch_indirect
// permutations). The core recognises it from what is bound (set 1: RG16F
// normals b1, albedo b2, material b3, R32F linear depth b5, RGB10A2 AO b6,
// R11G11B10F diffuse/specular storage outputs b10/b11; set 0: camera UBO b1,
// viewport UBO b4) and re-uses those exact descriptors.
//
//  pre  (before the first lighting dispatch): trace + temporal at half res,
//       then fold visibility into the game's AO so its own ambient (diffuse
//       and specular) is occluded by it.
//  post (end of the lighting's command buffer, or the next render pass):
//       add one-bounce radiance into the diffuse buffer before SSS and fog,
//       and keep the lit radiance for next frame's ray hits.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core.h"

namespace {

struct Snapshot {
  Bg3Binding cam, vp, nrm, alb, mat, dep, ao, shadow, dif, spe;
  VkAccelerationStructureKHR tlas;   // the TLAS the ray-traced passes used (Track D)
  VkDescriptorSet alpha;             // the host's set 1 for them (alpha-tested geometry, phase 3)
  bool gi;                           // the GI passes ran (gi=1); otherwise only the sun shadow work
};

struct Pipe {
  VkPipeline pipeline = VK_NULL_HANDLE;
  uint64_t generation = 0;
};

struct Push {
  uint32_t full[2], half[2];
  uint32_t frame;
  float strength, radius, thickness;
  uint32_t rays, steps, debug;
  float history, ao;
  float cs_length, cs_strength;
  uint32_t cs_steps;
  float mv_scale[2];
  uint32_t has_mv, pad;
  float rt_bias, rt_tmax;
  uint32_t rt_debug;
  float rt_strength;
  float rt_ao_radius;
  uint32_t rt_ao_rays;
  uint32_t pad2[2];
  float gi_rt_radius, gi_rt_tol;
  uint32_t gi_rt_fallback, rt_stats;
};
static_assert(sizeof(Push) == 128, "Push must match gi_common.hlsli");

constexpr uint32_t kBindings = 18;
// The ray-traced passes add the TLAS (18), their visibility image (19) and a
// statistics buffer (20, rt_stats=1).
constexpr uint32_t kRtBindings = kBindings + 3;

std::mutex g_mu;
std::unordered_map<VkCommandBuffer, Snapshot> g_pending;   // pre done, post owed
VkDescriptorSetLayout g_set_layout = VK_NULL_HANDLE, g_rt_set_layout = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE, g_rt_layout = VK_NULL_HANDLE;
Pipe g_trace, g_temporal, g_ao_apply, g_composite, g_contact, g_rt_shadow, g_rt_debug, g_rt_ao, g_trace_rt;
Bg3Image g_raw{}, g_hist[2]{}, g_raw_depth{}, g_hist_depth[2]{}, g_prev_radiance{}, g_rt_vis{};
uint32_t g_w = 0, g_h = 0, g_hw = 0, g_hh = 0, g_parity = 0;
bool g_images_ready = false, g_logged = false;
uint64_t g_last_pre_frame = 0;

// Motion vectors (from the NGX hook) and the layout their render pass leaves them in.
VkImage g_mv_image = VK_NULL_HANDLE;
VkImageView g_mv_view = VK_NULL_HANDLE;
VkImageLayout g_mv_layout = VK_IMAGE_LAYOUT_UNDEFINED;
float g_mv_scale[2] = {0, 0};
uint64_t g_mv_frame = 0;   // frame in which the velocity pass was last seen writing the MVs

// GPU timing: 4 timestamps per frame (pre start/end, post start/end) in a ring
// of 8 frames, read back a few frames later without waiting.
constexpr uint32_t kRing = 8;
VkQueryPool g_queries = VK_NULL_HANDLE;
uint32_t g_slot = 0;
bool g_slot_written[kRing] = {};
double g_ns_per_tick = 1.0, g_ms_sum = 0;
uint32_t g_ms_count = 0;

void timing_init() {
  if (g_queries) return;
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qi.queryCount = kRing * 4;
  host->vk->CreateQueryPool(host->device, &qi, nullptr, &g_queries);
  g_ns_per_tick = host->cfg_float("gpu_timestamp_period_ns", 1.0f);
}

void timestamp(VkCommandBuffer cmd, uint32_t index) {
  if (!g_queries) return;
  uint32_t q = g_slot * 4 + index;
  host->vk->CmdResetQueryPool(cmd, g_queries, q, 1);
  host->vk->CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_queries, q);
}

// Reads the oldest ring slot if the GPU is done with it.
void timing_collect() {
  if (!g_queries) return;
  uint32_t old = (g_slot + 1) % kRing;
  if (!g_slot_written[old]) return;
  uint64_t t[4];
  if (host->vk->GetQueryPoolResults(host->device, g_queries, old * 4, 4, sizeof t, t, sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    return;
  g_slot_written[old] = false;
  double ms = (double(t[1] - t[0]) + double(t[3] - t[2])) * g_ns_per_tick * 1e-6;
  if (ms >= 0 && ms < 1000) {
    g_ms_sum += ms;
    ++g_ms_count;
  }
  if (g_ms_count >= 300) {
    host->log("gi: GPU cost %.3f ms/frame (average of %u frames)", g_ms_sum / g_ms_count, g_ms_count);
    g_ms_sum = 0;
    g_ms_count = 0;
  }
}

Bg3Image* const all_images[] = {&g_raw, &g_hist[0], &g_hist[1], &g_raw_depth, &g_hist_depth[0], &g_hist_depth[1],
                                &g_prev_radiance, &g_rt_vis};

// Track D: GPU time of the ray-traced shadow pass, 2 timestamps per frame.
VkQueryPool g_rt_queries = VK_NULL_HANDLE;
uint32_t g_rt_slot = 0;
bool g_rt_written[kRing] = {};
double g_rt_ms_sum = 0;
uint32_t g_rt_ms_count = 0;
uint64_t g_rt_frames_traced = 0, g_rt_frames_stale = 0, g_rt_frames_missing = 0;
bool g_rt_logged = false;

void rt_timing_collect() {
  if (!g_rt_queries) return;
  uint32_t old = (g_rt_slot + 1) % kRing;
  if (!g_rt_written[old]) return;
  uint64_t t[2];
  if (host->vk->GetQueryPoolResults(host->device, g_rt_queries, old * 2, 2, sizeof t, t, sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    return;
  g_rt_written[old] = false;
  double ms = double(t[1] - t[0]) * g_ns_per_tick * 1e-6;
  if (ms >= 0 && ms < 1000) {
    g_rt_ms_sum += ms;
    ++g_rt_ms_count;
  }
  if (g_rt_ms_count >= 300) {
    host->log("rt passes: GPU cost %.3f ms/frame (average of %u frames); TLAS this frame %llu, previous frame %llu, "
              "none %llu", g_rt_ms_sum / g_rt_ms_count, g_rt_ms_count, (unsigned long long)g_rt_frames_traced,
              (unsigned long long)g_rt_frames_stale, (unsigned long long)g_rt_frames_missing);
    g_rt_ms_sum = 0;
    g_rt_ms_count = 0;
    g_rt_frames_traced = g_rt_frames_stale = g_rt_frames_missing = 0;
  }
}

void rt_timestamp(VkCommandBuffer cmd, uint32_t index) {
  if (!g_rt_queries) return;
  uint32_t q = g_rt_slot * 2 + index;
  host->vk->CmdResetQueryPool(cmd, g_rt_queries, q, 1);
  host->vk->CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_rt_queries, q);
}

// GI trace pass alone (screen-space march or ray queries), 2 timestamps per frame.
VkQueryPool g_tr_queries = VK_NULL_HANDLE;
uint32_t g_tr_slot = 0;
bool g_tr_written[kRing] = {}, g_tr_rt[kRing] = {};
std::vector<float> g_tr_ms[2];   // [0] screen-space, [1] ray-traced

void trace_timestamp(VkCommandBuffer cmd, uint32_t index, bool rt = false) {
  if (!g_tr_queries) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kRing * 2;
    if (host->vk->CreateQueryPool(host->device, &qi, nullptr, &g_tr_queries) != VK_SUCCESS) return;
  }
  uint32_t q = g_tr_slot * 2 + index;
  host->vk->CmdResetQueryPool(cmd, g_tr_queries, q, 1);
  host->vk->CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_tr_queries, q);
  if (index == 1) {
    g_tr_written[g_tr_slot] = true;
    g_tr_rt[g_tr_slot] = rt;
    g_tr_slot = (g_tr_slot + 1) % kRing;
  }
}

void trace_timing_collect() {
  if (!g_tr_queries) return;
  uint32_t old = (g_tr_slot + 1) % kRing;
  if (!g_tr_written[old]) return;
  uint64_t t[2];
  if (host->vk->GetQueryPoolResults(host->device, g_tr_queries, old * 2, 2, sizeof t, t, sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    return;
  g_tr_written[old] = false;
  double ms = double(t[1] - t[0]) * g_ns_per_tick * 1e-6;
  if (ms >= 0 && ms < 1000) g_tr_ms[g_tr_rt[old] ? 1 : 0].push_back(float(ms));
  for (int k = 0; k < 2; ++k) {
    auto& v = g_tr_ms[k];
    if (v.size() < 300) continue;
    std::sort(v.begin(), v.end());
    host->log("gi: trace pass (%s, %u rays/sample, half res) GPU median %.3f ms, p10 %.3f, p90 %.3f (%zu frames)",
              k ? "ray-traced, gi_rt=1" : "screen-space", uint32_t(host->cfg_int("gi_rays", 2)), v[v.size() / 2],
              v[v.size() / 10], v[v.size() * 9 / 10], v.size());
    v.clear();
  }
}

bool snapshot(VkCommandBuffer cmd, Snapshot& s) {
  auto q = [&](uint32_t set, uint32_t b, Bg3Binding& o) {
    return host->query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, set, b, 0, &o);
  };
  if (!q(1, 10, s.dif) || s.dif.format != VK_FORMAT_B10G11R11_UFLOAT_PACK32 || !s.dif.view) return false;
  if (!q(1, 11, s.spe) || s.spe.format != VK_FORMAT_B10G11R11_UFLOAT_PACK32 || !s.spe.view) return false;
  if (!q(1, 1, s.nrm) || s.nrm.format != VK_FORMAT_R16G16_SFLOAT) return false;
  if (!q(1, 2, s.alb) || !q(1, 3, s.mat) || !q(1, 5, s.dep) || s.dep.format != VK_FORMAT_R32_SFLOAT) return false;
  if (!q(1, 6, s.ao) || s.ao.format != VK_FORMAT_A2B10G10R10_UNORM_PACK32) return false;
  if (!q(1, 7, s.shadow) || s.shadow.format != VK_FORMAT_R8_UNORM) s.shadow = Bg3Binding{};
  if (!q(0, 1, s.cam) || !s.cam.buffer || !q(0, 4, s.vp) || !s.vp.buffer) return false;
  return s.nrm.width == s.dif.width && s.dep.width == s.dif.width && s.ao.width == s.dif.width && s.dif.width > 0;
}

void release_images() {
  for (Bg3Image* im : all_images) {
    host->defer_destroy(BG3_DEFER_IMAGE_VIEW, (uint64_t)im->view);
    host->defer_destroy(BG3_DEFER_IMAGE, (uint64_t)im->image);
    host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)im->memory);
    *im = Bg3Image{};
  }
  g_images_ready = false;
  g_w = g_h = 0;
}

bool ensure_images(uint32_t w, uint32_t h) {
  if (w == g_w && h == g_h && g_raw.image) return true;
  release_images();
  uint32_t hw = (w + 1) / 2, hh = (h + 1) / 2;
  VkImageUsageFlags use = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  bool ok = host->alloc_image(hw, hh, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_raw) &&
            host->alloc_image(hw, hh, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_hist[0]) &&
            host->alloc_image(hw, hh, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_hist[1]) &&
            host->alloc_image(hw, hh, VK_FORMAT_R32_SFLOAT, use, &g_raw_depth) &&
            host->alloc_image(hw, hh, VK_FORMAT_R32_SFLOAT, use, &g_hist_depth[0]) &&
            host->alloc_image(hw, hh, VK_FORMAT_R32_SFLOAT, use, &g_hist_depth[1]) &&
            host->alloc_image(w, h, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_prev_radiance) &&
            host->alloc_image(w, h, VK_FORMAT_R8G8B8A8_UNORM, use, &g_rt_vis);
  if (!ok) {
    host->log("gi: image allocation failed");
    release_images();
    return false;
  }
  g_w = w, g_h = h, g_hw = hw, g_hh = hh;
  host->log("gi: resources for %ux%u (GI at %ux%u)", w, h, hw, hh);
  return true;
}

VkDescriptorType binding_type(uint32_t i) {
  return i <= 1                ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
         : (i <= 5 || i == 17) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
         : i == 18             ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
         : i == 20             ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                               : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
}

// `set1`: the host's set layout for the ray-traced passes (alpha-tested geometry).
bool make_layout(uint32_t count, VkDescriptorSetLayout* set_layout, VkPipelineLayout* layout,
                 VkDescriptorSetLayout set1 = VK_NULL_HANDLE) {
  VkDescriptorSetLayoutBinding b[kRtBindings];
  for (uint32_t i = 0; i < count; ++i) b[i] = {i, binding_type(i), 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  sci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
  sci.bindingCount = count;
  sci.pBindings = b;
  if (host->vk->CreateDescriptorSetLayout(host->device, &sci, nullptr, set_layout) != VK_SUCCESS) return false;
  VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
  VkDescriptorSetLayout sets[2] = {*set_layout, set1};
  VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lci.setLayoutCount = set1 ? 2 : 1;
  lci.pSetLayouts = sets;
  lci.pushConstantRangeCount = 1;
  lci.pPushConstantRanges = &pc;
  return host->vk->CreatePipelineLayout(host->device, &lci, nullptr, layout) == VK_SUCCESS;
}

bool ensure_layout() {
  if (g_layout) return true;
  return make_layout(kBindings, &g_set_layout, &g_layout);
}

// Only on a device with ray tracing (rt=1): the layout has an AS binding, and
// set 1 is the host's (alpha-tested geometry, Track D phase 3).
bool ensure_rt_layout() {
  if (g_rt_layout) return true;
  VkDescriptorSetLayout host_layout = VK_NULL_HANDLE;
  VkDescriptorSet unused;
  if (!host->rt_alpha || !host->rt_alpha(&host_layout, &unused) || !host_layout) return false;
  return make_layout(kRtBindings, &g_rt_set_layout, &g_rt_layout, host_layout);
}

bool refresh(Pipe& p, const char* file, VkPipelineLayout layout = VK_NULL_HANDLE) {
  Bg3ShaderBlob blob{};
  if (!host->get_shader(file, "main", "cs_6_6", &blob)) return p.pipeline != VK_NULL_HANDLE;
  if (p.pipeline && blob.generation == p.generation) return true;
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  // A real module: chaining VkShaderModuleCreateInfo with a null module needs
  // maintenance5, which the game's device does not enable (VUID-...-stage-08771).
  VkShaderModule mod = VK_NULL_HANDLE;
  if (host->vk->CreateShaderModule(host->device, &mci, nullptr, &mod) != VK_SUCCESS) {
    host->log("gi: %s shader module creation failed; keeping the previous pipeline", file);
    return p.pipeline != VK_NULL_HANDLE;
  }
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main",
               nullptr};
  pci.layout = layout ? layout : g_layout;
  VkPipeline np = VK_NULL_HANDLE;
  VkResult pr = host->vk->CreateComputePipelines(host->device, VK_NULL_HANDLE, 1, &pci, nullptr, &np);
  host->vk->DestroyShaderModule(host->device, mod, nullptr);
  if (pr != VK_SUCCESS) {
    host->log("gi: %s pipeline creation failed; keeping the previous one", file);
    return p.pipeline != VK_NULL_HANDLE;
  }
  host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)p.pipeline);
  p.pipeline = np;
  p.generation = blob.generation;
  host->log("gi: %s generation %llu", file, (unsigned long long)blob.generation);
  return true;
}

void memory_barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst) {
  // MEMORY_* access is valid with every stage mask.
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  host->vk->CmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void image_layout(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags src,
                  VkPipelineStageFlags dst) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcAccessMask = (src & VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT) ? 0 : VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  host->vk->CmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// Everything the passes need, bound through one push-descriptor layout.
struct Bound {
  VkDescriptorBufferInfo cam, vp, stats;
  VkDescriptorImageInfo img[kRtBindings - 2];
  VkWriteDescriptorSet w[kRtBindings];
  VkWriteDescriptorSetAccelerationStructureKHR as;
  VkAccelerationStructureKHR tlas;
  VkDescriptorSet alpha;
};

// Statistics of the ray-traced passes (rt_stats=1), counted on the GPU with
// wave-reduced atomics into a host-visible ring, read kStatsRing frames later:
//   [0] pixels with depth, [1..4] primary-ray check (rt_debug=3/5): match, hit
//   in front, hit behind, miss; [5..7] in front on opaque geometry / a vt
//   cutout / a tex cutout; [13, 14] behind through a vt / tex cutout;
//   [8..12] hybrid GI rays: traced, hits, hits shaded from the screen, on
//   screen but hidden, off screen.
// Each counter has 64 bins (spread atomics); the slot holds 16 x 64 uints.
constexpr uint32_t kStatsRing = 8;
constexpr VkDeviceSize kStatsSlot = 16 * 64 * 4;
VkBuffer g_stats_buf = VK_NULL_HANDLE;
VkDeviceMemory g_stats_mem = VK_NULL_HANDLE;
uint32_t* g_stats_map = nullptr;
uint32_t g_stats_slot = 0;
uint64_t g_stats_frame[kStatsRing] = {};
uint64_t g_stats_sum[16] = {};
uint32_t g_stats_n = 0;

bool stats_init() {
  if (g_stats_buf) return true;
  if (!host->alloc_host_buffer) return false;
  void* m = nullptr;
  if (!host->alloc_host_buffer(kStatsSlot * kStatsRing, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               &g_stats_buf, &g_stats_mem, &m))
    return false;
  g_stats_map = static_cast<uint32_t*>(m);
  return true;
}

// Picks this frame's slot (cleared on the GPU) after folding in the one written
// kStatsRing - 1 frames ago.
void stats_begin(VkCommandBuffer cmd, bool alpha_test) {
  uint64_t now = host->frame_index();
  uint32_t slot = uint32_t(now % kStatsRing);
  if (g_stats_frame[slot] && g_stats_frame[slot] + kStatsRing - 1 <= now) {
    const uint32_t* c = g_stats_map + slot * (kStatsSlot / 4);
    for (int i = 0; i < 16; ++i)
      for (int b = 0; b < 64; ++b) g_stats_sum[i] += c[i * 64 + b];
    ++g_stats_n;
  }
  g_stats_slot = slot;
  g_stats_frame[slot] = now;
  host->vk->CmdFillBuffer(cmd, g_stats_buf, VkDeviceSize(slot) * kStatsSlot, kStatsSlot, 0);
  memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  if (g_stats_n < 120) return;
  const uint64_t* t = g_stats_sum;
  if (t[0]) {
    double px = double(t[0]);
    host->log("rt stats (%u frames, alpha test %s): primary-ray check match %.3f%%, in front %.3f%% (opaque geometry "
              "%.3f, vt cutouts %.3f, tex cutouts %.3f), behind %.3f%% (through vt %.3f, tex %.3f), miss %.3f%%",
              g_stats_n, alpha_test ? "on" : "off", 100.0 * t[1] / px, 100.0 * t[2] / px, 100.0 * t[5] / px,
              100.0 * t[6] / px, 100.0 * t[7] / px, 100.0 * t[3] / px, 100.0 * t[13] / px, 100.0 * t[14] / px,
              100.0 * t[4] / px);
  }
  if (t[8]) {
    double r = double(t[8]), h = double(t[9] ? t[9] : 1);
    host->log("rt stats (%u frames): hybrid GI %.2fM rays/frame, %.1f%% hit; of the hits %.1f%% shaded from the screen, "
              "%.1f%% on screen but hidden, %.1f%% off screen",
              g_stats_n, r / g_stats_n * 1e-6, 100.0 * t[9] / r, 100.0 * t[10] / h, 100.0 * t[11] / h, 100.0 * t[12] / h);
  }
  std::memset(g_stats_sum, 0, sizeof g_stats_sum);
  g_stats_n = 0;
}

// Adds the TLAS and the visibility image (bindings 18, 19) for the RT passes.
void bind_rt(Bound& b, VkAccelerationStructureKHR tlas, VkDescriptorSet alpha) {
  b.tlas = tlas;
  b.alpha = alpha;
  b.as = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
  b.as.accelerationStructureCount = 1;
  b.as.pAccelerationStructures = &b.tlas;
  b.w[18] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  b.w[18].pNext = &b.as;
  b.w[18].dstBinding = 18;
  b.w[18].descriptorCount = 1;
  b.w[18].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  b.img[17] = {VK_NULL_HANDLE, g_rt_vis.view, VK_IMAGE_LAYOUT_GENERAL};
  b.w[19] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  b.w[19].dstBinding = 19;
  b.w[19].descriptorCount = 1;
  b.w[19].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  b.w[19].pImageInfo = &b.img[17];
  b.stats = {g_stats_buf, VkDeviceSize(g_stats_slot) * kStatsSlot, kStatsSlot};
  b.w[20] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  b.w[20].dstBinding = 20;
  b.w[20].descriptorCount = 1;
  b.w[20].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  b.w[20].pBufferInfo = &b.stats;
}

void bind_all(Bound& b, const Snapshot& s) {
  Bg3Image& hin = g_hist[g_parity];
  Bg3Image& hout = g_hist[g_parity ^ 1];
  Bg3Image& din = g_hist_depth[g_parity];
  Bg3Image& dout = g_hist_depth[g_parity ^ 1];
  b.cam = {s.cam.buffer, s.cam.offset, s.cam.range};
  b.vp = {s.vp.buffer, s.vp.offset, s.vp.range};
  const VkImageLayout G = VK_IMAGE_LAYOUT_GENERAL;
  const VkDescriptorImageInfo img[kBindings - 2] = {
      {VK_NULL_HANDLE, s.nrm.view, s.nrm.layout}, {VK_NULL_HANDLE, s.alb.view, s.alb.layout},
      {VK_NULL_HANDLE, s.mat.view, s.mat.layout}, {VK_NULL_HANDLE, s.dep.view, s.dep.layout},
      {VK_NULL_HANDLE, s.dif.view, G},           {VK_NULL_HANDLE, s.spe.view, G},
      {VK_NULL_HANDLE, g_raw.view, G},           {VK_NULL_HANDLE, hin.view, G},
      {VK_NULL_HANDLE, hout.view, G},            {VK_NULL_HANDLE, g_prev_radiance.view, G},
      {VK_NULL_HANDLE, s.ao.view, G},            {VK_NULL_HANDLE, g_raw_depth.view, G},
      {VK_NULL_HANDLE, din.view, G},             {VK_NULL_HANDLE, dout.view, G},
      {VK_NULL_HANDLE, s.shadow.view ? s.shadow.view : s.ao.view, G},
      {VK_NULL_HANDLE, g_mv_view ? g_mv_view : s.nrm.view,
       g_mv_view ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : s.nrm.layout}};
  memcpy(b.img, img, sizeof img);
  for (uint32_t i = 0; i < kBindings; ++i) {
    b.w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    b.w[i].dstBinding = i;
    b.w[i].descriptorCount = 1;
    if (i <= 1) {
      b.w[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      b.w[i].pBufferInfo = i == 0 ? &b.cam : &b.vp;
    } else {
      b.w[i].descriptorType = (i <= 5 || i == 17) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      b.w[i].pImageInfo = &b.img[i - 2];
    }
  }
}

Push make_push() {
  Push pc{};
  pc.full[0] = g_w, pc.full[1] = g_h, pc.half[0] = g_hw, pc.half[1] = g_hh;
  pc.frame = uint32_t(host->frame_index());
  pc.strength = host->cfg_float("gi_strength", 1.0f);
  pc.radius = host->cfg_float("gi_radius", 0.15f);
  pc.thickness = host->cfg_float("gi_thickness", 0.03f);
  pc.rays = uint32_t(host->cfg_int("gi_rays", 2));
  pc.steps = uint32_t(host->cfg_int("gi_steps", 12));
  pc.debug = uint32_t(host->cfg_int("gi_debug", 0));
  pc.history = host->cfg_float("gi_history", 0.1f);
  pc.ao = host->cfg_float("gi_ao", 0.7f);
  pc.cs_length = host->cfg_float("cs_length", 0.02f);
  pc.cs_strength = host->cfg_float("cs_strength", 1.0f);
  pc.cs_steps = uint32_t(host->cfg_int("cs_steps", 12));
  pc.mv_scale[0] = g_mv_scale[0];
  pc.mv_scale[1] = g_mv_scale[1];
  pc.has_mv = g_mv_view && host->cfg_int("gi_reproject", 1) ? 1u : 0u;
  pc.rt_bias = host->cfg_float("rt_bias", 0.002f);
  pc.rt_tmax = host->cfg_float("rt_tmax", 300.0f);
  pc.rt_debug = uint32_t(host->cfg_int("rt_debug", 0));
  pc.rt_strength = host->cfg_float("rt_strength", 1.0f);
  pc.rt_ao_radius = host->cfg_float("rt_ao_radius", 1.0f);
  pc.rt_ao_rays = uint32_t((std::max)(host->cfg_int("rt_ao_rays", 2), 1));
  pc.gi_rt_radius = (std::max)(host->cfg_float("gi_rt_radius", 1.0f), 0.01f);
  pc.gi_rt_tol = host->cfg_float("gi_rt_depth_tol", 0.03f);
  pc.gi_rt_fallback = uint32_t(host->cfg_int("gi_rt_fallback", 0));
  return pc;
}

void dispatch(VkCommandBuffer cmd, Pipe& p, Bound& b, const Push& pc, uint32_t w, uint32_t h, bool rt = false) {
  const VkDeviceFuncs& vk = *host->vk;
  VkPipelineLayout layout = rt ? g_rt_layout : g_layout;
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, rt ? kRtBindings : kBindings, b.w);
  if (rt) vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &b.alpha, 0, nullptr);
  vk.CmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  vk.CmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
}

constexpr VkPipelineStageFlags kCS = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags kAll = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

// The frame's TLAS if ray-traced shadows or AO are on and the host built one
// this frame or the previous one: the game records the G-buffer of a frame
// before presenting the previous one and the lighting after, so the build is
// usually stamped one frame earlier. The GPU still runs it first (submission
// order), and the handle is the same object every frame.
VkAccelerationStructureKHR rt_tlas(VkDescriptorSet* alpha) {
  if (!host->rt_scene || (!host->cfg_int("rt_shadows", 0) && !host->cfg_int("rt_ao", 0) && !host->cfg_int("gi_rt", 0)))
    return VK_NULL_HANDLE;
  VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
  VkDescriptorSetLayout layout;
  uint64_t built = 0, now = host->frame_index();
  if (!host->rt_scene(&tlas, &built) || !tlas || built + 1 < now || !host->rt_alpha || !host->rt_alpha(&layout, alpha) ||
      !*alpha) {
    ++g_rt_frames_missing;
    return VK_NULL_HANDLE;
  }
  ++(built == now ? g_rt_frames_traced : g_rt_frames_stale);
  return tlas;
}

bool run_pre(VkCommandBuffer cmd, Snapshot& s) {
  if (!ensure_layout() || !ensure_images(s.dif.width, s.dif.height)) return false;
  bool ok = refresh(g_trace, "gi/gi_trace.hlsl") & refresh(g_temporal, "gi/gi_temporal.hlsl") &
            refresh(g_ao_apply, "gi/gi_ao_apply.hlsl") & refresh(g_composite, "gi/gi_composite.hlsl") &
            refresh(g_contact, "gi/gi_contact.hlsl");
  if (!ok) return false;
  if (!g_images_ready) {
    for (Bg3Image* im : all_images)
      image_layout(cmd, im->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkClearColorValue zero{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (Bg3Image* im : all_images) host->vk->CmdClearColorImage(cmd, im->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
    memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kCS);
    g_images_ready = true;
  }
  timing_init();
  timing_collect();
  timestamp(cmd, 0);
  {
    VkImage im;
    VkImageView v;
    VkFormat f;
    float sc[2];
    // Only an image seen being written this frame is known to be alive.
    if (host->dlss_motion_vectors && host->dlss_motion_vectors(&im, &v, &f, sc) && f == VK_FORMAT_R16G16_SFLOAT &&
        g_mv_layout != VK_IMAGE_LAYOUT_UNDEFINED && g_mv_frame == host->frame_index()) {
      g_mv_image = im, g_mv_view = v, g_mv_scale[0] = sc[0], g_mv_scale[1] = sc[1];
    } else {
      g_mv_image = VK_NULL_HANDLE, g_mv_view = VK_NULL_HANDLE;
    }
  }
  Bound b;
  bind_all(b, s);
  Push pc = make_push();
  // Track D: the frame's TLAS for the ray-traced passes.
  VkDescriptorSet alpha = VK_NULL_HANDLE;
  VkAccelerationStructureKHR tlas = rt_tlas(&alpha);
  if (tlas && (!ensure_rt_layout() || !refresh(g_rt_shadow, "gi/rt_shadow.hlsl", g_rt_layout) ||
               !refresh(g_rt_debug, "gi/rt_debug.hlsl", g_rt_layout) || !refresh(g_rt_ao, "gi/rt_ao.hlsl", g_rt_layout) ||
               !refresh(g_trace_rt, "gi/gi_trace_rt.hlsl", g_rt_layout) || !stats_init()))
    tlas = VK_NULL_HANDLE;   // (the statistics buffer is binding 20 and must exist)
  s.gi = host->cfg_int("gi", 0) != 0;
  // Hybrid GI: ray queries replace the screen-space march (and do the AO rays too).
  const bool gi_rt = tlas && s.gi && host->cfg_int("gi_rt", 0);
  const bool rt_ao = tlas && s.gi && !gi_rt && host->cfg_int("rt_ao", 0);
  const bool rt_shadows = tlas && s.shadow.view && host->cfg_int("rt_shadows", 0);
  if (tlas) {
    if (!g_rt_queries) {
      VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
      qi.queryCount = kRing * 2;
      host->vk->CreateQueryPool(host->device, &qi, nullptr, &g_rt_queries);
    }
    rt_timing_collect();
    pc.rt_stats = host->cfg_int("rt_stats", 0) ? 1u : 0u;
    if (pc.rt_stats) stats_begin(cmd, host->cfg_int("rt_alpha_test", 1) != 0);
    bind_rt(b, tlas, alpha);
    if (!g_rt_logged) {
      g_rt_logged = true;
      host->log("rt: first traced frame at %ux%u (TLAS %p, shadows %d, AO %d, GI %d)", g_w, g_h, (void*)tlas, rt_shadows,
                rt_ao, gi_rt);
    }
  }
  bool rt_started = false;
  auto rt_begin = [&] {
    if (!rt_started) rt_timestamp(cmd, 0);
    rt_started = true;
  };
  // Depth, normals and AO come from earlier graphics passes.
  memory_barrier(cmd, kAll, kCS);
  if (s.gi) {
    trace_timing_collect();
    trace_timestamp(cmd, 0);
    if (gi_rt) {
      rt_begin();
      dispatch(cmd, g_trace_rt, b, pc, g_hw, g_hh, true);
    } else {
      dispatch(cmd, g_trace, b, pc, g_hw, g_hh);
    }
    trace_timestamp(cmd, 1, gi_rt);
    memory_barrier(cmd, kCS, kCS);
    if (rt_ao) {
      // Ray-traced visibility replaces the screen-space one before accumulation.
      rt_begin();
      dispatch(cmd, g_rt_ao, b, pc, g_hw, g_hh, true);
      memory_barrier(cmd, kCS, kCS);
    }
    // The MVs are sampled in SHADER_READ_ONLY; their pass may leave them elsewhere.
    bool mv_move = pc.has_mv && g_mv_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (mv_move) image_layout(cmd, g_mv_image, g_mv_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kAll, kCS);
    dispatch(cmd, g_temporal, b, pc, g_hw, g_hh);
    if (mv_move) image_layout(cmd, g_mv_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, g_mv_layout, kCS, kAll);
    memory_barrier(cmd, kCS, kCS);
    if (pc.ao > 0.0f || pc.debug == 3) {
      image_layout(cmd, s.ao.image, s.ao.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
      dispatch(cmd, g_ao_apply, b, pc, g_w, g_h);
      image_layout(cmd, s.ao.image, VK_IMAGE_LAYOUT_GENERAL, s.ao.layout, kCS, kCS);
    }
  }
  // Ray-traced sun shadows (Track D) when the host has a scene, otherwise
  // screen-space contact shadows; both fold into the game's sun shadow mask.
  s.tlas = tlas;   // the post pass's debug views
  s.alpha = alpha;
  if (rt_shadows) {
    rt_begin();
    image_layout(cmd, s.shadow.image, s.shadow.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    dispatch(cmd, g_rt_shadow, b, pc, g_w, g_h, true);
    image_layout(cmd, s.shadow.image, VK_IMAGE_LAYOUT_GENERAL, s.shadow.layout, kCS, kCS);
  } else if (s.shadow.view && host->cfg_int("contact_shadows", 1)) {
    image_layout(cmd, s.shadow.image, s.shadow.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    dispatch(cmd, g_contact, b, pc, g_w, g_h);
    image_layout(cmd, s.shadow.image, VK_IMAGE_LAYOUT_GENERAL, s.shadow.layout, kCS, kCS);
  }
  if (rt_started) {
    rt_timestamp(cmd, 1);
    g_rt_written[g_rt_slot] = true;
    g_rt_slot = (g_rt_slot + 1) % kRing;
  }
  timestamp(cmd, 1);
  host->restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  return true;
}

void run_post(VkCommandBuffer cmd, const Snapshot& s) {
  Bound b;
  bind_all(b, s);
  Push pc = make_push();
  // The tiled lighting just wrote diffuse/specular from compute.
  memory_barrier(cmd, kCS, kCS);
  timestamp(cmd, 2);
  if (s.gi) dispatch(cmd, g_composite, b, pc, g_w, g_h);
  if (s.tlas && pc.rt_debug >= 1 && pc.rt_debug <= 5) {
    memory_barrier(cmd, kCS, kCS);
    bind_rt(b, s.tlas, s.alpha);
    dispatch(cmd, g_rt_debug, b, pc, g_w, g_h, true);
  }
  timestamp(cmd, 3);
  g_slot_written[g_slot] = true;
  g_slot = (g_slot + 1) % kRing;
  // The SSS pass samples the diffuse buffer in its fragment shader.
  memory_barrier(cmd, kCS, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | kCS);
  if (s.gi) g_parity ^= 1;
  host->restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  if (!g_logged) {
    g_logged = true;
    host->log("gi: first frame done at %ux%u; AO layout %d, diffuse layout %d, cam UBO +%llu", g_w, g_h, s.ao.layout,
              s.dif.layout, (unsigned long long)s.cam.offset);
  }
}

// Logs what a few indirect dispatches have bound, to diagnose recognition.
void describe(VkCommandBuffer cmd, const Bg3DispatchInfo* d) {
  static int n = 0;
  if (n >= host->cfg_int("gi_describe", 0)) return;
  ++n;
  char line[1024];
  int len = snprintf(line, sizeof line, "gi: dispatch cs %016llx:", (unsigned long long)d->cs_hash);
  const uint32_t probes[][2] = {{0, 1}, {0, 2}, {0, 4}, {1, 0}, {1, 1}, {1, 2}, {1, 3},
                                {1, 5}, {1, 6}, {1, 7}, {1, 9}, {1, 10}, {1, 11}};
  for (auto& p : probes) {
    Bg3Binding b{};
    bool ok = host->query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p[0], p[1], 0, &b);
    len += snprintf(line + len, sizeof line - len, " s%ub%u=%s", p[0], p[1], !ok ? "-" : b.buffer ? "buf" : b.view ? "img" : "?");
    if (ok && b.view) len += snprintf(line + len, sizeof line - len, "(f%d %ux%u L%d)", b.format, b.width, b.height, b.layout);
    if (ok && b.buffer) len += snprintf(line + len, sizeof line - len, "(t%d +%llu)", b.type, (unsigned long long)b.offset);
  }
  host->log("%s", line);
}

void flush(VkCommandBuffer cmd) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_pending.find(cmd);
  if (it == g_pending.end()) return;
  Snapshot s = it->second;
  g_pending.erase(it);
  run_post(cmd, s);
}

}  // namespace

void gi_on_dispatch_pre(VkCommandBuffer cmd, const Bg3DispatchInfo* d) {
  // GI, or just the ray-traced sun shadows (rt_shadows works with gi=0).
  if (!d->indirect || (!host->cfg_int("gi", 0) && !host->cfg_int("rt_shadows", 0))) return;
  describe(cmd, d);
  std::lock_guard<std::mutex> lock(g_mu);
  uint64_t f = host->frame_index();
  if (f == g_last_pre_frame) return;  // only before the first lighting dispatch of a frame
  Snapshot s{};
  if (!snapshot(cmd, s)) return;
  g_last_pre_frame = f;
  if (run_pre(cmd, s)) g_pending[cmd] = s;
}

void gi_on_dispatch(VkCommandBuffer, const Bg3DispatchInfo*) {}

// Learns the layout the game's velocity pass leaves the motion vectors in.
void gi_on_pass_end(VkCommandBuffer, const Bg3PassInfo* p) {
  if (p->color_count != 1 || p->color_formats[0] != VK_FORMAT_R16G16_SFLOAT || !host->dlss_motion_vectors) return;
  VkImage im;
  VkImageView v;
  VkFormat f;
  float sc[2];
  bool have = host->dlss_motion_vectors(&im, &v, &f, sc);
  static int seen = 0;
  if (seen < host->cfg_int("gi_describe_mv", 0)) {
    ++seen;
    host->log("gi: RG16F pass: image %p layout %d draws %u fs %016llx | DLSS MV %s %p", (void*)p->color_images[0],
              p->color_layouts[0], p->draw_count, (unsigned long long)p->first_fs_hash, have ? "image" : "none",
              have ? (void*)im : nullptr);
  }
  if (!have || im != p->color_images[0]) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_mv_layout != p->color_layouts[0])
    host->log("gi: motion vectors found (image %p), left in layout %d by their pass", (void*)im, p->color_layouts[0]);
  g_mv_layout = p->color_layouts[0];
  g_mv_frame = host->frame_index();
}

// The lighting and the SSS pass that reads its output are in different
// command buffers: finish at the end of the lighting's own buffer, or at the
// first render pass after it if that comes first.
void gi_on_pass_begin(VkCommandBuffer cmd, const Bg3PassInfo* p) {
  if (p->indirect_dispatches_before) flush(cmd);
}

void gi_on_cmd_end(VkCommandBuffer cmd) { flush(cmd); }

void gi_on_frame(uint64_t) {}

void gi_release() {
  std::lock_guard<std::mutex> lock(g_mu);
  release_images();
  for (Pipe* p : {&g_trace, &g_temporal, &g_ao_apply, &g_composite, &g_contact, &g_rt_shadow, &g_rt_debug, &g_rt_ao,
                  &g_trace_rt}) {
    host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)p->pipeline);
    *p = Pipe{};
  }
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_queries);
  g_queries = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_rt_queries);
  g_rt_queries = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_tr_queries);
  g_tr_queries = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)g_stats_buf);
  host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_stats_mem);
  g_stats_buf = VK_NULL_HANDLE;
  g_stats_mem = VK_NULL_HANDLE;
  g_stats_map = nullptr;
  std::memset(g_stats_frame, 0, sizeof g_stats_frame);
  host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_layout);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_set_layout);
  host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_rt_layout);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_rt_set_layout);
  g_layout = VK_NULL_HANDLE;
  g_set_layout = VK_NULL_HANDLE;
  g_rt_layout = VK_NULL_HANDLE;
  g_rt_set_layout = VK_NULL_HANDLE;
  g_pending.clear();
}
