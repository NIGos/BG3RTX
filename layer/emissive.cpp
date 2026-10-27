// Track D D-EM: emissive geometry capture (docs/TRACK_D_RT.md 12). The data side:
// which draws emit, their geometry in a TLAS of their own, a record per draw and a
// light list. Effects that use it (emission in GI and reflections, light sampling) come
// later; rt_debug=6/7/8 shows and checks the data.
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

namespace bg3 {

namespace {

constexpr uint32_t kRing = 8;            // descriptor sets and host regions in flight
constexpr uint32_t kOwn = 8;             // own command buffers (fenced)
constexpr uint32_t kMaxRecs = 1024;      // emissive draws per build
constexpr uint32_t kMaxTex = 256;        // textures per build (slot 0: dummy)
constexpr uint32_t kMaxCache = 16;       // VT tile caches per build (slot 0: dummy)
constexpr uint32_t kMaxInst = 16384;     // emissive TLAS instances that get lights
constexpr uint32_t kHeaderWords = 16 + 1024;
constexpr uint32_t kStatWords = 16 + kMaxRecs * 4;
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

// Must match shaders/rt/em_common.hlsli.
struct EmRec {
  float radiance[3];
  uint32_t mode;
  uint32_t tex, flags, fs_lo, fs_hi;
  float uv[4];
  uint32_t tex2, vt_tex, vt_cache, draw;
  float uv2[4];
  float vt_p0[4], vt_p1[4];
  float vt_t[32];
  uint32_t cls, glob, pad[2];   // glob: luminance-scale slot in g_globals (UINT32_MAX: none)
};
static_assert(sizeof(EmRec) == kRecBytes, "EmRec must match em_common.hlsli");
enum : uint32_t { kModeConst = 0, kModeMaskR = 1, kModeMaskRGB = 2, kModeBlackbody = 3 };
enum : uint32_t { kFlagLuminance = 1, kFlagApprox = 2, kFlagPulse = 4, kFlagBaseMap = 8, kFlagVtAlbedo = 16,
                  kFlagForward = 32, kFlagCalibrated = 64 };

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
  uint32_t global_conflicts = 0, vt_missing = 0, cb_unreadable = 0;
  uint64_t captured_tris = 0;
  void add(const Census& o) {
    passes += o.passes, draws += o.draws, captured += o.captured, captured_tris += o.captured_tris;
    for (int i = 0; i < kMatKinds; ++i) kinds[i] += o.kinds[i];
    skip_xfb += o.skip_xfb, skip_indirect += o.skip_indirect, skip_topo += o.skip_topo, skip_full += o.skip_full;
    skip_recs += o.skip_recs, skip_tex += o.skip_tex, no_ubo += o.no_ubo, global_conflicts += o.global_conflicts;
    vt_missing += o.vt_missing, cb_unreadable += o.cb_unreadable;
  }
};
struct FsCount { uint32_t draws = 0, captured = 0; };
std::map<uint64_t, FsCount> g_fs_window;   // per fragment shader over the log window
uint32_t g_stale = 0;                      // command buffers re-recorded before submission
uint32_t g_busy = 0;                       // builds skipped: no free command buffer of our own

// The effects target, as last seen.
struct Fx { VkImage image = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; uint32_t w = 0, h = 0; uint64_t frame = 0; };
Fx g_fx;

// ---- per command buffer (draws recorded, not yet submitted) ----
struct View { VkImageView v = VK_NULL_HANDLE; VkImageLayout l = VK_IMAGE_LAYOUT_UNDEFINED; };
struct PDraw {
  EmRec rec;
  View mask, base, vt_ind, vt_cache;
  VkDeviceSize off = 0;
  VkBuffer soup = VK_NULL_HANDLE;     // the soup it was captured into (a grown soup replaces it)
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
thread_local bool t_pass_fx = false;   // the pass being recorded on this thread drew an EMI material

// The emissive soup: a ring of 256-aligned ranges (a range lives about three frames:
// written by its pass, read by the build and by consumers of the next frame).
Buf g_soup;
VkDeviceSize g_soup_head = 0, g_soup_since = 0;   // bytes allocated since the last build
VkDeviceSize g_demand = 0;                        // bytes asked for since the last build (captured or not)
uint32_t g_regrown = 0;                           // draws dropped because the soup was replaced
uint32_t g_last_cluster = 1;                      // triangles per light of the latest build read back

// Per-family calibration against the game's effects target (rt_em_calib): for families whose
// formula is not fully reproduced (kFlagApprox), the radiance is scaled by game/ours as the
// projected check measures it on screen (damped, clamped to [1/64, 64]); exact families are
// never scaled (their ratio is the validation). Forward-pass families are not in the effects
// target and are not calibrated.
struct Calib { float scale = 1.0f; uint32_t updates = 0; };
std::unordered_map<uint64_t, Calib> g_calib;
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
  struct Cmp { double ours = 0, game = 0, px = 0; uint32_t flags = 0; };
  std::map<uint64_t, Cmp> cmp;                 // projected comparison per fragment shader
  uint64_t dbg_frames = 0, dbg_depth = 0, dbg_hit = 0, dbg_miss = 0, dbg_dark = 0, dbg_bright = 0, dbg_front = 0;
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
  dd->vk.BindBufferMemory(dd->device, out.b, out.m, 0);
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

// Reads `n` bytes at `extra` of the buffer bound at (set, binding) of the graphics bind
// point, from the game's host mapping (alphatest.cpp read_bound).
bool read_bound(VkCommandBuffer cmd, uint32_t set, uint32_t binding, VkDeviceSize extra, void* dst, size_t n) {
  Bg3Binding b;
  if (!query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, set, binding, 0, &b) || !b.buffer) return false;
  if (b.range != VK_WHOLE_SIZE && extra + n > b.range) return false;
  return read_mapped(b.buffer, b.offset + extra, dst, n);
}

View bound_view(VkCommandBuffer cmd, uint32_t set, uint32_t binding) {
  Bg3Binding t;
  if (query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, set, binding, 0, &t) && t.view) return View{t.view, t.layout};
  return View{};
}

bool finite(const float* f, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (!(f[i] > -1e30f && f[i] < 1e30f)) return false;
  return true;
}

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

// Families whose radiance is more than the generic colour x intensity x glow map (x base
// colour for BGM), decompiled from the captures (offsets in the material CB, set 1 b0):
//   222986b8 Base_GM_VT: mix(c * glow.r, glow.rgb, floor(m1)) * m2
//   159a6211 Base_GM_Pulse_VT: mix(c * glow.r, glow.rgb, floor(m4)) * m5
//            * mix(m2, m3, cos(time * m1) * 0.5 + 0.5) * global[0]   (time: set 0 b0 [0])
//   aab75fed ENV_Tileset_MultipleTextures_BGM_MaskedShadows:
//            mix(mix(b2(uv m0), b3(uv m1), vc.x), b4(uv m2), vc.y).rgb * glow.r * m3 * (1 - max(vc.x, vc.y))
//            The vertex colour vc is not captured: vc = 0 (the base map b2 at uv * m0) is taken,
//            the upper bound of the game's value (flagged approximate).
//   GM_Pulse (159a6211 _VT, 304be1c7 _FX, their aliases): the same constant buffer; only
//            the luminance-emissive ones (_VT) multiply by the global scale.
//   1a81f41b FLUID_Lava_Plane_Flowmap: blackbody(T) * m23 * (1, 2.5, 0), the LUT at set 0
//            b11 at u = (m0 + (m1 - m0) * h * m22) / 12000, h a heat field from flow noise,
//            vertex colour and distance. h is not reproduced: a constant rt_em_lava_heat
//            (0.5) is taken and the scale is left to the on-screen calibration.
//   The Pulse templates differ in where their constants are: 159a6211 _VT / 304be1c7 _FX at
//   set 1 b0 with the texture-colour switch at +24 and the intensity at +28; a201d825
//   _AlphaBlend at set 1 b1, intensity at +24 and no switch. The pulse terms are +12/+16/+20
//   of the material CB in all of them: the DB's material CB binding and intensity offset
//   say which layout a shader has.
void family_fixup(VkCommandBuffer cmd, uint64_t fs, const Mat& mat, EmRec& r, PDraw& d) {
  const uint32_t fam = uint32_t(fs >> 32);
  const std::string& name = mat.name;
  const uint32_t cbs = mat.info.material_cb_set, cbb = mat.info.material_cb_binding;
  const bool cb_ok = cbs < 8 && cbb < 64;
  float m[6] = {};
  const bool pulse = fam == 0x159a6211u || name.find("_GM_Pulse") != std::string::npos;
  if (fam == 0x1a81f41bu) {
    float lv[24] = {};
    if (read_bound(cmd, 1, 0, 0, lv, 96) && finite(lv, 24)) {
      d.mask = bound_view(cmd, 0, 11);   // the blackbody LUT
      r.mode = d.mask.v ? kModeBlackbody : kModeConst;
      r.uv[0] = lv[0] / 12000.0f;
      r.uv[1] = (lv[1] - lv[0]) / 12000.0f * lv[22];
      r.uv[2] = cfg_float("rt_em_lava_heat", 0.5f);
      r.uv[3] = 0.0f;
      r.radiance[0] = lv[23];
      r.radiance[1] = 2.5f * lv[23];
      r.radiance[2] = 0.0f;
    }
    r.flags |= kFlagApprox;
    return;
  }
  if (fam == 0x222986b8u && read_bound(cmd, 1, 0, 0, m, 24)) {
    if (std::floor(m[3]) >= 1.0f) {
      r.mode = kModeMaskRGB;
      r.radiance[0] = r.radiance[1] = r.radiance[2] = m[4];
    }
  } else if (pulse && cb_ok && read_bound(cmd, cbs, cbb, 0, m, 24)) {
    float m45[2] = {}, time = 0.0f;
    const bool has_switch = mat.info.slot[BG3_MAT_EMISSIVE_INTENSITY].offset == 28;
    if (has_switch && read_bound(cmd, cbs, cbb, 24, m45, 8) && std::floor(m45[0]) >= 1.0f) {
      r.mode = kModeMaskRGB;
      for (float& c : r.radiance) c = m45[1];
    }
    if (read_bound(cmd, 0, 0, 0, &time, 4) && std::isfinite(time)) {
      float pulse = m[4] + (m[5] - m[4]) * (std::cos(time * m[3]) * 0.5f + 0.5f);
      if (std::isfinite(pulse) && pulse >= 0.0f) {
        for (float& c : r.radiance) c *= pulse;
        r.flags |= kFlagPulse;
      }
    }
  } else if (name.find("MutipleUV") != std::string::npos || name.find("MultipleUV") != std::string::npos) {
    // f298bf4d ENV_Base_MutipleUV: glow(uv1).rgb * noise(scrolled uv0) * 1.5 * m2 * global. The
    // glow map reads the second UV, which XFB does not capture: the map is dropped (its mean
    // and the noise's (0.5 x 1.5) go to the calibration).
    r.mode = kModeConst;
    for (float& c : r.radiance) c *= 0.75f;
    d.mask = View{};
    r.flags |= kFlagApprox;
  } else if (fam == 0xaab75fedu && read_bound(cmd, 1, 0, 0, m, 4) && std::isfinite(m[0])) {
    r.uv2[0] = r.uv2[1] = m[0];
    r.flags |= kFlagApprox;   // vertex colour
  }
}

// The record of an emissive draw (caller holds g_mu).
void make_record(VkCommandBuffer cmd, uint64_t fs, const Mat& mat, PCmd& P, PDraw& d) {
  const Bg3MaterialInfo& mi = mat.info;
  EmRec& r = d.rec;
  r = EmRec{};
  r.radiance[0] = r.radiance[1] = r.radiance[2] = 1.0f;
  r.uv[0] = r.uv[1] = 1.0f;
  r.uv2[0] = r.uv2[1] = 1.0f;
  r.fs_lo = uint32_t(fs);
  r.fs_hi = uint32_t(fs >> 32);
  r.cls = mi.cls;
  auto rd = [&](const Bg3MatSlot& s, void* dst, size_t n) {
    return s.kind >= BG3_MAT_KIND_SCALAR && s.offset != 0xFFFF && read_bound(cmd, s.set, s.binding, s.offset, dst, n);
  };
  const Bg3MatSlot& col = mi.slot[BG3_MAT_EMISSIVE_COLOR];
  const Bg3MatSlot& base = mi.slot[BG3_MAT_BASE_COLOR];
  const bool bgm = mat.name.find("BGM") != std::string::npos;   // base colour x glow map templates
  float c[3] = {1, 1, 1};
  if (col.kind == BG3_MAT_KIND_VEC3 || col.kind == BG3_MAT_KIND_VEC4) {
    if (!rd(col, c, 12)) std::memcpy(c, col.value, 12), r.flags |= kFlagApprox, ++P.census.cb_unreadable;
  } else if ((mi.flags & BG3_MAT_F_UNLIT) && (base.kind == BG3_MAT_KIND_VEC3 || base.kind == BG3_MAT_KIND_VEC4)) {
    if (!rd(base, c, 12)) std::memcpy(c, base.value, 12), r.flags |= kFlagApprox;
  } else if (!bgm) {
    r.flags |= kFlagApprox;   // the colour comes from elsewhere
  }
  float k = 1.0f;
  const Bg3MatSlot& in = mi.slot[BG3_MAT_EMISSIVE_INTENSITY];
  if (in.kind == BG3_MAT_KIND_SCALAR && !rd(in, &k, 4)) k = in.value[0], r.flags |= kFlagApprox, ++P.census.cb_unreadable;
  for (int i = 0; i < 3; ++i) r.radiance[i] = c[i] * k;
  const Bg3MatSlot& map = mi.slot[BG3_MAT_EMISSIVE_MAP];
  if (map.kind == BG3_MAT_KIND_TEXTURE) {
    d.mask = bound_view(cmd, map.set, map.binding);
    if (d.mask.v) r.mode = kModeMaskR;
    else r.flags |= kFlagApprox;
  }
  // The game's luminance scale (set 2 b0 [0], a storage buffer in device memory). The game
  // rewrites that buffer later in the same batch, so the value is copied on the GPU right
  // after this draw's pass (em_pass_done, em_copy.hlsl) into a slot of our own buffer.
  r.glob = UINT32_MAX;
  if (mi.flags & BG3_MAT_F_LUMINANCE_EMISSIVE) {
    r.flags |= kFlagLuminance;
    Bg3Binding g{};
    if (query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 2, 0, 0, &g) && g.buffer) {
      // One slot per distinct binding in the pass.
      uint32_t slot = UINT32_MAX;
      for (const auto& c : P.copies)
        if (c.first.buffer == g.buffer && c.first.offset == g.offset) slot = c.second;
      if (slot == UINT32_MAX) {
        slot = g_glob_next++ % kGlobSlots;
        P.copies.push_back({g, slot});
        if (P.copies.size() > 1) ++P.census.global_conflicts;   // (several bindings: counted, handled)
      }
      r.glob = slot;
    } else {
      r.flags |= kFlagApprox;
    }
  }
  if (bgm) {
    if (mi.flags & BG3_MAT_F_VIRTUAL_TEXTURE) {
      // The VT albedo: uv transform and tile-set index from the engine CB, the tile-set
      // constants from the SSBO at set 0 b12, indirection and cache from the DB's slots.
      const Bg3MatSlot& xf = mi.slot[BG3_MAT_VT_UV_TRANSFORM];
      const Bg3MatSlot& ix = mi.slot[BG3_MAT_VT_TILESET_INDEX];
      const Bg3MatSlot& tb = mi.slot[BG3_MAT_VT_TABLE];
      const Bg3MatSlot& al = mi.slot[BG3_MAT_VT_ALBEDO];
      float p[8] = {};
      uint32_t idx = 0;
      bool ok = xf.kind && ix.kind && tb.kind && al.kind && rd(xf, p, 32) && rd(ix, &idx, 4) && idx < (1u << 20) &&
                read_bound(cmd, 0, 12, VkDeviceSize(idx) * 128, r.vt_t, 128) && finite(p, 8) && finite(r.vt_t, 32);
      if (ok) {
        std::memcpy(r.vt_p0, p, 16);
        std::memcpy(r.vt_p1, p + 4, 16);
        d.vt_ind = bound_view(cmd, tb.set, tb.binding);
        d.vt_cache = bound_view(cmd, al.set, al.binding);
        ok = d.vt_ind.v && d.vt_cache.v;
      }
      if (ok) r.flags |= kFlagVtAlbedo;
      else ++P.census.vt_missing, r.flags |= kFlagApprox;
    } else {
      const Bg3MatSlot& bm = mi.slot[BG3_MAT_BASE_COLOR_MAP];
      if (bm.kind == BG3_MAT_KIND_TEXTURE) d.base = bound_view(cmd, bm.set, bm.binding);
      if (d.base.v) r.flags |= kFlagBaseMap;
      else r.flags |= kFlagApprox;
    }
  }
  family_fixup(cmd, fs, mat, r, d);
  if (mat.kind == kMatForward) r.flags |= kFlagForward | kFlagApprox;
  if (!finite(r.radiance, 3) || r.radiance[0] < 0 || r.radiance[1] < 0 || r.radiance[2] < 0) {
    r.radiance[0] = r.radiance[1] = r.radiance[2] = 0.0f;
    r.flags |= kFlagApprox;
  }
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
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {1, 1, 1};
  ici.mipLevels = ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (dd->vk.CreateImage(dd->device, &ici, nullptr, &out.image) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetImageMemoryRequirements(dd->device, out.image, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &out.mem) != VK_SUCCESS)
    return false;
  dd->vk.BindImageMemory(dd->device, out.image, out.mem, 0);
  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = out.image;
  vci.viewType = array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  vci.format = ici.format;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  return dd->vk.CreateImageView(dd->device, &vci, nullptr, &out.view) == VK_SUCCESS;
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
  if (!g_qp) return UINT32_MAX;
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

void noop_stamp(VkCommandBuffer, uint32_t) {}

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
  Bg3Binding global{};
  float cam[3] = {0, 0, 0};
  bool have_cam = false;
  // The scene's draws: one BLAS each (no cached meshes: nothing persists between builds).
  bvh_discard_scene(1);
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
        if (cal != g_calib.end() && cal->second.updates) {
          for (float& c : r.radiance) c *= cal->second.scale;
          r.flags |= kFlagCalibrated;
        }
      }
      r.tex = slot_of(tex, kMaxTex, d.mask);
      if (d.mask.v && !r.tex) ++census.skip_tex, r.mode = kModeConst, r.flags |= kFlagApprox;
      r.tex2 = slot_of(tex, kMaxTex, d.base);
      if (d.base.v && !r.tex2) ++census.skip_tex, r.flags = (r.flags & ~kFlagBaseMap) | kFlagApprox;
      r.vt_tex = slot_of(tex, kMaxTex, d.vt_ind);
      r.vt_cache = slot_of(cache, kMaxCache, d.vt_cache);
      if ((r.flags & kFlagVtAlbedo) && (!r.vt_tex || !r.vt_cache))
        ++census.skip_tex, r.flags = (r.flags & ~kFlagVtAlbedo) | kFlagApprox;
      CmdState cs;
      cs.vb[0] = {d.vb0, d.vb0_off, 0};
      cs.ib = d.ib;
      cs.ib_offset = d.ib_off;
      BvhDraw bd{nullptr, d.vs, d.pipe_id, false, d.args, &cs, 1};
      uint32_t token = 0, mesh_tris = 0;
      bvh_classify(cmd, bd, &token, &mesh_tris);
      bvh_captured(token, d.off, d.prims, false, UINT32_MAX);
      bvh_set_record(token, uint32_t(recs.size()));
      recs.push_back(r);
      ranges.push_back({d.off, d.prims});
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
  RtCapture cap{};
  cap.xfb = g_soup.b;
  cap.stride = g_stride;
  cap.split = true;
  cap.xfb_size = g_soup.size;
  cap.scene = 1;
  cap.converted = true;
  BvhResult res{};
  if (!bvh_build(cmd, cap, &noop_stamp, &res) || !res.tlas) {
    ts_end(cmd, tsp);
    return;
  }
  const uint32_t nrec = uint32_t(recs.size());
  if (nrec) std::memcpy(region + kOffRecs, recs.data(), size_t(nrec) * sizeof(EmRec));
  // Light ranges per instance, bounded by rt_em_max_lights (allocated at device creation):
  // past it, k consecutive triangles of an instance share one light (their summed area and
  // power, area-weighted radiance, the largest as the shape), so no emission is dropped.
  const uint32_t n = res.instances;
  uint32_t* prefix = reinterpret_cast<uint32_t*>(region + kOffPrefix);
  uint32_t total = 0, overflow = 0;
  const uint32_t cap_lights = (std::max)((std::min)(uint32_t((std::max)(cfg_int("rt_em_max_lights", 65536), 0)), g_max_lights), 1u);
  uint64_t all_tris = 0;
  for (uint32_t i = 0; i < n && i < kMaxInst; ++i) all_tris += res.info_host[i * 4 + 3];
  uint32_t k = uint32_t((std::max)((all_tris + cap_lights - 1) / cap_lights, uint64_t(1)));
  for (;;) {   // per-instance rounding may still overshoot: widen the clusters until it fits
    uint64_t need = 0;
    for (uint32_t i = 0; i < n && i < kMaxInst; ++i) need += (res.info_host[i * 4 + 3] + k - 1) / k;
    if (need <= cap_lights || k > 4096) break;
    ++k;
  }
  for (uint32_t i = 0; i < n && i < kMaxInst; ++i) {
    uint32_t tris = res.info_host[i * 4 + 3];
    uint32_t take = (std::min)((tris + k - 1) / k, cap_lights - total);
    prefix[i * 2] = total;
    prefix[i * 2 + 1] = take;
    total += take;
    overflow += tris - (std::min)(take * k, tris);
  }
  for (uint32_t i = kMaxInst; i < n; ++i) overflow += res.info_host[i * 4 + 3];
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
  wa.pAccelerationStructures = &res.tlas;
  VkDescriptorBufferInfo info{res.info ? res.info : g_dummy.b, res.info ? res.info_off : 0, res.info ? res.info_bytes : 256};
  VkDescriptorBufferInfo pool = res.pool && res.pool_bytes ? VkDescriptorBufferInfo{res.pool, 0, res.pool_bytes}
                                                           : VkDescriptorBufferInfo{g_dummy.b, 0, 256};
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
    VkDescriptorBufferInfo instb{res.ring, res.inst_off, VkDeviceSize((std::max)(ninst, 1u)) * 64};
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
  for (uint32_t i = 0; i < nrec; ++i) {
    si.rec_fs[i] = (uint64_t(recs[i].fs_hi) << 32) | recs[i].fs_lo;
    si.rec_flags[i] = recs[i].flags;
  }
  si.lights = total;
  si.cluster = k;
  si.overflow = overflow;
  si.instances = n;
  si.groups = (std::min)((total + 255) / 256, 1024u);
  si.census = census;
  si.tris = res.static_tris + res.dynamic_tris;
  g_current = int(slot);
  g_info = Bg3EmissiveInfo{now, n, nrec, total, pre, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, 0, 0};
  if (cfg_int("rt_em_log", 0))
    log("[em] build frame %llu: %u effects passes, %u draws (%u emissive, %u captured), %u records, %u textures, %u "
        "caches, %.0f triangles, %u lights (%u left out), global scale %s",
        (unsigned long long)now, census.passes, census.draws, census.kinds[kMatEmissive], census.captured, nrec,
        unsigned(tex.size()), unsigned(cache.size()), double(si.tris), total, overflow, global.buffer ? "bound" : "none");
}

}  // namespace

// ================= entry points =================

void em_device_created(DeviceData* dd) {
  if (g_dd) return;
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
  VkDescriptorSetLayoutBinding b[12] = {
      {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {6, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTex, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {7, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &g_sampler},
      {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {11, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxCache, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
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
  ok = ok && make_buf(g_globals, VkDeviceSize(kGlobSlots) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
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
      ok ? "available" : "UNAVAILABLE", cfg_int("rt_emissive", 1), (unsigned long long)soup_mb, g_max_lights,
      (unsigned long long)((VkDeviceSize(g_max_lights) * 80) >> 20), unsigned((kRegion * kRing) >> 10));
}

void em_device_destroyed(DeviceData* dd) {
  if (dd != g_dd) return;
  std::lock_guard<std::mutex> lock(g_mu);
  const VkDeviceFuncs& vk = dd->vk;
  for (Own& o : g_own)
    if (o.fence) {
      if (o.used) vk.WaitForFences(dd->device, 1, &o.fence, VK_TRUE, 1000000000ull);
      vk.DestroyFence(dd->device, o.fence, nullptr);
      o = Own{};
    }
  if (g_cmd_pool) vk.DestroyCommandPool(dd->device, g_cmd_pool, nullptr);
  g_cmd_pool = VK_NULL_HANDLE;
  for (Buf* b : {&g_soup, &g_lights, &g_host, &g_dummy, &g_globals}) free_buf(*b);
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

// Needs RT and the BVH split's machinery (bvhsplit.cpp), not rt_bvh_split itself: the
// emissive scene keeps no cached meshes and rebuilds all its BLAS every build.
bool em_wanted() { return g_on && rt_enabled() && bvh_available() && cfg_int("rt_emissive", 1) != 0; }

bool em_is_fx_pass(const Bg3PassInfo& p, uint32_t gw, uint32_t gh) {
  return g_on && p.color_count == 1 && p.color_formats[0] == VK_FORMAT_B10G11R11_UFLOAT_PACK32 &&
         p.depth_format == VK_FORMAT_D24_UNORM_S8_UINT && gw && p.width == gw && p.height == gh;
}

bool em_pass_begin(VkCommandBuffer cmd, const CmdState* s, int* ts) {
  (void)s;
  *ts = -1;
  t_pass_fx = false;
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

bool em_draw(VkCommandBuffer cmd, const CmdState* s, const DrawArgs& a, const EmDrawIn& in, VkBuffer* soup,
             VkDeviceSize* off, VkDeviceSize* bytes, uint32_t* prims_out) {
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
  const bool backdrop = mat.kind == kMatBackdrop && cfg_int("rt_em_backdrop", 0) != 0;
  // rt_em_decal=1 (a measurement only: a decal's box is not the surface it lights): emissive
  // decals are captured, so the check counts the pixels they light as covered "in front".
  const bool decal = mat.kind == kMatDecal && cfg_int("rt_em_decal", 0) != 0;
  if (mat.kind != kMatEmissive && mat.kind != kMatForward && !backdrop && !decal) return false;
  FsCount& fc = g_fs_window[s->fs];
  ++fc.draws;
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
  if (g_soup_head + need > g_soup.size) g_soup_head = 0;
  PDraw d;
  make_record(cmd, s->fs, mat, P, d);
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
      uint32_t pc[4] = {slot, 0, 0, 0};
      g_dd->vk.CmdPushConstants(cmd, g_cp_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
      g_dd->vk.CmdDispatch(cmd, 1, 1, 1);
    }
    barrier(cmd, kCS, VK_ACCESS_SHADER_WRITE_BIT, kCS | kAS, VK_ACCESS_SHADER_READ_BIT);
    if (ranges.empty()) restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  }
  if (!ranges.empty()) {
    barrier(cmd, kXFB, VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT, kCS | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    for (const auto& [off, tris] : ranges) {
      const VkDeviceSize bytes = VkDeviceSize(tris) * 3 * stride;
      if (ubo) {
        RtCapture rc{};
        rc.xfb = g_soup.b;
        rc.alpha_base = off;
        rc.alpha_bytes = bytes;
        rc.stride = stride;
        rc.ubo = ubo;
        rc.ubo_offset = ubo_off;
        rt_convert(cmd, rc);
      } else {
        g_dd->vk.CmdFillBuffer(cmd, g_soup.b, off, bytes, 0);   // no camera: collapsed (never hit)
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
  if (o.used && dd->vk.WaitForFences(dd->device, 1, &o.fence, VK_TRUE, 0) != VK_SUCCESS) {
    ++g_busy;
    return;
  }
  ++g_own_next;
  dd->vk.ResetFences(dd->device, 1, &o.fence);
  dd->vk.ResetCommandBuffer(o.cb, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  dd->vk.BeginCommandBuffer(o.cb, &bi);
  // Everything the game's batch wrote (effects-pass XFB, conversions) before our reads.
  barrier(o.cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
  build(o.cb, taken);
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
      free_buf(g_soup);   // deferred: command buffers in flight still write and read it
      g_soup = nb;
      g_soup_head = 0;
      // Defined contents from the start (only ranges the capture wrote are ever built; this
      // is the second line of defence).
      dd->vk.CmdFillBuffer(o.cb, g_soup.b, 0, g_soup.size, 0);
    }
  }
  g_demand = 0;
  dd->vk.EndCommandBuffer(o.cb);
  // The game's images our build samples (masks, base maps, VT caches) stay alive until it
  // ran: register what the game's command buffers referenced on ours too (tracker.cpp).
  if (CmdState* ms = cmd_state(o.cb, true)) ms->refs = std::move(refs);
  std::vector<VkCommandBuffer> mine{o.cb};
  bvh_on_submit(mine);
  life_on_submit(queue, mine);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &o.cb;
  VkResult r = dd->vk.QueueSubmit(queue, 1, &si, o.fence);   // the caller holds the queue lock
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

void em_on_present() {
  if (!g_on) return;
  const uint64_t now = frame_index();
  if (g_qp) {
    std::lock_guard<std::mutex> lock(g_ts_mu);
    for (uint32_t p = 0; p < kTsPairs; ++p) {
      TsPair& t = g_ts[p];
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
  std::lock_guard<std::mutex> lock(g_mu);
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
    const uint32_t* st = reinterpret_cast<const uint32_t*>(region + kOffStats);
    if (st[6]) {
      ++w.dbg_frames;
      w.dbg_depth += st[0];
      w.dbg_hit += st[1];
      w.dbg_miss += st[2];
      w.dbg_dark += st[3];
      w.dbg_bright += st[7];
      w.dbg_front += st[4];
      for (uint32_t r = 0; r < si.rec_fs.size(); ++r) {
        const uint32_t* q = st + 16 + r * 4;
        if (!q[2]) continue;
        Window::Cmp& cmp = w.cmp[si.rec_fs[r]];
        cmp.ours += q[0] / 16.0;
        cmp.game += q[1] / 16.0;
        cmp.px += q[2];
        cmp.flags |= si.rec_flags[r];
      }
    }
    if (cfg_int("rt_em_log", 0))
      log("[em] frame %llu: %u emissive draws, %.0f triangles, %u lights, total power %.4g (effects-target units x m^2)",
          (unsigned long long)(si.frame - 1), si.census.kinds[kMatEmissive], double(si.tris), si.lights, power);
    si.frame = 0;
  }
  const int every = (std::max)(cfg_int("rt_timing_frames", 300), 30);
  Window& w = g_win;
  if (w.builds < uint64_t(every)) return;
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
  log("[em] last %llu builds, per build: %.1f effects passes, %.1f draws in them (engine %.1f, unknown %.1f, decal %.1f, "
      "backdrop %.1f, other materials %.1f); emissive %.1f, captured %.1f; left out: no XFB %.2f, indirect %.2f, "
      "topology %.2f, soup full %.2f, records full %.2f, textures full %.2f; VT albedo missing %.2f, global scale "
      "conflicts %.2f; command buffers re-recorded unsubmitted %u, builds skipped (busy) %u",
      (unsigned long long)w.builds, c.passes / nb, c.draws / nb, c.kinds[kMatEngine] / nb, c.kinds[kMatUnknown] / nb,
      c.kinds[kMatDecal] / nb, c.kinds[kMatBackdrop] / nb, c.kinds[kMatOther] / nb, c.kinds[kMatEmissive] / nb,
      c.captured / nb, c.skip_xfb / nb, c.skip_indirect / nb, c.skip_topo / nb, c.skip_full / nb, c.skip_recs / nb,
      c.skip_tex / nb, c.vt_missing / nb, c.global_conflicts / nb, g_stale, g_busy);
  log("[em] soup %llu MB, %u draws dropped by a soup resize; forward-pass emissive draws %.1f per build; triangles per "
      "light %u (last build); invalid soup triangles collapsed by the sanitize pass %.1f per build; material constants "
      "not host-readable %.2f per build",
      (unsigned long long)(g_soup.size >> 20), g_regrown, c.kinds[kMatForward] / nb, g_last_cluster,
      double(w.collapsed) / nb, c.cb_unreadable / nb);
  log("[em] per build: %.1f TLAS instances, %.0f triangles, %.0f lights (%.0f left out by rt_em_max_lights), total power "
      "%.4g (effects-target units x m^2; last build %.4g, %u draws, %u lights), %llu non-finite power sums; GPU: build "
      "median %.3f ms (p99 %.3f, %zu samples), effects passes per frame median %.3f ms with capture (%zu frames), %.3f ms "
      "without (%zu frames)",
      w.instances / nb, w.tris / nb, w.lights / nb, w.overflow / nb, w.power / nb, w.last_power, w.last_draws,
      w.last_lights, (unsigned long long)w.nonfinite, pct(w.build_ms, 0.5), pct(w.build_ms, 0.99), w.build_ms.size(),
      pct(fx[1], 0.5), fx[1].size(), pct(fx[0], 0.5), fx[0].size());
  {
    std::string line;
    int shown = 0;
    std::vector<std::pair<uint32_t, uint64_t>> order;
    for (auto& [fs, fc] : g_fs_window) order.push_back({fc.draws, fs});
    std::sort(order.rbegin(), order.rend());
    for (auto& [d, fs] : order) {
      if (shown++ >= 16) break;
      const FsCount& fc = g_fs_window[fs];
      char buf[256];
      snprintf(buf, sizeof buf, "%s %08x %s %.1f/%.1f", line.empty() ? "" : ";", unsigned(fs >> 32),
               material(fs).name.c_str(), fc.draws / nb, fc.captured / nb);
      line += buf;
    }
    if (!line.empty()) log("[em] emissive families (draws/captured per build):%s", line.c_str());
  }
  if (w.dbg_frames) {
    double ours = 0, game = 0;
    std::string line;
    const bool calib = cfg_int("rt_em_calib", 1) != 0;
    for (auto& [fs, cm] : w.cmp) {
      ours += cm.ours, game += cm.game;
      const double ratio = cm.game > 0 ? cm.ours / cm.game : 0.0;
      const double px = cm.px / double(w.dbg_frames);
      Calib& cal = g_calib[fs];
      const float before = cal.updates ? cal.scale : 1.0f;
      // Calibrate approximate families seen on enough pixels, in the effects target (a
      // forward family's emission is not there: ours >> game).
      if (calib && (cm.flags & kFlagApprox) && !(cm.flags & kFlagForward) && px >= 200 && ratio > 0.02 && ratio < 50.0) {
        double step = (std::min)((std::max)(1.0 / ratio, 0.25), 4.0);   // the full correction, at most 4x per window
        cal.scale = float((std::min)((std::max)(double(before) * step, 1.0 / 64), 64.0));
        ++cal.updates;
      }
      char buf[260];
      snprintf(buf, sizeof buf, "%s %08x %s %.3f (%.0f px%s%s, scale %.3f)", line.empty() ? "" : ";", unsigned(fs >> 32),
               material(fs).name.c_str(), ratio, px, (cm.flags & kFlagApprox) ? ", approx" : "",
               (cm.flags & kFlagForward) ? ", forward" : "", before);
      line += buf;
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
    if (!line.empty()) log("[em] ours/game per family:%s", line.c_str());
  }
  g_win = Window{};
  g_fs_window.clear();
  g_stale = g_busy = g_regrown = 0;
}

}  // namespace bg3
