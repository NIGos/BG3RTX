#include "../common/perf_hook_state.h"
#include "../common/cpu_exact_cache.h"
#include "../common/cpu_instance_rows.h"
#include "../common/cpu_build_keys.h"
#include "../common/cpu_deferred_ranges.h"
#include "../common/native_hit_records.h"
#include "../common/native_hit_pool.h"
#include "mesh_content_id.h"
#include "hair_groom.h"
#include "../common/hair_groom_budget.h"
#include "../common/hair_primary.h"
// Track D: the static/dynamic BVH split (docs/TRACK_D_RT.md section 11,
// docs/archive/AUDIT_SOTA.md rank 2). rt_bvh_split=1 (live); 0 is the old single soup BLAS.
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
#include "hit_material.h"
#include "../common/dyn_hold.h"
#include "../common/cascade_casters.h"
#include "../common/rt_capture_budget.h"
#include "../common/host_memory.h"
#include "../common/vram_guard.h"
#include "../common/as_chunk_alloc.h"
#include "../common/vram_compact.h"
#include "omm_native_plain.h"
#include "omm_submit.h"
#include "../common/omm_material_cache.h"
#include "../common/omm_mask_bake.h"

#include <algorithm>
#include <smmintrin.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <cstdarg>
#include <deque>
#include <unordered_set>
#include "../common/omm_dyn_refusal.h"
#include "../common/omm_dyn_evict.h"

namespace bg3 {

namespace {
// cpu_timing=1: CPU time of bvh_build's phases (logged with the [cpu] scene build line).
double g_phase_ms[6] = {}, g_phase_t0 = 0; int g_phase_cur = -1; uint64_t g_phase_n = 0;
double now_ms() { LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f); return double(t.QuadPart) * 1000.0 / double(f.QuadPart); }
// The game's instance streams live in mapped memory that is usually write-combined (or
// uncached): ordinary loads from it are uncached, ~4 us per 16 bytes (P3: 28 ms per frame
// for 2,500 matrices). SSE4.1 streaming loads read such memory a full line at a time.
void load_mapped16(float* dst, const uint8_t* src) {
  if ((reinterpret_cast<uintptr_t>(src) & 15) == 0) {
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst),
                     _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<uint8_t*>(src))));
  } else {
    std::memcpy(dst, src, 16);
  }
}

void bvh_cpu_mark(int phase) {   // phase -1 closes the build
  const double t = now_ms();
  if (g_phase_cur >= 0) g_phase_ms[g_phase_cur] += t - g_phase_t0;
  g_phase_cur = phase; g_phase_t0 = t;
}

constexpr uint32_t kInfoPool = 1u << 8, kInfoLegacy = 1u << 9, kInfoAlpha = 1u << 10;   // rt_common.hlsli
constexpr uint32_t kInfoHairProcedural = 1u << 12;
constexpr uint32_t kInfoGroomReplaced = 1u << 13;   // rt_hair_groom: the card of a groom instance (rt_common.hlsli)
// rt_primary_fade_class (diagnostics): the draw's camera-occlusion fade cut is enabled
// (gi/pan_primary.hlsl RT_PRIMARY_FADE). Set only with the key on; no ray reads it otherwise.
constexpr uint32_t kInfoCameraFade = 1u << 14;
// A dynamic instance held by rt_dyn_hold (rt_occ_stats' occluder kind "held", common/occ_stats.h
// kOccInfoHeld). Bits 16..31 are the pool alpha slot only with nh_pool_meta, never set on a held one.
constexpr uint32_t kInfoHeld = 1u << 31;
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
  bool allocated = false;
  if (host) {
    // BAR VRAM first, system memory when the BAR heap is full (no Resizable BAR: common/host_memory.h).
    static_assert(bg3::host_memory::kBar == (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), "host_memory.h flag values");
    VkPhysicalDeviceMemoryProperties props{};
    if (dd->inst && dd->inst->GetPhysicalDeviceMemoryProperties) dd->inst->GetPhysicalDeviceMemoryProperties(dd->physical, &props);
    uint32_t flags[VK_MAX_MEMORY_TYPES];
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) flags[i] = props.memoryTypes[i].propertyFlags;
    bool fell_back = false;
    allocated = bg3::host_memory::allocate_host_visible(flags, props.memoryTypeCount, req.memoryTypeBits, [&](uint32_t t) {
      mai.memoryTypeIndex = t;
      return dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.m) == VK_SUCCESS;
    }, &fell_back) != bg3::host_memory::kNoType;
    static std::atomic<uint32_t> fallbacks{0};
    if (fell_back && fallbacks.fetch_add(1) < 8)
      log("[bvh] host-visible VRAM (BAR heap) full: %s (%llu KB) in system memory", what, (unsigned long long)(size >> 10));
  } else {
    mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    allocated = mai.memoryTypeIndex != UINT32_MAX && dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.m) == VK_SUCCESS;
  }
  if (!allocated) {
    dd->vk.DestroyBuffer(dd->device, out.b, nullptr);
    out = Buf{};
    return false;
  }
  if (dd->vk.BindBufferMemory(dd->device, out.b, out.m, 0) != VK_SUCCESS) {
    dd->vk.DestroyBuffer(dd->device, out.b, nullptr);
    dd->vk.FreeMemory(dd->device, out.m, nullptr);
    out = Buf{};
    return false;
  }
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

using as_chunks::RangeAlloc;

// ---- acceleration structure storage: suballocated chunks ----
struct AsChunk { Buf buf; as_chunks::ChunkRanges ra; };
uint64_t g_chunks_freed = 0, g_chunk_bytes_freed = 0;
std::vector<AsChunk> g_chunks;
uint64_t g_relocated = 0, g_relocated_bytes = 0;
int g_relocating_chunk = -1; // do not refill a source awaiting old-TLAS retirement
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
struct KeptAs { VkAccelerationStructureKHR as; int chunk; };
std::deque<KeptAs> g_kept_as;
uint64_t g_kept_dropped = 0;
void retire_as(VkAccelerationStructureKHR as, int chunk) {
  if (!as) return;
  if (!cfg_int("rt_as_keep", 0)) {
    defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(as));
    return;
  }
  g_kept_as.push_back({as, chunk});
  if (chunk >= 0) ++g_chunks[chunk].ra.kept;
  const size_t cap = size_t((std::max)(cfg_int("rt_as_keep_max", 200000), 1000));
  while (g_kept_as.size() > cap) {
    defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(g_kept_as.front().as));
    if (g_kept_as.front().chunk >= 0) --g_chunks[g_kept_as.front().chunk].ra.kept;
    g_kept_as.pop_front();
    if (g_kept_dropped++ == 0) log("[bvh] more than %zu released BLAS handles kept: the oldest are destroyed", cap);
  }
}

VkDeviceSize as_reserved() {
  VkDeviceSize n = 0;
  for (const AsChunk& c : g_chunks) n += c.buf.size;
  return n;
}

// guard_what: a new chunk for this BLAS is a growth rt_vram_guard may refuse (new cached meshes, per-draw
// BLAS: the geometry is then left out of the frame, logged by the guard). nullptr: never refused (compaction
// copies, which give memory back, and the scene's own cascade/sun BLAS).
bool as_handle(VkDeviceSize size, int ci, VkDeviceSize off, AsAlloc& out) {
  VkAccelerationStructureCreateInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  info.buffer = g_chunks[ci].buf.b;info.offset = off;info.size = size;
  info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  if (g_dd->vk.CreateAccelerationStructureKHR(g_dd->device, &info, nullptr, &out.as) != VK_SUCCESS) {
    g_chunks[ci].ra.cancel(off, size);out = {};return false;
  }
  VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
  ai.accelerationStructure = out.as;
  out.addr = g_dd->vk.GetAccelerationStructureDeviceAddressKHR(g_dd->device, &ai);
  out.chunk = ci;out.off = off;out.size = size;return true;
}
bool as_create(VkDeviceSize size, AsAlloc& out, const char* guard_what = nullptr, uint32_t arena = 0) {
  out = AsAlloc{};
  size = (size + kAsAlign - 1) / kAsAlign * kAsAlign;
  VkDeviceSize off = 0;
  // Small persistent compaction destinations must not pin holes left by large
  // per-draw/cascade rebuilds. Keep their retirement/reuse domains separate.
  // Key off retains the ordinary mixed allocator; ownership is recorded always.
  const uint32_t allocation_arena = cfg_int("rt_vram_compact", 0) ? arena : UINT32_MAX;
  int ci = as_chunks::allocate_fullest(g_chunks, size, kAsAlign, &off, g_relocating_chunk, allocation_arena);
  if (ci < 0) {
    VkDeviceSize cs = (std::max)(VkDeviceSize(cfg_int("rt_mesh_chunk_mb", 64)) << 20, size);
    // Retained empty chunks (rt_as_chunk_reuse_frames) too small for this range must not count against
    // the chunks' share: free them before the cap check, as the sweep would have without retention.
    if (guard_what && vram_guard_on() && as_reserved() + cs > vram_cap(vram_guard::kAsChunks, UINT64_MAX))
      for (AsChunk& c : g_chunks)
        if (c.buf.b && c.ra.reclaimable() && c.ra.empty_since) {
          ++g_chunks_freed;g_chunk_bytes_freed += c.buf.size;
          free_buf(c.buf);c.ra = as_chunks::ChunkRanges{};
        }
    if (guard_what && vram_guard_on() &&
        !vram_allow(vram_guard::kAsChunks, cs, guard_what,
                    as_reserved() + cs > vram_cap(vram_guard::kAsChunks, UINT64_MAX)))   // over the budget's share
      return false;
    AsChunk c;
    if (!make_buf(c.buf, cs, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false, "BLAS chunk (mesh/per-draw BLAS)"))
      return false;
    c.ra.init(cs);
    c.ra.arena = arena;
    c.ra.alloc(size, kAsAlign, &off);
    // A retired empty slot (buf.b null, no range in it) is reused: chunk indices stay stable.
    for (size_t i = 0; i < g_chunks.size() && ci < 0; ++i)
      if (!g_chunks[i].buf.b) g_chunks[i] = std::move(c), ci = int(i);
    if (ci < 0) {
      g_chunks.push_back(std::move(c));
      ci = int(g_chunks.size() - 1);
    }
  }
  return as_handle(size, ci, off, out);
}

// Groom BLAS, points and LOD ranges: the strand passes in the game's command buffers (rt_hair_groom_primary: the
// G-buffer variant, the coverage probe) may trace a groom TLAS published up to 2 frames before the one the scene slots
// track (hc1/hv5 2026-11-04: device lost in the first load, reads in no buffer of ours). Their frees wait that long more.
constexpr uint64_t kGroomFreeLag = 3;
void as_destroy(AsAlloc& a, uint64_t lag = 0) {
  if (a.chunk >= 0) {
    g_chunks[a.chunk].ra.mark_pending(a.size);
    g_pending_free.push_back({frame_index() + lag, a.chunk, a.off, a.size, a.as});
  }
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
uint64_t g_pool_epoch = 1;          // +1 whenever g_pool is replaced (hit material basis jobs, native_hit_pool.h)
VkDeviceSize g_basis_bytes = 0;     // pool ranges holding hit-material UV/basis (native_hit::pool::kBasisCapBytes)
// Live meshes that wanted a UV/basis range while the cap was full, and their bytes: logged when the count
// first passes 1, 2, 4, ... ("over kBasisCapBytes", smoke_ultra forbids it); 2026-11-01 ran at 240 of 256 MB.
uint64_t g_basis_over = 0, g_basis_over_logged = 0;
VkDeviceSize g_basis_over_bytes = 0;
struct PoolMove { uint64_t key, gen; VkDeviceSize position, basis, position_size, basis_size; };
struct PoolHoldMove { uint64_t key; VkDeviceSize source, destination, bytes; };
struct PoolCopy {
  Buf target;
  RangeAlloc ranges;
  std::vector<PoolMove> meshes;
  std::vector<PoolHoldMove> holds;
  uint64_t frame = 0, rec = 0;
  uint32_t done_slot = 0;
};
PoolCopy g_pool_copy;
Buf g_compact_status; // private completion words; never aliased by the legacy status ring
struct OldPool { Buf buf; uint64_t frame; };
std::vector<OldPool> g_old_pools;
vram_compact::QuietWindow g_pool_quiet;
bool g_pool_snapshots = false;
uint64_t g_pool_repacks = 0, g_pool_saved = 0;

bool pool_alloc(VkCommandBuffer cmd, VkDeviceSize size, VkDeviceSize* off) {
  if (g_pool_copy.target.b) return false; // immutable source until copy confirmation
  if (g_pool.b && g_pool_ra.alloc(size, 16, off)) return true;
  // Below 2^31 bytes and within maxStorageBufferRange (common/rt_capture_budget.h mesh_pool_capacity:
  // at 2048 MB set 1 read the pool out of range). Full: the mesh stays uncached, logged once per size.
  static VkDeviceSize s_range = 0, s_full_logged = 0;
  if (!s_range) {
    VkPhysicalDeviceProperties p{};
    g_dd->inst->GetPhysicalDeviceProperties(g_dd->physical, &p);
    s_range = p.limits.maxStorageBufferRange;
  }
  // rt_vram_guard: the cap also follows the device budget (vram_guard::cap; 2047 MB on the 32 GB reference).
  const VkDeviceSize cfg_max = (std::min)(VkDeviceSize((std::max)(cfg_int("rt_mesh_pool_max_mb", 3072), 64)) << 20,
                                          VkDeviceSize(vram_cap(vram_guard::kMeshPool, UINT64_MAX)));
  const VkDeviceSize cap = capture_budget::mesh_pool_capacity(
      g_pool.size, size, VkDeviceSize((std::max)(cfg_int("rt_mesh_pool_mb", 256), 16)) << 20, cfg_max, s_range);
  if (!cap) {
    if (s_full_logged != g_pool.size) {
      s_full_logged = g_pool.size;
      log("[bvh] mesh pool full at %llu MB (cap: below 2048 MB, rt_mesh_pool_max_mb, device range %llu MB): "
          "new meshes stay uncached", (unsigned long long)(g_pool.size >> 20), (unsigned long long)(s_range >> 20));
    }
    return false;
  }
  // The new pool is allocated while the old one is still alive (copied, freed deferred): the step is `cap`.
  if (!vram_allow(vram_guard::kMeshPool, cap, "mesh pool grow (new meshes stay uncached)")) return false;
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
  ++g_pool_epoch;
  g_pool_ra.grow(cap);
  g_pool_grew = true;
  return g_pool_ra.alloc(size, 16, off);
}

// ---- meshes ----
enum MeshState : uint8_t { kBuilt, kNeedCheck, kReady };
enum Reject : uint8_t { kRejInvalid, kRejMismatch, kRejAudit, kRejCount };
const char* kRejectName[kRejCount] = {"invalid vertices or matrix", "instance mismatch", "audit mismatch"};
struct Range { VkBuffer b; VkDeviceSize lo, hi; };
struct NativeOmmVariant {
  omm::MaterialKey material{};
  uint64_t record{};
  omm::PlainOutput baked{};
  omm::AttachedBlas blas{};
};
struct Mesh {
  uint64_t content_id = 0; // diagnostic candidate, never a material join
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
  uint64_t geo_key = 0;               // native_hit pool material record (rt_hit_material; 0 none)
  VkDeviceSize basis_off = 0, basis_size = 0;   // its UV/basis range in the pool (0: none)
  VkDeviceSize basis_over = 0;  // wanted a basis range but kBasisCapBytes was full (counted in g_basis_over)
  std::unique_ptr<std::vector<NativeOmmVariant>> omm;
};
uint64_t g_mesh_gen = 0;
std::unordered_map<uint64_t, Mesh> g_meshes;
std::unordered_map<VkBuffer, std::vector<uint64_t>> g_buf_meshes;
// Static instance ranges retain baseline order and InstInfo. The accelerated
// scene replaces only these ranges' BLAS addresses after copying late matrices.
struct OmmInstanceRange {
  uint32_t first{},count{};
  uint64_t mesh{},generation{};
  omm::PlainOutput baked{};
  omm::AttachedBlas blas{};
  VkBuffer epochs{};
  VkDeviceSize epoch_bytes{};
};
std::vector<OmmInstanceRange> g_omm_ranges[kSceneSlots];
struct RejectRec { uint64_t until; uint32_t count; };
std::unordered_map<uint64_t, RejectRec> g_rejected;
uint32_t g_compact_next = 0;

// ---- per-draw BLAS for everything captured ----
struct HairBoundsPush {
  uint32_t tris,step,soup_base,bounds_base;
  float vp[16],depth[4];
  uint32_t stride,camera,bounded,pad;
};
static_assert(sizeof(HairBoundsPush)==112);
// rt_hair_groom: the strand groom of a per-draw slot's matched hair draw (groom_decide).
constexpr uint32_t kGroomMax = 64;     // groom instances per scene (one per matched hair draw)
static_assert(kGroomMax == hair_primary::kMaxGrooms, "the primary header table has one row per groom instance");
constexpr uint32_t kGroomLods = 4;
struct GroomSlot {
  uint64_t id = 0;
  int lod = -1, lod_votes = 0;
  // rt_hair_groom_crowd_ksegments (hair_groom::plan_crowd): last frame's screen size and screen-size LOD, the
  // segments per LOD, and this frame's plan (-2 none, -1 cards, else the finest LOD it may use).
  float size = 0;
  float centre[3] = {}, bounds_frame_used = 0;   // diagnostics: camera-relative bounds centre of that readback's frame
  int want = 0, crowd = -2;
  uint32_t segments[kGroomLods] = {};
  VkDeviceSize pos_off = 0, pos_size = 0, bounds_off = 0;   // points then 24 bytes of bounds, in the mesh pool
  VkDeviceSize uv_off = 0;     // rt_hair_groom_primary: per-point card UV after the bounds (0: not allocated)
  VkDeviceSize prev_off = 0;   // and last frame's points after the UVs (valid once prev_frames > 0)
  uint32_t prev_frames = 0;    // updates since the points range was allocated (the first copies garbage)
  uint64_t last_update = 0;    // round 3 M1: the build frame of the last points update (prev points are that frame's)
  bool keyed = false;          // round 3 H2: a primary key was published for this slot (kept across LOD switches)
  AsAlloc as;
  VkDeviceSize build_scratch = 0, update_scratch = 0;
  bool built = false;          // the BLAS holds a full build of this LOD
  uint64_t rec = 0;            // build record of its last build/refit (refit once that was submitted)
  Buf readback;                // host-visible: the compacted size (at 32), then a ring of bounds (24 bytes per frame)
  uint64_t bounds_frame[4] = {};   // the frame whose bounds each ring slot (at 64 + 32 * slot) holds; 0 none
  int compact = 0;             // 0 none, 1 size queried (query, query_frame), 2 compacted
  uint32_t query = 0;
  uint64_t query_frame = 0, query_rec = 0;
};
struct DynSlot {
  AsAlloc as;
  uint32_t prims = 0, cap_prims = 0;
  bool alpha = false;
  bool hair = false;
  VkDeviceSize hair_bounds_off = 0, hair_bounds_size = 0;
  HairBoundsPush hair_push{};
  VkBuildAccelerationStructureFlagsKHR flags = 0;
  uint64_t last_used = 0;
  uint32_t since_build = 0;
  VkDeviceSize build_scratch = 0, update_scratch = 0;
  uint64_t rec = 0;             // build record of its last build/refit (refit only once that was submitted)
  VkDeviceSize hold_off = 0, hold_size = 0;
  uint64_t hold_epoch = 0, hold_rec = 0;
  uint32_t hold_stride = 0;
  AlphaSnapshot hold_alpha{};
  bool hold_has_alpha = false;
  // rt_omm_dyn: the micro-map its BLAS was built with (0 none) and its per-triangle index range in the pool.
  uint64_t omm_ticket = 0;
  VkDeviceSize omm_index_off = 0, omm_index_size = 0;
  std::shared_ptr<GroomSlot> groom;   // rt_hair_groom
};
std::unordered_map<uint64_t, DynSlot> g_dyn;
// rt_cascade_casters: the BLAS of last frame's cascade-only soup, a new one every build (the
// previous one is released once no live scene slot's TLAS references it: as_destroy).
AsAlloc g_casc_as;
uint64_t g_casc_sig = 0;          // the draw list g_casc_as was built from (rt_cascade_refit)
uint32_t g_casc_tris = 0, g_casc_refits = 0;
VkDeviceSize g_casc_update_scratch = 0;
uint64_t g_casc_n_build = 0, g_casc_n_refit = 0;
uint64_t g_hold_epoch = 1;
uint64_t g_hold_why[7] = {};

// ---- this capture's draws ----
struct FrameDraw {
  uint64_t content_id = 0;
  uint64_t groom_id = 0;        // rt_hair_groom: the groom file joined to this draw's index range (0 none)
  uint32_t groom_indices = 0;   // the draw's index count (also when unmatched: the log names it)
  hair_groom::JoinMiss groom_miss = hair_groom::JoinMiss::None;
  uint64_t groom_identity = 0;  // the draw's identity without the per-frame occurrence (the join caches)
  uint64_t hp_key = 0;          // rt_hair_groom_primary: the index-range key its G-buffer draw is swapped by
  std::shared_ptr<mesh_content::Sample> content_sample;
  uint64_t mesh_key = 0;      // 0: not instanceable
  uint64_t dyn_key = 0;
  uint64_t omm_key = 0;       // rt_omm_dyn: the draw's identity without the per-frame occurrence (shared micro-maps)
  const uint8_t* rows[3] = {};   // instance 0's matrix rows in the game's mapped memory
  uint32_t row_stride[3] = {};
  uint32_t instances = 0, tris_per_instance = 0;
  uint32_t first_instance = 0;
  uint64_t vs = 0;
  bool alpha = false;
  bool cached = false, audit = false, extract = false, check = false;
  bool captured = false, cap_alpha = false;
  bool held = false;
  bool camera_fade = false;   // rt_primary_fade_class
  VkDeviceSize soup_off = 0;
  uint32_t prims = 0;
  uint32_t rec = kNone,material_record=kNone;
  uint64_t geo_key = 0, pos_key = 0;  // pool material join (native_hit::pool_keys; rt_hit_material)
  std::shared_ptr<omm::NativePlainCapture> omm_material;
  std::vector<Range> ranges;
};
std::vector<FrameDraw> g_draws;
bool g_content_on = false;
uint64_t g_content_captures = 0;
mesh_content::Captures g_content_ids;

// Only existing host mappings. Never stage a GPU copy or hash XFB/world-space
// positions. Hold the tracker lock through reads so unmap cannot race them.
void content_sample(const BvhDraw& d, FrameDraw& fd) {
  const auto& ip = *d.inst;
  if (!ip.pos_float3 || ip.pos_binding >= CmdState::kMaxVertexBindings) return;
  const auto& vb = d.s->vb[ip.pos_binding];
  const uint64_t vsize = geo_buffer_size(vb.buffer);
  const uint64_t isize = d.args.kind == 1 ? geo_buffer_size(d.s->ib) : 0;
  std::shared_lock lock(tracker().mu);
  auto bytes = [&](VkBuffer b, uint64_t size, uint64_t offset) -> mesh_content::Bytes {
    const auto bm = tracker().buffer_mem.find(b);
    if (bm == tracker().buffer_mem.end() || offset >= size || bm->second.second > UINT64_MAX - offset) return {};
    const auto mp = tracker().mapped.find(bm->second.first);
    const auto range = tracker().mapped_ranges.find(bm->second.first);
    const auto start = bm->second.second + offset;
    if (mp == tracker().mapped.end() || range == tracker().mapped_ranges.end() ||
        start < range->second.first || start >= range->second.second) return {};
    return {mp->second + start, (std::min)(size - offset, range->second.second - start)};
  };
  if (vb.offset > UINT64_MAX - ip.pos_offset) return;
  mesh_content::Input in{};
  in.positions = bytes(vb.buffer, vsize, vb.offset + ip.pos_offset);
  in.stride = ip.pos_stride ? ip.pos_stride : uint32_t(vb.stride);
  in.count = d.args.count;
  in.vertex_offset = d.args.kind == 1 ? int64_t(d.args.vertex_offset) : int64_t(d.args.first);
  if (d.args.kind == 1) {
    in.index_bytes = d.s->ib_type == VK_INDEX_TYPE_UINT32 ? 4 :
        d.s->ib_type == VK_INDEX_TYPE_UINT16 ? 2 : d.s->ib_type == VK_INDEX_TYPE_UINT8_EXT ? 1 : 0;
    if (!in.index_bytes || d.s->ib_offset > UINT64_MAX - uint64_t(d.args.first) * in.index_bytes) return;
    in.indices = bytes(d.s->ib, isize, d.s->ib_offset + uint64_t(d.args.first) * in.index_bytes);
  } else if (d.args.kind != 0) return;
  mesh_content::Sample sample;
  if (!mesh_content::sample(in, sample)) return;
  fd.content_id = mesh_content::hash(sample);
  fd.content_sample = std::make_shared<mesh_content::Sample>(sample);
}
std::unordered_map<uint64_t, uint32_t> g_dyn_seen;   // dyn key -> occurrences this capture
uint32_t g_audits_this_frame = 0;
bool g_capture_all_meshes = false;

// ---- host ring: TLAS instances, InstInfo, job matrices; written at record, matrices at submit ----
Buf g_ring;
bool g_ring_transfer_source=false;
uint32_t g_ring_inst = 0, g_ring_mats = 0;   // capacity per region
VkDeviceSize g_region_bytes = 0, g_info_off = 0, g_mat_off = 0;

struct Fixup {
  uint8_t* dst;
  uint32_t count, dst_stride;
  const uint8_t* src[3];
  uint32_t src_stride[3];
  bool instance;              // a TLAS instance (mask byte set from the result) or a job matrix
  uint8_t sample[48];         // instance 0's rows at record time (late-write diagnostic)
  uint64_t log_key = 0, log_vs = 0;
  uint32_t log_tris = 0, log_first = 0, log_instance = 0;
  uint64_t log_frame = 0;
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
vram_compact::RecordPins g_compact_records; // under g_fix_mu; pending copies cannot lose drop detection
vram_compact::UncertainCommands g_pool_uncertain; // under g_fix_mu
sun_coverage::CommandOwners g_rec_owners;
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
void compact_record_pin(uint64_t id, bool pin) {
  std::lock_guard<std::mutex> lock(g_fix_mu);
  if (pin) g_compact_records.retain(id); else g_compact_records.release(id);
}

// ---- GPU-side bookkeeping ----
Buf g_status;                 // host-visible validation results
uint32_t g_status_next = 0;
VkQueryPool g_compact_qp = VK_NULL_HANDLE;
enum JobKind : uint8_t { kJobExtract, kJobCheck, kJobAudit };
// `gen`: the mesh the job or query was recorded for. A mesh evicted and created
// again under the same key (its buffer rewritten) is another mesh: an old
// batch's compacted size must not size the new one's compaction (a copy into a
// too small range overwrites the neighbouring BLAS in the chunk).
struct Job { uint64_t key, gen; uint32_t status; JobKind kind; bool cross_instance; bool moving; };   // moving: camera moved this build
struct CompactRead { uint64_t key, gen; uint32_t slot; };
// A compaction copy: the mesh switches to `c` only once the batch's done word shows
// the copy executed (a dropped command buffer never wrote it).
struct Swap { uint64_t key, gen; AsAlloc c; bool relocation = false; AsAlloc source; };
struct JobBatch {
  uint64_t frame;
  uint32_t done_slot;
  std::vector<Job> jobs;
  std::vector<CompactRead> compact;
  std::vector<Swap> swaps;
  uint64_t rec = 0;   // build record of the command buffer (dropped: the batch is lost at once)
  bool relocation = false;
  uint64_t command = 0;
};
constexpr uint32_t kDoneMagic = 0x42564844u;   // the batch's done word
std::vector<JobBatch> g_batches;

// TLAS: one per scene slot (rtscene.cpp), so a pass that traces an older
// build's TLAS still has that build's InstInfo, and no build rewrites a TLAS
// the GPU may still be tracing. The refit reads the previous slot's TLAS
// (src) and writes this slot's (dst).
Buf g_scratch;
struct TlasSlot {
  Buf buf;
  VkAccelerationStructureKHR as = VK_NULL_HANDLE;
  uint32_t cap = 0;
  VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
};
TlasSlot g_tlas[kSceneSlots];
TlasSlot g_omm_tlas[kSceneSlots];
Buf g_omm_instances[kSceneSlots],g_omm_rows[kSceneSlots],g_omm_replacements[kSceneSlots];
uint64_t g_omm_records[kSceneSlots]={};
VkCommandBuffer g_omm_commands[kSceneSlots]={};
uint64_t g_omm_recordings[kSceneSlots]={};
uint32_t g_omm_expected[kSceneSlots]={};
const uint32_t* g_omm_mapped_stamp[kSceneSlots]={};
struct OmmPipeline {VkDescriptorSetLayout dsl{};VkPipelineLayout layout{};VkPipeline pipeline{};uint64_t generation{};};
OmmPipeline g_omm_clone,g_omm_validate;
std::vector<VkPipeline> g_omm_old_pipelines;
uint32_t g_tlas_cap_hint = 0;   // instances the newest TLAS storage was sized for
int g_tlas_prev = -1;           // slot of the last TLAS build (the refit source)
uint64_t g_tlas_prev_rec = 0;   // its build record (refit only once submitted)
uint32_t g_slot_n[kSceneSlots] = {};   // instances each slot's ring region holds (fault reports)
uint64_t g_slot_frame[kSceneSlots] = {};
// rt_cascade_casters: each slot's cascade instance and the soup its BLAS came from (replay pack,
// bvh_replay_region). tris 0: the slot's build had none.
struct SlotCascade { uint32_t instance = 0, tris = 0, stride = 0; VkBuffer xfb = VK_NULL_HANDLE; uint64_t frame = 0, sig = 0; };
SlotCascade g_slot_casc[kSceneSlots];
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
VkDescriptorSetLayout g_hair_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_hair_pl = VK_NULL_HANDLE;
VkPipeline g_hair_pipe = VK_NULL_HANDLE;
uint64_t g_hair_gen = 0;
VkPipeline g_hold_pack_pipe = VK_NULL_HANDLE;
uint64_t g_hold_pack_gen = 0;

// ---- statistics (logged every rt_timing_frames builds) ----
struct Stats {
  uint64_t builds = 0, draws = 0, cached_draws = 0, static_inst = 0, dyn_draws = 0, static_tris = 0, dyn_tris = 0;
  uint64_t new_meshes = 0, new_tris = 0, ready = 0, rejected[kRejCount] = {}, checks_ok = 0, audits_ok = 0, lost = 0;
  uint64_t lost_magic = 0, lost_timeout = 0, lost_logged = 0;
  uint64_t evict_destroy = 0, evict_write = 0, evict_lru = 0, compactions = 0, compact_saved = 0;
  uint64_t dyn_refit = 0, dyn_rebuild = 0, dyn_new = 0, budget_deferred = 0, tlas_refit = 0;
  uint64_t hold_pack_jobs = 0, hold_pack_bytes = 0, hold_copy_jobs = 0, hold_copy_bytes = 0, hold_pack_unavailable = 0;
  uint64_t not_inst_pattern = 0, not_inst_stream = 0, not_inst_count = 0;
  uint64_t fix_applied = 0, fix_late = 0, fix_bad = 0, fix_stale = 0;
  float err_max_ok = 0;
  // Audits recorded while the camera moved: a cached mesh's TLAS matrices (read at submit)
  // against the same frame's raster capture of that draw (docs/INN_ROTATION_MATCH.md).
  uint64_t audits_moving_ok = 0, audits_moving_failed = 0;
  float err_max_moving = 0;
  // rt_omm (docs/P4_OMM.md 1): alpha draws seen at classify / with an audited native plain capture;
  // alpha-tested triangles per build (static pool instances, per-draw BLAS), of the static ones with a
  // capture, and the ones whose instance range got an attached micromap BLAS; variants baked / reused /
  // declined, accelerated scene builds recorded / declined.
  uint64_t omm_draws = 0, omm_captured = 0, omm_alpha_static_tris = 0, omm_alpha_dyn_tris = 0, omm_capture_tris = 0;
  uint64_t omm_range_tris = 0, omm_baked = 0, omm_reused = 0, omm_declined = 0, omm_scenes = 0, omm_scene_declined = 0;
  // rt_omm_dyn: masked per-draw draws/triangles seen, with a micro-map attached, baked (entries, triangles), not
  // baked for the per-build or memory budget or a failure, accelerated scenes built.
  uint64_t omm_dyn_draws = 0, omm_dyn_tris = 0, omm_dyn_att_draws = 0, omm_dyn_att_tris = 0, omm_dyn_baked = 0;
  uint64_t omm_dyn_bake_tris = 0, omm_dyn_deferred = 0, omm_dyn_failed = 0, omm_dyn_scenes = 0, omm_dyn_evicted = 0;
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

bool ensure_hair_pipeline() {
  Bg3ShaderBlob blob{};
  if(!shader_get("rt/hair_bounds.hlsl","main","cs_6_6",&blob))return false;
  if(g_hair_pipe && g_hair_gen==blob.generation)return true;
  auto& vk=g_dd->vk;
  if(!g_hair_dsl){
    VkDescriptorSetLayoutBinding b[3];for(uint32_t i=0;i<3;++i)b[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;ci.bindingCount=3;ci.pBindings=b;
    if(vk.CreateDescriptorSetLayout(g_dd->device,&ci,nullptr,&g_hair_dsl)!=VK_SUCCESS)return false;
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(HairBoundsPush)};
    VkPipelineLayoutCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pc.setLayoutCount=1;pc.pSetLayouts=&g_hair_dsl;
    pc.pushConstantRangeCount=1;pc.pPushConstantRanges=&range;
    if(vk.CreatePipelineLayout(g_dd->device,&pc,nullptr,&g_hair_pl)!=VK_SUCCESS)return false;
  }
  VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=blob.size_bytes;mi.pCode=blob.code;
  VkShaderModule module{};if(vk.CreateShaderModule(g_dd->device,&mi,nullptr,&module)!=VK_SUCCESS)return false;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main"};ci.layout=g_hair_pl;
  VkPipeline pipe{};const auto result=vk.CreateComputePipelines(g_dd->device,VK_NULL_HANDLE,1,&ci,nullptr,&pipe);
  vk.DestroyShaderModule(g_dd->device,module,nullptr);if(result!=VK_SUCCESS)return false;
  defer_destroy(BG3_DEFER_PIPELINE,uint64_t(g_hair_pipe));g_hair_pipe=pipe;g_hair_gen=blob.generation;return true;
}

bool ensure_hold_pack_pipeline() {
  if (!ensure_hair_pipeline()) return false;
  Bg3ShaderBlob blob{};
  if (!shader_get("rt/hold_pack.hlsl", "main", "cs_6_6", &blob)) return false;
  if (g_hold_pack_pipe && g_hold_pack_gen == blob.generation) return true;
  auto& vk = g_dd->vk;
  VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mi.codeSize = blob.size_bytes; mi.pCode = blob.code;
  VkShaderModule module{};
  if (vk.CreateShaderModule(g_dd->device, &mi, nullptr, &module) != VK_SUCCESS) return false;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main"};
  ci.layout = g_hair_pl;
  VkPipeline pipeline{};
  const auto result = vk.CreateComputePipelines(g_dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);
  vk.DestroyShaderModule(g_dd->device, module, nullptr);
  if (result != VK_SUCCESS) return false;
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_hold_pack_pipe));
  g_hold_pack_pipe = pipeline; g_hold_pack_gen = blob.generation;
  return true;
}


// ---- rt_hair_groom (docs/RTX_HAIR_INTEGRATION.md) ----
// A matched native hair draw also gets its strand groom: points moved each frame from the draw's soup triangles
// (rt/hair_groom_update.hlsl), an opaque linear swept sphere BLAS (refit; compacted after its first build) and an
// instance in the scene slot's groom TLAS, which only the secondary ray queries trace (rt_common.hlsli). The groom
// files' arrays are uploaded once into the mesh pool, never repacked while native hair is on (pool_repack_begin).
bool g_groom_on = false;                 // this capture: device feature, rt_hair_groom and native hair
// UV join (draws whose index buffer is not host data): per draw identity, the verdict once its soup readback
// landed (miss None: matched; UvPending while the readback is in flight; UvMismatch: no candidate matched).
struct GroomUvState { uint64_t id = 0; hair_groom::JoinMiss miss = hair_groom::JoinMiss::UvPending; };
std::unordered_map<uint64_t, GroomUvState> g_groom_uv;
constexpr uint32_t kGroomUvJobs = 16;      // readbacks in flight at most (256 bytes each)
struct GroomUvJob { uint64_t identity = 0, frame = 0, rec = 0; uint32_t count = 0; bool used = false; };
GroomUvJob g_groom_uv_jobs[kGroomUvJobs];
Buf g_groom_uv_rb;                         // host-visible, kGroomUvJobs x 32 x (u, v)
std::unordered_set<uint32_t> g_groom_uv_logged;
// Draw identity (index range) -> groom index id (0: none) and the join verdict.
std::unordered_map<uint64_t, std::pair<uint64_t, hair_groom::JoinMiss>> g_groom_join;
hair_primary::LoadGuard g_groom_load;   // level-load boundary for the join caches (the G-buffer depth image)
struct GroomAsset {
  // One LOD's arrays in the mesh pool [base, base + size): uploaded when a slot first selects it, evicted
  // (oldest first) when the groom budget needs room.
  struct Lod { VkDeviceSize tri = 0, bary = 0, radius = 0, seg = 0, segtri = 0, base = 0, size = 0; uint32_t points = 0, segments = 0;
               float keep = 1; bool ready = false; uint64_t last_used = 0, upload_frame = 0, upload_rec = 0; };
  std::vector<Lod> lods;
  uint32_t indices = 0;
  bool failed = false;
};
// Mesh-pool bytes held by grooms (LOD arrays and slot points) against common/hair_groom_budget.h pool_budget.
VkDeviceSize g_groom_pool_used = 0;
uint64_t g_groom_budget_refusals = 0;
uint64_t g_groom_soup_refused = 0;
std::vector<std::shared_ptr<GroomSlot>> g_groom_seen;   // this frame's groom slots (next frame's crowd plan)   // matched draws without a live soup range this frame (held, not captured)
std::unordered_map<uint64_t, GroomAsset> g_groom_assets;
struct GroomUpload { Buf staging; VkDeviceSize dst, bytes; GroomAsset::Lod* lod = nullptr; };
std::vector<GroomUpload> g_groom_uploads;   // recorded at the start of this build's GPU section
VkDescriptorSetLayout g_groom_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_groom_pl = VK_NULL_HANDLE;
VkPipeline g_groom_pipe = VK_NULL_HANDLE;
uint64_t g_groom_gen = 0;
VkQueryPool g_groom_qp = VK_NULL_HANDLE;
std::vector<uint32_t> g_groom_free_queries;
TlasSlot g_groom_tlas[kSceneSlots];
Buf g_groom_inst[kSceneSlots];
Buf g_groom_scratch;
struct GroomPush { uint32_t points, soup_base, stride, tri_off, bary_off, pos_off, bounds_off, uv_off, prev_off, pad[3]; };
static_assert(sizeof(GroomPush) == 48);
struct GroomStats {
  uint64_t matched = 0, builds = 0, refits = 0, compacted = 0, unmatched = 0, lod_waits = 0, lod_drops = 0;
  uint32_t max_instances = 0;   // the most groom instances of one scene in the window
  uint32_t lod[kGroomLods] = {};
  uint64_t miss[size_t(hair_groom::JoinMiss::Count)] = {};
} g_groom_st;
uint64_t g_groom_stats_start = 0;
std::unordered_set<uint32_t> g_groom_logged_counts;
std::unordered_set<uint64_t> g_groom_logged_unmatched;   // (index count, verdict) of unmatched hair draws, logged once

bool ensure_groom_pipeline() {
  Bg3ShaderBlob blob{};
  if (!shader_get("rt/hair_groom_update.hlsl", "main", "cs_6_6", &blob)) return false;
  if (g_groom_pipe && g_groom_gen == blob.generation) return true;
  auto& vk = g_dd->vk;
  if (!g_groom_dsl) {
    VkDescriptorSetLayoutBinding b[2];
    for (uint32_t i = 0; i < 2; ++i) b[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR; ci.bindingCount = 2; ci.pBindings = b;
    if (vk.CreateDescriptorSetLayout(g_dd->device, &ci, nullptr, &g_groom_dsl) != VK_SUCCESS) return false;
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GroomPush)};
    VkPipelineLayoutCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pc.setLayoutCount = 1; pc.pSetLayouts = &g_groom_dsl; pc.pushConstantRangeCount = 1; pc.pPushConstantRanges = &range;
    if (vk.CreatePipelineLayout(g_dd->device, &pc, nullptr, &g_groom_pl) != VK_SUCCESS) return false;
  }
  if (!g_groom_qp) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
    qi.queryCount = kGroomMax;
    if (vk.CreateQueryPool(g_dd->device, &qi, nullptr, &g_groom_qp) != VK_SUCCESS) return false;
    for (uint32_t q = kGroomMax; q-- > 0;) g_groom_free_queries.push_back(q);
  }
  VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; mi.codeSize = blob.size_bytes; mi.pCode = blob.code;
  VkShaderModule module{};
  if (vk.CreateShaderModule(g_dd->device, &mi, nullptr, &module) != VK_SUCCESS) return false;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main"};
  ci.layout = g_groom_pl;
  VkPipeline pipe{};
  const auto result = vk.CreateComputePipelines(g_dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipe);
  vk.DestroyShaderModule(g_dd->device, module, nullptr);
  if (result != VK_SUCCESS) return false;
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_groom_pipe));
  g_groom_pipe = pipe; g_groom_gen = blob.generation;
  return true;
}

void groom_release(GroomSlot& g, uint64_t now) {
  if (g.pos_size) {
    g_pending_free.push_back({now + kGroomFreeLag, -1, g.pos_off, g.pos_size});
    g_groom_pool_used -= (std::min)(g_groom_pool_used, g.pos_size);
  }
  as_destroy(g.as, kGroomFreeLag);
  if (g.readback.b) free_buf(g.readback);
  if (g.compact == 1) g_groom_free_queries.push_back(g.query);
  g = GroomSlot{};
}

// The mesh pool's largest capacity (pool_alloc's cap: configured maximum and the device-budget share).
VkDeviceSize groom_pool_capacity() {
  return (std::min)(VkDeviceSize((std::max)(cfg_int("rt_mesh_pool_max_mb", 3072), 64)) << 20,
                    VkDeviceSize(vram_cap(vram_guard::kMeshPool, UINT64_MAX)));
}

// Room for `need` more groom bytes in the pool budget, evicting LODs unused for 120 frames (oldest first).
bool groom_make_room(VkDeviceSize need, uint64_t now) {
  // Keyed by the index into refs: the ids are 64-bit hashes, so id * kGroomLods + LOD would lose their top bits.
  std::vector<hair_groom::Resident> residents;
  std::vector<GroomAsset::Lod*> refs;
  for (auto& [id, a] : g_groom_assets)
    for (size_t i = 0; i < a.lods.size(); ++i)
      if (a.lods[i].ready) {
        residents.push_back({refs.size(), a.lods[i].size, a.lods[i].last_used, a.lods[i].last_used == now});
        refs.push_back(&a.lods[i]);
      }
  std::vector<uint64_t> evict;
  if (!hair_groom::plan_evictions(residents, g_groom_pool_used, need, hair_groom::pool_budget(groom_pool_capacity()), now, 120, evict)) {
    if (g_groom_budget_refusals++ % 300 == 0)
      log("[hair groom] pool budget full (%.0f of %.0f MB, need %.1f MB): this draw keeps the card path",
          double(g_groom_pool_used) / 1048576.0, double(hair_groom::pool_budget(groom_pool_capacity())) / 1048576.0,
          double(need) / 1048576.0);
    return false;
  }
  for (uint64_t key : evict) {
    GroomAsset::Lod& l = *refs[size_t(key)];
    g_pending_free.push_back({now + kGroomFreeLag, -1, l.base, l.size});
    g_groom_pool_used -= (std::min)(g_groom_pool_used, l.size);
    l = {}; // clear upload proof too: an evicted range must never become ready again
  }
  return true;
}

// The groom's index entry (no arrays yet); nullptr when the file is missing (never retried).
GroomAsset* groom_asset(uint64_t id, uint32_t indices) {
  GroomAsset& a = g_groom_assets[id];
  if (a.failed) return nullptr;
  if (!a.lods.empty()) return a.indices == indices ? &a : nullptr;
  const hair_groom::Loaded* src = hair_groom::find(id, indices);
  if (!src) { a.failed = true; return nullptr; }
  a.indices = indices;
  a.lods.resize((std::min)(src->asset.lod_count, kGroomLods));
  return &a;
}

// One LOD's arrays in the mesh pool, uploaded by this build's GPU section on first use (the file is read from
// disk then and dropped after staging). nullptr: over budget or no pool room now (card path; retried later),
// or the file changed (never retried).
const GroomAsset::Lod* groom_lod(VkCommandBuffer cmd, uint64_t id, GroomAsset& a, int lod, uint64_t now) {
  GroomAsset::Lod& d = a.lods[size_t(lod)];
  if (d.ready) { d.last_used = now; return &d; }
  if (d.size) {
    if (!d.upload_rec) return nullptr; // still staged; no copy recording yet
    const auto state = hair_primary::readback_state(d.upload_frame, life_gpu_done(), rec_submitted(d.upload_rec));
    if (state == hair_primary::ReadbackState::Wait) return nullptr;
    if (state == hair_primary::ReadbackState::Ready) { d.ready = true; d.last_used = now; return &d; }
    // Discarded upload: retire its range and stage a fresh copy, never build from it.
    g_pending_free.push_back({now + kGroomFreeLag, -1, d.base, d.size});
    g_groom_pool_used -= (std::min)(g_groom_pool_used, d.size);
    d = {};
  }
  const hair_groom::Loaded* src = hair_groom::find(id, a.indices);
  std::vector<uint8_t> bytes;
  hair_groom::Lod l;   // round 3 M6: only this LOD's bytes are read and validated
  if (!src || !hair_groom::read_lod(*src, uint32_t(lod), bytes, l)) {
    log("[hair groom] %016llx: file unreadable or changed since startup, groom off for it", (unsigned long long)id);
    a.failed = true;
    return nullptr;
  }
  auto up = [](VkDeviceSize v) { return (v + 15) / 16 * 16; };
  GroomAsset::Lod n;
  n.points = l.points; n.segments = l.segments; n.keep = l.keep;
  VkDeviceSize total = 0;
  n.tri = total; total += up(VkDeviceSize(l.points) * 4);
  n.bary = total; total += up(VkDeviceSize(l.points) * 8);
  n.radius = total; total += up(VkDeviceSize(l.points) * 4);
  n.seg = total; total += up(VkDeviceSize(l.segments) * 4);
  n.segtri = total; total += up(VkDeviceSize(l.segments) * 4);
  if (!groom_make_room(total, now)) return nullptr;
  VkDeviceSize base = 0;
  if (!pool_alloc(cmd, total, &base)) return nullptr;
  GroomUpload u{};
  if (!make_buf(u.staging, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "hair groom upload")) {
    g_pending_free.push_back({now + kGroomFreeLag, -1, base, total});
    return nullptr;
  }
  std::memcpy(u.staging.map + n.tri, l.triangle, size_t(l.points) * 4);
  std::memcpy(u.staging.map + n.bary, l.bary, size_t(l.points) * 8);
  std::memcpy(u.staging.map + n.radius, l.radius, size_t(l.points) * 4);
  std::memcpy(u.staging.map + n.seg, l.segment, size_t(l.segments) * 4);
  for (uint32_t s = 0; s < l.segments; ++s) {   // the card triangle of each segment's first point (hit remap)
    uint32_t first, tri;
    std::memcpy(&first, l.segment + s, 4);
    std::memcpy(&tri, l.triangle + first, 4);
    std::memcpy(u.staging.map + n.segtri + VkDeviceSize(s) * 4, &tri, 4);
  }
  n.tri += base; n.bary += base; n.radius += base; n.seg += base; n.segtri += base;
  n.base = base; n.size = total; n.last_used = now;
  d = n;
  u.dst = base; u.bytes = total; u.lod = &d;
  g_groom_uploads.push_back(u);
  g_groom_pool_used += total;
  log("[hair groom] %016llx LOD %d uploaded: %.1f MB in the mesh pool at %llu (grooms %.0f of %.0f MB)", (unsigned long long)id,
      lod, double(total) / 1048576.0, (unsigned long long)base, double(g_groom_pool_used) / 1048576.0,
      double(hair_groom::pool_budget(groom_pool_capacity())) / 1048576.0);
  return nullptr; // usable only after this upload recording was submitted and completed
}

VkAccelerationStructureGeometryKHR groom_geometry(const GroomAsset::Lod& l, VkDeviceSize pos_off,
                                                  VkAccelerationStructureGeometryLinearSweptSpheresDataNV& d) {
  d = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_LINEAR_SWEPT_SPHERES_DATA_NV};
  d.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
  d.vertexData.deviceAddress = g_pool.addr + pos_off;
  d.vertexStride = 12;
  d.radiusFormat = VK_FORMAT_R32_SFLOAT;
  d.radiusData.deviceAddress = g_pool.addr + l.radius;
  d.radiusStride = 4;
  d.indexType = VK_INDEX_TYPE_UINT32;
  d.indexData.deviceAddress = g_pool.addr + l.seg;
  d.indexStride = 4;
  d.indexingMode = VK_RAY_TRACING_LSS_INDEXING_MODE_SUCCESSIVE_NV;
  d.endCapsMode = VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_CHAINED_NV;
  VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  g.pNext = &d;
  g.geometryType = VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV;
  g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  return g;
}
constexpr VkBuildAccelerationStructureFlagsKHR kGroomFlags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                                                            VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |
                                                            VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
VkAccelerationStructureBuildSizesInfoKHR groom_sizes(const GroomAsset::Lod& l) {
  VkAccelerationStructureGeometryLinearSweptSpheresDataNV d;
  VkAccelerationStructureGeometryKHR g = groom_geometry(l, 0, d);
  VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  bi.flags = kGroomFlags;
  bi.geometryCount = 1;
  bi.pGeometries = &g;
  VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  g_dd->vk.GetAccelerationStructureBuildSizesKHR(g_dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &l.segments, &sz);
  return sz;
}

// Screen-size LOD from the points' bounds of an earlier frame (camera-relative: |centre| is the distance).
// The angular size extent / distance picks LOD 0 above 0.09 rad (about a tenth of a 50-degree view), LOD 1
// above 0.03, LOD 2 (or the last) below; a change needs 30 consecutive frames (no flicker at a threshold).
// The bounds come from the newest ring slot whose frame completed on the GPU (never the slot a copy may still write).
int groom_pick_lod(GroomSlot& g, int lods, uint64_t gpu_done) {
  int want = g.lod < 0 ? 0 : g.lod;
  int slot = -1;
  for (int i = 0; i < 4; ++i)
    if (g.bounds_frame[i] && gpu_done > g.bounds_frame[i] + 1 && (slot < 0 || g.bounds_frame[i] > g.bounds_frame[slot])) slot = i;
  if (g.readback.map && g.lod >= 0 && slot >= 0) {
    uint32_t w[6];
    std::memcpy(w, g.readback.map + 64 + 32 * slot, sizeof w);
    float lo[3], hi[3];
    bool ok = true;
    for (int c = 0; c < 3; ++c) {
      auto decode = [](uint32_t u) { u = (u & 0x80000000u) ? (u & 0x7FFFFFFFu) : ~u; float f; std::memcpy(&f, &u, 4); return f; };
      lo[c] = decode(w[c]); hi[c] = decode(w[3 + c]);
      ok = ok && std::isfinite(lo[c]) && std::isfinite(hi[c]) && lo[c] <= hi[c];
    }
    if (ok) {
      const float ex = hi[0] - lo[0], ey = hi[1] - lo[1], ez = hi[2] - lo[2];
      const float cx = (hi[0] + lo[0]) / 2, cy = (hi[1] + lo[1]) / 2, cz = (hi[2] + lo[2]) / 2;
      const float size = std::sqrt(ex * ex + ey * ey + ez * ez) / (std::max)(std::sqrt(cx * cx + cy * cy + cz * cz), 0.1f);
      want = size > 0.09f ? 0 : size > 0.03f ? 1 : 2;
      g.size = size;
      g.centre[0] = cx, g.centre[1] = cy, g.centre[2] = cz;
      g.bounds_frame_used = float(g.bounds_frame[slot]);
    }
  }
  want = (std::min)(want, lods - 1);
  g.want = want;
  if (g.lod < 0) return want;
  if (want == g.lod) { g.lod_votes = 0; return g.lod; }
  return ++g.lod_votes >= 30 ? (g.lod_votes = 0, want) : g.lod;
}

// Reads the index range of an indexed draw from existing host mappings (as content_sample) and returns its groom
// index id when a groom file has that id and index count; cached per draw identity. 0: none or not host data.
uint64_t groom_join(const BvhDraw& d, uint64_t identity, hair_groom::JoinMiss* miss) {
  const auto cached = g_groom_join.find(identity);
  if (cached != g_groom_join.end()) { *miss = cached->second.second; return cached->second.first; }
  uint64_t id = 0;
  bool mapped = false;
  const CmdState* s = d.s;
  const uint32_t ib = s->ib_type == VK_INDEX_TYPE_UINT32 ? 4 : s->ib_type == VK_INDEX_TYPE_UINT16 ? 2 : 0;
  if (ib && d.args.kind == 1 && s->ib) {
    const uint64_t size = geo_buffer_size(s->ib);
    const uint64_t offset = s->ib_offset + uint64_t(d.args.first) * ib, bytes = uint64_t(d.args.count) * ib;
    std::shared_lock lock(tracker().mu);
    const auto bm = tracker().buffer_mem.find(s->ib);
    if (bm != tracker().buffer_mem.end() && offset <= size && bytes <= size - offset) {
      const auto mp = tracker().mapped.find(bm->second.first);
      const auto range = tracker().mapped_ranges.find(bm->second.first);
      const uint64_t start = bm->second.second + offset;
      if (mp != tracker().mapped.end() && range != tracker().mapped_ranges.end() && start >= range->second.first &&
          start + bytes <= range->second.second) {
        mapped = true;
        const uint8_t* p = mp->second + start;
        const uint64_t candidate = hair_groom::index_content_id([&](uint32_t i) {
          if (ib == 2) { uint16_t v; std::memcpy(&v, p + uint64_t(i) * 2, 2); return uint32_t(v); }
          uint32_t v; std::memcpy(&v, p + uint64_t(i) * 4, 4); return v;
        }, d.args.count);
        if (hair_groom::find(candidate, d.args.count)) id = candidate;
        else if (g_groom_logged_counts.insert(d.args.count).second && g_groom_logged_counts.size() <= 64) {
          log("[hair groom] draw with %u indices: index id %016llx has no groom file", d.args.count, (unsigned long long)candidate);
          hair_groom::note_seen(d.args.count, candidate);   // hair_seen.txt: convert this style first
        }
      }
    }
  }
  *miss = hair_groom::join_miss(true, true, mapped, id != 0);
  if (!mapped) {
    // Not host data: the UV join (g_groom_uv) decides from this draw's captured soup; never cached here.
    const auto uv = g_groom_uv.find(identity);
    if (uv == g_groom_uv.end()) return 0;
    *miss = uv->second.miss;
    return uv->second.id;
  }
  g_groom_join[identity] = {id, *miss};
  return id;
}

bool ensure_ring(uint32_t inst, uint32_t mats) {
  const bool transfer=omm::available_for(g_dd);
  if (g_ring.b && inst <= g_ring_inst && mats <= g_ring_mats && (!transfer || g_ring_transfer_source)) return true;
  uint32_t ni = (std::max)({inst + inst / 2, g_ring_inst, 4096u}), nm = (std::max)({mats + mats / 2, g_ring_mats, 1024u});
  VkDeviceSize ib = VkDeviceSize(ni) * 64, fb = VkDeviceSize(ni) * 16, mb = VkDeviceSize(nm) * 48;
  auto up = [](VkDeviceSize v) { return (v + 255) / 256 * 256; };
  VkDeviceSize region = up(ib) + up(fb) + up(mb);
  Buf nb;
  if (!make_buf(nb, region * kRingSlots,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    (transfer?VK_BUFFER_USAGE_TRANSFER_SRC_BIT:0u),
                true, "instance/InstInfo ring"))
    return false;
  if (g_ring.b) free_buf(g_ring);   // slots in flight keep the old one until it is freed (deferred)
  g_ring = nb;
  g_ring_transfer_source=transfer;
  g_ring_inst = ni;
  g_ring_mats = nm;
  g_region_bytes = region;
  g_info_off = up(ib);
  g_mat_off = up(ib) + up(fb);
  log("[bvh] instance ring: %u instances, %u job matrices per build (%llu KB x %u, host-visible)", ni, nm,
      (unsigned long long)(region >> 10), kRingSlots);
  return true;
}

bool ensure_tlas(uint32_t slot, uint32_t n,TlasSlot* extra=nullptr) {
  TlasSlot& ts = extra?*extra:g_tlas[slot];
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
  // The groom TLAS of a slot is labelled apart (fault dumps: the strand passes trace it, the core passes the scene's).
  if (!make_buf(nb, sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false,
                extra ? "hair groom TLAS slot" : "split TLAS slot"))
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
  ts.sizes = sz;
  g_tlas_cap_hint = cap;
  if (!extra && int(slot) == g_tlas_prev) g_tlas_valid = false;
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

VkAccelerationStructureGeometryKHR hair_geometry(VkDeviceAddress bounds) {
  VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  g.geometryType=VK_GEOMETRY_TYPE_AABBS_KHR;
  g.flags=VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
  g.geometry.aabbs.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
  g.geometry.aabbs.data.deviceAddress=bounds;g.geometry.aabbs.stride=24;return g;
}
VkAccelerationStructureBuildSizesInfoKHR hair_sizes(uint32_t prims,VkBuildAccelerationStructureFlagsKHR flags) {
  const auto g=hair_geometry(0);
  VkAccelerationStructureBuildGeometryInfoKHR b{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  b.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;b.flags=flags;b.geometryCount=1;b.pGeometries=&g;
  VkAccelerationStructureBuildSizesInfoKHR size{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  g_dd->vk.GetAccelerationStructureBuildSizesKHR(g_dd->device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&b,&prims,&size);return size;
}

bool ensure_omm_pipeline(OmmPipeline& out,const char* file,uint32_t count) {
  Bg3ShaderBlob code{};if(!shader_get(file,"main","cs_6_6",&code))return false;
  if(out.pipeline && out.generation==code.generation)return true;
  auto& vk=g_dd->vk;
  if(!out.dsl) {
    VkDescriptorSetLayoutBinding bindings[3]{};
    for(uint32_t i=0;i<count;++i)bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;ci.bindingCount=count;ci.pBindings=bindings;
    if(vk.CreateDescriptorSetLayout(g_dd->device,&ci,nullptr,&out.dsl)!=VK_SUCCESS)return false;
  }
  if(!out.layout) {
    const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,16};
    VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};ci.setLayoutCount=1;ci.pSetLayouts=&out.dsl;ci.pushConstantRangeCount=1;ci.pPushConstantRanges=&push;
    if(vk.CreatePipelineLayout(g_dd->device,&ci,nullptr,&out.layout)!=VK_SUCCESS)return false;
  }
  VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=code.size_bytes;mi.pCode=code.code;
  VkShaderModule module{};if(vk.CreateShaderModule(g_dd->device,&mi,nullptr,&module)!=VK_SUCCESS)return false;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.layout=out.layout;
  ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main"};
  VkPipeline pipeline{};const auto result=vk.CreateComputePipelines(g_dd->device,VK_NULL_HANDLE,1,&ci,nullptr,&pipeline);
  vk.DestroyShaderModule(g_dd->device,module,nullptr);if(result!=VK_SUCCESS)return false;
  // Recorded commands retain old code until explicit idle at device teardown.
  if(out.pipeline)g_omm_old_pipelines.push_back(out.pipeline);
  out.pipeline=pipeline;out.generation=code.generation;return true;
}
bool ensure_omm_buffer(Buf& out,VkDeviceSize bytes,VkBufferUsageFlags usage,bool host,const char* name) {
  if(out.b && out.size>=bytes)return true;
  Buf next{};if(!make_buf(next,bytes,usage,host,name))return false;
  if(out.b)free_buf(out);out=next;return true;
}
bool build_omm_scene(VkCommandBuffer cmd,uint32_t slot,uint32_t instances,VkDeviceSize ring_base,uint64_t record) {
  g_omm_records[slot]=0;
  const auto& ranges=g_omm_ranges[slot];if(ranges.empty() || !instances || !g_ring_transfer_source)return false;
  const VkDeviceSize ib=VkDeviceSize(instances)*64u,ob=VkDeviceSize(instances)*8u,rb=VkDeviceSize(ranges.size())*32u;
  VkPhysicalDeviceProperties properties{};g_dd->inst->GetPhysicalDeviceProperties(g_dd->physical,&properties);
  const auto groups=(uint64_t(instances)+63u)/64u;
  if(ib>properties.limits.maxStorageBufferRange || ob>properties.limits.maxStorageBufferRange ||
     rb>properties.limits.maxStorageBufferRange || ranges.size()>UINT32_MAX || groups>properties.limits.maxComputeWorkGroupCount[0])return false;
  const auto* state=cmd_state(cmd);if(!state || !state->recording)return false;
  if(!ensure_omm_pipeline(g_omm_clone,"rt/omm_scene_clone.hlsl",2) ||
     !ensure_omm_pipeline(g_omm_validate,"rt/omm_scene_validate.hlsl",3) ||
     !ensure_tlas(slot,instances,&g_omm_tlas[slot]) ||
     !ensure_omm_buffer(g_omm_instances[slot],ib,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,false,"OMM scene instances") ||
     !ensure_omm_buffer(g_omm_replacements[slot],ob,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true,"OMM scene replacements") ||
     !ensure_omm_buffer(g_omm_rows[slot],rb,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,true,"OMM scene source stamps"))return false;
  std::vector<uint64_t> addresses(instances,0);std::vector<uint32_t> rows(ranges.size()*8,0);
  for(uint32_t i=0;i<ranges.size();++i) {
    const auto& range=ranges[i];
    if(range.first>instances || range.count>instances-range.first || !range.blas.address ||
       range.epochs!=ranges[0].epochs || range.epoch_bytes!=ranges[0].epoch_bytes)return false;
    for(uint32_t j=0;j<range.count;++j)addresses[range.first+j]=range.blas.address;
    rows[i*8+4]=range.baked.source_epoch_offset;
  }
  std::memcpy(g_omm_replacements[slot].map,addresses.data(),size_t(ob));
  std::memcpy(g_omm_rows[slot].map,rows.data(),size_t(rb));
  auto& vk=g_dd->vk;
  barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,kXfer,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
  VkBufferCopy copy{ring_base,0,ib};vk.CmdCopyBuffer(cmd,g_ring.b,g_omm_instances[slot].b,1,&copy);
  for(uint32_t i=0;i<ranges.size();++i){copy={0,VkDeviceSize(i)*32,16};vk.CmdCopyBuffer(cmd,ranges[i].baked.data,g_omm_rows[slot].b,1,&copy);}
  barrier(cmd,kXfer,VK_ACCESS_TRANSFER_WRITE_BIT,kCS,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  const VkDescriptorBufferInfo buffers[]={{g_omm_instances[slot].b,0,ib},{g_omm_replacements[slot].b,0,ob}};
  VkWriteDescriptorSet writes[2]{};for(uint32_t i=0;i<2;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=buffers+i;}
  const uint32_t push[]={instances,0,0,0};vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,g_omm_clone.pipeline);
  vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,g_omm_clone.layout,0,2,writes);vk.CmdPushConstants(cmd,g_omm_clone.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,push);vk.CmdDispatch(cmd,(instances+63)/64,1,1);
  barrier(cmd,kCS,VK_ACCESS_SHADER_WRITE_BIT,kAS,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
  VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};geometry.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;
  geometry.geometry.instances={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};geometry.geometry.instances.data.deviceAddress=g_omm_instances[slot].addr;
  VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};build.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;build.flags=kTlasFlags;build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;build.geometryCount=1;build.pGeometries=&geometry;build.dstAccelerationStructure=g_omm_tlas[slot].as;
  VkAccelerationStructureBuildSizesInfoKHR size{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};vk.GetAccelerationStructureBuildSizesKHR(g_dd->device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&instances,&size);
  if(!ensure_scratch(size.buildScratchSize))return false;
  // Baseline TLAS and this clone share scratch. Order AS scratch writes too,
  // not only the compute writes of the instance-address patch.
  barrier(cmd,kAS|kCS|kXfer,VK_ACCESS_MEMORY_WRITE_BIT,kAS,
          VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
  build.scratchData.deviceAddress=scratch_at(0);const VkAccelerationStructureBuildRangeInfoKHR range{instances,0,0,0};const auto* rp=&range;
  vk.CmdBuildAccelerationStructuresKHR(cmd,1,&build,&rp);g_omm_records[slot]=record;
  g_omm_commands[slot]=cmd;g_omm_recordings[slot]=state->recording;return true;
}

// ---- rt_omm_dyn (live, default 0; docs/P4_OMM.md 11.2c): micro-maps on masked per-draw BLAS ----
// A per-draw BLAS (camp grass: wind VS, refit every frame) whose record reads an offline mask through its mip-0
// view (rt_vt_alpha_masks + rt_alpha_fine_lod: the same hard-cut test for every ray) gets a four-state micro-map
// (subdivision 4, common/omm_mask_bake.h), baked once per (draw identity without the per-frame occurrence, mask
// view, cuts, uv scale, triangle count) from the soup's uvs and reused across frames and draws. Every frame, before
// the build or refit, shaders/rt/omm_bake_mask.hlsl check writes each triangle's micro-map index: itself while its
// uv bits equal the bake's snapshot, else FULLY_UNKNOWN_OPAQUE (the any-hit decides): exact per triangle whatever
// the identity matched (wind moves positions only; uvs are checked, not assumed). In the ordinary TLAS these
// instances carry DISABLE_OPACITY_MICROMAPS: it is the reference scene of the OMM variants and their omm check. A
// copy with the flag cleared is the accelerated TLAS (g_omm_tlas, the static variants' slot; bvh_omm_commit writes
// the 'OMM3' proof). rt_omm_reference=1 traces the reference: the in-launch A/B without a rebuild. With the key on
// the static pool-variant path is not used (0.06% of camp's alpha triangles). Micro-maps live until teardown.
struct DynMicromap {
  uint32_t tris = 0;
  uint64_t ticket = 0;
  VkImageView mask = VK_NULL_HANDLE;
  Buf data, snapshot, descriptors;   // data/descriptors: micro-map build inputs, freed once the bake ran
  uint64_t rec = 0;                  // build record of the bake's command buffer
  bool ready = false;                // that command buffer was submitted (latched: records are pruned)
  uint64_t last_use = 0;             // rt_omm_dyn_evict_frames: frame a draw last matched it (bake frame first)
};
std::unordered_map<uint64_t, DynMicromap> g_omm_dyn;
VkDeviceSize g_omm_dyn_bytes = 0;    // our buffers of every entry (the backend's micro-map storage is separate)
uint64_t g_omm_dyn_cached_tris = 0;
bg3::omm_dyn::Refusal g_omm_dyn_refusal;   // rt_omm_dyn_retry_frames: backend refusals that prove the next ones
std::vector<uint64_t> g_omm_dyn_tickets[kSceneSlots];   // micro-maps the slot's accelerated TLAS traverses
struct OmmDynPipe { VkDescriptorSetLayout dsl{}; VkPipelineLayout layout{}; VkPipeline bake{}, check{}; uint64_t gen_bake{}, gen_check{}; };
OmmDynPipe g_omm_dyn_pipe;
Buf g_omm_dyn_stats, g_omm_dyn_readback;   // device-local counters of omm_bake_mask.hlsl, their host copy
bool g_omm_dyn_stats_zero = false;
constexpr VkDeviceSize kOmmDynStatsBytes = 32;
struct OmmDynPush { uint32_t soup_off, stride, tris, groups_x; float cut_lo, cut_hi, scale_u, scale_v; uint32_t index_off, mask_w, mask_h, pad; };
static_assert(sizeof(OmmDynPush) == 48, "omm_bake_mask.hlsl Push");
constexpr VkBuildAccelerationStructureFlagsKHR kOmmDynBlasFlags =
    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DISABLE_OPACITY_MICROMAPS_EXT | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_OPACITY_MICROMAP_UPDATE_EXT;
constexpr uint32_t kOmmDynDisable = VK_GEOMETRY_INSTANCE_DISABLE_OPACITY_MICROMAPS_EXT;

bool ensure_omm_dyn_pipe() {
  Bg3ShaderBlob bake{}, check{};
  if (!shader_get("rt/omm_bake_mask.hlsl", "bake", "cs_6_6", &bake) || !shader_get("rt/omm_bake_mask.hlsl", "check", "cs_6_6", &check))
    return false;
  OmmDynPipe& p = g_omm_dyn_pipe;
  // Retry each allocation independently: the stats buffer may survive a failed readback allocation.
  if (!g_omm_dyn_stats.b) {
    if (!make_buf(g_omm_dyn_stats, kOmmDynStatsBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, "OMM dyn counters"))
      return false;
    g_omm_dyn_stats_zero = true;
  }
  if (!g_omm_dyn_readback.b) {
    if (!make_buf(g_omm_dyn_readback, kOmmDynStatsBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "OMM dyn counters readback"))
      return false;
    std::memset(g_omm_dyn_readback.map, 0, size_t(kOmmDynStatsBytes));
  }
  if (p.bake && p.check && p.gen_bake == bake.generation && p.gen_check == check.generation) return true;
  auto& vk = g_dd->vk;
  if (!p.dsl) {
    VkDescriptorSetLayoutBinding b[6]{};
    for (uint32_t i = 0; i < 6; ++i)
      b[i] = {i, i == 1 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR; ci.bindingCount = 6; ci.pBindings = b;
    if (vk.CreateDescriptorSetLayout(g_dd->device, &ci, nullptr, &p.dsl) != VK_SUCCESS) return false;
  }
  if (!p.layout) {
    const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(OmmDynPush)};
    VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ci.setLayoutCount = 1; ci.pSetLayouts = &p.dsl; ci.pushConstantRangeCount = 1; ci.pPushConstantRanges = &push;
    if (vk.CreatePipelineLayout(g_dd->device, &ci, nullptr, &p.layout) != VK_SUCCESS) return false;
  }
  auto make = [&](const Bg3ShaderBlob& code, const char* entry, VkPipeline& out) {
    VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; mi.codeSize = code.size_bytes; mi.pCode = code.code;
    VkShaderModule module{};
    if (vk.CreateShaderModule(g_dd->device, &mi, nullptr, &module) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; ci.layout = p.layout;
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, entry};
    const auto r = vk.CreateComputePipelines(g_dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &out);
    vk.DestroyShaderModule(g_dd->device, module, nullptr);
    return r == VK_SUCCESS;
  };
  VkPipeline nb{}, nc{};
  if (!make(bake, "bake", nb) || !make(check, "check", nc)) {
    if (nb) vk.DestroyPipeline(g_dd->device, nb, nullptr);
    return false;
  }
  // Recorded commands may still use the old pipelines: destroyed at device teardown (after idle).
  if (p.bake) g_omm_old_pipelines.push_back(p.bake);
  if (p.check) g_omm_old_pipelines.push_back(p.check);
  p.bake = nb; p.check = nc; p.gen_bake = bake.generation; p.gen_check = check.generation;
  return true;
}

void omm_dyn_dispatch(VkCommandBuffer cmd, VkPipeline pipe, const OmmDynPush& pc, VkBuffer soup, VkDeviceSize soup_bytes,
                      const DynMicromap& e, uint32_t gx, uint32_t gy) {
  auto& vk = g_dd->vk;
  const VkDescriptorBufferInfo buffers[5] = {{soup, 0, soup_bytes},
                                             {e.data.b ? e.data.b : e.snapshot.b, 0, VK_WHOLE_SIZE},
                                             {e.snapshot.b, 0, VK_WHOLE_SIZE},
                                             {g_omm_dyn_stats.b, 0, kOmmDynStatsBytes},
                                             {g_pool.b, 0, g_pool.size}};
  const VkDescriptorImageInfo image{VK_NULL_HANDLE, e.mask, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet w[6]{};
  for (uint32_t i = 0; i < 6; ++i) {
    w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w[i].dstBinding = i; w[i].descriptorCount = 1;
    w[i].descriptorType = i == 1 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    if (i == 1) w[i].pImageInfo = &image; else w[i].pBufferInfo = &buffers[i == 0 ? 0 : i - 1];
  }
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_omm_dyn_pipe.layout, 0, 6, w);
  vk.CmdPushConstants(cmd, g_omm_dyn_pipe.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  vk.CmdDispatch(cmd, gx, gy, 1);
}

// The micro-map attachment of a per-draw BLAS: `count` triangles (the build's, or the capacity for sizing), each
// indexed through the pool range at `index` (written by the check this frame).
bool omm_dyn_attachment(uint64_t ticket, uint32_t tris, uint32_t count, VkDeviceAddress index,
                        VkAccelerationStructureTrianglesOpacityMicromapEXT* out, VkMicromapUsageEXT* usage) {
  if (!omm::attachment(ticket, tris, *out) || out->usageCountsCount != 1) return false;
  *usage = out->pUsageCounts[0];
  usage->count = count;
  out->pUsageCounts = usage;
  out->indexType = VK_INDEX_TYPE_UINT32;
  out->indexBuffer.deviceAddress = index;
  out->indexStride = 4;
  out->baseTriangle = 0;
  return true;
}

VkAccelerationStructureBuildSizesInfoKHR blas_sizes_omm(uint32_t prims, uint32_t stride, VkBuildAccelerationStructureFlagsKHR flags,
                                                        const VkAccelerationStructureTrianglesOpacityMicromapEXT* omm_attach) {
  VkAccelerationStructureGeometryKHR g = tri_geometry(0, prims, stride, false);
  g.geometry.triangles.pNext = omm_attach;
  VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  bi.flags = flags;
  bi.geometryCount = 1;
  bi.pGeometries = &g;
  VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  g_dd->vk.GetAccelerationStructureBuildSizesKHR(g_dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &prims, &sz);
  return sz;
}

void omm_dyn_free(DynMicromap& e) {
  for (Buf* b : {&e.data, &e.snapshot, &e.descriptors})
    if (b->b) { g_omm_dyn_bytes -= (std::min)(g_omm_dyn_bytes, b->size); free_buf(*b); }
}

// The accelerated scene of a slot whose per-draw BLAS carry micro-maps: the instance array copied after the
// matrices were fixed up at submit, the `enable` instances' flags rewritten without DISABLE_OPACITY_MICROMAPS.
bool build_omm_dyn_scene(VkCommandBuffer cmd, uint32_t slot, uint32_t instances, VkDeviceSize ring_base, uint64_t record,
                         const std::vector<std::pair<uint32_t, uint32_t>>& enable) {
  g_omm_records[slot] = 0;
  if (enable.empty() || !instances || !g_ring_transfer_source) return false;
  const auto* state = cmd_state(cmd);
  if (!state || !state->recording) return false;
  const VkDeviceSize ib = VkDeviceSize(instances) * 64u;
  if (!ensure_tlas(slot, instances, &g_omm_tlas[slot]) ||
      !ensure_omm_buffer(g_omm_instances[slot], ib, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, false, "OMM scene instances"))
    return false;
  auto& vk = g_dd->vk;
  barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT,
          kXfer, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
  const VkBufferCopy copy{ring_base, 0, ib};
  vk.CmdCopyBuffer(cmd, g_ring.b, g_omm_instances[slot].b, 1, &copy);
  barrier(cmd, kXfer, VK_ACCESS_TRANSFER_WRITE_BIT, kXfer, VK_ACCESS_TRANSFER_WRITE_BIT);
  // VkAccelerationStructureInstanceKHR: the word at byte 52 is instanceShaderBindingTableRecordOffset (24 bits,
  // 0 here) and the 8 flag bits; the mask (byte 48, set at submit) is untouched.
  for (const auto& [index, flags] : enable)
    if (index < instances) vk.CmdFillBuffer(cmd, g_omm_instances[slot].b, VkDeviceSize(index) * 64u + 52u, 4, flags << 24);
  // The baseline TLAS and this one share scratch: order AS scratch writes too.
  barrier(cmd, kAS | kCS | kXfer, VK_ACCESS_MEMORY_WRITE_BIT, kAS,
          VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_READ_BIT);
  VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  geometry.geometry.instances = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
  geometry.geometry.instances.data.deviceAddress = g_omm_instances[slot].addr;
  VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR; build.flags = kTlasFlags; build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  build.geometryCount = 1; build.pGeometries = &geometry; build.dstAccelerationStructure = g_omm_tlas[slot].as;
  VkAccelerationStructureBuildSizesInfoKHR size{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  vk.GetAccelerationStructureBuildSizesKHR(g_dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &instances, &size);
  if (!ensure_scratch(size.buildScratchSize)) return false;
  build.scratchData.deviceAddress = scratch_at(0);
  const VkAccelerationStructureBuildRangeInfoKHR range{instances, 0, 0, 0};
  const auto* rp = &range;
  vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &build, &rp);
  g_omm_records[slot] = record; g_omm_commands[slot] = cmd; g_omm_recordings[slot] = state->recording;
  return true;
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
  native_hit::pool_evicted(m.geo_key);
  if(m.omm)for(const auto& v:*m.omm)omm::release_ticket(v.baked.ticket);
  as_destroy(m.as);
  if (m.pool_size) g_pending_free.push_back({frame_index(), -1, m.pool_off, m.pool_size});
  if (m.basis_size) { g_pending_free.push_back({frame_index(), -1, m.basis_off, m.basis_size}); g_basis_bytes -= (std::min)(g_basis_bytes, m.basis_size); }
  if (m.basis_over) { g_basis_over -= (std::min)(g_basis_over, uint64_t(1)); g_basis_over_bytes -= (std::min)(g_basis_over_bytes, m.basis_over); }
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
bool compact_status_ready() {
  return g_compact_status.b || make_buf(g_compact_status, 32, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                       true, "VRAM compact completion");
}

void pool_repack_begin(VkCommandBuffer cmd, uint64_t rec, uint64_t now) {
  const VkDeviceSize initial = VkDeviceSize((std::max)(cfg_int("rt_mesh_pool_mb", 256), 16)) << 20;
  VkDeviceSize target = vram_compact::pool_target(g_pool_ra.used, g_pool.size, initial);
  // Outlast the existing 240-frame build-result watchdog, plus the scene ring
  // and six-present range retirement. Demand must remain low for the full window.
  const bool quiet = g_pool_quiet.ready(now, cfg_int("rt_vram_compact", 0) && target,
                                         240 + kSceneSlots + 6, target);
  if (!cfg_int("rt_vram_compact",0) || !target) return;
  static uint64_t next_log=0;
  auto deferred=[&](const char* why){
    if(now>=next_log){
      next_log=now+240+kSceneSlots+6;
      log("[bvh] mesh pool shrink deferred: %s (%.1f / %.0f MB, target %.1f MB)",why,
          double(g_pool_ra.used)/1048576.0,double(g_pool.size)/1048576.0,double(target)/1048576.0);
    }
  };
  if (!quiet) {deferred("low-demand window");return;}
  target = (std::max)(target, VkDeviceSize(g_pool_quiet.peak_target));
  if (g_pool_copy.target.b || !g_old_pools.empty()) {deferred("copy or old scene retirement");return;}
  if (geo_native_hair_enabled()) {deferred("native hair ranges");return;}
  {
    std::lock_guard<std::mutex> fl(g_fix_mu);
    if (!g_pool_uncertain.empty()) {deferred("uncertain pool recording");return;}
    // Never copy ahead of another command's registered but unsubmitted pool
    // work, including legacy batches whose watchdog already discarded results.
    for (const auto& [other, records] : g_rec_cmd)
      if (other != cmd && !records.empty()) {deferred("unsubmitted pool work");return;}
  }
  // Never copy data an outstanding extraction/basis/hold job can still write.
  if (!g_batches.empty()) {deferred("outstanding build results");return;}
  for (const auto& p : g_pending_free) if (p.chunk < 0) {deferred("pending pool ranges");return;}
  for (const auto& [key, d] : g_dyn) if (d.hair_bounds_size) {deferred("hair bounds ranges");return;}
  for (const auto& [key, m] : g_meshes) if (m.state != kReady) {deferred("unverified mesh");return;}
  if (!compact_status_ready() || !native_hit::pool_compact_freeze()) {deferred("material jobs or completion allocation");return;}
  PoolCopy copy;
  copy.ranges.init(target);
  std::vector<VkBufferCopy> regions;
  bool fits = true;
  for (const auto& [key, m] : g_meshes) {
    uint64_t position = 0, basis = 0;
    if ((m.pool_size && !copy.ranges.alloc(m.pool_size, 16, &position)) ||
        (m.basis_size && !copy.ranges.alloc(m.basis_size, 16, &basis))) { fits = false; break; }
    copy.meshes.push_back({key, m.gen, position, basis, m.pool_size, m.basis_size});
    if (m.pool_size) regions.push_back({m.pool_off, position, m.pool_size});
    if (m.basis_size) regions.push_back({m.basis_off, basis, m.basis_size});
  }
  // Dynamic hold snapshots are live pool allocations too, even with no new
  // meshes. Copy them rather than waiting forever for a static view to release them.
  for (const auto& [key,d] : g_dyn) if(d.hold_size){
    uint64_t destination=0;
    if(!copy.ranges.alloc(d.hold_size,16,&destination)){fits=false;break;}
    copy.holds.push_back({key,d.hold_off,destination,d.hold_size});
    regions.push_back({d.hold_off,destination,d.hold_size});
  }
  if (!fits || copy.ranges.used != g_pool_ra.used ||
      !vram_compact::pool_shrink_overlap(target,g_pool.size) ||
      !make_buf(copy.target, target, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, "mesh pool repack")) {
    deferred("range accounting or replacement allocation failed");
    native_hit::pool_compact_thaw();g_pool_quiet = {};return;
  }
  copy.frame = now;copy.rec = rec;copy.done_slot = 0;
  std::memset(g_compact_status.map, 0, 16);
  barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, kRW, kXfer, kRW);
  if (!regions.empty()) g_dd->vk.CmdCopyBuffer(cmd, g_pool.b, copy.target.b, uint32_t(regions.size()), regions.data());
  barrier(cmd, kXfer, VK_ACCESS_TRANSFER_WRITE_BIT, kXfer, VK_ACCESS_TRANSFER_WRITE_BIT);
  g_dd->vk.CmdFillBuffer(cmd, g_compact_status.b, 0, 4, kDoneMagic);
  barrier(cmd, kXfer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
  g_pool_copy = std::move(copy);
  compact_record_pin(rec, true);
}

void pool_repack_poll(uint64_t now) {
  if (g_pool_copy.target.b) {
    auto& copy = g_pool_copy;
    const bool done = *reinterpret_cast<const volatile uint32_t*>(g_compact_status.map) == kDoneMagic;
    const bool dropped = rec_state(copy.rec) == kRecDropped;
    // No elapsed-frame timeout: an unsubmitted/stalled copy cannot be published
    // or freed while the command buffer may still run later.
    const auto outcome = vram_compact::copy_outcome(done, dropped, cfg_int("rt_vram_compact", 0) != 0);
    if (outcome != vram_compact::CopyOutcome::Wait) {
      const bool commit = outcome == vram_compact::CopyOutcome::Publish;
      if (commit) {
        const auto before = g_pool.size;
        ++g_pool_epoch;
        for (const auto& move : copy.meshes) {
          auto it = g_meshes.find(move.key);
          if (it == g_meshes.end() || it->second.gen != move.gen) {
            if (move.position_size) copy.ranges.release(move.position, move.position_size);
            if (move.basis_size) copy.ranges.release(move.basis, move.basis_size);
            continue;
          }
          auto& m = it->second;m.pool_off = move.position;m.basis_off = move.basis;
          native_hit::pool_compact_rebase(m.geo_key, m.gen, m.pool_off, m.basis_off, g_pool_epoch);
        }
        for(const auto& move:copy.holds){
          auto it=g_dyn.find(move.key);
          if(it!=g_dyn.end() && vram_compact::hold_move_matches(it->second.hold_off,it->second.hold_size,move.source,move.bytes))
            it->second.hold_off=move.destination;
          else copy.ranges.release(move.destination,move.bytes);
        }
        // Evictions during the copy belong to the old allocator. Its complete
        // buffer remains retained until every old scene and GPU fence retires.
        std::erase_if(g_pending_free, [](const PendingFree& p) { return p.chunk < 0; });
        g_old_pools.push_back({g_pool, now});
        g_pool = copy.target;copy.target = {};g_pool_ra = std::move(copy.ranges);
        g_pool_snapshots = true;++g_pool_repacks;g_pool_saved += before - g_pool.size;
        log("[bvh] mesh pool capacity: repacked %.1f / %.0f -> %.0f MB, epoch %llu; old pool waits for scene retirement",
            double(g_pool_ra.used) / 1048576.0, double(before) / 1048576.0,
            double(g_pool.size) / 1048576.0, (unsigned long long)g_pool_epoch);
      } else {
        g_old_pools.push_back({copy.target, now});copy.target = {};
        log("[bvh] mesh pool capacity: repack discarded (%s), original pool retained", dropped ? "dropped recording" : "key off");
      }
      compact_record_pin(copy.rec, false);
      copy = {};native_hit::pool_compact_thaw();g_pool_quiet = {};
    }
  }
  for (auto it = g_old_pools.begin(); it != g_old_pools.end();) {
    if (!as_chunks::retired(it->frame, now, life_gpu_done(), rt_scene_oldest_live())) { ++it;continue; }
    free_buf(it->buf);it = g_old_pools.erase(it);
  }
}

void relocate_cached_blas(VkCommandBuffer cmd, JobBatch& batch) {
  if (!cfg_int("rt_vram_compact", 0) || g_relocating_chunk >= 0 || !batch.swaps.empty() ||
      !batch.jobs.empty() || !batch.compact.empty()) return;
  if (!compact_status_ready()) return;
  for (const auto& b : g_batches) if (!b.swaps.empty()) return;
  const size_t budget = size_t((std::max)(cfg_int("rt_mesh_compact_per_frame", 256), 0));
  std::vector<size_t> order;
  for (size_t i = 0; i < g_chunks.size(); ++i)
    if (g_chunks[i].ra.live && !g_chunks[i].ra.kept) order.push_back(i);
  std::sort(order.begin(), order.end(), [](size_t a, size_t b) {
    const double x = double(g_chunks[a].ra.live) / double(g_chunks[a].ra.cap);
    const double y = double(g_chunks[b].ra.live) / double(g_chunks[b].ra.cap);
    return x != y ? x < y : a < b;
  });
  for (size_t source : order) {
    std::vector<uint64_t> keys, sizes;
    for (const auto& [key, m] : g_meshes)
      if (m.as.chunk == int(source) && m.state == kReady && (m.compacted || !cfg_int("rt_mesh_compact", 1))) {
        keys.push_back(key);sizes.push_back(m.as.size);
      }
    if (keys.empty() || keys.size() > budget) continue;
    std::vector<vram_compact::Move> plan;
    if (!vram_compact::evacuation(g_chunks, source, sizes, plan)) continue;
    std::vector<Swap> swaps;
    bool ok = true;
    for (const auto& move : plan) {
      auto& m = g_meshes.at(keys[move.item]);
      VkDeviceSize off = 0;
      AsAlloc target;
      if (!g_chunks[move.chunk].ra.alloc(m.as.size, kAsAlign, &off)) { ok = false;break; }
      if (!as_handle(m.as.size, int(move.chunk), off, target)) { ok = false;break; }
      swaps.push_back({keys[move.item], m.gen, target, true, m.as});
    }
    if (!ok) { for (auto& s : swaps) as_destroy(s.c);return; }
    // CLONE accepts both already compacted BLAS and builds with no compaction
    // saving. Destinations have the exact aligned source size; no new chunk.
    barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, kRW, kAS, kRW);
    for (const auto& s : swaps) {
      VkCopyAccelerationStructureInfoKHR ci{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
      ci.src = g_meshes.at(s.key).as.as;ci.dst = s.c.as;ci.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
      g_dd->vk.CmdCopyAccelerationStructureKHR(cmd, &ci);
    }
    barrier(cmd, kAS, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, kAS, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    batch.swaps.insert(batch.swaps.end(), swaps.begin(), swaps.end());
    // This batch has no validation/query payload. A dedicated done word cannot
    // be recycled by the normal status ring during an arbitrarily long stall.
    batch.relocation = true;
    std::memset(g_compact_status.map + 16, 0, 16);
    compact_record_pin(batch.rec, true);
    g_relocating_chunk = int(source);
    return;
  }
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
  log("[bvh] static/dynamic BVH split %s (rt_bvh_split=%d)", ok ? "available" : "UNAVAILABLE", cfg_hot(CfgHot::rt_bvh_split));
}

void bvh_device_destroyed(DeviceData* dd) {
  if (dd != g_dd) return;
  hair_groom::flush_seen();   // the last level's styles without a groom (hair_seen.txt)
  std::lock_guard<std::mutex> lock(g_mu);
  g_content_ids.clear(); g_content_on = false; g_content_captures = 0;
  if(g_omm_clone.pipeline || g_omm_validate.pipeline || g_omm_dyn_pipe.bake)dd->vk.DeviceWaitIdle(dd->device);
  // rt_omm_dyn: the backend destroys the micro-maps themselves (omm::device_destroyed).
  for (auto& [k, e] : g_omm_dyn) { omm::release_ticket(e.ticket); omm_dyn_free(e); }
  g_omm_dyn.clear(); g_omm_dyn_bytes = 0; g_omm_dyn_cached_tris = 0; g_omm_dyn_refusal = {};
  for (auto& t : g_omm_dyn_tickets) t.clear();
  for (VkPipeline p : {g_omm_dyn_pipe.bake, g_omm_dyn_pipe.check}) if (p) dd->vk.DestroyPipeline(dd->device, p, nullptr);
  if (g_omm_dyn_pipe.layout) dd->vk.DestroyPipelineLayout(dd->device, g_omm_dyn_pipe.layout, nullptr);
  if (g_omm_dyn_pipe.dsl) dd->vk.DestroyDescriptorSetLayout(dd->device, g_omm_dyn_pipe.dsl, nullptr);
  g_omm_dyn_pipe = {};
  for (Buf* b : {&g_omm_dyn_stats, &g_omm_dyn_readback}) if (b->b) free_buf(*b);
  for(auto* p:{&g_omm_clone,&g_omm_validate}) {
    if(p->pipeline)dd->vk.DestroyPipeline(dd->device,p->pipeline,nullptr);
    if(p->layout)dd->vk.DestroyPipelineLayout(dd->device,p->layout,nullptr);
    if(p->dsl)dd->vk.DestroyDescriptorSetLayout(dd->device,p->dsl,nullptr);*p={};
  }
  for(auto p:g_omm_old_pipelines)dd->vk.DestroyPipeline(dd->device,p,nullptr);g_omm_old_pipelines.clear();
  for(uint32_t i=0;i<kSceneSlots;++i) {
    if(g_omm_tlas[i].as)dd->vk.DestroyAccelerationStructureKHR(dd->device,g_omm_tlas[i].as,nullptr);
    g_omm_tlas[i].as=VK_NULL_HANDLE;
    for(Buf* p:{&g_omm_tlas[i].buf,&g_omm_instances[i],&g_omm_rows[i],&g_omm_replacements[i]})if(p->b)free_buf(*p);
    g_omm_tlas[i]={};g_omm_records[i]=0;
    g_omm_expected[i]=0;g_omm_mapped_stamp[i]=nullptr;g_omm_commands[i]=VK_NULL_HANDLE;g_omm_recordings[i]=0;
  }
  for (auto& [k, m] : g_meshes) {
    if(m.omm)for(const auto& v:*m.omm)omm::release_ticket(v.baked.ticket);
    as_destroy(m.as);
  }
  for(auto& ranges:g_omm_ranges)ranges.clear();
  for (auto& [k, s] : g_dyn) {
    as_destroy(s.as);
    if (s.groom) as_destroy(s.groom->as);
    if (s.groom && s.groom->readback.b) free_buf(s.groom->readback);
  }
  as_destroy(g_casc_as);
  for (JobBatch& b : g_batches)
    for (Swap& s : b.swaps) as_destroy(s.c);
  g_batches.clear();
  if (g_pool_copy.target.b) free_buf(g_pool_copy.target);
  g_pool_copy = {};
  { std::lock_guard<std::mutex> fl(g_fix_mu);g_compact_records.clear();g_pool_uncertain.clear(); }
  for (auto& old : g_old_pools) free_buf(old.buf);
  g_old_pools.clear();native_hit::pool_compact_thaw();g_pool_quiet = {};
  g_pool_snapshots = false;g_pool_repacks = g_pool_saved = 0;
  g_relocated = g_relocated_bytes = 0;g_relocating_chunk = -1;
  for (const PendingFree& p : g_pending_free)
    if (p.as) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(p.as));
  g_pending_free.clear();
  for (const KeptAs& a : g_kept_as) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(a.as));
  g_kept_as.clear();
  g_meshes.clear();
  g_dyn.clear();
  g_buf_meshes.clear();
  g_any_mesh = false;
  g_basis_bytes = 0;
  g_basis_over = g_basis_over_logged = 0;
  g_basis_over_bytes = 0;
  ++g_pool_epoch;
  for (auto& c : g_chunks)
    if (c.buf.b) free_buf(c.buf);
  g_chunks.clear();
  g_chunks_freed = g_chunk_bytes_freed = 0;
  for (Buf* b : {&g_pool, &g_ring, &g_status, &g_scratch, &g_compact_status})
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
  defer_destroy(BG3_DEFER_PIPELINE,uint64_t(g_hair_pipe));
  defer_destroy(BG3_DEFER_PIPELINE,uint64_t(g_hold_pack_pipe));
  g_hold_pack_pipe=VK_NULL_HANDLE;g_hold_pack_gen=0;
  defer_destroy(BG3_DEFER_PIPELINE_LAYOUT,uint64_t(g_hair_pl));
  defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT,uint64_t(g_hair_dsl));
  g_hair_pipe=VK_NULL_HANDLE;g_hair_pl=VK_NULL_HANDLE;g_hair_dsl=VK_NULL_HANDLE;g_hair_gen=0;
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_groom_pipe));
  defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, uint64_t(g_groom_pl));
  defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, uint64_t(g_groom_dsl));
  defer_destroy(BG3_DEFER_QUERY_POOL, uint64_t(g_groom_qp));
  g_groom_pipe = VK_NULL_HANDLE; g_groom_pl = VK_NULL_HANDLE; g_groom_dsl = VK_NULL_HANDLE; g_groom_qp = VK_NULL_HANDLE;
  g_groom_gen = 0; g_groom_free_queries.clear();
  for (uint32_t i = 0; i < kSceneSlots; ++i) {
    if (g_groom_tlas[i].as) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(g_groom_tlas[i].as));
    for (Buf* b : {&g_groom_tlas[i].buf, &g_groom_inst[i]}) if (b->b) free_buf(*b);
    g_groom_tlas[i] = TlasSlot{};
  }
  if (g_groom_scratch.b) free_buf(g_groom_scratch);
  for (auto& u : g_groom_uploads) if (u.staging.b) free_buf(u.staging);
  g_groom_uploads.clear(); g_groom_assets.clear(); g_groom_join.clear(); g_groom_logged_unmatched.clear(); g_groom_seen.clear();
  g_groom_pool_used = 0; g_groom_budget_refusals = 0;
  if (g_groom_uv_rb.b) free_buf(g_groom_uv_rb);
  g_groom_uv.clear(); g_groom_uv_logged.clear();
  for (GroomUvJob& j : g_groom_uv_jobs) j = GroomUvJob{};
  defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, uint64_t(g_pl));
  defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, uint64_t(g_dsl));
  g_compact_qp = VK_NULL_HANDLE;
  g_pipe = VK_NULL_HANDLE;
  g_pl = VK_NULL_HANDLE;
  g_dsl = VK_NULL_HANDLE;
  g_on = false;
  g_dd = nullptr;
}

// rt_vram_guard breakdown and yield sizes (present thread, under this module's lock).
void bvh_vram(BvhVram* out) {
  *out = BvhVram{};
  if (!g_on) return;
  std::lock_guard<std::mutex> lock(g_mu);
  const uint64_t now = frame_index();
  for (const AsChunk& c : g_chunks) {
    if (!c.buf.b) continue;
    out->as_reserved += c.buf.size;
    out->as_live += c.ra.live;
    out->as_empty += c.ra.reclaimable() ? c.buf.size : 0;
    ++out->chunks;
  }
  for (const auto& [k, m] : g_meshes)
    if (m.last_used + 8 < now) out->lru += m.as.size;
  out->pool = g_pool.size;
  out->pool_used = g_pool_ra.used;
  out->pool_pending = g_pool_copy.target.size;
  for (const auto& old : g_old_pools) out->pool_pending += old.buf.size;
  out->pool_repacks = g_pool_repacks;out->blas_relocated = g_relocated;
}

bool bvh_split_wanted() { return g_on && !g_failed && cfg_hot(CfgHot::rt_bvh_split) != 0; }

void bvh_begin_capture(VkCommandBuffer) {
  std::lock_guard<std::mutex> lock(g_mu);
  const bool content_on = cfg_int("rt_mesh_content_id", 0) != 0;
  if (g_content_on) {
    if (g_content_captures == 2 || g_content_captures % 120 == 0 || !content_on)
      log("[mesh id] content ids: %llu meshes, %llu collisions, %llu stable across 2 captures",
          (unsigned long long)g_content_ids.current.size(), (unsigned long long)g_content_ids.collisions,
          (unsigned long long)g_content_ids.stable);
    g_content_ids.next();
  }
  if (!content_on && g_content_on) { g_content_ids.clear(); g_content_captures = 0; }
  g_content_on = content_on;
  if (content_on) ++g_content_captures;
  g_draws.clear();
  g_dyn_seen.clear();
  g_audits_this_frame = 0;
  g_capture_all_meshes = cfg_int("rt_mesh_capture", 0) != 0;
  const bool groom_key = cfg_hot(CfgHot::rt_hair_groom) != 0, lss = hair_groom::lss_enabled(), native = geo_native_hair_enabled();
  g_groom_on = groom_key && lss && native;
  // Review 2026-11-08 item 15: grooms off: their LOD arrays leave the mesh pool (freed after the strand passes' lag;
  // slots release their points as their draws are captured without a groom). A staged upload not yet recorded keeps
  // its range until it is recorded (the copy still targets it), then goes the next frame.
  if (!g_groom_on && g_groom_pool_used) {
    uint64_t freed = 0;
    const uint64_t now = frame_index();
    for (auto& [id, a] : g_groom_assets)
      for (auto& l : a.lods)
        if (l.size && l.upload_rec) {
          g_pending_free.push_back({now + kGroomFreeLag, -1, l.base, l.size});
          g_groom_pool_used -= (std::min)(g_groom_pool_used, l.size);
          freed += l.size;
          l = {};
        }
    if (freed) log("[hair groom] off: %.1f MB of groom LOD arrays released from the mesh pool", double(freed) / 1048576.0);
  }
  // The join's gate, logged when it changes (startup, key toggles, level loads that change the capture).
  static int logged_gate = -1;
  const int gate = int(groom_key) | int(lss) << 1 | int(native) << 2;
  if (groom_key && gate != logged_gate)
    log("[hair groom] join gate: rt_hair_groom 1, linear swept spheres %d, native hair capture %d (88-byte stride, alpha) -> "
        "groom joins %s", int(lss), int(native), g_groom_on ? "on" : "OFF");
  logged_gate = groom_key ? gate : -1;
}

BvhVerdict bvh_classify(VkCommandBuffer cmd, const BvhDraw& d, uint32_t* token, uint32_t* mesh_tris) {
  std::lock_guard<std::mutex> lock(g_mu);
  *token = uint32_t(g_draws.size());
  g_draws.emplace_back();
  FrameDraw& fd = g_draws.back();
  cpu_opt::DeferredRanges<Range, CmdState::kMaxVertexBindings + 1> ranges(fd.ranges, cfg_hot(CfgHot::cpu_opt2) != 0);
  fd.alpha = d.alpha;
  fd.instances = d.args.instances;
  fd.first_instance = d.args.first_instance;
  fd.vs = d.vs;
  const CmdState* s = d.s;
  const bool indexed = d.args.kind == 1;
  // The draw's identity across frames (per-draw BLAS of captured draws). The
  // instance-rate bindings change offset every frame (the game's upload ring),
  // so only binding 0 (positions in every BG3 layout) and the index range count.
  uint64_t dk = mix(mix(mix(uint64_t(d.pipe_id), d.args.count), d.args.first), uint64_t(int64_t(d.args.vertex_offset)));
  dk = mix(mix(dk, uint64_t(s->vb[0].buffer)), s->vb[0].offset);
  if (indexed) dk = mix(mix(dk, uint64_t(s->ib)), s->ib_offset);
  dk = mix(dk, d.alpha);
  fd.dyn_key = mix(dk, g_dyn_seen[dk]++);   // the n-th draw with that identity this frame
  fd.omm_key = dk;
  if (g_groom_on) {
    // Every draw gets its join verdict (cheap: the count filter before any index read); only hair draws use it.
    fd.groom_indices = d.args.count;
    fd.groom_identity = dk;
    fd.hp_key = hair_primary_draw_key(s, d.args);
    if (!indexed) fd.groom_miss = hair_groom::JoinMiss::NotIndexed;
    else if (!hair_groom::count_known(d.args.count)) fd.groom_miss = hair_groom::JoinMiss::CountUnknown;
    else fd.groom_id = groom_join(d, dk, &fd.groom_miss);
  }
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
  std::array<cpu_opt::InstanceRowSource,3> row_sources{};
  for(unsigned k=0;k<3;++k){const auto& vb=s->vb[ip.row_binding[k]];
    row_sources[k]={uint64_t(vb.buffer),vb.offset,vb.stride,ip.row_stride[k],ip.row_offset[k]};
  }
  const auto row_prepared=cpu_opt::instance_rows(row_sources,d.args.first_instance,d.args.instances,
    buffer_resource_epoch(),cfg_hot(CfgHot::cpu_opt3)!=0,
    [](uint64_t buffer){return geo_buffer_size(VkBuffer(buffer));},
    [](uint64_t buffer,uint64_t offset){return mapped_ptr(VkBuffer(buffer),offset);});
  for(unsigned k=0;k<3;++k){fd.rows[k]=row_prepared.rows[k];fd.row_stride[k]=row_prepared.strides[k];}
  if(!row_prepared.valid){++g_st.not_inst_stream;return BVH_CAPTURE;}
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
    ranges.push_back({vb.buffer, lo, lo + VkDeviceSize(d.args.count) * (std::max)(vs, VkDeviceSize(64))});
  }
  if (indexed) {
    if (!s->ib) {
      ++g_st.not_inst_stream;
      return BVH_CAPTURE;
    }
    VkDeviceSize isz = s->ib_type == VK_INDEX_TYPE_UINT32 ? 4 : s->ib_type == VK_INDEX_TYPE_UINT8_EXT ? 1 : 2;
    mk = mix(mix(mix(mk, uint64_t(s->ib)), s->ib_offset), s->ib_type);
    VkDeviceSize lo = s->ib_offset + VkDeviceSize(d.args.first) * isz;
    ranges.push_back({s->ib, lo, lo + VkDeviceSize(d.args.count) * isz});
  }
  if (!mk) mk = 1;
  fd.mesh_key = mk;
  if (native_hit::enabled()) native_hit::pool_keys(ip, *s, d.args, &fd.geo_key, &fd.pos_key);
  fd.tris_per_instance = d.args.count / 3;
  // Capture while the exact native draw is still bound, including cached draws
  // that skip transform feedback. Build-time tracker state belongs to another
  // pipeline and cannot recover this material's sampler/descriptor identity.
  if(d.alpha && ip.uv_ok && cfg_hot(CfgHot::rt_omm) && omm::available_for(g_dd)) {
    AlphaFs fs{};AlphaSnapshot source{};omm::NativePlainCapture captured{};
    ++g_st.omm_draws;
    if(alpha_lookup(s->fs,&fs) && alpha_capture_source(cmd,fs,0,fd.tris_per_instance,&source) &&
       omm::capture_native_plain(cmd,fs,d.vs,source,&captured)) {
      fd.omm_material=std::make_shared<omm::NativePlainCapture>(std::move(captured));++g_st.omm_captured;
    }
  }
  auto capture = [&](BvhVerdict verdict) {
    if (g_content_on) content_sample(d, fd);
    return verdict;
  };
  if (g_capture_all_meshes) return capture(BVH_CAPTURE);
  auto it = g_meshes.find(mk);
  if (it == g_meshes.end()) {
    auto rj = g_rejected.find(mk);
    if (rj == g_rejected.end() || frame_index() >= rj->second.until) { fd.extract = true; ranges.commit(); }
    return capture(BVH_CAPTURE);
  }
  Mesh& m = it->second;
  if (m.tris != fd.tris_per_instance) return capture(BVH_CAPTURE);   // cannot happen for one key; be safe
  if (m.state == kNeedCheck && !m.check_pending) fd.check = true;
  if (m.state != kReady) return capture(BVH_CAPTURE);
  fd.cached = true;
  *mesh_tris = m.tris;
  // A periodic audit: the cached draw is captured once more and compared.
  const int audit_frames = cfg_hot(CfgHot::rt_mesh_audit_frames);
  if (audit_frames > 0 && frame_index() >= m.last_audit + uint64_t(audit_frames) &&
      g_audits_this_frame < uint32_t((std::max)(cfg_hot(CfgHot::rt_mesh_audits), 0))) {
    ++g_audits_this_frame;
    m.last_audit = frame_index();
    fd.audit = true;
    return capture(BVH_CACHED_AUDIT);
  }
  return BVH_CACHED;
}

// The mesh pool as the hit-material basis jobs write it (native_hit_pool.h). The epoch changes
// whenever the buffer is replaced (grown by copy): a job recorded before a change may have
// written the old buffer, so its result is discarded (hit_material.cpp pool_jobs_poll).
bool bvh_pool_target(VkBuffer* buffer, VkDeviceSize* bytes, uint64_t* epoch) {
  std::lock_guard<std::mutex> lock(g_mu);
  *buffer = g_pool.b; *bytes = g_pool.size; *epoch = g_pool_epoch;
  return g_pool.b != VK_NULL_HANDLE;
}

void bvh_set_record(uint32_t token, uint32_t rec) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (token < g_draws.size()) g_draws[token].rec = rec;
}

void bvh_captured(uint32_t token, VkDeviceSize offset, uint32_t prims, bool alpha, uint32_t rec, uint32_t materialRecord) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (token >= g_draws.size()) return;
  FrameDraw& fd = g_draws[token];
  fd.captured = true;
  if (g_content_on && fd.content_sample) g_content_ids.add(fd.content_id, *fd.content_sample);
  fd.soup_off = offset;
  fd.prims = prims;
  fd.cap_alpha = alpha;
  fd.material_record=alpha?kNone:materialRecord;
  if (!fd.cached) fd.rec = alpha ? rec : kNone;
}

void bvh_set_camera_fade(uint32_t token) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (token < g_draws.size()) g_draws[token].camera_fade = true;
}

void bvh_note_skipped(uint32_t token) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (token < g_draws.size()) g_draws[token].captured = false;
}

// After the RT source pass, outside it, in the game's command buffer.
static NativeOmmVariant* native_omm_variant(VkCommandBuffer cmd,uint64_t record,Mesh& mesh,
    const omm::NativePlainCapture& source,uint32_t stride) {
  if(mesh.state!=kReady || !mesh.alpha || !mesh.pool_size || !omm::available_for(g_dd))return nullptr;
  const omm::MaterialKey material{source.fs,source.vs,source.source.imageId,source.source.viewId,source.sampler.sampler.id};
  if(mesh.omm)for(auto it=mesh.omm->begin();it!=mesh.omm->end();++it)if(it->material==material) {
    const int producer_state=bvh_rec_state(it->record);
    if(producer_state<0 || producer_state==2) {
      omm::release_ticket(it->baked.ticket);mesh.omm->erase(it);break;
    }
    if(!omm::cached_bake_ready(it->record,record,producer_state)){++g_st.omm_declined;return nullptr;}
    if(!omm::register_ticket_use(cmd,it->baked.ticket)){++g_st.omm_declined;return nullptr;}
    ++g_st.omm_reused;return &*it;
  }
  // Own only this mesh's immutable post-VS range, not a whole historic pool.
  // The producer records the copy after validating/allocating all inputs, so a
  // rejected material cannot leave a command referring to a freed snapshot.
  omm::PlainVertices vertices{};omm::PlainInput input{};
  ++g_st.omm_declined;   // undone below once the variant exists
  if(!omm::allocate_plain_vertices(cmd,mesh.pool_size,&vertices) ||
     !omm::make_native_plain_input(cmd,source,vertices.buffer,vertices.bytes,0,stride,mesh.tris,vertices.owner,&input))return nullptr;
  input.copy_source=g_pool.b;input.copy_source_bytes=g_pool.size;input.copy_offset=mesh.pool_off;
  NativeOmmVariant fresh{};
  fresh.material=material;fresh.record=record;
  const auto ticket=omm::record_plain(cmd,input,&fresh.baked);
  if(!ticket)return nullptr;
  if(!omm::record_blas(cmd,ticket,vertices.address,vertices.bytes,stride,mesh.tris,&fresh.blas)) {
    omm::release_ticket(ticket);return nullptr;
  }
  if(!mesh.omm)mesh.omm=std::make_unique<std::vector<NativeOmmVariant>>();
  --g_st.omm_declined;++g_st.omm_baked;
  mesh.omm->push_back(fresh);return &mesh.omm->back();
}

bool bvh_build(VkCommandBuffer cmd, const RtCapture& cap, void (*stamp)(VkCommandBuffer, uint32_t), uint32_t slot,
               BvhResult* out) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_on || g_failed || slot >= kSceneSlots || !ensure_pipeline()) return false;
  const bool cpu_opt = cfg_hot(CfgHot::cpu_opt1) != 0;
  // No pointers or GPU state. Only capacity survives a build, including failures.
  struct CpuScratch {
    cpu_opt::BuildKeys present, dynamic;
    std::vector<uint32_t> cached, flags;
    std::vector<uint64_t> refs;
    std::vector<std::pair<uint32_t, uint64_t>> dyn;
    void clear() { present.clear(); dynamic.clear(); cached.clear(); flags.clear(); refs.clear(); dyn.clear(); }
  };
  static CpuScratch retained;
  CpuScratch local;
  CpuScratch& scratch = cpu_opt ? retained : local;
  scratch.clear();
  const DeviceData* dd = g_dd;
  const uint64_t now = frame_index();
  const uint32_t stride = cap.stride, tri_bytes = 3 * stride;
  const uint32_t hold_frames = uint32_t((std::clamp)(cfg_int("rt_dyn_hold", 4), 0, 120));
  const bool moving = rt_camera_moving();
  const uint32_t hold_max_prims = uint32_t((std::max)(cfg_int("rt_dyn_hold_max_prims", 8192), 0));
  const bool compact_holds_requested = dyn_hold_compact_requested(geo_native_hair_enabled(),
      cfg_int("rt_hair_pool_trim", 1) != 0, cfg_int("rt_scene_compact_holds", 1) != 0, hold_frames, moving);
  const bool compact_holds = compact_holds_requested && ensure_hold_pack_pipeline();
  if (compact_holds_requested && !compact_holds) ++g_st.hold_pack_unavailable;
  if (moving || !hold_frames) {
    ++g_hold_epoch;
    for (auto& [key, s] : g_dyn) if (s.hold_size) {
      g_pending_free.push_back({now, -1, s.hold_off, s.hold_size});
      s.hold_size = 0;
      s.hold_rec = 0;
    }
  }
  const VkDeviceAddress soup_addr = rt_buffer_address(cap.xfb);
  const bool alpha_test = cfg_hot(CfgHot::rt_alpha_test) != 0;
  // rt_hit_material_pool_alpha (live, off): alpha-tested pool meshes (Base_AlphaTest_VT STI shares the G-buffer pair
  // 2d2ac3c3/14678345 with Base_VT) get a UV/basis range and a pool row too; their InstInfo rec keeps the alpha
  // record, the row slot + 1 goes to meta bits 16..31 (native_hit_records.h nh_pool_alpha_slot_shift).
  const bool pool_alpha_rows = native_hit::enabled() && cfg_int("rt_hit_material_pool_alpha", 0) != 0;
  const int rebuild_frames = (std::max)(cfg_int("rt_rebuild_frames", 60), 1);
  // This command buffer's build record (bvh_on_submit marks it submitted).
  const uint64_t rec = bvh_rec_register(cmd);
  // rtscene handed us a genuinely retired slot. Inspect its immutable GPU
  // result and end-of-use invalidation flag BEFORE resetting its generation.
  if(const auto* proof=g_omm_mapped_stamp[slot];proof && g_omm_expected[slot] &&
      (proof[0]!=g_omm_expected[slot] || proof[32]!=g_omm_expected[slot] ||
       (proof[35]!=0x4f4d4d32u && proof[35]!=0x4f4d4d33u) || proof[36]!=0u)) {
    for(const auto& old:g_omm_ranges[slot]) {
      auto found=g_meshes.find(old.mesh);
      if(found==g_meshes.end() || found->second.gen!=old.generation || !found->second.omm)continue;
      auto& variants=*found->second.omm;
      for(auto it=variants.begin();it!=variants.end();) {
        if(it->baked.ticket==old.baked.ticket){omm::release_ticket(it->baked.ticket);it=variants.erase(it);}
        else ++it;
      }
    }
  }
  g_omm_expected[slot]=0;g_omm_mapped_stamp[slot]=nullptr;
  g_omm_ranges[slot].clear();
  g_omm_dyn_tickets[slot].clear();
  g_omm_records[slot]=0;
  const VkBuildAccelerationStructureFlagsKHR dyn_flags =
      VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
      VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
  const VkBuildAccelerationStructureFlagsKHR mesh_flags =
      VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
      (cfg_int("rt_mesh_compact", 1) ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR : 0);
  const uint64_t new_budget = uint64_t((std::max)(cfg_int("rt_mesh_new_tris", 4000000), 1000));
  g_pool_grew = false;
  pool_repack_begin(cmd, rec, now);

  // Procedural bounds are required for correctness, unlike mesh-cache and
  // hold uploads. Allocate every current draw's bounds before either optional
  // user can consume the load burst's remaining pool space.
  const bool native_pool = geo_native_hair_enabled();
  if (native_pool) {
    // Equal-key draws can be reordered by the later dynamic sort. Reserve
    // the maximum of every native recipe, whichever duplicate wins there.
    std::unordered_map<uint64_t,uint32_t> needed;
    for (const FrameDraw& fd : g_draws) {
      if (fd.cached || !fd.captured || !fd.prims) continue;
      AlphaRec ar{};
      if (!fd.cap_alpha || fd.rec==kNone || !alpha_native_hair_record(fd.rec,&ar)) continue;
      auto& prims=needed[fd.dyn_key];prims=(std::max)(prims,fd.prims);
    }
    for (const auto& [key,prims] : needed) {
      DynSlot& s = g_dyn[key];
      if (s.hair_bounds_size >= VkDeviceSize(prims)*24u) continue;
      if (s.hair_bounds_size) g_pending_free.push_back({now,-1,s.hair_bounds_off,s.hair_bounds_size});
      s.hair_bounds_size=0;
      // Mark newly discovered slots for normal expiry even if this scene fails.
      s.last_used=now;
      const VkDeviceSize bytes=(VkDeviceSize(prims)+prims/4u)*24u;
      if (!pool_alloc(cmd,bytes,&s.hair_bounds_off)) return false;
      s.hair_bounds_size=bytes;
    }
  }
  const VkDeviceSize cache_budget = VkDeviceSize((std::max)(cfg_int("rt_mesh_budget_mb",2048),64)) << 20;

  bvh_cpu_mark(0);
  // ---- 1. decide: new meshes, checks, per-draw BLAS, instances ----
  struct Ext { uint64_t key; VkDeviceSize soup, soup_n, pool; uint32_t tris, mat0, matn, status; JobKind kind; uint32_t draw; };
  std::vector<Ext> ext;
  struct MeshBuild { uint64_t key; };
  std::vector<MeshBuild> mesh_builds;
  struct DynBuild { DynSlot* slot; VkDeviceSize soup_off; uint32_t prims; bool refit; HairBoundsPush hair; uint64_t omm = 0; uint32_t omm_tris = 0; };
  std::vector<DynBuild> dyn_builds;
  struct HairBoundsWork {HairBoundsPush push;bool pool_source;};
  std::vector<HairBoundsWork> hair_bounds_work;
  struct Inst { uint32_t draw; uint64_t key; };      // static: draw index (instances follow); dynamic: slot key
  auto& cached = scratch.cached;   // draw indices
  auto& dyn = scratch.dyn;   // draw index, slot key
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
      m.content_id = fd.content_id;
      m.gen = ++g_mesh_gen;
      m.tris = fd.tris_per_instance;
      m.alpha = fd.alpha;
      m.created = m.last_used = m.last_audit = now;
      m.pool_size = VkDeviceSize(m.tris) * tri_bytes;
      VkAccelerationStructureBuildSizesInfoKHR sz = blas_sizes(m.tris, stride, !m.alpha, mesh_flags);
      // Old LRU policy reacted only after all uploads: one load could allocate
      // millions of wide vertices while evicted ranges still awaited GPU use.
      // A declined extraction keeps this draw's captured dynamic BLAS path.
      if (native_pool && !capture_budget::mesh_cache_admit(g_pool_ra.used,as_bytes(),
              m.pool_size,sz.accelerationStructureSize,cache_budget)) {
        ++g_st.budget_deferred;
        continue;
      }
      if (!pool_alloc(cmd, m.pool_size, &m.pool_off)) continue;
      if (!as_create(sz.accelerationStructureSize, m.as, "BLAS chunk for a new cached mesh (stays uncached)")) {
        g_pending_free.push_back({now, -1, m.pool_off, m.pool_size});
        continue;
      }
      m.ranges = fd.ranges;
      m.geo_key = fd.geo_key;
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
  // Absent draws retain their submitted BLAS and a GPU snapshot of the complete
  // soup (position + UV). The pool path uses an identity transform, so its
  // camera-relative coordinates retain exactly the same interpretation.
  if (hold_frames && !moving) {
    std::unordered_set<uint64_t> present;
    for (const FrameDraw& fd : g_draws) {
      if (cpu_opt) scratch.present.insert(fd.dyn_key);
      else present.insert(fd.dyn_key);
    }
    for (auto& [key, s] : g_dyn) {
      if (cpu_opt ? scratch.present.contains(key) : present.count(key) != 0) continue;
      // Why an absent draw was not held (rt_stability_log): 0 aged out, 1 no snapshot,
      // 2 stride, 3 not submitted / newer record, 4 epoch, 5 alpha record not restorable, 6 held.
      int why = 6;
      if (!s.hold_size) why = 1;
      else if (s.hold_stride != stride && !(geo_native_hair_enabled() && s.hold_stride == 24u)) why = 2;
      else if (!(now > s.last_used && now - s.last_used <= hold_frames)) why = 0;
      else if (!(rec_submitted(s.hold_rec) && s.hold_rec == s.rec)) why = 3;
      else if (s.hold_epoch != g_hold_epoch) why = 4;
      if (why != 6) { if (why) ++g_hold_why[why]; continue; }
      FrameDraw fd;
      fd.dyn_key = key; fd.held = true; fd.prims = s.prims;
      fd.soup_off = s.hold_off; fd.cap_alpha = s.alpha;
      if (s.hold_has_alpha) {
        fd.rec = alpha_restore(s.hold_alpha);
        if (fd.rec == kNone) { ++g_hold_why[5]; continue; }   // never substitute an opaque alpha hold
      }
      ++g_hold_why[6];
      dyn.push_back({uint32_t(g_draws.size()), key});
      g_draws.push_back(fd);
    }
    // Stable instance order also allows TLAS refits when the union is unchanged.
    std::sort(dyn.begin(), dyn.end(), [](const auto& a, const auto& b) {
      return g_draws[a.first].dyn_key < g_draws[b.first].dyn_key;
    });
  }
  // Instance count: every instance of a cached draw, one per captured draw, and the cascade
  // soup's one (rt_cascade_casters, rtscene.cpp attach_cascade).
  uint32_t n_static = 0;
  for (uint32_t i : cached) n_static += g_draws[i].instances;
  const bool casc = cap.cascade_tris && cap.cascade_xfb && cap.cascade_stride >= 12;
  const uint32_t n_inst = n_static + uint32_t(dyn.size()) + (casc ? 1u : 0u);
  if (!ensure_ring(n_inst + (g_groom_on ? kGroomMax : 0u), n_mats + (compact_holds ? uint32_t(dyn.size()) : 0u)) || !ensure_tlas(slot, (std::max)(n_inst, 1u))) {
    g_failed = true;
    log("[bvh] allocation failed (%u instances, ring %d, TLAS %d); the split is off, the legacy build takes over%s", n_inst,
        int(g_ring.b != VK_NULL_HANDLE), int(g_tlas[slot].as != VK_NULL_HANDLE),
        g_groom_on ? " (hair grooms join only in the split: no strands this session)" : "");
    return false;
  }
  // This build's region of the host ring: the scene slot's, retired on the GPU
  // (rtscene.cpp pick_scene_slot), so no frame in flight still reads it.
  const uint32_t region = slot;
  const VkDeviceSize rbase = VkDeviceSize(region) * g_region_bytes;
  auto* inst_out = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(g_ring.map + rbase);
  auto* info_out = reinterpret_cast<uint32_t*>(g_ring.map + rbase + g_info_off);
  uint8_t* mat_out = g_ring.map + rbase + g_mat_off;

  bvh_cpu_mark(1);
  // Per-draw BLAS of the captured draws: refit while the triangle count holds.
  // One build per slot and frame: two builds into one BLAS in one call would race.
  std::unordered_set<uint64_t> dyn_this_frame;
  const bool inplace_ok = cfg_int("rt_dyn_inplace", 0) != 0;
  // A/B: rt_dyn_refit=0 never updates a per-draw BLAS (a full build into a new range
  // every frame); rt_build_serial=1 issues one build command per BLAS with a barrier
  // between them instead of one batched call.
  const bool dyn_refit_ok = cfg_int("rt_dyn_refit", 1) != 0;
  struct HoldCopy { VkDeviceSize src, dst, bytes; };
  std::vector<HoldCopy> hold_copies;
  std::vector<DynHoldPackJob> hold_pack_jobs;
  // rt_hair_groom: this frame's matched hair draws (groom_decide), in TLAS-instance order of their cards.
  struct GroomWork { GroomSlot* g; const GroomAsset::Lod* l; VkDeviceSize soup_off; bool refit, compact_copy; AsAlloc old;
                     uint64_t key; uint32_t card = UINT32_MAX; uint64_t hp_key = 0; uint32_t prims = 0;
                     bool prev_ok = false; };   // round 3 M1: the slot's last update was the previous build frame
  const bool hp_on = hair_primary_wanted();   // rt_hair_groom_primary: per-point UVs and the published header
  std::vector<GroomWork> groom_work;
  std::unordered_map<uint64_t, size_t> groom_of_key;
  const bool groom_ready = g_groom_on && ensure_groom_pipeline() &&
      (g_groom_uv_rb.b || make_buf(g_groom_uv_rb, VkDeviceSize(kGroomUvJobs) * 256, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                                   "hair groom UV join readback"));
  // rt_hair_groom_crowd_ksegments (docs/RTX_HAIR_PRIMARY.md increment 5): last frame's groom instances share a
  // segment budget, largest on screen first (hair_groom::plan_crowd); 0: off, every instance its screen-size LOD.
  {
    const uint64_t budget = uint64_t((std::max)(cfg_int("rt_hair_groom_crowd_ksegments", 0), 0)) * 1000u;
    std::vector<hair_groom::CrowdCandidate> cand;
    cand.reserve(g_groom_seen.size());
    for (const auto& g : g_groom_seen) {
      hair_groom::CrowdCandidate c;
      c.size = g->size;
      c.want = g->want;
      c.lods = int(kGroomLods);
      for (uint32_t l = 0; l < kGroomLods && l < 4; ++l) c.segments[l] = g->segments[l];
      cand.push_back(c);
    }
    const std::vector<int> plan = hair_groom::plan_crowd(cand, budget);
    for (size_t i = 0; i < g_groom_seen.size(); ++i) g_groom_seen[i]->crowd = budget ? plan[i] : -2;
    if (budget && (now % 300) == 0 && !plan.empty()) {
      uint32_t per[kGroomLods] = {}, cards = 0;
      for (int l : plan) l < 0 ? ++cards : ++per[l];
      log("[hair groom] crowd plan: %zu instances, budget %llu k segments: LOD %u/%u/%u, %u keep their cards",
          plan.size(), (unsigned long long)(budget / 1000), per[0], per[1], per[2], cards);
    }
    g_groom_seen.clear();
  }
  // A level load (a new G-buffer depth image, hair_primary::LoadGuard): the join caches are keyed by draw identities
  // made of Vulkan handles, which the new level reuses for other meshes; a stale verdict would give a new mesh with
  // the same index count another style's groom, and a UV readback issued before the load would land on a reused
  // identity. Drop the cached verdicts and the readbacks in flight (their copies still land; nothing reads them).
  // Review 2026-11-08 item 14: with the image's creation id, so a new level whose depth image reuses the old handle is
  // a boundary too (the join verdicts below are keyed by handles and must be rebuilt, index content id included).
  const uint64_t groom_depth = geo_gbuffer_depth_image();
  uint64_t groom_depth_id = 0;
  if (groom_depth) {
    std::shared_lock lock(tracker().mu);
    const auto it = tracker().images.find(VkImage(groom_depth));
    if (it != tracker().images.end()) groom_depth_id = it->second.id;
  }
  if (g_groom_load.observe(groom_depth, now, groom_depth_id)) {
    uint32_t pending = 0;
    for (GroomUvJob& job : g_groom_uv_jobs) {
      pending += job.used ? 1u : 0u;
      job = GroomUvJob{};
    }
    log("[hair groom] level load: %zu join verdicts, %zu UV joins and %u UV readbacks in flight dropped",
        g_groom_join.size(), g_groom_uv.size(), pending);
    g_groom_join.clear();
    g_groom_uv.clear();
    g_groom_logged_unmatched.clear();
    hair_groom::flush_seen();   // the previous level's styles without a groom (one small file write per level load)
  }
  struct GroomUvCopy { uint32_t job; VkDeviceSize soup_off; };
  std::vector<GroomUvCopy> groom_uv_copies;
  if (groom_ready) {
    // UV joins whose readback completed on the GPU: match against the files with that index count.
    for (uint32_t j = 0; j < kGroomUvJobs; ++j) {
      GroomUvJob& job = g_groom_uv_jobs[j];
      if (!job.used) continue;
      const auto state = hair_primary::readback_state(job.frame, life_gpu_done(), rec_submitted(job.rec));
      if (state == hair_primary::ReadbackState::Wait) continue;
      if (state == hair_primary::ReadbackState::Drop) {
        g_groom_uv.erase(job.identity); // allow a new copy for this identity
        job = GroomUvJob{}; continue;
      }
      float got[hair_groom::kSamples * 2];
      std::memcpy(got, g_groom_uv_rb.map + VkDeviceSize(j) * 256, sizeof got);
      GroomUvState st{0, hair_groom::JoinMiss::UvMismatch};
      const auto cands = hair_groom::with_count(job.count);
      for (const hair_groom::Loaded* l : cands)
        if (l->asset.has_uv && hair_groom::uv_samples_match(got, l->asset.uv)) { st = {l->asset.index_id, hair_groom::JoinMiss::None}; break; }
      g_groom_uv[job.identity] = st;
      if (g_groom_uv_logged.size() < 64 && g_groom_uv_logged.insert(job.count).second) {
        const float* want = !cands.empty() && cands[0]->asset.has_uv ? cands[0]->asset.uv : nullptr;
        log("[hair groom] UV join %u indices: %s; captured uv0 (%.4f, %.4f) uv31 (%.4f, %.4f); file %s (%.4f, %.4f) (%.4f, %.4f)",
            job.count, st.id ? "matched" : "no match", got[0], got[1], got[62], got[63], want ? "uv0/uv31" : "without UV samples",
            want ? want[0] : 0.0f, want ? want[1] : 0.0f, want ? want[62] : 0.0f, want ? want[63] : 0.0f);
      }
      job = GroomUvJob{};
    }
  }
  // rt_omm_dyn (live): needs the EXT backend (rt_omm=1 at startup), the alpha test and a transfer-source ring.
  // Bakes of new micro-maps (dispatched after the soup conversion) and this frame's uv checks.
  const bool omm_dyn_on = cfg_int("rt_omm_dyn", 0) != 0 && alpha_test && omm::available_for(g_dd) && g_ring_transfer_source && g_pool.b &&
                          ensure_omm_dyn_pipe();
  struct OmmBake { uint64_t key; VkDeviceSize soup_off; uint32_t tris, w, h; VkImageView view; float cut_lo, cut_hi, su, sv; };
  struct OmmCheck { const DynMicromap* e; VkDeviceSize soup_off, index_off; uint32_t tris; };
  std::vector<OmmBake> omm_bakes;
  std::vector<OmmCheck> omm_checks;
  std::unordered_set<uint64_t> omm_baking;
  uint64_t omm_bake_tris = 0;
  const uint64_t omm_bake_cap = uint64_t((std::max)(cfg_int("rt_omm_dyn_bake_tris", 131072), 0));
  const VkDeviceSize omm_dyn_cap = VkDeviceSize((std::max)(cfg_int("rt_omm_dyn_mb", 256), 0)) << 20;
  const uint32_t omm_retry = uint32_t((std::max)(cfg_int("rt_omm_dyn_retry_frames", 64), 0));
  if (omm_dyn_on) g_omm_dyn_refusal.refresh(now, omm::live_bytes(), omm_retry);
  if (omm_dyn_on) {
    // Bakes whose command buffer ran: usable, inputs freed; dropped ones are discarded.
    for (auto it = g_omm_dyn.begin(); it != g_omm_dyn.end();) {
      DynMicromap& e = it->second;
      const int st = e.ready ? kRecSubmitted : rec_state(e.rec);
      if (!e.ready && st == kRecSubmitted) {
        e.ready = true;
        for (Buf* b : {&e.data, &e.descriptors})
          if (b->b) { g_omm_dyn_bytes -= (std::min)(g_omm_dyn_bytes, b->size); free_buf(*b); }
      }
      if (!e.ready && st != kRecPending) {
        omm::release_ticket(e.ticket);
        omm_dyn_free(e);
        g_omm_dyn_cached_tris -= (std::min)(g_omm_dyn_cached_tris, uint64_t(e.tris));
        it = g_omm_dyn.erase(it);
        continue;
      }
      ++it;
    }
    // rt_omm_dyn_evict_frames (off by default, common/omm_dyn_evict.h): LRU eviction of micro-maps no draw matched
    // for N frames and no per-draw BLAS was built with, at most 64 per build.
    const uint32_t evict_frames = uint32_t((std::max)(cfg_int("rt_omm_dyn_evict_frames", 0), 0));
    if (evict_frames && !g_omm_dyn.empty()) {
      std::unordered_set<uint64_t> held;
      for (const auto& [k, s] : g_dyn) if (s.omm_ticket) held.insert(s.omm_ticket);
      std::vector<bg3::omm_dyn::EvictCandidate> all;
      all.reserve(g_omm_dyn.size());
      for (const auto& [k, e] : g_omm_dyn) all.push_back({k, e.last_use, e.ready, held.count(e.ticket) != 0});
      for (const uint64_t k : bg3::omm_dyn::pick_evictions(std::move(all), now, evict_frames, 64)) {
        auto it = g_omm_dyn.find(k);
        DynMicromap& e = it->second;
        omm::release_ticket(e.ticket);   // destroyed once every registered GPU use completed
        omm_dyn_free(e);
        g_omm_dyn_cached_tris -= (std::min)(g_omm_dyn_cached_tris, uint64_t(e.tris));
        g_omm_dyn.erase(it);
        ++g_st.omm_dyn_evicted;
      }
    }
  }
  for (auto& [di, key] : dyn) {
    FrameDraw& fd = g_draws[di];
    if (fd.held) {
      // Re-certify held ranges for EACH new scene's invalid-status word. A
      // previous scene may have rejected these same bounds; never clear that
      // evidence merely because its native inputs were snapshotted.
      const auto old=g_dyn.find(key);
      if(old!=g_dyn.end() && old->second.hair){
        auto push=old->second.hair_push;push.soup_base=uint32_t(fd.soup_off);
        hair_bounds_work.push_back({push,true});
      }
      continue;
    }
    key = fd.dyn_key;
    if (!(cpu_opt ? scratch.dynamic.insert(key) : dyn_this_frame.insert(key).second)) {
      key = 0;
      continue;
    }
    DynSlot& s = g_dyn[key];
    AlphaRec hair_record{};
    const bool hair=geo_native_hair_enabled() && fd.cap_alpha && fd.rec!=kNone && alpha_native_hair_record(fd.rec,&hair_record);
    if(hair && !ensure_hair_pipeline()){
      log("[hair geometry] bounds pipeline unavailable: scene rejected, RT must not pass its gate");return false;
    }
    // The bounds were reserved before the decisions (the native pool pass above; the Vulkan AABB stride is 24).
    if(hair && s.hair_bounds_size<VkDeviceSize(fd.prims)*24u)return false;
    if(!hair && geo_native_hair_enabled() &&
       (cpu_opt ? cfg_hot(CfgHot::rt_hair_pool_trim) : cfg_int("rt_hair_pool_trim",0)) && s.hair_bounds_size){
      // A toggled-to-triangle slot can remain active indefinitely. Its old
      // procedural sidecar otherwise never expires with the inactive slot LRU.
      g_pending_free.push_back({now,-1,s.hair_bounds_off,s.hair_bounds_size});
      s.hair_bounds_off=0;s.hair_bounds_size=0;s.hair_push={};
    }
    // rt_omm_dyn: a masked record with a ready micro-map for this draw's identity gets it (a new identity is baked
    // this frame and used from a later one); the BLAS flags allow the instance flag to switch it off.
    uint64_t omm_want = 0;
    const DynMicromap* omm_entry = nullptr;
    if (omm_dyn_on && !hair && fd.cap_alpha && fd.rec != kNone) {
      AlphaRec ar{};
      VkImageView view{};
      uint32_t mw = 0, mh = 0;
      if (alpha_fine_mask(fd.rec, &ar, &view, &mw, &mh)) {
        ++g_st.omm_dyn_draws;
        g_st.omm_dyn_tris += fd.prims;
        const float lo = (std::min)(ar.cut, ar.cut_first), hi = (std::max)(ar.cut, ar.cut_first);
        uint32_t b[4];
        std::memcpy(&b[0], &lo, 4); std::memcpy(&b[1], &hi, 4); std::memcpy(&b[2], &ar.scale[0], 4); std::memcpy(&b[3], &ar.scale[1], 4);
        uint64_t mk = mix(mix(mix(mix(mix(mix(fd.omm_key, uint64_t(view)), b[0]), b[1]), b[2]), b[3]), fd.prims);
        if (!mk) mk = 1;
        auto it = g_omm_dyn.find(mk);
        if (it == g_omm_dyn.end()) {
          const VkDeviceSize bytes = VkDeviceSize(fd.prims) * (64u + 24u + 8u);
          if (omm_baking.count(mk)) {
          } else if (omm_bake_tris + fd.prims > omm_bake_cap || g_omm_dyn_bytes + bytes > omm_dyn_cap ||
                     fd.soup_off > UINT32_MAX || fd.prims > 65535u * 65535u || !g_omm_dyn_refusal.allow(fd.prims)) {
            ++g_st.omm_dyn_deferred;
          } else {
            omm_baking.insert(mk);
            omm_bake_tris += fd.prims;
            omm_bakes.push_back({mk, fd.soup_off, fd.prims, mw, mh, view, lo, hi, ar.scale[0], ar.scale[1]});
          }
        } else if (it->second.ready && it->second.tris == fd.prims && it->second.mask == view) {
          it->second.last_use = now;   // rt_omm_dyn_evict_frames: matched by a draw this build
          // Index range for the check (capacity like the BLAS's, reallocated when the draw grows).
          const VkDeviceSize need = VkDeviceSize(fd.prims) * 4u;
          if (s.omm_index_size < need) {
            if (s.omm_index_size) g_pending_free.push_back({now, -1, s.omm_index_off, s.omm_index_size});
            s.omm_index_size = 0;
            const VkDeviceSize bytes = VkDeviceSize(fd.prims + fd.prims / 4u) * 4u;
            if (pool_alloc(cmd, bytes, &s.omm_index_off)) s.omm_index_size = bytes;
          }
          // The check covers at most 65535 x 64 triangles in one dispatch: every index of the build is written.
          if (s.omm_index_size >= need && s.omm_index_off <= UINT32_MAX && fd.prims <= 65535u * 64u &&
              omm::register_ticket_use(cmd, it->second.ticket)) {
            omm_want = it->second.ticket;
            omm_entry = &it->second;
          }
        }
      }
    }
    // Sizes with the attachment: the capacity's and this build's triangle counts.
    VkAccelerationStructureTrianglesOpacityMicromapEXT omm_cap_attach{}, omm_now_attach{};
    VkMicromapUsageEXT omm_cap_use{}, omm_now_use{};
    if (omm_want && (!omm_dyn_attachment(omm_want, omm_entry->tris, fd.prims + fd.prims / 4, 0, &omm_cap_attach, &omm_cap_use) ||
                     !omm_dyn_attachment(omm_want, omm_entry->tris, fd.prims, 0, &omm_now_attach, &omm_now_use))) {
      omm_want = 0;
      omm_entry = nullptr;
    }
    const VkBuildAccelerationStructureFlagsKHR slot_flags = omm_want ? dyn_flags | kOmmDynBlasFlags : dyn_flags;
    bool fresh = !s.as.as;
    // A refit (UPDATE) needs the source built: its last build must have been submitted
    // (a dropped command buffer left the BLAS unbuilt; a full build does not read it).
    // rt_omm_dyn: only with the same micro-map (the per-triangle indices may change: ALLOW_OPACITY_MICROMAP_UPDATE).
    bool refit = dyn_refit_ok && !fresh && s.prims == fd.prims && s.alpha == fd.cap_alpha && s.hair==hair && s.flags == slot_flags &&
                 s.omm_ticket == omm_want &&
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
    if (!refit && (fresh || !inplace_ok || fd.prims > s.cap_prims || s.alpha != fd.cap_alpha || s.hair!=hair || s.flags != slot_flags ||
                   s.omm_ticket != omm_want)) {
      if (s.as.as) as_destroy(s.as);
      uint32_t cap_prims = fd.prims + fd.prims / 4;
      VkAccelerationStructureBuildSizesInfoKHR sz = hair?hair_sizes(cap_prims,dyn_flags):
          omm_want ? blas_sizes_omm(cap_prims, stride, slot_flags, &omm_cap_attach) : blas_sizes(cap_prims, stride, !fd.cap_alpha, dyn_flags);
      // Micromap attachments change sizes, not the per-draw BLAS lifetime domain.
      if (!as_create(sz.accelerationStructureSize, s.as, "BLAS chunk for a per-draw BLAS (draw left out this frame)", 1)) {
        if (s.hold_size) g_pending_free.push_back({now, -1, s.hold_off, s.hold_size});
        if(s.hair_bounds_size)g_pending_free.push_back({now,-1,s.hair_bounds_off,s.hair_bounds_size});
        if (s.omm_index_size) g_pending_free.push_back({now, -1, s.omm_index_off, s.omm_index_size});
        g_dyn.erase(key);
        key = 0;
        continue;
      }
      s.cap_prims = cap_prims;
      s.alpha = fd.cap_alpha;
      s.hair=hair;
      s.flags = slot_flags;
      s.omm_ticket = omm_want;
      ++g_st.dyn_new;
    }
    if (!refit || s.prims != fd.prims) {
      VkAccelerationStructureBuildSizesInfoKHR sz = hair?hair_sizes(fd.prims,dyn_flags):
          omm_want ? blas_sizes_omm(fd.prims, stride, slot_flags, &omm_now_attach) : blas_sizes(fd.prims, stride, !fd.cap_alpha, dyn_flags);
      s.build_scratch = sz.buildScratchSize;
      s.update_scratch = sz.updateScratchSize;
    }
    if (omm_want) {
      omm_checks.push_back({omm_entry, fd.soup_off, s.omm_index_off, fd.prims});
      ++g_st.omm_dyn_att_draws;
      g_st.omm_dyn_att_tris += fd.prims;
    }
    s.prims = fd.prims;
    s.last_used = now;
    s.since_build = refit ? s.since_build + 1 : 0;
    // Staggered forced rebuilds: a slot's first rebuild comes at a key-dependent frame.
    if (!refit && fresh) s.since_build = uint32_t(key % uint64_t(rebuild_frames));
    HairBoundsPush hp{};
    if(hair){
      hp.tris=fd.prims;hp.step=((fd.prims+127u)/128u)*128u;hp.soup_base=uint32_t(fd.soup_off);hp.bounds_base=uint32_t(s.hair_bounds_off);
      const auto* data=reinterpret_cast<const uint8_t*>(&hair_record);
      std::memcpy(hp.vp,data+96,64);std::memcpy(hp.depth+1,data+176,12);
      hp.depth[0]=0.01f*(std::max)({0.0f,hair_record.p1[0],hair_record.p1[1]});
      hp.stride=stride;hp.camera=hair_record.channel;
      float bounded{};std::memcpy(&bounded,data+188,4);hp.bounded=bounded==1.0f;
      if(s.hair_bounds_off>UINT32_MAX || s.hair_bounds_size>UINT32_MAX ||
         !alpha_native_hair_bounds(fd.rec,uint32_t(s.hair_bounds_off)))return false;
      s.hair_push=hp;hair_bounds_work.push_back({hp,false});
    }
    dyn_builds.push_back({&s, fd.soup_off, fd.prims, refit,hp, omm_want, omm_entry ? omm_entry->tris : 0u});
    // rt_hair_groom: the matched hair draw's groom (its card instance stays for primary queries).
    GroomAsset* groom_src = hair && groom_ready && fd.groom_id && groom_work.size() < kGroomMax
                                ? groom_asset(fd.groom_id, fd.groom_indices) : nullptr;
    if (groom_src && fd.prims * 3u != groom_src->indices) groom_src = nullptr;   // a partial draw of the mesh
    // The point update reads this frame's soup at the draw's offset: only a draw captured into it this frame, inside
    // the live range (a held draw's offset is a mesh-pool snapshot offset: reading the soup there lost the device,
    // 2026-11-03). Such a frame keeps the slot (no update, no groom instance: its cards in every pass).
    bool groom_keep = false;
    if (groom_src && !hair_primary::soup_update_ok(fd.captured, fd.held, fd.soup_off, fd.prims, stride, cap.xfb_size,
                                                   groom_src->indices / 3u)) {
      if (g_groom_soup_refused++ < 8)
        log("[hair groom] %016llx: no point update this frame (captured %u, held %u, soup offset %llu, %u triangles, stride %u, "
            "soup %llu bytes): cards", (unsigned long long)fd.groom_id, unsigned(fd.captured), unsigned(fd.held),
            (unsigned long long)fd.soup_off, fd.prims, stride, (unsigned long long)cap.xfb_size);
      groom_src = nullptr;
      groom_keep = true;
    }
    if (hair && groom_ready && !fd.groom_id && fd.groom_miss == hair_groom::JoinMiss::NotHostMapped &&
        !g_groom_uv.count(fd.groom_identity) && fd.captured && !fd.held && fd.groom_indices == fd.prims * 3u &&
        fd.soup_off + VkDeviceSize(fd.groom_indices) * stride <= cap.xfb_size) {
      // UV join: copy the 32 sampled corners' UVs (soup +16, untouched by the conversion) in this build.
      for (uint32_t j = 0; j < kGroomUvJobs; ++j) {
        if (g_groom_uv_jobs[j].used) continue;
        g_groom_uv_jobs[j] = {fd.groom_identity, now, rec, fd.groom_indices, true};
        g_groom_uv[fd.groom_identity] = GroomUvState{};
        groom_uv_copies.push_back({j, fd.soup_off});
        fd.groom_miss = hair_groom::JoinMiss::UvPending;
        break;
      }
    }
    if (hair && groom_ready && !fd.groom_id) {
      ++g_groom_st.unmatched;
      ++g_groom_st.miss[size_t(fd.groom_miss)];
      const uint64_t what = (uint64_t(fd.groom_indices) << 8) | uint64_t(fd.groom_miss);
      if (g_groom_logged_unmatched.size() < 64 && g_groom_logged_unmatched.insert(what).second) {
        log("[hair groom] unmatched hair draw: %u indices (%u triangles): %s", fd.groom_indices, fd.prims,
            hair_groom::join_miss_name(fd.groom_miss));
        // No groom for this mesh (its id is unknown here: the count names the candidates in hair_seen.txt).
        if (fd.groom_miss == hair_groom::JoinMiss::CountUnknown || fd.groom_miss == hair_groom::JoinMiss::UvMismatch)
          hair_groom::note_seen(fd.groom_indices, 0);
      }
    }
    if (!groom_src) {
      if (s.groom && !groom_keep) { groom_release(*s.groom, now); s.groom.reset(); }
    } else {
      if (!s.groom || s.groom->id != fd.groom_id) {
        if (s.groom) groom_release(*s.groom, now);
        s.groom = std::make_shared<GroomSlot>();
        s.groom->id = fd.groom_id;
      }
      GroomSlot& g = *s.groom;
      int lod = groom_pick_lod(g, int(groom_src->lods.size()), life_gpu_done());
      if (const auto* src = hair_groom::find(fd.groom_id, groom_src->indices))
        for (uint32_t l = 0; l < kGroomLods; ++l) g.segments[l] = l < groom_src->lods.size() ? src->asset.lod_segments[l] : 0u;
      g_groom_seen.push_back(s.groom);
      // This frame's crowd plan: a coarser LOD, or the cards (the slot is kept, no groom instance this frame).
      const bool crowd_cards = g.crowd == -1;
      if (g.crowd >= 0) lod = (std::max)(lod, (std::min)(g.crowd, int(groom_src->lods.size()) - 1));
      const GroomAsset::Lod* lp = crowd_cards ? nullptr : groom_lod(cmd, fd.groom_id, *groom_src, lod, now);
      // rt_hair_groom_lod_keep: the wanted LOD is still uploading (groom_lod staged it): keep drawing the built one and
      // ask again next frame instead of dropping the groom (the upload goes on either way).
      const bool lod_keep = cfg_int("rt_hair_groom_lod_keep", 0) != 0;
      if (!lp && !crowd_cards && g.lod >= 0 && g.lod != lod && g.lod < int(groom_src->lods.size()) && g.pos_size &&
          g.as.as && lod_keep) {
        // Review 2026-11-08 item 5: only a still-resident built LOD is kept. groom_lod() would stage a new upload of an
        // evicted one (its range freed by groom_make_room), churning the pool while the wanted LOD uploads.
        GroomAsset::Lod& resident = groom_src->lods[size_t(g.lod)];
        const GroomAsset::Lod* built = resident.ready ? &resident : nullptr;
        if (built) resident.last_used = now;
        if (hair_groom::lod_this_frame(lod, g.lod, false, built != nullptr, true) == g.lod) {
          lp = built; lod = g.lod;
          g.lod_votes = 29;   // groom_pick_lod returns the wanted LOD again on the next frame
          ++g_groom_st.lod_waits;
        }
      }
      bool ok = lp != nullptr;
      const GroomAsset::Lod& l = lp ? *lp : groom_src->lods[size_t(lod)];
      GroomWork w{&g, &l, fd.soup_off, false, false, {}, key};
      w.hp_key = fd.hp_key;
      w.prims = fd.prims;   // the shader bounds every triangle read by it
      // A new LOD, or primary strands turned on for a slot without per-point UVs: new points range and a full build.
      if (ok && (lod != g.lod || (hp_on && g.pos_size && !g.uv_off))) {
        if (g.pos_size) {
          g_pending_free.push_back({now + kGroomFreeLag, -1, g.pos_off, g.pos_size});
          g_groom_pool_used -= (std::min)(g_groom_pool_used, g.pos_size);
        }
        as_destroy(g.as, kGroomFreeLag);
        if (g.compact == 1) g_groom_free_queries.push_back(g.query);
        g.pos_size = 0; g.uv_off = 0; g.prev_off = 0; g.prev_frames = 0; g.built = false; g.compact = 0; g.lod = lod;
      }
      if (ok && !g.pos_size) {
        const VkDeviceSize pos = (VkDeviceSize(l.points) * 12 + 15) / 16 * 16;
        const VkDeviceSize uv = hp_on ? (VkDeviceSize(l.points) * 8 + 15) / 16 * 16 : 0, prev = hp_on ? pos : 0;
        ok = groom_make_room(pos + 32 + uv + prev, now) && pool_alloc(cmd, pos + 32 + uv + prev, &g.pos_off);
        if (ok) {
          g.pos_size = pos + 32 + uv + prev; g.bounds_off = g.pos_off + pos;
          g.uv_off = uv ? g.pos_off + pos + 32 : 0;
          g.prev_off = prev ? g.pos_off + pos + 32 + uv : 0;
          g.prev_frames = 0;
          g_groom_pool_used += g.pos_size;
        }
      }
      if (ok && !g.readback.b) ok = make_buf(g.readback, 64 + 4 * 32, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "hair groom readback");
      if (ok && !g.as.as) {
        const auto sz = groom_sizes(l);
        g.build_scratch = sz.buildScratchSize; g.update_scratch = sz.updateScratchSize;
        ok = as_create(sz.accelerationStructureSize, g.as, "hair groom BLAS", 1);
        g.built = false; g.compact = 0;
      }
      // Compaction: the size queried after the first build, read once that frame completed on the GPU.
      if (ok && g.compact == 1 &&
          hair_primary::readback_state(g.query_frame, life_gpu_done(), rec_submitted(g.query_rec)) == hair_primary::ReadbackState::Drop) {
        g_groom_free_queries.push_back(g.query);
        g.compact = 0; g.built = false; // redo both the full build and its query
      }
      if (ok && g.compact == 1 &&
          hair_primary::readback_state(g.query_frame, life_gpu_done(), rec_submitted(g.query_rec)) == hair_primary::ReadbackState::Ready) {
        uint64_t size = 0;
        std::memcpy(&size, g.readback.map + 32, 8);
        g_groom_free_queries.push_back(g.query);
        g.compact = 2;
        AsAlloc c;
        if (size && size < g.as.size && as_create(size, c, nullptr, 1)) {
          w.compact_copy = true; w.old = g.as; g.as = c;
          ++g_groom_st.compacted;
        }
      }
      if (ok) {
        w.refit = g.built && rec_submitted(g.rec) && dyn_refit_ok;
        if (!w.refit) g.built = false;   // a full build (also after an unsubmitted one)
        g.rec = rec;
        // M1: prev points are last frame's, not an older update's (prev_frames is checked at the row, after this update)
        w.prev_ok = hair_primary::groom_prev_points(2u, g.last_update, now);
        g.last_update = now;
        groom_of_key[key] = groom_work.size();
        groom_work.push_back(w);
        ++g_groom_st.matched;
        ++g_groom_st.lod[size_t(lod)];
        w.refit ? ++g_groom_st.refits : ++g_groom_st.builds;
      } else if (!crowd_cards && lod_keep && !lp && g.lod < 0 && !g.pos_size) {
        // Item 5: a new slot whose first LOD is still uploading keeps its (empty) slot, its LOD choice and the crowd
        // plan's entry instead of being released and made again every frame of the upload; cards meanwhile.
      } else if (!crowd_cards) {
        if (!lp && g.lod >= 0 && g.lod != lod) ++g_groom_st.lod_drops;   // a LOD switch dropped the groom (5i)
        groom_release(g, now);
        s.groom.reset();
      }
    }
    s.rec = rec;
    // Only small draws are snapshotted: BG3's LOD swaps are props of a few hundred to a few
    // thousand triangles, while the terrain/large meshes (up to 131k) were most of the copy cost.
    if (!g_pool_copy.target.b && hold_frames && !moving && fd.prims <= hold_max_prims) {
      s.hold_has_alpha = fd.cap_alpha && fd.rec != kNone;
      const bool material_ok = !s.hold_has_alpha || alpha_snapshot(fd.rec, &s.hold_alpha);
      const uint32_t snapshot_stride = dyn_hold_stride(stride, compact_holds, s.hold_has_alpha, s.hold_alpha.rec.kind);
      const VkDeviceSize bytes = VkDeviceSize(fd.prims) * 3u * snapshot_stride;
      if (s.hold_size && (s.hold_size != bytes || s.hold_stride != snapshot_stride)) {
        g_pending_free.push_back({now, -1, s.hold_off, s.hold_size});
        s.hold_size = 0;
      }
      s.hold_rec = 0;
      if (material_ok && dyn_hold_pack_range(fd.soup_off, uint64_t(fd.prims)*3u, stride, cap.xfb_size) &&
          (s.hold_size || pool_alloc(cmd, bytes, &s.hold_off))) {
        s.hold_size = bytes; s.hold_stride = snapshot_stride; s.hold_epoch = g_hold_epoch;
        s.hold_rec = rec;
        if (snapshot_stride != stride)
          hold_pack_jobs.push_back({uint32_t(fd.soup_off), uint32_t(s.hold_off), fd.prims*3u, stride});
        else hold_copies.push_back({fd.soup_off, s.hold_off, bytes});
      }
    }
    refit ? ++g_st.dyn_refit : ++g_st.dyn_rebuild;
  }

  bvh_cpu_mark(2);
  // ---- 2. TLAS instances and InstInfo (static matrices at submit) ----
  std::vector<Fixup> fix;
  const int draw_log = cfg_int("rt_mesh_draw_log", 0);
  const bool draw_sample = draw_log && now % uint64_t(draw_log > 1 ? draw_log :
                                      stats_period(unsigned((std::max)(cfg_int("rt_timing_frames", 300), 30)))) == 0;
  uint32_t n = 0;
  // Local copies of each instance's BLAS reference and flags for the TLAS signature and
  // the chunk guard below: reading them back from the mapped ring is uncached (P3).
  auto& inst_ref = scratch.refs;
  auto& inst_flags = scratch.flags;
  uint64_t static_tris = 0, dyn_tris = 0;
  struct OmmDrawRange {uint32_t draw,first,count;};
  std::vector<OmmDrawRange> omm_draws;
  for (uint32_t di : cached) {
    const FrameDraw& fd = g_draws[di];
    const Mesh& m = g_meshes[fd.mesh_key];
    const bool alpha = fd.alpha && fd.rec != kNone;
    if(alpha && alpha_test) {
      g_st.omm_alpha_static_tris+=uint64_t(m.tris)*fd.instances;
      if(fd.omm_material){omm_draws.push_back({di,n,fd.instances});g_st.omm_capture_tris+=uint64_t(m.tris)*fd.instances;}
    }
    Fixup f{reinterpret_cast<uint8_t*>(inst_out + n), fd.instances, uint32_t(sizeof(VkAccelerationStructureInstanceKHR)),
            {fd.rows[0], fd.rows[1], fd.rows[2]}, {fd.row_stride[0], fd.row_stride[1], fd.row_stride[2]}, true, {}};
    for (int k = 0; k < 3; ++k) load_mapped16(reinterpret_cast<float*>(f.sample + 16 * k), fd.rows[k]);
    if (draw_sample) {
      f.log_key = fd.mesh_key; f.log_vs = fd.vs;
      f.log_tris = m.tris; f.log_first = fd.first_instance;
      f.log_instance = n;
      f.log_frame = now;
    }
    fix.push_back(f);
    // Built in local memory and copied whole: inst_out/info_out are mapped (often
    // write-combined) memory, where the bit-field stores below would read back
    // uncached memory (read-modify-write), ~2.5 us each (P3: 7.6 ms per build).
    VkAccelerationStructureInstanceKHR in{};
    in.mask = 0;   // enabled at submit, with a finite matrix
    in.instanceShaderBindingTableRecordOffset = 0;
    in.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
               (alpha && alpha_test ? 0u : uint32_t(VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR));
    in.accelerationStructureReference = m.as.addr;
    // Pool material records (native_hit_pool.h): counted per instance; a qualified record with
    // an extracted UV/basis gives a non-alpha instance its pool material row (rec = flag | slot).
    native_hit::PoolUse use{};
    if (native_hit::enabled()) {
      Mesh& mm = g_meshes[fd.mesh_key];
      use = native_hit::pool_lookup(fd.geo_key, fd.pos_key, fd.instances,
                                    {mm.gen, mm.pool_off, mm.tris, stride, mm.basis_off, mm.basis_size, g_pool_epoch});
      const VkDeviceSize need = native_hit::pool::basis_bytes(mm.tris);
      if (use.wants_basis && (!alpha || pool_alpha_rows) && !mm.basis_size && g_basis_bytes + need <= native_hit::pool::kBasisCapBytes &&
          pool_alloc(cmd, need, &mm.basis_off)) {
        mm.basis_size = need;   // passed to the record by the next lookup; extracted at its G-buffer draw
        g_basis_bytes += need;
        if (mm.basis_over) { --g_basis_over; g_basis_over_bytes -= (std::min)(g_basis_over_bytes, mm.basis_over); mm.basis_over = 0; }
      } else if (use.wants_basis && (!alpha || pool_alpha_rows) && !mm.basis_size && !mm.basis_over &&
                 g_basis_bytes + need > native_hit::pool::kBasisCapBytes) {
        mm.basis_over = need;   // its hits keep the base material (no pool row) until a range frees up
        ++g_basis_over;
        g_basis_over_bytes += need;
        if (g_basis_over > g_basis_over_logged) {
          g_basis_over_logged = g_basis_over * 2 - 1;   // next line at twice the count
          log("[hit material pool basis] over kBasisCapBytes (%.1f of %.0f MB used): %llu meshes (%.1f MB) wait "
              "for a UV/basis range; their hits use no pool row", double(g_basis_bytes) / 1048576.0,
              double(native_hit::pool::kBasisCapBytes) / 1048576.0, (unsigned long long)g_basis_over,
              double(g_basis_over_bytes) / 1048576.0);
        }
      }
    }
    const bool pool_row = (!alpha || pool_alpha_rows) && use.slot < native_hit::nh_pool_rows;
    const uint32_t inf[4] = {uint32_t(m.pool_off), (stride & 0xffu) | kInfoPool | (alpha ? kInfoAlpha : 0u) |
                             (fd.camera_fade ? kInfoCameraFade : 0u) | (pool_row ? native_hit::nh_pool_meta : 0u) |
                             (pool_row && alpha ? (use.slot + 1u) << native_hit::nh_pool_alpha_slot_shift : 0u),
                             alpha ? 16 + fd.rec * kRecBytes : pool_row ? native_hit::nh_pool_rec_flag | use.slot : 0u, m.tris};
    if (fd.camera_fade) camera_fade_marked(fd.instances, uint64_t(m.tris) * fd.instances);
    for (uint32_t i = 0; i < fd.instances; ++i, ++n) {
      in.instanceCustomIndex = n;
      std::memcpy(inst_out + n, &in, sizeof in);
      std::memcpy(info_out + n * 4, inf, sizeof inf);
      inst_ref.push_back(in.accelerationStructureReference);
      inst_flags.push_back(in.flags);
    }
    static_tris += uint64_t(m.tris) * fd.instances;
    ++g_st.cached_draws;
    g_st.static_inst += fd.instances;
  }
  const uint32_t n_static_written = n;
  std::vector<std::pair<uint32_t, uint32_t>> omm_enable;   // rt_omm_dyn: instance, flags in the accelerated TLAS
  std::vector<uint64_t> omm_tickets;
  for (auto& [di, key] : dyn) {
    if (!key) continue;
    const FrameDraw& fd = g_draws[di];
    const DynSlot& s = g_dyn[key];
    const bool alpha = fd.cap_alpha && fd.rec != kNone;
    if (draw_sample)
      log("[bvh-draw] frame %llu dynamic instance %u key %016llx VS %016llx tris %u held %u soup %llu",
          (unsigned long long)now, n, (unsigned long long)key, (unsigned long long)fd.vs, fd.prims, unsigned(fd.held),
          (unsigned long long)fd.soup_off);
    VkAccelerationStructureInstanceKHR in{};   // local, then copied whole (see above)
    in.transform.matrix[0][0] = in.transform.matrix[1][1] = in.transform.matrix[2][2] = 1.0f;
    in.instanceCustomIndex = n;
    in.mask = 0xFF;
    in.instanceShaderBindingTableRecordOffset = 0;
    in.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
               (alpha && alpha_test ? 0u : uint32_t(VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR));
    if (s.omm_ticket && !s.hair) {
      // rt_omm_dyn: the ordinary TLAS is the reference scene (micro-maps off); the accelerated copy enables them.
      if (omm_dyn_on && alpha && alpha_test) {
        omm_enable.push_back({n, in.flags});
        if (std::find(omm_tickets.begin(), omm_tickets.end(), s.omm_ticket) == omm_tickets.end()) omm_tickets.push_back(s.omm_ticket);
      }
      in.flags |= kOmmDynDisable;
    }
    in.accelerationStructureReference = s.as.addr;
    const bool opaque_material=!alpha && !fd.held && fd.material_record>=native_hit::nh_alpha_rows && fd.material_record<native_hit::nh_material_rows;
    uint32_t groom_bit = 0;
    if (const auto gw = groom_of_key.find(key); gw != groom_of_key.end() && !fd.held) {
      groom_work[gw->second].card = n;
      groom_bit = kInfoGroomReplaced;
    }
    const uint32_t inf[4] = {uint32_t(fd.soup_off), ((fd.held ? s.hold_stride : stride) & 0xffu) | (fd.held ? kInfoPool | kInfoHeld : 0u) | (alpha ? kInfoAlpha : 0u) | (s.hair?kInfoHairProcedural:0u) | (opaque_material?native_hit::nh_opaque_meta:0u) | (fd.camera_fade?kInfoCameraFade:0u) | groom_bit,
                             alpha ? 16 + fd.rec * kRecBytes : opaque_material?fd.material_record:0u, fd.prims};
    if (fd.camera_fade) camera_fade_marked(1, fd.prims);
    std::memcpy(inst_out + n, &in, sizeof in);
    std::memcpy(info_out + n * 4, inf, sizeof inf);
    inst_ref.push_back(in.accelerationStructureReference);
    inst_flags.push_back(in.flags);
    ++n;
    dyn_tris += fd.prims;
    if(alpha && alpha_test)g_st.omm_alpha_dyn_tris+=fd.prims;
    ++g_st.dyn_draws;
  }
  // rt_cascade_casters: last frame's cascade-only soup (camera-relative to last frame's
  // camera), translated into this frame's space; mask 0x02 (only the sun's rays see it) and
  // not opaque, so the sun's variant can ignore its hits near the ray origin (rt_common.hlsli
  // RT_CASCADE_TMIN). InstInfo: no vertices (tris 0), never read by those rays.
  bool casc_build = false, casc_refit = false;
  SlotCascade slot_casc{};
  VkDeviceSize casc_scratch = 0;
  // rt_cascade_refit=1: the same draw list as the last build (cap.cascade_sig, same triangle
  // count) refits the BLAS instead of rebuilding it, up to rt_cascade_rebuild_frames refits.
  const bool casc_refit_on = cfg_int("rt_cascade_refit", 0) != 0;
  const VkBuildAccelerationStructureFlagsKHR casc_flags =
      VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
      (casc_refit_on ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0);
  if (casc) {
    casc_refit = casc_refit_on && g_casc_as.as && g_casc_sig == cap.cascade_sig && g_casc_tris == cap.cascade_tris &&
                 g_casc_update_scratch && g_casc_refits < uint32_t((std::max)(cfg_int("rt_cascade_rebuild_frames", 60), 1));
    bool ok = casc_refit;
    if (!casc_refit) {
      if (g_casc_as.as) as_destroy(g_casc_as);
      const VkAccelerationStructureBuildSizesInfoKHR sz =
          blas_sizes(cap.cascade_tris, cap.cascade_stride, false, casc_flags);
      ok = as_create(sz.accelerationStructureSize, g_casc_as, nullptr, 2);
      casc_scratch = sz.buildScratchSize;
      g_casc_update_scratch = casc_refit_on ? sz.updateScratchSize : 0;
      g_casc_sig = cap.cascade_sig;
      g_casc_tris = cap.cascade_tris;
      g_casc_refits = 0;
      ++g_casc_n_build;
    } else {
      casc_scratch = g_casc_update_scratch;
      ++g_casc_refits;
      ++g_casc_n_refit;
    }
    if ((g_casc_n_build + g_casc_n_refit) % 600 == 0 && casc_refit_on)
      log_stats("[cascade] BLAS: %llu builds, %llu refits", (unsigned long long)g_casc_n_build,
          (unsigned long long)g_casc_n_refit);
    if (ok) {
      casc_build = true;
      VkAccelerationStructureInstanceKHR in{};
      in.transform.matrix[0][0] = in.transform.matrix[1][1] = in.transform.matrix[2][2] = 1.0f;
      for (int k = 0; k < 3; ++k) in.transform.matrix[k][3] = cap.cascade_t[k];
      in.instanceCustomIndex = cascade::kCustomIndex;
      in.mask = cascade::kMask;
      in.instanceShaderBindingTableRecordOffset = 0;
      in.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
      in.accelerationStructureReference = g_casc_as.addr;
      const uint32_t inf[4] = {0, cap.cascade_stride & 0xffu, 0, 0};
      slot_casc = {n, cap.cascade_tris, cap.cascade_stride, cap.cascade_xfb, cap.cascade_frame, cap.cascade_sig};
      std::memcpy(inst_out + n, &in, sizeof in);
      std::memcpy(info_out + n * 4, inf, sizeof inf);
      inst_ref.push_back(in.accelerationStructureReference);
      inst_flags.push_back(in.flags);
      ++n;
      geo_cascade_note_build(0, cap.cascade_tris);
    } else {
      geo_cascade_note_build(5, 0);
    }
  } else if (g_casc_as.as) {
    as_destroy(g_casc_as);   // the key went off, or no soup this frame
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

  bvh_cpu_mark(3);
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
  VkBuffer hair_status{};VkDeviceSize hair_status_base{};
  if(alpha_hair_build_status(slot,&hair_status,&hair_status_base)){
    dd->vk.CmdFillBuffer(cmd,hair_status,hair_status_base+12,4,0);
    barrier(cmd,kXfer,VK_ACCESS_TRANSFER_WRITE_BIT,kCS,kRW);
  }
  for(const auto& work:hair_bounds_work){
    if(!hair_status)return false;
    const VkDescriptorBufferInfo b[3]={{work.pool_source?g_pool.b:cap.xfb,0,work.pool_source?g_pool.size:cap.xfb_size},
                                     {g_pool.b,0,g_pool.size},{hair_status,hair_status_base,512}};
    VkWriteDescriptorSet w[3];for(uint32_t i=0;i<3;++i){w[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w[i].dstBinding=i;w[i].descriptorCount=1;w[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w[i].pBufferInfo=b+i;}
    dd->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,g_hair_pipe);
    dd->vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,g_hair_pl,0,3,w);
    dd->vk.CmdPushConstants(cmd,g_hair_pl,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(work.push),&work.push);
    dd->vk.CmdDispatch(cmd,work.push.step/128u,1,1);
  }
  if(hair_status)barrier(cmd,kCS,VK_ACCESS_SHADER_WRITE_BIT,kAS|kCS,kRW);
  if (!groom_work.empty() || !g_groom_uploads.empty() || !groom_uv_copies.empty()) {
    // rt_hair_groom: groom arrays into the pool, compacted copies of last builds, then this frame's points.
    barrier(cmd, kCS | kXfer | kAS, VK_ACCESS_MEMORY_WRITE_BIT, kXfer | kAS, kRW);
    for (auto& u : g_groom_uploads) {
      u.lod->upload_rec = rec; u.lod->upload_frame = now;
      const VkBufferCopy c{0, u.dst, u.bytes};
      dd->vk.CmdCopyBuffer(cmd, u.staging.b, g_pool.b, 1, &c);
      free_buf(u.staging);
    }
    g_groom_uploads.clear();
    for (const GroomUvCopy& c : groom_uv_copies) {
      const uint32_t count = g_groom_uv_jobs[c.job].count;
      VkBufferCopy r[hair_groom::kSamples];
      for (uint32_t s = 0; s < hair_groom::kSamples; ++s)
        r[s] = {c.soup_off + VkDeviceSize(hair_groom::uv_sample_corner(s, count)) * stride + 16, VkDeviceSize(c.job) * 256 + s * 8, 8};
      dd->vk.CmdCopyBuffer(cmd, cap.xfb, g_groom_uv_rb.b, hair_groom::kSamples, r);
    }
    // rt_hair_groom_primary: the strand passes of the game's frame (G-buffer variant, coverage query, velocity) traced the
    // previous publish's groom TLAS and read its points earlier on this queue; this build rewrites the same BLAS (full
    // builds rewrite node pointers) and points ranges in place. Their reads finish first (write after read: an
    // execution dependency; hv5/hc1 2026-11-04 lost the device in loads, where LOD changes force full builds).
    if (!groom_work.empty() && hair_primary_wanted())
      barrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, kXfer | kCS | kAS, 0);
    for (GroomWork& w : groom_work) {
      if (w.compact_copy) {
        VkCopyAccelerationStructureInfoKHR cp{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
        cp.src = w.old.as; cp.dst = w.g->as.as; cp.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
        dd->vk.CmdCopyAccelerationStructureKHR(cmd, &cp);
        as_destroy(w.old, kGroomFreeLag);
      }
      dd->vk.CmdFillBuffer(cmd, g_pool.b, w.g->bounds_off, 12, 0xFFFFFFFFu);
      dd->vk.CmdFillBuffer(cmd, g_pool.b, w.g->bounds_off + 12, 12, 0u);
    }
    barrier(cmd, kXfer | kAS, VK_ACCESS_MEMORY_WRITE_BIT, kCS | kAS, kRW);
    if (!groom_work.empty()) {
      const VkDescriptorBufferInfo b[2] = {{cap.xfb, 0, cap.xfb_size}, {g_pool.b, 0, g_pool.size}};
      VkWriteDescriptorSet wr[2];
      for (uint32_t i = 0; i < 2; ++i) {
        wr[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; wr[i].dstBinding = i; wr[i].descriptorCount = 1;
        wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; wr[i].pBufferInfo = b + i;
      }
      dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_groom_pipe);
      dd->vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_groom_pl, 0, 2, wr);
      for (const GroomWork& w : groom_work) {
        const GroomPush gp{w.l->points, uint32_t(w.soup_off), stride, uint32_t(w.l->tri), uint32_t(w.l->bary),
                           uint32_t(w.g->pos_off), uint32_t(w.g->bounds_off), uint32_t(w.g->uv_off),
                           uint32_t(w.g->prev_off), {w.prims, 0, 0}};
        dd->vk.CmdPushConstants(cmd, g_groom_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof gp, &gp);
        dd->vk.CmdDispatch(cmd, (w.l->points + 127) / 128, 1, 1);
        ++w.g->prev_frames;
      }
      barrier(cmd, kCS, VK_ACCESS_SHADER_WRITE_BIT, kAS | kCS | kXfer, kRW);
      for (const GroomWork& w : groom_work) {
        const uint32_t slot = uint32_t(now % 4);
        const VkBufferCopy c{w.g->bounds_off, 64 + 32 * slot, 24};
        dd->vk.CmdCopyBuffer(cmd, g_pool.b, w.g->readback.b, 1, &c);
        w.g->bounds_frame[slot] = now;
      }
    }
  }
  if (!hold_copies.empty()) {
    // Conversion -> copy source; prior frame's shader reads -> snapshot writes.
    barrier(cmd, kCS | kXfer, kRW, kXfer, kRW);
    std::vector<VkBufferCopy> copies;
    for (const HoldCopy& c : hold_copies) {
      copies.push_back({c.src, c.dst, c.bytes});
      g_st.hold_copy_bytes += c.bytes;
    }
    g_st.hold_copy_jobs += hold_copies.size();
    dd->vk.CmdCopyBuffer(cmd, cap.xfb, g_pool.b, uint32_t(copies.size()), copies.data());
    barrier(cmd, kXfer, VK_ACCESS_TRANSFER_WRITE_BIT, kCS | kAS, kRW);
  }
  if (!hold_pack_jobs.empty()) {
    // This retired scene slot owns the matrix ring tail. No per-draw dispatch,
    // new metadata buffer, or host-visible shader atomics are needed.
    const VkDeviceSize jobs_offset = rbase + g_mat_off + VkDeviceSize(n_mats)*48u;
    const VkDeviceSize jobs_bytes = VkDeviceSize(hold_pack_jobs.size())*sizeof(DynHoldPackJob);
    std::memcpy(g_ring.map + jobs_offset, hold_pack_jobs.data(), size_t(jobs_bytes));
    barrier(cmd, kCS | kXfer | VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT,
            kCS, kRW);
    const VkDescriptorBufferInfo buffers[3] = {{cap.xfb,0,cap.xfb_size},{g_pool.b,0,g_pool.size},
                                             {g_ring.b,rbase+g_mat_off,VkDeviceSize(g_ring_mats)*48u}};
    VkWriteDescriptorSet writes[3]{};
    for (uint32_t i=0;i<3;++i) {
      writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; writes[i].dstBinding=i;
      writes[i].descriptorCount=1; writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[i].pBufferInfo=buffers+i;
    }
    dd->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,g_hold_pack_pipe);
    dd->vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,g_hair_pl,0,3,writes);
    const uint32_t count=uint32_t(hold_pack_jobs.size()), gx=(std::min)(count,65535u);
    const uint32_t push[4]={n_mats*48u,count,gx,0};
    dd->vk.CmdPushConstants(cmd,g_hair_pl,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),push);
    dd->vk.CmdDispatch(cmd,gx,(count+gx-1u)/gx,1);
    g_st.hold_pack_jobs += count;
    for (const auto& job : hold_pack_jobs) g_st.hold_pack_bytes += uint64_t(job.vertices)*24u;
    barrier(cmd,kCS,VK_ACCESS_SHADER_WRITE_BIT,kCS|kAS,kRW);
  }
  fault_checkpoint(cmd, kCkBvhConverted, uint32_t(rec));
  // rt_omm_dyn: bakes of new micro-maps from this frame's converted soup, then the per-triangle uv checks that
  // write the indices this frame's per-draw BLAS builds read.
  if (omm_dyn_on && (!omm_bakes.empty() || !omm_checks.empty())) {
    if (g_omm_dyn_stats_zero) {
      dd->vk.CmdFillBuffer(cmd, g_omm_dyn_stats.b, 0, kOmmDynStatsBytes, 0);
      g_omm_dyn_stats_zero = false;
      barrier(cmd, kXfer, VK_ACCESS_TRANSFER_WRITE_BIT, kCS, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    constexpr VkBufferUsageFlags kIn = VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT;
    for (const OmmBake& b : omm_bakes) {
      // A refusal earlier in this loop proves this one (no buffers, no bake dispatch).
      if (!g_omm_dyn_refusal.allow(b.tris)) { ++g_st.omm_dyn_deferred; continue; }
      DynMicromap e;
      e.tris = b.tris; e.mask = b.view; e.rec = rec; e.last_use = now;
      const VkDeviceSize db = VkDeviceSize(b.tris) * 64u, sb = VkDeviceSize(b.tris) * 24u, tb = VkDeviceSize(b.tris) * 8u;
      const bool made = make_buf(e.data, db, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | kIn, false, "OMM dyn micro-map data") &&
                        make_buf(e.snapshot, sb, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "OMM dyn uv snapshot") &&
                        make_buf(e.descriptors, tb, kIn, true, "OMM dyn micro-map descriptors");
      for (Buf* p : {&e.data, &e.snapshot, &e.descriptors}) g_omm_dyn_bytes += p->size;
      if (!made) {
        omm_dyn_free(e); ++g_st.omm_dyn_failed;
        g_omm_dyn_refusal.refused(b.tris, now, omm::live_bytes(), omm_retry);
        continue;
      }
      std::vector<omm::Triangle> tris(b.tris);
      for (uint32_t i = 0; i < b.tris; ++i) tris[i] = {i * 64u, uint16_t(omm_mask::kOmbLevel), uint16_t(VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT)};
      std::memcpy(e.descriptors.map, tris.data(), size_t(tb));
      const uint32_t gx = (std::min)(b.tris, 65535u), gy = (b.tris + gx - 1) / gx;
      const OmmDynPush pc{uint32_t(b.soup_off), stride, b.tris, gx, b.cut_lo, b.cut_hi, b.su, b.sv, 0, b.w, b.h, 0};
      omm_dyn_dispatch(cmd, g_omm_dyn_pipe.bake, pc, cap.xfb, cap.xfb_size, e, gx, gy);
      omm::Input input{};
      input.descriptors = tris;
      input.data = e.data.addr; input.dataBytes = db;
      input.triangles = e.descriptors.addr; input.triangleBytes = tb;
      input.dataUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | kIn | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
      input.triangleUsage = kIn | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
      e.ticket = omm::record(cmd, input);   // its barriers order the bake's writes before the micro-map build
      // A failed record leaves the dispatch recorded: free_buf defers the destruction past every frame in flight.
      if (!e.ticket) {
        omm_dyn_free(e); ++g_st.omm_dyn_failed;
        g_omm_dyn_refusal.refused(b.tris, now, omm::live_bytes(), omm_retry);
        continue;
      }
      ++g_st.omm_dyn_baked;
      g_st.omm_dyn_bake_tris += b.tris;
      g_omm_dyn_cached_tris += b.tris;
      g_omm_dyn.emplace(b.key, std::move(e));
    }
    for (const OmmCheck& c : omm_checks) {
      const OmmDynPush pc{uint32_t(c.soup_off), stride, c.tris, 0, 0.0f, 0.0f, 0.0f, 0.0f, uint32_t(c.index_off), 0, 0, 0};
      omm_dyn_dispatch(cmd, g_omm_dyn_pipe.check, pc, cap.xfb, cap.xfb_size, *c.e, (c.tris + 63) / 64, 1);
    }
    barrier(cmd, kCS, VK_ACCESS_SHADER_WRITE_BIT, kAS | kXfer,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_TRANSFER_READ_BIT);
    const VkBufferCopy copy{0, 0, kOmmDynStatsBytes};
    dd->vk.CmdCopyBuffer(cmd, g_omm_dyn_stats.b, g_omm_dyn_readback.b, 1, &copy);
  }
  JobBatch batch{now, 0, {}, {}, {}, rec};
  batch.command = reinterpret_cast<uint64_t>(cmd);
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
      batch.jobs.push_back({e.key, mi != g_meshes.end() ? mi->second.gen : 0, e.status, e.kind, e.matn != kNone, moving});
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

  bvh_cpu_mark(4);
  VkDeviceAddress groom_tlas_address = 0;   // rt_hair_groom: this slot's groom TLAS (0: none)
  hair_primary::Row hp_rows[hair_primary::kMaxGrooms] = {};   // rt_hair_groom_primary: groom k's pool offsets
  uint64_t hp_keys[hair_primary::kMaxGrooms] = {};            // and its draw's index-range key
  uint8_t hp_lods[hair_primary::kMaxGrooms] = {};             // and its LOD (diagnostics)
  uint32_t groom_count = 0;
  // BLAS builds: new meshes (object space, from the pool) and the per-draw BLAS (from the soup).
  {
    std::vector<VkAccelerationStructureGeometryKHR> geo;
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
    geo.reserve(mesh_builds.size() + dyn_builds.size() + 1);
    std::vector<VkAccelerationStructureTrianglesOpacityMicromapEXT> omm_attach;
    std::vector<VkMicromapUsageEXT> omm_use;
    omm_attach.reserve(dyn_builds.size());
    omm_use.reserve(dyn_builds.size());
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
      geo.push_back(db.slot->hair?hair_geometry(g_pool.addr+db.slot->hair_bounds_off):tri_geometry(soup_addr + db.soup_off, db.prims, stride, !db.slot->alpha));
      if (db.omm) {
        // rt_omm_dyn: the micro-map, indexed per triangle by this frame's check (the same flags and sizes as decided).
        omm_attach.emplace_back();
        omm_use.emplace_back();
        if (omm_dyn_attachment(db.omm, db.omm_tris, db.prims, g_pool.addr + db.slot->omm_index_off, &omm_attach.back(), &omm_use.back()))
          geo.back().geometry.triangles.pNext = &omm_attach.back();
        else
          log("[omm dyn] WARNING frame %llu: micro-map attachment lost between decision and build", (unsigned long long)now);
      }
      ranges.push_back({db.prims, 0, 0, 0});
      VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
      bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      bi.flags = db.slot->flags;
      bi.mode = db.refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      bi.srcAccelerationStructure = db.refit ? db.slot->as.as : VK_NULL_HANDLE;
      bi.dstAccelerationStructure = db.slot->as.as;
      bi.geometryCount = 1;
      infos.push_back(bi);
      scratch_off.push_back(scratch);
      scratch += ((db.refit ? db.slot->update_scratch : db.slot->build_scratch) + al - 1) / al * al;
    }
    std::vector<VkAccelerationStructureGeometryLinearSweptSpheresDataNV> groom_lss(groom_work.size());
    for (size_t k = 0; k < groom_work.size(); ++k) {
      GroomWork& w = groom_work[k];
      geo.push_back(groom_geometry(*w.l, w.g->pos_off, groom_lss[k]));
      ranges.push_back({w.l->segments, 0, 0, 0});
      VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
      bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      bi.flags = kGroomFlags;
      bi.mode = w.refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      bi.srcAccelerationStructure = w.refit ? w.g->as.as : VK_NULL_HANDLE;
      bi.dstAccelerationStructure = w.g->as.as;
      bi.geometryCount = 1;
      infos.push_back(bi);
      scratch_off.push_back(scratch);
      scratch += ((w.refit ? w.g->update_scratch : w.g->build_scratch) + al - 1) / al * al;
    }
    if (casc_build) {   // rt_cascade_casters: converted in place after last frame's cascade pass
      geo.push_back(tri_geometry(rt_buffer_address(cap.cascade_xfb), cap.cascade_tris, cap.cascade_stride, false));
      ranges.push_back({cap.cascade_tris, 0, 0, 0});
      VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
      bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      bi.flags = casc_flags;
      bi.mode = casc_refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      if (casc_refit) bi.srcAccelerationStructure = g_casc_as.as;
      bi.dstAccelerationStructure = g_casc_as.as;
      bi.geometryCount = 1;
      infos.push_back(bi);
      scratch_off.push_back(scratch);
      scratch += (casc_scratch + al - 1) / al * al;
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
    // Same device, capacity, flags and zero-address instance geometry as
    // ensure_tlas's query. Slot replacement refreshes this immutable result;
    // device teardown destroys the slot. No scene contents are cached.
    if (cfg_hot(CfgHot::cpu_opt2)) tsz = g_tlas[slot].sizes;
    else dd->vk.GetAccelerationStructureBuildSizesKHR(dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tbi, &cap_inst,
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
    for (GroomWork& w : groom_work) {
      if (w.refit || w.g->compact != 0 || g_groom_free_queries.empty()) continue;
      w.g->query = g_groom_free_queries.back();
      g_groom_free_queries.pop_back();
      dd->vk.CmdResetQueryPool(cmd, g_groom_qp, w.g->query, 1);
      dd->vk.CmdWriteAccelerationStructuresPropertiesKHR(cmd, 1, &w.g->as.as, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                                                         g_groom_qp, w.g->query);
      dd->vk.CmdCopyQueryPoolResults(cmd, g_groom_qp, w.g->query, 1, w.g->readback.b, 32, 8,
                                     VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
      w.g->compact = 1;
      w.g->query_frame = now; w.g->query_rec = rec;
    }
    for (GroomWork& w : groom_work) w.g->built = true;
    // All fallible build allocations have succeeded. A recorded relocation
    // must reach g_batches, even if an earlier scratch allocation would fail.
    if (ext.empty() && !n_cq) relocate_cached_blas(cmd, batch);
    {   // the done word, after every job, compaction copy and BLAS build of this batch
      barrier(cmd, kCS | kXfer | kAS, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, kXfer,
              VK_ACCESS_TRANSFER_WRITE_BIT);
      dd->vk.CmdFillBuffer(cmd, batch.relocation ? g_compact_status.b : g_status.b,
                          batch.relocation ? 16 : VkDeviceSize(batch.done_slot) * 16, 16, kDoneMagic);
      barrier(cmd, kXfer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    }    stamp(cmd, 2);

    // TLAS over this frame's instances.
    tg.geometry.instances.data.deviceAddress = g_ring.addr + rbase;
    uint64_t sig = 1469598103934665603ull;
    for (uint32_t i = 0; i < n; ++i) sig = mix(mix(sig, inst_ref[i]), inst_flags[i]);
    // Every BLAS reference must lie in one of our chunks (a cheap guard; the fault
    // report checks each against the BLAS we hold).
    {
      static uint32_t bad_logged = 0;
      for (uint32_t i = 0; i < n; ++i) {
        const uint64_t r = inst_ref[i];
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
    // rt_hair_groom: the groom TLAS (own scratch: no barrier against the main TLAS build).
    groom_tlas_address = 0;
    groom_count = 0;
    if (!groom_work.empty() && ensure_tlas(slot, kGroomMax, &g_groom_tlas[slot]) &&
        (g_groom_inst[slot].b || make_buf(g_groom_inst[slot], VkDeviceSize(kGroomMax) * sizeof(VkAccelerationStructureInstanceKHR),
                                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true, "hair groom instances"))) {
      auto* gi = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(g_groom_inst[slot].map);
      for (const GroomWork& w : groom_work) {
        if (w.card == UINT32_MAX) continue;   // its card instance was not written (cannot happen; be safe)
        VkAccelerationStructureInstanceKHR in{};
        in.transform.matrix[0][0] = in.transform.matrix[1][1] = in.transform.matrix[2][2] = 1.0f;
        in.instanceCustomIndex = groom_count;
        // 5i (review 6): a groom without a primary key (no UVs yet, first update of its points) keeps its cards in the
        // G-buffer, so the primary strand rays (mask 0xFE, hair_primary_gbuffer.hlsl) skip it: its strands would be the
        // closest hit over a swapped draw's own and be discarded there (a hole). Shadow and GI rays (0xFF) still see it.
        // Round 3 H2: a slot that already published its key keeps it on a LOD switch frame (prev_frames 1): the new
        // LOD's points, UVs and BLAS are all written by this build, before this TLAS. Dropping it freed the draw slot,
        // put the draw in card mode at submit and refused the next frames: a strand-card-strand flash per LOD change.
        const bool primary_key = groom_count < hair_primary::kMaxGrooms &&
                                 hair_primary::groom_primary_key(w.g->uv_off != 0, w.hp_key, w.g->prev_frames, w.g->keyed);
        in.mask = hp_on && !primary_key ? 0x01 : 0xFF;
        in.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
        in.accelerationStructureReference = w.g->as.addr;
        std::memcpy(gi + groom_count, &in, sizeof in);
        // InstInfo row n + k: the card instance and the segment -> card triangle table (rt_common.hlsli rt_groom_trace).
        // Word 2: the strands' shadow transmittance (rt_hair_groom_shadow_t; 0: they block, as before).
        // Word 3: the segment start table (rt_common.hlsli rt_groom_links: chained segments share a joint).
        const uint32_t row[4] = {w.card, uint32_t(w.l->segtri), hair_primary_transmit(w.hp_key), uint32_t(w.l->seg)};
        std::memcpy(info_out + (n + groom_count) * 4, row, sizeof row);
        if (frame_index() % 300 == 0 && groom_count < 6)
          log("[hair groom] diag groom %u: camera-relative bounds centre (%.3f %.3f %.3f) of frame %.0f (now %llu), "
              "size %.3f, LOD %d (ready %d), BLAS 0x%llx, built %d, refit %d, points off %llu, segments %u",
              groom_count, w.g->centre[0], w.g->centre[1], w.g->centre[2], w.g->bounds_frame_used,
              (unsigned long long)frame_index(), w.g->size, w.g->lod, int(w.l->ready), (unsigned long long)w.g->as.addr,
              int(w.g->built), int(w.refit),
              (unsigned long long)w.g->pos_off, unsigned(w.l->segments));
        if (groom_count < hair_primary::kMaxGrooms) {
          // Previous points: valid from the second update of this points range (the first copies stale bytes).
          hp_rows[groom_count] = {uint32_t(w.g->pos_off), uint32_t(w.g->uv_off), uint32_t(w.l->seg),
                                  w.prev_ok && w.g->prev_frames > 1 ? uint32_t(w.g->prev_off) : 0u};   // M1: last frame's
          // No UVs yet, or a points range in its first update (new LOD or upload: its BLAS and points exist on the
          // GPU only from this build on): its draw keeps the card.
          // H2: a slot that published its key keeps it through a LOD switch (primary_key above).
          hp_keys[groom_count] = primary_key ? w.hp_key : 0;
          if (primary_key) w.g->keyed = true;
          hp_lods[groom_count] = uint8_t(w.g->lod < 0 ? 0 : w.g->lod);
        }
        ++groom_count;
      }
      VkAccelerationStructureGeometryKHR gg{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
      gg.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
      gg.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
      gg.geometry.instances.data.deviceAddress = g_groom_inst[slot].addr;
      VkAccelerationStructureBuildGeometryInfoKHR gbi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
      gbi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
      gbi.flags = kTlasFlags;
      gbi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      gbi.geometryCount = 1;
      gbi.pGeometries = &gg;
      gbi.dstAccelerationStructure = g_groom_tlas[slot].as;
      uint32_t gcap = g_groom_tlas[slot].cap;
      VkAccelerationStructureBuildSizesInfoKHR gsz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
      dd->vk.GetAccelerationStructureBuildSizesKHR(dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &gbi, &gcap, &gsz);
      const VkDeviceSize need = gsz.buildScratchSize + al;
      if (groom_count && (g_groom_scratch.size >= need ||
                          ((g_groom_scratch.b ? (free_buf(g_groom_scratch), true) : true) &&
                           make_buf(g_groom_scratch, need, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "hair groom TLAS scratch")))) {
        gbi.scratchData.deviceAddress = (g_groom_scratch.addr + al - 1) / al * al;
        VkAccelerationStructureBuildRangeInfoKHR gr{groom_count, 0, 0, 0};
        const VkAccelerationStructureBuildRangeInfoKHR* grp = &gr;
        dd->vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &gbi, &grp);
        VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        ai.accelerationStructure = g_groom_tlas[slot].as;
        groom_tlas_address = dd->vk.GetAccelerationStructureDeviceAddressKHR(dd->device, &ai);
        if (hp_on) {
          uint32_t w = 0, h = 0;
          float viewport[4];
          geo_render_size(&w, &h);
          geo_gbuffer_viewport(viewport);
          hair_primary_publish(cmd, rec, groom_tlas_address, g_pool.addr, g_pool.size, hp_rows, hp_keys, hp_lods, groom_count, w, h, viewport);
        }
      } else {
        groom_count = 0;
      }
    }
  }
  // No groom TLAS this frame (allocation or scratch failed): the cards marked replaced while the instance list was
  // written keep their card path (rt_common.hlsli skips a replaced card, and rt_groom_trace finds no strand).
  if (!groom_tlas_address)
    for (const GroomWork& w : groom_work)
      if (w.card != UINT32_MAX) info_out[w.card * 4 + 1] &= ~kInfoGroomReplaced;
  // Preserve the baseline mesh BLAS and TLAS. Material-specific variants are
  // separate, and expose instance ranges for the accelerated scene clone.
  if (omm_dyn_on) {
    if (!omm_enable.empty() && build_omm_dyn_scene(cmd, slot, n, rbase, rec, omm_enable)) {
      g_omm_dyn_tickets[slot] = omm_tickets;
      ++g_st.omm_dyn_scenes;
    }
    omm_draws.clear();   // rt_omm_dyn replaces the static pool-variant path (one accelerated TLAS per slot)
  }
  for(const auto& draw:omm_draws) {
    const auto& fd=g_draws[draw.draw];auto found=g_meshes.find(fd.mesh_key);
    if(found==g_meshes.end() || !fd.omm_material)continue;
    if(auto* variant=native_omm_variant(cmd,rec,found->second,*fd.omm_material,stride)) {
      g_omm_ranges[slot].push_back({draw.first,draw.count,fd.mesh_key,found->second.gen,variant->baked,variant->blas,
                                   fd.omm_material->source.buffer,fd.omm_material->source.bytes});
      g_st.omm_range_tris+=uint64_t(found->second.tris)*draw.count;
    }
  }
  if(!g_omm_ranges[slot].empty()) {
    if(build_omm_scene(cmd,slot,n,rbase,rec))++g_st.omm_scenes;else ++g_st.omm_scene_declined;
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
  g_slot_casc[slot] = slot_casc;
  ++g_st.builds;
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
  out->info_bytes = VkDeviceSize((std::max)(n + groom_count, 1u)) * 16;
  out->groom_tlas = groom_tlas_address;
  out->groom_count = groom_count;
  out->groom_first = n;
  g_groom_st.max_instances = (std::max)(g_groom_st.max_instances, groom_count);
  if (g_groom_on && (cfg_int("rt_stats_period_frames", 0) <= 0 ? now % 300 == 0 : now > g_groom_stats_start && (now - g_groom_stats_start) % stats_period(300) == 0)) {
    log_stats("[hair groom] frame %llu: %u groom instances; last %u frames: at most %u groom instances, matched %llu (LOD %u/%u/%u), full builds %llu, "
        "refits %llu, LOD switches waiting for upload %llu (kept) / %llu (groom dropped), compacted %llu, unmatched hair draws %llu (not indexed %llu, count unknown %llu, not mapped %llu, no file %llu, "
        "UV pending %llu, UV mismatch %llu); groom pool %.0f of %.0f MB",
        (unsigned long long)now, groom_count, stats_period(300), g_groom_st.max_instances, (unsigned long long)g_groom_st.matched, g_groom_st.lod[0], g_groom_st.lod[1],
        g_groom_st.lod[2], (unsigned long long)g_groom_st.builds, (unsigned long long)g_groom_st.refits,
        (unsigned long long)g_groom_st.lod_waits, (unsigned long long)g_groom_st.lod_drops, (unsigned long long)g_groom_st.compacted, (unsigned long long)g_groom_st.unmatched,
        (unsigned long long)g_groom_st.miss[1], (unsigned long long)g_groom_st.miss[2], (unsigned long long)g_groom_st.miss[3],
        (unsigned long long)g_groom_st.miss[4], (unsigned long long)g_groom_st.miss[5], (unsigned long long)g_groom_st.miss[6],
        double(g_groom_pool_used) / 1048576.0, double(hair_groom::pool_budget(groom_pool_capacity())) / 1048576.0);
    g_groom_st = GroomStats{};
  }
  out->pool = g_pool.b;
  out->pool_bytes = g_pool.size;
  if (cfg_int("rt_vram_compact", 0) || g_pool_snapshots) {
    g_pool_snapshots = true;native_hit::pool_compact_scene(slot);
  }
  out->rec = rec;

  if (cfg_int("rt_stability_log", 0) && now % 300 == 0) {
    uint32_t held = 0;
    for (const auto& [di, key] : dyn) held += key && g_draws[di].held;
    log("[bvh] dyn hold frame %llu: key %u, moving %u, held %u, copied %zu draws; last 300 frames: held %llu, "
        "not held: no snapshot %llu, stride %llu, unsubmitted %llu, epoch %llu, alpha %llu",
        (unsigned long long)now, hold_frames, moving ? 1u : 0u, held, hold_copies.size(),
        (unsigned long long)g_hold_why[6], (unsigned long long)g_hold_why[1], (unsigned long long)g_hold_why[2],
        (unsigned long long)g_hold_why[3], (unsigned long long)g_hold_why[4], (unsigned long long)g_hold_why[5]);
    for (uint64_t& v : g_hold_why) v = 0;
  }
  bvh_cpu_mark(-1);
  {
    // Static-camera scene stability (user report: small polygonal shadow patches flicker with
    // the camera still). With the camera static the game should draw the same set every frame;
    // count frames where the TLAS content changes anyway.
    static uint32_t p_static = 0, p_dyn = 0; static uint64_t p_dtris = 0, p_stris = 0;
    static uint64_t frames = 0, ch_static = 0, ch_dyn = 0, ch_dtris = 0, ch_stris = 0, newm = 0;
    static uint32_t s_min = UINT32_MAX, s_max = 0, d_min = UINT32_MAX, d_max = 0;
    const uint32_t ns = n_static_written, nd = n - n_static_written;
    if (!rt_camera_moving()) {
      ++frames;
      ch_static += ns != p_static; ch_dyn += nd != p_dyn; ch_dtris += dyn_tris != p_dtris; ch_stris += static_tris != p_stris;
      newm += mesh_builds.size();
      if ((ns != p_static || nd != p_dyn || dyn_tris != p_dtris) && cfg_int("rt_stability_log", 0))
        log("[bvh] static-camera change frame %llu: static instances %u -> %u, dynamic draws %u -> %u, dynamic triangles %llu -> %llu, "
            "new meshes %zu, cached draws %zu",
            (unsigned long long)now, p_static, ns, p_dyn, nd, (unsigned long long)p_dtris, (unsigned long long)dyn_tris,
            mesh_builds.size(), cached.size());
      s_min = (std::min)(s_min, ns); s_max = (std::max)(s_max, ns); d_min = (std::min)(d_min, nd); d_max = (std::max)(d_max, nd);
    }
    if (cfg_int("rt_stability_log", 0)) {
      // Which draws appear/disappear between consecutive builds (static camera only).
      static std::map<uint64_t, uint32_t> prev_dyn, prev_inst;
      std::map<uint64_t, uint32_t> cur_dyn, cur_inst;
      for (auto& [di, key] : dyn) if (key) cur_dyn[g_draws[di].dyn_key ^ (uint64_t(g_draws[di].audit) << 63)] += g_draws[di].prims;
      for (uint32_t di : cached) cur_inst[g_draws[di].mesh_key] += g_draws[di].instances;
      if (!rt_camera_moving() && !prev_dyn.empty()) {
        std::string d; int shown = 0;
        auto diff = [&](const std::map<uint64_t, uint32_t>& a, const std::map<uint64_t, uint32_t>& b, const char* what) {
          for (auto& [k, v] : b) {
            auto it = a.find(k);
            const uint32_t old_v = it == a.end() ? 0 : it->second;
            if (old_v != v && shown++ < 8) { char t[96]; snprintf(t, sizeof t, " %s %016llx %u->%u;", what, (unsigned long long)k, old_v, v); d += t; }
          }
          for (auto& [k, v] : a)
            if (!b.count(k) && shown++ < 8) { char t[96]; snprintf(t, sizeof t, " %s %016llx %u->0;", what, (unsigned long long)k, v); d += t; }
        };
        diff(prev_dyn, cur_dyn, "dyn(prims)");
        diff(prev_inst, cur_inst, "mesh(instances)");
        if (!d.empty()) log("[bvh] static-camera draw diff frame %llu:%s", (unsigned long long)now, d.c_str());
      }
      prev_dyn.swap(cur_dyn); prev_inst.swap(cur_inst);
    }
    p_static = ns; p_dyn = nd; p_dtris = dyn_tris; p_stris = static_tris;
    if (frames >= 300) {
      log("[bvh] static-camera stability over %llu frames: static instances changed in %llu (min %u max %u), "
          "dynamic draws changed in %llu (min %u max %u), static triangles changed in %llu, dynamic triangles changed in %llu, "
          "new meshes %llu",
          (unsigned long long)frames, (unsigned long long)ch_static, s_min, s_max, (unsigned long long)ch_dyn, d_min, d_max,
          (unsigned long long)ch_stris, (unsigned long long)ch_dtris, (unsigned long long)newm);
      frames = ch_static = ch_dyn = ch_dtris = ch_stris = newm = 0;
      s_min = d_min = UINT32_MAX; s_max = d_max = 0;
    }
  }
  if (++g_phase_n >= stats_period(300)) {
    if (cfg_int("cpu_timing", 0))
      log("[cpu] bvh_build phases, ms/build over %llu: decide %.3f, per-draw BLAS keys %.3f, TLAS instances %.3f, "
          "GPU convert/extract/compaction %.3f, BLAS/TLAS build recording %.3f",
          (unsigned long long)g_phase_n, g_phase_ms[0] / g_phase_n, g_phase_ms[1] / g_phase_n, g_phase_ms[2] / g_phase_n,
          g_phase_ms[3] / g_phase_n, g_phase_ms[4] / g_phase_n);
    for (double& v : g_phase_ms) v = 0;
    g_phase_n = 0;
  }
  return true;
}

// Called by alpha_commit after its scene-generation token is assigned. Failure
// leaves proof zero and therefore always selects the original reference scene.
bool bvh_omm_commit(VkCommandBuffer cmd,uint32_t slot,VkBuffer stamp,VkDeviceSize base,uint32_t generation,const uint32_t* mapped) {
  std::lock_guard lock(g_mu);
  if(!g_dd || slot>=kSceneSlots || !stamp || base!=VkDeviceSize(slot)*512u)return false;
  auto& vk=g_dd->vk;vk.CmdFillBuffer(cmd,stamp,base+128u,32u,0u);
  g_omm_expected[slot]=0;g_omm_mapped_stamp[slot]=nullptr;
  const auto* state=cmd_state(cmd);
  if(!mapped || !generation || !state || g_omm_commands[slot]!=cmd ||
     g_omm_recordings[slot]!=state->recording ||
     !omm::cached_bake_ready(g_omm_records[slot],g_omm_records[slot],bvh_rec_state(g_omm_records[slot])) ||
     (g_omm_ranges[slot].empty() && g_omm_dyn_tickets[slot].empty())) {
    barrier(cmd,kXfer,VK_ACCESS_TRANSFER_WRITE_BIT,kCS,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    return false;
  }
  if(g_omm_ranges[slot].empty()) {
    // rt_omm_dyn: micro-maps of immutable masks, checked per triangle before this scene's BLAS builds: the
    // proof is the generation alone ('OMM3', omm_scene_guard.hlsli), no content epochs.
    const uint32_t proof[4]={generation,0u,uint32_t(g_omm_dyn_tickets[slot].size()),0x4f4d4d33u};
    barrier(cmd,kXfer,VK_ACCESS_TRANSFER_WRITE_BIT,kXfer,VK_ACCESS_TRANSFER_WRITE_BIT);
    vk.CmdUpdateBuffer(cmd,stamp,base+128u,sizeof proof,proof);
    barrier(cmd,kXfer,VK_ACCESS_TRANSFER_WRITE_BIT,kCS|VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_HOST_READ_BIT);
    g_omm_expected[slot]=generation;g_omm_mapped_stamp[slot]=mapped;return true;
  }
  const auto& source=g_omm_ranges[slot][0];
  const VkDescriptorBufferInfo buffers[]={{g_omm_rows[slot].b,0,VkDeviceSize(g_omm_ranges[slot].size())*32},
                                         {source.epochs,0,source.epoch_bytes},{stamp,base,512}};
  VkWriteDescriptorSet writes[3]{};for(uint32_t i=0;i<3;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=buffers+i;}
  const uint32_t push[]={uint32_t(g_omm_ranges[slot].size()),generation,0,0};
  barrier(cmd,kAS|kCS|kXfer,VK_ACCESS_MEMORY_WRITE_BIT,kCS,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,g_omm_validate.pipeline);
  vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,g_omm_validate.layout,0,3,writes);
  vk.CmdPushConstants(cmd,g_omm_validate.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,push);vk.CmdDispatch(cmd,1,1,1);
  barrier(cmd,kCS,VK_ACCESS_SHADER_WRITE_BIT,kCS|VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_HOST_READ_BIT);
  g_omm_expected[slot]=generation;g_omm_mapped_stamp[slot]=mapped;return true;
}

VkAccelerationStructureKHR bvh_omm_scene(uint32_t slot,VkAccelerationStructureKHR reference){
  std::lock_guard lock(g_mu);
  if(slot>=kSceneSlots || !g_omm_expected[slot] || reference!=g_tlas[slot].as ||
     !omm::cached_bake_ready(g_omm_records[slot],g_omm_records[slot],bvh_rec_state(g_omm_records[slot])))return VK_NULL_HANDLE;
  return g_omm_tlas[slot].as;
}
bool bvh_omm_use(VkCommandBuffer cmd,uint32_t slot,VkAccelerationStructureKHR selected,uint64_t* identity){
  if(!identity)return false;*identity=0;
  std::lock_guard lock(g_mu);
  if(slot>=kSceneSlots || !selected || selected!=g_omm_tlas[slot].as || !g_omm_expected[slot] ||
     !cfg_hot(CfgHot::rt_omm) || !omm::available_for(g_dd) || (g_omm_ranges[slot].empty() && g_omm_dyn_tickets[slot].empty()) ||
     !omm::cached_bake_ready(g_omm_records[slot],g_omm_records[slot],bvh_rec_state(g_omm_records[slot])))return false;
  for(const auto& source:g_omm_ranges[slot])if(!omm::register_ticket_use(cmd,source.baked.ticket))return false;
  for(const uint64_t ticket:g_omm_dyn_tickets[slot])if(!omm::register_ticket_use(cmd,ticket))return false;
  *identity=g_omm_records[slot];return true;
}

// The matrices of this frame's TLAS instances and jobs, read from the game's
// instance stream as it submits the command buffer that draws with them.
uint64_t bvh_rec_register(VkCommandBuffer cmd) {
  std::lock_guard<std::mutex> fl(g_fix_mu);
  const uint64_t id = ++g_rec_next;
  g_rec_cmd[cmd].push_back(id);
  g_rec_state[id] = kRecPending;
  g_rec_owners.record(id,reinterpret_cast<uint64_t>(cmd));
  if (g_rec_state.size() > 8192)   // forget old records (an unknown one is never trusted)
    for (auto it = g_rec_state.begin(); it != g_rec_state.end();)
      if(it->first + 4096 < g_rec_next && !g_compact_records.contains(it->first)) {g_rec_owners.forget(it->first);it=g_rec_state.erase(it);} else ++it;
  return id;
}

int bvh_rec_state(uint64_t id) { return rec_state(id); }

bool bvh_rec_matches_command(uint64_t id,VkCommandBuffer command) {
  if(!id || !command) return false;
  std::lock_guard<std::mutex> lock(g_fix_mu);
  const auto state=g_rec_state.find(id);
  return state!=g_rec_state.end() && state->second!=kRecDropped &&
    g_rec_owners.matches(id,reinterpret_cast<uint64_t>(command));
}

// The game begins `cmd` again or frees it: whatever we recorded into it and did not
// see submitted never runs.
void bvh_cmd_reset(VkCommandBuffer cmd) {
  std::lock_guard<std::mutex> fl(g_fix_mu);
  g_pool_uncertain.reset(reinterpret_cast<uint64_t>(cmd));
  g_rec_owners.reset(reinterpret_cast<uint64_t>(cmd));
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
  const double t_fix0 = now_ms();
  uint64_t applied = 0, late = 0, bad = 0;
  const bool reuse = cfg_hot(CfgHot::perf_hook_state) != 0;
  perf::SubmitMatrices matrices; // destroyed before return; never reused across submits
  uint64_t unique = 0, build_count = 0;
  for (VkCommandBuffer c : cmds) {
    auto it = g_fixups.find(c);
    if (it == g_fixups.end()) continue;
    if (!it->second.empty()) ++build_count;
    for (const Fixup& f : it->second) {
      for (uint32_t i = 0; i < f.count; ++i) {
        uint8_t* d = f.dst + VkDeviceSize(i) * f.dst_stride;
        float m[12];
        bool ok;
        if (reuse) {
          perf::MatrixSource source;
          for (int k = 0; k < 3; ++k) source.rows[k] = f.src[k] + VkDeviceSize(i) * f.src_stride[k];
          const auto& value = matrices.get(source, load_mapped16);
          std::memcpy(m, value.rows.data(), sizeof m); ok = value.valid;
        } else {
          for (int k = 0; k < 3; ++k) load_mapped16(m + 4 * k, f.src[k] + VkDeviceSize(i) * f.src_stride[k]);
          ok = perf::valid_matrix(m);
          ++unique;
        }
        if (i == 0 && f.instance && std::memcmp(f.sample, m, 48)) ++late;
        if (i == 0 && f.log_key)
          log("[bvh-draw] frame %llu mesh %016llx VS %016llx tris %u firstInstance %u instanceCount %u translation %.6f %.6f %.6f instance %u",
              (unsigned long long)f.log_frame, (unsigned long long)f.log_key, (unsigned long long)f.log_vs, f.log_tris, f.log_first, f.count,
              m[3], m[7], m[11], f.log_instance);
        if (!ok) ++bad;
        perf::scatter_matrix(d, m, ok, f.instance);
      }
      applied += f.count;
    }
    g_fixups.erase(it);
  }
  { // cpu_timing: fresh samples after each mode/timing transition
    static double ms = 0; static uint64_t calls = 0, mats = 0, sources = 0, builds = 0;
    static int mode = -1;
    const bool timing = cfg_int("cpu_timing", 0) != 0;
    if (!timing || mode != int(reuse)) { ms = 0; calls = mats = sources = builds = 0; mode = int(reuse); }
    if (applied && timing) {
      ms += now_ms() - t_fix0; builds += build_count; mats += applied; sources += reuse ? matrices.size() : unique;
    }
    if (applied && timing && ++calls >= 300) {
      if (cfg_int("cpu_timing", 0))
        log("[cpu] submit-time instance matrix copies: %.3f ms per build (%.0f matrices); unique %.0f; builds %llu; submits %llu; perf_hook_state %d", ms / builds, double(mats) / builds, double(sources) / builds, (unsigned long long)builds, (unsigned long long)calls, int(reuse));
      ms = 0; calls = mats = sources = builds = 0;
    }
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
      for (const auto& [k, s] : g_dyn) {
        if (s.as.addr) live[s.as.addr] = "per-draw";
        if (s.groom && s.groom->as.addr) live[s.groom->as.addr] = "groom";
      }
      for (const JobBatch& b : g_batches)
        for (const Swap& s : b.swaps)
          if (s.c.addr) live[s.c.addr] = "pending compaction";
      // The cascade casters' BLAS (the last instance with rt_cascade_casters): live, not a released range (hv6
      // 2026-11-04 listed the current one as 'RELEASED range' because it was missing here).
      if (g_casc_as.addr) live[g_casc_as.addr] = "cascade";
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
  static int reset_token = 0;
  if (stats_window::reset(cfg_int, reset_token)) {
    std::lock_guard<std::mutex> sl(g_st_mu); g_st = Stats{};
    g_groom_st = GroomStats{}; g_groom_stats_start = now;
    for (double& v : g_phase_ms) v = 0; g_phase_n = 0;
  }
  pool_repack_poll(now);
  // Ranges of evicted meshes and slots, once no frame in flight can use them:
  // the GPU must have finished frame + 2 (as defer_tick), not just 6 presents
  // passed. Reusing a range a trace still reads was the intermittent device
  // lost a few seconds into gameplay (docs/archive/STATUS.md, "render stall").
  {
    // Also: no scene slot the GPU may still read was built at or before the free
    // (its TLAS can reference the range: an older TLAS is traced up to a frame later).
    const uint64_t gpu_done = life_gpu_done(), oldest_live = rt_scene_oldest_live();
    size_t w = 0;
    for (size_t i = 0; i < g_pending_free.size(); ++i) {
      const PendingFree& p = g_pending_free[i];
      bool copy_reads = false;
      for (const auto& b : g_batches) for (const auto& s : b.swaps)
        copy_reads = copy_reads || (s.relocation && p.as && s.source.as == p.as);
      if (copy_reads || !as_chunks::retired(p.frame, now, gpu_done, oldest_live)) {
        g_pending_free[w++] = p;
        continue;
      }
      retire_as(p.as, p.chunk);
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
    const auto* done_word = b.relocation ? g_compact_status.map + 16 : g_status.map + VkDeviceSize(b.done_slot) * 16;
    const bool done = *reinterpret_cast<const volatile uint32_t*>(done_word) == kDoneMagic;
    const int res = 0;
    // Its command buffer dropped without a submit: nothing of it ran, lost at once.
    const bool dropped = !done && rec_state(b.rec) == kRecDropped;
    const bool relocation = b.relocation;
    if (vram_compact::batch_wait(done, dropped, now >= b.frame + 240, relocation)) {
      ++bi;
      continue;
    }
    if (!done && !dropped && !b.jobs.empty()) {
      std::lock_guard<std::mutex> fl(g_fix_mu);
      if (g_rec_owners.matches(b.rec, b.command)) g_pool_uncertain.note(b.rec, b.command);
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
          if (j.moving && invalid == 0) {
            ++(ok ? g_st.audits_moving_ok : g_st.audits_moving_failed);
            if (std::isfinite(err)) g_st.err_max_moving = (std::max)(g_st.err_max_moving, err);
          }
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
        if (s.relocation) { ++g_relocated;g_relocated_bytes += s.c.size; }
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
    if (relocation) compact_record_pin(b.rec, false);
    g_batches.erase(g_batches.begin() + bi);
  }
  if (g_relocating_chunk >= 0) {
    bool pending = false;
    for (const auto& b : g_batches) for (const auto& s : b.swaps) pending = pending || s.relocation;
    // A dropped/evicted copy retains original ownership; allow retry. A fully
    // evacuated source stays excluded until its pending ranges retire below.
    if (!pending && g_chunks[g_relocating_chunk].ra.live) g_relocating_chunk = -1;
  }
  // Per-draw BLAS not seen for a while; the mesh cache over its memory budget (LRU).
  // Native hair raises the whole capture's stride to 88, including ordinary
  // dynamic draws. Their hold snapshots used to retain that larger payload for
  // the BLAS cache's 120-frame lifetime, although no hold can use it after four
  // absent frames by default. Trim ownership, then use the SAME deferred range
  // retirement as eviction; no live TLAS or bounds read can see reused storage.
  const bool hair_pool_trim = geo_native_hair_enabled() && cfg_int("rt_hair_pool_trim", 1) != 0;
  const uint32_t hold_horizon = uint32_t((std::clamp)(cfg_int("rt_dyn_hold", 4), 0, 120));
  for (auto it = g_dyn.begin(); it != g_dyn.end();) {
    if (hair_pool_trim && it->second.hold_size && dyn_hold_expired(now, it->second.last_used, hold_horizon)) {
      g_pending_free.push_back({now, -1, it->second.hold_off, it->second.hold_size});
      it->second.hold_size = 0;
      it->second.hold_rec = 0;
      it->second.hold_has_alpha = false;
      it->second.hold_alpha = {};
    }
    if (dyn_slot_expired(now, it->second.last_used, hold_horizon, hair_pool_trim, it->second.hair)) {
      as_destroy(it->second.as);
      if (it->second.hold_size)
        g_pending_free.push_back({now, -1, it->second.hold_off, it->second.hold_size});
      if(it->second.hair_bounds_size)
        g_pending_free.push_back({now,-1,it->second.hair_bounds_off,it->second.hair_bounds_size});
      if (it->second.omm_index_size)
        g_pending_free.push_back({now, -1, it->second.omm_index_off, it->second.omm_index_size});
      if (it->second.groom) groom_release(*it->second.groom, now);
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
  // Empty chunks go, independent of guard state/cap (after the reuse window below). used still owns
  // pending ranges until BOTH the fence and all old TLAS slots retire above.
  // free_buf uses the existing deferred destruction queue, after retired AS
  // handles. Retained diagnostic handles pin only their own backing chunks.
  // rt_as_chunk_reuse_frames: an empty chunk stays for that many frames to take the next new-chunk
  // allocation (as_chunks::keep_empty_chunk); a guard ask, an evacuated source or the key at 0 free it now.
  const bool yield_empty = vram_take_ask(vram_guard::kYieldEmptyChunks) != 0;
  const uint64_t reuse_frames = uint64_t((std::max)(cfg_int("rt_as_chunk_reuse_frames", 120), 0));
  for (size_t ci = 0; ci < g_chunks.size(); ++ci) {
    AsChunk& c = g_chunks[ci];
    if (!c.buf.b || !c.ra.reclaimable()) continue;
    if (int(ci) != g_relocating_chunk && as_chunks::keep_empty_chunk(c.ra, now, reuse_frames, yield_empty)) continue;
    ++g_chunks_freed;
    g_chunk_bytes_freed += c.buf.size;
    free_buf(c.buf);
    c.ra = as_chunks::ChunkRanges{}; // keep the vector slot: AsAlloc indices stay stable
  }
  if (g_relocating_chunk >= 0 && !g_chunks[g_relocating_chunk].buf.b) g_relocating_chunk = -1;
  if (const VkDeviceSize ask = vram_take_ask(vram_guard::kYieldMeshLru)) {
    std::vector<std::pair<uint64_t, uint64_t>> old;   // last used, key
    for (auto& [k, m] : g_meshes)
      if (m.last_used + 8 < now) old.push_back({m.last_used, k});
    std::sort(old.begin(), old.end());
    VkDeviceSize freed = 0;
    uint32_t n = 0;
    for (auto& [lu, k] : old) {
      if (freed >= ask) break;
      freed += g_meshes[k].as.size;
      evict(k);
      ++g_st.evict_lru;
      ++n;
    }
    if (n) log("[vram] guard: evicted %u LRU meshes (%.0f MB of BLAS ranges; asked %.0f MB)", n,
               double(freed) / 1048576.0, double(ask) / 1048576.0);
  }
  for (auto it = g_rejected.begin(); it != g_rejected.end();)
    it = now > it->second.until + 72000 ? g_rejected.erase(it) : std::next(it);

  int every = stats_period(unsigned((std::max)(cfg_int("rt_timing_frames", 300), 30)));
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
  log_stats("[bvh] last %llu builds, per build: %.1f cached draws -> %.0f static instances (%.3fM triangles), %.1f captured "
      "draws (%.3fM triangles; per-draw BLAS: %.1f refits, %.1f rebuilds, %.2f new); TLAS refit %.0f%%; not instanceable per build: "
      "pattern %.1f, stream %.1f, count %.1f; hold pack %llu jobs (%llu bytes), copy %llu jobs (%llu bytes), unavailable %llu builds",
      (unsigned long long)s.builds, s.cached_draws / nb, s.static_inst / nb, s.static_tris / nb * 1e-6, s.dyn_draws / nb,
      s.dyn_tris / nb * 1e-6, s.dyn_refit / nb, s.dyn_rebuild / nb, s.dyn_new / nb, 100.0 * s.tlas_refit / nb, s.not_inst_pattern / nb,
      s.not_inst_stream / nb, s.not_inst_count / nb,
      (unsigned long long)s.hold_pack_jobs, (unsigned long long)s.hold_pack_bytes,
      (unsigned long long)s.hold_copy_jobs, (unsigned long long)s.hold_copy_bytes,
      (unsigned long long)s.hold_pack_unavailable);
  size_t chunks = 0;
  VkDeviceSize live = 0;
  for (const AsChunk& c : g_chunks) {
    chunks += c.buf.b != VK_NULL_HANDLE;
    live += c.ra.live;
  }
  log_stats("[bvh] mesh cache: %zu meshes (%zu ready, %.2fM triangles), pool %.1f / %.0f MB, AS %.1f MB in %zu chunks; this "
      "window: %llu new (%.2fM triangles, %llu deferred by the per-build budget), %llu verified, %llu checks ok, "
      "%llu audits ok, rejected %llu/%llu/%llu (%s/%s/%s), %llu results lost (%llu timed out), max accepted error %.3f x tolerance; "
      "evicted %llu destroyed, %llu written, %llu LRU; %llu compactions (-%.1f MB); "
      "chunks reserved/live/pending %.1f/%.1f/%.1f MB, freed %llu (%.1f MB), relocated %llu (%.1f MB); "
      "pool repacks %llu (-%.1f MB)",
      g_meshes.size(), size_t(ready), double(mesh_tris) * 1e-6, double(g_pool_ra.used) / 1048576.0,
      double(g_pool.size) / 1048576.0, double(as_bytes()) / 1048576.0, chunks, (unsigned long long)s.new_meshes,
      s.new_tris * 1e-6, (unsigned long long)s.budget_deferred, (unsigned long long)s.ready,
      (unsigned long long)s.checks_ok, (unsigned long long)s.audits_ok, (unsigned long long)s.rejected[0],
      (unsigned long long)s.rejected[1], (unsigned long long)s.rejected[2], kRejectName[0], kRejectName[1],
      kRejectName[2], (unsigned long long)s.lost, (unsigned long long)s.lost_timeout, s.err_max_ok, (unsigned long long)s.evict_destroy,
      (unsigned long long)s.evict_write, (unsigned long long)s.evict_lru, (unsigned long long)s.compactions,
      double(s.compact_saved) / 1048576.0, double(as_reserved()) / 1048576.0,
      double(live) / 1048576.0, double(as_bytes() - live) / 1048576.0,
      (unsigned long long)g_chunks_freed, double(g_chunk_bytes_freed) / 1048576.0,
      (unsigned long long)g_relocated, double(g_relocated_bytes) / 1048576.0,
      (unsigned long long)g_pool_repacks, double(g_pool_saved) / 1048576.0);
  g_chunks_freed = g_chunk_bytes_freed = 0;
  log_stats("[bvh] instance matrices read at submit: %llu, non-finite/singular %llu; draws whose instance data changed "
      "between record and submit %llu; recorded but never submitted %llu",
      (unsigned long long)s.fix_applied, (unsigned long long)s.fix_bad, (unsigned long long)s.fix_late,
      (unsigned long long)s.fix_stale);
  if (omm::available_for(g_dd)) {
    // Coverage (docs/P4_OMM.md 9.3): only instance ranges with an attached micromap BLAS can skip
    // any-hit work, and only for their known-opaque microtriangles (the bake's states below).
    const double alpha_tris = double(s.omm_alpha_static_tris + s.omm_alpha_dyn_tris);
    uint64_t st[4] = {};
    const bool have_states = omm::bake_state_totals(st);
    log_stats("[omm] last %llu builds: alpha draws %llu, native plain captures %llu; alpha-tested triangles per build %.3fM "
        "(static %.3fM, per-draw %.3fM), with a capture %.3fM, attached micromaps %.3fM (%.4f%% of alpha-tested); variants "
        "baked %llu, reused %llu, declined %llu; accelerated scenes %llu, declined %llu; micromap+BLAS bytes %.2f MB, bake "
        "inputs %.2f MB; baked microtriangles so far %llu: known opaque %llu, known transparent %llu, unknown %llu%s",
        (unsigned long long)s.builds, (unsigned long long)s.omm_draws, (unsigned long long)s.omm_captured, alpha_tris / nb * 1e-6,
        s.omm_alpha_static_tris / nb * 1e-6, s.omm_alpha_dyn_tris / nb * 1e-6, s.omm_capture_tris / nb * 1e-6,
        s.omm_range_tris / nb * 1e-6, alpha_tris > 0 ? 100.0 * double(s.omm_range_tris) / alpha_tris : 0.0,
        (unsigned long long)s.omm_baked, (unsigned long long)s.omm_reused, (unsigned long long)s.omm_declined,
        (unsigned long long)s.omm_scenes, (unsigned long long)s.omm_scene_declined, double(omm::live_bytes()) / 1048576.0,
        double(omm::bake_input_bytes()) / 1048576.0, (unsigned long long)st[0], (unsigned long long)st[1],
        (unsigned long long)st[2], (unsigned long long)(st[0] - (std::min)(st[0], st[1] + st[2])),
        have_states ? "" : " (bake counters not read back yet)");
    if (cfg_int("rt_omm_dyn", 0) || !g_omm_dyn.empty()) {
      // rt_omm_dyn coverage and cost inputs (docs/P4_OMM.md 11.2c); micro-triangle states and uv checks are
      // cumulative device counters (omm_bake_mask.hlsl), read back after each build that baked or checked.
      uint32_t c[8] = {};
      if (g_omm_dyn_readback.map) std::memcpy(c, g_omm_dyn_readback.map, sizeof c);
      static uint32_t last_checked = 0, last_changed = 0;   // the check counters wrap: logged per window
      const uint32_t checked = c[3] - last_checked, changed = c[4] - last_changed;
      last_checked = c[3]; last_changed = c[4];
      const double micro = double(c[0]) + double(c[1]) + double(c[2]);
      log_stats("[omm dyn] last %llu builds (rt_omm_dyn %d): masked per-draw draws %.1f (%.3fM triangles) per build, with a micro-map "
          "%.1f (%.3fM triangles, %.1f%%); baked %llu micro-maps (%.3fM triangles), deferred %llu, failed %llu, evicted %llu; accelerated "
          "scenes %llu; cache %zu micro-maps, %.3fM triangles, our buffers %.2f MB, backend micro-map+scratch %.2f MB; baked "
          "micro-triangles so far: known opaque %.1f%%, known transparent %.1f%%, unknown %.1f%% (%u triangles); uv checks "
          "this window: %u triangles, %u changed",
          (unsigned long long)s.builds, cfg_int("rt_omm_dyn", 0), s.omm_dyn_draws / nb, s.omm_dyn_tris / nb * 1e-6,
          s.omm_dyn_att_draws / nb, s.omm_dyn_att_tris / nb * 1e-6,
          s.omm_dyn_tris ? 100.0 * double(s.omm_dyn_att_tris) / double(s.omm_dyn_tris) : 0.0, (unsigned long long)s.omm_dyn_baked,
          s.omm_dyn_bake_tris * 1e-6, (unsigned long long)s.omm_dyn_deferred, (unsigned long long)s.omm_dyn_failed,
          (unsigned long long)s.omm_dyn_evicted, (unsigned long long)s.omm_dyn_scenes, g_omm_dyn.size(), g_omm_dyn_cached_tris * 1e-6, double(g_omm_dyn_bytes) / 1048576.0,
          double(omm::live_bytes()) / 1048576.0, micro ? 100.0 * c[0] / micro : 0.0, micro ? 100.0 * c[1] / micro : 0.0,
          micro ? 100.0 * c[2] / micro : 0.0, c[5], checked, changed);
    }
  }
  if (s.audits_moving_ok + s.audits_moving_failed)
    log_stats("[bvh] audits while the camera moved: %llu ok, %llu failed, max error %.3f x tolerance "
        "(cached instance matrices at submit against the same frame's raster capture)",
        (unsigned long long)s.audits_moving_ok, (unsigned long long)s.audits_moving_failed, s.err_max_moving);
}

// Replay pack (layer/replay.cpp, rt_replay_region; caller holds rtscene's lock): the ring region
// whose InstInfo starts at `info_offset` of `info` (a set 1 b7 descriptor): its TLAS instance array
// (same buffer, the region's start, matrices fixed up at submit), the instance count and a CPU copy
// of the InstInfo (written at record, final once the build is recorded).
bool bvh_replay_region(VkBuffer info, VkDeviceSize info_offset, BvhReplayRegion* out) {
  if (!out || !g_ring.b || !g_ring.map || info != g_ring.b || !g_region_bytes || info_offset < g_info_off) return false;
  const VkDeviceSize rel = info_offset - g_info_off;
  if (rel % g_region_bytes) return false;
  const uint32_t slot = uint32_t(rel / g_region_bytes);
  if (slot >= kRingSlots) return false;
  const uint32_t n = g_slot_n[slot];
  if (!n || n > g_ring_inst) return false;
  out->ring = g_ring.b;
  out->ring_bytes = g_ring.size;
  out->instances_offset = VkDeviceSize(slot) * g_region_bytes;
  out->count = n;
  out->info.resize(size_t(n) * 4);
  std::memcpy(out->info.data(), g_ring.map + info_offset, size_t(n) * 16);
  const SlotCascade& c = g_slot_casc[slot];
  if (c.tris && c.instance < n) {
    out->casc_instance = c.instance; out->casc_tris = c.tris; out->casc_stride = c.stride;
    out->casc_xfb = c.xfb; out->casc_frame = c.frame; out->casc_sig = c.sig;
  }
  return true;
}

// A build record whose command buffer was submitted (hair_primary's ordered publish).
bool bvh_rec_submitted(uint64_t id) { return rec_submitted(id); }

}  // namespace bg3
