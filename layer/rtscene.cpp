// Track D phase 2: hardware ray tracing on the game's own device
// (docs/TRACK_D_RT.md).
//
// Device: with rt=1 the layer asks for instance API 1.3 (layer.cpp) and adds
// VK_KHR_acceleration_structure, VK_KHR_ray_query and
// VK_KHR_deferred_host_operations with their features to a copy of the game's
// device create info. Buffer device addresses come from the game's own
// VK_EXT_buffer_device_address (enabling VK_KHR_buffer_device_address as well
// is forbidden, VUID-03328; with API 1.2 the AS extensions no longer depend on
// the KHR extension, and vkGetBufferDeviceAddress accepts the EXT feature,
// VUID-vkGetBufferDeviceAddress-bufferDeviceAddress-03324). If the game does not
// enable the EXT extension, the core 1.2 bufferDeviceAddress feature is used
// instead and our allocations carry VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT.
//
// Per frame (geocapture.cpp captures the G-buffer pass into a device-local XFB
// buffer, every draw at a CPU-known offset, so the soup is contiguous and its
// size is known while recording):
//   1. compute: clip-space gl_Position -> camera-relative world, in place, by
//      solving the x, y, w rows of the camera UBO's ViewProj (_m2); vertices
//      whose z does not match (the sky dome writes z = 0) or that are not
//      finite (the game parks culled vertices at NaN) collapse their triangle
//      to a point (zero area: never hit, and still "active", so refits stay valid);
//   2. one BLAS over the soup: full build, or a refit when the draw sequence is
//      identical to the last build (rt_refit, rt_rebuild_frames);
//   3. a one-instance TLAS (identity transform: the space is already camera-relative).
// The core traces against the TLAS in the tiled lighting (rt_shadow.hlsl).
#include "host.h"
#include "hit_material.h"
#include "omm_backend.h"
#include "hair_groom.h"
#include "rt_pipeline.h"
#include "omm_submit.h"
#include "../common/omm_scene_capability.h"
#include "omm_content.h"
#include "omm_sampler.h"
#include "omm_bake.h"
#include "../common/cascade_casters.h"
#include "../common/water_build_camera.h"
#include "../common/hair_cost_epoch.h"
#include "../common/scene_handout_layout.h"
#include "../common/scene_handout.h"

#include <intrin.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>

namespace bg3 {

namespace {

std::atomic<bool> g_rt{false};
DeviceData* g_dd = nullptr;
bool g_bda_core = false;          // core bufferDeviceAddress feature (allocation flag needed)
PFN_vkGetBufferDeviceAddress p_bda = nullptr;
VkDeviceSize g_scratch_align = 256;
float g_ts_period = 1.0f;

// Storage for the modified device create info.
std::vector<const char*> g_ext_names;
VkPhysicalDeviceAccelerationStructureFeaturesKHR g_f_as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
VkPhysicalDeviceRayQueryFeaturesKHR g_f_rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
VkPhysicalDeviceBufferDeviceAddressFeatures g_f_bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
VkPhysicalDeviceBufferDeviceAddressFeaturesEXT g_f_bda_ext{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT};
// SHaRC radiance cache (core/sharc.cpp): 64-bit integers and buffer atomics, float16 and 16-bit storage.
VkPhysicalDeviceShaderAtomicInt64Features g_f_i64{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES};
VkPhysicalDeviceShaderFloat16Int8Features g_f_f16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
VkPhysicalDevice16BitStorageFeatures g_f_s16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
VkPhysicalDeviceFeatures g_f_core{};
uint32_t g_sharc_bits = 0;   // Bg3DeviceInfo::features of the create info last built
// Fields we set to VK_TRUE inside the game's own feature structs, to undo before a retry.
std::vector<std::pair<VkBool32*, VkBool32>> g_feature_edits;
// rt_scene_handout_layout (restart, docs/SCENE_HANDOUT_DESIGN.md 3.2): the descriptor-indexing UAB features for set 1.
VkPhysicalDeviceDescriptorIndexingFeatures g_f_di{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
bool g_handout_request = true;    // false after a device creation with them failed (retried without)
bool g_handout_features = false;  // the create info last built asks for them
bool g_handout_on = false;        // the device was created with them
uint64_t g_handout_downgrades = 0;  // stamp traversal addresses set back to the reference (rt_omm_scene_reference)

struct Buf {
  VkBuffer b = VK_NULL_HANDLE;
  VkDeviceMemory m = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  VkDeviceAddress addr = 0;
};

std::mutex g_mu;   // everything below
Buf g_blas_buf, g_tlas_buf, g_scratch, g_inst;
VkAccelerationStructureKHR g_blas = VK_NULL_HANDLE, g_tlas = VK_NULL_HANDLE;
VkDeviceAddress g_blas_addr = 0;
uint32_t g_blas_capacity = 0;     // primitives the BLAS storage was sized for
uint32_t g_alpha_capacity = 0;    // ... in the alpha-tested geometry
uint32_t g_blas_geoms = 0;
uint32_t g_built_aprims = 0;
VkBuildAccelerationStructureFlagsKHR g_blas_flags = 0;
bool g_blas_valid = false;        // built at least once with the current storage
uint32_t g_built_prims = 0;
uint64_t g_built_sig = 0;
uint32_t g_since_build = 0;
uint64_t g_last_frame = UINT64_MAX;   // frame the scene was last recorded in
std::atomic<uint64_t> g_tlas_frame{0};
bool g_logged_first = false;
bool g_failed = false;

// XFB -> world conversion (shaders/rt/xfb_to_world.hlsl), push descriptors.
VkDescriptorSetLayout g_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_pl = VK_NULL_HANDLE;
VkPipeline g_pipe = VK_NULL_HANDLE;
uint64_t g_pipe_gen = 0;
// rt_cascade_casters: the same layout, xfb_to_world_ortho.hlsl (built on present, when the key is on).
VkPipeline g_ortho_pipe = VK_NULL_HANDLE;
uint64_t g_ortho_gen = 0;
// Read without g_mu by the cascade's recording thread (a build may hold g_mu for milliseconds);
// replaced only on present, after a shader edit (as g_pipe for the split's conversion).
std::atomic<VkPipeline> g_ortho_live{VK_NULL_HANDLE};
std::atomic<VkPipelineLayout> g_pl_live{VK_NULL_HANDLE};
// The camera position (UBO byte 208) of the last builds, by frame: the cascade soup of frame F
// is camera-relative to F's camera (rt_build_camera).
struct CamHist { uint64_t frame = 0; float pos[3] = {}; };
CamHist g_cam_hist[4];

// The split's TLAS (bvhsplit.cpp), when the last build was a split one.
VkAccelerationStructureKHR g_split_tlas = VK_NULL_HANDLE;
bool g_split_valid = false;

// Scene snapshots (host.h kSceneSlots). A slot holds one build: its TLAS (the
// split's own per slot; the legacy one is shared), the bvhsplit ring region of
// its instances and InstInfo, and its alpha set and record region (same index).
// The RT passes get a slot's TLAS and set together (rt_scene_set), so a TLAS is
// never traced with another build's InstInfo, records, soup or pool bindings.
// A slot is overwritten only once the GPU finished every frame it was handed
// out in: `last_use` is the latest frame_index() of its build or a hand-out;
// work recorded then is submitted in that frame or the next, which the present
// fence of last_use + 2 covers (life_gpu_done). The current slot is never
// reused, and a slot is not handed out once stale (built + 1 < now, as
// core/gi.cpp rt_tlas), so a retired slot cannot come back into use.
struct SceneSlot {
  uint64_t material_record = 0; // includes pre-conversion copies even if AS build later fails
  bool used = false;
  uint64_t built = 0, last_use = 0;
  VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
  VkAccelerationStructureKHR omm_reference = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
  bool split = false;
  uint64_t rec = 0;   // build record of the command buffer it was recorded into (bvhsplit.cpp)
  VkBuffer pose = VK_NULL_HANDLE;
  VkDeviceMemory pose_memory = VK_NULL_HANDLE;
  bool pose_valid = false;
  VkBuffer camera = VK_NULL_HANDLE;
  VkDeviceSize camera_offset = 0;
  float camera_pose[56] = {};
  bool camera_pose_valid = false;
};
uint64_t g_legacy_rec = 0;   // the legacy BLAS's last build: refit only once it was submitted
SceneSlot g_scene[kSceneSlots];
int g_scene_cur = -1;
// rt_scene_wait=1 (P1, docs/TRACK_D_RT.md): BG3 records the lighting command buffer, where the
// RT passes go, on a worker thread 1-5 ms before another thread ends the G-buffer pass of the
// same frame (where the scene is captured and built). Both go down in one vkQueueSubmit with the
// G-buffer first, so the RT passes can trace this frame's TLAS if their recording waits for it.
// Without the wait they trace the previous frame's scene: a camera-relative scene one frame
// old, which detaches shadows while the camera moves.
std::condition_variable g_scene_cv;
// cpu_timing=1: CPU split of the scene build recording (P3 base frame rate).
// lock: waiting for g_mu at the capture end; prepare: native_hit::scene_prepare; commit: capture end
// (before the lock) to the scene slot commit, the part of the G-buffer thread the RT passes wait for.
struct BuildCpu { double total = 0, bvh = 0, alpha = 0, lock = 0, prepare = 0, commit = 0; uint64_t n = 0; };
BuildCpu g_bcpu;
double qpc_ms() { LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f); return double(t.QuadPart) * 1000.0 / double(f.QuadPart); }
// Where the scene thread was when a wait timed out (g_scene_phase): 0 no capture begun this frame,
// 1 recording the prepass (its draws and their hooks), 2 waiting for g_mu at the capture end,
// 3 hit-material scene_prepare, 4 bvh_build, 5 alpha_commit and the commit.
constexpr int kScenePhases = 6;
const char* const kScenePhaseName[kScenePhases] = {"no capture yet", "prepass recording", "scene lock",
                                                   "hit material prepare", "BVH build", "alpha commit"};
std::atomic<int> g_scene_phase{0};
std::atomic<uint64_t> g_scene_phase_frame{0};
struct WaitStats { uint64_t calls = 0, waited = 0, got = 0, timeouts = 0, skipped = 0; double ms = 0, ms_max = 0; uint64_t at[kScenePhases] = {}, traced = 0; };
void scene_phase(int p) { g_scene_phase.store(p, std::memory_order_relaxed); g_scene_phase_frame.store(frame_index(), std::memory_order_relaxed); }
WaitStats g_wait;
// g_wait and its log line: own lock, so the handout path (lighting thread, never g_mu) can count its frames.
// Lock order: g_mu -> g_wait_mu -> g_handout_mu.
std::mutex g_wait_mu;
uint64_t g_wait_handed = 0;   // frames whose first RT query took a handout (counted as waited 0 ms, got)
// rt_scene_handout=2 (increment 3, docs/SCENE_HANDOUT_DESIGN.md 3.1, common/scene_handout.h): the reservation state
// machine. g_handout_mu is a leaf for the lighting thread: it never takes g_mu, so a scene build in progress (which
// holds g_mu) never blocks the lighting's first RT query.
std::mutex g_handout_mu;
scene_handout::Machine g_ho;
std::atomic<bool> g_ho_open{false};
// Increment 4: the OMM tickets of the reserved slot's accelerated TLAS (set at its commit, under g_handout_mu), and the
// window's counts: commits that kept byte 24 on the accelerated TLAS, and downgrades (a user could not hold the tickets).
std::vector<uint64_t> g_ho_tickets;
// Increment 4b: the camera bindings the handout's hit-material users bound (checked against the query's at the submit),
// and the window's commit-time queries built / failed.
std::vector<std::pair<VkBuffer, VkDeviceSize>> g_ho_cameras;
uint64_t g_ho_query_built = 0, g_ho_query_failed = 0;
uint64_t g_ho_omm_accelerated = 0, g_ho_omm_downgraded = 0;
// Registers `tickets` on `cmd` while it still records `user` (omm_submit.cpp: the submit of that recording then keeps
// the micro-map data alive until its GPU work is done). false: it ended or was reset, or a ticket was refused.
bool handout_tickets(VkCommandBuffer cmd, uint64_t user, const std::vector<uint64_t>& tickets) {
  if (!cmd || cmd_recording(cmd) != user) return false;
  for (const uint64_t t : tickets) if (!omm::register_ticket_use(cmd, t)) return false;
  return true;
}   // a handout waits for its resolution at a submit (fast path for every submit)
bool slot_pinned(uint32_t i) { std::lock_guard<std::mutex> l(g_handout_mu); return g_ho.pinned(int(i)); }
uint32_t g_wait_misses = 0;   // consecutive timeouts: stop waiting after a few (no capture this frame)
uint32_t g_scene_next = 0;
uint64_t g_scene_skipped = 0, g_scene_skip_logged = 0;
std::atomic<uint64_t> g_oldest_live{UINT64_MAX};

bool slot_retired(const SceneSlot& s, uint64_t gpu_done, uint64_t now) {
  if (g_ho_open.load(std::memory_order_acquire) && slot_pinned(uint32_t(&s - g_scene))) return false;   // handed out or its P
  if(!native_hit::slot_reusable(uint32_t(&s-g_scene),gpu_done))return false;
  if(native_hit::enabled() && s.material_record){
    const int state=bvh_rec_state(s.material_record);
    if(state==0 || state<0)return false;
    if(state==2)return true; // one-time recording dropped before execution
  }
  if (!s.used) return true;
  if (gpu_done >= s.last_use + 2) return true;
  return false;   // Frame age never proves GPU completion.
}

// Caller holds g_mu.
void update_oldest_live() {
  const uint64_t gpu_done = life_gpu_done(), now = frame_index();
  uint64_t oldest = UINT64_MAX;
  for (uint32_t i = 0; i < kSceneSlots; ++i)
    if (int(i) == g_scene_cur || !slot_retired(g_scene[i], gpu_done, now))
      if (g_scene[i].used) oldest = (std::min)(oldest, g_scene[i].built);
  g_oldest_live = oldest;
}

// A retired slot for this frame's build, or -1 (all in flight: the build is skipped).
int pick_scene_slot() {
  const uint64_t gpu_done = life_gpu_done(), now = frame_index();
  for (uint32_t k = 0; k < kSceneSlots; ++k) {
    const uint32_t i = (g_scene_next + k) % kSceneSlots;
    if (int(i) == g_scene_cur || !slot_retired(g_scene[i], gpu_done, now)) continue;
    g_scene_next = i + 1;
    return int(i);
  }
  ++g_scene_skipped;
  if (g_scene_skip_logged < 5 || (g_scene_skipped & 1023) == 0) {
    ++g_scene_skip_logged;
    log("[rt] frame %llu: every scene slot is still in flight on the GPU (presents done %llu): build skipped (%llu so far)",
        (unsigned long long)now, (unsigned long long)gpu_done, (unsigned long long)g_scene_skipped);
  }
  return -1;
}

// rt_scene_handout: a TLAS's device address for the scene stamp (0 for none).
uint64_t tlas_address(VkAccelerationStructureKHR as) {
  if (!as || !g_dd->vk.GetAccelerationStructureDeviceAddressKHR) return 0;
  VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
  ai.accelerationStructure = as;
  return g_dd->vk.GetAccelerationStructureDeviceAddressKHR(g_dd->device, &ai);
}

void commit_scene_slot(int slot, VkAccelerationStructureKHR tlas, VkDescriptorSet set, bool split, uint64_t rec) {
  SceneSlot& s = g_scene[slot];
  s.used = true;
  s.rec = rec;
  s.built = s.last_use = frame_index();
  s.tlas = tlas;
  s.omm_reference=VK_NULL_HANDLE;
  s.set = set;
  s.split = split;
  g_scene_cur = slot;
  update_oldest_live();
  g_scene_cv.notify_all();
}

// GPU timing: 4 timestamps per build (start, converted, BLAS, TLAS) in a ring.
constexpr uint32_t kRing = 16;
VkQueryPool g_qp = VK_NULL_HANDLE;
struct Slot { uint64_t frame = 0; int mode = 0; bool moving = false; uint32_t prims = 0; uint64_t hair_epoch = 0; };   // mode: 0 rebuild, 1 refit, 2 split
Slot g_slots[kRing];
uint32_t g_next_slot = 0;
struct Acc { std::vector<float> conv, blas, tlas, total; };
Acc g_acc[3][2];                   // [rebuild, refit, split][static, camera moving]
uint32_t g_n_rebuild = 0, g_n_refit = 0, g_n_split = 0, g_n_frames = 0;
uint64_t g_prims_sum = 0, g_aprims_sum = 0, g_alpha_draws_sum = 0, g_static_tris_sum = 0, g_instances_sum = 0;
HairCostEpoch g_hair_timing_epoch;
bool g_hair_timing_reset = false;
void hair_timing_observe() {
  if (g_hair_timing_epoch.update(hair_cost_mask([](const char* key,int fallback) { return cfg_int(key,fallback); })))
    g_hair_timing_reset = true;
}

const VkBaseInStructure* find_in(const void* chain, VkStructureType t) {
  for (auto p = static_cast<const VkBaseInStructure*>(chain); p; p = p->pNext)
    if (p->sType == t) return p;
  return nullptr;
}

bool make_buf(Buf& out, VkDeviceSize size, VkBufferUsageFlags usage, const char* what) {
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
  if (g_bda_core) mai.pNext = &fi;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.m) != VK_SUCCESS) {
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
  VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
  ai.buffer = out.b;
  out.addr = p_bda(dd->device, &ai);
  fault_track(what, out.addr, size);
  return true;
}

Buf g_water_build_cameras;

void free_buf(Buf& b) {
  fault_untrack(b.addr);
  defer_destroy(BG3_DEFER_BUFFER, uint64_t(b.b));
  defer_destroy(BG3_DEFER_MEMORY, uint64_t(b.m));
  b = Buf{};
}

// Record the exact source camera only after a successful TLAS build, in the build command buffer.
void publish_water_build_camera(VkCommandBuffer cmd, const SceneSlot& scene, const float* camera, bool valid) {
  using namespace water;
  if (!cfg_int("rt_water_reflections", 0)) return;
  auto* ring = static_cast<BuildCameraRing*>(persist(kBuildCameraKey, sizeof(BuildCameraRing)));
  if (!ring) return;
  BuildCameraLock lock(*ring);
  if (!g_water_build_cameras.b && !make_buf(g_water_build_cameras, kBuildCameraCount * kBuildCameraBytes,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "water build cameras")) return;
  const uint64_t frame = scene.built;
  const uint32_t index = build_camera_index(frame);
  float value[4]{};
  if (valid && std::isfinite(camera[52]) && std::isfinite(camera[53]) && std::isfinite(camera[54])) {
    std::memcpy(value, camera + 52, 12); value[3] = 1;
  }
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  g_dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 1, &barrier, 0, nullptr, 0, nullptr);
  g_dd->vk.CmdUpdateBuffer(cmd, g_water_build_cameras.b, index * kBuildCameraBytes, sizeof value, value);
  ring->buffer = uint64_t(g_water_build_cameras.b);
  // Unreadable/non-finite cameras cannot certify a translation (or F-1 hit shading).
  ring->entries[index] = {frame, value[3] != 0 ? uint64_t(scene.tlas) : 0,
                         value[3] != 0 ? uint64_t(scene.omm_reference) : 0};
}

VkDeviceAddress buffer_address(VkBuffer b) {
  VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
  ai.buffer = b;
  return p_bda(g_dd->device, &ai);
}

void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  g_dd->vk.CmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

constexpr VkPipelineStageFlags kAS = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
constexpr VkPipelineStageFlags kCS = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags kXFB = VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;
constexpr VkPipelineStageFlags kXfer = VK_PIPELINE_STAGE_TRANSFER_BIT;

bool ensure_pipeline_file(const char* file, VkPipeline& pipe, uint64_t& gen);
bool ensure_pipeline() { return ensure_pipeline_file("rt/xfb_to_world.hlsl", g_pipe, g_pipe_gen); }
bool ensure_pipeline_file(const char* file, VkPipeline& pipe, uint64_t& gen) {
  Bg3ShaderBlob blob{};
  if (!shader_get(file, "main", "cs_6_6", &blob)) return pipe != VK_NULL_HANDLE;
  if (pipe && blob.generation == gen) return true;
  DeviceData* dd = g_dd;
  if (!g_dsl) {
    VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                         {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dci.bindingCount = 2;
    dci.pBindings = b;
    if (dd->vk.CreateDescriptorSetLayout(dd->device, &dci, nullptr, &g_dsl) != VK_SUCCESS) return false;
    VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
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
  if (dd->vk.CreateShaderModule(dd->device, &mci, nullptr, &mod) != VK_SUCCESS) return pipe != VK_NULL_HANDLE;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main"};
  ci.layout = g_pl;
  VkPipeline p = VK_NULL_HANDLE;
  VkResult r = dd->vk.CreateComputePipelines(dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &p);
  dd->vk.DestroyShaderModule(dd->device, mod, nullptr);
  if (r != VK_SUCCESS) {
    log("[rt] %s pipeline creation failed (%d)", file, r);
    return pipe != VK_NULL_HANDLE;
  }
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(pipe));
  pipe = p;
  gen = blob.generation;
  const std::string name = std::string(file).substr(3, std::strlen(file) - 8);   // "rt/<name>.hlsl"
  log("[rt] %s pipeline generation %llu", name.c_str(), (unsigned long long)blob.generation);
  return true;
}

// Geometry 0: the opaque soup. Geometry 1 (rt_alpha): the alpha-tested draws,
// not opaque, so ray queries see them as candidates and test their alpha.
VkAccelerationStructureGeometryKHR blas_geometry(VkDeviceAddress verts, uint32_t prims, uint32_t stride, bool opaque) {
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

VkAccelerationStructureGeometryKHR tlas_geometry() {
  VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  g.geometry.instances.data.deviceAddress = g_inst.addr;
  return g;
}

bool grow_scratch(VkDeviceSize need) {
  need += g_scratch_align;
  if (g_scratch.b && g_scratch.size >= need) return true;
  if (g_scratch.b) free_buf(g_scratch);
  return make_buf(g_scratch, need + need / 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "legacy AS scratch");
}

VkDeviceAddress scratch_addr() { return (g_scratch.addr + g_scratch_align - 1) & ~(g_scratch_align - 1); }

// BLAS storage for at least `prims` opaque and `aprims` alpha-tested primitives
// with `flags` (`geoms` = 2 with the alpha geometry).
bool ensure_blas(uint32_t prims, uint32_t aprims, uint32_t geoms, uint32_t stride, VkBuildAccelerationStructureFlagsKHR flags,
                 VkDeviceAddress verts, VkDeviceAddress averts) {
  if (g_blas && prims <= g_blas_capacity && aprims <= g_alpha_capacity && geoms == g_blas_geoms && flags == g_blas_flags)
    return true;
  DeviceData* dd = g_dd;
  uint32_t cap = (std::max)(prims + prims / 4, 65536u);
  uint32_t acap = geoms > 1 ? (std::max)(aprims + aprims / 4, 16384u) : 0;
  VkAccelerationStructureGeometryKHR g[2] = {blas_geometry(verts, cap, stride, true),
                                             blas_geometry(averts, acap, stride, false)};
  uint32_t caps[2] = {cap, acap};
  VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  bi.flags = flags;
  bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  bi.geometryCount = geoms;
  bi.pGeometries = g;
  VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  dd->vk.GetAccelerationStructureBuildSizesKHR(dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, caps, &sz);
  if (g_blas) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(g_blas));
  g_blas = VK_NULL_HANDLE;
  g_blas_valid = false;
  if (g_blas_buf.b) free_buf(g_blas_buf);
  if (!make_buf(g_blas_buf, sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, "legacy soup BLAS") ||
      !grow_scratch((std::max)(sz.buildScratchSize, sz.updateScratchSize))) {
    log("[rt] could not allocate the BLAS (%u primitives, %llu MB)", cap,
        (unsigned long long)(sz.accelerationStructureSize >> 20));
    return false;
  }
  VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  ci.buffer = g_blas_buf.b;
  ci.size = sz.accelerationStructureSize;
  ci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  if (dd->vk.CreateAccelerationStructureKHR(dd->device, &ci, nullptr, &g_blas) != VK_SUCCESS) {
    g_blas = VK_NULL_HANDLE;
    return false;
  }
  VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
  ai.accelerationStructure = g_blas;
  g_blas_addr = dd->vk.GetAccelerationStructureDeviceAddressKHR(dd->device, &ai);
  g_blas_capacity = cap;
  g_alpha_capacity = acap;
  g_blas_geoms = geoms;
  g_blas_flags = flags;
  log("[rt] BLAS storage for %u + %u alpha-tested primitives: %llu MB, scratch build %llu MB / update %llu MB (flags 0x%x)",
      cap, acap,
      (unsigned long long)(sz.accelerationStructureSize >> 20), (unsigned long long)(sz.buildScratchSize >> 20),
      (unsigned long long)(sz.updateScratchSize >> 20), flags);
  return true;
}

bool ensure_tlas() {
  if (g_tlas) return true;
  DeviceData* dd = g_dd;
  if (!make_buf(g_inst, sizeof(VkAccelerationStructureInstanceKHR),
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                "legacy TLAS instance"))
    return false;
  VkAccelerationStructureGeometryKHR g = tlas_geometry();
  VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  bi.geometryCount = 1;
  bi.pGeometries = &g;
  uint32_t one = 1;
  VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  dd->vk.GetAccelerationStructureBuildSizesKHR(dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &one, &sz);
  if (!make_buf(g_tlas_buf, sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, "legacy TLAS") ||
      !grow_scratch(sz.buildScratchSize))
    return false;
  VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  ci.buffer = g_tlas_buf.b;
  ci.size = sz.accelerationStructureSize;
  ci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  if (dd->vk.CreateAccelerationStructureKHR(dd->device, &ci, nullptr, &g_tlas) != VK_SUCCESS) {
    g_tlas = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

// rt_scene_handout_layout: the empty dummy TLAS (docs/SCENE_HANDOUT_DESIGN.md 3; common/scene_handout_layout.h).
// Its own instance input and scratch, so its build never shares the scene build's scratch in the same command buffer.
VkAccelerationStructureKHR g_dummy_tlas = VK_NULL_HANDLE;
Buf g_dummy_tlas_buf, g_dummy_inst, g_dummy_scratch;
uint64_t g_dummy_rec = 0;   // build record of the command buffer it was recorded into (pinned until submitted)
bool g_dummy_ready = false;

// Caller holds g_mu. Creates the dummy once and records its (zero-instance) build into `cmd`, the scene build's
// command buffer, outside any render pass; again only if that recording was dropped without a submit.
void ensure_dummy_tlas(VkCommandBuffer cmd) {
  if (!g_handout_on || g_dummy_ready) return;
  if (g_dummy_rec) {
    const int state = bvh_rec_state(g_dummy_rec);
    if (state == 1) { g_dummy_ready = true; bvh_rec_pin(g_dummy_rec, false); return; }
    if (state != 2) return;   // recorded, not submitted yet
    bvh_rec_pin(g_dummy_rec, false);
    g_dummy_rec = 0;          // dropped: record the build again
  }
  DeviceData* dd = g_dd;
  if (!g_dummy_tlas) {
    scene_handout::EmptyTlas probe;
    uint32_t one = scene_handout::EmptyTlas::kMaxInstances;
    VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    dd->vk.GetAccelerationStructureBuildSizesKHR(dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &probe.build, &one, &sz);
    if (!make_buf(g_dummy_inst, scene_handout::EmptyTlas::kInstanceBytes,
                  VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, "scene handout dummy TLAS instance") ||
        !make_buf(g_dummy_tlas_buf, sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                  "scene handout dummy TLAS") ||
        !make_buf(g_dummy_scratch, sz.buildScratchSize + g_scratch_align, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                  "scene handout dummy TLAS scratch")) {
      for (Buf* b : {&g_dummy_inst, &g_dummy_tlas_buf, &g_dummy_scratch}) if (b->b) free_buf(*b);
      log("[rt] scene handout: dummy TLAS allocation failed");
      g_handout_on = false;
      return;
    }
    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ci.buffer = g_dummy_tlas_buf.b;
    ci.size = sz.accelerationStructureSize;
    ci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (dd->vk.CreateAccelerationStructureKHR(dd->device, &ci, nullptr, &g_dummy_tlas) != VK_SUCCESS) {
      g_dummy_tlas = VK_NULL_HANDLE;
      for (Buf* b : {&g_dummy_inst, &g_dummy_tlas_buf, &g_dummy_scratch}) free_buf(*b);
      log("[rt] scene handout: dummy TLAS creation failed");
      g_handout_on = false;
      return;
    }
  }
  scene_handout::EmptyTlas e(g_dummy_inst.addr);
  e.build.dstAccelerationStructure = g_dummy_tlas;
  e.build.scratchData.deviceAddress = (g_dummy_scratch.addr + g_scratch_align - 1) & ~(g_scratch_align - 1);
  const VkAccelerationStructureBuildRangeInfoKHR* ranges = &e.range;
  dd->vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &e.build, &ranges);
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  dd->vk.CmdPipelineBarrier(cmd, kAS, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  g_dummy_rec = bvh_rec_register(cmd);
  bvh_rec_pin(g_dummy_rec, true);
  log("[rt] scene handout: dummy TLAS (0 instances, %llu bytes) recorded", (unsigned long long)g_dummy_tlas_buf.size);
}

void release_dummy_tlas() {
  if (g_dummy_tlas) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(g_dummy_tlas));
  g_dummy_tlas = VK_NULL_HANDLE;
  for (Buf* b : {&g_dummy_inst, &g_dummy_tlas_buf, &g_dummy_scratch}) if (b->b) free_buf(*b);
  if (g_dummy_rec) bvh_rec_pin(g_dummy_rec, false);
  g_dummy_rec = 0;
  g_dummy_ready = false;
}

void release_all() {
  if (g_blas) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(g_blas));
  if (g_tlas) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(g_tlas));
  g_blas = g_tlas = VK_NULL_HANDLE;
  for (Buf* b : {&g_blas_buf, &g_tlas_buf, &g_scratch, &g_inst})
    if (b->b) free_buf(*b);
  g_blas_capacity = g_alpha_capacity = g_blas_geoms = 0;
  g_blas_valid = false;
}

void timestamp(VkCommandBuffer cmd, int slot, uint32_t i) {
  if (!logging::stats_enabled(cfg_int) || slot < 0) return;
  if (i == 0) {
    hair_timing_observe();
    g_slots[slot].hair_epoch = g_hair_timing_epoch.generation;
  }
  g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_qp, uint32_t(slot) * 4 + i);
}

}  // namespace

// ================= host-facing entry points =================

uint32_t rt_instance_api(uint32_t requested) {
  if (!cfg_int("rt", 1)) return requested;
  // 1.2 is what the AS/ray query dependencies need; 1.3 also makes our own
  // shaders (DXC -fspv-target-env=vulkan1.3, SPIR-V 1.6) valid for the device,
  // and is what Streamline asks for anyway (fg>=2).
  return (std::max)(requested, uint32_t(VK_API_VERSION_1_3));
}

// The SHaRC radiance cache's device features (core/sharc.cpp, docs/DESIGN_SOTA_NEXT.md 2.3):
// shaderInt64 + shaderBufferInt64Atomics for its 64-bit hash keys, shaderFloat16 +
// storageBuffer16BitAccess for its packed radiance (DXC -enable-16bit-types). Each one only
// if the physical device supports it; set in the game's own struct when it chains one (the
// game chains VkPhysicalDeviceFeatures2, ...Float16Int8 and ...16BitStorage), otherwise in a
// struct of ours in front. Returns the new chain head; *core_features is set when the core
// 1.0 features must come from g_f_core (pEnabledFeatures). Bits: Bg3DeviceInfo::features.
static const void* sharc_features(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                  const void* next, bool* core_features) {
  *core_features = false;
  g_sharc_bits = 0;
  if (!id->GetPhysicalDeviceFeatures2) {
    log("[rt] SHaRC device features: vkGetPhysicalDeviceFeatures2 missing; not enabled");
    return next;
  }
  VkPhysicalDeviceShaderAtomicInt64Features si64{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES};
  VkPhysicalDeviceShaderFloat16Int8Features sf16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES, &si64};
  VkPhysicalDevice16BitStorageFeatures ss16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES, &sf16};
  VkPhysicalDeviceFeatures2 sup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &ss16};
  id->GetPhysicalDeviceFeatures2(phys, &sup);
  // The game's VkPhysicalDeviceVulkan11/12Features would forbid the individual structs (VUID 02829/02830).
  auto* v11 = reinterpret_cast<const VkPhysicalDeviceVulkan11Features*>(find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES));
  auto* v12 = reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES));
  auto set = [](const VkBool32* field) {
    auto* f = const_cast<VkBool32*>(field);
    if (*f) return;
    g_feature_edits.emplace_back(f, *f);
    *f = VK_TRUE;
  };
  // shaderInt64 (core 1.0): the game's VkPhysicalDeviceFeatures2, else pEnabledFeatures (copied).
  if (sup.features.shaderInt64) {
    if (auto* f2 = reinterpret_cast<const VkPhysicalDeviceFeatures2*>(find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2))) {
      set(&f2->features.shaderInt64);
    } else {
      g_f_core = ci->pEnabledFeatures ? *ci->pEnabledFeatures : VkPhysicalDeviceFeatures{};
      g_f_core.shaderInt64 = VK_TRUE;
      *core_features = true;
    }
    g_sharc_bits |= BG3_DEV_INT64;
  }
  if (si64.shaderBufferInt64Atomics) {
    if (v12) set(&v12->shaderBufferInt64Atomics);
    else if (auto* e = reinterpret_cast<const VkPhysicalDeviceShaderAtomicInt64Features*>(
                 find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES)))
      set(&e->shaderBufferInt64Atomics);
    else {
      g_f_i64 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES};
      g_f_i64.shaderBufferInt64Atomics = VK_TRUE;
      g_f_i64.pNext = const_cast<void*>(next);
      next = &g_f_i64;
    }
    g_sharc_bits |= BG3_DEV_INT64_ATOMICS;
  }
  if (sf16.shaderFloat16) {
    if (v12) set(&v12->shaderFloat16);
    else if (auto* e = reinterpret_cast<const VkPhysicalDeviceShaderFloat16Int8Features*>(
                 find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES)))
      set(&e->shaderFloat16);
    else {
      g_f_f16 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
      g_f_f16.shaderFloat16 = VK_TRUE;
      g_f_f16.pNext = const_cast<void*>(next);
      next = &g_f_f16;
    }
    g_sharc_bits |= BG3_DEV_FLOAT16;
  }
  if (ss16.storageBuffer16BitAccess) {
    if (v11) set(&v11->storageBuffer16BitAccess);
    else if (auto* e = reinterpret_cast<const VkPhysicalDevice16BitStorageFeatures*>(
                 find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES)))
      set(&e->storageBuffer16BitAccess);
    else {
      g_f_s16 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
      g_f_s16.storageBuffer16BitAccess = VK_TRUE;
      g_f_s16.pNext = const_cast<void*>(next);
      next = &g_f_s16;
    }
    g_sharc_bits |= BG3_DEV_STORAGE16;
  }
  log("[rt] SHaRC device features (supported -> enabled): shaderInt64 %d, shaderBufferInt64Atomics %d, shaderFloat16 %d, "
      "storageBuffer16BitAccess %d; %zu set in the game's own structs%s%s",
      sup.features.shaderInt64, si64.shaderBufferInt64Atomics, sf16.shaderFloat16, ss16.storageBuffer16BitAccess,
      g_feature_edits.size(), v11 || v12 ? " (through its VkPhysicalDeviceVulkan11/12Features)" : "",
      *core_features ? "; shaderInt64 through pEnabledFeatures" : "");
  return next;
}

// rt_scene_handout_layout: storage-buffer and sampled-image update-after-bind in the game's own struct when it chains
// one (it chains VkPhysicalDeviceDescriptorIndexingFeatures; ...Vulkan12Features would forbid ours, VUID 04478),
// otherwise in ours in front; the acceleration-structure one goes into g_f_as (*as_uab). Only when all three are
// supported (scene_handout::uab_supported). Returns the new chain head.
static const void* handout_features(InstanceData* id, VkPhysicalDevice phys, const void* next, bool* as_uab) {
  *as_uab = false;
  VkPhysicalDeviceDescriptorIndexingFeatures di{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &di};
  VkPhysicalDeviceFeatures2 sup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &as};
  id->GetPhysicalDeviceFeatures2(phys, &sup);
  if (!scene_handout::uab_supported(di, as)) {
    log("[rt] scene handout layout: update-after-bind not supported (storage buffer %d, sampled image %d, "
        "acceleration structure %d); set 1 stays as it is", di.descriptorBindingStorageBufferUpdateAfterBind,
        di.descriptorBindingSampledImageUpdateAfterBind, as.descriptorBindingAccelerationStructureUpdateAfterBind);
    return next;
  }
  auto set = [](const VkBool32* field) {
    auto* f = const_cast<VkBool32*>(field);
    if (*f) return;
    g_feature_edits.emplace_back(f, *f);
    *f = VK_TRUE;
  };
  const char* where = "ours";
  if (auto* v12 = reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES))) {
    set(&v12->descriptorBindingStorageBufferUpdateAfterBind);
    set(&v12->descriptorBindingSampledImageUpdateAfterBind);
    where = "the game's VkPhysicalDeviceVulkan12Features";
  } else if (auto* e = reinterpret_cast<const VkPhysicalDeviceDescriptorIndexingFeatures*>(
                 find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES))) {
    set(&e->descriptorBindingStorageBufferUpdateAfterBind);
    set(&e->descriptorBindingSampledImageUpdateAfterBind);
    where = "the game's VkPhysicalDeviceDescriptorIndexingFeatures";
  } else {
    g_f_di = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    g_f_di.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    g_f_di.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    g_f_di.pNext = const_cast<void*>(next);
    next = &g_f_di;
  }
  *as_uab = true;
  g_handout_features = true;
  log("[rt] + scene handout layout: storage buffer, sampled image (in %s) and acceleration structure update-after-bind", where);
  return next;
}

bool rt_scene_handout_requested() { return g_handout_features; }
void rt_scene_handout_disable_request() { g_handout_request = false; }
bool rt_scene_handout_layout() { return g_handout_on; }

bool rt_device_features_added() { return g_sharc_bits != 0; }

void rt_device_features_undo() {
  for (auto& [field, old] : g_feature_edits) *field = old;
  if (!g_feature_edits.empty()) log("[rt] restored %zu feature fields of the game's create info", g_feature_edits.size());
  g_feature_edits.clear();
  g_sharc_bits = 0;
  g_handout_features = false;
}

const VkDeviceCreateInfo* rt_device_create_info(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                VkDeviceCreateInfo* scratch, bool sharc) {
  g_sharc_bits = 0;
  if (!cfg_int("rt", 1) || !id) return ci;
  VkPhysicalDeviceProperties props{};
  id->GetPhysicalDeviceProperties(phys, &props);
  if (id->api_version < VK_API_VERSION_1_2 || props.apiVersion < VK_API_VERSION_1_2) {
    log("[rt] needs Vulkan 1.2 (instance %u.%u, device %u.%u): ray tracing off", VK_API_VERSION_MAJOR(id->api_version),
        VK_API_VERSION_MINOR(id->api_version), VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion));
    return ci;
  }
  // Supported?
  uint32_t n = 0;
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> avail(n);
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, avail.data());
  auto available = [&](const char* e) {
    for (auto& a : avail)
      if (!strcmp(a.extensionName, e)) return true;
    return false;
  };
  VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq};
  VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES, &as};
  VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &bda};
  if (id->GetPhysicalDeviceFeatures2) id->GetPhysicalDeviceFeatures2(phys, &f2);
  const char* need[] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
  for (const char* e : need)
    if (!available(e)) {
      log("[rt] %s not available: ray tracing off", e);
      return ci;
    }
  if (!as.accelerationStructure || !rq.rayQuery || !bda.bufferDeviceAddress) {
    log("[rt] features missing (accelerationStructure %d, rayQuery %d, bufferDeviceAddress %d): ray tracing off",
        as.accelerationStructure, rq.rayQuery, bda.bufferDeviceAddress);
    return ci;
  }

  // What the game already asks for.
  std::string chain;
  for (auto p = static_cast<const VkBaseInStructure*>(ci->pNext); p; p = p->pNext) chain += " " + std::to_string(p->sType);
  log("[rt] game device create chain:%s", chain.c_str());
  // Track D phase 3 indexes sampled-image arrays non-uniformly (rt_common.hlsli).
  if (auto* di = reinterpret_cast<const VkPhysicalDeviceDescriptorIndexingFeatures*>(
          find_in(ci->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES)))
    log("[rt] game's descriptor indexing: sampledImageArrayNonUniform %d, runtimeArray %d, partiallyBound %d",
        di->shaderSampledImageArrayNonUniformIndexing, di->runtimeDescriptorArray, di->descriptorBindingPartiallyBound);
  g_ext_names.assign(ci->ppEnabledExtensionNames, ci->ppEnabledExtensionNames + ci->enabledExtensionCount);
  auto listed = [&](const char* e) {
    for (auto x : g_ext_names)
      if (!strcmp(x, e)) return true;
    return false;
  };
  if (find_in(ci->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR) ||
      find_in(ci->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR)) {
    log("[rt] the application already chains ray tracing features; leaving the device alone");
    return ci;
  }
  const void* next = ci->pNext;
  if (listed(VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)) {
    // Keep the game's EXT extension (KHR + EXT is VUID-03328). Its feature
    // struct must say bufferDeviceAddress = TRUE; add one if it has none.
    auto* e = reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeaturesEXT*>(
        find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT));
    if (e && !e->bufferDeviceAddress) {
      log("[rt] the game enables VK_EXT_buffer_device_address with bufferDeviceAddress off: ray tracing off");
      return ci;
    }
    if (!e) {
      g_f_bda_ext = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT};
      g_f_bda_ext.bufferDeviceAddress = VK_TRUE;
      g_f_bda_ext.pNext = const_cast<void*>(next);
      next = &g_f_bda_ext;
      log("[rt] + VkPhysicalDeviceBufferDeviceAddressFeaturesEXT");
    }
    // With API 1.2 the core feature can be enabled next to the EXT extension
    // (only VkPhysicalDeviceVulkan12Features::bufferDeviceAddress is excluded,
    // VUID-04748). Something in the process (NGX's own allocations) already
    // passes VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, which needs the core
    // feature (VUID-VkMemoryAllocateInfo-flags-03331) once the device is 1.2.
    g_bda_core = false;
    if (cfg_int("rt_bda_core", 1) && !find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) &&
        !find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES)) {
      g_f_bda = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
      g_f_bda.bufferDeviceAddress = VK_TRUE;
      g_f_bda.pNext = const_cast<void*>(next);
      next = &g_f_bda;
      g_bda_core = true;
    }
    log("[rt] buffer device addresses through the game's VK_EXT_buffer_device_address%s",
        g_bda_core ? " plus the core 1.2 bufferDeviceAddress feature" : "");
  } else {
    auto* v12 = reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(
        find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES));
    auto* core = reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(
        find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES));
    if ((v12 && !v12->bufferDeviceAddress) || (core && !core->bufferDeviceAddress)) {
      log("[rt] the game chains bufferDeviceAddress = FALSE: ray tracing off");
      return ci;
    }
    if (!v12 && !core) {
      g_f_bda = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
      g_f_bda.bufferDeviceAddress = VK_TRUE;
      g_f_bda.pNext = const_cast<void*>(next);
      next = &g_f_bda;
    }
    g_bda_core = true;
    log("[rt] buffer device addresses through the core 1.2 feature");
  }
  for (const char* e : need)
    if (!listed(e)) g_ext_names.push_back(e);
  bool core_features = false;
  if (sharc && cfg_int("sharc_device", 1)) next = sharc_features(id, phys, ci, next, &core_features);
  else log("[rt] SHaRC device features not requested (%s)", sharc ? "sharc_device=0" : "retry without them");
  // rt_hair_groom_primary's coverage gate (layer/hair_primary.cpp): precise occlusion queries count the card and strand
  // probe samples per draw; without the feature no draw is ever swapped. Core 1.0 feature, no cost when unused, so it
  // is requested whenever supported (5m: RTX Hair turns on live, docs/RTX_HAIR_PRIMARY.md).
  if (id->GetPhysicalDeviceFeatures2) {
    VkPhysicalDeviceFeatures2 sup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    id->GetPhysicalDeviceFeatures2(phys, &sup);
    if (sup.features.occlusionQueryPrecise) {
      if (auto* f2 = reinterpret_cast<const VkPhysicalDeviceFeatures2*>(find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2))) {
        auto* f = const_cast<VkBool32*>(&f2->features.occlusionQueryPrecise);
        if (!*f) { g_feature_edits.emplace_back(f, *f); *f = VK_TRUE; }
      } else {
        if (!core_features) g_f_core = ci->pEnabledFeatures ? *ci->pEnabledFeatures : VkPhysicalDeviceFeatures{};
        g_f_core.occlusionQueryPrecise = VK_TRUE;
        core_features = true;
      }
      log("[rt] + occlusionQueryPrecise (strand hair coverage gate)");
    }
  }
  next = omm::device_features(id, phys, next, g_ext_names);
  next = hair_groom::device_features(id, phys, next, g_ext_names);
  next = rt_pipeline::device_features(id, phys, next, g_ext_names);   // gi_rt_pipeline (layer/rt_pipeline.cpp)
  g_handout_features = false;
  bool handout_as = false;
  if (g_handout_request && cfg_int("rt_scene_handout_layout", 0) && id->GetPhysicalDeviceFeatures2)
    next = handout_features(id, phys, next, &handout_as);
  g_f_rq = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  g_f_rq.rayQuery = VK_TRUE;
  g_f_rq.pNext = const_cast<void*>(next);
  g_f_as = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  g_f_as.accelerationStructure = VK_TRUE;
  g_f_as.descriptorBindingAccelerationStructureUpdateAfterBind = handout_as ? VK_TRUE : VK_FALSE;
  g_f_as.pNext = &g_f_rq;
  *scratch = *ci;
  scratch->pNext = &g_f_as;
  if (core_features) scratch->pEnabledFeatures = &g_f_core;
  scratch->enabledExtensionCount = uint32_t(g_ext_names.size());
  scratch->ppEnabledExtensionNames = g_ext_names.data();
  log("[rt] + VK_KHR_acceleration_structure, VK_KHR_ray_query, VK_KHR_deferred_host_operations");
  return scratch;
}

void rt_device_created(DeviceData* dd, bool enabled) {
  if (g_dd) return;
  g_dd = dd;
  // For the core (no ABI change): what the device was created with, through persist().
  if (auto* info = static_cast<Bg3DeviceInfo*>(persist(BG3_DEVICE_INFO_KEY, sizeof(Bg3DeviceInfo)))) {
    *info = Bg3DeviceInfo{};
    info->version = 1;
    info->features = enabled ? g_sharc_bits : 0u;
    if (dd->inst && dd->inst->GetPhysicalDeviceMemoryProperties)
      dd->inst->GetPhysicalDeviceMemoryProperties(dd->physical, &info->memory);
    if (enabled && g_sharc_bits) log("[rt] SHaRC device features on the device: 0x%x", g_sharc_bits);
  }
  g_feature_edits.clear();   // the device exists: nothing to undo any more
  g_handout_on = enabled && g_handout_features;
  if (cfg_int("rt_scene_handout_layout", 0))
    log("[rt] scene handout layout %s", g_handout_on ? "on (set 1 update-after-bind)" : "off (not on the device)");
  if (!enabled) {
    if (cfg_int("rt", 1)) log("[rt] ray tracing is NOT enabled on the device");
    return;
  }
  p_bda = dd->vk.GetBufferDeviceAddress;
  if (!p_bda) p_bda = reinterpret_cast<PFN_vkGetBufferDeviceAddress>(dd->gdpa(dd->device, "vkGetBufferDeviceAddressEXT"));
  bool fns = p_bda && dd->vk.CreateAccelerationStructureKHR && dd->vk.DestroyAccelerationStructureKHR &&
             dd->vk.GetAccelerationStructureBuildSizesKHR && dd->vk.GetAccelerationStructureDeviceAddressKHR &&
             dd->vk.CmdBuildAccelerationStructuresKHR && dd->vk.CmdUpdateBuffer && dd->vk.CmdPushDescriptorSet;
  if (!fns) {
    log("[rt] ray tracing entry points missing; off");
    return;
  }
  if (dd->inst && dd->inst->GetPhysicalDeviceProperties) {
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &asp};
    auto props2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        dd->inst->gipa(dd->inst->instance, "vkGetPhysicalDeviceProperties2"));
    if (props2) {
      props2(dd->physical, &p2);
      g_scratch_align = (std::max)(VkDeviceSize(asp.minAccelerationStructureScratchOffsetAlignment), VkDeviceSize(16));
    }
    VkPhysicalDeviceProperties p;
    dd->inst->GetPhysicalDeviceProperties(dd->physical, &p);
    g_ts_period = p.limits.timestampPeriod;
  }
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qi.queryCount = kRing * 4;
  if (dd->vk.CreateQueryPool(dd->device, &qi, nullptr, &g_qp) != VK_SUCCESS) g_qp = VK_NULL_HANDLE;
  g_rt = true;
  omm::device_created(dd, true);
  if(omm::available() || cfg_int("rt_hit_material",0))omm::sampler_device_created(dd);
  native_hit::device_created(dd);
  log("[rt] acceleration structures + ray query enabled on the game's device (scratch alignment %llu)",
      (unsigned long long)g_scratch_align);
}

void rt_device_destroyed(DeviceData* dd) {
  if (dd != g_dd) return;
  native_hit::device_destroyed(dd);
  omm::device_destroyed(dd);
  omm::bake_device_destroyed(dd);
  omm::submit_device_destroyed(dd);
  omm::content_device_destroyed(dd);
  omm::sampler_device_destroyed(dd);
  {
    std::lock_guard<std::mutex> lock(g_mu);
    release_all();
    release_dummy_tlas();
    if (auto* ring = static_cast<water::BuildCameraRing*>(persist(water::kBuildCameraKey, sizeof(water::BuildCameraRing)))) {
      water::BuildCameraLock lock(*ring);
      ring->buffer = 0;
      for (auto& e : ring->entries) e = {};
      free_buf(g_water_build_cameras);
    }
    for (auto& s : g_scene) {
      defer_destroy(BG3_DEFER_BUFFER, uint64_t(s.pose));
      defer_destroy(BG3_DEFER_MEMORY, uint64_t(s.pose_memory));
      s.pose = VK_NULL_HANDLE; s.pose_memory = VK_NULL_HANDLE; s.pose_valid = false;
    }
    defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_pipe));
    defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_ortho_pipe));
    g_ortho_pipe = VK_NULL_HANDLE;
    g_ortho_live = VK_NULL_HANDLE;
    g_pl_live = VK_NULL_HANDLE;
    defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, uint64_t(g_pl));
    defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, uint64_t(g_dsl));
    defer_destroy(BG3_DEFER_QUERY_POOL, uint64_t(g_qp));
    g_pipe = VK_NULL_HANDLE;
    g_pl = VK_NULL_HANDLE;
    g_dsl = VK_NULL_HANDLE;
    g_qp = VK_NULL_HANDLE;
  }
  g_rt = false;
  g_dd = nullptr;
}

bool rt_enabled() { return g_rt.load(); }

bool rt_capture_wanted() {
  if (!g_rt || g_failed || !cfg_hot(CfgHot::rt_build)) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  return g_last_frame != frame_index();
}

VkBufferUsageFlags rt_xfb_usage() {
  return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
}

bool rt_alloc_device_address() { return g_bda_core; }

// rt_scene_handout=2 needs the update-after-bind set 1 and the dummy TLAS (rt_scene_handout_layout at startup),
// the BVH split. Native hit materials (increment 4b): their query is built at the commit (native_hit::commit_query).
bool handout_mode() {
  return cfg_int("rt_scene_handout", 0) >= 2 && g_handout_on && g_dummy_ready && cfg_int("rt_scene_wait", 1) != 0;
}

void rt_before_capture(VkCommandBuffer cmd) {
  if (!g_rt) return;
  scene_phase(1);
  if (handout_mode()) {   // reserve the slot this capture's build will write (common/scene_handout.h)
    std::lock_guard<std::mutex> lock(g_mu);
    bool open;
    { std::lock_guard<std::mutex> h(g_handout_mu); open = g_ho.open(); }
    if (!open) {
      const uint64_t now = frame_index();
      int current = g_scene_cur;
      if (current >= 0) {
        const SceneSlot& c = g_scene[current];
        if (!c.used || !c.tlas || !c.set || !c.split || c.built + 1 < now || bvh_rec_state(c.rec) == 2) current = -1;
      }
      const int pick = current >= 0 ? pick_scene_slot() : -1;
      std::lock_guard<std::mutex> h(g_handout_mu);
      g_ho.reserve(now, pick, current);
      g_ho_open.store(g_ho.open(), std::memory_order_release);
    }
    // The handout query set of the reserved slot exists before the lighting asks for it (increment 4b).
    int reserved;
    { std::lock_guard<std::mutex> h(g_handout_mu); reserved = g_ho.state == scene_handout::Machine::State::kReserved ? g_ho.slot : -1; }
    if (reserved >= 0 && native_hit::enabled()) native_hit::handout_set(uint32_t(reserved));
  }
  // Last frame's BLAS build read the XFB buffer this pass is about to overwrite.
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.dstAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
  g_dd->vk.CmdPipelineBarrier(cmd, kAS | kCS | kXfer, kXFB, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

// ---- helpers shared with the split (bvhsplit.cpp) ----

VkDeviceAddress rt_buffer_address(VkBuffer b) { return buffer_address(b); }
VkDeviceSize rt_scratch_alignment() { return g_scratch_align; }

const uint8_t* mapped_ptr(VkBuffer b, VkDeviceSize offset) {
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  auto bm = tracker().buffer_mem.find(b);
  if (bm == tracker().buffer_mem.end()) return nullptr;
  auto mp = tracker().mapped.find(bm->second.first);
  if (mp == tracker().mapped.end() || !mp->second) return nullptr;
  return mp->second + bm->second.second + offset;
}

bool read_mapped(VkBuffer b, VkDeviceSize offset, void* dst, size_t n) {
  const uint8_t* p = mapped_ptr(b, offset);
  if (!p) return false;
  std::memcpy(dst, p, n);
  return true;
}

// 1. clip -> camera-relative world, in place: the opaque region, then the alpha one.
// Caller holds g_mu (or the split's lock; the pipeline is only replaced on present).
bool rt_convert(VkCommandBuffer cmd, const RtCapture& cap) {
  if (!g_pipe) return false;
  const DeviceData* dd = g_dd;
  const uint32_t prims = uint32_t(cap.opaque_bytes / (3 * cap.stride)), aprims = uint32_t(cap.alpha_bytes / (3 * cap.stride));
  // rt_capture_wide_split: a third region, the wide draws at the top of the alpha region, with their own stride.
  const uint32_t wprims = cap.wide_stride ? uint32_t(cap.wide_bytes / (3 * cap.wide_stride)) : 0u;
  for (uint32_t region = 0; region < 3; ++region) {
    const uint32_t n = region == 2 ? wprims : region ? aprims : prims;
    if (!n) continue;
    const uint32_t stride = region == 2 ? cap.wide_stride : cap.stride, tri_bytes = 3 * stride;
    const VkDeviceSize base = region == 2 ? cap.wide_base : region ? cap.alpha_base : 0;
    VkDescriptorBufferInfo in{cap.ubo, cap.ubo_offset, 224}, out{cap.xfb, base, VkDeviceSize(n) * tri_bytes};
    VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &in;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &out;
    uint32_t groups = (std::min)((n + 255) / 256, 65535u);
    uint32_t pc[4] = {n, groups * 256, stride, 0};
    dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe);
    dd->vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl, 0, 2, w);
    dd->vk.CmdPushConstants(cmd, g_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    dd->vk.CmdDispatch(cmd, groups, 1, 1);
  }
  return true;
}

// rt_cascade_casters: the cascade soup, clip -> camera-relative world with the cascade's own
// orthographic matrix (geocapture.cpp, after the cascade pass, in its command buffer).
bool rt_cascade_convert(VkCommandBuffer cmd, VkBuffer xfb, uint32_t tris, uint32_t stride, VkBuffer ubo,
                        VkDeviceSize ubo_offset, VkDeviceSize base) {
  if (!g_rt || !tris || !xfb || !ubo) return false;
  const VkPipeline pipe = g_ortho_live.load();
  const VkPipelineLayout layout = g_pl_live.load();
  if (!pipe || !layout) return false;
  {
    const DeviceData* dd = g_dd;
    VkDescriptorBufferInfo in{ubo, ubo_offset, 224}, out{xfb, base, VkDeviceSize(tris) * 3 * stride};
    VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &in;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &out;
    const uint32_t groups = (std::min)((tris + 255) / 256, 65535u);
    const uint32_t pc[4] = {tris, groups * 256, stride, 0};
    dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    dd->vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2, w);
    dd->vk.CmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    dd->vk.CmdDispatch(cmd, groups, 1, 1);
  }
  restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  return true;
}

// This build's camera, set by rt_after_capture under g_mu before bvh_build: read by code inside the build (g_mu is
// already held there: rt_build_camera would lock it again; hv6 2026-11-04: the process vanished at the first publish).
float g_this_build_cam[3] = {};
uint64_t g_this_build_frame = 0;
bool rt_this_build_camera(uint64_t frame, float pos[3]) {
  if (!frame || g_this_build_frame != frame) return false;
  std::memcpy(pos, g_this_build_cam, sizeof g_this_build_cam);
  return true;
}

bool rt_current_build_camera(float pos[3], uint64_t* frame) {
  if (!g_this_build_frame) return false;   // reset at every build's start: never a previous build's camera
  std::memcpy(pos, g_this_build_cam, sizeof g_this_build_cam);
  *frame = g_this_build_frame;
  return true;
}

bool rt_build_camera(uint64_t frame, float pos[3]) {
  std::lock_guard<std::mutex> lock(g_mu);
  for (const CamHist& h : g_cam_hist)
    if (frame && h.frame == frame) {
      std::memcpy(pos, h.pos, sizeof h.pos);
      return true;
    }
  return false;
}

namespace {
// Caller holds g_mu. Last frame's cascade-only soup for this (split) build, translated from
// last frame's camera-relative space into this one's (common/cascade_casters.h rebase).
void attach_cascade(RtCapture& cap, const float* cam, uint64_t now) {
  if (!cap.split) {
    geo_cascade_note_build(4, 0);
    return;
  }
  // The newest converted soup of F-1 or F-2: with frame generation off the game may record
  // frame F's build before F-1's cascade pass (user, 2026-10-29: 294 of 297 builds had no
  // F-1 soup, the roofs leaked). Static casters: the rebase from the soup's own origin holds.
  CascadeSoup soup{};
  int why = 1;
  uint64_t from = 0;
  for (uint64_t back = 1; back <= 2 && why; ++back) {
    why = geo_cascade_soup(now - back, &soup);
    from = now - back;
  }
  if (why) {
    geo_cascade_note_build(why, 0);
    return;
  }
  const bool own = cfg_int("rt_cascade_origin", 1) != 0 && soup.has_origin;
  const CamHist* prev = nullptr;
  for (const CamHist& h : g_cam_hist)
    if (h.frame == from) prev = &h;
  if ((!own && !prev) || !cam) {
    geo_cascade_note_build(3, 0);
    return;
  }
  // rt_cascade_origin=1: the soup is relative to the cascade UBO's own origin (byte 208);
  // 0: to that frame's camera (the first version's premise, kept for the A/B).
  cascade::rebase(own ? soup.origin : prev->pos, cam, cap.cascade_t);
  cap.cascade_xfb = soup.xfb;
  cap.cascade_tris = soup.tris;
  cap.cascade_stride = soup.stride;
  cap.cascade_sig = soup.sig;
  cap.cascade_frame = from;
  cap.cascade_wide_off = soup.wide_off;   // rt_capture_wide_split (0 triangles when off)
  cap.cascade_wide_tris = soup.wide_tris;
  cap.cascade_wide_stride = soup.wide_stride;
}
}  // namespace

namespace {
VkCommandBuffer g_stamp_cmd = VK_NULL_HANDLE;
int g_stamp_slot = -1;
void stamp_cb(VkCommandBuffer cmd, uint32_t i) { timestamp(cmd, g_stamp_slot, i); }

// Camera motion of this build's frame: the view matrix and position of the
// camera UBO (set 0 binding 2, read from its host mapping) against the last build.
float g_prev_cam[56] = {};
bool g_prev_cam_valid = false, g_cam_unreadable_logged = false;
std::atomic<bool> g_moving{false};

void note_camera(const RtCapture& cap) {
  float cam[56];
  if (!read_mapped(cap.ubo, cap.ubo_offset, cam, sizeof cam)) {
    if (!g_cam_unreadable_logged) {
      g_cam_unreadable_logged = true;
      log("[rt] the camera UBO is not host-mapped: timings are not split by camera motion");
    }
    g_moving = false;
    return;
  }
  bool moving = false;
  if (g_prev_cam_valid) {
    for (int i = 0; i < 16 && !moving; ++i) moving = std::fabs(cam[i] - g_prev_cam[i]) > 1e-5f;      // view (_m0)
    for (int i = 52; i < 55 && !moving; ++i) moving = std::fabs(cam[i] - g_prev_cam[i]) > 1e-4f;     // position (_m6)
  }
  std::memcpy(g_prev_cam, cam, sizeof cam);
  g_prev_cam_valid = true;
  g_moving = moving;
}
}  // namespace

bool rt_camera_moving() { return g_moving.load(); }

void rt_after_capture(VkCommandBuffer cmd, const RtCapture& cap) {
  if (!g_rt) return;
  const bool cpu_t = cfg_int("cpu_timing", 0) != 0;
  const double t_entry = cpu_t ? qpc_ms() : 0;
  scene_phase(2);
  struct PhaseReset { ~PhaseReset() { scene_phase(0); } } phase_reset;   // every exit: done (or no build)
  std::lock_guard<std::mutex> lock(g_mu);
  const double t_locked = cpu_t ? qpc_ms() : 0;
  if (order_trace_active())
    log("[order] capture end cmd %p frame %llu tid %lu%s", (void*)cmd, (unsigned long long)frame_index(),
        GetCurrentThreadId(), g_last_frame == frame_index() ? " (second capture this frame: no build)" : "");
  if (g_last_frame == frame_index()) return;
  const VkBuffer xfb = cap.xfb, ubo = cap.ubo;
  const VkDeviceSize ubo_offset = cap.ubo_offset;
  const uint64_t signature = cap.signature;
  const uint32_t draws = cap.draws, stride = cap.stride, tri_bytes = 3 * cap.stride;
  const uint32_t prims = uint32_t(cap.opaque_bytes / tri_bytes), aprims = uint32_t(cap.alpha_bytes / tri_bytes);
  // Two geometries whenever the capture has an alpha region (rt_alpha=1), so the
  // BLAS layout does not change with the number of alpha-tested draws.
  const uint32_t geoms = cap.alpha_region ? 2 : 1;
  // The split may have nothing to capture (everything cached); the legacy build needs triangles.
  if ((!cap.split && !prims) || !ubo || !xfb) {
    static int warned = 0;
    if (warned++ < 5) log("[rt] frame %llu: nothing to build (%u triangles, camera UBO %s)",
                          (unsigned long long)frame_index(), prims, ubo ? "found" : "missing");
    return;
  }
  if (!ensure_pipeline()) return;
  ensure_dummy_tlas(cmd);   // rt_scene_handout_layout only (once)
  // The scene slot this build writes (retired on the GPU); none free: no build
  // this frame, the previous scene stays current.
  // rt_scene_handout: the reserved slot R (its set may already be bound by the lighting); else a pick as before.
  // The guard reports the outcome on every exit (failed unless the split commit below sets ok).
  struct HandoutCommit {
    int slot = -1; bool ok = false; uint64_t rec = 0;
    ~HandoutCommit() {
      if (slot < 0) return;
      std::lock_guard<std::mutex> h(g_handout_mu);
      g_ho.commit_done(slot, ok, rec, frame_index());
      g_ho_open.store(g_ho.open(), std::memory_order_release);
    }
  } handout_commit;
  { std::lock_guard<std::mutex> h(g_handout_mu); handout_commit.slot = g_ho.commit_slot(); }
  if (handout_commit.slot >= 0 && !cap.split) {   // the legacy single-BLAS build never fills a reservation
    { std::lock_guard<std::mutex> h(g_handout_mu); g_ho.commit_done(handout_commit.slot, false, 0, frame_index());
      g_ho_open.store(g_ho.open(), std::memory_order_release); }
    handout_commit.slot = -1;
  }
  const int scene_slot = handout_commit.slot >= 0 ? handout_commit.slot : pick_scene_slot();
  if (scene_slot < 0) return;
  SceneSlot& scene = g_scene[scene_slot];
  if(native_hit::enabled()){
    scene.used=true;scene.last_use=frame_index();scene.tlas=VK_NULL_HANDLE;scene.set=VK_NULL_HANDLE;
    // Pinned until the slot is built again: slot_retired never trusts a pruned record, so a slot not re-picked
    // for 4096 records (builds failing, it stayed current) would never retire (vram_compact::prune_records).
    bvh_rec_pin(scene.material_record,false);
    scene.material_record=bvh_rec_register(cmd);
    bvh_rec_pin(scene.material_record,true);
  }
  scene_phase(3);
  const double t_prep0 = cpu_t ? qpc_ms() : 0;
  native_hit::scene_prepare(cmd,uint32_t(scene_slot),cap);
  const double t_prep1 = cpu_t ? qpc_ms() : 0;
  float source_camera[56]{};
  const bool source_camera_valid = read_mapped(cap.ubo, cap.ubo_offset, source_camera, sizeof source_camera);
  scene.pose_valid = false;
  if (cfg_int("pan_capture", 0) > 0 || cfg_int("pan_prepare", 0)) {
    void* mapped = nullptr;
    if (!scene.pose) alloc_host_buffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      &scene.pose, &scene.pose_memory, &mapped);
    scene.pose_valid = geo_copy_camera(cmd, ubo, ubo_offset, scene.pose);
  }
  note_camera(cap);
  // rt_cascade_casters: this build's camera (the next frame's cascade rebase) and last frame's soup.
  RtCapture capx = cap;
  capx.cascade_xfb = VK_NULL_HANDLE;
  capx.cascade_tris = 0;
  capx.cascade_wide_tris = 0;
  capx.cascade_sig = 0;
  capx.cascade_frame = 0;
  {
    const uint64_t now = frame_index();
    g_this_build_frame = 0;
    if (source_camera_valid) {
      CamHist& h = g_cam_hist[now % 4];
      h.frame = now;
      std::memcpy(h.pos, source_camera + 52, sizeof h.pos);
      std::memcpy(g_this_build_cam, source_camera + 52, sizeof g_this_build_cam);
      g_this_build_frame = now;
    }
    if (cfg_int("rt_cascade_casters", 0)) attach_cascade(capx, source_camera_valid ? source_camera + 52 : nullptr, now);
  }
  fault_track("soup (XFB capture)", buffer_address(xfb), cap.xfb_size);
  const DeviceData* dd = g_dd;

  // ---- Track D BVH split (bvhsplit.cpp) ----
  if (cap.split) {
    if (g_blas) {   // the legacy soup BLAS is not used while the split runs
      if (g_blas) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(g_blas));
      g_blas = VK_NULL_HANDLE;
      if (g_blas_buf.b) free_buf(g_blas_buf);
      g_blas_capacity = g_alpha_capacity = g_blas_geoms = 0;
      g_blas_valid = false;
    }
    int slot = -1;
    if (logging::stats_enabled(cfg_int) && g_qp) {
      uint32_t s = g_next_slot % kRing;
      if (!g_slots[s].frame) {
        slot = int(s);
        g_next_slot = s + 1;
        g_slots[s] = {frame_index() + 1, 2, g_moving.load(), prims};
        dd->vk.CmdResetQueryPool(cmd, g_qp, s * 4, 4);
      }
    }
    timestamp(cmd, slot, 0);
    g_stamp_slot = slot;
    BvhResult res{};
    scene_phase(4);
    const double t_bvh0 = cpu_t ? qpc_ms() : 0;
    const bool bvh_ok = bvh_build(cmd, capx, &stamp_cb, uint32_t(scene_slot), &res);
    const double t_bvh1 = cpu_t ? qpc_ms() : 0;
    if (!bvh_ok) {
      if (slot >= 0) g_slots[slot] = Slot{};   // the timestamps were not all written
      restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
      return;
    }
    restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    scene_phase(5);
    AlphaBindings ab{xfb, cap.alpha_base, aprims ? VkDeviceSize(aprims) * tri_bytes : 0, VkDeviceSize(prims) * tri_bytes,
                     cap.xfb_size, stride, true, res.info, res.info_off, res.info_bytes, res.pool, res.pool_bytes};
    ab.camera = cap.ubo;
    ab.camera_offset = cap.ubo_offset;
    ab.reference_tlas=res.tlas;
    ab.groom_tlas = res.groom_tlas;
    ab.groom_count = res.groom_count;
    ab.groom_first = res.groom_first;
    const VkDescriptorSet set = alpha_commit(cmd, ab, uint32_t(scene_slot));
    if(set)alpha_omm_commit(cmd,uint32_t(scene_slot));
    if (set && handout_commit.slot >= 0 && native_hit::enabled() && cfg_int("rt_hit_material", 0)) {
      // Increment 4b: the hit-material query of the reserved slot, built now into the G-buffer command buffer.
      const bool query = native_hit::commit_query(cmd, uint32_t(scene_slot));
      std::lock_guard<std::mutex> h(g_handout_mu);
      g_ho.commit_query(scene_slot, query);
      ++(query ? g_ho_query_built : g_ho_query_failed);
    }
    restore_bindings(cmd,VK_PIPELINE_BIND_POINT_COMPUTE);
    if (cpu_t) {
      const double t2 = qpc_ms();
      g_bcpu.bvh += t_bvh1 - t_bvh0;
      g_bcpu.alpha += t2 - t_bvh1;
      g_bcpu.lock += t_locked - t_entry;
      g_bcpu.prepare += t_prep1 - t_prep0;
      g_bcpu.commit += t2 - t_entry;
      if (++g_bcpu.n >= 300) {
        log("[cpu] scene build recording, ms/build over %llu: bvh_build %.3f, alpha_commit (records, set) %.3f; "
            "capture end to commit %.3f, of which scene lock wait %.3f, hit material scene_prepare %.3f",
            (unsigned long long)g_bcpu.n, g_bcpu.bvh / g_bcpu.n, g_bcpu.alpha / g_bcpu.n, g_bcpu.commit / g_bcpu.n,
            g_bcpu.lock / g_bcpu.n, g_bcpu.prepare / g_bcpu.n);
        g_bcpu = BuildCpu{};
      }
    }
    g_split_tlas = res.tlas;
    g_split_valid = res.tlas != VK_NULL_HANDLE;
    const bool reserved = handout_commit.slot >= 0;
    if (reserved && !set) return;   // a handed-out set without a valid build: the guard resolves it to P at the submit
    // A reserved slot takes the OMM accelerated TLAS only with its tickets (increment 4): they are registered below on
    // every recording that bound the handed-out set so far, and on later ones when they register (rt_scene_handout).
    std::vector<uint64_t> omm_tickets;
    const bool omm_ok = set && (!reserved || bvh_omm_tickets(uint32_t(scene_slot), &omm_tickets));
    const auto accelerated=omm_ok?bvh_omm_scene(uint32_t(scene_slot),res.tlas):VK_NULL_HANDLE;
    commit_scene_slot(scene_slot, accelerated?accelerated:res.tlas, set, true, res.rec);
    if (reserved) { handout_commit.ok = true; handout_commit.rec = res.rec; }
    else { std::lock_guard<std::mutex> h(g_handout_mu); g_ho.note_commit(frame_index()); }
    if(accelerated)scene.omm_reference=res.tlas;
    alpha_stamp_scene(uint32_t(scene_slot), tlas_address(res.tlas), tlas_address(accelerated ? accelerated : res.tlas));
    if (reserved && accelerated) {
      uint64_t users[scene_handout::Machine::kMaxUsers], handles[scene_handout::Machine::kMaxUsers];
      uint32_t count;
      { std::lock_guard<std::mutex> h(g_handout_mu); count = g_ho.accelerate(scene_slot, users, handles); g_ho_tickets = omm_tickets; }
      bool all = true;
      for (uint32_t i = 0; i < count; ++i) all = handout_tickets(reinterpret_cast<VkCommandBuffer>(handles[i]), users[i], omm_tickets) && all;
      if (!all) alpha_stamp_traversal_to_reference(uint32_t(scene_slot));   // monotonic: the reference never needs tickets
      std::lock_guard<std::mutex> h(g_handout_mu);
      ++(all ? g_ho_omm_accelerated : g_ho_omm_downgraded);
    }
    scene.camera = cap.ubo;
    scene.camera_offset = cap.ubo_offset;
    std::memcpy(scene.camera_pose, source_camera, sizeof source_camera);
    scene.camera_pose_valid = source_camera_valid;
    publish_water_build_camera(cmd, scene, source_camera, source_camera_valid);
    fault_checkpoint(cmd, kCkSceneCommitted, uint32_t(scene_slot));
    ++g_n_frames;
    ++g_n_split;
    g_prims_sum += res.static_tris + res.dynamic_tris;
    g_static_tris_sum += res.static_tris;
    g_instances_sum += res.instances;
    g_last_frame = frame_index();
    g_tlas_frame = frame_index();
    if (!g_logged_first) {
      g_logged_first = true;
      log("[rt] first split scene: %u TLAS instances (%u static from %u cached draws, %u dynamic draws), %.2fM + %.2fM "
          "triangles (static + dynamic), %u new meshes",
          res.instances, res.static_instances, res.cached_draws, res.dynamic_draws, double(res.static_tris) * 1e-6,
          double(res.dynamic_tris) * 1e-6, res.new_meshes);
    }
    return;
  }

  // ---- legacy: one BLAS over the whole soup ----
  const bool want_refit = cfg_int("rt_refit", 1) != 0;
  VkBuildAccelerationStructureFlagsKHR flags =
      VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
      (want_refit ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0);
  VkDeviceAddress verts = buffer_address(xfb), averts = verts + cap.alpha_base;
  if (!ensure_tlas() || !ensure_blas(prims, aprims, geoms, stride, flags, verts, averts)) {
    g_failed = true;
    log("[rt] acceleration structure allocation failed; scene building stopped");
    release_all();
    if (auto* ring = static_cast<water::BuildCameraRing*>(persist(water::kBuildCameraKey, sizeof(water::BuildCameraRing)))) {
      water::BuildCameraLock lock(*ring);
      ring->buffer = 0;
      for (auto& e : ring->entries) e = {};
      free_buf(g_water_build_cameras);
    }
    return;
  }
  // Track D phase 3: the alpha-tested draws' records and textures; InstInfo of the one instance.
  VkBuffer info = VK_NULL_HANDLE;
  VkDeviceSize info_bytes = 0;
  alpha_legacy_info(cmd, stride, &info, &info_bytes);
  AlphaBindings ab{xfb, cap.alpha_base, geoms == 2 ? VkDeviceSize(aprims) * tri_bytes : 0, VkDeviceSize(prims) * tri_bytes,
                   cap.xfb_size, stride, false, info, 0, info_bytes, VK_NULL_HANDLE, 0};
  ab.camera = cap.ubo;
  ab.camera_offset = cap.ubo_offset;
  ab.reference_tlas=g_tlas;
  const VkDescriptorSet legacy_set = alpha_commit(cmd, ab, uint32_t(scene_slot));
  int rebuild_frames = (std::max)(cfg_int("rt_rebuild_frames", 60), 1);
  // A refit reads the BLAS: only if its last build's command buffer was submitted.
  const uint64_t legacy_rec = bvh_rec_register(cmd);
  bool refit = want_refit && g_blas_valid && bvh_rec_state(g_legacy_rec) == 1 && prims == g_built_prims &&
               aprims == g_built_aprims &&
               signature == g_built_sig && int(g_since_build) < rebuild_frames;

  int slot = -1;
  if (logging::stats_enabled(cfg_int) && g_qp) {
    uint32_t s = g_next_slot % kRing;
    if (!g_slots[s].frame) {
      slot = int(s);
      g_next_slot = s + 1;
      g_slots[s] = {frame_index() + 1, refit ? 1 : 0, g_moving.load(), prims};
      dd->vk.CmdResetQueryPool(cmd, g_qp, s * 4, 4);
    }
  }
  timestamp(cmd, slot, 0);
  // The XFB writes are done; last frame's ray queries and builds are done with
  // the AS memory, scratch and instance buffer.
  barrier(cmd, kXFB | kAS | kCS, kCS | kAS | kXfer);
  fault_checkpoint(cmd, kCkLegacyStart, uint32_t(legacy_rec));
  VkAccelerationStructureInstanceKHR inst{};
  inst.transform.matrix[0][0] = inst.transform.matrix[1][1] = inst.transform.matrix[2][2] = 1.0f;
  inst.mask = 0xFF;
  // Alpha-tested geometry is tested in the ray queries; rt_alpha_test=0 makes
  // the instance opaque again (live A/B switch, no rebuild).
  const bool alpha_live = geoms == 2 && cfg_int("rt_alpha_test", 1) != 0;
  inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
               (alpha_live ? 0u : uint32_t(VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR));
  inst.accelerationStructureReference = g_blas_addr;
  dd->vk.CmdUpdateBuffer(cmd, g_inst.b, 0, sizeof inst, &inst);

  // 1. clip -> camera-relative world, in place.
  rt_convert(cmd, cap);
  barrier(cmd, kCS | kXfer, kAS);
  timestamp(cmd, slot, 1);

  // 2. BLAS
  {
    VkAccelerationStructureGeometryKHR g[2] = {blas_geometry(verts, prims, stride, true),
                                               blas_geometry(averts, aprims, stride, false)};
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bi.flags = g_blas_flags;
    bi.mode = refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bi.srcAccelerationStructure = refit ? g_blas : VK_NULL_HANDLE;
    bi.dstAccelerationStructure = g_blas;
    bi.geometryCount = geoms;
    bi.pGeometries = g;
    bi.scratchData.deviceAddress = scratch_addr();
    VkAccelerationStructureBuildRangeInfoKHR range[2] = {{prims, 0, 0, 0}, {aprims, 0, 0, 0}};
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = range;
    dd->vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &bi, &ranges);
  }
  // The TLAS build reads the BLAS and reuses the scratch memory.
  barrier(cmd, kAS, kAS);
  timestamp(cmd, slot, 2);

  // 3. TLAS (one instance, identity: the soup is already camera-relative)
  {
    VkAccelerationStructureGeometryKHR g = tlas_geometry();
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bi.dstAccelerationStructure = g_tlas;
    bi.geometryCount = 1;
    bi.pGeometries = &g;
    bi.scratchData.deviceAddress = scratch_addr();
    VkAccelerationStructureBuildRangeInfoKHR range{1, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    dd->vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &bi, &ranges);
  }
  // Ray queries in later command buffers of this frame (submission order).
  barrier(cmd, kAS, kCS | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  fault_checkpoint(cmd, kCkLegacyTlas, uint32_t(legacy_rec));
  timestamp(cmd, slot, 3);
  restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

  if (refit) {
    ++g_since_build;
    ++g_n_refit;
  } else {
    g_since_build = 0;
    ++g_n_rebuild;
  }
  ++g_n_frames;
  g_prims_sum += prims + aprims;
  g_aprims_sum += aprims;
  g_alpha_draws_sum += cap.alpha_draws;
  g_blas_valid = true;
  g_legacy_rec = legacy_rec;
  g_split_valid = false;
  commit_scene_slot(scene_slot, g_tlas, legacy_set, false, legacy_rec);
  { std::lock_guard<std::mutex> h(g_handout_mu); g_ho.note_commit(frame_index()); }
  alpha_stamp_scene(uint32_t(scene_slot), tlas_address(g_tlas), tlas_address(g_tlas));
  scene.camera = cap.ubo;
  scene.camera_offset = cap.ubo_offset;
  std::memcpy(scene.camera_pose, source_camera, sizeof source_camera);
  scene.camera_pose_valid = source_camera_valid;
  publish_water_build_camera(cmd, scene, source_camera, source_camera_valid);
  g_built_prims = prims;
  g_built_aprims = aprims;
  g_built_sig = signature;
  g_last_frame = frame_index();
  g_tlas_frame = frame_index();
  if (!g_logged_first) {
    g_logged_first = true;
    log("[rt] first scene: %u draws (%u alpha-tested), %u + %u triangles, camera UBO %p+%llu, stride %u", draws,
        cap.alpha_draws, prims, aprims, (void*)ubo, (unsigned long long)ubo_offset, stride);
  }
}

// Caller holds g_mu. The current slot, stamped as handed out in this frame;
// false before the first build and once it is stale (it may be retired then).
bool hand_out(VkAccelerationStructureKHR* tlas, VkDescriptorSet* set, uint64_t* built_frame) {
  const int slot = g_scene_cur;
  if (slot < 0) return false;
  SceneSlot& s = g_scene[slot];
  const uint64_t now = frame_index();
  if (!s.used || !s.tlas) return false;
  if (s.built + 1 < now) return false;
  // Its command buffer was dropped without a submit: the TLAS was never built.
  if (bvh_rec_state(s.rec) == 2) return false;
  if (s.split ? !g_split_valid : !g_blas_valid) return false;
  s.last_use = (std::max)(s.last_use, now);
  if (order_trace_active())
    log("[order] hand out slot %d built %llu now %llu tid %lu", slot, (unsigned long long)s.built,
        (unsigned long long)now, GetCurrentThreadId());
  alpha_slot_in_use(uint32_t(slot), now);   // the set's game views live while it may be bound
  *tlas = s.omm_reference && (!cfg_int("rt_omm",0) || !omm::scene_consumer(static_cast<uint32_t*>(persist(omm::scene_capability_key,sizeof(uint32_t)))))?s.omm_reference:s.tlas;
  if (set) *set = s.set;
  *built_frame = s.built;
  return true;
}

bool rt_scene(VkAccelerationStructureKHR* tlas, uint64_t* built_frame) {
  if (!g_rt) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  return hand_out(tlas, nullptr, built_frame);
}

VkBuffer rt_scene_pose() {
  std::lock_guard<std::mutex> lock(g_mu);
  return g_scene_cur >= 0 && g_scene[g_scene_cur].pose_valid ? g_scene[g_scene_cur].pose : VK_NULL_HANDLE;
}

// Caller holds `lock` (g_mu). Waits (bounded) for this frame's scene commit; see g_scene_cv.
void wait_log(int ms);
void wait_for_frame_scene(std::unique_lock<std::mutex>& lock) {
  const int ms = cfg_int("rt_scene_wait_ms", 12);
  const uint64_t now = frame_index();
  auto current = [&] { return g_scene_cur >= 0 && g_scene[g_scene_cur].used && g_scene[g_scene_cur].built >= now; };
  static int reset_token = 0;
  { std::lock_guard<std::mutex> w(g_wait_mu);
    if (stats_window::reset(cfg_int, reset_token)) g_wait = WaitStats{}, g_wait_handed = 0;
    ++g_wait.calls; }
  if (!current()) {
    // Captures that resumed (a scene built in the last 2 frames) end the pause at once:
    // a pause of up to 64 frames after a short gap (8 frames without the scene pass)
    // traced the previous frame's scene for about a second.
    const bool recent = g_scene_cur >= 0 && g_scene[g_scene_cur].used && g_scene[g_scene_cur].built + 2 >= now;
    if (recent) g_wait_misses = (std::min)(g_wait_misses, 7u);
    if (g_wait_misses >= 8 && now % 64 != 0) {
      ++g_wait.skipped;   // no capture lately (menus, loading): probe once every 64 frames
    } else {
      // A frame the tracer records (a PNG/dump "trace", tests only) records every pass through the tracer:
      // its scene commits 13-16 ms after the lighting starts (su1 23:48: both trace frames, the only
      // in-plan waits past 12 ms). It waits up to 4x the cap, so the traced image shows its own frame.
      const bool traced = trace_recording();
      { std::lock_guard<std::mutex> w(g_wait_mu); g_wait.traced += traced; }
      const int cap = traced ? 4 * (ms > 0 ? ms : 1) : (ms > 0 ? ms : 1);
      const auto t0 = std::chrono::steady_clock::now();
      const bool ok = g_scene_cv.wait_for(lock, std::chrono::milliseconds(cap), current);
      const double dt = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      std::lock_guard<std::mutex> w(g_wait_mu);
      ++g_wait.waited;
      g_wait.ms += dt;
      g_wait.ms_max = (std::max)(g_wait.ms_max, dt);
      if (ok) { ++g_wait.got; g_wait_misses = 0; } else {
        ++g_wait.timeouts; ++g_wait_misses;
        // A phase set in an earlier frame means this frame's capture has not begun.
        const int ph = g_scene_phase_frame.load(std::memory_order_relaxed) == now ? g_scene_phase.load(std::memory_order_relaxed) : 0;
        ++g_wait.at[(std::clamp)(ph, 0, kScenePhases - 1)];
      }
    }
  }
  wait_log(ms);
}

// The "[rt] scene wait" line (and the handout line) once per rt_scene_wait_log_frames first queries. Takes g_wait_mu.
void wait_log(int ms) {
  std::lock_guard<std::mutex> w(g_wait_mu);
  // rt_scene_wait_log_frames: lean jobs use 120 (complete lines in every cost window for the gate).
  if (g_wait.calls >= uint64_t(stats_period(unsigned((std::clamp)(cfg_int("rt_scene_wait_log_frames", 600), 60, 6000))))) {
    if (g_wait.timeouts || logging::stats_enabled(cfg_int))
    log("[rt] scene wait: %llu frames, %llu waited (%llu got this frame's scene, %llu timeouts, %llu skipped), "
        "wait mean %.3f ms, max %.3f ms%s",
        (unsigned long long)g_wait.calls, (unsigned long long)g_wait.waited, (unsigned long long)g_wait.got,
        (unsigned long long)g_wait.timeouts, (unsigned long long)g_wait.skipped,
        g_wait.waited ? g_wait.ms / double(g_wait.waited) : 0.0, g_wait.ms_max,
        g_wait_handed ? (", handed out " + std::to_string(g_wait_handed) + " (0 ms)").c_str() : "");
    {   // rt_scene_handout=2 (common/scene_handout.h): whenever a reservation was made in the window
      scene_handout::Counters c;
      uint64_t omm_accelerated, omm_downgraded, query_built, query_failed;
      { std::lock_guard<std::mutex> h(g_handout_mu); c = g_ho.n; g_ho.n = {};
        omm_accelerated = g_ho_omm_accelerated; omm_downgraded = g_ho_omm_downgraded; g_ho_omm_accelerated = g_ho_omm_downgraded = 0;
        query_built = g_ho_query_built; query_failed = g_ho_query_failed; g_ho_query_built = g_ho_query_failed = 0; }
      if (c.reserved || c.handed || c.fallbacks())
        log("[rt] scene handout: %llu frames, reserved %llu, handed out %llu, in order %llu, fallbacks %llu (not committed %llu, "
            "build failed %llu, order %llu, dropped %llu), pairing mismatches %llu, committed first %llu, unused %llu, users refused %llu; "
            "OMM accelerated commits %llu, downgraded %llu; hit material queries at the commit %llu, failed %llu "
            "(fallbacks: query %llu, camera %llu)",
            (unsigned long long)g_wait.calls, (unsigned long long)c.reserved, (unsigned long long)c.handed,
            (unsigned long long)c.in_order, (unsigned long long)c.fallbacks(),
            (unsigned long long)c.fallback[uint32_t(scene_handout::Fallback::kNotCommitted)],
            (unsigned long long)c.fallback[uint32_t(scene_handout::Fallback::kBuildFailed)],
            (unsigned long long)c.fallback[uint32_t(scene_handout::Fallback::kOrder)],
            (unsigned long long)c.fallback[uint32_t(scene_handout::Fallback::kDropped)], (unsigned long long)c.mismatches(),
            (unsigned long long)c.committed_first, (unsigned long long)c.unused, (unsigned long long)c.refused_busy,
            (unsigned long long)omm_accelerated, (unsigned long long)omm_downgraded, (unsigned long long)query_built,
            (unsigned long long)query_failed, (unsigned long long)c.fallback[uint32_t(scene_handout::Fallback::kQuery)],
            (unsigned long long)c.fallback[uint32_t(scene_handout::Fallback::kCamera)]);
    }
    if (g_handout_downgrades) {   // rt_scene_handout: stamp traversal addresses set back to the reference TLAS
      log("[rt] scene handout: %llu stamp traversal downgrade(s) in the last %llu frames", (unsigned long long)g_handout_downgrades,
          (unsigned long long)g_wait.calls);
      g_handout_downgrades = 0;
    }
    if (g_wait.traced) log("[rt] scene wait: %llu traced frame(s) in the last %llu waited up to %d ms (4x rt_scene_wait_ms)",
                           (unsigned long long)g_wait.traced, (unsigned long long)g_wait.calls, 4 * ms);
    if (g_wait.timeouts) {
      std::string at;
      for (int i = 0; i < kScenePhases; ++i) { char b[64]; snprintf(b, sizeof b, "%s%s %llu", i ? ", " : "", kScenePhaseName[i], (unsigned long long)g_wait.at[i]); at += b; }
      log("[rt] scene wait timeouts by the scene thread's phase: %s", at.c_str());
    }
    g_wait = WaitStats{};
    g_wait_handed = 0;
  }
}

VkAccelerationStructureKHR rt_scene_dummy_tlas() {
  if (!g_rt) return VK_NULL_HANDLE;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_dummy_ready && g_dummy_rec && bvh_rec_state(g_dummy_rec) == 1) {
    g_dummy_ready = true;
    bvh_rec_pin(g_dummy_rec, false);
  }
  return g_dummy_ready ? g_dummy_tlas : VK_NULL_HANDLE;
}

// rt_scene_handout=2: the reserved slot's set and the dummy TLAS for `cmd`, without waiting and without g_mu.
// false: no handout (keys off, no reservation yet, or this frame's scene is already committed): the caller takes
// rt_scene_set (which waits only when this frame's scene is not committed yet).
bool rt_scene_handout(VkCommandBuffer cmd, VkDescriptorSet* set, VkAccelerationStructureKHR* dummy, uint64_t* frame) {
  if (!g_rt || !set || !dummy || !cmd || !handout_mode()) return false;
  const uint64_t now = frame_index(), user = cmd_recording(cmd);
  if (!user) return false;
  // *set on entry: a further command buffer registering for the set it already bound (only that handout).
  int expect = -1;
  if (*set) {
    for (uint32_t i = 0; i < kSceneSlots; ++i) if (alpha_slot_set(i) == *set) expect = int(i);
    if (expect < 0) return false;
  }
  int slot;
  bool first;
  {
    std::lock_guard<std::mutex> h(g_handout_mu);
    const uint64_t before = g_ho.n.handed;
    slot = g_ho.hand_out(now, user, expect, uint64_t(cmd));
    first = g_ho.n.handed != before;
    g_ho_open.store(g_ho.open(), std::memory_order_release);
  }
  if (slot < 0) return false;
  {   // a recording joining after an accelerated commit holds the slot's OMM tickets too, or byte 24 goes back
    std::vector<uint64_t> tickets;
    bool accelerated;
    { std::lock_guard<std::mutex> h(g_handout_mu); accelerated = g_ho.accelerated && g_ho.slot == slot; if (accelerated) tickets = g_ho_tickets; }
    if (accelerated && !handout_tickets(cmd, user, tickets)) {
      alpha_stamp_traversal_to_reference(uint32_t(slot));
      std::lock_guard<std::mutex> h(g_handout_mu);
      ++g_ho_omm_downgraded;
    }
  }
  *set = alpha_slot_set(uint32_t(slot));
  *dummy = g_dummy_tlas;
  if (frame) *frame = now;
  if (omm::available()) omm::mark_feedback_use(cmd);   // OMM variants may write the stamp's feedback word (144)
  if (first) {   // this frame's first RT query: counted with the scene wait line as a 0 ms wait that got its scene
    { std::lock_guard<std::mutex> w(g_wait_mu); ++g_wait.calls; ++g_wait.waited; ++g_wait.got; ++g_wait_handed; }
    wait_log(cfg_int("rt_scene_wait_ms", 12));
  }
  if (order_trace_active())
    log("[order] handout slot %d frame %llu cmd %p tid %lu", slot, (unsigned long long)now, (void*)cmd, GetCurrentThreadId());
  return *set != VK_NULL_HANDLE;
}

// Every vkQueueSubmit, before the driver call (layer.cpp): resolve an open handout whose recording is in the batch
// (common/scene_handout.h resolve). In order: R's build runs first. Otherwise set P is copied into set R now (all
// bindings are update-after-bind, the batch is not submitted yet), P's traversal address downgraded to its reference.
void rt_scene_handout_submit(const std::vector<VkCommandBuffer>& cmds) {
  if (!g_rt || !g_ho_open.load(std::memory_order_acquire)) return;
  std::vector<uint64_t> recs(cmds.size());
  for (size_t i = 0; i < cmds.size(); ++i) recs[i] = cmd_recording(cmds[i]);
  std::lock_guard<std::mutex> lock(g_mu);   // no scene build is half recorded while R's set may be rewritten
  const uint64_t now = frame_index();
  scene_handout::Resolution r;
  std::vector<VkCommandBuffer> users;
  {
    std::lock_guard<std::mutex> h(g_handout_mu);
    auto user_index = [&](uint64_t user) {
      for (size_t i = 0; i < recs.size(); ++i) if (recs[i] && recs[i] == user) return int(i);
      return -1;
    };
    auto record_before = [&](uint64_t rec, int at) {
      for (size_t j = 0; j < cmds.size(); ++j)
        if (bvh_rec_matches_command(rec, cmds[j])) return int(j) < at;   // in this batch: its position decides
      return bvh_rec_state(rec) == 1;   // an earlier submit
    };
    const int slot = g_ho.slot;
    for (uint32_t i = 0; i < g_ho.user_count; ++i) users.push_back(reinterpret_cast<VkCommandBuffer>(g_ho.handles[i]));
    auto cameras = [&](int s) {
      for (const auto& [buffer, offset] : g_ho_cameras)
        if (!native_hit::handout_camera_ok(uint32_t(s), buffer, offset)) return scene_handout::Fallback::kCamera;
      return scene_handout::Fallback::kNone;
    };
    r = g_ho.resolve(now, 8, user_index, record_before, cameras);
    if (r.resolved || slot < 0) g_ho_cameras.clear();
    g_ho_open.store(g_ho.open(), std::memory_order_release);
  }
  if (r.resolved && native_hit::enabled())   // users own the query, or P's query set is copied into R's (increment 4b)
    native_hit::handout_settle(uint32_t(r.slot), uint32_t(r.prev), r.in_order, users);
  if (!r.resolved) return;
  SceneSlot& R = g_scene[r.slot];
  R.last_use = (std::max)(R.last_use, now);
  if (r.in_order) {
    alpha_slot_in_use(uint32_t(r.slot), now);
  } else {
    SceneSlot& P = g_scene[r.prev];
    P.last_use = (std::max)(P.last_use, now);
    alpha_copy_set(uint32_t(r.prev), uint32_t(r.slot));
    alpha_stamp_traversal_to_reference(uint32_t(r.prev));
    alpha_slot_in_use(uint32_t(r.prev), now);
    if (r.poison) {   // R was committed but its build runs after this batch: P stays the scene
      if (g_scene_cur == r.slot) g_scene_cur = r.prev;
      R.built = 0;
    }
    log_stats("[rt] scene handout fallback: frame %llu slot %d -> previous slot %d (%s)", (unsigned long long)r.frame, r.slot,
              r.prev, scene_handout::fallback_name(r.reason));
  }
  update_oldest_live();
}

bool rt_scene_set(VkAccelerationStructureKHR* tlas, VkDescriptorSet* set, uint64_t* built_frame) {
  if (!g_rt) return false;
  std::unique_lock<std::mutex> lock(g_mu);
  *set = VK_NULL_HANDLE;
  if (cfg_int("rt_scene_wait", 1)) {
    static uint64_t last_frame = 0;   // the first RT query of a frame waits; later ones reuse its result
    if (last_frame != frame_index()) {
      last_frame = frame_index();
      wait_for_frame_scene(lock);
    }
  }
  return hand_out(tlas, set, built_frame);
}

// traversal (ABI 31): the caller's shaders may trace the stamp's traversal address (byte 24). Only such a refusal
// downgrades it; a reference lookup (water: its shaders read the reference word 16) leaves it alone.
bool rt_omm_scene_reference(VkCommandBuffer cmd,VkAccelerationStructureKHR selected,VkAccelerationStructureKHR* reference,uint64_t* identity,bool traversal){
  if(!reference || !identity)return false;const uint64_t expected=*identity;*reference=VK_NULL_HANDLE;*identity=0;
  if(!g_rt || !selected || !cmd)return false;
  std::lock_guard lock(g_mu);
  for(uint32_t i=0;i<kSceneSlots;++i){
    SceneSlot& s=g_scene[i];
    if(!s.used || (expected && expected!=s.rec) || (s.tlas!=selected && s.omm_reference!=selected) ||
       slot_retired(s,life_gpu_done(),frame_index()) || bvh_rec_state(s.rec)==2)continue;
    *reference=s.omm_reference?s.omm_reference:s.tlas;*identity=s.rec;
    s.last_use=(std::max)(s.last_use,frame_index());alpha_slot_in_use(i,s.last_use);update_oldest_live();
    // Even reference-only dispatches can write stale-proof feedback at byte144.
    if(!omm::mark_feedback_use(cmd) && omm::available()){
      if(scene_handout::downgrade_on_refusal(traversal,false,s.omm_reference!=VK_NULL_HANDLE) && alpha_stamp_traversal_to_reference(i))++g_handout_downgrades;
      *reference=VK_NULL_HANDLE;*identity=0;return false;}
    uint64_t accelerated_identity{};
    const bool fast=s.omm_reference && bvh_omm_use(cmd,i,selected,&accelerated_identity) && accelerated_identity==s.rec;
    // rt_scene_handout: a recording that may not trace the accelerated TLAS binds the reference one; the stamp's
    // traversal address (read by spec-1051 shaders) follows, never back up (docs/SCENE_HANDOUT_DESIGN.md 3.3).
    if(scene_handout::downgrade_on_refusal(traversal,fast,s.omm_reference!=VK_NULL_HANDLE) && alpha_stamp_traversal_to_reference(i))++g_handout_downgrades;
    return fast;
  }
  return false;
}

uint64_t rt_scene_oldest_live() { return g_oldest_live.load(); }

bool rt_replay_region(VkBuffer info, VkDeviceSize info_offset, BvhReplayRegion* out) {
  if (!g_rt) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  return bvh_replay_region(info, info_offset, out);
}
bool rt_hit_material_use(VkCommandBuffer cmd,VkAccelerationStructureKHR selected,VkDescriptorSet alpha,uint64_t identity,VkDescriptorSet* query_alpha){
  // rt_scene_handout=2 (increment 4b): a pass bound to the handed-out set and the dummy TLAS gets the slot's handout
  // query set; the query is built at the commit, the camera this pass bound is checked against it at the submit.
  if(query_alpha && selected && selected==g_dummy_tlas && alpha && native_hit::enabled() && cfg_int("rt_hit_material",0)){
    *query_alpha={};
    int slot=-1;
    {std::lock_guard h(g_handout_mu);
     if(g_ho.open() && g_ho.handed && g_ho.slot>=0 && alpha_slot_set(uint32_t(g_ho.slot))==alpha){
       const uint64_t user=cmd_recording(cmd);
       for(uint32_t i=0;i<g_ho.user_count;++i)if(g_ho.users[i]==user)slot=g_ho.slot;}}
    if(slot<0)return false;
    Bg3Binding camera{};
    if(!query_binding(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,0,2,0,&camera) || !camera.buffer)return false;
    {std::lock_guard h(g_handout_mu);
     if(g_ho.slot!=slot || !g_ho.open())return false;
     const std::pair<VkBuffer,VkDeviceSize> c{camera.buffer,camera.offset};
     if(std::find(g_ho_cameras.begin(),g_ho_cameras.end(),c)==g_ho_cameras.end())g_ho_cameras.push_back(c);}
    *query_alpha=native_hit::handout_set(uint32_t(slot));
    return *query_alpha!=VK_NULL_HANDLE;
  }
  if(query_alpha)*query_alpha={};
  static std::atomic<uint64_t> last_query_log{UINT64_MAX};const auto now=frame_index();
  if(cfg_int("rt_stats",0) && now%120==0 && last_query_log.exchange(now)!=now){
    const auto* state=cmd?cmd_state(cmd):nullptr;Bg3Binding camera{};const bool bound=cmd && query_binding(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,0,2,0,&camera);
    log("[hit material query entry] frame %llu cmd %p level %u flags 0x%x recording %llu selected %p alpha %p identity %llu capability %u requested %u camera bound %u %p+%llu",
      (unsigned long long)now,(void*)cmd,state?unsigned(state->level):UINT32_MAX,state?unsigned(state->begin_flags):0u,(unsigned long long)(state?state->recording:0),
      (void*)selected,(void*)alpha,(unsigned long long)identity,unsigned(native_hit::enabled()),unsigned(cfg_int("rt_hit_material",0)!=0),unsigned(bound),(void*)camera.buffer,(unsigned long long)camera.offset);
  }
  if(!query_alpha || !g_rt || !native_hit::enabled() || !cfg_int("rt_hit_material",0) || !cmd || !selected || !alpha || !identity)return false;
  if(cfg_hot(CfgHot::rt_hit_material_recycle)){
    // The query (register_use: rows, copies, set clone, join/remap/gather recording) runs without
    // g_mu, which the next frame's capture end needs to commit its scene slot. The slot is found
    // and kept live (last_use = now: slot_retired is false until the GPU is 2 frames past it, so
    // no build can take it) under g_mu; register_use checks the slot's frame and generation as
    // before, so rows of another frame are still rejected.
    uint32_t found=UINT32_MAX;
    {std::lock_guard lock(g_mu);
     for(uint32_t i=0;i<kSceneSlots;++i){auto& s=g_scene[i];
       if(!s.used || s.rec!=identity || s.set!=alpha || (s.tlas!=selected && s.omm_reference!=selected) ||
          slot_retired(s,life_gpu_done(),frame_index()) || bvh_rec_state(s.rec)==2)continue;
       s.last_use=(std::max)(s.last_use,frame_index());alpha_slot_in_use(i,s.last_use);found=i;break;}}
    if(found==UINT32_MAX || !native_hit::register_use(cmd,found,query_alpha))return false;
    // The views the query retained live with the slot too (as the locked path does after it).
    std::lock_guard lock(g_mu);auto& s=g_scene[found];
    if(s.rec==identity && s.set==alpha){s.last_use=(std::max)(s.last_use,frame_index());alpha_slot_in_use(found,s.last_use);}
    return true;
  }
  std::lock_guard lock(g_mu);
  for(uint32_t i=0;i<kSceneSlots;++i){auto& s=g_scene[i];
    if(!s.used || s.rec!=identity || s.set!=alpha || (s.tlas!=selected && s.omm_reference!=selected) ||
       slot_retired(s,life_gpu_done(),frame_index()) || bvh_rec_state(s.rec)==2)continue;
    if(!native_hit::register_use(cmd,i,query_alpha))return false;
    s.last_use=(std::max)(s.last_use,frame_index());alpha_slot_in_use(i,s.last_use);return true;
  }return false;
}

void rt_log_state(const char* prefix) {
  if (!g_rt) return;
  std::unique_lock<std::mutex> lock(g_mu, std::try_to_lock);   // never block a device-loss report
  if (!lock.owns_lock()) {
    log("%s scene slots: busy (lock held)", prefix);
    return;
  }
  const uint64_t gpu_done = life_gpu_done(), now = frame_index();
  for (uint32_t i = 0; i < kSceneSlots; ++i) {
    const SceneSlot& s = g_scene[i];
    if (!s.used) continue;
    static const char* const kRec[] = {"unknown", "pending", "submitted", "DROPPED"};
    log("%s scene slot %u%s: %s, built frame %llu, last handed out %llu, TLAS %p, set %p, %s, build record %llu %s", prefix,
        i, int(i) == g_scene_cur ? " (current)" : "", s.split ? "split" : "legacy", (unsigned long long)s.built,
        (unsigned long long)s.last_use, (void*)s.tlas, (void*)s.set, slot_retired(s, gpu_done, now) ? "retired" : "in flight",
        (unsigned long long)s.rec, kRec[bvh_rec_state(s.rec) + 1]);
  }
  log("%s builds skipped for want of a retired slot: %llu", prefix, (unsigned long long)g_scene_skipped);
}

void rt_on_present() {
  if (!g_rt || !g_qp) return;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    update_oldest_live();   // what bvh_on_present may release (rtscene before bvhsplit: the lock order)
  }
  bvh_on_present();
  std::lock_guard<std::mutex> lock(g_mu);
  hair_timing_observe();
  static int reset_token = 0;
  if (stats_window::reset(cfg_int, reset_token)) {
    g_hair_timing_reset = true;
    for (auto& slot : g_slots) slot.frame = 0;
  }
  if (g_hair_timing_reset) {
    for (auto& modes : g_acc) for (auto& acc : modes) acc = Acc{};
    g_n_frames = g_n_rebuild = g_n_refit = g_n_split = 0;
    g_prims_sum = g_aprims_sum = g_alpha_draws_sum = g_static_tris_sum = g_instances_sum = 0;
    g_hair_timing_reset = false;
  }
  // Compile (and recompile after edits) on the present thread, not while
  // the game records.
  if (!g_failed && cfg_hot(CfgHot::rt_build)) ensure_pipeline();
  if (!g_failed && cfg_int("rt_cascade_casters", 0) &&
      ensure_pipeline_file("rt/xfb_to_world_ortho.hlsl", g_ortho_pipe, g_ortho_gen)) {
    g_pl_live = g_pl;
    g_ortho_live = g_ortho_pipe;
  }
  uint64_t now = frame_index();
  for (uint32_t s = 0; logging::stats_enabled(cfg_int) && s < kRing; ++s) {
    Slot& sl = g_slots[s];
    if (!sl.frame || sl.frame + 4 > now) continue;
    uint64_t r[8] = {};
    VkResult res = g_dd->vk.GetQueryPoolResults(g_dd->device, g_qp, s * 4, 4, sizeof r, r, 16,
                                                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    bool done = res == VK_SUCCESS && r[1] && r[3] && r[5] && r[7];
    if (!done && sl.frame + 60 > now) continue;
    const int mode = sl.mode;
    const bool moving = sl.moving;
    const bool same_hair_epoch = g_hair_timing_epoch.accepts(sl.hair_epoch);
    sl = Slot{};
    if (!done || !same_hair_epoch || r[6] < r[0]) continue;
    auto ms = [&](uint64_t a, uint64_t b) { return float(double(b - a) * g_ts_period * 1e-6); };
    Acc& a = g_acc[mode][moving ? 1 : 0];
    a.conv.push_back(ms(r[0], r[2]));
    a.blas.push_back(ms(r[2], r[4]));
    a.tlas.push_back(ms(r[4], r[6]));
    a.total.push_back(ms(r[0], r[6]));
  }
  int every = stats_period(unsigned((std::max)(cfg_int("rt_timing_frames", 300), 30)));
  if (g_n_frames < uint32_t(every)) return;
  auto pct = [](std::vector<float>& v, double q) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    return v[(std::min)(v.size() - 1, size_t(double(v.size()) * q))];
  };
  static const char* kMode[3] = {"legacy BLAS rebuild", "legacy BLAS refit", "split"};
  static const char* kParts[3][3] = {{"convert", "BLAS", "TLAS"}, {"convert", "BLAS", "TLAS"},
                                     {"convert+extract", "BLAS builds/refits", "TLAS"}};
  for (int k = 0; k < 3; ++k)
    for (int mv = 0; mv < 2; ++mv) {
      Acc& a = g_acc[k][mv];
      if (a.total.empty()) continue;
      size_t n = a.total.size();
      float c = pct(a.conv, 0.5), b = pct(a.blas, 0.5), t = pct(a.tlas, 0.5);
      float tm = pct(a.total, 0.5), t99 = pct(a.total, 0.99), b99 = pct(a.blas, 0.99);
      log_stats("[rt] scene build GPU (%s, %s, %zu samples): median %s %.3f ms, %s %.3f ms (p99 %.3f), %s %.3f ms; total "
          "median %.3f ms, p99 %.3f ms",
          kMode[k], mv ? "camera moving" : "static", n, kParts[k][0], c, kParts[k][1], b, b99, kParts[k][2], t, tm, t99);
      a = Acc{};
    }
  if (g_n_split)
    log_stats("[rt] last %u frames: %u split builds (%.0f TLAS instances, %.3fM static + %.3fM dynamic triangles average), "
        "%u legacy rebuilds, %u refits",
        g_n_frames, g_n_split, double(g_instances_sum) / g_n_split, double(g_static_tris_sum) / g_n_split * 1e-6,
        double(g_prims_sum - g_static_tris_sum) / g_n_frames * 1e-6, g_n_rebuild, g_n_refit);
  else
    log_stats("[rt] last %u frames: %u rebuilds, %u refits, %.2fM triangles average (%.3fM alpha-tested in %.1f draws), BLAS "
        "capacity %u + %u",
        g_n_frames, g_n_rebuild, g_n_refit, double(g_prims_sum) / g_n_frames * 1e-6,
        double(g_aprims_sum) / g_n_frames * 1e-6, double(g_alpha_draws_sum) / g_n_frames, g_blas_capacity,
        g_alpha_capacity);
  g_n_frames = g_n_rebuild = g_n_refit = g_n_split = 0;
  g_prims_sum = g_aprims_sum = g_alpha_draws_sum = g_static_tris_sum = g_instances_sum = 0;
}

// Diagnostic: who allocates memory with VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
// (the reason the core bufferDeviceAddress feature is enabled, see above).
static VKAPI_ATTR VkResult VKAPI_CALL AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* ai,
                                                     const VkAllocationCallbacks* alloc, VkDeviceMemory* out) {
  static std::atomic<int> logged{0};
  if (g_rt && logged < 4)
    for (auto p = static_cast<const VkBaseInStructure*>(ai->pNext); p; p = p->pNext)
      if (p->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO &&
          (reinterpret_cast<const VkMemoryAllocateFlagsInfo*>(p)->flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT) &&
          logged++ < 4) {
        HMODULE mod = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(_ReturnAddress()), &mod))
          GetModuleFileNameA(mod, name, MAX_PATH);
        log("[rt] DEVICE_ADDRESS allocation of %llu KB, caller %s", (unsigned long long)(ai->allocationSize >> 10), name);
      }
  return device_of(device)->vk.AllocateMemory(device, ai, alloc, out);
}

void register_rt_hooks(std::unordered_map<std::string, PFN_vkVoidFunction>& m) {
  m["vkAllocateMemory"] = reinterpret_cast<PFN_vkVoidFunction>(&AllocateMemory);
}

}  // namespace bg3
