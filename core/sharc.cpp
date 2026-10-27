// SHaRC radiance cache, v1: resources, constants, statistics and timing (core/sharc.h).
#include "sharc.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

// shaders/gi/sharc_common.hlsli SharcConsts (std140: 20 scalars).
struct SharcConsts {
  uint32_t capacity;
  float scene_scale, log_base, level_bias;
  float radiance_scale;
  uint32_t accum_frames, stale_frames, flags;
  uint32_t stride, debug, stats_off, frame;
  uint32_t groups_x;
  float rf_rough, rf_cone, near_voxels;
  float max_radiance, reset_jump, em_scale;
  float gi_base_radius;   // gi_rt_radius: the occlusion range of hits without radiance
};
static_assert(sizeof(SharcConsts) == 80, "SharcConsts must match sharc_common.hlsli");

constexpr uint32_t kRing = 8;
constexpr uint32_t kCounters = 24;              // sharc_common.hlsli kSh*
constexpr uint32_t kCounterWords = kCounters * 64;   // x 64 bins (kShBins)
constexpr VkDeviceSize kStateCounters = 16;     // byte offset of the counters in the state buffer
constexpr VkDeviceSize kStateBytes = kStateCounters + kCounterWords * 4;

struct Buf {
  VkBuffer b = VK_NULL_HANDLE;
  VkDeviceMemory m = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
};

Buf g_hash, g_accum, g_resolved, g_lock, g_consts, g_state;
uint32_t g_log2 = 0;          // of the current allocation (0: none)
bool g_lock_mode = false;
bool g_unsupported_logged = false;
uint32_t g_alloc_fail_log2 = 0;
VkDescriptorSetLayout g_dsl = VK_NULL_HANDLE;
VkDescriptorPool g_pool = VK_NULL_HANDLE;
VkDescriptorSet g_set = VK_NULL_HANDLE;
uint64_t g_last_frame = 0;    // frame of the last sharc_begin

// Counters: device-local state buffer -> host ring (copied in sharc_end), folded kRing frames later.
VkBuffer g_rb = VK_NULL_HANDLE;
VkDeviceMemory g_rb_mem = VK_NULL_HANDLE;
uint32_t* g_rb_map = nullptr;
uint64_t g_rb_frame[kRing] = {};
uint64_t g_sum[kCounters] = {};
uint32_t g_sum_n = 0;

// Timing: 4 timestamps per frame (resolve start/end, inject start/end).
VkQueryPool g_qp = VK_NULL_HANDLE;
uint32_t g_slot = 0;
uint32_t g_written[kRing] = {};   // bit i: timestamp i was written in that slot
std::vector<float> g_ms_resolve, g_ms_inject;
double g_ns_per_tick = 1.0;

const Bg3DeviceInfo* device_info() {
  auto* d = static_cast<const Bg3DeviceInfo*>(host->persist(BG3_DEVICE_INFO_KEY, sizeof(Bg3DeviceInfo)));
  return d && d->version >= 1 ? d : nullptr;
}

uint32_t device_local_type(uint32_t bits) {
  const Bg3DeviceInfo* d = device_info();
  if (!d) return UINT32_MAX;
  for (uint32_t i = 0; i < d->memory.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (d->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return i;
  return UINT32_MAX;
}

void free_buf(Buf& b) {
  host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)b.b);
  host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)b.m);
  b = Buf{};
}

bool make_buf(Buf& out, VkDeviceSize size, VkBufferUsageFlags usage) {
  const VkDeviceFuncs& vk = *host->vk;
  out = Buf{};
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  if (vk.CreateBuffer(host->device, &bci, nullptr, &out.b) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  vk.GetBufferMemoryRequirements(host->device, out.b, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = device_local_type(req.memoryTypeBits);
  if (mai.memoryTypeIndex == UINT32_MAX || vk.AllocateMemory(host->device, &mai, nullptr, &out.m) != VK_SUCCESS ||
      vk.BindBufferMemory(host->device, out.b, out.m, 0) != VK_SUCCESS) {
    vk.DestroyBuffer(host->device, out.b, nullptr);
    if (out.m) vk.FreeMemory(host->device, out.m, nullptr);
    out = Buf{};
    return false;
  }
  out.size = size;
  return true;
}

void release_buffers() {
  for (Buf* b : {&g_hash, &g_accum, &g_resolved, &g_lock, &g_consts, &g_state}) free_buf(*b);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_POOL, (uint64_t)g_pool);   // frees the set
  g_pool = VK_NULL_HANDLE;
  g_set = VK_NULL_HANDLE;
  g_log2 = 0;
}

void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  host->vk->CmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

// Buffers for 2^log2 entries and the set that binds them.
bool allocate(uint32_t log2, bool lock) {
  release_buffers();
  const VkDeviceSize n = VkDeviceSize(1) << log2;
  const VkBufferUsageFlags sb = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bool ok = make_buf(g_hash, n * 8, sb) && make_buf(g_accum, n * 16, sb) && make_buf(g_resolved, n * 16, sb) &&
            (!lock || make_buf(g_lock, n * 4, sb)) &&
            make_buf(g_consts, sizeof(SharcConsts), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT) &&
            make_buf(g_state, kStateBytes, sb | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  if (ok) {
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 1;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = ps;
    VkDescriptorSetLayout dsl = sharc_set_layout();
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ok = dsl && host->vk->CreateDescriptorPool(host->device, &pci, nullptr, &g_pool) == VK_SUCCESS;
    if (ok) {
      ai.descriptorPool = g_pool;
      ai.descriptorSetCount = 1;
      ai.pSetLayouts = &dsl;
      ok = host->vk->AllocateDescriptorSets(host->device, &ai, &g_set) == VK_SUCCESS;
    }
  }
  if (!ok) {
    if (g_alloc_fail_log2 != log2)
      host->log("sharc: allocation of 2^%u entries (%llu MiB) failed; the cache stays off", log2,
                (unsigned long long)((n * (40 + (lock ? 4 : 0))) >> 20));
    g_alloc_fail_log2 = log2;
    release_buffers();
    return false;
  }
  // Binding 5 (lock) without the lock mode: any storage buffer (the shaders do not read it).
  const Buf* src[6] = {&g_hash, &g_accum, &g_resolved, &g_consts, &g_state, lock ? &g_lock : &g_state};
  VkDescriptorBufferInfo bi[6];
  VkWriteDescriptorSet w[6];
  for (uint32_t i = 0; i < 6; ++i) {
    bi[i] = {src[i]->b, 0, VK_WHOLE_SIZE};
    w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w[i].dstSet = g_set;
    w[i].dstBinding = i;
    w[i].descriptorCount = 1;
    w[i].descriptorType = i == 3 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[i].pBufferInfo = &bi[i];
  }
  host->vk->UpdateDescriptorSets(host->device, 6, w, 0, nullptr);
  g_log2 = log2;
  g_lock_mode = lock;
  host->log("sharc: 2^%u entries: hash keys %llu MiB, accumulation %llu MiB, resolved %llu MiB%s = %llu MiB (%s)", log2,
            (unsigned long long)((n * 8) >> 20), (unsigned long long)((n * 16) >> 20), (unsigned long long)((n * 16) >> 20),
            lock ? ", lock 16 B/4 entries" : "", (unsigned long long)((n * (40 + (lock ? 4 : 0))) >> 20),
            lock ? "lock-buffer insertion: no shaderBufferInt64Atomics" : "64-bit atomics");
  return true;
}

// Zeroes the cache (all entries invalid) and the injection camera.
void clear(VkCommandBuffer cmd) {
  const VkDeviceFuncs& vk = *host->vk;
  barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  for (Buf* b : {&g_hash, &g_accum, &g_resolved, &g_lock, &g_state})
    if (b->b) vk.CmdFillBuffer(cmd, b->b, 0, VK_WHOLE_SIZE, 0);
}

void timing_collect() {
  if (!g_qp) return;
  const uint32_t old = (g_slot + 1) % kRing;
  const uint32_t mask = g_written[old];
  if ((mask & 3u) != 3u) return;
  uint64_t t[4] = {};
  if (host->vk->GetQueryPoolResults(host->device, g_qp, old * 4, (mask & 12u) == 12u ? 4 : 2, sizeof t, t, sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    return;
  g_written[old] = 0;
  const double r = double(t[1] - t[0]) * g_ns_per_tick * 1e-6;
  if (r >= 0 && r < 1000) g_ms_resolve.push_back(float(r));
  if ((mask & 12u) == 12u) {
    const double i = double(t[3] - t[2]) * g_ns_per_tick * 1e-6;
    if (i >= 0 && i < 1000) g_ms_inject.push_back(float(i));
  }
  if (g_ms_resolve.size() < 300) return;
  auto med = [](std::vector<float>& v) {
    if (v.empty()) return -1.0f;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
  };
  const size_t ni = g_ms_inject.size();
  const float mi = med(g_ms_inject), mr = med(g_ms_resolve);
  host->log("sharc: resolve GPU median %.3f ms, p90 %.3f; inject median %.3f ms (%zu of %zu frames injected); "
            "2^%u entries, stride %d, GI radius %.1f m, query 0x%x",
            mr, g_ms_resolve[g_ms_resolve.size() * 9 / 10], mi, ni, g_ms_resolve.size(), g_log2,
            host->cfg_int("sharc_inject_stride", 2), host->cfg_float("sharc_gi_radius", 4.0f), host->cfg_int("sharc_query", 7));
  g_ms_resolve.clear();
  g_ms_inject.clear();
}

void stats_fold(uint64_t now) {
  if (!g_rb_map) return;
  const uint32_t slot = uint32_t(now % kRing);
  if (!g_rb_frame[slot] || g_rb_frame[slot] + kRing - 1 > now) return;
  const uint32_t* c = g_rb_map + slot * kCounterWords;
  for (uint32_t i = 0; i < kCounters; ++i)
    for (uint32_t b = 0; b < 64; ++b) g_sum[i] += c[i * 64 + b];
  g_rb_frame[slot] = 0;
  if (++g_sum_n < 300) return;
  const double n = g_sum_n, cap = double(1u << g_log2);
  const uint64_t* t = g_sum;
  const double gi = double(t[3] + t[4]), q = double(t[5] ? t[5] : 1), ask = double(t[5] + t[7] ? t[5] + t[7] : 1);
  host->log("sharc stats (%u frames): occupancy %.2f%% (%.0f of 2^%u entries live), evicted %.0f/frame, resets %llu; "
            "injected %.0f px/frame (insert failed %.3f%%, with emission %.1f%%, skipped sky/unlit %.0f)",
            g_sum_n, 100.0 * t[10] / n / cap, t[10] / n, g_log2, t[11] / n, (unsigned long long)t[12], t[0] / n,
            t[0] + t[1] ? 100.0 * t[1] / double(t[0] + t[1]) : 0.0, t[0] ? 100.0 * t[2] / double(t[0]) : 0.0, t[13] / n);
  host->log("sharc stats (%u frames): GI hits not on screen %.3fM/frame (hidden %.1f%%, off screen %.1f%%): asked %.1f%%, "
            "nearer than the voxel %.1f%% of asked, cache found %.1f%% of queried; reflections eligible %.3fM/frame, found %.1f%%",
            g_sum_n, gi / n * 1e-6, gi ? 100.0 * t[3] / gi : 0.0, gi ? 100.0 * t[4] / gi : 0.0,
            gi ? 100.0 * (t[5] + t[7]) / gi : 0.0, 100.0 * t[7] / ask, 100.0 * t[6] / q, t[8] / n * 1e-6,
            t[8] ? 100.0 * t[9] / double(t[8]) : 0.0);
  if (t[16]) {   // the leak test ran (sharc_leak_check=1)
    host->log("sharc stats (%u frames): leak test: cache hits from a cell occluded from the query point %.2f%% (%.0f/frame), "
              "from a cell thicker than the surface %.2f%%; of the cache's GI contribution: occluded cells %.3f%%, thin %.3f%% "
              "(reject_occluded %d)",
              g_sum_n, t[6] + t[14] ? 100.0 * t[14] / double(t[6] + t[14]) : 0.0, t[14] / n,
              t[6] ? 100.0 * t[15] / double(t[6]) : 0.0, 100.0 * t[17] / double(t[16]), 100.0 * t[18] / double(t[16]),
              host->cfg_int("sharc_reject_occluded", 1));
  }
  std::memset(g_sum, 0, sizeof g_sum);  g_sum_n = 0;
}

}  // namespace

VkDescriptorSetLayout sharc_set_layout() {
  if (g_dsl) return g_dsl;
  VkDescriptorSetLayoutBinding b[6];
  for (uint32_t i = 0; i < 6; ++i)
    b[i] = {i, i == 3 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
            nullptr};
  VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  ci.bindingCount = 6;
  ci.pBindings = b;
  if (host->vk->CreateDescriptorSetLayout(host->device, &ci, nullptr, &g_dsl) != VK_SUCCESS) g_dsl = VK_NULL_HANDLE;
  return g_dsl;
}

bool sharc_supported() {
  const Bg3DeviceInfo* d = device_info();
  const uint32_t need = BG3_DEV_INT64 | BG3_DEV_FLOAT16 | BG3_DEV_STORAGE16;
  const bool ok = d && (d->features & need) == need && host->vk->CmdUpdateBuffer && host->vk->CmdFillBuffer;
  if (!ok && !g_unsupported_logged) {
    g_unsupported_logged = true;
    host->log("sharc: the device lacks the cache's features (enabled 0x%x, needs 0x%x: shaderInt64, shaderFloat16, "
              "storageBuffer16BitAccess; the layer enables them with rt=1, sharc_device=1); sharc stays off",
              d ? d->features : 0u, need);
  }
  return ok;
}

bool sharc_begin(VkCommandBuffer cmd, SharcFrame* out) {
  if (!sharc_supported()) return false;
  const VkDeviceFuncs& vk = *host->vk;
  const uint64_t now = host->frame_index();
  const uint32_t log2 = uint32_t((std::min)((std::max)(host->cfg_int("sharc_log2", 22), 16), 24));
  const bool lock = (device_info()->features & BG3_DEV_INT64_ATOMICS) == 0;
  bool cleared = false;
  if (g_log2 != log2 || g_lock_mode != lock || !g_set) {
    if (g_alloc_fail_log2 == log2 || !allocate(log2, lock)) return false;
    cleared = true;
  }
  if (!g_rb) {
    void* m = nullptr;
    if (host->alloc_host_buffer &&
        host->alloc_host_buffer(kRing * kCounterWords * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &g_rb, &g_rb_mem, &m)) {
      g_rb_map = static_cast<uint32_t*>(m);
      std::memset(g_rb_frame, 0, sizeof g_rb_frame);
    }
  }
  if (!g_qp) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kRing * 4;
    if (vk.CreateQueryPool(host->device, &qi, nullptr, &g_qp) != VK_SUCCESS) g_qp = VK_NULL_HANDLE;
    g_ns_per_tick = host->cfg_float("gpu_timestamp_period_ns", 1.0f);
  }
  timing_collect();
  stats_fold(now);
  // Re-enabled, or the GI did not run for a while (a load screen): what the cache holds may
  // belong to another place.
  const bool stale = g_last_frame + 8 < now;
  if (cleared || stale) {
    clear(cmd);
    if (!cleared) host->log("sharc: cache cleared (not updated for %llu frames)", (unsigned long long)(now - g_last_frame));
  } else {
    barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);   // last frame's reads first
    vk.CmdFillBuffer(cmd, g_state.b, kStateCounters, kCounterWords * 4, 0);
  }
  g_last_frame = now;

  const uint32_t cap = 1u << log2, groups = cap / 256u;
  SharcConsts c{};
  c.capacity = cap;
  c.scene_scale = (std::max)(host->cfg_float("sharc_scene_scale", 50.0f), 1e-3f);
  c.log_base = (std::max)(host->cfg_float("sharc_log_base", 2.0f), 1.01f);
  c.level_bias = host->cfg_float("sharc_level_bias", 0.0f);
  c.radiance_scale = (std::max)(host->cfg_float("sharc_radiance_scale", 1000.0f), 1.0f);
  c.accum_frames = uint32_t((std::max)(host->cfg_int("sharc_frames", 16), 1));
  c.stale_frames = uint32_t((std::max)(host->cfg_int("sharc_stale", 128), 1));
  const uint32_t query = uint32_t(host->cfg_int("sharc_query", 7)) & 7u;
  // Leak test (sharc_common.hlsli sharc_cell_test): bit 4 measures, bit 5 rejects occluded cells.
  const bool leak_check = host->cfg_int("sharc_leak_check", 0) != 0 || host->cfg_int("sharc_debug", 0) == 6;
  c.flags = query | (host->cfg_int("sharc_stats", 0) ? 8u : 0u) | (leak_check ? 16u : 0u) |
            (host->cfg_int("sharc_reject_occluded", 1) ? 32u : 0u);
  c.stride = uint32_t((std::min)((std::max)(host->cfg_int("sharc_inject_stride", 2), 1), 16));
  c.debug = uint32_t((std::max)(host->cfg_int("sharc_debug", 0), 0));
  c.stats_off = uint32_t(kStateCounters);
  c.frame = uint32_t(now);
  c.groups_x = (std::min)(groups, 32768u);
  c.rf_rough = host->cfg_float("sharc_rf_rough", 0.2f);
  c.rf_cone = host->cfg_float("sharc_rf_cone", 1.0f);
  c.near_voxels = host->cfg_float("sharc_near", 1.0f);
  // The accumulation is 32-bit fixed point (radiance x radiance_scale summed over a voxel's
  // samples of a frame): 256 x 1000 x 4096 samples stays below 2^32.
  c.max_radiance = host->cfg_float("sharc_max_radiance", 256.0f);
  c.reset_jump = host->cfg_float("sharc_reset_jump", 50.0f);
  c.em_scale = host->cfg_float("sharc_em", 1.0f);
  c.gi_base_radius = (std::max)(host->cfg_float("gi_rt_radius", 1.0f), 0.01f);
  vk.CmdUpdateBuffer(cmd, g_consts.b, 0, sizeof c, &c);
  barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

  out->set = g_set;
  out->debug = c.debug;
  out->query = query;
  out->resolve_groups[0] = c.groups_x;
  out->resolve_groups[1] = (groups + c.groups_x - 1) / c.groups_x;
  out->lock = lock;
  return true;
}

void sharc_end(VkCommandBuffer cmd) {
  if (g_rb && g_state.b) {
    const uint64_t now = host->frame_index();
    const uint32_t slot = uint32_t(now % kRing);
    barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferCopy r{kStateCounters, VkDeviceSize(slot) * kCounterWords * 4, kCounterWords * 4};
    host->vk->CmdCopyBuffer(cmd, g_state.b, g_rb, 1, &r);
    if (host->cfg_int("sharc_stats", 0)) g_rb_frame[slot] = now;
  }
  if (g_written[g_slot]) g_slot = (g_slot + 1) % kRing;
}

void sharc_timestamp(VkCommandBuffer cmd, uint32_t index) {
  if (!g_qp || index > 3) return;
  const uint32_t q = g_slot * 4 + index;
  if (index == 0) g_written[g_slot] = 0;
  host->vk->CmdResetQueryPool(cmd, g_qp, q, 1);
  host->vk->CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_qp, q);
  g_written[g_slot] |= 1u << index;
}

void sharc_release() {
  release_buffers();
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_dsl);
  g_dsl = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)g_rb);
  host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_rb_mem);
  g_rb = VK_NULL_HANDLE;
  g_rb_mem = VK_NULL_HANDLE;
  g_rb_map = nullptr;
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_qp);
  g_qp = VK_NULL_HANDLE;
  std::memset(g_written, 0, sizeof g_written);
  g_ms_resolve.clear();
  g_ms_inject.clear();
  g_alloc_fail_log2 = 0;
  g_unsupported_logged = false;
  g_last_frame = 0;
}
