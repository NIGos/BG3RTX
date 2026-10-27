// Track D: the static/dynamic BVH split (docs/TRACK_D_RT.md section 11,
// docs/AUDIT_SOTA.md rank 2). rt_bvh_split=1 (live); 0 is the old single soup BLAS.
//
// The old scene was one BLAS over the whole per-frame transform-feedback soup,
// refit while the draw sequence held and rebuilt whenever it changed, i.e. on
// every camera pan (BG3 culls on the CPU, so the draw set changes all the time).
// Here:
//
// Static instanced meshes (class A, TRACK_D 1): the vertex shader places a mesh
// with a per-instance affine matrix whose rows are instance-rate vertex inputs
// (spirv_xfb.h analyze_vs_instancing, geocapture.cpp make_inst_pipe). A mesh is
// keyed by what its per-vertex inputs read: VS + vertex layout, the per-vertex
// vertex buffers and offsets, the index buffer range, the draw's counts and the
// alpha test. The first time a mesh is seen, its draw is captured as before;
// shaders/rt/mesh_extract.hlsl maps the first instance's triangles back to
// object space with the inverse of that instance's matrix (into the mesh pool,
// with the UV for the alpha test), and checks the result against another
// instance of the draw (or, for single-instance draws, against a later frame's
// capture). The mesh's BLAS is built once (PREFER_FAST_TRACE |
// ALLOW_COMPACTION) and compacted. From then on the draw is not captured at all:
// every instance becomes a TLAS instance whose transform is copied from the
// game's own instance stream as the game submits the frame (host-mapped memory:
// exactly what the GPU will read). The cache is evicted when one of the mesh's
// buffers is destroyed or written by a transfer, by LRU over a memory budget,
// and when a periodic audit (a cached draw captured again and compared) fails.
//
// Everything else (skinned, wind/cloth, per-draw matrices, terrain, sky, and
// meshes still in probation) is captured as before and gets a BLAS per draw,
// keyed by the draw's identity, refit while its triangle count holds.
//
// Ray queries find the per-instance data through InstInfo (set 1 binding 7):
// where the instance's vertices are (pool or soup), its stride, and its alpha
// record (shaders/rt/rt_common.hlsli).
#include "host.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <cstdarg>
#include <deque>
#include <unordered_set>

namespace bg3 {

namespace {

constexpr uint32_t kInfoPool = 1u << 8, kInfoLegacy = 1u << 9, kInfoAlpha = 1u << 10;   // rt_common.hlsli
constexpr uint32_t kRecBytes = 208;                // AlphaRec, rt_common.hlsli
constexpr uint32_t kRingSlots = kSceneSlots;       // per-build regions of the host ring (= scene slots, rtscene.cpp)
constexpr uint32_t kStatusSlots = 16384;           // validation results (16 B each)
constexpr uint32_t kCompactSlots = 2048;
constexpr VkDeviceSize kAsAlign = 256;
constexpr uint32_t kNone = UINT32_MAX;

DeviceData* g_dd = nullptr;
bool g_on = false;
bool g_failed = false;
std::mutex g_mu;
std::atomic<bool> g_any_mesh{false};

struct Buf {
  VkBuffer b = VK_NULL_HANDLE;
  VkDeviceMemory m = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  VkDeviceAddress addr = 0;
  uint8_t* map = nullptr;
};

bool make_buf(Buf& out, VkDeviceSize size, VkBufferUsageFlags usage, bool host, const char* what) {
  DeviceData* dd = g_dd;
  out = Buf{};
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  if (dd->vk.CreateBuffer(dd->device, &bci, nullptr, &out.b) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetBufferMemoryRequirements(dd->device, out.b, &req);
  VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
  fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  if (rt_alloc_device_address()) mai.pNext = &fi;
  mai.allocationSize = req.size;
  const VkMemoryPropertyFlags hv = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  mai.memoryTypeIndex = host ? find_memory_type(dd, req.memoryTypeBits, hv | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                             : find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (host && mai.memoryTypeIndex == UINT32_MAX) mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, hv);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.m) != VK_SUCCESS) {
    dd->vk.DestroyBuffer(dd->device, out.b, nullptr);
    out = Buf{};
    return false;
  }
  dd->vk.BindBufferMemory(dd->device, out.b, out.m, 0);
  out.size = size;
  out.addr = rt_buffer_address(out.b);
  fault_track(what, out.addr, size);
  if (host) {
    void* p = nullptr;
    if (dd->vk.MapMemory(dd->device, out.m, 0, VK_WHOLE_SIZE, 0, &p) != VK_SUCCESS) {
      dd->vk.DestroyBuffer(dd->device, out.b, nullptr);
      dd->vk.FreeMemory(dd->device, out.m, nullptr);
      out = Buf{};
      return false;
    }
    out.map = static_cast<uint8_t*>(p);
  }
  return true;
}

void free_buf(Buf& b) {
  fault_untrack(b.addr);
  // FreeMemory unmaps; both wait until no frame in flight can use them.
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

constexpr VkPipelineStageFlags kAS = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
constexpr VkPipelineStageFlags kCS = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags kXFB = VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;
constexpr VkPipelineStageFlags kXfer = VK_PIPELINE_STAGE_TRANSFER_BIT;
constexpr VkAccessFlags kRW = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

uint64_t mix(uint64_t h, uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  return h * 1099511628211ull;
}

// ---- a first-fit range allocator ----
struct RangeAlloc {
  VkDeviceSize cap = 0, used = 0;
  std::map<VkDeviceSize, VkDeviceSize> free;   // offset -> size
  void init(VkDeviceSize c) {
    cap = c;
    used = 0;
    free.clear();
    if (c) free[0] = c;
  }
  bool alloc(VkDeviceSize size, VkDeviceSize align, VkDeviceSize* off) {
    for (auto it = free.begin(); it != free.end(); ++it) {
      VkDeviceSize s0 = it->first, e0 = it->first + it->second, a = (s0 + align - 1) / align * align;
      if (a + size > e0) continue;
      free.erase(it);
      if (a > s0) free[s0] = a - s0;
      if (a + size < e0) free[a + size] = e0 - (a + size);
      *off = a;
      used += size;
      return true;
    }
    return false;
  }
  void release(VkDeviceSize off, VkDeviceSize size) {
    used -= (std::min)(used, size);
    auto next = free.lower_bound(off);
    if (next != free.end() && off + size == next->first) {
      size += next->second;
      next = free.erase(next);
    }
    if (next != free.begin()) {
      auto prev = std::prev(next);
      if (prev->first + prev->second == off) {
        prev->second += size;
        return;
      }
    }
    free[off] = size;
  }
  void grow(VkDeviceSize ncap) {
    if (ncap <= cap) return;
    VkDeviceSize old = cap;
    cap = ncap;
    used += ncap - old;   // release() subtracts it again
    release(old, ncap - old);
  }
};

// ---- acceleration structure storage: suballocated chunks ----
struct AsChunk { Buf buf; RangeAlloc ra; };
std::vector<AsChunk> g_chunks;
struct AsAlloc {
  int chunk = -1;
  VkDeviceSize off = 0, size = 0;
  VkAccelerationStructureKHR as = VK_NULL_HANDLE;
  VkDeviceAddress addr = 0;
};
// chunk -1: the mesh pool. `as`: the BLAS object over the range, destroyed with it
// (a TLAS of a slot in flight may still reference it).
struct PendingFree { uint64_t frame; int chunk; VkDeviceSize off, size; VkAccelerationStructureKHR as = VK_NULL_HANDLE; };
std::vector<PendingFree> g_pending_free;
// rt_as_keep=1 (diagnostic, default 0): a released BLAS's VkAccelerationStructureKHR is kept, not
// destroyed, while the device runs (its range is reused by new AS objects). Every
// fault log of the split had the faulting page within 70 KB of one of our AS
// handles (host heap: the driver's AS objects), and every configuration that faults
// destroys AS objects at run time (compaction, new-range builds, the emissive
// capture), while the legacy build, which never does, never faults. Kept handles
// are destroyed at device destruction; above rt_as_keep_max the oldest go.
std::deque<VkAccelerationStructureKHR> g_kept_as;
uint64_t g_kept_dropped = 0;
void retire_as(VkAccelerationStructureKHR as) {
  if (!as) return;
  if (!cfg_int("rt_as_keep", 0)) {
    defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(as));
    return;
  }
  g_kept_as.push_back(as);
  const size_t cap = size_t((std::max)(cfg_int("rt_as_keep_max", 200000), 1000));
  while (g_kept_as.size() > cap) {
    defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(g_kept_as.front()));
    g_kept_as.pop_front();
    if (g_kept_dropped++ == 0) log("[bvh] more than %zu released BLAS handles kept: the oldest are destroyed", cap);
  }
}

bool as_create(VkDeviceSize size, AsAlloc& out) {
  out = AsAlloc{};
  size = (size + kAsAlign - 1) / kAsAlign * kAsAlign;
  int ci = -1;
  VkDeviceSize off = 0;
  for (size_t i = 0; i < g_chunks.size() && ci < 0; ++i)
    if (g_chunks[i].buf.b && g_chunks[i].ra.alloc(size, kAsAlign, &off)) ci = int(i);
  if (ci < 0) {
    VkDeviceSize cs = (std::max)(VkDeviceSize(cfg_int("rt_mesh_chunk_mb", 64)) << 20, size);
    AsChunk c;
    if (!make_buf(c.buf, cs, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false, "BLAS chunk (mesh/per-draw BLAS)"))
      return false;
    c.ra.init(cs);
    c.ra.alloc(size, kAsAlign, &off);
    g_chunks.push_back(std::move(c));
    ci = int(g_chunks.size() - 1);
  }
  VkAccelerationStructureCreateInfoKHR ci2{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  ci2.buffer = g_chunks[ci].buf.b;
  ci2.offset = off;
  ci2.size = size;
  ci2.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  if (g_dd->vk.CreateAccelerationStructureKHR(g_dd->device, &ci2, nullptr, &out.as) != VK_SUCCESS) {
    g_chunks[ci].ra.release(off, size);
    out = AsAlloc{};
    return false;
  }
  VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
  ai.accelerationStructure = out.as;
  out.addr = g_dd->vk.GetAccelerationStructureDeviceAddressKHR(g_dd->device, &ai);
  out.chunk = ci;
  out.off = off;
  out.size = size;
  return true;
}

void as_destroy(AsAlloc& a) {
  if (a.chunk >= 0) g_pending_free.push_back({frame_index(), a.chunk, a.off, a.size, a.as});
  else if (a.as) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(a.as));
  a = AsAlloc{};
}

VkDeviceSize as_bytes() {
  VkDeviceSize n = 0;
  for (auto& c : g_chunks) n += c.ra.used;
  return n;
}

// ---- the mesh pool: object-space vertices of every cached mesh (one buffer, grown by copy) ----
Buf g_pool;
RangeAlloc g_pool_ra;
bool g_pool_grew = false;

bool pool_alloc(VkCommandBuffer cmd, VkDeviceSize size, VkDeviceSize* off) {
  if (g_pool.b && g_pool_ra.alloc(size, 16, off)) return true;
  VkDeviceSize cap = g_pool.size ? g_pool.size * 2 : VkDeviceSize((std::max)(cfg_int("rt_mesh_pool_mb", 256), 16)) << 20;
  while (cap < g_pool.size + size) cap *= 2;
  VkDeviceSize max_cap = VkDeviceSize((std::max)(cfg_int("rt_mesh_pool_max_mb", 3072), 64)) << 20;
  if (cap > max_cap) return false;
  Buf nb;
  if (!make_buf(nb, cap,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                false, "mesh pool"))
    return false;
  if (g_pool.b) {
    // Earlier frames' extractions wrote it; this frame's come after the copy.
    barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, kXfer, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferCopy r{0, 0, g_pool.size};
    g_dd->vk.CmdCopyBuffer(cmd, g_pool.b, nb.b, 1, &r);
    barrier(cmd, kXfer, VK_ACCESS_TRANSFER_WRITE_BIT, kCS | kAS, kRW);
    free_buf(g_pool);
    log("[bvh] mesh pool grown to %llu MB", (unsigned long long)(cap >> 20));
  }
  g_pool = nb;
  g_pool_ra.grow(cap);
  g_pool_grew = true;
  return g_pool_ra.alloc(size, 16, off);
}

// ---- meshes ----
enum MeshState : uint8_t { kBuilt, kNeedCheck, kReady };
enum Reject : uint8_t { kRejInvalid, kRejMismatch, kRejAudit, kRejCount };
const char* kRejectName[kRejCount] = {"invalid vertices or matrix", "instance mismatch", "audit mismatch"};
struct Range { VkBuffer b; VkDeviceSize lo, hi; };
struct Mesh {
  uint32_t tris = 0;
  bool alpha = false;
  MeshState state = kBuilt;
  std::vector<Range> ranges;          // game buffer ranges the mesh was read from (eviction)
  VkDeviceSize pool_off = 0, pool_size = 0;
  AsAlloc as;
  VkDeviceSize compact_size = 0;
  bool compacted = false;
  bool check_pending = false;
  uint64_t created = 0, last_used = 0, last_audit = 0;
  uint32_t instances_seen = 0;
  uint64_t gen = 0;                   // unique per created mesh: results of an evicted one never apply to a new one
};
uint64_t g_mesh_gen = 0;
std::unordered_map<uint64_t, Mesh> g_meshes;
std::unordered_map<VkBuffer, std::vector<uint64_t>> g_buf_meshes;
struct RejectRec { uint64_t until; uint32_t count; };
std::unordered_map<uint64_t, RejectRec> g_rejected;
uint32_t g_compact_next = 0;

// ---- per-draw BLAS for everything captured ----
struct DynSlot {
  AsAlloc as;
  uint32_t prims = 0, cap_prims = 0;
  bool alpha = false;
  VkBuildAccelerationStructureFlagsKHR flags = 0;
  uint64_t last_used = 0;
  uint32_t since_build = 0;
  VkDeviceSize build_scratch = 0, update_scratch = 0;
  uint64_t rec = 0;             // build record of its last build/refit (refit only once that was submitted)
};
std::unordered_map<uint64_t, DynSlot> g_dyn;

// ---- this capture's draws ----
struct FrameDraw {
  uint64_t mesh_key = 0;      // 0: not instanceable
  uint64_t dyn_key = 0;
  const uint8_t* rows[3] = {};   // instance 0's matrix rows in the game's mapped memory
  uint32_t row_stride[3] = {};
  uint32_t instances = 0, tris_per_instance = 0;
  bool alpha = false;
  bool cached = false, audit = false, extract = false, check = false;
  bool captured = false, cap_alpha = false;
  VkDeviceSize soup_off = 0;
  uint32_t prims = 0;
  uint32_t rec = kNone;
  std::vector<Range> ranges;
  uint64_t geom_key = 0;                // draw_geom_key (EN2: the G-buffer pass's join, hitmat.cpp)
  uint32_t inst_first = 0, inst_count = 0;   // its TLAS instances in this build
};
std::vector<FrameDraw> g_draws;
// EN2: the latest build's draws with their TLAS instances (bvh_join_frame).
std::mutex g_join_mu;
BvhJoinFrame g_join;
std::unordered_map<uint64_t, uint32_t> g_dyn_seen;   // dyn key -> occurrences this capture
uint32_t g_audits_this_frame = 0;

// ---- host ring: TLAS instances, InstInfo, job matrices; written at record, matrices at submit ----
Buf g_ring;
uint32_t g_ring_inst = 0, g_ring_mats = 0;   // capacity per region
VkDeviceSize g_region_bytes = 0, g_info_off = 0, g_mat_off = 0;

struct Fixup {
  uint8_t* dst;
  uint32_t count, dst_stride;
  const uint8_t* src[3];
  uint32_t src_stride[3];
  bool instance;              // a TLAS instance (mask byte set from the result) or a job matrix
  uint8_t sample[48];         // instance 0's rows at record time (late-write diagnostic)
};
std::mutex g_fix_mu;
std::unordered_map<VkCommandBuffer, std::vector<Fixup>> g_fixups;

// ---- build records: was the command buffer a build was recorded into submitted? ----
// The game sometimes records a command buffer and drops it (frees it, or begins it
// again) without submitting it, around level loads. Work of ours recorded there
// never runs: nothing it builds may be used later. Under g_fix_mu.
enum RecState : uint8_t { kRecPending, kRecSubmitted, kRecDropped };
uint64_t g_rec_next = 0;
std::unordered_map<VkCommandBuffer, std::vector<uint64_t>> g_rec_cmd;   // not yet submitted
std::unordered_map<uint64_t, RecState> g_rec_state;
uint64_t g_rec_dropped = 0, g_rec_dropped_logged = 0;

// The last scene-build events, for a device-loss or stall report (bvh_log_state).
std::mutex g_ev_mu;
std::deque<std::string> g_events;
void bvh_event(const char* fmt, ...) {
  char b[320];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof b, fmt, a);
  va_end(a);
  std::lock_guard<std::mutex> l(g_ev_mu);
  g_events.emplace_back(b);
  if (g_events.size() > 20) g_events.pop_front();
}

int rec_state(uint64_t id) {   // -1: unknown (0, or pruned: never trusted)
  std::lock_guard<std::mutex> fl(g_fix_mu);
  auto it = g_rec_state.find(id);
  return it == g_rec_state.end() ? -1 : int(it->second);
}
bool rec_submitted(uint64_t id) { return rec_state(id) == kRecSubmitted; }

// ---- GPU-side bookkeeping ----
Buf g_status;                 // host-visible validation results
uint32_t g_status_next = 0;
VkQueryPool g_compact_qp = VK_NULL_HANDLE;
enum JobKind : uint8_t { kJobExtract, kJobCheck, kJobAudit };
// `gen`: the mesh the job or query was recorded for. A mesh evicted and created
// again under the same key (its buffer rewritten) is another mesh: an old
// batch's compacted size must not size the new one's compaction (a copy into a
// too small range overwrites the neighbouring BLAS in the chunk).
struct Job { uint64_t key, gen; uint32_t status; JobKind kind; bool cross_instance; };
struct CompactRead { uint64_t key, gen; uint32_t slot; };
// A compaction copy: the mesh switches to `c` only once the batch's done word shows
// the copy executed (a dropped command buffer never wrote it).
struct Swap { uint64_t key, gen; AsAlloc c; };
struct JobBatch {
  uint64_t frame;
  uint32_t done_slot;
  std::vector<Job> jobs;
  std::vector<CompactRead> compact;
  std::vector<Swap> swaps;
  uint64_t rec = 0;   // build record of the command buffer (dropped: the batch is lost at once)
};
constexpr uint32_t kDoneMagic = 0x42564844u;   // the batch's done word
std::vector<JobBatch> g_batches;

// TLAS: one per scene slot (rtscene.cpp), so a pass that traces an older
// build's TLAS still has that build's InstInfo, and no build rewrites a TLAS
// the GPU may still be tracing. The refit reads the previous slot's TLAS
// (src) and writes this slot's (dst).
Buf g_scratch;
struct TlasSlot { Buf buf; VkAccelerationStructureKHR as = VK_NULL_HANDLE; uint32_t cap = 0; };
TlasSlot g_tlas[kSceneSlots];
uint32_t g_tlas_cap_hint = 0;   // instances the newest TLAS storage was sized for
int g_tlas_prev = -1;           // slot of the last TLAS build (the refit source)
uint64_t g_tlas_prev_rec = 0;   // its build record (refit only once submitted)
uint32_t g_slot_n[kSceneSlots] = {};   // instances each slot's ring region holds (fault reports)
uint64_t g_slot_frame[kSceneSlots] = {};
constexpr VkBuildAccelerationStructureFlagsKHR kTlasFlags =
    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
// The TLAS is refit while its instance list (count, BLAS and flags in order) holds:
// only transforms (the camera-relative matrices) and masks change then.
uint64_t g_tlas_sig = 0;
uint32_t g_tlas_n = 0, g_tlas_since = 0;
bool g_tlas_valid = false;

// Extraction pipeline (shaders/rt/mesh_extract.hlsl), push descriptors.
VkDescriptorSetLayout g_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_pl = VK_NULL_HANDLE;
VkPipeline g_pipe = VK_NULL_HANDLE;
uint64_t g_pipe_gen = 0;

// ---- statistics (logged every rt_timing_frames builds) ----
struct Stats {
  uint64_t builds = 0, draws = 0, cached_draws = 0, static_inst = 0, dyn_draws = 0, static_tris = 0, dyn_tris = 0;
  uint64_t new_meshes = 0, new_tris = 0, ready = 0, rejected[kRejCount] = {}, checks_ok = 0, audits_ok = 0, lost = 0;
  uint64_t lost_magic = 0, lost_timeout = 0, lost_logged = 0;
  uint64_t evict_destroy = 0, evict_write = 0, evict_lru = 0, compactions = 0, compact_saved = 0;
  uint64_t dyn_refit = 0, dyn_rebuild = 0, dyn_new = 0, budget_deferred = 0, tlas_refit = 0;
  uint64_t not_inst_pattern = 0, not_inst_stream = 0, not_inst_count = 0;
  uint64_t fix_applied = 0, fix_late = 0, fix_bad = 0, fix_stale = 0;
  float err_max_ok = 0;
};
Stats g_st;
std::mutex g_st_mu;   // fixup counters (submit thread)

bool ensure_pipeline() {
  Bg3ShaderBlob blob{};
  if (!shader_get("rt/mesh_extract.hlsl", "main", "cs_6_6", &blob)) return g_pipe != VK_NULL_HANDLE;
  if (g_pipe && blob.generation == g_pipe_gen) return true;
  DeviceData* dd = g_dd;
  if (!g_dsl) {
    VkDescriptorSetLayoutBinding b[4];
    for (uint32_t i = 0; i < 4; ++i) b[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dci.bindingCount = 4;
    dci.pBindings = b;
    if (dd->vk.CreateDescriptorSetLayout(dd->device, &dci, nullptr, &g_dsl) != VK_SUCCESS) return false;
    VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, 64};
    VkPipelineLayoutCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pci.setLayoutCount = 1;
    pci.pSetLayouts = &g_dsl;
    pci.pushConstantRangeCount = 1;
    pci.pPushConstantRanges = &pc;
    if (dd->vk.CreatePipelineLayout(dd->device, &pci, nullptr, &g_pl) != VK_SUCCESS) return false;
  }
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  VkShaderModule mod = VK_NULL_HANDLE;
  if (dd->vk.CreateShaderModule(dd->device, &mci, nullptr, &mod) != VK_SUCCESS) return g_pipe != VK_NULL_HANDLE;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main"};
  ci.layout = g_pl;
  VkPipeline p = VK_NULL_HANDLE;
  VkResult r = dd->vk.CreateComputePipelines(dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &p);
  dd->vk.DestroyShaderModule(dd->device, mod, nullptr);
  if (r != VK_SUCCESS) {
    log("[bvh] mesh_extract pipeline creation failed (%d)", r);
    return g_pipe != VK_NULL_HANDLE;
  }
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_pipe));
  g_pipe = p;
  g_pipe_gen = blob.generation;
  log("[bvh] mesh_extract pipeline generation %llu", (unsigned long long)blob.generation);
  return true;
}

bool ensure_ring(uint32_t inst, uint32_t mats) {
  if (g_ring.b && inst <= g_ring_inst && mats <= g_ring_mats) return true;
  uint32_t ni = (std::max)({inst + inst / 2, g_ring_inst, 4096u}), nm = (std::max)({mats + mats / 2, g_ring_mats, 1024u});
  VkDeviceSize ib = VkDeviceSize(ni) * 64, fb = VkDeviceSize(ni) * 16, mb = VkDeviceSize(nm) * 48;
  auto up = [](VkDeviceSize v) { return (v + 255) / 256 * 256; };
  VkDeviceSize region = up(ib) + up(fb) + up(mb);
  Buf nb;
  if (!make_buf(nb, region * kRingSlots,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                true, "instance/InstInfo ring"))
    return false;
  if (g_ring.b) free_buf(g_ring);   // slots in flight keep the old one until it is freed (deferred)
  g_ring = nb;
  g_ring_inst = ni;
  g_ring_mats = nm;
  g_region_bytes = region;
  g_info_off = up(ib);
  g_mat_off = up(ib) + up(fb);
  log("[bvh] instance ring: %u instances, %u job matrices per build (%llu KB x %u, host-visible)", ni, nm,
      (unsigned long long)(region >> 10), kRingSlots);
  return true;
}

bool ensure_tlas(uint32_t slot, uint32_t n) {
  TlasSlot& ts = g_tlas[slot];
  // Every slot is grown to the newest size, so consecutive slots match and the
  // refit (previous slot into this one) keeps working after a growth.
  if (ts.as && n <= ts.cap && ts.cap >= g_tlas_cap_hint) return true;
  uint32_t cap = (std::max)({n + n / 2, 4096u, g_tlas_cap_hint});
  VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  bi.flags = kTlasFlags;
  bi.geometryCount = 1;
  bi.pGeometries = &g;
  VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  g_dd->vk.GetAccelerationStructureBuildSizesKHR(g_dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &cap, &sz);
  Buf nb;
  if (!make_buf(nb, sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false, "split TLAS slot"))
    return false;
  VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  ci.buffer = nb.b;
  ci.size = sz.accelerationStructureSize;
  ci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  VkAccelerationStructureKHR t = VK_NULL_HANDLE;
  if (g_dd->vk.CreateAccelerationStructureKHR(g_dd->device, &ci, nullptr, &t) != VK_SUCCESS) {
    free_buf(nb);
    return false;
  }
  // The slot is retired (rtscene.cpp): nothing in flight reads its old TLAS.
  if (ts.as) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(ts.as));
  if (ts.buf.b) free_buf(ts.buf);
  ts.as = t;
  ts.buf = nb;
  ts.cap = cap;
  g_tlas_cap_hint = cap;
  if (int(slot) == g_tlas_prev) g_tlas_valid = false;
  log("[bvh] TLAS storage of slot %u for %u instances: %llu KB", slot, cap,
      (unsigned long long)(sz.accelerationStructureSize >> 10));
  return true;
}

bool ensure_scratch(VkDeviceSize need) {
  need += rt_scratch_alignment();
  if (g_scratch.b && g_scratch.size >= need) return true;
  if (g_scratch.b) free_buf(g_scratch);
  return make_buf(g_scratch, need + need / 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "split AS scratch");
}

VkDeviceAddress scratch_at(VkDeviceSize off) {
  VkDeviceSize al = rt_scratch_alignment();
  return ((g_scratch.addr + al - 1) & ~(al - 1)) + off;
}

VkAccelerationStructureGeometryKHR tri_geometry(VkDeviceAddress verts, uint32_t prims, uint32_t stride, bool opaque) {
  VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  g.flags = opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
  auto& t = g.geometry.triangles;
  t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
  t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
  t.vertexData.deviceAddress = verts;
  t.vertexStride = stride;
  t.maxVertex = prims * 3 ? prims * 3 - 1 : 0;
  t.indexType = VK_INDEX_TYPE_NONE_KHR;
  return g;
}

VkAccelerationStructureBuildSizesInfoKHR blas_sizes(uint32_t prims, uint32_t stride, bool opaque,
                                                    VkBuildAccelerationStructureFlagsKHR flags) {
  VkAccelerationStructureGeometryKHR g = tri_geometry(0, prims, stride, opaque);
  VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  bi.flags = flags;
  bi.geometryCount = 1;
  bi.pGeometries = &g;
  VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  g_dd->vk.GetAccelerationStructureBuildSizesKHR(g_dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &prims, &sz);
  return sz;
}

void unindex(uint64_t key, const std::vector<Range>& ranges) {
  for (const Range& r : ranges) {
    auto it = g_buf_meshes.find(r.b);
    if (it == g_buf_meshes.end()) continue;
    auto& v = it->second;
    v.erase(std::remove(v.begin(), v.end(), key), v.end());
    if (v.empty()) g_buf_meshes.erase(it);
  }
}

void evict(uint64_t key) {
  auto it = g_meshes.find(key);
  if (it == g_meshes.end()) return;
  Mesh& m = it->second;
  unindex(key, m.ranges);
  as_destroy(m.as);
  if (m.pool_size) g_pending_free.push_back({frame_index(), -1, m.pool_off, m.pool_size});
  g_meshes.erase(it);
  g_any_mesh = !g_meshes.empty();
}

void reject(uint64_t key, Reject why) {
  evict(key);
  RejectRec& r = g_rejected[key];
  ++r.count;
  // Retried later (the cause may be transient: a vertex parked at NaN, a buffer
  // rewritten), with a backoff that doubles up to about 20 minutes at 120 fps.
  r.until = frame_index() + (uint64_t(600) << (std::min)(r.count - 1, 8u));
  ++g_st.rejected[why];
}

uint32_t status_slots(uint32_t n) {
  uint32_t s = g_status_next;
  if (s + n > kStatusSlots) s = 0;
  g_status_next = s + n;
  std::memset(g_status.map + VkDeviceSize(s) * 16, 0, size_t(n) * 16);
  return s;
}

}  // namespace

// ================= entry points =================

void bvh_device_created(DeviceData* dd) {
  if (g_dd) return;
  g_dd = dd;
  bool ok = dd->vk.CmdCopyAccelerationStructureKHR && dd->vk.CmdWriteAccelerationStructuresPropertiesKHR &&
            dd->vk.CmdCopyBuffer && dd->vk.CmdCopyQueryPoolResults && dd->vk.CmdFillBuffer;
  ok = ok && make_buf(g_status, VkDeviceSize(kStatusSlots) * 16,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "split status");
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
  qi.queryCount = kCompactSlots;
  ok = ok && dd->vk.CreateQueryPool(dd->device, &qi, nullptr, &g_compact_qp) == VK_SUCCESS;
  g_on = ok;
  log("[bvh] static/dynamic BVH split %s (rt_bvh_split=%d)", ok ? "available" : "UNAVAILABLE", cfg_int("rt_bvh_split", 1));
}

void bvh_device_destroyed(DeviceData* dd) {
  if (dd != g_dd) return;
  std::lock_guard<std::mutex> lock(g_mu);
  for (auto& [k, m] : g_meshes) as_destroy(m.as);
  for (auto& [k, s] : g_dyn) as_destroy(s.as);
  for (JobBatch& b : g_batches)
    for (Swap& s : b.swaps) as_destroy(s.c);
  g_batches.clear();
  for (const PendingFree& p : g_pending_free)
    if (p.as) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(p.as));
  g_pending_free.clear();
  for (VkAccelerationStructureKHR a : g_kept_as) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(a));
  g_kept_as.clear();
  g_meshes.clear();
  g_dyn.clear();
  g_buf_meshes.clear();
  g_any_mesh = false;
  {
    std::lock_guard<std::mutex> jl(g_join_mu);
    g_join = BvhJoinFrame{};
  }
  for (auto& c : g_chunks)
    if (c.buf.b) free_buf(c.buf);
  g_chunks.clear();
  for (Buf* b : {&g_pool, &g_ring, &g_status, &g_scratch})
    if (b->b) free_buf(*b);
  for (TlasSlot& ts : g_tlas) {
    if (ts.as) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(ts.as));
    if (ts.buf.b) free_buf(ts.buf);
    ts = TlasSlot{};
  }
  g_tlas_prev = -1;
  g_tlas_valid = false;
  defer_destroy(BG3_DEFER_QUERY_POOL, uint64_t(g_compact_qp));
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_pipe));
  defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, uint64_t(g_pl));
  defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, uint64_t(g_dsl));
  g_compact_qp = VK_NULL_HANDLE;
  g_pipe = VK_NULL_HANDLE;
  g_pl = VK_NULL_HANDLE;
  g_dsl = VK_NULL_HANDLE;
  g_on = false;
  g_dd = nullptr;
}

bool bvh_split_wanted() { return g_on && !g_failed && cfg_int("rt_bvh_split", 1) != 0; }

void bvh_begin_capture(VkCommandBuffer) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_draws.clear();
  g_dyn_seen.clear();
  g_audits_this_frame = 0;
}

BvhVerdict bvh_classify(VkCommandBuffer, const BvhDraw& d, uint32_t* token, uint32_t* mesh_tris) {
  std::lock_guard<std::mutex> lock(g_mu);
  *token = uint32_t(g_draws.size());
  g_draws.emplace_back();
  FrameDraw& fd = g_draws.back();
  fd.alpha = d.alpha;
  fd.instances = d.args.instances;
  const CmdState* s = d.s;
  fd.geom_key = draw_geom_key(s, d.args);
  const bool indexed = d.args.kind == 1;
  // The draw's identity across frames (per-draw BLAS of captured draws). The
  // instance-rate bindings change offset every frame (the game's upload ring),
  // so only binding 0 (positions in every BG3 layout) and the index range count.
  uint64_t dk = mix(mix(mix(uint64_t(d.pipe_id), d.args.count), d.args.first), uint64_t(int64_t(d.args.vertex_offset)));
  dk = mix(mix(dk, uint64_t(s->vb[0].buffer)), s->vb[0].offset);
  if (indexed) dk = mix(mix(dk, uint64_t(s->ib)), s->ib_offset);
  dk = mix(dk, d.alpha);
  fd.dyn_key = mix(dk, g_dyn_seen[dk]++);   // the n-th draw with that identity this frame
  if (!d.inst || !d.inst->ok) {
    ++g_st.not_inst_pattern;
    return BVH_CAPTURE;
  }
  const InstPipe& ip = *d.inst;
  if (!d.args.instances || d.args.count < 3 || d.args.count % 3) {
    ++g_st.not_inst_count;
    return BVH_CAPTURE;
  }
  // The instance stream: host-mapped, in range.
  for (int k = 0; k < 3; ++k) {
    const CmdState::VertexBinding& vb = s->vb[ip.row_binding[k]];
    uint32_t stride = ip.row_stride[k] ? ip.row_stride[k] : uint32_t(vb.stride);
    VkDeviceSize size = vb.buffer ? geo_buffer_size(vb.buffer) : 0;
    VkDeviceSize first = vb.offset + VkDeviceSize(d.args.first_instance) * stride + ip.row_offset[k];
    VkDeviceSize last = first + VkDeviceSize(d.args.instances - 1) * stride + 16;
    const uint8_t* p = vb.buffer && stride && size && last <= size ? mapped_ptr(vb.buffer, first) : nullptr;
    if (!p) {
      ++g_st.not_inst_stream;
      return BVH_CAPTURE;
    }
    fd.rows[k] = p;
    fd.row_stride[k] = stride;
  }
  // The mesh: what the per-vertex inputs read.
  uint64_t mk = mix(mix(ip.vtx_sig, d.alpha), indexed);
  mk = mix(mix(mix(mk, d.args.count), d.args.first), uint64_t(int64_t(d.args.vertex_offset)));
  for (uint32_t b = 0; b < CmdState::kMaxVertexBindings; ++b) {
    if (!(ip.vtx_bindings & (1u << b))) continue;
    const CmdState::VertexBinding& vb = s->vb[b];
    if (!vb.buffer) {
      ++g_st.not_inst_stream;
      return BVH_CAPTURE;
    }
    mk = mix(mix(mix(mk, uint64_t(vb.buffer)), vb.offset), vb.stride);
    // The vertices this draw can read (indexed: a mesh references at most as many
    // vertices as it has indices, from vertexOffset on).
    VkDeviceSize vs = vb.stride ? vb.stride : 16;
    VkDeviceSize lo = vb.offset + VkDeviceSize(indexed ? (std::max)(d.args.vertex_offset, 0) : int32_t(d.args.first)) * vs;
    fd.ranges.push_back({vb.buffer, lo, lo + VkDeviceSize(d.args.count) * (std::max)(vs, VkDeviceSize(64))});
  }
  if (indexed) {
    if (!s->ib) {
      ++g_st.not_inst_stream;
      return BVH_CAPTURE;
    }
    VkDeviceSize isz = s->ib_type == VK_INDEX_TYPE_UINT32 ? 4 : s->ib_type == VK_INDEX_TYPE_UINT8_EXT ? 1 : 2;
    mk = mix(mix(mix(mk, uint64_t(s->ib)), s->ib_offset), s->ib_type);
    VkDeviceSize lo = s->ib_offset + VkDeviceSize(d.args.first) * isz;
    fd.ranges.push_back({s->ib, lo, lo + VkDeviceSize(d.args.count) * isz});
  }
  if (!mk) mk = 1;
  fd.mesh_key = mk;
  fd.tris_per_instance = d.args.count / 3;
  auto it = g_meshes.find(mk);
  if (it == g_meshes.end()) {
    auto rj = g_rejected.find(mk);
    if (rj == g_rejected.end() || frame_index() >= rj->second.until) fd.extract = true;
    return BVH_CAPTURE;
  }
  Mesh& m = it->second;
  if (m.tris != fd.tris_per_instance) return BVH_CAPTURE;   // cannot happen for one key; be safe
  if (m.state == kNeedCheck && !m.check_pending) fd.check = true;
  if (m.state != kReady) return BVH_CAPTURE;
  fd.cached = true;
  *mesh_tris = m.tris;
  // A periodic audit: the cached draw is captured once more and compared.
  const int audit_frames = cfg_int("rt_mesh_audit_frames", 600);
  if (audit_frames > 0 && frame_index() >= m.last_audit + uint64_t(audit_frames) &&
      g_audits_this_frame < uint32_t((std::max)(cfg_int("rt_mesh_audits", 2), 0))) {
    ++g_audits_this_frame;
    m.last_audit = frame_index();
    fd.audit = true;
    return BVH_CACHED_AUDIT;
  }
  return BVH_CACHED;
}

void bvh_set_record(uint32_t token, uint32_t rec) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (token < g_draws.size()) g_draws[token].rec = rec;
}

void bvh_captured(uint32_t token, VkDeviceSize offset, uint32_t prims, bool alpha, uint32_t rec) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (token >= g_draws.size()) return;
  FrameDraw& fd = g_draws[token];
  fd.captured = true;
  fd.soup_off = offset;
  fd.prims = prims;
  fd.cap_alpha = alpha;
  if (!fd.cached) fd.rec = alpha ? rec : kNone;
}

void bvh_note_skipped(uint32_t token) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (token < g_draws.size()) g_draws[token].captured = false;
}

// After the RT source pass, outside it, in the game's command buffer.
bool bvh_build(VkCommandBuffer cmd, const RtCapture& cap, void (*stamp)(VkCommandBuffer, uint32_t), uint32_t slot,
               BvhResult* out) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_on || g_failed || slot >= kSceneSlots || !ensure_pipeline()) return false;
  const DeviceData* dd = g_dd;
  const uint64_t now = frame_index();
  const uint32_t stride = cap.stride, tri_bytes = 3 * stride;
  const VkDeviceAddress soup_addr = rt_buffer_address(cap.xfb);
  const bool alpha_test = cfg_int("rt_alpha_test", 1) != 0;
  const int rebuild_frames = (std::max)(cfg_int("rt_rebuild_frames", 60), 1);
  // This command buffer's build record (bvh_on_submit marks it submitted).
  const uint64_t rec = bvh_rec_register(cmd);
  const VkBuildAccelerationStructureFlagsKHR dyn_flags =
      (cfg_int("rt_fast_trace", 0) ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                                   : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR) |
      VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
  const VkBuildAccelerationStructureFlagsKHR mesh_flags =
      VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
      (cfg_int("rt_mesh_compact", 1) ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR : 0);
  const uint64_t new_budget = uint64_t((std::max)(cfg_int("rt_mesh_new_tris", 4000000), 1000));
  g_pool_grew = false;

  // ---- 1. decide: new meshes, checks, per-draw BLAS, instances ----
  struct Ext { uint64_t key; VkDeviceSize soup, soup_n, pool; uint32_t tris, mat0, matn, status; JobKind kind; uint32_t draw; };
  std::vector<Ext> ext;
  struct MeshBuild { uint64_t key; };
  std::vector<MeshBuild> mesh_builds;
  struct DynBuild { DynSlot* slot; VkDeviceSize soup_off; uint32_t prims; bool refit; };
  std::vector<DynBuild> dyn_builds;
  struct Inst { uint32_t draw; uint64_t key; };      // static: draw index (instances follow); dynamic: slot key
  std::vector<uint32_t> cached;   // draw indices
  std::vector<std::pair<uint32_t, uint64_t>> dyn;   // draw index, slot key
  uint32_t n_mats = 0;
  uint64_t new_tris = 0;
  std::unordered_set<uint64_t> new_keys;
  for (uint32_t i = 0; i < g_draws.size(); ++i) {
    FrameDraw& fd = g_draws[i];
    ++g_st.draws;
    if (fd.cached) {
      auto it = g_meshes.find(fd.mesh_key);
      if (it == g_meshes.end() || it->second.state != kReady) continue;   // evicted since: not in this frame
      it->second.last_used = now;
      cached.push_back(i);
      if (fd.audit && fd.captured && fd.prims >= fd.tris_per_instance) {
        ext.push_back({fd.mesh_key, fd.soup_off, 0, it->second.pool_off, it->second.tris, n_mats++, kNone, 0, kJobAudit, i});
      }
      continue;
    }
    if (!fd.captured || !fd.prims) continue;
    dyn.push_back({i, 0});
    if (!fd.mesh_key || fd.prims != fd.tris_per_instance * fd.instances) continue;
    if (fd.extract && !new_keys.count(fd.mesh_key) && !g_meshes.count(fd.mesh_key)) {
      if (new_tris + fd.tris_per_instance > new_budget || new_keys.size() >= 1024) {
        ++g_st.budget_deferred;
        continue;
      }
      // A new mesh: its storage, its extraction from instance 0, checked against the last instance.
      Mesh m;
      m.gen = ++g_mesh_gen;
      m.tris = fd.tris_per_instance;
      m.alpha = fd.alpha;
      m.created = m.last_used = m.last_audit = now;
      m.pool_size = VkDeviceSize(m.tris) * tri_bytes;
      VkAccelerationStructureBuildSizesInfoKHR sz = blas_sizes(m.tris, stride, !m.alpha, mesh_flags);
      if (!pool_alloc(cmd, m.pool_size, &m.pool_off)) continue;
      if (!as_create(sz.accelerationStructureSize, m.as)) {
        g_pending_free.push_back({now, -1, m.pool_off, m.pool_size});
        continue;
      }
      m.ranges = fd.ranges;
      for (const Range& r : m.ranges) g_buf_meshes[r.b].push_back(fd.mesh_key);
      g_meshes[fd.mesh_key] = std::move(m);
      g_any_mesh = true;
      new_keys.insert(fd.mesh_key);
      new_tris += fd.tris_per_instance;
      mesh_builds.push_back({fd.mesh_key});
      const Mesh& mm = g_meshes[fd.mesh_key];
      bool cross = fd.instances >= 2;
      ext.push_back({fd.mesh_key, fd.soup_off,
                     cross ? fd.soup_off + VkDeviceSize(fd.instances - 1) * fd.tris_per_instance * tri_bytes : 0,
                     mm.pool_off, mm.tris, n_mats, cross ? n_mats + 1 : kNone, 0, kJobExtract, i});
      n_mats += cross ? 2 : 1;
      ++g_st.new_meshes;
      g_st.new_tris += fd.tris_per_instance;
    } else if (fd.check) {
      auto it = g_meshes.find(fd.mesh_key);
      if (it != g_meshes.end() && it->second.state == kNeedCheck && !it->second.check_pending) {
        it->second.check_pending = true;
        ext.push_back({fd.mesh_key, fd.soup_off, 0, it->second.pool_off, it->second.tris, n_mats++, kNone, 0, kJobCheck, i});
      }
    }
  }
  // Instance count: every instance of a cached draw, one per captured draw.
  uint32_t n_static = 0;
  for (uint32_t i : cached) n_static += g_draws[i].instances;
  const uint32_t n_inst = n_static + uint32_t(dyn.size());
  if (!ensure_ring(n_inst, n_mats) || !ensure_tlas(slot, (std::max)(n_inst, 1u))) {
    g_failed = true;
    log("[bvh] allocation failed (%u instances); the split is off, the legacy build takes over", n_inst);
    return false;
  }
  // This build's region of the host ring: the scene slot's, retired on the GPU
  // (rtscene.cpp pick_scene_slot), so no frame in flight still reads it.
  const uint32_t region = slot;
  const VkDeviceSize rbase = VkDeviceSize(region) * g_region_bytes;
  auto* inst_out = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(g_ring.map + rbase);
  auto* info_out = reinterpret_cast<uint32_t*>(g_ring.map + rbase + g_info_off);
  uint8_t* mat_out = g_ring.map + rbase + g_mat_off;

  // Per-draw BLAS of the captured draws: refit while the triangle count holds.
  // One build per slot and frame: two builds into one BLAS in one call would race.
  std::unordered_set<uint64_t> dyn_this_frame;
  const bool inplace_ok = cfg_int("rt_dyn_inplace", 0) != 0;
  // A/B: rt_dyn_refit=0 never updates a per-draw BLAS (a full build into a new range
  // every frame); rt_build_serial=1 issues one build command per BLAS with a barrier
  // between them instead of one batched call.
  const bool dyn_refit_ok = cfg_int("rt_dyn_refit", 1) != 0;
  for (auto& [di, key] : dyn) {
    FrameDraw& fd = g_draws[di];
    key = fd.dyn_key;
    if (!dyn_this_frame.insert(key).second) {
      key = 0;
      continue;
    }
    DynSlot& s = g_dyn[key];
    bool fresh = !s.as.as;
    // A refit (UPDATE) needs the source built: its last build must have been submitted
    // (a dropped command buffer left the BLAS unbuilt; a full build does not read it).
    bool refit = dyn_refit_ok && !fresh && s.prims == fd.prims && s.alpha == fd.cap_alpha && s.flags == dyn_flags &&
                 rec_submitted(s.rec) &&
                 s.since_build < uint32_t(rebuild_frames);
    // A full build never goes into the BLAS that TLASes in flight reference: a
    // rebuild changes the BLAS's internal layout, and a TLAS built over the old one
    // (an older scene slot still traced, or a TLAS *refit* whose signature, i.e.
    // the BLAS address, did not change) would then follow stale node offsets: wild
    // reads (fault_3f5fb8b_run1: read at a computed address after a burst of full
    // per-draw rebuilds; d33fca6 run 1: TLAS refits right after 9 full rebuilds).
    // A new range gives the BLAS a new address, so the TLAS signature changes and
    // the TLAS is rebuilt; the old range is released once no live slot references it.
    // Refits (UPDATE) keep the layout and stay in place. rt_dyn_inplace=1: old behaviour.
    if (!refit && (fresh || !inplace_ok || fd.prims > s.cap_prims || s.alpha != fd.cap_alpha || s.flags != dyn_flags)) {
      if (s.as.as) as_destroy(s.as);
      uint32_t cap_prims = fd.prims + fd.prims / 4;
      VkAccelerationStructureBuildSizesInfoKHR sz = blas_sizes(cap_prims, stride, !fd.cap_alpha, dyn_flags);
      if (!as_create(sz.accelerationStructureSize, s.as)) {
        g_dyn.erase(key);
        key = 0;
        continue;
      }
      s.cap_prims = cap_prims;
      s.alpha = fd.cap_alpha;
      s.flags = dyn_flags;
      ++g_st.dyn_new;
    }
    if (!refit || s.prims != fd.prims) {
      VkAccelerationStructureBuildSizesInfoKHR sz = blas_sizes(fd.prims, stride, !fd.cap_alpha, dyn_flags);
      s.build_scratch = sz.buildScratchSize;
      s.update_scratch = sz.updateScratchSize;
    }
    s.prims = fd.prims;
    s.last_used = now;
    s.since_build = refit ? s.since_build + 1 : 0;
    // Staggered forced rebuilds: a slot's first rebuild comes at a key-dependent frame.
    if (!refit && fresh) s.since_build = uint32_t(key % uint64_t(rebuild_frames));
    dyn_builds.push_back({&s, fd.soup_off, fd.prims, refit});
    s.rec = rec;
    refit ? ++g_st.dyn_refit : ++g_st.dyn_rebuild;
  }

  // ---- 2. TLAS instances and InstInfo (static matrices at submit) ----
  std::vector<Fixup> fix;
  uint32_t n = 0;
  uint64_t static_tris = 0, dyn_tris = 0;
  for (uint32_t di : cached) {
    const FrameDraw& fd = g_draws[di];
    const Mesh& m = g_meshes[fd.mesh_key];
    const bool alpha = fd.alpha && fd.rec != kNone;
    Fixup f{reinterpret_cast<uint8_t*>(inst_out + n), fd.instances, uint32_t(sizeof(VkAccelerationStructureInstanceKHR)),
            {fd.rows[0], fd.rows[1], fd.rows[2]}, {fd.row_stride[0], fd.row_stride[1], fd.row_stride[2]}, true, {}};
    for (int k = 0; k < 3; ++k) std::memcpy(f.sample + 16 * k, fd.rows[k], 16);
    fix.push_back(f);
    g_draws[di].inst_first = n;
    g_draws[di].inst_count = fd.instances;
    for (uint32_t i = 0; i < fd.instances; ++i, ++n) {
      VkAccelerationStructureInstanceKHR& in = inst_out[n];
      std::memset(&in.transform, 0, sizeof in.transform);
      in.instanceCustomIndex = n;
      in.mask = 0;   // enabled at submit, with a finite matrix
      in.instanceShaderBindingTableRecordOffset = 0;
      in.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
                 (alpha && alpha_test ? 0u : uint32_t(VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR));
      in.accelerationStructureReference = m.as.addr;
      uint32_t* inf = info_out + n * 4;
      inf[0] = uint32_t(m.pool_off);
      inf[1] = (stride & 0xffu) | kInfoPool | (alpha ? kInfoAlpha : 0u);
      inf[2] = alpha ? 16 + fd.rec * kRecBytes : 0;
      inf[3] = m.tris;
    }
    static_tris += uint64_t(m.tris) * fd.instances;
    ++g_st.cached_draws;
    g_st.static_inst += fd.instances;
  }
  const uint32_t n_static_written = n;
  for (auto& [di, key] : dyn) {
    if (!key) continue;
    FrameDraw& fd = g_draws[di];
    const DynSlot& s = g_dyn[key];
    const bool alpha = fd.cap_alpha && fd.rec != kNone;
    fd.inst_first = n;
    fd.inst_count = 1;
    VkAccelerationStructureInstanceKHR& in = inst_out[n];
    std::memset(&in.transform, 0, sizeof in.transform);
    in.transform.matrix[0][0] = in.transform.matrix[1][1] = in.transform.matrix[2][2] = 1.0f;
    in.instanceCustomIndex = n;
    in.mask = 0xFF;
    in.instanceShaderBindingTableRecordOffset = 0;
    in.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
               (alpha && alpha_test ? 0u : uint32_t(VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR));
    in.accelerationStructureReference = s.as.addr;
    uint32_t* inf = info_out + n * 4;
    inf[0] = uint32_t(fd.soup_off);
    inf[1] = (stride & 0xffu) | (alpha ? kInfoAlpha : 0u);
    inf[2] = alpha ? 16 + fd.rec * kRecBytes : 0;
    inf[3] = fd.prims;
    ++n;
    dyn_tris += fd.prims;
    ++g_st.dyn_draws;
  }
  g_st.static_tris += static_tris;
  g_st.dyn_tris += dyn_tris;
  // Job matrices: instance 0 of the job's draw (and its last instance for a
  // cross-instance check), read at submit like the TLAS instances.
  for (const Ext& e : ext) {
    const FrameDraw& fd = g_draws[e.draw];
    fix.push_back(Fixup{mat_out + VkDeviceSize(e.mat0) * 48, 1, 48, {fd.rows[0], fd.rows[1], fd.rows[2]}, {0, 0, 0}, false, {}});
    if (e.matn != kNone) {
      Fixup g{mat_out + VkDeviceSize(e.matn) * 48, 1, 48, {}, {0, 0, 0}, false, {}};
      for (int k = 0; k < 3; ++k) g.src[k] = fd.rows[k] + VkDeviceSize(fd.instances - 1) * fd.row_stride[k];
      fix.push_back(g);
    }
  }
  {
    std::lock_guard<std::mutex> fl(g_fix_mu);
    auto& v = g_fixups[cmd];
    if (!v.empty()) ++g_st.fix_stale;   // recorded again before it was submitted
    v = std::move(fix);
  }

  // ---- 3. GPU: convert, extract/check, compaction, builds, TLAS ----
  // XFB writes done; last frame's ray queries and builds are done with what we reuse.
  // (HOST: the instance/InstInfo writes made at record; vkQueueSubmit already makes
  // them visible, the explicit dependency costs nothing.)
  barrier(cmd, kXFB | kAS | kCS | kXfer | VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT,
          kCS | kAS | kXfer, kRW);
  fault_checkpoint(cmd, kCkBvhStart, uint32_t(rec));
  rt_convert(cmd, cap);
  // The in-place conversion before everything that reads the soup: the extraction
  // jobs (compute) AND the per-draw BLAS builds (AS build stage). The AS stage was
  // missing here: the only compute -> AS barrier came after the extraction jobs, so
  // in a frame without jobs (no new mesh, check or audit) the per-draw builds raced
  // the conversion and could read clip-space data, with the game's NaN-parked
  // vertices: an undefined build, a corrupt BLAS, wild reads in traversal (the
  // f9fac2a faults: per-draw builds only). The legacy build has always had
  // kCS -> kAS here (rtscene.cpp), and never faulted.
  barrier(cmd, kCS | kXfer, VK_ACCESS_MEMORY_WRITE_BIT, kCS | kAS, kRW);
  fault_checkpoint(cmd, kCkBvhConverted, uint32_t(rec));
  JobBatch batch{now, 0, {}, {}, {}, rec};
  // Status slots: one per job, one per new mesh's compacted size, then the batch's
  // done word (written by a transfer after all of them).
  const bool compact = cfg_int("rt_mesh_compact", 1) != 0;
  const uint32_t n_cq = compact ? uint32_t((std::min)(mesh_builds.size(), size_t(kCompactSlots))) : 0;
  const uint32_t n_status = uint32_t(ext.size()) + n_cq;
  // The done word always exists: compaction swaps (decided below) wait for it too.
  const uint32_t st0 = status_slots(n_status + 1);
  batch.done_slot = st0 + n_status;
  if (!ext.empty()) {
    VkDescriptorBufferInfo soup{cap.xfb, 0, cap.xfb_size}, pool{g_pool.b, 0, g_pool.size},
        mats{g_ring.b, rbase + g_mat_off, VkDeviceSize(g_ring_mats) * 48}, status{g_status.b, 0, g_status.size};
    VkWriteDescriptorSet w[4];
    const VkDescriptorBufferInfo* infos[4] = {&soup, &pool, &mats, &status};
    for (uint32_t i = 0; i < 4; ++i) {
      w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[i].dstBinding = i;
      w[i].descriptorCount = 1;
      w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w[i].pBufferInfo = infos[i];
    }
    dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe);
    dd->vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl, 0, 4, w);
    const float tol_abs = cfg_float("rt_mesh_tol_abs", 2e-3f), tol_rel = cfg_float("rt_mesh_tol_rel", 4e-5f);
    for (uint32_t j = 0; j < ext.size(); ++j) {
      Ext& e = ext[j];
      e.status = st0 + j;
      uint32_t groups = (std::min)((e.tris + 255) / 256, 65535u);
      uint32_t pc[16] = {e.tris,
                         groups * 256,
                         stride,
                         e.kind == kJobExtract ? 0u : 1u,
                         uint32_t(e.soup),
                         uint32_t(e.pool),
                         e.mat0 * 48,
                         e.matn == kNone ? kNone : e.matn * 48,
                         uint32_t(e.soup_n),
                         e.status * 16,
                         0, 0, 0, 0, 0, 0};
      std::memcpy(&pc[10], &tol_abs, 4);
      std::memcpy(&pc[11], &tol_rel, 4);
      dd->vk.CmdPushConstants(cmd, g_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
      dd->vk.CmdDispatch(cmd, groups, 1, 1);
      auto mi = g_meshes.find(e.key);
      batch.jobs.push_back({e.key, mi != g_meshes.end() ? mi->second.gen : 0, e.status, e.kind, e.matn != kNone});
    }
    barrier(cmd, kCS, VK_ACCESS_SHADER_WRITE_BIT, kAS | kXfer, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                                                                 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
  }
  stamp(cmd, 1);

  // Compaction of meshes whose compacted size came back (copies read BLAS of earlier frames).
  {
    uint32_t budget = uint32_t((std::max)(cfg_int("rt_mesh_compact_per_frame", 256), 0));
    bool any = false;
    for (auto& [key, m] : g_meshes) {
      if (!budget) break;
      // Only verified meshes: a mesh still in probation may have a BLAS built from pool
      // data its extraction never wrote, and a compaction copy walks the BLAS's nodes.
      if (m.compacted || !m.compact_size || m.state != kReady || new_keys.count(key)) continue;
      AsAlloc c;
      if (m.compact_size >= m.as.size || !as_create(m.compact_size, c)) {
        m.compacted = true;   // nothing to gain, or no memory: keep the original
        continue;
      }
      VkCopyAccelerationStructureInfoKHR ci{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
      ci.src = m.as.as;
      ci.dst = c.as;
      ci.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
      dd->vk.CmdCopyAccelerationStructureKHR(cmd, &ci);
      g_st.compact_saved += m.as.size - c.size;
      // The mesh keeps its original BLAS until the batch is done (bvh_on_present).
      batch.swaps.push_back({key, m.gen, c});
      m.compacted = true;
      ++g_st.compactions;
      --budget;
      any = true;
    }
    if (any) barrier(cmd, kAS, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, kAS, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
  }

  // BLAS builds: new meshes (object space, from the pool) and the per-draw BLAS (from the soup).
  {
    std::vector<VkAccelerationStructureGeometryKHR> geo;
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
    geo.reserve(mesh_builds.size() + dyn_builds.size());
    VkDeviceSize scratch = 0, al = rt_scratch_alignment();
    std::vector<VkDeviceSize> scratch_off;
    for (const MeshBuild& mb : mesh_builds) {
      const Mesh& m = g_meshes[mb.key];
      geo.push_back(tri_geometry(g_pool.addr + m.pool_off, m.tris, stride, !m.alpha));
      ranges.push_back({m.tris, 0, 0, 0});
      VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
      bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      bi.flags = mesh_flags;
      bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      bi.dstAccelerationStructure = m.as.as;
      bi.geometryCount = 1;
      infos.push_back(bi);
      scratch_off.push_back(scratch);
      scratch += (blas_sizes(m.tris, stride, !m.alpha, mesh_flags).buildScratchSize + al - 1) / al * al;
    }
    for (const DynBuild& db : dyn_builds) {
      // The vertex range must lie in this frame's soup and its address be 4-byte aligned
      // (the component size of R32G32B32_SFLOAT, VUID-...-vertexData-03711; logged, the
      // capture guarantees it). Not a multiple of the stride: the alpha region starts at a
      // 256-byte boundary, which 24 does not divide.
      if (db.soup_off % 4u || db.soup_off + VkDeviceSize(db.prims) * tri_bytes > cap.xfb_size) {
        static uint32_t logged = 0;
        if (logged++ < 20)
          log("[bvh] WARNING frame %llu: per-draw BLAS vertices [%llu, +%llu) outside the soup (%llu bytes) or misaligned",
              (unsigned long long)now, (unsigned long long)db.soup_off,
              (unsigned long long)(VkDeviceSize(db.prims) * tri_bytes), (unsigned long long)cap.xfb_size);
      }
      geo.push_back(tri_geometry(soup_addr + db.soup_off, db.prims, stride, !db.slot->alpha));
      ranges.push_back({db.prims, 0, 0, 0});
      VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
      bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      bi.flags = dyn_flags;
      bi.mode = db.refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      bi.srcAccelerationStructure = db.refit ? db.slot->as.as : VK_NULL_HANDLE;
      bi.dstAccelerationStructure = db.slot->as.as;
      bi.geometryCount = 1;
      infos.push_back(bi);
      scratch_off.push_back(scratch);
      scratch += ((db.refit ? db.slot->update_scratch : db.slot->build_scratch) + al - 1) / al * al;
    }
    // The TLAS reuses the scratch from 0 after the barrier below.
    VkAccelerationStructureGeometryKHR tg{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    tg.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tg.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    VkAccelerationStructureBuildGeometryInfoKHR tbi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    tbi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tbi.flags = kTlasFlags;
    tbi.geometryCount = 1;
    tbi.pGeometries = &tg;
    VkAccelerationStructureBuildSizesInfoKHR tsz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    uint32_t cap_inst = g_tlas[slot].cap;
    dd->vk.GetAccelerationStructureBuildSizesKHR(dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tbi, &cap_inst,
                                                 &tsz);
    if (!ensure_scratch((std::max)({scratch, tsz.buildScratchSize, tsz.updateScratchSize}))) {
      g_failed = true;
      log("[bvh] scratch allocation failed (%llu MB); the split is off", (unsigned long long)(scratch >> 20));
      return false;
    }
    for (size_t i = 0; i < infos.size(); ++i) {
      infos[i].pGeometries = &geo[i];
      infos[i].scratchData.deviceAddress = scratch_at(scratch_off[i]);
    }
    if (!infos.empty()) {
      std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rp(ranges.size());
      for (size_t i = 0; i < ranges.size(); ++i) rp[i] = &ranges[i];
      if (cfg_int("rt_build_serial", 0)) {
        for (size_t i = 0; i < infos.size(); ++i) {
          if (i) barrier(cmd, kAS, VK_ACCESS_MEMORY_WRITE_BIT, kAS, kRW);
          dd->vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &infos[i], &rp[i]);
        }
      } else {
        dd->vk.CmdBuildAccelerationStructuresKHR(cmd, uint32_t(infos.size()), infos.data(), rp.data());
      }
    }
    // BLAS builds (and the copies before them) -> the size queries, the TLAS build
    // and its scratch reuse: as broad as the legacy build's barrier.
    barrier(cmd, kAS | kCS | kXfer, VK_ACCESS_MEMORY_WRITE_BIT, kAS | kXfer, kRW);
    fault_checkpoint(cmd, kCkBvhBlas, uint32_t(rec));
    // The compacted size of each new mesh: written, then copied into the batch's
    // status slots in order (a query's host-visible availability may still be a
    // previous use's until the reset executes, so the host never reads the pool).
    for (uint32_t k = 0; k < n_cq; ++k) {
      Mesh& m = g_meshes[mesh_builds[k].key];
      const uint32_t q = (g_compact_next + k) % kCompactSlots, sslot = st0 + uint32_t(ext.size()) + k;
      dd->vk.CmdResetQueryPool(cmd, g_compact_qp, q, 1);
      dd->vk.CmdWriteAccelerationStructuresPropertiesKHR(cmd, 1, &m.as.as, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                                                         g_compact_qp, q);
      dd->vk.CmdCopyQueryPoolResults(cmd, g_compact_qp, q, 1, g_status.b, VkDeviceSize(sslot) * 16, 8,
                                     VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
      batch.compact.push_back({mesh_builds[k].key, m.gen, sslot});
    }
    g_compact_next = (g_compact_next + n_cq) % kCompactSlots;
    {   // the done word, after every job, compaction copy and BLAS build of this batch
      barrier(cmd, kCS | kXfer | kAS, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, kXfer,
              VK_ACCESS_TRANSFER_WRITE_BIT);
      dd->vk.CmdFillBuffer(cmd, g_status.b, VkDeviceSize(batch.done_slot) * 16, 16, kDoneMagic);
      barrier(cmd, kXfer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    }    stamp(cmd, 2);

    // TLAS over this frame's instances.
    tg.geometry.instances.data.deviceAddress = g_ring.addr + rbase;
    uint64_t sig = 1469598103934665603ull;
    for (uint32_t i = 0; i < n; ++i) sig = mix(mix(sig, inst_out[i].accelerationStructureReference), inst_out[i].flags);
    // Every BLAS reference must lie in one of our chunks (a cheap guard; the fault
    // report checks each against the BLAS we hold).
    {
      static uint32_t bad_logged = 0;
      for (uint32_t i = 0; i < n; ++i) {
        const uint64_t r = inst_out[i].accelerationStructureReference;
        bool in = false;
        for (const AsChunk& c : g_chunks) in = in || (r >= c.buf.addr && r < c.buf.addr + c.buf.size);
        if (!in && bad_logged < 20) {
          ++bad_logged;
          log("[bvh] WARNING frame %llu slot %u: TLAS instance %u references 0x%llx, outside every BLAS chunk",
              (unsigned long long)now, slot, i, (unsigned long long)r);
          bvh_event("frame %llu: instance %u of slot %u references 0x%llx OUTSIDE our chunks", (unsigned long long)now, i,
                    slot, (unsigned long long)r);
        }
      }
    }
    // Refit: the previous slot's TLAS into this slot's (src != dst, same storage size).
    const bool tlas_refit = g_tlas_valid && g_tlas_prev >= 0 && g_tlas_prev != int(slot) && g_tlas[g_tlas_prev].as &&
                            rec_submitted(g_tlas_prev_rec) &&
                            g_tlas[g_tlas_prev].cap == g_tlas[slot].cap && n == g_tlas_n && sig == g_tlas_sig &&
                            g_tlas_since < uint32_t(rebuild_frames) && cfg_int("rt_tlas_refit", 1) != 0;
    g_tlas_since = tlas_refit ? g_tlas_since + 1 : 0;
    g_tlas_sig = sig;
    g_tlas_n = n;
    g_tlas_valid = true;
    g_st.tlas_refit += tlas_refit;
    tbi.mode = tlas_refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tbi.srcAccelerationStructure = tlas_refit ? g_tlas[g_tlas_prev].as : VK_NULL_HANDLE;
    tbi.dstAccelerationStructure = g_tlas[slot].as;
    g_tlas_prev = int(slot);
    g_tlas_prev_rec = rec;
    tbi.scratchData.deviceAddress = scratch_at(0);
    VkAccelerationStructureBuildRangeInfoKHR tr{n, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* trp = &tr;
    dd->vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &tbi, &trp);
  }
  // Ray queries in later command buffers of this frame (submission order).
  // Everything this build wrote (TLAS, BLAS, the converted soup and the pool the
  // ray queries read through set 1) -> the RT passes in later command buffers.
  barrier(cmd, kAS | kCS | kXfer, VK_ACCESS_MEMORY_WRITE_BIT, kCS | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          VK_ACCESS_MEMORY_READ_BIT);
  fault_checkpoint(cmd, kCkBvhTlas, uint32_t(rec));
  stamp(cmd, 3);
  const size_t batch_swaps = batch.swaps.size();
  if (n_status || !batch.swaps.empty()) g_batches.push_back(std::move(batch));

  {
    uint32_t n_new_dyn = 0;
    for (const DynBuild& db : dyn_builds) n_new_dyn += db.refit ? 0u : 1u;
    bvh_event("frame %llu: build into slot %u, record %llu, cmd %p: %u instances (%u static), %zu new meshes, %zu "
              "compaction copies, %zu per-draw BLAS (%u full builds), TLAS %s%s",
              (unsigned long long)now, slot, (unsigned long long)rec, (void*)cmd, n, n_static_written, mesh_builds.size(),
              batch_swaps, dyn_builds.size(), n_new_dyn, g_tlas_since ? "refit" : "build", g_pool_grew ? ", mesh pool GREW" : "");
  }
  g_slot_n[slot] = n;
  g_slot_frame[slot] = now;
  ++g_st.builds;
  {
    // EN2: this build's draws and their instances, for the G-buffer pass's join (hitmat.cpp).
    BvhJoinFrame j;
    j.frame = now;
    j.slot = slot;
    j.n_inst = n;
    j.stride = stride;
    j.soup = cap.xfb;
    j.soup_size = cap.xfb_size;
    j.pool = g_pool.b;
    j.pool_size = g_pool.size;
    j.draws.reserve(g_draws.size());
    for (const FrameDraw& fd : g_draws) {
      BvhJoinDraw d{};
      d.geom_key = fd.geom_key;
      d.inst_first = fd.inst_first;
      d.inst_count = fd.inst_count;
      d.cached = fd.cached;
      d.instances = fd.instances;
      d.tris_per_instance = fd.tris_per_instance;
      d.prims = fd.prims;
      d.soup_off = fd.soup_off;
      if (fd.cached) {
        auto it = g_meshes.find(fd.mesh_key);
        if (it == g_meshes.end()) d.inst_count = 0;
        else d.mesh_key = fd.mesh_key, d.mesh_gen = it->second.gen, d.pool_off = it->second.pool_off, d.alpha = it->second.alpha;
      } else {
        d.alpha = fd.cap_alpha && fd.rec != kNone;
      }
      j.draws.push_back(d);
    }
    std::lock_guard<std::mutex> jl(g_join_mu);
    g_join = std::move(j);
  }
  out->tlas = g_tlas[slot].as;
  out->instances = n;
  out->static_instances = n_static_written;
  out->dynamic_draws = n - n_static_written;
  out->cached_draws = uint32_t(cached.size());
  out->new_meshes = uint32_t(mesh_builds.size());
  out->static_tris = static_tris;
  out->dynamic_tris = dyn_tris;
  out->info = g_ring.b;
  out->info_off = rbase + g_info_off;
  out->info_bytes = VkDeviceSize((std::max)(n, 1u)) * 16;
  out->pool = g_pool.b;
  out->pool_bytes = g_pool.size;
  out->rec = rec;
  return true;
}

bool bvh_join_frame(BvhJoinFrame* out) {
  std::lock_guard<std::mutex> jl(g_join_mu);
  if (!g_join.frame) return false;
  *out = g_join;
  return true;
}

// The matrices of this frame's TLAS instances and jobs, read from the game's
// instance stream as it submits the command buffer that draws with them.
uint64_t bvh_rec_register(VkCommandBuffer cmd) {
  std::lock_guard<std::mutex> fl(g_fix_mu);
  const uint64_t id = ++g_rec_next;
  g_rec_cmd[cmd].push_back(id);
  g_rec_state[id] = kRecPending;
  if (g_rec_state.size() > 8192)   // forget old records (an unknown one is never trusted)
    for (auto it = g_rec_state.begin(); it != g_rec_state.end();)
      it = it->first + 4096 < g_rec_next ? g_rec_state.erase(it) : std::next(it);
  return id;
}

int bvh_rec_state(uint64_t id) { return rec_state(id); }

// The game begins `cmd` again or frees it: whatever we recorded into it and did not
// see submitted never runs.
void bvh_cmd_reset(VkCommandBuffer cmd) {
  std::lock_guard<std::mutex> fl(g_fix_mu);
  g_fixups.erase(cmd);   // matrices for a recording that will never run (the ring slot may be reused)
  auto it = g_rec_cmd.find(cmd);
  if (it == g_rec_cmd.end()) return;
  for (uint64_t id : it->second) {
    auto s = g_rec_state.find(id);
    if (s != g_rec_state.end() && s->second == kRecPending) {
      s->second = kRecDropped;
      ++g_rec_dropped;
      bvh_event("frame %llu: command buffer %p dropped without a submit: build record %llu", (unsigned long long)frame_index(),
                (void*)cmd, (unsigned long long)id);
      if (g_rec_dropped_logged < 10) {
        ++g_rec_dropped_logged;
        log("[bvh] frame %llu: a command buffer holding scene build %llu was dropped without a submit (%llu so far): "
            "its builds are not used",
            (unsigned long long)frame_index(), (unsigned long long)id, (unsigned long long)g_rec_dropped);
      }
    }
  }
  g_rec_cmd.erase(it);
}

void bvh_on_submit(const std::vector<VkCommandBuffer>& cmds) {
  std::lock_guard<std::mutex> fl(g_fix_mu);
  for (VkCommandBuffer c : cmds) {
    auto it = g_rec_cmd.find(c);
    if (it == g_rec_cmd.end()) continue;
    for (uint64_t id : it->second) {
      auto s = g_rec_state.find(id);
      if (s != g_rec_state.end() && s->second == kRecPending) s->second = kRecSubmitted;
    }
    g_rec_cmd.erase(it);
  }
  if (g_fixups.empty()) return;
  uint64_t applied = 0, late = 0, bad = 0;
  for (VkCommandBuffer c : cmds) {
    auto it = g_fixups.find(c);
    if (it == g_fixups.end()) continue;
    for (const Fixup& f : it->second) {
      if (f.instance && std::memcmp(f.sample, f.src[0], 16) | std::memcmp(f.sample + 16, f.src[1], 16) |
                            std::memcmp(f.sample + 32, f.src[2], 16))
        ++late;
      for (uint32_t i = 0; i < f.count; ++i) {
        uint8_t* d = f.dst + VkDeviceSize(i) * f.dst_stride;
        float m[12];
        for (int k = 0; k < 3; ++k) std::memcpy(m + 4 * k, f.src[k] + VkDeviceSize(i) * f.src_stride[k], 16);
        bool ok = true;
        for (float v : m) ok = ok && v > -1e7f && v < 1e7f;
        if (ok) {
          float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
                      m[2] * (m[4] * m[9] - m[5] * m[8]);
          ok = std::fabs(det) > 1e-12f;
        }
        if (!ok) ++bad;
        if (f.instance) {
          std::memcpy(d, m, 48);
          d[51] = ok ? 0xFF : 0;   // mask (the high byte of instanceCustomIndex:24 | mask:8)
        } else {
          if (!ok) m[0] = std::numeric_limits<float>::quiet_NaN();   // the job sees an invalid matrix
          std::memcpy(d, m, 48);
        }
      }
      applied += f.count;
    }
    g_fixups.erase(it);
  }
  std::lock_guard<std::mutex> sl(g_st_mu);
  g_st.fix_applied += applied;
  g_st.fix_late += late;
  g_st.fix_bad += bad;
}

void bvh_forget_buffer(VkBuffer b) {
  if (!g_any_mesh.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_buf_meshes.find(b);
  if (it == g_buf_meshes.end()) return;
  std::vector<uint64_t> keys = it->second;
  for (uint64_t k : keys) {
    evict(k);
    ++g_st.evict_destroy;
  }
}

void bvh_buffer_written(VkBuffer b, VkDeviceSize offset, VkDeviceSize size) {
  if (!g_any_mesh.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_buf_meshes.find(b);
  if (it == g_buf_meshes.end()) return;
  VkDeviceSize end = size == VK_WHOLE_SIZE ? ~VkDeviceSize(0) : offset + size;
  std::vector<uint64_t> hit;
  for (uint64_t k : it->second) {
    auto m = g_meshes.find(k);
    if (m == g_meshes.end()) continue;
    for (const Range& r : m->second.ranges)
      if (r.b == b && offset < r.hi && end > r.lo) {
        hit.push_back(k);
        break;
      }
  }
  for (uint64_t k : hit) {
    evict(k);
    ++g_st.evict_write;
  }
}

// The split's state for a device-loss or stall report (never blocks on g_mu).
void bvh_log_state(const char* prefix) {
  {
    std::unique_lock<std::mutex> lock(g_mu, std::try_to_lock);
    if (!lock.owns_lock()) {
      log("%s bvh: state busy (lock held)", prefix);
    } else {
      size_t swaps = 0, jobs = 0;
      for (const JobBatch& b : g_batches) swaps += b.swaps.size(), jobs += b.jobs.size();
      size_t ready = 0;
      for (const auto& [k, m] : g_meshes) ready += m.state == kReady;
      log("%s bvh: %zu released BLAS handles kept alive (rt_as_keep), %llu destroyed over the cap", prefix, g_kept_as.size(),
          (unsigned long long)g_kept_dropped);
      log("%s bvh: %zu meshes (%zu ready), %zu per-draw BLAS, %zu chunks, pool %llu MB, %zu batches pending (%zu jobs, %zu "
          "compaction copies not yet swapped in), %zu ranges awaiting release, last TLAS slot %d (record %llu), "
          "oldest live scene slot built in frame %llu, now %llu",
          prefix, g_meshes.size(), ready, g_dyn.size(), g_chunks.size(), (unsigned long long)(g_pool.size >> 20),
          g_batches.size(), jobs, swaps, g_pending_free.size(), g_tlas_prev, (unsigned long long)g_tlas_prev_rec,
          (unsigned long long)rt_scene_oldest_live(), (unsigned long long)frame_index());
      // Every slot's TLAS instances: is each BLAS reference one we hold?
      std::unordered_map<VkDeviceAddress, const char*> live;
      for (const auto& [k, m] : g_meshes)
        if (m.as.addr) live[m.as.addr] = "mesh";
      for (const auto& [k, s] : g_dyn)
        if (s.as.addr) live[s.as.addr] = "per-draw";
      for (const JobBatch& b : g_batches)
        for (const Swap& s : b.swaps)
          if (s.c.addr) live[s.c.addr] = "pending compaction";
      std::unordered_map<VkDeviceAddress, uint64_t> pending;
      for (const PendingFree& p : g_pending_free)
        if (p.chunk >= 0 && size_t(p.chunk) < g_chunks.size()) pending[g_chunks[p.chunk].buf.addr + p.off] = p.frame;
      for (uint32_t s = 0; s < kSceneSlots; ++s) {
        if (!g_slot_n[s] || !g_ring.map || g_slot_n[s] > g_ring_inst) continue;
        const auto* inst = reinterpret_cast<const VkAccelerationStructureInstanceKHR*>(g_ring.map + VkDeviceSize(s) * g_region_bytes);
        uint32_t ok = 0, freed = 0, released = 0, foreign = 0, masked = 0;
        std::string first;
        for (uint32_t i = 0; i < g_slot_n[s]; ++i) {
          const uint64_t r = inst[i].accelerationStructureReference;
          masked += inst[i].mask == 0;
          if (live.count(r)) {
            ++ok;
            continue;
          }
          auto pf = pending.find(r);
          bool in_chunk = false;
          for (const AsChunk& c : g_chunks) in_chunk = in_chunk || (r >= c.buf.addr && r < c.buf.addr + c.buf.size);
          (pf != pending.end() ? freed : in_chunk ? released : foreign)++;
          if (first.size() < 300) {
            char b[96];
            snprintf(b, sizeof b, " #%u 0x%llx (%s)", i, (unsigned long long)r,
                     pf != pending.end() ? "freed, unreleased" : in_chunk ? "RELEASED range" : "NOT OURS");
            first += b;
          }
        }
        log("%s bvh: slot %u (built frame %llu): %u instances, %u masked; BLAS references: %u held, %u freed but not "
            "released, %u released, %u outside our chunks%s%s",
            prefix, s, (unsigned long long)g_slot_frame[s], g_slot_n[s], masked, ok, freed, released, foreign,
            first.empty() ? "" : ":", first.c_str());
      }
    }
  }
  {
    std::lock_guard<std::mutex> fl(g_fix_mu);
    size_t pending = 0;
    for (const auto& [id, s] : g_rec_state) pending += s == kRecPending;
    log("%s bvh: build records: %zu pending (recorded, not submitted), %llu dropped so far, %zu command buffers with "
        "unsubmitted fixups",
        prefix, pending, (unsigned long long)g_rec_dropped, g_fixups.size());
  }
  std::lock_guard<std::mutex> l(g_ev_mu);
  log("%s bvh: the last %zu events:", prefix, g_events.size());
  for (const std::string& e : g_events) log("%s   %s", prefix, e.c_str());
}

// What of ours lies at device address `a` (device-fault reports, devfault.cpp).
std::string bvh_describe_address(VkDeviceAddress a) {
  std::unique_lock<std::mutex> lock(g_mu, std::try_to_lock);   // never block a device-loss report
  if (!lock.owns_lock()) return "bvh state busy";
  char b[256];
  auto in = [&](VkDeviceAddress lo, VkDeviceSize size) { return lo && a >= lo && a < lo + size; };
  const uint64_t now = frame_index();
  for (size_t ci = 0; ci < g_chunks.size(); ++ci) {
    const AsChunk& c = g_chunks[ci];
    if (!in(c.buf.addr, c.buf.size)) continue;
    const VkDeviceSize off = a - c.buf.addr;
    for (const auto& [k, m] : g_meshes)
      if (m.as.chunk == int(ci) && off >= m.as.off && off < m.as.off + m.as.size) {
        snprintf(b, sizeof b, "BLAS of mesh %016llx (gen %llu, %u tris, %s, state %d, created frame %llu, now %llu)",
                 (unsigned long long)k, (unsigned long long)m.gen, m.tris, m.compacted ? "compacted" : "not compacted",
                 int(m.state), (unsigned long long)m.created, (unsigned long long)now);
        return b;
      }
    for (const auto& [k, s] : g_dyn)
      if (s.as.chunk == int(ci) && off >= s.as.off && off < s.as.off + s.as.size) {
        snprintf(b, sizeof b, "per-draw BLAS %016llx (%u prims, cap %u, last used frame %llu)", (unsigned long long)k,
                 s.prims, s.cap_prims, (unsigned long long)s.last_used);
        return b;
      }
    for (const PendingFree& p : g_pending_free)
      if (p.chunk == int(ci) && off >= p.off && off < p.off + p.size) {
        snprintf(b, sizeof b, "a BLAS range freed in frame %llu, not yet released (now %llu)", (unsigned long long)p.frame,
                 (unsigned long long)now);
        return b;
      }
    snprintf(b, sizeof b, "BLAS chunk %zu at +%llu: no BLAS allocated there (released, maybe reused)", ci,
             (unsigned long long)off);
    return b;
  }
  if (in(g_pool.addr, g_pool.size)) {
    const VkDeviceSize off = a - g_pool.addr;
    for (const auto& [k, m] : g_meshes)
      if (m.pool_size && off >= m.pool_off && off < m.pool_off + m.pool_size) {
        snprintf(b, sizeof b, "mesh pool: vertices of mesh %016llx (gen %llu)", (unsigned long long)k,
                 (unsigned long long)m.gen);
        return b;
      }
    snprintf(b, sizeof b, "mesh pool at +%llu: no live mesh there", (unsigned long long)off);
    return b;
  }
  if (in(g_ring.addr, g_ring.size) && g_region_bytes) {
    const VkDeviceSize off = a - g_ring.addr, r = off / g_region_bytes, in_r = off % g_region_bytes;
    snprintf(b, sizeof b, "host ring slot %llu, %s", (unsigned long long)r,
             in_r < g_info_off ? "TLAS instances" : in_r < g_mat_off ? "InstInfo" : "job matrices");
    return b;
  }
  for (uint32_t i = 0; i < kSceneSlots; ++i)
    if (in(g_tlas[i].buf.addr, g_tlas[i].buf.size)) {
      snprintf(b, sizeof b, "split TLAS of slot %u%s", i, int(i) == g_tlas_prev ? " (the latest build)" : "");
      return b;
    }
  return "";
}

void bvh_on_present() {
  if (!g_on) return;
  std::lock_guard<std::mutex> lock(g_mu);
  const uint64_t now = frame_index();
  // Ranges of evicted meshes and slots, once no frame in flight can use them:
  // the GPU must have finished frame + 2 (as defer_tick), not just 6 presents
  // passed. Reusing a range a trace still reads was the intermittent device
  // lost a few seconds into gameplay (docs/STATUS.md, "render stall").
  {
    // Also: no scene slot the GPU may still read was built at or before the free
    // (its TLAS can reference the range: an older TLAS is traced up to a frame later).
    const uint64_t gpu_done = life_gpu_done(), oldest_live = rt_scene_oldest_live();
    size_t w = 0;
    for (size_t i = 0; i < g_pending_free.size(); ++i) {
      const PendingFree& p = g_pending_free[i];
      if (now < p.frame + 6 || (p.frame + 2 > gpu_done && now < p.frame + 240) || p.frame >= oldest_live) {
        g_pending_free[w++] = p;
        continue;
      }
      retire_as(p.as);
      if (p.chunk < 0) g_pool_ra.release(p.off, p.size);
      else if (size_t(p.chunk) < g_chunks.size()) g_chunks[p.chunk].ra.release(p.off, p.size);
    }
    g_pending_free.resize(w);
  }
  // Validation results, once the GPU wrote the batch's done word.
  const float tol_ok = 1.0f;
  for (size_t bi = 0; bi < g_batches.size();) {
    JobBatch& b = g_batches[bi];
    // The done word is zeroed at record time and filled after the batch's jobs:
    // unlike a query's availability, it cannot be a previous use's.
    const bool done = *reinterpret_cast<const volatile uint32_t*>(g_status.map + VkDeviceSize(b.done_slot) * 16) == kDoneMagic;
    const int res = 0;
    // Its command buffer dropped without a submit: nothing of it ran, lost at once.
    const bool dropped = !done && rec_state(b.rec) == kRecDropped;
    if (!done && !dropped && now < b.frame + 240) {
      ++bi;
      continue;
    }
    for (const Job& j : b.jobs) {
      auto it = g_meshes.find(j.key);
      if (it == g_meshes.end() || it->second.gen != j.gen) continue;   // evicted (maybe re-created) since
      Mesh& m = it->second;
      const uint32_t* st = reinterpret_cast<const uint32_t*>(g_status.map + VkDeviceSize(j.status) * 16);
      float err;
      std::memcpy(&err, &st[0], 4);
      const uint32_t invalid = st[1], magic = st[3];
      if (!done || magic != 0x42564853u) {
        // Lost (not done within 240 frames, or the job did not run): check again
        // later; an extraction's pool data is then verified by a check job.
        ++g_st.lost;
        ++(done ? g_st.lost_magic : g_st.lost_timeout);
        if (g_st.lost_logged < 3) {
          ++g_st.lost_logged;
          log("[bvh] validation result lost: job kind %d, batch frame %llu, now %llu, done %d (%d), magic 0x%08x",
              int(j.kind), (unsigned long long)b.frame, (unsigned long long)now, int(done), res, magic);
        }
        m.check_pending = false;
        if (j.kind == kJobExtract) {
          if (!done) {
            // The batch never ran (dropped, or not within 240 frames): the mesh's BLAS was
            // never built and its pool range never written. Evicted; extracted again later.
            evict(j.key);
            continue;
          }
          // The batch ran but the job did not report: its pool range may never have been
          // written, so the BLAS was built from whatever the memory held. Evicted.
          evict(j.key);
          continue;
        }
        continue;
      }
      const bool ok = invalid == 0 && err <= tol_ok;
      if (ok) g_st.err_max_ok = (std::max)(g_st.err_max_ok, err);
      switch (j.kind) {
        case kJobExtract:
          if (!ok) reject(j.key, invalid ? kRejInvalid : kRejMismatch);
          else if (j.cross_instance) m.state = kReady, ++g_st.ready;
          else m.state = kNeedCheck;
          break;
        case kJobCheck:
          m.check_pending = false;
          if (!ok) reject(j.key, invalid ? kRejInvalid : kRejMismatch);
          else m.state = kReady, ++g_st.ready, ++g_st.checks_ok;
          break;
        case kJobAudit:
          if (!ok) {
            log("[bvh] WARNING audit failed for a cached mesh (%u triangles, error %.3g x tolerance, %u invalid): evicted",
                m.tris, err, invalid);
            reject(j.key, kRejAudit);
          } else {
            ++g_st.audits_ok;
          }
          break;
      }
    }
    // Compaction copies: the mesh takes the compacted BLAS only once the copy ran.
    for (Swap& s : b.swaps) {
      auto it = g_meshes.find(s.key);
      if (done && it != g_meshes.end() && it->second.gen == s.gen) {
        as_destroy(it->second.as);   // the original: released once no live slot can reference it
        it->second.as = s.c;
      } else {
        as_destroy(s.c);             // never written (or the mesh is gone)
      }
    }
    if (!b.swaps.empty() || (!done && !b.jobs.empty()))
      bvh_event("frame %llu: batch of frame %llu (record %llu) %s: %zu jobs, %zu compaction copies %s",
                (unsigned long long)now, (unsigned long long)b.frame, (unsigned long long)b.rec,
                done ? "done" : dropped ? "DROPPED" : "TIMED OUT", b.jobs.size(), b.swaps.size(),
                done ? "swapped in" : "discarded");
    if (!done && !b.swaps.empty() && g_st.lost_logged < 6) {
      ++g_st.lost_logged;
      log("[bvh] batch of frame %llu %s: %zu compaction copies discarded, the meshes keep their original BLAS",
          (unsigned long long)b.frame, dropped ? "dropped without a submit" : "not done in 240 frames", b.swaps.size());
    }
    if (done)
      for (const CompactRead& c : b.compact) {
        auto it = g_meshes.find(c.key);
        uint64_t size = 0;
        std::memcpy(&size, g_status.map + VkDeviceSize(c.slot) * 16, 8);
        if (it != g_meshes.end() && it->second.gen == c.gen && !it->second.compacted && size)
          it->second.compact_size = size;
      }
    g_batches.erase(g_batches.begin() + bi);
  }
  // Per-draw BLAS not seen for a while; the mesh cache over its memory budget (LRU).
  for (auto it = g_dyn.begin(); it != g_dyn.end();) {
    if (now > it->second.last_used + 120) {
      as_destroy(it->second.as);
      it = g_dyn.erase(it);
    } else {
      ++it;
    }
  }
  const VkDeviceSize budget = VkDeviceSize((std::max)(cfg_int("rt_mesh_budget_mb", 2048), 64)) << 20;
  const VkDeviceSize used = as_bytes() + g_pool_ra.used;
  const uint64_t lru_frames = uint64_t((std::max)(cfg_int("rt_mesh_lru_frames", 36000), 600));
  if (used > budget || (now % 600) == 0) {
    std::vector<std::pair<uint64_t, uint64_t>> old;   // last used, key
    for (auto& [k, m] : g_meshes)
      if (m.last_used + 8 < now) old.push_back({m.last_used, k});
    std::sort(old.begin(), old.end());
    VkDeviceSize cur = used;
    for (auto& [lu, k] : old) {
      if (cur <= budget * 9 / 10 && lu + lru_frames > now) break;
      const Mesh& m = g_meshes[k];
      cur -= (std::min)(cur, m.as.size + m.pool_size);
      evict(k);
      ++g_st.evict_lru;
    }
  }
  for (auto it = g_rejected.begin(); it != g_rejected.end();)
    it = now > it->second.until + 72000 ? g_rejected.erase(it) : std::next(it);

  int every = (std::max)(cfg_int("rt_timing_frames", 300), 30);
  if (g_st.builds < uint64_t(every)) return;
  Stats s;
  {
    std::lock_guard<std::mutex> sl(g_st_mu);
    s = g_st;
    g_st = Stats{};
  }
  const double nb = double(s.builds);
  uint64_t mesh_tris = 0, ready = 0;
  for (auto& [k, m] : g_meshes) {
    mesh_tris += m.tris;
    ready += m.state == kReady;
  }
  log("[bvh] last %llu builds, per build: %.1f cached draws -> %.0f static instances (%.3fM triangles), %.1f captured "
      "draws (%.3fM triangles; per-draw BLAS: %.1f refits, %.1f rebuilds, %.2f new); TLAS refit %.0f%%; not instanceable per build: "
      "pattern %.1f, stream %.1f, count %.1f",
      (unsigned long long)s.builds, s.cached_draws / nb, s.static_inst / nb, s.static_tris / nb * 1e-6, s.dyn_draws / nb,
      s.dyn_tris / nb * 1e-6, s.dyn_refit / nb, s.dyn_rebuild / nb, s.dyn_new / nb, 100.0 * s.tlas_refit / nb, s.not_inst_pattern / nb,
      s.not_inst_stream / nb, s.not_inst_count / nb);
  log("[bvh] mesh cache: %zu meshes (%zu ready, %.2fM triangles), pool %.1f / %.0f MB, AS %.1f MB in %zu chunks; this "
      "window: %llu new (%.2fM triangles, %llu deferred by the per-build budget), %llu verified, %llu checks ok, "
      "%llu audits ok, rejected %llu/%llu/%llu (%s/%s/%s), %llu results lost (%llu timed out), max accepted error %.3f x tolerance; "
      "evicted %llu destroyed, %llu written, %llu LRU; %llu compactions (-%.1f MB)",
      g_meshes.size(), size_t(ready), double(mesh_tris) * 1e-6, double(g_pool_ra.used) / 1048576.0,
      double(g_pool.size) / 1048576.0, double(as_bytes()) / 1048576.0, g_chunks.size(), (unsigned long long)s.new_meshes,
      s.new_tris * 1e-6, (unsigned long long)s.budget_deferred, (unsigned long long)s.ready,
      (unsigned long long)s.checks_ok, (unsigned long long)s.audits_ok, (unsigned long long)s.rejected[0],
      (unsigned long long)s.rejected[1], (unsigned long long)s.rejected[2], kRejectName[0], kRejectName[1],
      kRejectName[2], (unsigned long long)s.lost, (unsigned long long)s.lost_timeout, s.err_max_ok, (unsigned long long)s.evict_destroy,
      (unsigned long long)s.evict_write, (unsigned long long)s.evict_lru, (unsigned long long)s.compactions,
      double(s.compact_saved) / 1048576.0);
  log("[bvh] instance matrices read at submit: %llu, non-finite/singular %llu; draws whose instance data changed "
      "between record and submit %llu; recorded but never submitted %llu",
      (unsigned long long)s.fix_applied, (unsigned long long)s.fix_bad, (unsigned long long)s.fix_late,
      (unsigned long long)s.fix_stale);
}

}  // namespace bg3
