// Track D D-EM: emissive geometry capture (docs/TRACK_D_RT.md 13). The data side:
// which draws emit, their geometry in a TLAS of their own, a record per draw and a
// light list. Effects that use it (emission in GI and reflections, light sampling) come
// later; rt_debug=16/17/18 shows and checks the data.
//
// Where emission is drawn (AUDIT_ENGINE_DATA 2.2): after the G-buffer, BG3 redraws its
// emissive meshes, depth-tested, into the effects target, a B10G11R11 render-resolution
// target that shares the G-buffer's D24S8 depth and that compose adds to the lit
// diffuse and specular. The sky and the clouds are drawn into it too (engine shaders),
// as are emissive deferred decals. So:
//   pass:  one colour attachment, B10G11R11_UFLOAT, depth D24S8, render resolution
//          (em_is_fx_pass). The SSS and forward passes have the same signature: a pass
//          is an effects pass when one of its draws is an EMI material variant;
//   draw:  its fragment shader is, in the asset DB (docs/ASSET_DB.md), a material's EMI
//          variant (pass 2). Decals (box volumes, not surfaces) and skydome backdrops
//          are counted, not captured. Engine and unknown shaders (sky, clouds, magic
//          VFX not in the database) are counted;
//   radiance (one record per draw): the material's GlowColour x GlowMultiplier (asset
//          DB roles, read from the game's mapped constant buffer at record time) x the
//          glow map; "BGM" templates also multiply by their base colour (a base-colour
//          map, or the virtual-texture albedo for the _VT ones); luminance-emissive
//          materials by the game's global scale (set 2 binding 0 [0], bound as a buffer:
//          its device memory is not host-visible). Families decoded from the captures:
//          family_fixup.
//
// Geometry: every vertex shader is already patched for transform feedback. Each
// emissive draw is captured into the emissive soup (a ring) and converted to camera-
// relative world after its pass, in the pass's command buffer, with its camera UBO.
//
// Build: per game submission. The draws are kept per command buffer; when the game
// submits the command buffers holding a frame's effects passes, the layer records a
// command buffer of its own that builds one BLAS per draw (bvhsplit.cpp scene 1: always
// a full build), the emissive TLAS and the light list (shaders/rt/em_lights.hlsl), and
// submits it right after the game's batch on the same queue. That groups exactly one
// frame's draws whatever order the game records its command buffers in (it records the
// next frame's effects passes before this frame's lighting; a build at the lighting lost
// the device), and does not depend on the BVH split's mesh cache (rt_bvh_split may be 0).
// Consumers in later frames see the latest build: one frame of latency, the build
// camera's position in the header (em_shift).
//
// Separate TLAS, not an instance mask in the main one: see shaders/rt/em_common.hlsli.
#include "host.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_set>

#include "assetdb.h"
#include "rt_pipeline.h"
#include "../common/emissive_policy.h"

namespace bg3 {

namespace {

constexpr uint32_t kRing = 8;            // descriptor sets and host regions in flight
constexpr uint32_t kOwn = 8;             // own command buffers (fenced)
constexpr uint32_t kMaxRecs = 1024;      // emissive draws per build
constexpr uint32_t kMaxTex = 256;        // textures per build (slot 0: dummy)
constexpr uint32_t kMaxCache = 16;       // VT tile caches per build (slot 0: dummy)
constexpr uint32_t kMaxInst = 16384;     // emissive TLAS instances that get lights
constexpr uint32_t kHeaderWords = 16 + 1024;
constexpr uint32_t kStatWords = 16 + kMaxRecs * 16;   // per record (em_debug.hlsl rec_stat): ours, game, pixels, ...
constexpr VkDeviceSize up256(VkDeviceSize v) { return (v + 255) / 256 * 256; }
constexpr VkDeviceSize kRecBytes = 256;
constexpr VkDeviceSize kOffRecs = 0;
constexpr VkDeviceSize kOffPrefix = up256(kOffRecs + VkDeviceSize(kMaxRecs) * kRecBytes);
constexpr VkDeviceSize kOffHeader = up256(kOffPrefix + VkDeviceSize(kMaxInst) * 8);
constexpr VkDeviceSize kOffStats = up256(kOffHeader + VkDeviceSize(kHeaderWords) * 4);
constexpr VkDeviceSize kRegion = up256(kOffStats + VkDeviceSize(kStatWords) * 4);
constexpr uint32_t kTsPairs = 128;       // timestamp pairs: effects passes and builds

constexpr VkPipelineStageFlags kAS = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
constexpr VkPipelineStageFlags kCS = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags kXFB = VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;

// The record layout, its modes and flags, the radiance formulas and the calibration
// rule are policy in common/emissive_policy.h (pure, unit-tested), run by the core
// (core/em_policy.cpp, ABI 24): here only the layout and flags are used.
using em::EmRec;
using em::kModeConst; using em::kModeMaskR; using em::kModeMaskRGB; using em::kModeBlackbody;
using em::kFlagLuminance; using em::kFlagApprox; using em::kFlagPulse; using em::kFlagBaseMap; using em::kFlagVtAlbedo;
using em::kFlagForward; using em::kFlagCalibrated;
static_assert(sizeof(EmRec) == kRecBytes, "EmRec must match em_common.hlsli");

struct Buf {
  VkBuffer b = VK_NULL_HANDLE;
  VkDeviceMemory m = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  uint8_t* map = nullptr;
};

DeviceData* g_dd = nullptr;
bool g_on = false;
std::mutex g_mu;

// ---- materials (asset DB), per fragment shader ----
// kMatForward: an emissive surface material drawn in the forward pass (FOR variant, not VFX,
// water or decal), e.g. Grymforge's ENV_Lava; captured, its radiance from the DB roles.
enum MatKind : uint8_t { kMatEngine, kMatUnknown, kMatEmissive, kMatDecal, kMatBackdrop, kMatOther, kMatForward, kMatKinds };
struct Mat {
  MatKind kind = kMatUnknown;
  Bg3MaterialInfo info{};
  std::string name;
};
std::unordered_map<uint64_t, Mat> g_mats;

struct Census {
  uint32_t passes = 0, draws = 0, kinds[kMatKinds] = {}, captured = 0;
  uint32_t skip_xfb = 0, skip_indirect = 0, skip_topo = 0, skip_full = 0, skip_recs = 0, skip_tex = 0, no_ubo = 0;
  uint32_t global_conflicts = 0, vt_missing = 0, cb_unreadable = 0, pulse_no_time = 0;
  uint32_t skip_core = 0;   // no core loaded (the record policy is the core's, ABI 24)
  uint32_t skip_excluded = 0;   // draws of a family the policy excludes (em::kFlagExcluded, rt_em_fx_pulse)
  uint64_t captured_tris = 0;
  void add(const Census& o) {
    passes += o.passes, draws += o.draws, captured += o.captured, captured_tris += o.captured_tris;
    for (int i = 0; i < kMatKinds; ++i) kinds[i] += o.kinds[i];
    skip_xfb += o.skip_xfb, skip_indirect += o.skip_indirect, skip_topo += o.skip_topo, skip_full += o.skip_full;
    skip_recs += o.skip_recs, skip_tex += o.skip_tex, no_ubo += o.no_ubo, global_conflicts += o.global_conflicts;
    vt_missing += o.vt_missing, cb_unreadable += o.cb_unreadable, pulse_no_time += o.pulse_no_time;
    skip_core += o.skip_core, skip_excluded += o.skip_excluded;
  }
};
struct FsCount {
  uint32_t draws = 0, captured = 0;
  uint32_t uv[5] = {};              // draws per UvMatch (spirv_xfb.h)
  int uv_loc = -1, uv_fs = -1;      // the last draw's captured location and the FS's UV location
  uint32_t uv_vs_comps = 0;
};
std::map<uint64_t, FsCount> g_fs_window;   // per fragment shader over the log window
uint32_t g_stale = 0;                      // command buffers re-recorded before submission
uint32_t g_busy = 0;                       // builds skipped: no free command buffer of our own

// The effects target, as last seen.
struct Fx { VkImage image = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; uint32_t w = 0, h = 0; uint64_t frame = 0; };
Fx g_fx;

// ---- per command buffer (draws recorded, not yet submitted) ----
struct View { VkImageView v = VK_NULL_HANDLE; VkImageLayout l = VK_IMAGE_LAYOUT_UNDEFINED; };
struct PDraw {
  EmRec rec{};
  View mask, base, vt_ind, vt_cache;
  VkDeviceSize off = 0;
  VkBuffer soup = VK_NULL_HANDLE;     // the soup it was captured into (a grown soup replaces it)
  uint32_t pulse_no_time = 0;         // the pulse's time was not host-readable (its mean was used)
  uint32_t prims = 0;
  DrawArgs args{};
  uint32_t pipe_id = 0;
  uint64_t vs = 0;
  VkBuffer vb0 = VK_NULL_HANDLE, ib = VK_NULL_HANDLE;
  VkDeviceSize vb0_off = 0, ib_off = 0;
};
struct PCmd {
  std::vector<PDraw> draws;
  bool fx = false;                     // holds an effects pass (an EMI draw)
  Census census;
  VkBuffer ubo = VK_NULL_HANDLE;       // a pass's camera UBO (absolute position at byte 208)
  VkDeviceSize ubo_off = 0;
  Bg3Binding global{};                 // (unused)
  std::vector<std::pair<Bg3Binding, uint32_t>> copies;   // luminance-scale copies owed at the pass end
};
std::unordered_map<VkCommandBuffer, PCmd> g_pending;
thread_local VkBuffer t_pass_soup = VK_NULL_HANDLE;   // the soup this thread's pass captured into
thread_local bool t_pass_fx = false;   // the pass being recorded on this thread drew an EMI material

// The emissive soup: a ring of 256-aligned ranges (a range lives about three frames:
// written by its pass, read by the build and by consumers of the next frame).
Buf g_soup;
VkDeviceSize g_soup_head = 0, g_soup_since = 0;   // bytes allocated since the last build
VkDeviceSize g_demand = 0;                        // bytes asked for since the last build (captured or not)
uint32_t g_regrown = 0;                           // draws dropped because the soup was replaced
// Soups replaced by a larger one, kept until every descriptor set that binds them (the
// consumers read emSoup a frame or more later) and every game command buffer recorded with
// them has been replaced: freed after kRing + kOwn further builds, then deferred as usual.
// Freed after a frame-count deferral alone, a set still current (no build for a few frames
// at a level load) read a destroyed buffer (Grymforge em7, device lost right after the
// "soup grown" line).
struct Retired { Buf b; uint32_t builds_left; };
std::vector<Retired> g_soup_retired;
uint32_t g_last_cluster = 1;                      // triangles per light of the latest build read back
uint32_t g_win_frames = 0;                        // presents in the log window
// Diagnostics: the latest record per fragment shader and the constants its fixup read
// (logged for luminance families and for exact families outside 0.8-1.25).
struct RecDbg { EmRec r{}; float m[6] = {}; float m45[2] = {}; float time = 0, pulse = -1; bool cb = false; };
std::map<uint64_t, RecDbg> g_rec_dbg;

// Per-family calibration against the game's effects target (rt_em_calib): the state is
// kept here, the rule (em::calib_first / calib_window / calib_apply) runs in the core.
using em::Calib;
std::unordered_map<uint64_t, Calib> g_calib;
// em::calib_first (first) or calib_window through the core; unchanged without a core.
bool calib_update(Calib& cal, bool first, double ratio, uint32_t flags, uint64_t frames, double bright) {
  Bg3EmCalib b{cal.scale, cal.updates, cal.log_u};
  const bool changed = em_core_calib_update(&b, first, ratio, flags, frames, bright);
  cal = Calib{b.scale, b.updates, b.log_u};
  return changed;
}
uint32_t g_stride = 24;

// ---- per build ----
VkDescriptorSetLayout g_dsl = VK_NULL_HANDLE, g_push_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_pl = VK_NULL_HANDLE;
VkPipeline g_pipe = VK_NULL_HANDLE;
uint64_t g_pipe_gen = 0;
VkDescriptorPool g_pool = VK_NULL_HANDLE;
VkDescriptorSet g_sets[kRing] = {};
bool g_set_init[kRing] = {};
uint32_t g_set_tex[kRing] = {}, g_set_cache[kRing] = {};
VkSampler g_sampler = VK_NULL_HANDLE;
Buf g_host;                 // kRing regions: records, prefix, header, statistics
Buf g_lights;               // EmLight[g_max_lights]
Buf g_dummy;
uint32_t g_max_lights = 0;
constexpr uint32_t kGlobSlots = 4096;   // luminance-scale copies (a ring, ~100 frames)
Buf g_globals;
uint32_t g_glob_next = 0;
std::atomic<uint32_t> g_copies_recorded{0}, g_copies_skipped{0};   // em_copy dispatches recorded / owed but not recorded
// Diagnostics: the latest copy's source (the game's buffer, offset with the dynamic offset
// applied, range); em_copy.hlsl also writes the source's first 4 words after the slots.
std::atomic<uint64_t> g_copy_src_off{0}, g_copy_src_range{0};
// em_copy.hlsl: push descriptors src (the game's buffer) + dst (g_globals).
VkDescriptorSetLayout g_cp_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_cp_pl = VK_NULL_HANDLE;
VkPipeline g_cp_pipe = VK_NULL_HANDLE;
uint64_t g_cp_gen = 0;
struct Img { VkImage image = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; };
Img g_dimg, g_darr;
bool g_dummy_ready = false;
uint32_t g_next_slot = 0;
int g_current = -1;
Bg3EmissiveInfo g_info{};
struct Own { VkCommandBuffer cb = VK_NULL_HANDLE; VkFence fence = VK_NULL_HANDLE; bool used = false; };
VkCommandPool g_cmd_pool = VK_NULL_HANDLE;
Own g_own[kOwn];
uint32_t g_own_next = 0;
struct SlotInfo {
  uint64_t frame = 0;                  // build frame + 1 (0: free)
  std::vector<uint64_t> rec_fs;        // fragment shader per record
  std::vector<uint32_t> rec_flags;     // its flags (kFlagApprox: calibrated)
  std::vector<uint32_t> rec_glob;      // its luminance-scale slot
  std::vector<uint64_t> handles;       // game views, images and buffers the set reads (lifetime)
  uint32_t lights = 0, overflow = 0, instances = 0, groups = 0, cluster = 1;
  Census census;
  uint64_t tris = 0;
};
SlotInfo g_slots[kRing];

// ---- timing ----
VkQueryPool g_qp = VK_NULL_HANDLE;
float g_ts_period = 1.0f;
std::mutex g_ts_mu;
struct TsPair { uint64_t frame = 0; uint8_t kind = 0; bool capture = false; };   // kind 0 effects pass, 1 build, 2 ignore
TsPair g_ts[kTsPairs];
uint32_t g_ts_next = 0;

// ---- window statistics (logged every rt_timing_frames builds) ----
struct Window {
  uint64_t builds = 0;
  Census c;
  double instances = 0, tris = 0, lights = 0, overflow = 0, power = 0;
  std::vector<float> build_ms;
  std::map<uint64_t, double> fx_frame_ms[2];   // effects passes per frame: [no capture, capture]
  uint64_t nonfinite = 0, collapsed = 0;
  struct Cmp { double ours = 0, game = 0, px = 0, zero = 0, nomap = 0, bright = 0, ours_b = 0, game_b = 0, overlap = 0; uint32_t flags = 0; };
  struct Glob { uint32_t slot = UINT32_MAX, n = 0, missing = 0; float value = 0, vmin = 1e30f, vmax = 0; };
  std::map<uint64_t, Glob> glob;               // luminance-scale copies per fragment shader (read back)
  std::map<uint64_t, Cmp> cmp;                 // projected comparison per fragment shader
  uint64_t dbg_frames = 0, dbg_depth = 0, dbg_hit = 0, dbg_miss = 0, dbg_dark = 0, dbg_bright = 0, dbg_front = 0;
  uint64_t overlap_probe[7] = {}; // unused global debug words8..14
  double last_power = 0;
  uint32_t last_draws = 0, last_lights = 0;
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
  if (dd->vk.BindBufferMemory(dd->device, out.b, out.m, 0) != VK_SUCCESS) {
    dd->vk.DestroyBuffer(dd->device, out.b, nullptr);
    dd->vk.FreeMemory(dd->device, out.m, nullptr);
    out = Buf{};
    return false;
  }
  out.size = size;
  if (host) {
    void* p = nullptr;
    dd->vk.MapMemory(dd->device, out.m, 0, VK_WHOLE_SIZE, 0, &p);
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

using em::finite;

const Mat& material(uint64_t fs) {
  auto it = g_mats.find(fs);
  if (it != g_mats.end()) return it->second;
  Mat m;
  const Bg3AssetDbApi* db = assetdb_api();
  if (db && db->available && db->available()) {
    if (db->material && db->material(fs, &m.info)) {
      m.name = m.info.template_name ? m.info.template_name : "?";
      const bool emi = m.info.pass == 2;   // the EMI variant: drawn into the effects target
      const uint32_t f = m.info.flags;
      const bool fwd_emissive = m.info.pass == 1 && (f & BG3_MAT_F_EMISSIVE) &&
                                !(f & (BG3_MAT_F_VFX | BG3_MAT_F_WATER | BG3_MAT_F_DECAL)) && m.info.cls != BG3_MAT_CLASS_DECAL;
      if (fwd_emissive) m.kind = kMatForward;
      else if (!emi) m.kind = kMatOther;
      else if (m.info.cls == BG3_MAT_CLASS_DECAL || (m.info.flags & BG3_MAT_F_DECAL)) m.kind = kMatDecal;
      else if (m.name.find("Skydome") != std::string::npos) m.kind = kMatBackdrop;
      else m.kind = kMatEmissive;
    } else if (db->engine_shader && db->engine_shader(fs)) {
      m.kind = kMatEngine;
      m.name = db->engine_shader(fs);
    }
  }
  return g_mats.emplace(fs, std::move(m)).first->second;
}

// The record of an emissive draw (caller holds core_shared_lock, then g_mu): the core runs
// em::make_record on the draw's bindings (Bg3CoreApi::em_record, ABI 24); the views it
// hands back and the luminance-copy slots stay here. False without a core.
bool make_record(VkCommandBuffer cmd, uint64_t fs, const Mat& mat, PCmd& P, PDraw& d) {
  const Bg3EmRecordIn in{cmd, fs, &mat.info, mat.name.c_str(), mat.kind == kMatForward, mat.kind == kMatDecal};
  Bg3EmRecordOut o;
  if (!em_core_record(&in, &o)) return false;
  static_assert(sizeof o.rec == sizeof d.rec, "Bg3EmRecordOut::rec holds an EmRec");
  std::memcpy(&d.rec, o.rec, sizeof d.rec);
  if (d.rec.flags & em::kFlagExcluded) return false;   // counted by the caller, not captured
  View* v[4] = {&d.mask, &d.base, &d.vt_ind, &d.vt_cache};   // em::EmView order
  for (int i = 0; i < 4; ++i) *v[i] = View{o.views[i].view, o.views[i].layout};
  RecDbg& g = g_rec_dbg[fs];
  std::memcpy(&g.r, o.dbg_rec, sizeof g.r);
  if (o.lum_wanted) {
    // One slot per distinct binding in the pass.
    uint32_t slot = UINT32_MAX;
    for (const auto& c : P.copies)
      if (c.first.buffer == o.luminance.buffer && c.first.offset == o.luminance.offset) slot = c.second;
    if (slot == UINT32_MAX) {
      slot = g_glob_next++ % kGlobSlots;
      P.copies.push_back({o.luminance, slot});
      if (P.copies.size() > 1) ++P.census.global_conflicts;   // (several bindings: counted, handled)
    }
    d.rec.glob = g.r.glob = slot;
  }
  P.census.cb_unreadable += o.cb_unreadable;
  P.census.vt_missing += o.vt_missing;
  d.pulse_no_time += o.pulse_no_time;
  if (o.dbg_m) {
    std::memcpy(g.m, o.m, sizeof g.m);
    g.cb = true;
    if (o.dbg_pulse) {
      std::memcpy(g.m45, o.m45, sizeof g.m45);
      g.time = o.time;
      g.pulse = o.pulse;
    }
  }
  P.census.pulse_no_time += d.pulse_no_time;
  return true;
}

bool ensure_pipeline() {
  Bg3ShaderBlob blob{};
  if (!shader_get("rt/em_lights.hlsl", "main", "cs_6_6", &blob)) return g_pipe != VK_NULL_HANDLE;
  if (g_pipe && blob.generation == g_pipe_gen) return true;
  DeviceData* dd = g_dd;
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
    log("[em] em_lights pipeline creation failed (%d)", r);
    return g_pipe != VK_NULL_HANDLE;
  }
  if (g_pipe) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_pipe));
  g_pipe = p;
  g_pipe_gen = blob.generation;
  log("[em] em_lights pipeline generation %llu", (unsigned long long)blob.generation);
  return true;
}

bool make_image(bool array, Img& out) {
  DeviceData* dd = g_dd;
  auto failed = [&]() {
    if (out.image) dd->vk.DestroyImage(dd->device, out.image, nullptr);
    if (out.mem) dd->vk.FreeMemory(dd->device, out.mem, nullptr);
    out = Img{};
    return false;
  };
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {1, 1, 1};
  ici.mipLevels = ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (dd->vk.CreateImage(dd->device, &ici, nullptr, &out.image) != VK_SUCCESS) return failed();
  VkMemoryRequirements req;
  dd->vk.GetImageMemoryRequirements(dd->device, out.image, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.mem) != VK_SUCCESS)
    return failed();
  if (dd->vk.BindImageMemory(dd->device, out.image, out.mem, 0) != VK_SUCCESS) return failed();
  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = out.image;
  vci.viewType = array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  vci.format = ici.format;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (dd->vk.CreateImageView(dd->device, &vci, nullptr, &out.view) != VK_SUCCESS) return failed();
  return true;
}

// The dummies are white (factor 1), filled once in the first build's command buffer.
void dummy_ready(VkCommandBuffer cmd) {
  if (g_dummy_ready) return;
  VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageMemoryBarrier ib[2];
  for (int i = 0; i < 2; ++i) {
    ib[i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ib[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ib[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ib[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ib[i].srcQueueFamilyIndex = ib[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib[i].image = i ? g_darr.image : g_dimg.image;
    ib[i].subresourceRange = range;
  }
  g_dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                              nullptr, 2, ib);
  VkClearColorValue one{{1, 1, 1, 1}};
  for (int i = 0; i < 2; ++i)
    g_dd->vk.CmdClearColorImage(cmd, ib[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &one, 1, &range);
  g_dd->vk.CmdFillBuffer(cmd, g_dummy.b, 0, g_dummy.size, 0);
  for (int i = 0; i < 2; ++i) {
    ib[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ib[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ib[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ib[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  g_dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kCS, 0, 1, &mb, 0, nullptr, 2, ib);
  g_dummy_ready = true;
}

uint32_t ts_pair(VkCommandBuffer cmd, uint8_t kind, bool capture) {
  if (!logging::stats_enabled(cfg_int) || !g_qp) return UINT32_MAX;
  std::lock_guard<std::mutex> lock(g_ts_mu);
  uint32_t p = g_ts_next % kTsPairs;
  if (g_ts[p].frame) return UINT32_MAX;   // not read back yet: skip this sample
  g_ts_next = p + 1;
  g_ts[p] = {frame_index() + 1, kind, capture};
  g_dd->vk.CmdResetQueryPool(cmd, g_qp, p * 2, 2);
  g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_qp, p * 2);
  return p;
}

void ts_end(VkCommandBuffer cmd, uint32_t p) {
  if (p != UINT32_MAX) g_dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qp, p * 2 + 1);
}

// ---- the emissive scene's acceleration structures (own builder) ----
// One BLAS per build with one geometry per draw (the soup's camera-relative world
// triangles, no index buffer), and a TLAS with that BLAS as its one identity instance;
// em_common.hlsli reads the geometry index as the "instance" of emInfo. Each ring slot
// keeps its BLAS and TLAS objects and reuses them while they are large enough: no
// acceleration-structure objects are created or destroyed per frame (the split's fault
// analysis ties device faults to run-time AS object destruction), only on growth. It
// shares nothing with the BVH split (bvhsplit.cpp), so rt_bvh_split and the split's
// changes do not affect it.
constexpr uint32_t kInfoEmissive = 1u << 11;   // rt_common.hlsli / em_common.hlsli
constexpr VkDeviceSize kInstOffTlas = 0;                       // the TLAS's one instance (64 B)
constexpr VkDeviceSize kInstOffFake = 256;                     // per geometry: identity instances for em_lights
constexpr VkDeviceSize kInstOffInfo = kInstOffFake + VkDeviceSize(kMaxRecs) * 64;   // per geometry: info (16 B)
constexpr VkDeviceSize kInstRegion = up256(kInstOffInfo + VkDeviceSize(kMaxRecs) * 16);
Buf g_inst;           // host-visible, kRing regions (TLAS build input, emInfo, em_lights instances)
Buf g_as_scratch;
struct AsSlot {
  Buf buf;            // BLAS at 0, TLAS at tlas_off
  VkAccelerationStructureKHR blas = VK_NULL_HANDLE, tlas = VK_NULL_HANDLE;
  VkDeviceSize blas_size = 0, tlas_off = 0, tlas_size = 0;
  VkDeviceAddress blas_addr = 0;
  // The last BLAS build of this slot: its geometries' keys (a refit needs the same meshes in
  // the same order with the same triangle counts) and refits since its full build.
  std::vector<uint64_t> sig;
  uint32_t refits = 0;
};
AsSlot g_as[kRing];
uint64_t g_as_refits = 0, g_as_builds = 0;
uint64_t g_as_grown = 0;

void as_slot_release(AsSlot& a) {
  if (a.blas) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(a.blas));
  if (a.tlas) defer_destroy(BG3_DEFER_ACCELERATION_STRUCTURE, uint64_t(a.tlas));
  free_buf(a.buf);
  a = AsSlot{};
}

VkAccelerationStructureKHR as_make(VkBuffer b, VkDeviceSize off, VkDeviceSize size, VkAccelerationStructureTypeKHR type) {
  VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  ci.buffer = b;
  ci.offset = off;
  ci.size = size;
  ci.type = type;
  VkAccelerationStructureKHR as = VK_NULL_HANDLE;
  if (g_dd->vk.CreateAccelerationStructureKHR(g_dd->device, &ci, nullptr, &as) != VK_SUCCESS) return VK_NULL_HANDLE;
  return as;
}

// Records the builds of ring slot `slot` from `ranges` (soup offset, triangles; sanitized)
// into `cmd`; the TLAS, or VK_NULL_HANDLE (nothing recorded).
// A slot whose last build had the same geometries (keys) is refit in place instead of rebuilt
// (rt_em_refit, default 1): the same meshes, only their camera-relative positions moved (and
// skinned or animated vertices). Every rt_em_rebuild-th use of the slot rebuilds, so a BVH
// never keeps a stale topology for long. Our command buffers are fenced and always submitted,
// so the source of a refit has always been built (the hazard of the game's dropped buffers,
// TRACK_D 13.3, does not apply).
VkAccelerationStructureKHR em_as_build(VkCommandBuffer cmd, uint32_t slot,
                                       const std::vector<std::pair<VkDeviceSize, uint32_t>>& ranges,
                                       const std::vector<uint64_t>& keys) {
  DeviceData* dd = g_dd;
  AsSlot& a = g_as[slot];
  const uint32_t ngeo = uint32_t(ranges.size());
  const VkDeviceAddress soup = rt_buffer_address(g_soup.b);
  const VkDeviceAddress inst = rt_buffer_address(g_inst.b) + VkDeviceSize(slot) * kInstRegion;
  if (!soup || !inst) return VK_NULL_HANDLE;
  std::vector<VkAccelerationStructureGeometryKHR> geo(ngeo);
  std::vector<VkAccelerationStructureBuildRangeInfoKHR> rng(ngeo);
  std::vector<uint32_t> counts(ngeo);
  for (uint32_t i = 0; i < ngeo; ++i) {
    VkAccelerationStructureGeometryKHR& g = geo[i];
    g = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto& t = g.geometry.triangles;
    t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    t.vertexData.deviceAddress = soup + ranges[i].first;
    t.vertexStride = g_stride;
    t.maxVertex = ranges[i].second * 3 - 1;
    t.indexType = VK_INDEX_TYPE_NONE_KHR;
    counts[i] = ranges[i].second;
    rng[i] = {ranges[i].second, 0, 0, 0};
  }
  VkAccelerationStructureBuildGeometryInfoKHR bb{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bb.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  bb.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
  bb.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  bb.geometryCount = ngeo;
  bb.pGeometries = geo.data();
  VkAccelerationStructureBuildSizesInfoKHR bs{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  if (ngeo)
    dd->vk.GetAccelerationStructureBuildSizesKHR(dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bb,
                                                 counts.data(), &bs);

  VkAccelerationStructureGeometryKHR tg{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  tg.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  tg.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  tg.geometry.instances.data.deviceAddress = inst + kInstOffTlas;
  VkAccelerationStructureBuildGeometryInfoKHR tb{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  tb.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  tb.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  tb.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  tb.geometryCount = 1;
  tb.pGeometries = &tg;
  const uint32_t one = 1;
  VkAccelerationStructureBuildSizesInfoKHR ts{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  dd->vk.GetAccelerationStructureBuildSizesKHR(dd->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tb, &one, &ts);

  // The slot's storage: grown (to 1.5x the need) only when this build does not fit.
  const VkDeviceSize need_blas = up256(bs.accelerationStructureSize);
  if (!a.buf.b || need_blas > a.blas_size || ts.accelerationStructureSize > a.tlas_size) {
    a.sig.clear();
    const VkDeviceSize blas_cap = up256((std::max)(need_blas + need_blas / 2, VkDeviceSize(1) << 20));
    const VkDeviceSize tlas_cap = up256((std::max)(ts.accelerationStructureSize, VkDeviceSize(4096)));
    as_slot_release(a);
    if (!make_buf(a.buf, blas_cap + tlas_cap,
                  VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false))
      return VK_NULL_HANDLE;
    a.blas_size = blas_cap;
    a.tlas_off = blas_cap;
    a.tlas_size = tlas_cap;
    a.blas = as_make(a.buf.b, 0, blas_cap, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);
    a.tlas = as_make(a.buf.b, a.tlas_off, tlas_cap, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);
    if (!a.blas || !a.tlas) {
      as_slot_release(a);
      return VK_NULL_HANDLE;
    }
    VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    ai.accelerationStructure = a.blas;
    a.blas_addr = dd->vk.GetAccelerationStructureDeviceAddressKHR(dd->device, &ai);
    ++g_as_grown;
  }
  // Scratch: shared by every slot (our command buffers run in submission order on one
  // queue and each begins with a full barrier).
  const VkDeviceSize al = (std::max)(rt_scratch_alignment(), VkDeviceSize(256));
  const VkDeviceSize need_scratch = (std::max)({bs.buildScratchSize, bs.updateScratchSize, ts.buildScratchSize}) + al;
  if (g_as_scratch.size < need_scratch) {
    free_buf(g_as_scratch);
    if (!make_buf(g_as_scratch, up256(need_scratch + need_scratch / 2),
                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false))
      return VK_NULL_HANDLE;
  }
  const VkDeviceAddress sbase = rt_buffer_address(g_as_scratch.b);
  const VkDeviceAddress scratch = (sbase + al - 1) / al * al;

  // The TLAS's one instance: the BLAS, identity, every ray mask (none when empty).
  VkAccelerationStructureInstanceKHR ti{};
  ti.transform.matrix[0][0] = ti.transform.matrix[1][1] = ti.transform.matrix[2][2] = 1.0f;
  ti.mask = ngeo ? 0xFF : 0x00;
  ti.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  ti.accelerationStructureReference = a.blas_addr;
  std::memcpy(g_inst.map + VkDeviceSize(slot) * kInstRegion + kInstOffTlas, &ti, sizeof ti);

  const int rebuild_every = (std::max)(cfg_int("rt_em_rebuild", 30), 1);
  const bool refit = ngeo && cfg_int("rt_em_refit", 1) != 0 && a.sig == keys && a.refits + 1 < uint32_t(rebuild_every);
  a.refits = refit ? a.refits + 1 : 0;
  a.sig = keys;
  ++(refit ? g_as_refits : g_as_builds);
  if (ngeo) {
    bb.dstAccelerationStructure = a.blas;
    bb.scratchData.deviceAddress = scratch;
    if (refit) {
      bb.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
      bb.srcAccelerationStructure = a.blas;
    }
    const VkAccelerationStructureBuildRangeInfoKHR* pr = rng.data();
    dd->vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &bb, &pr);
    barrier(cmd, kAS, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, kAS,
            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
  }
  // No geometry: the TLAS gets zero instances (an empty scene, every ray misses).
  tb.dstAccelerationStructure = a.tlas;
  tb.scratchData.deviceAddress = scratch;
  VkAccelerationStructureBuildRangeInfoKHR tr{ngeo ? 1u : 0u, 0, 0, 0};
  const VkAccelerationStructureBuildRangeInfoKHR* ptr = &tr;
  dd->vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &tb, &ptr);
  barrier(cmd, kAS, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, kCS | kAS,
          VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT);
  return a.tlas;
}

// The sanitize pipeline (shaders/rt/em_sanitize.hlsl): push descriptors soup + header.
VkDescriptorSetLayout g_san_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_san_pl = VK_NULL_HANDLE;
VkPipeline g_san_pipe = VK_NULL_HANDLE;
uint64_t g_san_gen = 0;
bool ensure_sanitize() {
  DeviceData* dd = g_dd;
  if (!g_san_dsl) {
    VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                         {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    ci.bindingCount = 2;
    ci.pBindings = b;
    if (dd->vk.CreateDescriptorSetLayout(dd->device, &ci, nullptr, &g_san_dsl) != VK_SUCCESS) return false;
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &g_san_dsl;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pcr;
    if (dd->vk.CreatePipelineLayout(dd->device, &li, nullptr, &g_san_pl) != VK_SUCCESS) return false;
  }
  Bg3ShaderBlob blob{};
  if (!shader_get("rt/em_sanitize.hlsl", "main", "cs_6_6", &blob)) return g_san_pipe != VK_NULL_HANDLE;
  if (g_san_pipe && blob.generation == g_san_gen) return true;
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  VkShaderModule mod = VK_NULL_HANDLE;
  if (dd->vk.CreateShaderModule(dd->device, &mci, nullptr, &mod) != VK_SUCCESS) return g_san_pipe != VK_NULL_HANDLE;
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main"};
  pci.layout = g_san_pl;
  VkPipeline p = VK_NULL_HANDLE;
  VkResult r = dd->vk.CreateComputePipelines(dd->device, VK_NULL_HANDLE, 1, &pci, nullptr, &p);
  dd->vk.DestroyShaderModule(dd->device, mod, nullptr);
  if (r != VK_SUCCESS) return g_san_pipe != VK_NULL_HANDLE;
  if (g_san_pipe) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_san_pipe));
  g_san_pipe = p;
  g_san_gen = blob.generation;
  return true;
}
bool ensure_copy() {
  DeviceData* dd = g_dd;
  if (!g_cp_dsl) {
    VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                         {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    ci.bindingCount = 2;
    ci.pBindings = b;
    if (dd->vk.CreateDescriptorSetLayout(dd->device, &ci, nullptr, &g_cp_dsl) != VK_SUCCESS) return false;
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &g_cp_dsl;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pcr;
    if (dd->vk.CreatePipelineLayout(dd->device, &li, nullptr, &g_cp_pl) != VK_SUCCESS) return false;
  }
  Bg3ShaderBlob blob{};
  if (!shader_get("rt/em_copy.hlsl", "main", "cs_6_6", &blob)) return g_cp_pipe != VK_NULL_HANDLE;
  if (g_cp_pipe && blob.generation == g_cp_gen) return true;
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  VkShaderModule mod = VK_NULL_HANDLE;
  if (dd->vk.CreateShaderModule(dd->device, &mci, nullptr, &mod) != VK_SUCCESS) return g_cp_pipe != VK_NULL_HANDLE;
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main"};
  pci.layout = g_cp_pl;
  VkPipeline p = VK_NULL_HANDLE;
  VkResult r = dd->vk.CreateComputePipelines(dd->device, VK_NULL_HANDLE, 1, &pci, nullptr, &p);
  dd->vk.DestroyShaderModule(dd->device, mod, nullptr);
  if (r != VK_SUCCESS) return g_cp_pipe != VK_NULL_HANDLE;
  if (g_cp_pipe) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_cp_pipe));
  g_cp_pipe = p;
  g_cp_gen = blob.generation;
  return true;
}

void log_once_sanitize() {
  static bool done = false;
  if (!done) done = true, log("[em] WARNING the sanitize pipeline is unavailable: emissive builds are skipped");
}

uint32_t slot_of(std::vector<View>& list, uint32_t cap, const View& v) {
  if (!v.v) return 0;
  for (size_t i = 0; i < list.size(); ++i)
    if (list[i].v == v.v) return uint32_t(i + 1);
  if (list.size() + 1 >= cap) return 0;
  list.push_back(v);
  return uint32_t(list.size());
}

// One frame's draws (the command buffers of one game submission) into the emissive scene,
// recorded into our own command buffer `cmd` (caller holds g_mu).
void build(VkCommandBuffer cmd, std::vector<PCmd>& taken) {
  const uint64_t now = frame_index();
  const DeviceData* dd = g_dd;
  if (!ensure_pipeline()) return;
  const uint32_t tsp = ts_pair(cmd, 1, true);
  dummy_ready(cmd);
  Census census;
  std::vector<EmRec> recs;
  std::vector<View> tex, cache;
  std::vector<std::pair<VkDeviceSize, uint32_t>> ranges;   // soup ranges the BLAS builds read
  std::vector<uint64_t> keys;                                // per range: which mesh it is (the refit's signature)
  Bg3Binding global{};
  float cam[3] = {0, 0, 0};
  bool have_cam = false;
  // The scene's draws: one geometry each of the build's single BLAS (em_as_build).
  for (PCmd& P : taken) {
    census.add(P.census);
    if (!global.buffer && P.global.buffer) global = P.global;
    if (!have_cam && P.ubo && read_mapped(P.ubo, P.ubo_off + 208, cam, 12) && finite(cam, 3)) have_cam = true;
    for (PDraw& d : P.draws) {
      if (recs.size() >= kMaxRecs) {
        ++census.skip_recs;
        continue;
      }
      if (d.soup != g_soup.b) {   // captured into a soup replaced since (it grew)
        ++g_regrown;
        continue;
      }
      EmRec r = d.rec;
      r.draw = uint32_t(recs.size());
      if (r.flags & kFlagApprox) {
        auto cal = g_calib.find((uint64_t(r.fs_hi) << 32) | r.fs_lo);
        Bg3EmCalib b{};
        if (cal != g_calib.end()) b = Bg3EmCalib{cal->second.scale, cal->second.updates, cal->second.log_u};
        uint32_t words[64];
        static_assert(sizeof words == sizeof r, "EmRec is 64 words");
        std::memcpy(words, &r, sizeof r);
        if (em_core_calib_apply(words, cal != g_calib.end() ? &b : nullptr)) std::memcpy(&r, words, sizeof r);
      }
      r.tex = slot_of(tex, kMaxTex, d.mask);
      if (d.mask.v && !r.tex) ++census.skip_tex, r.mode = kModeConst, r.flags |= kFlagApprox;
      r.tex2 = slot_of(tex, kMaxTex, d.base);
      if (d.base.v && !r.tex2) ++census.skip_tex, r.flags = (r.flags & ~kFlagBaseMap) | kFlagApprox;
      r.vt_tex = slot_of(tex, kMaxTex, d.vt_ind);
      r.vt_cache = slot_of(cache, kMaxCache, d.vt_cache);
      if ((r.flags & kFlagVtAlbedo) && (!r.vt_tex || !r.vt_cache))
        ++census.skip_tex, r.flags = (r.flags & ~kFlagVtAlbedo) | kFlagApprox;
      recs.push_back(r);
      ranges.push_back({d.off, d.prims});
      uint64_t key = 1469598103934665603ull;
      // Not the vertex/index buffer offsets: the game streams dynamic and instanced geometry
      // through per-frame ring offsets, so the same mesh moves between frames (em11: 12% refits
      // in AF, 30% in GF, 95% in WQ). A refit of a different mesh with the same counts stays
      // correct (only its BVH is worse) and the rt_em_rebuild cadence bounds that.
      for (uint64_t v : {uint64_t(r.fs_hi) << 32 | r.fs_lo, (uint64_t)d.ib, uint64_t(d.prims), uint64_t(d.args.count),
                         uint64_t(d.args.instances), uint64_t(d.args.kind)})
        key = (key ^ v) * 1099511628211ull;
      keys.push_back(key);
    }
  }
  // This build's host region (records, header, statistics); the header is cleared before
  // the GPU writes into it (sanitize counts, the global scale's copy).
  const uint32_t slot = g_next_slot++ % kRing;
  uint8_t* region = g_host.map + VkDeviceSize(slot) * kRegion;
  const VkDeviceSize rbase = VkDeviceSize(slot) * kRegion;
  uint32_t* header = reinterpret_cast<uint32_t*>(region + kOffHeader);
  std::memset(header, 0, size_t(kHeaderWords) * 4);
  std::memset(region + kOffStats, 0, size_t(kStatWords) * 4);
  // Sanitize every range the BLAS builds will read: only finite, bounded, converted
  // vertices reach an acceleration structure build (em_sanitize.hlsl).
  if (!ranges.empty() && ensure_sanitize()) {
    VkDescriptorBufferInfo soupb{g_soup.b, 0, g_soup.size}, headb{g_host.b, rbase + kOffHeader, VkDeviceSize(kHeaderWords) * 4};
    VkWriteDescriptorSet sw[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    for (int i = 0; i < 2; ++i) {
      sw[i].dstBinding = uint32_t(i);
      sw[i].descriptorCount = 1;
      sw[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      sw[i].pBufferInfo = i ? &headb : &soupb;
    }
    dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_san_pipe);
    dd->vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_san_pl, 0, 2, sw);
    for (const auto& [off, tris] : ranges) {
      uint32_t pc[4] = {uint32_t(off), tris, g_stride, 0};
      dd->vk.CmdPushConstants(cmd, g_san_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
      dd->vk.CmdDispatch(cmd, (tris + 255) / 256, 1, 1);
    }
    barrier(cmd, kCS, VK_ACCESS_SHADER_WRITE_BIT, kCS | kAS, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
  } else if (!ranges.empty()) {
    ts_end(cmd, tsp);   // never build from unchecked data
    log_once_sanitize();
    return;
  }
  // Info (16 B per geometry: soup offset, stride | flags, record, triangles) and identity
  // instances (64 B per geometry, for em_lights) in this slot's region of g_inst.
  const uint32_t ngeo = uint32_t(ranges.size());
  uint8_t* ireg = g_inst.map + VkDeviceSize(slot) * kInstRegion;
  uint32_t* info_host = reinterpret_cast<uint32_t*>(ireg + kInstOffInfo);
  for (uint32_t i = 0; i < ngeo; ++i) {
    info_host[i * 4 + 0] = uint32_t(ranges[i].first);
    info_host[i * 4 + 1] = (g_stride & 0xffu) | kInfoEmissive;
    info_host[i * 4 + 2] = i;
    info_host[i * 4 + 3] = ranges[i].second;
    VkAccelerationStructureInstanceKHR in{};
    in.transform.matrix[0][0] = in.transform.matrix[1][1] = in.transform.matrix[2][2] = 1.0f;
    in.mask = 0xFF;
    std::memcpy(ireg + kInstOffFake + VkDeviceSize(i) * 64, &in, 64);
  }
  VkAccelerationStructureKHR tlas = em_as_build(cmd, slot, ranges, keys);
  if (!tlas) {
    ts_end(cmd, tsp);
    return;
  }
  const uint32_t nrec = uint32_t(recs.size());
  if (nrec) std::memcpy(region + kOffRecs, recs.data(), size_t(nrec) * sizeof(EmRec));
  // Light ranges per instance, bounded by rt_em_max_lights (allocated at device creation):
  // past it, k consecutive triangles of an instance share one light (their summed area and
  // power, area-weighted radiance, the largest as the shape), so no emission is dropped.
  const uint32_t n = ngeo;
  uint32_t* prefix = reinterpret_cast<uint32_t*>(region + kOffPrefix);
  uint32_t total = 0, overflow = 0;
  const uint32_t cap_lights = (std::max)((std::min)(uint32_t((std::max)(cfg_int("rt_em_max_lights", 65536), 0)), g_max_lights), 1u);
  uint64_t all_tris = 0;
  for (uint32_t i = 0; i < n && i < kMaxInst; ++i) all_tris += info_host[i * 4 + 3];
  uint32_t k = uint32_t((std::max)((all_tris + cap_lights - 1) / cap_lights, uint64_t(1)));
  for (;;) {   // per-instance rounding may still overshoot: widen the clusters until it fits
    uint64_t need = 0;
    for (uint32_t i = 0; i < n && i < kMaxInst; ++i) need += (info_host[i * 4 + 3] + k - 1) / k;
    if (need <= cap_lights || k > 4096) break;
    ++k;
  }
  for (uint32_t i = 0; i < n && i < kMaxInst; ++i) {
    uint32_t tris = info_host[i * 4 + 3];
    uint32_t take = (std::min)((tris + k - 1) / k, cap_lights - total);
    prefix[i * 2] = total;
    prefix[i * 2 + 1] = take;
    total += take;
    overflow += tris - (std::min)(take * k, tris);
  }
  for (uint32_t i = kMaxInst; i < n; ++i) overflow += info_host[i * 4 + 3];
  const uint32_t ninst = (std::min)(n, kMaxInst);
  const float pre = rrpost_pre_exposure();
  header[0] = total;
  header[1] = overflow;
  header[2] = n;
  header[3] = nrec;
  std::memcpy(&header[4], &pre, 4);
  header[5] = uint32_t(now);
  header[6] = global.buffer ? 1u : 0u;
  header[7] = k;   // triangles per light
  std::memcpy(&header[8], cam, 12);

  // The set.
  VkDescriptorSet set = g_sets[slot];
  VkWriteDescriptorSetAccelerationStructureKHR wa{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
  wa.accelerationStructureCount = 1;
  wa.pAccelerationStructures = &tlas;
  VkDescriptorBufferInfo info{g_inst.b, VkDeviceSize(slot) * kInstRegion + kInstOffInfo, VkDeviceSize(kMaxRecs) * 16};
  VkDescriptorBufferInfo pool{g_dummy.b, 0, 256};   // (no pool: every vertex is in the soup)
  VkDescriptorBufferInfo soup{g_soup.b, 0, g_soup.size};
  VkDescriptorBufferInfo recb{g_host.b, rbase + kOffRecs, VkDeviceSize(kMaxRecs) * kRecBytes};
  VkDescriptorBufferInfo lights{g_lights.b, 0, g_lights.size};
  VkDescriptorBufferInfo stats{g_host.b, rbase + kOffStats, VkDeviceSize(kStatWords) * 4};
  VkDescriptorBufferInfo head{g_host.b, rbase + kOffHeader, VkDeviceSize(kHeaderWords) * 4};
  VkDescriptorBufferInfo glob{g_globals.b, 0, g_globals.size};
  const uint32_t ntex = g_set_init[slot] ? (std::max)(g_set_tex[slot], uint32_t(tex.size())) + 1 : kMaxTex;
  const uint32_t ncache = g_set_init[slot] ? (std::max)(g_set_cache[slot], uint32_t(cache.size())) + 1 : kMaxCache;
  g_set_init[slot] = true;
  g_set_tex[slot] = uint32_t(tex.size());
  g_set_cache[slot] = uint32_t(cache.size());
  std::vector<VkDescriptorImageInfo> ti((std::min)(ntex, kMaxTex)), ci((std::min)(ncache, kMaxCache));
  for (uint32_t i = 0; i < ti.size(); ++i)
    ti[i] = i && i <= tex.size() ? VkDescriptorImageInfo{VK_NULL_HANDLE, tex[i - 1].v, tex[i - 1].l}
                                 : VkDescriptorImageInfo{VK_NULL_HANDLE, g_dimg.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  for (uint32_t i = 0; i < ci.size(); ++i)
    ci[i] = i && i <= cache.size() ? VkDescriptorImageInfo{VK_NULL_HANDLE, cache[i - 1].v, cache[i - 1].l}
                                   : VkDescriptorImageInfo{VK_NULL_HANDLE, g_darr.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet w[11];
  const VkDescriptorBufferInfo* bufs[11] = {nullptr, &info, &pool, &soup, &recb, &lights, nullptr, &stats, &head, &glob, nullptr};
  const uint32_t bindings[11] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11};
  for (int i = 0; i < 11; ++i) {
    w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w[i].dstSet = set;
    w[i].dstBinding = bindings[i];
    w[i].descriptorCount = 1;
    w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[i].pBufferInfo = bufs[i];
  }
  w[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  w[0].pNext = &wa;
  w[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  w[6].descriptorCount = uint32_t(ti.size());
  w[6].pImageInfo = ti.data();
  w[10].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  w[10].descriptorCount = uint32_t(ci.size());
  w[10].pImageInfo = ci.data();
  dd->vk.UpdateDescriptorSets(dd->device, 11, w, 0, nullptr);

  // The light list (bvh_build ends with AS build -> compute).
  barrier(cmd, kCS, VK_ACCESS_SHADER_READ_BIT, kCS, VK_ACCESS_SHADER_WRITE_BIT);
  if (total && cfg_int("rt_em_lights", 1)) {
    VkDescriptorBufferInfo instb{g_inst.b, VkDeviceSize(slot) * kInstRegion + kInstOffFake, VkDeviceSize(kMaxRecs) * 64};
    VkDescriptorBufferInfo prefb{g_host.b, rbase + kOffPrefix, VkDeviceSize((std::max)(ninst, 1u)) * 8};
    VkWriteDescriptorSet pw[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    pw[0].dstBinding = 0;
    pw[0].descriptorCount = 1;
    pw[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pw[0].pBufferInfo = &instb;
    pw[1] = pw[0];
    pw[1].dstBinding = 1;
    pw[1].pBufferInfo = &prefb;
    dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe);
    dd->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl, 0, 1, &set, 0, nullptr);
    dd->vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl, 1, 2, pw);
    uint32_t pc[4] = {total, ninst, k, 0};
    dd->vk.CmdPushConstants(cmd, g_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    dd->vk.CmdDispatch(cmd, (total + 255) / 256, 1, 1);
  }
  barrier(cmd, kCS | kAS, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
          kCS | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
  ts_end(cmd, tsp);

  SlotInfo& si = g_slots[slot];
  si.frame = now + 1;
  si.handles.clear();
  {
    std::shared_lock<std::shared_mutex> tl(tracker().mu);
    auto note = [&](VkImageView v) {
      if (!v) return;
      si.handles.push_back(uint64_t(v));
      auto it = tracker().views.find(v);
      if (it != tracker().views.end()) si.handles.push_back(uint64_t(it->second.image));
    };
    for (const View& v : tex) note(v.v);
    for (const View& v : cache) note(v.v);
    note(g_fx.view);
    if (g_fx.image) si.handles.push_back(uint64_t(g_fx.image));
  }
  (void)global;
  si.rec_fs.resize(nrec);
  si.rec_flags.resize(nrec);
  si.rec_glob.resize(nrec);
  for (uint32_t i = 0; i < nrec; ++i) {
    si.rec_glob[i] = recs[i].glob;
    si.rec_fs[i] = (uint64_t(recs[i].fs_hi) << 32) | recs[i].fs_lo;
    si.rec_flags[i] = recs[i].flags;
  }
  si.lights = total;
  si.cluster = k;
  si.overflow = overflow;
  si.instances = n;
  si.groups = (std::min)((total + 255) / 256, 1024u);
  si.census = census;
  si.tris = 0;
  for (const auto& r : ranges) si.tris += r.second;
  g_current = int(slot);
  g_info = Bg3EmissiveInfo{now, n, nrec, total, pre, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, 0, 0};
  if (cfg_int("rt_em_log", 0))
    log("[em] build frame %llu: %u effects passes, %u draws (%u emissive, %u captured), %u records, %u textures, %u "
        "caches, %.0f triangles, %u lights (%u left out), global scale %s",
        (unsigned long long)now, census.passes, census.draws, census.kinds[kMatEmissive], census.captured, nrec,
        unsigned(tex.size()), unsigned(cache.size()), double(si.tris), total, overflow, global.buffer ? "bound" : "none");
}

}  // namespace

// rt_vram_guard breakdown: the soup buffer (present thread, read only).
uint64_t em_vram_bytes() { return g_soup.size; }

// ================= entry points =================

void em_device_created(DeviceData* dd) {
  if (g_dd || !cfg_hot(CfgHot::rt_emissive)) return;
  g_dd = dd;
  const VkDeviceFuncs& vk = dd->vk;
  VkPhysicalDeviceProperties props{};
  if (dd->inst && dd->inst->GetPhysicalDeviceProperties) dd->inst->GetPhysicalDeviceProperties(dd->physical, &props);
  g_ts_period = props.limits.timestampPeriod > 0 ? props.limits.timestampPeriod : 1.0f;
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = si.minFilter = VK_FILTER_LINEAR;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  si.maxLod = 16.0f;
  bool ok = vk.CreateSampler(dd->device, &si, nullptr, &g_sampler) == VK_SUCCESS;
  // gi_rt_pipeline: the raygen GI trace binds the emissive scene as its set 2 (layer/rt_pipeline.cpp).
  const VkShaderStageFlags stages = rt_pipeline::shared_stages();
  VkDescriptorSetLayoutBinding b[12] = {
      {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, stages, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr},
      {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr},
      {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr},
      {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr},
      {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr},
      {6, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTex, stages, nullptr},
      {7, VK_DESCRIPTOR_TYPE_SAMPLER, 1, stages, &g_sampler},
      {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr},
      {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr},
      {10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr},
      {11, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxCache, stages, nullptr}};
  VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dci.bindingCount = 12;
  dci.pBindings = b;
  ok = ok && vk.CreateDescriptorSetLayout(dd->device, &dci, nullptr, &g_dsl) == VK_SUCCESS;
  VkDescriptorSetLayoutBinding pb[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
  VkDescriptorSetLayoutCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  pci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
  pci.bindingCount = 2;
  pci.pBindings = pb;
  ok = ok && vk.CreateDescriptorSetLayout(dd->device, &pci, nullptr, &g_push_dsl) == VK_SUCCESS;
  VkDescriptorSetLayout sl[2] = {g_dsl, g_push_dsl};
  VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
  VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lci.setLayoutCount = 2;
  lci.pSetLayouts = sl;
  lci.pushConstantRangeCount = 1;
  lci.pPushConstantRanges = &pcr;
  ok = ok && vk.CreatePipelineLayout(dd->device, &lci, nullptr, &g_pl) == VK_SUCCESS;
  VkDescriptorPoolSize ps[4] = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, kRing},
                                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 9 * kRing},
                                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (kMaxTex + kMaxCache) * kRing},
                                {VK_DESCRIPTOR_TYPE_SAMPLER, kRing}};
  VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpi.maxSets = kRing;
  dpi.poolSizeCount = 4;
  dpi.pPoolSizes = ps;
  ok = ok && vk.CreateDescriptorPool(dd->device, &dpi, nullptr, &g_pool) == VK_SUCCESS;
  VkDescriptorSetLayout layouts[kRing];
  for (auto& l : layouts) l = g_dsl;
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = g_pool;
  ai.descriptorSetCount = kRing;
  ai.pSetLayouts = layouts;
  ok = ok && vk.AllocateDescriptorSets(dd->device, &ai, g_sets) == VK_SUCCESS;
  const VkDeviceSize soup_mb = VkDeviceSize((std::max)(cfg_int("rt_em_xfb_mb", 32), 4));
  ok = ok && make_buf(g_soup, soup_mb << 20,
                      VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | rt_xfb_usage(),
                      false);
  g_max_lights = uint32_t((std::min)((std::max)(cfg_int("rt_em_max_lights", 65536), 256), 1 << 20));
  ok = ok && make_buf(g_lights, VkDeviceSize(g_max_lights) * 80, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
  ok = ok && make_buf(g_host, kRegion * kRing, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true) && g_host.map;
  ok = ok && make_buf(g_dummy, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
  // Host-visible: the log reads back what the copies wrote (a few KB).
  ok = ok && make_buf(g_inst, kInstRegion * kRing,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                      true) && g_inst.map;
  ok = ok && make_buf(g_globals, VkDeviceSize(kGlobSlots + 4) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true) && g_globals.map;
  ok = ok && make_image(false, g_dimg) && make_image(true, g_darr);
  // Our own command buffers (one build per game submission of effects passes).
  VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cpi.queueFamilyIndex = dd->graphics_family;
  ok = ok && dd->graphics_family != UINT32_MAX && vk.CreateCommandPool(dd->device, &cpi, nullptr, &g_cmd_pool) == VK_SUCCESS;
  for (uint32_t i = 0; ok && i < kOwn; ++i) {
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = g_cmd_pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    ok = vk.AllocateCommandBuffers(dd->device, &cai, &g_own[i].cb) == VK_SUCCESS;
    if (ok && dd->set_loader_data) dd->set_loader_data(dd->device, g_own[i].cb);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    ok = ok && vk.CreateFence(dd->device, &fci, nullptr, &g_own[i].fence) == VK_SUCCESS;
  }
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qi.queryCount = kTsPairs * 2;
  if (ok && vk.CreateQueryPool(dd->device, &qi, nullptr, &g_qp) != VK_SUCCESS) g_qp = VK_NULL_HANDLE;
  g_on = ok;
  log("[em] emissive capture %s (rt_emissive=%d): soup %llu MB, light list %u triangles (%llu MB), %u KB host ring",
      ok ? "available" : "UNAVAILABLE", cfg_hot(CfgHot::rt_emissive), (unsigned long long)soup_mb, g_max_lights,
      (unsigned long long)((VkDeviceSize(g_max_lights) * 80) >> 20), unsigned((kRegion * kRing) >> 10));
}

void em_device_destroyed(DeviceData* dd) {
  if (dd != g_dd) return;
  std::lock_guard<std::mutex> lock(g_mu);
  const VkDeviceFuncs& vk = dd->vk;
  for (Own& o : g_own)
    if (o.fence) {
      if (o.used) fault_on_result(dd, vk.WaitForFences(dd->device, 1, &o.fence, VK_TRUE, 1000000000ull),
                                  "emissive teardown fence wait");
      vk.DestroyFence(dd->device, o.fence, nullptr);
      o = Own{};
    }
  if (g_cmd_pool) vk.DestroyCommandPool(dd->device, g_cmd_pool, nullptr);
  g_cmd_pool = VK_NULL_HANDLE;
  for (Buf* b : {&g_soup, &g_lights, &g_host, &g_dummy, &g_globals, &g_inst, &g_as_scratch}) free_buf(*b);
  for (AsSlot& a : g_as) as_slot_release(a);
  for (Retired& r : g_soup_retired) free_buf(r.b);
  g_soup_retired.clear();
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_cp_pipe));
  defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, uint64_t(g_cp_pl));
  defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, uint64_t(g_cp_dsl));
  g_cp_pipe = VK_NULL_HANDLE;
  g_cp_pl = VK_NULL_HANDLE;
  g_cp_dsl = VK_NULL_HANDLE;
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_pipe));
  defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, uint64_t(g_pl));
  defer_destroy(BG3_DEFER_DESCRIPTOR_POOL, uint64_t(g_pool));
  defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, uint64_t(g_dsl));
  defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, uint64_t(g_push_dsl));
  defer_destroy(BG3_DEFER_SAMPLER, uint64_t(g_sampler));
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_san_pipe));
  defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, uint64_t(g_san_pl));
  defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, uint64_t(g_san_dsl));
  g_san_pipe = VK_NULL_HANDLE;
  g_san_pl = VK_NULL_HANDLE;
  g_san_dsl = VK_NULL_HANDLE;
  for (Img* i : {&g_dimg, &g_darr}) {
    defer_destroy(BG3_DEFER_IMAGE_VIEW, uint64_t(i->view));
    defer_destroy(BG3_DEFER_IMAGE, uint64_t(i->image));
    defer_destroy(BG3_DEFER_MEMORY, uint64_t(i->mem));
    *i = Img{};
  }
  if (g_qp) defer_destroy(BG3_DEFER_QUERY_POOL, uint64_t(g_qp));
  g_pipe = VK_NULL_HANDLE;
  g_pl = VK_NULL_HANDLE;
  g_pool = VK_NULL_HANDLE;
  g_dsl = g_push_dsl = VK_NULL_HANDLE;
  g_sampler = VK_NULL_HANDLE;
  g_qp = VK_NULL_HANDLE;
  g_current = -1;
  g_pending.clear();
  g_on = false;
  g_dd = nullptr;
}

// Needs RT only: the emissive scene has its own acceleration structures (em_as_build).
bool em_wanted() { return g_on && rt_enabled() && cfg_hot(CfgHot::rt_emissive) != 0; }

bool em_is_fx_pass(const Bg3PassInfo& p, uint32_t gw, uint32_t gh) {
  return g_on && p.color_count == 1 && p.color_formats[0] == VK_FORMAT_B10G11R11_UFLOAT_PACK32 &&
         p.depth_format == VK_FORMAT_D24_UNORM_S8_UINT && gw && p.width == gw && p.height == gh;
}

bool em_pass_begin(VkCommandBuffer cmd, const CmdState* s, int* ts) {
  (void)s;
  *ts = -1;
  t_pass_fx = false;
  t_pass_soup = VK_NULL_HANDLE;
  if (!g_on) return false;
  const bool want = em_wanted();
  // Earlier builds read the soup ranges this pass may overwrite (BLAS builds, light list).
  if (want)
    barrier(cmd, kAS | kCS | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, kXFB,
            VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT);
  uint32_t p = ts_pair(cmd, 0, want);
  *ts = p == UINT32_MAX ? -1 : int(p);
  return want;
}

// A draw in an effects pass that is not captured (rt_emissive=0): only marks the pass as
// one that writes the effects target, so its time counts as "without capture".
void em_note_draw(const CmdState* s) {
  if (t_pass_fx) return;
  std::lock_guard<std::mutex> lock(g_mu);
  const MatKind k = material(s->fs).kind;
  if (k == kMatEmissive || k == kMatDecal || k == kMatBackdrop) t_pass_fx = true;
}

bool em_draw(VkCommandBuffer cmd, const CmdState* s, const DrawArgs& a, const EmDrawIn& in, VkBuffer* soup,
             VkDeviceSize* off, VkDeviceSize* bytes, uint32_t* prims_out) {
  const auto core = core_shared_lock();   // the record policy is the core's (before g_mu)
  std::lock_guard<std::mutex> lock(g_mu);
  PCmd& P = g_pending[cmd];
  Census& C = P.census;
  ++C.draws;
  const Mat& mat = material(s->fs);
  ++C.kinds[mat.kind];
  if (mat.kind == kMatEmissive || mat.kind == kMatDecal || mat.kind == kMatBackdrop) {
    // An EMI-variant draw: this pass writes the effects target.
    t_pass_fx = true;
    P.fx = true;
    g_fx = Fx{s->pass.color_images[0], s->pass.color_views[0], s->color_layouts[0], s->pass.width, s->pass.height,
              frame_index()};
  }
  // rt_em_backdrop=1 (a measurement): skydome backdrops are captured too, so the projected
  // check shows their share of the effects target.
  const bool backdrop = mat.kind == kMatBackdrop && cfg_hot(CfgHot::rt_em_backdrop) != 0;
  // rt_em_decal=1 (default): emissive deferred decals are captured too. Their geometry is
  // the projection box, not the surface they light, so they are flagged approximate (the
  // calibration sets their level) and the check counts the pixels they light as covered by
  // an emitter in front of the depth. In Water Queen's House they are 55% of the effects
  // target's bright pixels.
  const bool decal = mat.kind == kMatDecal && cfg_hot(CfgHot::rt_em_decal) != 0;
  if (mat.kind != kMatEmissive && mat.kind != kMatForward && !backdrop && !decal) return false;
  FsCount& fc = g_fs_window[s->fs];
  ++fc.draws;
  ++fc.uv[in.uv_match < 5 ? in.uv_match : 0];
  fc.uv_loc = in.uv;
  fc.uv_fs = in.uv_fs;
  fc.uv_vs_comps = in.uv_vs_comps;
  if (!in.xfb) return ++C.skip_xfb, false;
  if (a.kind > 1) return ++C.skip_indirect, false;
  const bool list = in.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  if (!in.triangles || (!list && !((in.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
                                     in.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) &&
                                    (a.kind == 0 || !in.restart))))
    return ++C.skip_topo, false;
  uint64_t prims = list ? a.count / 3 : (a.count > 2 ? a.count - 2 : 0);
  prims *= a.instances;
  if (!prims) return false;
  const VkDeviceSize need = prims * 3 * in.stride;
  g_demand += up256(need);   // what this frame asks for (the soup grows to it at the next build)
  // The ring: what the pending builds may still read stays (a third of the soup per build).
  if (g_soup_since + need > g_soup.size / 3) return ++C.skip_full, false;
  PDraw d;
  if (!make_record(cmd, s->fs, mat, P, d)) return (d.rec.flags & em::kFlagExcluded) ? ++C.skip_excluded : ++C.skip_core, false;
  if (g_soup_head + need > g_soup.size) g_soup_head = 0;
  d.off = g_soup_head;
  d.soup = g_soup.b;
  d.prims = uint32_t(prims);
  d.args = a;
  d.pipe_id = s->pipe_id[0];
  d.vs = s->vs;
  d.vb0 = s->vb[0].buffer;
  d.vb0_off = s->vb[0].offset;
  d.ib = s->ib;
  d.ib_off = s->ib_offset;
  g_soup_head = up256(g_soup_head + need);
  g_soup_since += up256(need);
  g_stride = in.stride;
  *off = d.off;
  *bytes = need;
  *prims_out = d.prims;
  *soup = g_soup.b;
  t_pass_soup = g_soup.b;
  P.draws.push_back(d);
  ++C.captured;
  C.captured_tris += prims;
  ++fc.captured;
  return true;
}

void em_pass_done(VkCommandBuffer cmd, const CmdState* s, VkBuffer ubo, VkDeviceSize ubo_off, uint32_t stride,
                  const std::vector<std::pair<VkDeviceSize, uint32_t>>& ranges, int ts) {
  (void)s;
  const bool fx = t_pass_fx;
  t_pass_fx = false;
  std::vector<std::pair<Bg3Binding, uint32_t>> copies;
  if (fx || !ranges.empty()) {
    std::lock_guard<std::mutex> lock(g_mu);
    PCmd& P = g_pending[cmd];
    if (fx) ++P.census.passes;
    if (!P.ubo && ubo) P.ubo = ubo, P.ubo_off = ubo_off;
    if (!ranges.empty() && !ubo) ++P.census.no_ubo;
    copies.swap(P.copies);
  }
  // The luminance scales the pass's draws read, as they read them (the game rewrites the
  // buffer later in the batch).
  if (!copies.empty() && !ensure_copy()) g_copies_skipped += uint32_t(copies.size());
  if (!copies.empty() && ensure_copy()) {
    barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, kCS, VK_ACCESS_SHADER_READ_BIT);
    g_dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_cp_pipe);
    for (const auto& [g, slot] : copies) {
      VkDescriptorBufferInfo src{g.buffer, g.offset, g.range && g.range != VK_WHOLE_SIZE ? (std::min)(g.range, VkDeviceSize(16)) : 16};
      VkDescriptorBufferInfo dst{g_globals.b, 0, g_globals.size};
      VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
      for (int i = 0; i < 2; ++i) {
        w[i].dstBinding = uint32_t(i);
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[i].pBufferInfo = i ? &dst : &src;
      }
      g_dd->vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_cp_pl, 0, 2, w);
      // [1]: where the source's first 4 words go; [2]: the source's bound bytes (up to 16)
      uint32_t pc[4] = {slot, kGlobSlots, uint32_t(src.range), 0};
      g_copy_src_off = g.offset;
      g_copy_src_range = g.range;
      g_dd->vk.CmdPushConstants(cmd, g_cp_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
      g_dd->vk.CmdDispatch(cmd, 1, 1, 1);
    }
    g_copies_recorded += uint32_t(copies.size());
    barrier(cmd, kCS, VK_ACCESS_SHADER_WRITE_BIT, kCS | kAS | VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT);
    if (ranges.empty()) restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  }
  // The pass's ranges are in the soup its draws captured into, which a resize may have
  // replaced since (the old one is retired, not freed, until the sets rotate).
  const VkBuffer pass_soup = t_pass_soup ? t_pass_soup : g_soup.b;
  t_pass_soup = VK_NULL_HANDLE;
  if (!ranges.empty()) {
    barrier(cmd, kXFB, VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT, kCS | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    for (const auto& [off, tris] : ranges) {
      const VkDeviceSize bytes = VkDeviceSize(tris) * 3 * stride;
      if (ubo) {
        RtCapture rc{};
        rc.xfb = pass_soup;
        rc.alpha_base = off;
        rc.alpha_bytes = bytes;
        rc.stride = stride;
        rc.ubo = ubo;
        rc.ubo_offset = ubo_off;
        rt_convert(cmd, rc);
      } else {
        g_dd->vk.CmdFillBuffer(cmd, pass_soup, off, bytes, 0);   // no camera: collapsed (never hit)
      }
    }
    barrier(cmd, kCS | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            kAS | kCS, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  }
  if (ts >= 0) {
    ts_end(cmd, uint32_t(ts));
    if (!fx) {   // an SSS or forward pass: not timed as an effects pass
      std::lock_guard<std::mutex> lock(g_ts_mu);
      g_ts[ts].kind = 2;
    }
  }
}

void em_forget_cmd(VkCommandBuffer cmd) {
  if (!g_on) return;
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_pending.find(cmd);
  if (it == g_pending.end()) return;
  if (!it->second.draws.empty()) ++g_stale;   // recorded again before it was submitted
  g_pending.erase(it);
}

void em_after_submit(DeviceData* dd, VkQueue queue, const std::vector<VkCommandBuffer>& cmds) {
  if (!g_on || dd != g_dd) return;
  const auto core = core_shared_lock();   // the build applies the core's calibration (before g_mu)
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_pending.empty()) return;
  std::vector<PCmd> taken;
  bool fx = false;
  std::vector<uint64_t> refs;
  for (VkCommandBuffer c : cmds) {
    auto it = g_pending.find(c);
    if (it == g_pending.end()) continue;
    fx |= it->second.fx;
    if (const CmdState* s = cmd_state(c)) refs.insert(refs.end(), s->refs.begin(), s->refs.end());
    taken.push_back(std::move(it->second));
    g_pending.erase(it);
  }
  if (!fx || !em_wanted()) return;
  Own& o = g_own[g_own_next % kOwn];
  const VkResult ready = o.used ? dd->vk.WaitForFences(dd->device, 1, &o.fence, VK_TRUE, 0) : VK_SUCCESS;
  fault_on_result(dd, ready, "emissive build fence poll");
  if (ready != VK_SUCCESS) {
    ++g_busy;
    return;
  }
  ++g_own_next;
  o.used = false;   // The previous submission is complete; failed recording owns no fence.
  if (dd->vk.ResetFences(dd->device, 1, &o.fence) != VK_SUCCESS ||
      dd->vk.ResetCommandBuffer(o.cb, 0) != VK_SUCCESS) return;
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  const VkResult begin_result = dd->vk.BeginCommandBuffer(o.cb, &bi);
  fault_on_result(dd, begin_result, "emissive build begin recording");
  if (begin_result != VK_SUCCESS) return;
  // Everything the game's batch wrote (effects-pass XFB, conversions) before our reads.
  barrier(o.cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
  build(o.cb, taken);
  for (size_t i = 0; i < g_soup_retired.size();) {
    if (--g_soup_retired[i].builds_left == 0) {
      free_buf(g_soup_retired[i].b);
      g_soup_retired.erase(g_soup_retired.begin() + i);
    } else {
      ++i;
    }
  }
  g_soup_since = 0;
  // Size the soup to the demand: three frames of it (a range lives about three frames).
  const VkDeviceSize max_soup = VkDeviceSize((std::max)(cfg_int("rt_em_xfb_max_mb", 512), 8)) << 20;
  if (g_demand * 3 > g_soup.size && g_soup.size < max_soup) {
    VkDeviceSize want = g_soup.size;
    while (want < g_demand * 4 && want < max_soup) want *= 2;
    Buf nb;
    if (make_buf(nb, want, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | rt_xfb_usage(),
                 false)) {
      log("[em] emissive soup grown to %llu MB (demand %.1f MB per frame)", (unsigned long long)(want >> 20),
          double(g_demand) / 1048576.0);
      g_soup_retired.push_back({g_soup, kRing + kOwn});   // still bound by sets and recorded passes
      g_soup = nb;
      g_soup_head = 0;
      // Defined contents from the start (only ranges the capture wrote are ever built; this
      // is the second line of defence).
      dd->vk.CmdFillBuffer(o.cb, g_soup.b, 0, g_soup.size, 0);
    }
  }
  g_demand = 0;
  const VkResult end_result = dd->vk.EndCommandBuffer(o.cb);
  fault_on_result(dd, end_result, "emissive build end recording");
  if (end_result != VK_SUCCESS) return;
  // The game's images our build samples (masks, base maps, VT caches) stay alive until it
  // ran: register what the game's command buffers referenced on ours too (tracker.cpp).
  if (CmdState* ms = cmd_state(o.cb, true)) ms->refs = std::move(refs);
  std::vector<VkCommandBuffer> mine{o.cb};
  life_on_submit(queue, mine);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &o.cb;
  VkResult r = dd->vk.QueueSubmit(queue, 1, &si, o.fence);   // the caller holds the queue lock
  fault_on_result(dd, r, "emissive build submission");
  o.used = r == VK_SUCCESS;
  if (r != VK_SUCCESS) log("[em] build submission failed (%d)", r);
}

bool em_scene(VkCommandBuffer cmd, VkDescriptorSetLayout* layout, VkDescriptorSet* set, Bg3EmissiveInfo* info) {
  if (!g_on) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  *layout = g_dsl;
  *set = g_current < 0 || !em_wanted() ? VK_NULL_HANDLE : g_sets[g_current];
  // The consumer's command buffer reads the game's images and buffers through the set a
  // frame after they were drawn with: keep them alive until it has run (tracker.cpp).
  if (cmd && *set)
    for (uint64_t h : g_slots[g_current].handles) life_note(cmd, h);
  if (info) {
    *info = g_info;
    // The effects target of the latest frame that drew one (consumers compare with it).
    const bool fresh = g_fx.image && g_fx.frame + 2 >= frame_index();
    info->fx_image = fresh ? g_fx.image : VK_NULL_HANDLE;
    info->fx_view = fresh ? g_fx.view : VK_NULL_HANDLE;
    info->fx_layout = g_fx.layout;
    info->fx_width = g_fx.w;
    info->fx_height = g_fx.h;
  }
  return true;
}

// log() keeps 2 KB per line: a long list goes out in parts.
void log_list(const char* head, const std::vector<std::string>& items) {
  std::vector<std::string> parts(1);
  for (const auto& it : items) {
    if (!parts.back().empty() && parts.back().size() + it.size() > 1700) parts.emplace_back();
    parts.back() += parts.back().empty() ? " " + it : "; " + it;
  }
  for (size_t i = 0; i < parts.size(); ++i) {
    if (parts.size() > 1) log_stats("%s (part %zu/%zu):%s", head, i + 1, parts.size(), parts[i].c_str());
    else log_stats("%s:%s", head, parts[i].c_str());
  }
}

void em_on_present() {
  if (!g_on) return;
  const uint64_t now = frame_index();
  static int reset_token = 0;
  const bool reset = stats_window::reset(cfg_int, reset_token);
  if (logging::stats_enabled(cfg_int) && g_qp) {
    std::lock_guard<std::mutex> lock(g_ts_mu);
    for (uint32_t p = 0; p < kTsPairs; ++p) {
      TsPair& t = g_ts[p];
      if (reset) t.frame = 0;
      if (!t.frame || t.frame + 4 > now) continue;
      uint64_t r[4] = {};
      VkResult res = g_dd->vk.GetQueryPoolResults(g_dd->device, g_qp, p * 2, 2, sizeof r, r, 16,
                                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
      const bool done = res == VK_SUCCESS && r[1] && r[3];
      if (!done && t.frame + 60 > now) continue;
      if (done && r[2] >= r[0]) {
        float ms = float(double(r[2] - r[0]) * g_ts_period * 1e-6);
        if (t.kind == 1) g_win.build_ms.push_back(ms);
        else if (t.kind == 0) g_win.fx_frame_ms[t.capture ? 1 : 0][t.frame] += ms;
      }
      t = TsPair{};
    }
  }
  const auto core = core_shared_lock();   // the calibration rule is the core's (before g_mu)
  std::lock_guard<std::mutex> lock(g_mu);
  if (reset) { g_win = Window{}; g_win_frames = 0; g_fs_window.clear(); for (auto& slot : g_slots) slot.frame = 0; }
  // Command buffers the game never submitted (a dropped frame): their draws go.
  if (g_pending.size() > 256) g_pending.clear();
  for (uint32_t s = 0; s < kRing; ++s) {
    SlotInfo& si = g_slots[s];
    if (!si.frame || si.frame + 4 > now) continue;
    const uint8_t* region = g_host.map + VkDeviceSize(s) * kRegion;
    const uint32_t* header = reinterpret_cast<const uint32_t*>(region + kOffHeader);
    const float* sums = reinterpret_cast<const float*>(header + 16);
    double power = 0;
    for (uint32_t g = 0; g < si.groups; ++g) {
      if (std::isfinite(sums[g])) power += sums[g];
      else ++g_win.nonfinite;
    }
    Window& w = g_win;
    w.collapsed += header[12];
    ++w.builds;
    w.c.add(si.census);
    w.instances += si.instances;
    w.tris += double(si.tris);
    w.lights += si.lights;
    w.overflow += si.overflow;
    w.power += power;
    w.last_power = power;
    w.last_draws = si.census.kinds[kMatEmissive];
    w.last_lights = si.lights;
    g_last_cluster = si.cluster;
    // The luminance-scale copies the records used (em_copy.hlsl), as written on the GPU.
    for (uint32_t r = 0; r < si.rec_glob.size(); ++r) {
      if (!(si.rec_flags[r] & kFlagLuminance)) continue;
      Window::Glob& gl = w.glob[si.rec_fs[r]];
      const uint32_t slot = si.rec_glob[r];
      if (slot == UINT32_MAX || slot >= kGlobSlots) {
        ++gl.missing;
        continue;
      }
      float v;
      std::memcpy(&v, g_globals.map + VkDeviceSize(slot) * 4, 4);
      gl.slot = slot;
      gl.value = v;
      gl.vmin = (std::min)(gl.vmin, v);
      gl.vmax = (std::max)(gl.vmax, v);
      ++gl.n;
    }
    const uint32_t* st = reinterpret_cast<const uint32_t*>(region + kOffStats);
    if (st[6]) {
      ++w.dbg_frames;
      w.dbg_depth += st[0];
      w.dbg_hit += st[1];
      w.dbg_miss += st[2];
      w.dbg_dark += st[3];
      w.dbg_bright += st[7];
      w.dbg_front += st[4];
      for (uint32_t i = 0; i < 7; ++i) w.overlap_probe[i] += st[8 + i];
      for (uint32_t r = 0; r < si.rec_fs.size(); ++r) {
        const uint32_t* q = st + 16 + r * 16;
        if (!q[4]) continue;
        Window::Cmp& cmp = w.cmp[si.rec_fs[r]];
        cmp.ours += double((uint64_t(q[1]) << 32) | q[0]) / 4096.0;
        cmp.game += double((uint64_t(q[3]) << 32) | q[2]) / 4096.0;
        cmp.px += q[4];
        cmp.zero += q[5];
        cmp.bright += q[8];
        cmp.ours_b += double((uint64_t(q[10]) << 32) | q[9]) / 4096.0;
        cmp.game_b += double((uint64_t(q[12]) << 32) | q[11]) / 4096.0;
        cmp.overlap += q[13];
        cmp.nomap += double((uint64_t(q[7]) << 32) | q[6]) / 4096.0;
        cmp.flags |= si.rec_flags[r];
      }
      // A family never calibrated is calibrated as soon as 4 checks have seen it on enough
      // bright pixels, not at the end of its first 300-present window: uncalibrated, the
      // decals' boxes lit Water Queen's House 35x too bright for the first seconds after a
      // load (em8-wq-on). The family's window sums restart at the new scale.
      if (cfg_int("rt_em_calib", 1) != 0) {
        for (auto it = w.cmp.begin(); it != w.cmp.end();) {
          const Window::Cmp& cm = it->second;
          Calib& cal = g_calib[it->first];
          const double ratio = cm.game > 0 ? cm.ours / cm.game : 0.0;
          if (calib_update(cal, true, ratio, cm.flags, w.dbg_frames, cm.bright)) {
            it = w.cmp.erase(it);
          } else {
            ++it;
          }
        }
      }
    }
    if (cfg_int("rt_em_log", 0))
      log("[em] frame %llu: %u emissive draws, %.0f triangles, %u lights, total power %.4g (effects-target units x m^2)",
          (unsigned long long)(si.frame - 1), si.census.kinds[kMatEmissive], double(si.tris), si.lights, power);
    si.frame = 0;
  }
  // Windows of rt_timing_frames presents, not builds: with rt_emissive=0 nothing is built,
  // and the effects passes' cost without the capture must still be logged.
  const int every = stats_period(unsigned((std::max)(cfg_int("rt_timing_frames", 300), 30)));
  Window& w = g_win;
  if (++g_win_frames < uint32_t(every)) return;
  g_win_frames = 0;
  if (!w.builds) {
    std::vector<float> fx[2];
    for (int k = 0; k < 2; ++k)
      for (auto& [f, ms] : w.fx_frame_ms[k]) fx[k].push_back(float(ms));
    auto med = [](std::vector<float>& v) {
      if (v.empty()) return 0.0f;
      std::sort(v.begin(), v.end());
      return v[v.size() / 2];
    };
    if (!fx[0].empty() || !fx[1].empty())
      log_stats("[em] no builds in the last %d presents (rt_emissive=0 or no effects pass): effects passes per frame median "
          "%.3f ms without capture (%zu frames), %.3f ms with (%zu frames)",
          every, med(fx[0]), fx[0].size(), med(fx[1]), fx[1].size());
    g_win = Window{};
    g_fs_window.clear();
    return;
  }
  const double nb = double(w.builds);
  const Census& c = w.c;
  auto pct = [](std::vector<float>& v, double q) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    return v[(std::min)(v.size() - 1, size_t(double(v.size()) * q))];
  };
  std::vector<float> fx[2];
  for (int k = 0; k < 2; ++k)
    for (auto& [f, ms] : w.fx_frame_ms[k]) fx[k].push_back(float(ms));
  log_stats("[em] last %llu builds, per build: %.1f effects passes, %.1f draws in them (engine %.1f, unknown %.1f, decal %.1f, "
      "backdrop %.1f, other materials %.1f); emissive %.1f, captured %.1f; left out: no XFB %.2f, indirect %.2f, "
      "topology %.2f, soup full %.2f, records full %.2f, textures full %.2f; VT albedo missing %.2f, global scale "
      "conflicts %.2f; command buffers re-recorded unsubmitted %u, builds skipped (busy) %u",
      (unsigned long long)w.builds, c.passes / nb, c.draws / nb, c.kinds[kMatEngine] / nb, c.kinds[kMatUnknown] / nb,
      c.kinds[kMatDecal] / nb, c.kinds[kMatBackdrop] / nb, c.kinds[kMatOther] / nb, c.kinds[kMatEmissive] / nb,
      c.captured / nb, c.skip_xfb / nb, c.skip_indirect / nb, c.skip_topo / nb, c.skip_full / nb, c.skip_recs / nb,
      c.skip_tex / nb, c.vt_missing / nb, c.global_conflicts / nb, g_stale, g_busy);
  log_stats("[em] soup %llu MB, %u draws dropped by a soup resize; forward-pass emissive draws %.1f per build; triangles per "
      "light %u (last build); invalid soup triangles collapsed by the sanitize pass %.1f per build; material constants "
      "not host-readable %.2f per build; draws without a core (no record) %.2f per build; draws of excluded families "
      "(rt_em_fx_pulse=0: 304be1c7) %.2f per build",
      (unsigned long long)(g_soup.size >> 20), g_regrown, c.kinds[kMatForward] / nb, g_last_cluster,
      double(w.collapsed) / nb, c.cb_unreadable / nb, c.skip_core / nb, c.skip_excluded / nb);
  {
    std::string line;
    for (auto& [fs, gl] : w.glob) {
      char buf[400];
      // Mean luminance per checked pixel, ours (with the scale) and the game's: ours / value is
      // the record's level before the scale, to compare with the game's.
      const auto it = w.cmp.find(fs);
      const double px = it != w.cmp.end() ? it->second.px : 0.0;
      const double mo = px ? it->second.ours / px : 0.0, mg = px ? it->second.game / px : 0.0;
      snprintf(buf, sizeof buf,
               "%s %08x %s slot %u value %.6g (min %.6g, max %.6g, %u read, %u without a slot); per checked px "
               "ours %.5g, game %.5g, ours before the scale %.5g",
               line.empty() ? "" : ";", unsigned(fs >> 32), material(fs).name.c_str(), gl.slot, double(gl.value),
               double(gl.n ? gl.vmin : 0.0f), double(gl.vmax), gl.n, gl.missing, mo, mg,
               gl.value > 0.0f ? mo / double(gl.value) : 0.0);
      line += buf;
    }
    float src[4] = {};
    std::memcpy(src, g_globals.map + VkDeviceSize(kGlobSlots) * 4, 16);
    log_stats("[em] luminance scale copies: %u dispatched, %u owed but not recorded (pipeline missing); pulse time not "
        "host-readable %.2f per build (its mean used); latest source offset %llu range %llu, words 0-3 %.6g %.6g "
        "%.6g %.6g;%s",
        g_copies_recorded.exchange(0), g_copies_skipped.exchange(0), c.pulse_no_time / nb,
        (unsigned long long)g_copy_src_off.load(), (unsigned long long)g_copy_src_range.load(), double(src[0]),
        double(src[1]), double(src[2]), double(src[3]), line.empty() ? " no luminance-emissive records" : line.c_str());
  }
  log_stats("[em] per build: %.1f TLAS instances, %.0f triangles, %.0f lights (%.0f left out by rt_em_max_lights), total power "
      "%.4g (effects-target units x m^2; last build %.4g, %u draws, %u lights), %llu non-finite power sums; GPU: build "
      "median %.3f ms (p99 %.3f, %zu samples), effects passes per frame median %.3f ms with capture (%zu frames), %.3f ms "
      "without (%zu frames)",
      w.instances / nb, w.tris / nb, w.lights / nb, w.overflow / nb, w.power / nb, w.last_power, w.last_draws,
      w.last_lights, (unsigned long long)w.nonfinite, pct(w.build_ms, 0.5), pct(w.build_ms, 0.99), w.build_ms.size(),
      pct(fx[1], 0.5), fx[1].size(), pct(fx[0], 0.5), fx[0].size());
  log_stats("[em] BLAS: %llu refits, %llu full builds since the last line (rt_em_refit, rt_em_rebuild); slot storage grown %llu "
      "times in all",
      (unsigned long long)g_as_refits, (unsigned long long)g_as_builds, (unsigned long long)g_as_grown);
  g_as_refits = g_as_builds = 0;
  {
    // Every family, with its UV-slot verdict (TRACK_D 13.19): whether the captured UV is the
    // one its fragment shader reads.
    std::vector<std::string> items;
    std::vector<std::pair<uint32_t, uint64_t>> order;
    for (auto& [fs, fc] : g_fs_window) order.push_back({fc.draws, fs});
    std::sort(order.rbegin(), order.rend());
    static const char* kUv[5] = {"uv unknown", "uv ok", "UV WRONG: the FS's UV is a wider VS output",
                                 "UV WRONG: the FS reads its UV elsewhere", "no FS uv"};
    for (auto& [d, fs] : order) {
      const FsCount& fc = g_fs_window[fs];
      int worst = 0;
      for (int k = 0; k < 5; ++k)
        if (fc.uv[k] && (worst == 0 || k == 2 || k == 3)) worst = k;
      char buf[320];
      snprintf(buf, sizeof buf, "%08x %s %.1f/%.1f, %s (captured location %d, FS location %d, VS writes %u there)",
               unsigned(fs >> 32), material(fs).name.c_str(), fc.draws / nb, fc.captured / nb, kUv[worst], fc.uv_loc,
               fc.uv_fs, fc.uv_vs_comps);
      items.push_back(buf);
    }
    if (!items.empty()) log_list("[em] emissive families (draws/captured per build)", items);
    geo_uv_summary();
  }
  if (w.dbg_frames) {
    double ours = 0, game = 0;
    std::vector<std::string> items, dump;
    const bool calib = cfg_int("rt_em_calib", 1) != 0;
    for (auto& [fs, cm] : w.cmp) {
      ours += cm.ours, game += cm.game;
      const double ratio = cm.game > 0 ? cm.ours / cm.game : 0.0;
      const double px = cm.bright / double(w.dbg_frames);   // bright hits per frame: the gate
      Calib& cal = g_calib[fs];
      const float before = em::calib_scale(cal);
      // Calibrate approximate families seen on enough pixels, in the effects target (a
      // forward family's emission is not there: ours >> game).
      // The family's own ratio at scale 1 is ratio / scale (ours scales with it). Animated
      // emitters (the lava's heat flow) swing it 1.0-2.1 between 300-build windows, so a
      // full step chased the animation (the scale rose while the ratio was > 1). The scale
      // is 1 / the exponential average of that ratio in log space (weight 1/2; the first
      // window sets it), i.e. it matches the time average.
      if (calib) calib_update(cal, false, ratio, cm.flags, 0, px);
      // Two ratios: over every hit (the family's energy: what the light list carries and the
      // calibration matches) and over its bright hits only (the emission the player sees,
      // apart from dark texels and texels whose game pixel other emitters add to): the gate.
      const double ratio_b = cm.game_b > 0 ? cm.ours_b / cm.game_b : 0.0;
      char buf[300];
      snprintf(buf, sizeof buf, "%08x %s %.3f, bright %.3f (%.0f bright px of %.0f, %.0f overlapped%s%s%s, scale %.3f)",
               unsigned(fs >> 32), material(fs).name.c_str(), ratio, ratio_b, px, cm.px / double(w.dbg_frames),
               cm.overlap / double(w.dbg_frames),
               (cm.flags & kFlagApprox) ? ", approx" : "", (cm.flags & kFlagForward) ? ", forward" : "",
               px < 200 ? ", under 200 bright px: not gated" : "", before);
      items.push_back(buf);
      // The record behind a luminance family or an exact family outside 0.8-1.25.
      const bool off = !(cm.flags & kFlagApprox) && px >= 200 && (ratio_b < 0.8 || ratio_b > 1.25);
      if ((cm.flags & kFlagLuminance) || off) {
        const RecDbg& g = g_rec_dbg[fs];
        char d[520];
        int n = snprintf(d, sizeof d,
                         "%08x: radiance %.4g %.4g %.4g, mode %u, tex %u, flags 0x%x, slot %d; hits with ours = 0 %.1f%%; per "
                         "hit ours %.4g, ours without the maps %.4g, game %.4g",
                         unsigned(fs >> 32), double(g.r.radiance[0]), double(g.r.radiance[1]), double(g.r.radiance[2]),
                         g.r.mode, g.r.tex, g.r.flags, g.r.glob == UINT32_MAX ? -1 : int(g.r.glob),
                         cm.px ? 100.0 * cm.zero / cm.px : 0.0, cm.px ? cm.ours / cm.px : 0.0,
                         cm.px ? cm.nomap / cm.px : 0.0, cm.px ? cm.game / cm.px : 0.0);
        if (g.cb && n > 0 && n < int(sizeof d))
          snprintf(d + n, sizeof d - n, "; CB words 0-5 %.4g %.4g %.4g %.4g %.4g %.4g, 6-7 %.4g %.4g, time %.4g, pulse %.4g",
                   double(g.m[0]), double(g.m[1]), double(g.m[2]), double(g.m[3]), double(g.m[4]), double(g.m[5]),
                   double(g.m45[0]), double(g.m45[1]), double(g.time), double(g.pulse));
        dump.push_back(d);
      }
    }
    log("[em] projected check over %llu frames: emissive hits on %.3f%% of the depth pixels; ours/game radiance %.3f over "
        "them; effects-target pixels brighter than rt_em_fx_thr with no emissive hit %.3f%% of %.0f (coverage %.1f%%, of "
        "which %.1f%% by an emitter in front of the depth: alpha-blended); hits where the game shows < 2%% of ours %.3f%% of "
        "the hits",
        (unsigned long long)w.dbg_frames, w.dbg_depth ? 100.0 * double(w.dbg_hit) / double(w.dbg_depth) : 0.0,
        game > 0 ? ours / game : 0.0, w.dbg_bright ? 100.0 * double(w.dbg_miss) / double(w.dbg_bright) : 0.0,
        double(w.dbg_bright) / double(w.dbg_frames),
        w.dbg_bright ? 100.0 * (1.0 - double(w.dbg_miss) / double(w.dbg_bright)) : 0.0,
        w.dbg_bright ? 100.0 * double(w.dbg_front) / double(w.dbg_bright) : 0.0,
        w.dbg_hit ? 100.0 * double(w.dbg_dark) / double(w.dbg_hit) : 0.0);
    if (!items.empty()) log_list("[em] ours/game per family", items);
    if (w.overlap_probe[0])
      log("[em-overlap] receivers %llu candidates %llu multi-draw %llu distinct-draw sum %llu invalid %llu overflow %llu missing %llu",
          (unsigned long long)w.overlap_probe[0], (unsigned long long)w.overlap_probe[1],
          (unsigned long long)w.overlap_probe[2], (unsigned long long)w.overlap_probe[3],
          (unsigned long long)w.overlap_probe[4], (unsigned long long)w.overlap_probe[5], (unsigned long long)w.overlap_probe[6]);
    if (!dump.empty()) log_list("[em] record dump (luminance families, exact families outside 0.8-1.25)", dump);
  }
  g_win = Window{};
  g_fs_window.clear();
  g_stale = g_busy = g_regrown = 0;
}

}  // namespace bg3
