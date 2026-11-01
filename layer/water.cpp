// M5.1 water geometry (docs/PLAN_M5_WATER.md, rt_water_geometry, default 0). The forward
// water surfaces go into a scene of their own: XFB soup, one BLAS (a geometry per draw) and a
// one-instance TLAS per game submission, plus a record per draw. Data only: nothing binds the
// water TLAS yet, so the image cannot change (M5.2+ add consumers, ABI 25 then).
//
// Reuses the D-EM forward-capture machinery (layer/emissive.cpp, docs/TRACK_D_EMISSIVE.md
// 13): the same pass hook (geocapture.cpp em_pre_draw: one B10G11R11 target + D24S8 at render
// resolution, which is also the forward pass), the per-draw XFB of the already patched vertex
// shaders, rt_convert with the pass's camera UBO (set 0 b2) after the pass, and a build in a
// fenced command buffer of ours submitted right after the game's batch on the same queue.
// Water has its own soup, records and acceleration structures: it is never in the opaque or
// emissive TLAS (no water-shaped blockers in other queries).
//
// Classification and record are policy (common/water_policy.h), run by the core through
// Bg3CoreApi::em_record with forward == BG3_EM_REQ_WATER (hot-reloaded, no ABI change). Here:
// the asset-DB lookup, the capture, the build, the counters and the "[water]" log lines.
// Key off: no pass is hooked for water and nothing is allocated (resources are created at the
// first enabled pass, so the key is live).
#include "host.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>

#include "assetdb.h"
#include "../common/water_policy.h"

namespace bg3 {

namespace {

constexpr uint32_t kOwn = 4;            // own command buffers = acceleration-structure slots (fenced)
constexpr uint32_t kMaxDraws = 256;     // captured water draws per build (BLAS geometries)
constexpr uint32_t kSampleVerts = 8;    // vertices per draw read back for the position check
constexpr uint32_t kMaxStride = 256;    // XFB bytes per vertex the readback allows for
constexpr VkDeviceSize up256(VkDeviceSize v) { return (v + 255) / 256 * 256; }
constexpr VkDeviceSize kReadRegion = up256(VkDeviceSize(kMaxDraws) * kSampleVerts * kMaxStride);

constexpr VkPipelineStageFlags kAS = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
constexpr VkPipelineStageFlags kCS = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags kXFB = VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;
constexpr VkPipelineStageFlags kXfer = VK_PIPELINE_STAGE_TRANSFER_BIT;

using water::WaterRec;

struct Buf {
  VkBuffer b = VK_NULL_HANDLE;
  VkDeviceMemory m = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  uint8_t* map = nullptr;
};

DeviceData* g_dd = nullptr;
std::mutex g_mu;
bool g_ready = false;        // resources allocated (at the first enabled pass)
bool g_init_failed = false;
float g_ts_period = 1.0f;

// ---- the asset DB per fragment shader (pass FOR only goes to the policy) ----
struct Mat {
  bool forward = false;
  Bg3MaterialInfo info{};
  std::string name;
};
std::unordered_map<uint64_t, Mat> g_mats;

const Mat& material(uint64_t fs) {
  auto it = g_mats.find(fs);
  if (it != g_mats.end()) return it->second;
  Mat m;
  const Bg3AssetDbApi* db = assetdb_api();
  if (db && db->available && db->available() && db->material && db->material(fs, &m.info)) {
    m.name = m.info.template_name ? m.info.template_name : "?";
    m.forward = m.info.pass == 1;
  }
  return g_mats.emplace(fs, std::move(m)).first->second;
}

// ---- counters ----
struct Census {
  uint64_t passes = 0, pass_draws = 0;           // forward passes with a water draw; all their draws
  uint64_t cls[water::kClassCount] = {};         // water draws per class (kNotWater unused)
  uint64_t captured = 0, tris = 0, tris_wanted = 0;
  uint64_t skip_xfb = 0, skip_indirect = 0, skip_topo = 0, skip_full = 0, skip_core = 0, no_ubo = 0;
  uint64_t zwrite_on = 0, zwrite_off = 0, zwrite_dyn = 0;   // captured draws by the pipeline's depth write
  void add(const Census& o) {
    passes += o.passes, pass_draws += o.pass_draws, captured += o.captured, tris += o.tris;
    tris_wanted += o.tris_wanted;
    for (uint32_t i = 0; i < water::kClassCount; ++i) cls[i] += o.cls[i];
    skip_xfb += o.skip_xfb, skip_indirect += o.skip_indirect, skip_topo += o.skip_topo, skip_full += o.skip_full;
    skip_core += o.skip_core, no_ubo += o.no_ubo;
    zwrite_on += o.zwrite_on, zwrite_off += o.zwrite_off, zwrite_dyn += o.zwrite_dyn;
  }
  uint64_t water() const { return cls[water::kSupported] + cls[water::kUnknown] + cls[water::kExcludedFlow] + cls[water::kExcludedVfx]; }
};

// ---- per command buffer (draws recorded, not yet submitted) ----
struct PDraw {
  WaterRec rec{};
  VkBuffer soup = VK_NULL_HANDLE;
  VkDeviceSize off = 0;
  uint32_t prims = 0, stride = 0;
};
struct PCmd {
  std::vector<PDraw> draws;
  Census census;
};
std::unordered_map<VkCommandBuffer, PCmd> g_pending;

// ---- the soup (a ring of 256-aligned ranges; a third of it per build) ----
Buf g_soup;
VkDeviceSize g_soup_head = 0, g_soup_since = 0, g_demand = 0;
struct Retired { Buf b; uint32_t builds_left; };
std::vector<Retired> g_retired;

// ---- own command buffers, one acceleration-structure slot each ----
struct Slot {
  VkCommandBuffer cb = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  bool used = false, harvest = false;
  Buf as, scratch, inst, read;     // BLAS + TLAS storage, scratch, the TLAS instance (host), samples (host)
  VkAccelerationStructureKHR blas = VK_NULL_HANDLE, tlas = VK_NULL_HANDLE;
  VkDeviceSize blas_cap = 0, tlas_off = 0, tlas_cap = 0;
  VkDeviceAddress blas_addr = 0;
  // What the harvest reads back once the fence signalled.
  struct Sample { uint32_t verts, stride; VkDeviceSize at; };
  std::vector<Sample> samples;
  uint64_t tris = 0;
  uint32_t geos = 0;
};
VkCommandPool g_pool = VK_NULL_HANDLE;
Slot g_slot[kOwn];
uint32_t g_next = 0;
VkQueryPool g_qp = VK_NULL_HANDLE;
uint64_t g_latest_build = 0;   // frame of the latest submitted build (1-based)

// ---- the log window (rt_timing_frames presents) ----
struct Window {
  uint64_t frames = 0, builds = 0, busy = 0, build_failed = 0, geos = 0, build_tris = 0;
  Census c;
  std::vector<float> build_ms;
  uint64_t sampled = 0, nonfinite = 0, collapsed = 0;
  float ymin = 1e30f, ymax = -1e30f;               // sampled camera-relative heights (m)
  struct Fam { uint64_t n = 0; float rough = 0, refl = 0; uint32_t flags = 0; };
  std::map<uint32_t, Fam> family;                  // built draws per kAudited index, the latest record's roles
  std::map<std::pair<uint64_t, uint32_t>, uint64_t> other;   // (fs, class) of counted draws
};
Window g_win;

bool make_buf(Buf& out, VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
  DeviceData* dd = g_dd;
  out = Buf{};
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  if (dd->vk.CreateBuffer(dd->device, &bci, nullptr, &out.b) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetBufferMemoryRequirements(dd->device, out.b, &req);
  VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
  fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) && rt_alloc_device_address()) mai.pNext = &fi;
  mai.allocationSize = req.size;
  const VkMemoryPropertyFlags hv = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  mai.memoryTypeIndex = host ? find_memory_type(dd, req.memoryTypeBits, hv)
                             : find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.m) != VK_SUCCESS) {
    dd->vk.DestroyBuffer(dd->device, out.b, nullptr);
    out = Buf{};
    return false;
  }
  dd->vk.BindBufferMemory(dd->device, out.b, out.m, 0);
  out.size = size;
  if (host) {
    void* p = nullptr;
    if (dd->vk.MapMemory(dd->device, out.m, 0, VK_WHOLE_SIZE, 0, &p) != VK_SUCCESS) p = nullptr;
    out.map = static_cast<uint8_t*>(p);
  }
  return true;
}

void free_buf(Buf& b) {
  if (!b.b) return;
  defer_destroy(BG3_DEFER_BUFFER, uint64_t(b.b));
  defer_destroy(BG3_DEFER_MEMORY, uint64_t(b.m));
  b = Buf{};
}

void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkAccessFlags sa, VkPipelineStageFlags dst, VkAccessFlags da) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = sa;
  mb.dstAccessMask = da;
  g_dd->vk.CmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

VkBufferUsageFlags soup_usage() {
  return VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | rt_xfb_usage();
}

void release_slot_as(Slot& s) {
  if (s.blas) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(s.blas));
  if (s.tlas) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(s.tlas));
  s.blas = s.tlas = VK_NULL_HANDLE;
  free_buf(s.as);
  s.blas_cap = s.tlas_off = s.tlas_cap = 0;
  s.blas_addr = 0;
}

// Caller holds g_mu. Everything the capture needs, created once at the first enabled pass.
bool ensure_ready() {
  if (g_ready) return true;
  if (g_init_failed || !g_dd) return false;
  DeviceData* dd = g_dd;
  const VkDeviceFuncs& vk = dd->vk;
  VkPhysicalDeviceProperties props{};
  if (dd->inst && dd->inst->GetPhysicalDeviceProperties) dd->inst->GetPhysicalDeviceProperties(dd->physical, &props);
  g_ts_period = props.limits.timestampPeriod > 0 ? props.limits.timestampPeriod : 1.0f;
  const VkDeviceSize soup_mb = VkDeviceSize((std::max)(cfg_int("rt_water_xfb_mb", 32), 4));
  bool ok = make_buf(g_soup, soup_mb << 20, soup_usage(), false);
  VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cpi.queueFamilyIndex = dd->graphics_family;
  ok = ok && dd->graphics_family != UINT32_MAX && vk.CreateCommandPool(dd->device, &cpi, nullptr, &g_pool) == VK_SUCCESS;
  for (uint32_t i = 0; ok && i < kOwn; ++i) {
    Slot& s = g_slot[i];
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = g_pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    ok = vk.AllocateCommandBuffers(dd->device, &cai, &s.cb) == VK_SUCCESS;
    if (ok && dd->set_loader_data) dd->set_loader_data(dd->device, s.cb);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    ok = ok && vk.CreateFence(dd->device, &fci, nullptr, &s.fence) == VK_SUCCESS;
    ok = ok && make_buf(s.inst, 256,
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                        true) && s.inst.map;
    ok = ok && make_buf(s.read, kReadRegion, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true) && s.read.map;
  }
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qi.queryCount = kOwn * 2;
  if (ok && vk.CreateQueryPool(dd->device, &qi, nullptr, &g_qp) != VK_SUCCESS) g_qp = VK_NULL_HANDLE;
  if (!ok) {
    g_init_failed = true;
    log("[water] water geometry UNAVAILABLE (rt_water_geometry=1): allocation failed; nothing is captured");
    return false;
  }
  g_ready = true;
  log("[water] water geometry capture on (rt_water_geometry=1, docs/PLAN_M5_WATER.md M5.1): soup %llu MB, "
      "%u audited water pipelines, own TLAS (bound nowhere yet: no image change)",
      (unsigned long long)soup_mb, water::kAuditedCount);
  return true;
}

// Reads back a finished build (its fence signalled): GPU time and the sampled positions.
void harvest(uint32_t i) {
  Slot& s = g_slot[i];
  if (!s.harvest) return;
  s.harvest = false;
  if (g_qp) {
    uint64_t r[4] = {};
    const VkResult res = g_dd->vk.GetQueryPoolResults(g_dd->device, g_qp, i * 2, 2, sizeof r, r, 16,
                                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (res == VK_SUCCESS && r[1] && r[3] && r[2] >= r[0]) g_win.build_ms.push_back(float(double(r[2] - r[0]) * g_ts_period * 1e-6));
  }
  for (const Slot::Sample& sm : s.samples) {
    bool all_zero = true, bad = false;
    for (uint32_t v = 0; v < sm.verts; ++v) {
      float p[3];
      std::memcpy(p, s.read.map + sm.at + VkDeviceSize(v) * sm.stride, sizeof p);
      for (float f : p) {
        if (!std::isfinite(f)) bad = true;
        if (f != 0.0f) all_zero = false;
      }
      if (std::isfinite(p[1])) g_win.ymin = (std::min)(g_win.ymin, p[1]), g_win.ymax = (std::max)(g_win.ymax, p[1]);
    }
    ++g_win.sampled;
    g_win.nonfinite += bad;
    g_win.collapsed += all_zero;
  }
  s.samples.clear();
}

bool soup_alive(VkBuffer b) {
  if (b == g_soup.b) return true;
  for (const Retired& r : g_retired)
    if (r.b.b == b) return true;
  return false;
}

// Records the build of slot i from `draws` into its command buffer (begun). False: nothing built.
bool build(Slot& s, uint32_t i, const std::vector<const PDraw*>& draws) {
  DeviceData* dd = g_dd;
  const uint32_t ngeo = uint32_t(draws.size());
  std::vector<VkAccelerationStructureGeometryKHR> geo(ngeo);
  std::vector<VkAccelerationStructureBuildRangeInfoKHR> rng(ngeo);
  std::vector<uint32_t> counts(ngeo);
  uint64_t tris = 0;
  for (uint32_t k = 0; k < ngeo; ++k) {
    const PDraw& d = *draws[k];
    const VkDeviceAddress base = rt_buffer_address(d.soup);
    if (!base) return false;
    VkAccelerationStructureGeometryKHR& g = geo[k];
    g = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto& t = g.geometry.triangles;
    t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    t.vertexData.deviceAddress = base + d.off;
    t.vertexStride = d.stride;
    t.maxVertex = d.prims * 3 - 1;
    t.indexType = VK_INDEX_TYPE_NONE_KHR;
    counts[k] = d.prims;
    rng[k] = {d.prims, 0, 0, 0};
    tris += d.prims;
  }
  VkAccelerationStructureBuildGeometryInfoKHR bb{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bb.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  bb.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  bb.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  bb.geometryCount = ngeo;
  bb.pGeometries = geo.data();
  VkAccelerationStructureBuildSizesInfoKHR bs{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  dd->vk.GetAccelerationStructureBuildSizesKHR(dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bb,
                                               counts.data(), &bs);
  VkAccelerationStructureGeometryKHR tg{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  tg.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  tg.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  tg.geometry.instances.data.deviceAddress = rt_buffer_address(s.inst.b);
  VkAccelerationStructureBuildGeometryInfoKHR tb{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  tb.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  tb.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  tb.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  tb.geometryCount = 1;
  tb.pGeometries = &tg;
  const uint32_t one = 1;
  VkAccelerationStructureBuildSizesInfoKHR ts{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  dd->vk.GetAccelerationStructureBuildSizesKHR(dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tb, &one, &ts);
  if (!tg.geometry.instances.data.deviceAddress) return false;

  // Storage: grown (to 1.5x the need) only when this build does not fit.
  const VkDeviceSize need_blas = up256(bs.accelerationStructureSize);
  if (!s.as.b || need_blas > s.blas_cap || ts.accelerationStructureSize > s.tlas_cap) {
    release_slot_as(s);
    const VkDeviceSize blas_cap = up256((std::max)(need_blas + need_blas / 2, VkDeviceSize(1) << 20));
    const VkDeviceSize tlas_cap = up256((std::max)(ts.accelerationStructureSize, VkDeviceSize(4096)));
    if (!make_buf(s.as, blas_cap + tlas_cap,
                  VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false))
      return false;
    s.blas_cap = blas_cap;
    s.tlas_off = blas_cap;
    s.tlas_cap = tlas_cap;
    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ci.buffer = s.as.b;
    ci.size = blas_cap;
    ci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (dd->vk.CreateAccelerationStructureKHR(dd->device, &ci, nullptr, &s.blas) != VK_SUCCESS) s.blas = VK_NULL_HANDLE;
    ci.offset = s.tlas_off;
    ci.size = tlas_cap;
    ci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (dd->vk.CreateAccelerationStructureKHR(dd->device, &ci, nullptr, &s.tlas) != VK_SUCCESS) s.tlas = VK_NULL_HANDLE;
    if (!s.blas || !s.tlas) {
      release_slot_as(s);
      return false;
    }
    VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    ai.accelerationStructure = s.blas;
    s.blas_addr = dd->vk.GetAccelerationStructureDeviceAddressKHR(dd->device, &ai);
  }
  const VkDeviceSize al = (std::max)(rt_scratch_alignment(), VkDeviceSize(256));
  const VkDeviceSize need_scratch = (std::max)(bs.buildScratchSize, ts.buildScratchSize) + al;
  if (s.scratch.size < need_scratch) {
    free_buf(s.scratch);
    if (!make_buf(s.scratch, up256(need_scratch + need_scratch / 2),
                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false))
      return false;
  }
  const VkDeviceAddress scratch = (rt_buffer_address(s.scratch.b) + al - 1) / al * al;
  // The TLAS's one instance: the water BLAS, identity (camera-relative world of the build's pass).
  VkAccelerationStructureInstanceKHR ti{};
  ti.transform.matrix[0][0] = ti.transform.matrix[1][1] = ti.transform.matrix[2][2] = 1.0f;
  ti.mask = 0xFF;
  ti.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  ti.accelerationStructureReference = s.blas_addr;
  std::memcpy(s.inst.map, &ti, sizeof ti);

  if (g_qp) {
    dd->vk.CmdResetQueryPool(s.cb, g_qp, i * 2, 2);
    dd->vk.CmdWriteTimestamp(s.cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_qp, i * 2);
  }
  bb.dstAccelerationStructure = s.blas;
  bb.scratchData.deviceAddress = scratch;
  const VkAccelerationStructureBuildRangeInfoKHR* pr = rng.data();
  dd->vk.CmdBuildAccelerationStructuresKHR(s.cb, 1, &bb, &pr);
  barrier(s.cb, kAS, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, kAS,
          VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
  tb.dstAccelerationStructure = s.tlas;
  tb.scratchData.deviceAddress = scratch;
  VkAccelerationStructureBuildRangeInfoKHR tr{1, 0, 0, 0};
  const VkAccelerationStructureBuildRangeInfoKHR* ptr = &tr;
  dd->vk.CmdBuildAccelerationStructuresKHR(s.cb, 1, &tb, &ptr);
  barrier(s.cb, kAS, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, kCS | kAS,
          VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT);
  if (g_qp) dd->vk.CmdWriteTimestamp(s.cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qp, i * 2 + 1);
  // The position check: the first vertices of every draw, as the BLAS read them.
  s.samples.clear();
  VkDeviceSize at = 0;
  for (const PDraw* d : draws) {
    const uint32_t verts = (std::min)(d->prims * 3, kSampleVerts);
    if (d->stride < 12 || d->stride > kMaxStride) continue;
    const VkBufferCopy c{d->off, at, VkDeviceSize(verts) * d->stride};
    dd->vk.CmdCopyBuffer(s.cb, d->soup, s.read.b, 1, &c);
    s.samples.push_back({verts, d->stride, at});
    at += VkDeviceSize(kSampleVerts) * kMaxStride;
  }
  barrier(s.cb, kXfer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
  s.tris = tris;
  s.geos = ngeo;
  return true;
}

}  // namespace

// ================= entry points =================

void water_device_created(DeviceData* dd) {
  if (!g_dd) g_dd = dd;   // nothing is allocated until the key is on (ensure_ready)
}

void water_device_destroyed(DeviceData* dd) {
  if (dd != g_dd) return;
  std::lock_guard<std::mutex> lock(g_mu);
  const VkDeviceFuncs& vk = dd->vk;
  for (Slot& s : g_slot) {
    if (s.fence) {
      if (s.used) vk.WaitForFences(dd->device, 1, &s.fence, VK_TRUE, 1000000000ull);
      vk.DestroyFence(dd->device, s.fence, nullptr);
    }
    release_slot_as(s);
    for (Buf* b : {&s.scratch, &s.inst, &s.read}) free_buf(*b);
    s = Slot{};
  }
  if (g_pool) vk.DestroyCommandPool(dd->device, g_pool, nullptr);
  g_pool = VK_NULL_HANDLE;
  if (g_qp) defer_destroy(BG3_DEFER_QUERY_POOL, uint64_t(g_qp));
  g_qp = VK_NULL_HANDLE;
  free_buf(g_soup);
  for (Retired& r : g_retired) free_buf(r.b);
  g_retired.clear();
  g_pending.clear();
  g_ready = g_init_failed = false;
  g_dd = nullptr;
}

bool water_wanted() { return g_dd && rt_enabled() && cfg_hot(CfgHot::rt_water_geometry) != 0; }

bool water_is_pass(const Bg3PassInfo& p, uint32_t gw, uint32_t gh) {
  return water_wanted() && p.color_count == 1 && p.color_formats[0] == VK_FORMAT_B10G11R11_UFLOAT_PACK32 &&
         p.depth_format == VK_FORMAT_D24_UNORM_S8_UINT && gw && p.width == gw && p.height == gh;
}

bool water_pass_begin(VkCommandBuffer cmd) {
  if (!water_wanted()) return false;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!ensure_ready()) return false;
  }
  // Earlier builds read the soup ranges this pass may overwrite.
  barrier(cmd, kAS | kXfer, VK_ACCESS_MEMORY_READ_BIT, kXFB, VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT);
  return true;
}

int water_draw(VkCommandBuffer cmd, const CmdState* s, const DrawArgs& a, const EmDrawIn& in, int depth_write,
               WaterRange* out) {
  const auto core = core_shared_lock();   // the policy is the core's (before g_mu)
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_ready) return 0;
  const Mat& mat = material(s->fs);
  if (!mat.forward) return 0;
  PCmd& P = g_pending[cmd];
  Census& C = P.census;
  const Bg3EmRecordIn rin{cmd, s->fs, &mat.info, mat.name.c_str(), BG3_EM_REQ_WATER, uint32_t(s->vs >> 32)};
  Bg3EmRecordOut ro;
  WaterRec rec{};
  if (!em_core_record(&rin, &ro)) return ++C.skip_core, 0;
  std::memcpy(&rec, ro.rec, sizeof rec);
  if (rec.magic != water::kMagic) return ++C.skip_core, 0;   // a core without the water route
  if (rec.cls == water::kNotWater || rec.cls >= water::kClassCount) return 0;
  ++C.cls[rec.cls];
  if (rec.cls != water::kSupported) {
    ++g_win.other[{s->fs, rec.cls}];
    return 1;
  }
  const bool list = in.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  uint64_t prims = 0;
  if (a.kind <= 1) {
    prims = list ? a.count / 3 : (a.count > 2 ? a.count - 2 : 0);
    prims *= a.instances;
    C.tris_wanted += prims;
  }
  if (!in.xfb) return ++C.skip_xfb, 1;
  if (a.kind > 1) return ++C.skip_indirect, 1;
  if (!in.triangles || (!list && !((in.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
                                     in.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) &&
                                    (a.kind == 0 || !in.restart))))
    return ++C.skip_topo, 1;
  if (!prims) return 1;
  const VkDeviceSize need = prims * 3 * in.stride;
  g_demand += up256(need);
  if (g_soup_since + need > g_soup.size / 3) return ++C.skip_full, 1;
  if (g_soup_head + need > g_soup.size) g_soup_head = 0;
  PDraw d;
  d.rec = rec;
  d.soup = g_soup.b;
  d.off = g_soup_head;
  d.prims = uint32_t(prims);
  d.stride = in.stride;
  g_soup_head = up256(g_soup_head + need);
  g_soup_since += up256(need);
  P.draws.push_back(d);
  ++C.captured;
  C.tris += prims;
  (depth_write > 0 ? C.zwrite_on : depth_write == 0 ? C.zwrite_off : C.zwrite_dyn) += 1;
  *out = WaterRange{d.soup, d.off, need, d.prims};
  return 2;
}

void water_pass_done(VkCommandBuffer cmd, VkBuffer ubo, VkDeviceSize ubo_off, uint32_t stride,
                     const std::vector<WaterRange>& ranges, uint32_t pass_draws, uint32_t water_draws) {
  if (water_draws) {
    std::lock_guard<std::mutex> lock(g_mu);
    Census& C = g_pending[cmd].census;
    ++C.passes;
    C.pass_draws += pass_draws;
    if (!ranges.empty() && !ubo) ++C.no_ubo;
  }
  if (ranges.empty()) return;
  barrier(cmd, kXFB, VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT, kCS | kXfer,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
  for (const WaterRange& r : ranges) {
    if (ubo) {
      RtCapture rc{};
      rc.xfb = r.soup;
      rc.alpha_base = r.off;
      rc.alpha_bytes = r.bytes;
      rc.stride = stride;
      rc.ubo = ubo;
      rc.ubo_offset = ubo_off;
      rt_convert(cmd, rc);
    } else {
      g_dd->vk.CmdFillBuffer(cmd, r.soup, r.off, r.bytes, 0);   // no camera: collapsed (never hit)
    }
  }
  barrier(cmd, kCS | kXfer, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, kAS | kXfer,
          VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_TRANSFER_READ_BIT);
  restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
}

void water_forget_cmd(VkCommandBuffer cmd) {
  if (!g_dd) return;
  std::lock_guard<std::mutex> lock(g_mu);
  g_pending.erase(cmd);
}

void water_after_submit(DeviceData* dd, VkQueue queue, const std::vector<VkCommandBuffer>& cmds) {
  if (dd != g_dd || !g_ready) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_pending.empty()) return;
  std::vector<PCmd> taken;
  for (VkCommandBuffer c : cmds) {
    auto it = g_pending.find(c);
    if (it == g_pending.end()) continue;
    g_win.c.add(it->second.census);
    taken.push_back(std::move(it->second));
    g_pending.erase(it);
  }
  std::vector<const PDraw*> draws;
  for (const PCmd& P : taken)
    for (const PDraw& d : P.draws)
      if (draws.size() < kMaxDraws && soup_alive(d.soup)) draws.push_back(&d);
      else ++g_win.c.skip_full;
  g_soup_since = 0;
  if (draws.empty() || !water_wanted()) return;
  const uint32_t i = g_next % kOwn;
  Slot& s = g_slot[i];
  if (s.used && dd->vk.WaitForFences(dd->device, 1, &s.fence, VK_TRUE, 0) != VK_SUCCESS) {
    ++g_win.busy;
    return;
  }
  harvest(i);
  ++g_next;
  dd->vk.ResetFences(dd->device, 1, &s.fence);
  dd->vk.ResetCommandBuffer(s.cb, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  dd->vk.BeginCommandBuffer(s.cb, &bi);
  // Everything the game's batch wrote (XFB, conversions) before our reads.
  barrier(s.cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
  const bool built = build(s, i, draws);
  if (built) {
    ++g_win.builds;
    g_win.geos += s.geos;
    g_win.build_tris += s.tris;
    for (const PDraw* d : draws)
      if (d->rec.family != water::kNoFamily) {
        Window::Fam& f = g_win.family[d->rec.family];
        ++f.n;
        f.rough = d->rec.roughness;
        f.refl = d->rec.reflectance;
        f.flags |= d->rec.flags;
      }
  } else {
    ++g_win.build_failed;
  }
  for (size_t k = 0; k < g_retired.size();) {
    if (--g_retired[k].builds_left == 0) {
      free_buf(g_retired[k].b);
      g_retired.erase(g_retired.begin() + k);
    } else {
      ++k;
    }
  }
  // Size the soup to the demand: three frames of it (rt_water_xfb_max_mb at most).
  const VkDeviceSize max_soup = VkDeviceSize((std::max)(cfg_int("rt_water_xfb_max_mb", 256), 8)) << 20;
  if (g_demand * 3 > g_soup.size && g_soup.size < max_soup) {
    VkDeviceSize want = g_soup.size;
    while (want < g_demand * 4 && want < max_soup) want *= 2;
    Buf nb;
    if (make_buf(nb, want, soup_usage(), false)) {
      log("[water] soup grown to %llu MB (demand %.1f MB per frame)", (unsigned long long)(want >> 20),
          double(g_demand) / 1048576.0);
      g_retired.push_back({g_soup, kOwn + 4});   // still in recorded passes and pending builds
      g_soup = nb;
      g_soup_head = 0;
    }
  }
  g_demand = 0;
  dd->vk.EndCommandBuffer(s.cb);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &s.cb;
  const VkResult r = dd->vk.QueueSubmit(queue, 1, &si, s.fence);   // the caller holds the queue lock
  s.used = r == VK_SUCCESS;
  s.harvest = s.used && built;
  if (r != VK_SUCCESS) log("[water] build submission failed (%d)", r);
  else g_latest_build = frame_index() + 1;
}

void water_on_present() {
  if (!g_dd) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_ready) return;
  for (uint32_t i = 0; i < kOwn; ++i) {
    Slot& s = g_slot[i];
    if (s.harvest && g_dd->vk.WaitForFences(g_dd->device, 1, &s.fence, VK_TRUE, 0) == VK_SUCCESS) harvest(i);
  }
  if (g_pending.size() > 256) g_pending.clear();   // command buffers the game never submitted
  const bool on = cfg_hot(CfgHot::rt_water_geometry) != 0;
  ++g_win.frames;
  const uint64_t every = uint64_t((std::max)(cfg_int("rt_timing_frames", 300), 30));
  if (g_win.frames < every) return;
  Window& w = g_win;
  const Census& c = w.c;
  if (on || c.water() || w.builds) {
    const double f = double(w.frames);
    const uint64_t sup = c.cls[water::kSupported];
    const uint64_t failed = c.skip_xfb + c.skip_indirect + c.skip_topo + c.no_ubo + w.build_failed;
    log("[water] capture (%llu frames): supported draws %llu, unknown %llu, excluded %llu (flow %llu, VFX %llu), "
        "triangles %llu, overflow %llu, failed %llu (xfb %llu, indirect %llu, topology %llu, no camera %llu, build %llu), "
        "no core policy %llu",
        (unsigned long long)w.frames, (unsigned long long)sup, (unsigned long long)c.cls[water::kUnknown],
        (unsigned long long)(c.cls[water::kExcludedFlow] + c.cls[water::kExcludedVfx]),
        (unsigned long long)c.cls[water::kExcludedFlow], (unsigned long long)c.cls[water::kExcludedVfx],
        (unsigned long long)c.tris, (unsigned long long)c.skip_full, (unsigned long long)failed,
        (unsigned long long)c.skip_xfb, (unsigned long long)c.skip_indirect, (unsigned long long)c.skip_topo,
        (unsigned long long)c.no_ubo, (unsigned long long)w.build_failed, (unsigned long long)c.skip_core);
    const double draw_pct = c.water() ? 100.0 * double(c.captured) / double(c.water()) : 0.0;
    const double tri_pct = c.tris_wanted ? 100.0 * double(c.tris) / double(c.tris_wanted) : 0.0;
    log("[water] coverage (%llu frames): water passes %.2f/frame (%.1f draws/frame, %.1f water), captured %llu of %llu "
        "water draws (%.1f%%), triangles %llu of %llu supported (%.1f%%); depth write on %llu, off %llu, dynamic %llu; "
        "sampled draws %llu: non-finite %llu, collapsed %llu, height %.2f..%.2f m from the camera",
        (unsigned long long)w.frames, double(c.passes) / f, c.passes ? double(c.pass_draws) / double(c.passes) : 0.0,
        c.passes ? double(c.water()) / double(c.passes) : 0.0, (unsigned long long)c.captured,
        (unsigned long long)c.water(), draw_pct, (unsigned long long)c.tris, (unsigned long long)c.tris_wanted, tri_pct,
        (unsigned long long)c.zwrite_on, (unsigned long long)c.zwrite_off, (unsigned long long)c.zwrite_dyn,
        (unsigned long long)w.sampled, (unsigned long long)w.nonfinite, (unsigned long long)w.collapsed,
        w.sampled ? double(w.ymin) : 0.0, w.sampled ? double(w.ymax) : 0.0);
    std::vector<float> ms = w.build_ms;
    std::sort(ms.begin(), ms.end());
    const double med = ms.empty() ? 0.0 : double(ms[ms.size() / 2]);
    const double p90 = ms.empty() ? 0.0 : double(ms[(ms.size() * 9) / 10 < ms.size() ? (ms.size() * 9) / 10 : ms.size() - 1]);
    log("[water] build GPU %.3f ms median, p90 %.3f (%zu timed of %llu builds; %.1f geometries, %.0f triangles per build; "
        "busy %llu, failed %llu; soup %llu MB)",
        med, p90, ms.size(), (unsigned long long)w.builds, w.builds ? double(w.geos) / double(w.builds) : 0.0,
        w.builds ? double(w.build_tris) / double(w.builds) : 0.0, (unsigned long long)w.busy,
        (unsigned long long)w.build_failed, (unsigned long long)(g_soup.size >> 20));
    std::string fam, other;
    for (const auto& [k, f] : w.family) {
      if (fam.size() > 1200) break;
      char b[224];
      std::snprintf(b, sizeof b, "%s%016llx %s %llu (Roughness_Base %.3f, Reflectance_Base %.3f, flags 0x%x)",
                    fam.empty() ? "" : "; ", (unsigned long long)water::kAudited[k].fs, water::kAudited[k].name,
                    (unsigned long long)f.n, double(f.rough), double(f.refl), f.flags);
      fam += b;
    }
    static const char* kCls[] = {"-", "supported", "unknown", "excluded flow", "excluded VFX"};
    for (const auto& [k, n] : w.other) {
      if (other.size() > 1200) break;
      auto it = g_mats.find(k.first);
      char b[256];
      std::snprintf(b, sizeof b, "%s%016llx %s %s %llu", other.empty() ? "" : "; ", (unsigned long long)k.first,
                    kCls[k.second < water::kClassCount ? k.second : 0], it != g_mats.end() ? it->second.name.c_str() : "?",
                    (unsigned long long)n);
      other += b;
    }
    if (!fam.empty() || !other.empty())
      log("[water] families: captured %s; counted %s", fam.empty() ? "none" : fam.c_str(), other.empty() ? "none" : other.c_str());
  }
  g_win = Window{};
}

}  // namespace bg3
