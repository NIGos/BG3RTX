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
//          (em_is_fx_pass). There are several such passes per frame, each in its own
//          command buffer (foundry units 52-59);
//   draw:  its fragment shader is, in the asset DB (docs/ASSET_DB.md), a material's EMI
//          variant (pass 2). Decals (box volumes, not surfaces) and skydome backdrops
//          are counted, not captured. Engine and unknown shaders (sky, clouds, magic
//          VFX not in the database) are counted.
//   constants: the material's GlowColour/GlowMultiplier (asset DB roles, read from the
//          game's mapped constant buffer at record time), the glow map (the bound view
//          at the role's binding) and, for luminance-emissive materials, the global
//          scale at set 2 binding 0 [0]. Two families with a texture-colour switch and a
//          pulse are decoded exactly (family_fixup).
//
// Geometry: every vertex shader is already patched for transform feedback. An emissive
// draw is classified by the BVH split as scene 1 (bvhsplit.cpp): a cached instanced
// mesh contributes its instances from the game's instance stream; everything else is
// captured into the emissive soup and gets a per-draw BLAS. The soup is converted to
// camera-relative world after each effects pass, in its own command buffer, with that
// pass's camera UBO.
//
// Build: at the frame's first tiled-lighting dispatch (em_dispatch_pre), bvh_build
// (scene 1 or 2) builds the per-draw BLAS, new meshes and the emissive TLAS from the
// draws recorded during the previous frame index, then em_lights.hlsl writes the light
// list. BG3 records a frame's effects passes before the previous present and may record
// the next frame's before this lighting: those must wait for the next build (their
// transform feedback runs after this one on the GPU; including them lost the device).
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
constexpr uint32_t kMaxRecs = 1024;      // emissive draws per build
constexpr uint32_t kMaxTex = 256;        // mask textures per build (slot 0: dummy)
constexpr uint32_t kMaxInst = 16384;     // emissive TLAS instances that get lights
constexpr uint32_t kHeaderWords = 16 + 1024;
constexpr uint32_t kStatWords = 16 + kMaxRecs * 4;
constexpr VkDeviceSize up256(VkDeviceSize v) { return (v + 255) / 256 * 256; }
constexpr VkDeviceSize kOffRecs = 0;
constexpr VkDeviceSize kOffPrefix = up256(kOffRecs + VkDeviceSize(kMaxRecs) * 64);
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
  uint32_t draw, cls;
  float pad[2];
};
static_assert(sizeof(EmRec) == 64, "EmRec must match em_common.hlsli");
enum : uint32_t { kModeConst = 0, kModeMaskR = 1, kModeMaskRGB = 2 };
enum : uint32_t { kFlagLuminance = 1, kFlagApprox = 2, kFlagPulse = 4 };

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
enum MatKind : uint8_t { kMatEngine, kMatUnknown, kMatEmissive, kMatDecal, kMatBackdrop, kMatOther, kMatKinds };
const char* kMatKindName[kMatKinds] = {"engine", "unknown", "emissive", "decal", "backdrop", "other"};
struct Mat {
  MatKind kind = kMatUnknown;
  Bg3MaterialInfo info{};
  std::string name;
};
std::unordered_map<uint64_t, Mat> g_mats;

// ---- this generation (draws recorded since the last build) ----
Buf g_soup;                 // XFB output, two halves (generation parity)
uint32_t g_stride = 24;
struct Census {
  uint32_t passes = 0, draws = 0, kinds[kMatKinds] = {}, captured = 0, cached = 0, late = 0;
  uint32_t skip_xfb = 0, skip_indirect = 0, skip_topo = 0, skip_full = 0, skip_recs = 0, skip_tex = 0, no_ubo = 0;
  uint64_t captured_tris = 0;
};
struct FsCount { uint32_t draws = 0, captured = 0, cached = 0; };
std::map<uint64_t, FsCount> g_fs_window;   // per fragment shader over the log window
uint32_t g_stale = 0;       // generations dropped because no build consumed them

// The effects target, as last seen.
struct Fx { VkImage image = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; uint32_t w = 0, h = 0; uint64_t frame = 0; };

// A generation: the draws recorded during one frame index. BG3 records a frame's
// effects passes before the previous frame's present, and the next frame's ones can
// be recorded before this frame's lighting: a build at frame index N therefore takes
// only the draws recorded at N - 1 (their command buffers are submitted no later than
// the lighting's, and earlier in it), never those of N. Two generations alternate by
// frame parity, each with its own half of the soup and its own BVH split scene (1 + parity).
struct Gen {
  std::vector<EmRec> recs;
  std::vector<std::pair<VkImageView, VkImageLayout>> tex;
  Census census;
  uint64_t frame = 0;        // frame index the draws were recorded at (0: empty)
  VkDeviceSize cursor = 0;   // in this generation's half of the soup
  Fx fx;                     // its effects target
};
Gen g_gens[2];
Gen* g_g = &g_gens[0];       // the generation being recorded into (caller holds g_mu)

// ---- per build ----
VkDescriptorSetLayout g_dsl = VK_NULL_HANDLE, g_push_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_pl = VK_NULL_HANDLE;
VkPipeline g_pipe = VK_NULL_HANDLE;
uint64_t g_pipe_gen = 0;
VkDescriptorPool g_pool = VK_NULL_HANDLE;
VkDescriptorSet g_sets[kRing] = {};
bool g_set_init[kRing] = {};
uint32_t g_set_tex[kRing] = {};
VkSampler g_sampler = VK_NULL_HANDLE;
Buf g_host;                 // kRing regions: records, prefix, header, statistics
Buf g_lights;               // EmLight[g_max_lights]
Buf g_dummy;
uint32_t g_max_lights = 0;
struct DummyImg { VkImage image = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; bool ready = false; };
DummyImg g_dimg;
uint32_t g_next_slot = 0;
int g_current = -1;
Bg3EmissiveInfo g_info{};
std::atomic<uint64_t> g_build_frame{UINT64_MAX};
struct SlotInfo {
  uint64_t frame = 0;                  // build frame + 1 (0: free)
  std::vector<uint64_t> rec_fs;        // fragment shader per record
  uint32_t lights = 0, overflow = 0, instances = 0, groups = 0;
  Census census;
  uint64_t static_tris = 0, dynamic_tris = 0;
};
SlotInfo g_slots[kRing];

// The pass being recorded on this thread drew an EMI material (it is an effects pass).
thread_local bool t_pass_fx = false;

// lighting recognition (cs hash -> is the tiled lighting)
std::mutex g_cs_mu;
std::unordered_map<uint64_t, bool> g_cs_lighting;

// ---- timing ----
VkQueryPool g_qp = VK_NULL_HANDLE;
float g_ts_period = 1.0f;
std::mutex g_ts_mu;
struct TsPair { uint64_t frame = 0; uint8_t kind = 0; bool capture = false; };   // kind 0 effects pass, 1 build
TsPair g_ts[kTsPairs];
uint32_t g_ts_next = 0;

// ---- window statistics (logged every rt_timing_frames builds) ----
struct Window {
  uint64_t builds = 0, read = 0;
  double draws = 0, emissive = 0, captured = 0, cached = 0, instances = 0, tris = 0, lights = 0, overflow = 0, power = 0;
  double passes = 0, late = 0, decal = 0, backdrop = 0, engine = 0, unknown = 0, other = 0;
  double skip_xfb = 0, skip_indirect = 0, skip_topo = 0, skip_full = 0, skip_recs = 0, skip_tex = 0;
  std::vector<float> build_ms, fx_ms[2];   // fx: [no capture, capture], per frame (sum of the effects passes)
  std::map<uint64_t, double> fx_frame_ms[2];
  uint64_t nonfinite = 0;
  // projected comparison (rt_debug 6-8), per fragment shader: ours, game, pixels
  struct Cmp { double ours = 0, game = 0, px = 0; };
  std::map<uint64_t, Cmp> cmp;
  uint64_t dbg_frames = 0, dbg_depth = 0, dbg_hit = 0, dbg_miss = 0, dbg_dark = 0, dbg_bright = 0;
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
      if (!emi) m.kind = kMatOther;
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

uint32_t tex_slot(VkImageView v, VkImageLayout l) {
  for (size_t i = 0; i < g_g->tex.size(); ++i)
    if (g_g->tex[i].first == v) return uint32_t(i + 1);
  if (g_g->tex.size() + 1 >= kMaxTex) return 0;
  g_g->tex.push_back({v, l});
  return uint32_t(g_g->tex.size());
}

// Families whose radiance is more than colour x intensity x mask.r (decompiled from
// the captures; the offsets are of the material constant buffer, set 1 binding 0).
//   222986b8 Base_GM_VT:  mix(c * tex.r, tex.rgb, floor(m1)) * m2
//   159a6211 Base_GM_Pulse_VT: mix(c * tex.r, tex.rgb, floor(m4)) * m5
//            * mix(m2, m3, cos(time * m1) * 0.5 + 0.5) * global   (time: set 0 b0 [0])
void family_fixup(VkCommandBuffer cmd, uint64_t fs, EmRec& r) {
  const uint32_t fam = uint32_t(fs >> 32);
  float m[6] = {};
  if (fam == 0x222986b8u && read_bound(cmd, 1, 0, 0, m, 24)) {
    if (std::floor(m[3]) >= 1.0f) {
      r.mode = kModeMaskRGB;
      r.radiance[0] = r.radiance[1] = r.radiance[2] = m[4];
    }
  } else if (fam == 0x159a6211u && read_bound(cmd, 1, 0, 0, m, 24)) {
    float m45[2] = {}, time = 0.0f;
    read_bound(cmd, 1, 0, 24, m45, 8);
    if (std::floor(m45[0]) >= 1.0f) {
      r.mode = kModeMaskRGB;
      for (float& c : r.radiance) c = m45[1];
      float g = 1.0f;
      if ((r.flags & kFlagLuminance) && read_bound(cmd, 2, 0, 0, &g, 4) && g > 0.0f && g < 1e6f)
        for (float& c : r.radiance) c *= g;
    }
    if (read_bound(cmd, 0, 0, 0, &time, 4) && std::isfinite(time)) {
      float pulse = m[4] + (m[5] - m[4]) * (std::cos(time * m[3]) * 0.5f + 0.5f);
      if (std::isfinite(pulse) && pulse >= 0.0f) {
        for (float& c : r.radiance) c *= pulse;
        r.flags |= kFlagPulse;
      }
    }
  }
}

// The record of an emissive draw (caller holds g_mu); false: no room.
bool make_record(VkCommandBuffer cmd, uint64_t fs, const Mat& mat, uint32_t* index) {
  if (g_g->recs.size() >= kMaxRecs) {
    ++g_g->census.skip_recs;
    return false;
  }
  const Bg3MaterialInfo& mi = mat.info;
  EmRec r{};
  r.radiance[0] = r.radiance[1] = r.radiance[2] = 1.0f;
  r.uv[0] = r.uv[1] = 1.0f;
  r.fs_lo = uint32_t(fs);
  r.fs_hi = uint32_t(fs >> 32);
  r.cls = mi.cls;
  r.draw = uint32_t(g_g->recs.size());
  auto rd = [&](const Bg3MatSlot& s, float* dst, size_t n) {
    return s.kind >= BG3_MAT_KIND_SCALAR && s.kind <= BG3_MAT_KIND_VEC4 && s.offset != 0xFFFF &&
           read_bound(cmd, s.set, s.binding, s.offset, dst, n * 4);
  };
  const Bg3MatSlot& col = mi.slot[BG3_MAT_EMISSIVE_COLOR];
  const Bg3MatSlot& base = mi.slot[BG3_MAT_BASE_COLOR];
  float c[3] = {1, 1, 1};
  if (col.kind == BG3_MAT_KIND_VEC3 || col.kind == BG3_MAT_KIND_VEC4) {
    if (!rd(col, c, 3)) std::memcpy(c, col.value, 12), r.flags |= kFlagApprox;
  } else if ((mi.flags & BG3_MAT_F_UNLIT) && (base.kind == BG3_MAT_KIND_VEC3 || base.kind == BG3_MAT_KIND_VEC4)) {
    if (!rd(base, c, 3)) std::memcpy(c, base.value, 12), r.flags |= kFlagApprox;
  } else {
    r.flags |= kFlagApprox;   // the colour comes from elsewhere (base colour maps, vertex colour)
  }
  float k = 1.0f;
  const Bg3MatSlot& in = mi.slot[BG3_MAT_EMISSIVE_INTENSITY];
  if (in.kind == BG3_MAT_KIND_SCALAR && !rd(in, &k, 1)) k = in.value[0], r.flags |= kFlagApprox;
  if (mi.flags & BG3_MAT_F_LUMINANCE_EMISSIVE) {
    r.flags |= kFlagLuminance;
    float g = 1.0f;
    if (read_bound(cmd, 2, 0, 0, &g, 4) && g > 0.0f && g < 1e6f) k *= g;
    else r.flags |= kFlagApprox;
  }
  for (int i = 0; i < 3; ++i) r.radiance[i] = c[i] * k;
  const Bg3MatSlot& map = mi.slot[BG3_MAT_EMISSIVE_MAP];
  if (map.kind == BG3_MAT_KIND_TEXTURE) {
    Bg3Binding t;
    if (query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, map.set, map.binding, 0, &t) && t.view) {
      r.tex = tex_slot(t.view, t.layout);
      if (r.tex) r.mode = kModeMaskR;
      else ++g_g->census.skip_tex, r.flags |= kFlagApprox;
    } else {
      r.flags |= kFlagApprox;
    }
  }
  family_fixup(cmd, fs, r);
  if (!finite(r.radiance, 3) || r.radiance[0] < 0 || r.radiance[1] < 0 || r.radiance[2] < 0) {
    r.radiance[0] = r.radiance[1] = r.radiance[2] = 0.0f;
    r.flags |= kFlagApprox;
  }
  *index = uint32_t(g_g->recs.size());
  g_g->recs.push_back(r);
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

bool make_dummy_image() {
  DeviceData* dd = g_dd;
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {1, 1, 1};
  ici.mipLevels = ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (dd->vk.CreateImage(dd->device, &ici, nullptr, &g_dimg.image) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  dd->vk.GetImageMemoryRequirements(dd->device, g_dimg.image, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = find_memory_type(dd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX || dd->vk.AllocateMemory(dd->device, &mai, nullptr, &g_dimg.mem) != VK_SUCCESS)
    return false;
  dd->vk.BindImageMemory(dd->device, g_dimg.image, g_dimg.mem, 0);
  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = g_dimg.image;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = ici.format;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  return dd->vk.CreateImageView(dd->device, &vci, nullptr, &g_dimg.view) == VK_SUCCESS;
}

// The dummy mask is white (mask 1), filled once in the first build's command buffer.
void dummy_ready(VkCommandBuffer cmd) {
  if (g_dimg.ready) return;
  VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  ib.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  ib.image = g_dimg.image;
  ib.subresourceRange = range;
  g_dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                              nullptr, 1, &ib);
  VkClearColorValue one{{1, 1, 1, 1}};
  g_dd->vk.CmdClearColorImage(cmd, g_dimg.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &one, 1, &range);
  g_dd->vk.CmdFillBuffer(cmd, g_dummy.b, 0, g_dummy.size, 0);
  ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  ib.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  ib.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  g_dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kCS, 0, 1, &mb, 0, nullptr, 1, &ib);
  g_dimg.ready = true;
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

bool is_lighting(VkCommandBuffer cmd, uint64_t cs) {
  {
    std::lock_guard<std::mutex> lock(g_cs_mu);
    auto it = g_cs_lighting.find(cs);
    if (it != g_cs_lighting.end()) return it->second;
  }
  // The tiled lighting's light list (assetdb.cpp assetdb_dispatch_pre): set 1 b25
  // 80-byte lights, b23/b24 froxel lists, b13 scale; set 0 b2 the camera.
  Bg3Binding b25{}, b23{}, b24{}, b13{}, cam{};
  auto q = [&](uint32_t set, uint32_t b, Bg3Binding& o) {
    return query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, set, b, 0, &o) && o.buffer && o.range > 0 &&
           o.range != VK_WHOLE_SIZE;
  };
  bool yes = q(1, 25, b25) && b25.range % 80 == 0 && b25.range >= 80 * 256 && q(0, 2, cam) && cam.range >= 224 &&
             q(1, 13, b13) && q(1, 23, b23) && q(1, 24, b24);
  std::lock_guard<std::mutex> lock(g_cs_mu);
  g_cs_lighting[cs] = yes;
  if (yes) log("[em] tiled lighting recognised: cs %016llx (the emissive scene is built before its first dispatch)",
               (unsigned long long)cs);
  return yes;
}

void noop_stamp(VkCommandBuffer, uint32_t) {}

// Starts a new generation; its draws, records and views are dropped (caller holds g_mu).
// A generation must not outlive its frame: its views, instance streams and soup belong
// to it (during loading screens the effects passes run without a lighting dispatch).
void reset_gen(int parity) {
  Gen& g = g_gens[parity];
  g.recs.clear();
  g.tex.clear();
  g.census = Census{};
  g.frame = 0;
  g.cursor = 0;
  g.fx = Fx{};
  bvh_discard_scene(1 + parity);
}

// Selects this frame index's generation for recording; an older one there was never
// consumed by a build (caller holds g_mu).
void select_gen() {
  const uint64_t now = frame_index();
  const int p = int(now & 1);
  g_g = &g_gens[p];
  if (g_g->frame != now) {
    if (g_g->frame) ++g_stale, reset_gen(p);
    g_g->frame = now;
  }
}

// Caller holds g_mu.
void build(VkCommandBuffer cmd) {
  const uint64_t now = frame_index();
  const DeviceData* dd = g_dd;
  // The previous frame index's generation (see Gen). This index's draws, if any, are
  // the next build's ("pending"); an older one was never consumed and is dropped.
  const int q = int((now & 1) ^ 1);
  Gen& G = g_gens[q];
  if (G.frame && G.frame + 1 != now) ++g_stale, reset_gen(q);
  std::vector<EmRec> recs;
  recs.swap(G.recs);
  std::vector<std::pair<VkImageView, VkImageLayout>> tex;
  tex.swap(G.tex);
  Census census = G.census;
  census.late = g_gens[q ^ 1].frame == now ? g_gens[q ^ 1].census.draws : 0;
  const Fx fx = G.fx;
  struct Done {   // the generation is consumed, whatever happens below
    int q;
    ~Done() { reset_gen(q); }
  } done{q};
  if (!ensure_pipeline()) return;
  const uint32_t tsp = ts_pair(cmd, 1, true);
  dummy_ready(cmd);

  RtCapture cap{};
  cap.xfb = g_soup.b;
  cap.stride = g_stride;
  cap.split = true;
  cap.xfb_size = g_soup.size;
  cap.scene = 1 + q;
  cap.converted = true;
  BvhResult res{};
  if (!bvh_build(cmd, cap, &noop_stamp, &res) || !res.tlas) {
    restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    ts_end(cmd, tsp);
    return;
  }
  const uint32_t slot = g_next_slot++ % kRing;
  uint8_t* region = g_host.map + VkDeviceSize(slot) * kRegion;
  const VkDeviceSize rbase = VkDeviceSize(slot) * kRegion;
  // Records.
  const uint32_t nrec = uint32_t((std::min)(recs.size(), size_t(kMaxRecs)));
  if (nrec) std::memcpy(region + kOffRecs, recs.data(), size_t(nrec) * sizeof(EmRec));
  // Light ranges per instance (bounded: rt_em_max_lights, allocated at device creation).
  const uint32_t n = res.instances;
  uint32_t* prefix = reinterpret_cast<uint32_t*>(region + kOffPrefix);
  uint32_t total = 0, overflow = 0;
  const uint32_t cap_lights = (std::min)(uint32_t((std::max)(cfg_int("rt_em_max_lights", 65536), 0)), g_max_lights);
  for (uint32_t i = 0; i < n && i < kMaxInst; ++i) {
    uint32_t tris = res.info_host[i * 4 + 3];
    uint32_t take = (std::min)(tris, cap_lights - total);
    prefix[i * 2] = total;
    prefix[i * 2 + 1] = take;
    total += take;
    overflow += tris - take;
  }
  for (uint32_t i = kMaxInst; i < n; ++i) overflow += res.info_host[i * 4 + 3];
  const uint32_t ninst = (std::min)(n, kMaxInst);
  uint32_t* header = reinterpret_cast<uint32_t*>(region + kOffHeader);
  std::memset(header, 0, size_t(kHeaderWords) * 4);
  const float pre = rrpost_pre_exposure();
  header[0] = total;
  header[1] = overflow;
  header[2] = n;
  header[3] = nrec;
  std::memcpy(&header[4], &pre, 4);
  header[5] = uint32_t(now);
  std::memset(region + kOffStats, 0, size_t(kStatWords) * 4);

  // The set.
  VkDescriptorSet set = g_sets[slot];
  VkWriteDescriptorSetAccelerationStructureKHR wa{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
  wa.accelerationStructureCount = 1;
  wa.pAccelerationStructures = &res.tlas;
  VkDescriptorBufferInfo info{res.info ? res.info : g_dummy.b, res.info ? res.info_off : 0, res.info ? res.info_bytes : 256};
  VkDescriptorBufferInfo pool = res.pool && res.pool_bytes ? VkDescriptorBufferInfo{res.pool, 0, res.pool_bytes}
                                                           : VkDescriptorBufferInfo{g_dummy.b, 0, 256};
  VkDescriptorBufferInfo soup{g_soup.b, 0, g_soup.size};
  VkDescriptorBufferInfo recb{g_host.b, rbase + kOffRecs, VkDeviceSize(kMaxRecs) * 64};
  VkDescriptorBufferInfo lights{g_lights.b, 0, g_lights.size};
  VkDescriptorBufferInfo stats{g_host.b, rbase + kOffStats, VkDeviceSize(kStatWords) * 4};
  VkDescriptorBufferInfo head{g_host.b, rbase + kOffHeader, VkDeviceSize(kHeaderWords) * 4};
  const uint32_t ntex = g_set_init[slot] ? (std::max)(g_set_tex[slot], uint32_t(tex.size())) + 1 : kMaxTex;
  g_set_init[slot] = true;
  g_set_tex[slot] = uint32_t(tex.size());
  std::vector<VkDescriptorImageInfo> ti((std::min)(ntex, kMaxTex));
  for (uint32_t i = 0; i < ti.size(); ++i)
    ti[i] = i && i <= tex.size() ? VkDescriptorImageInfo{VK_NULL_HANDLE, tex[i - 1].first, tex[i - 1].second}
                                 : VkDescriptorImageInfo{VK_NULL_HANDLE, g_dimg.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet w[9];
  const VkDescriptorBufferInfo* bufs[9] = {nullptr, &info, &pool, &soup, &recb, &lights, nullptr, &stats, &head};
  const uint32_t bindings[9] = {0, 1, 2, 3, 4, 5, 6, 8, 9};
  for (int i = 0; i < 9; ++i) {
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
  w[6].pBufferInfo = nullptr;
  dd->vk.UpdateDescriptorSets(dd->device, 9, w, 0, nullptr);

  // The light list: after the TLAS build (bvh_build ends with AS -> compute), after last
  // frame's readers of the list.
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
    uint32_t pc[4] = {total, ninst, 0, 0};
    dd->vk.CmdPushConstants(cmd, g_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    dd->vk.CmdDispatch(cmd, (total + 255) / 256, 1, 1);
  }
  barrier(cmd, kCS, VK_ACCESS_SHADER_WRITE_BIT, kCS | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT);
  restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  ts_end(cmd, tsp);

  SlotInfo& si = g_slots[slot];
  si.frame = now + 1;
  si.rec_fs.resize(nrec);
  for (uint32_t i = 0; i < nrec; ++i) si.rec_fs[i] = (uint64_t(recs[i].fs_hi) << 32) | recs[i].fs_lo;
  si.lights = total;
  si.overflow = overflow;
  si.instances = n;
  si.groups = (std::min)((total + 255) / 256, 1024u);
  si.census = census;
  si.static_tris = res.static_tris;
  si.dynamic_tris = res.dynamic_tris;
  g_current = int(slot);
  // The effects target only when an effects pass of this generation was recorded: the
  // image is then alive for the frame being built (level loads replace it).
  const bool fx_fresh = fx.image && fx.frame + 1 == now;
  g_info = Bg3EmissiveInfo{now, n, nrec, total, pre, fx_fresh ? fx.image : VK_NULL_HANDLE,
                           fx_fresh ? fx.view : VK_NULL_HANDLE, fx.layout, fx.w, fx.h};
  if (cfg_int("rt_em_log", 0))
    log("[em] build frame %llu: %u effects passes, %u draws (%u emissive: %u captured, %u cached; %u recorded this frame, for the next build), %u records, "
        "%u instances, %.0f + %.0f triangles, %u lights (%u left out)",
        (unsigned long long)now, census.passes, census.draws, census.kinds[kMatEmissive], census.captured, census.cached,
        census.late, nrec, n, double(res.static_tris), double(res.dynamic_tris), total, overflow);
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
  VkDescriptorSetLayoutBinding b[10] = {
      {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {6, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTex, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {7, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &g_sampler},
      {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
  VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dci.bindingCount = 10;
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
                                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8 * kRing},
                                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTex * kRing},
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
  // The soup: XFB output read in place by the BLAS builds (both halves).
  const VkDeviceSize soup_mb = VkDeviceSize((std::max)(cfg_int("rt_em_xfb_mb", 32), 2));
  ok = ok && make_buf(g_soup, soup_mb << 20,
                      VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | rt_xfb_usage(),
                      false);
  g_max_lights = uint32_t((std::min)((std::max)(cfg_int("rt_em_max_lights", 65536), 256), 1 << 20));
  ok = ok && make_buf(g_lights, VkDeviceSize(g_max_lights) * 80, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
  ok = ok && make_buf(g_host, kRegion * kRing, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true) && g_host.map;
  ok = ok && make_buf(g_dummy, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
  ok = ok && make_dummy_image();
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
  for (Buf* b : {&g_soup, &g_lights, &g_host, &g_dummy}) free_buf(*b);
  defer_destroy(BG3_DEFER_PIPELINE, uint64_t(g_pipe));
  defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, uint64_t(g_pl));
  defer_destroy(BG3_DEFER_DESCRIPTOR_POOL, uint64_t(g_pool));
  defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, uint64_t(g_dsl));
  defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, uint64_t(g_push_dsl));
  defer_destroy(BG3_DEFER_SAMPLER, uint64_t(g_sampler));
  defer_destroy(BG3_DEFER_IMAGE_VIEW, uint64_t(g_dimg.view));
  defer_destroy(BG3_DEFER_IMAGE, uint64_t(g_dimg.image));
  defer_destroy(BG3_DEFER_MEMORY, uint64_t(g_dimg.mem));
  if (g_qp) defer_destroy(BG3_DEFER_QUERY_POOL, uint64_t(g_qp));
  (void)vk;
  g_pipe = VK_NULL_HANDLE;
  g_pl = VK_NULL_HANDLE;
  g_pool = VK_NULL_HANDLE;
  g_dsl = g_push_dsl = VK_NULL_HANDLE;
  g_sampler = VK_NULL_HANDLE;
  g_dimg = DummyImg{};
  g_qp = VK_NULL_HANDLE;
  g_current = -1;
  g_on = false;
  g_dd = nullptr;
}

bool em_wanted() { return g_on && rt_enabled() && bvh_split_wanted() && cfg_int("rt_emissive", 1) != 0; }

bool em_is_fx_pass(const Bg3PassInfo& p, uint32_t gw, uint32_t gh) {
  return g_on && p.color_count == 1 && p.color_formats[0] == VK_FORMAT_B10G11R11_UFLOAT_PACK32 &&
         p.depth_format == VK_FORMAT_D24_UNORM_S8_UINT && gw && p.width == gw && p.height == gh;
}

bool em_pass_begin(VkCommandBuffer cmd, const CmdState* s, int* ts) {
  *ts = -1;
  t_pass_fx = false;
  if (!g_on) return false;
  const bool want = em_wanted();
  // Which of these passes draw into the effects target is only known from their draws
  // (em_draw: EMI materials); the SSS and forward passes look the same.
  // Earlier builds read this half of the soup (BLAS builds, the light list).
  if (want) barrier(cmd, kAS | kCS, VK_ACCESS_MEMORY_READ_BIT, kXFB, VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT);
  uint32_t p = ts_pair(cmd, 0, want);
  *ts = p == UINT32_MAX ? -1 : int(p);
  return want;
}

bool em_draw(VkCommandBuffer cmd, const CmdState* s, const DrawArgs& a, const EmDrawIn& in, VkBuffer* soup,
             VkDeviceSize* off, VkDeviceSize* bytes, uint32_t* prims_out) {
  std::lock_guard<std::mutex> lock(g_mu);
  {
    const uint64_t now = frame_index();
    (void)now;
    select_gen();
  }
  ++g_g->census.draws;
  const Mat& mat = material(s->fs);
  ++g_g->census.kinds[mat.kind];
  if (mat.kind == kMatEmissive || mat.kind == kMatDecal || mat.kind == kMatBackdrop) {
    // An EMI-variant draw: this pass writes the effects target.
    t_pass_fx = true;
    g_g->fx = Fx{s->pass.color_images[0], s->pass.color_views[0], s->color_layouts[0], s->pass.width, s->pass.height,
              frame_index()};
  }
  if (mat.kind != kMatEmissive) return false;
  FsCount& fc = g_fs_window[s->fs];
  ++fc.draws;
  if (!in.xfb) return ++g_g->census.skip_xfb, false;
  if (a.kind > 1) return ++g_g->census.skip_indirect, false;
  const bool list = in.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  if (!in.triangles || (!list && !((in.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
                                     in.topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) &&
                                    (a.kind == 0 || !in.restart))))
    return ++g_g->census.skip_topo, false;
  uint64_t prims = list ? a.count / 3 : (a.count > 2 ? a.count - 2 : 0);
  prims *= a.instances;
  if (!prims) return false;
  uint32_t rec = 0;
  if (!make_record(cmd, s->fs, mat, &rec)) return false;
  g_stride = in.stride;
  const bool cache = cfg_int("rt_em_cache", 1) != 0;   // 0: every emissive draw gets a per-draw BLAS
  BvhDraw bd{cache && list && in.inst && in.inst->ok ? in.inst : nullptr, s->vs, s->pipe_id[0], false, a, s,
             1 + int(g_g - g_gens)};
  uint32_t token = 0, mesh_tris = 0;
  BvhVerdict v = bvh_classify(cmd, bd, &token, &mesh_tris);
  if (v == BVH_CACHED) {
    bvh_set_record(token, rec);
    ++g_g->census.cached;
    ++fc.cached;
    return false;
  }
  // Captured (a new mesh, a per-draw BLAS, or an audit of a cached one): 256-aligned in this half.
  const VkDeviceSize need = prims * 3 * in.stride, half = g_soup.size / 2 / 256 * 256;
  const VkDeviceSize start = g_g == &g_gens[1] ? half : 0;
  if (g_g->cursor + need > half) {
    bvh_note_skipped(token);
    if (v == BVH_CACHED_AUDIT) bvh_set_record(token, rec);
    return ++g_g->census.skip_full, false;
  }
  *off = start + g_g->cursor;
  *bytes = need;
  *prims_out = uint32_t(prims);
  *soup = g_soup.b;
  g_g->cursor = up256(g_g->cursor + need);
  bvh_captured(token, *off, uint32_t(prims), false, UINT32_MAX);
  bvh_set_record(token, rec);
  if (v == BVH_CACHED_AUDIT) ++g_g->census.cached, ++fc.cached;
  else ++g_g->census.captured, ++fc.captured, g_g->census.captured_tris += prims;
  return true;
}

void em_pass_done(VkCommandBuffer cmd, const CmdState* s, VkBuffer ubo, VkDeviceSize ubo_off, uint32_t stride,
                  const std::vector<std::pair<VkDeviceSize, uint32_t>>& ranges, int ts) {
  (void)s;
  const bool fx = t_pass_fx;
  t_pass_fx = false;
  if (fx) {
    std::lock_guard<std::mutex> lock(g_mu);
    select_gen();
    ++g_g->census.passes;
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
        // No camera: collapse the draw to the origin (zero area, never hit).
        g_dd->vk.CmdFillBuffer(cmd, g_soup.b, off, bytes, 0);
      }
    }
    if (!ubo) {
      std::lock_guard<std::mutex> lock(g_mu);
      select_gen();
      ++g_g->census.no_ubo;
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

void em_dispatch_pre(VkCommandBuffer cmd, uint64_t cs, bool indirect) {
  if (!g_on || !indirect) return;
  const uint64_t now = frame_index();
  if (g_build_frame.load(std::memory_order_relaxed) == now) return;
  if (!is_lighting(cmd, cs)) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_build_frame.load() == now) return;
  g_build_frame = now;
  if (!em_wanted() || !cfg_int("rt_em_build", 1)) {
    reset_gen(int((now & 1) ^ 1));
    return;
  }
  build(cmd);
}

bool em_scene(VkDescriptorSetLayout* layout, VkDescriptorSet* set, Bg3EmissiveInfo* info) {
  if (!g_on) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  *layout = g_dsl;
  *set = g_current < 0 || !em_wanted() ? VK_NULL_HANDLE : g_sets[g_current];
  if (info) *info = g_info;
  return true;
}

void em_on_present() {
  if (!g_on) return;
  const uint64_t now = frame_index();
  // Timestamps: effects passes summed per frame (capture on/off), builds.
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
    const Census& c = si.census;
    Window& w = g_win;
    ++w.builds;
    w.passes += c.passes;
    w.draws += c.draws;
    w.emissive += c.kinds[kMatEmissive];
    w.captured += c.captured;
    w.cached += c.cached;
    w.late += c.late;
    w.decal += c.kinds[kMatDecal];
    w.backdrop += c.kinds[kMatBackdrop];
    w.engine += c.kinds[kMatEngine];
    w.unknown += c.kinds[kMatUnknown];
    w.other += c.kinds[kMatOther];
    w.skip_xfb += c.skip_xfb;
    w.skip_indirect += c.skip_indirect;
    w.skip_topo += c.skip_topo;
    w.skip_full += c.skip_full;
    w.skip_recs += c.skip_recs;
    w.skip_tex += c.skip_tex;
    w.instances += si.instances;
    w.tris += double(si.static_tris + si.dynamic_tris);
    w.lights += si.lights;
    w.overflow += si.overflow;
    w.power += power;
    w.last_power = power;
    w.last_draws = c.kinds[kMatEmissive];
    w.last_lights = si.lights;
    // Debug statistics (rt_debug 6-8 ran on this build's set).
    const uint32_t* st = reinterpret_cast<const uint32_t*>(region + kOffStats);
    if (st[6]) {
      ++w.dbg_frames;
      w.dbg_depth += st[0];
      w.dbg_hit += st[1];
      w.dbg_miss += st[2];
      w.dbg_dark += st[3];
      w.dbg_bright += st[7];
      for (uint32_t r = 0; r < si.rec_fs.size(); ++r) {
        const uint32_t* q = st + 16 + r * 4;
        if (!q[2]) continue;
        Window::Cmp& cmp = w.cmp[si.rec_fs[r]];
        cmp.ours += q[0] / 16.0;
        cmp.game += q[1] / 16.0;
        cmp.px += q[2];
      }
    }
    if (cfg_int("rt_em_log", 0))
      log("[em] frame %llu: %u emissive draws, %.0f triangles, %u lights, total power %.4g (effects-target units x m^2)",
          (unsigned long long)(si.frame - 1), c.kinds[kMatEmissive], double(si.static_tris + si.dynamic_tris), si.lights,
          power);
    si.frame = 0;
  }
  const int every = (std::max)(cfg_int("rt_timing_frames", 300), 30);
  Window& w = g_win;
  if (w.builds < uint64_t(every)) return;
  const double nb = double(w.builds);
  auto pct = [](std::vector<float>& v, double q) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    return v[(std::min)(v.size() - 1, size_t(double(v.size()) * q))];
  };
  std::vector<float> fx[2];
  for (int k = 0; k < 2; ++k)
    for (auto& [f, ms] : w.fx_frame_ms[k]) fx[k].push_back(float(ms));
  log("[em] last %llu builds, per build: %.1f effects passes, %.1f draws in them (engine %.1f, unknown %.1f, decal %.1f, "
      "backdrop %.1f, other materials %.1f); emissive %.1f: captured %.1f, cached %.1f, recorded during the build's frame (next build) %.2f; left "
      "out: no XFB %.2f, indirect %.2f, topology %.2f, soup full %.2f, records full %.2f, textures full %.2f",
      (unsigned long long)w.builds, w.passes / nb, w.draws / nb, w.engine / nb, w.unknown / nb, w.decal / nb,
      w.backdrop / nb, w.other / nb, w.emissive / nb, w.captured / nb, w.cached / nb, w.late / nb, w.skip_xfb / nb,
      w.skip_indirect / nb, w.skip_topo / nb, w.skip_full / nb, w.skip_recs / nb, w.skip_tex / nb);
  log("[em] per build: %.1f TLAS instances, %.0f triangles, %.0f lights (%.0f left out by rt_em_max_lights), total power "
      "%.4g (effects-target units x m^2; last build %.4g, %u draws, %u lights), %llu non-finite power sums, %u stale generations dropped; GPU: build "
      "median %.3f ms (p99 %.3f, %zu samples), effects passes per frame median %.3f ms with capture (%zu frames), %.3f ms "
      "without (%zu frames)",
      w.instances / nb, w.tris / nb, w.lights / nb, w.overflow / nb, w.power / nb, w.last_power, w.last_draws,
      w.last_lights, (unsigned long long)w.nonfinite, g_stale, pct(w.build_ms, 0.5), pct(w.build_ms, 0.99), w.build_ms.size(),
      pct(fx[1], 0.5), fx[1].size(), pct(fx[0], 0.5), fx[0].size());
  // Coverage per emissive family over the window.
  {
    std::string line;
    int shown = 0;
    std::vector<std::pair<uint32_t, uint64_t>> order;
    for (auto& [fs, c] : g_fs_window) order.push_back({c.draws, fs});
    std::sort(order.rbegin(), order.rend());
    for (auto& [d, fs] : order) {
      if (shown++ >= 16) break;
      const FsCount& c = g_fs_window[fs];
      const Mat& m = material(fs);
      char buf[256];
      snprintf(buf, sizeof buf, "%s %08x %s %.1f/%.1f/%.1f", line.empty() ? "" : ";", unsigned(fs >> 32), m.name.c_str(),
               c.draws / nb, c.captured / nb, c.cached / nb);
      line += buf;
    }
    if (!line.empty()) log("[em] emissive families (draws/captured/cached per build):%s", line.c_str());
  }
  if (w.dbg_frames) {
    double ours = 0, game = 0, px = 0;
    std::string line;
    for (auto& [fs, c] : w.cmp) {
      ours += c.ours, game += c.game, px += c.px;
      char buf[200];
      snprintf(buf, sizeof buf, "%s %08x %s %.3f (%.0f px)", line.empty() ? "" : ";", unsigned(fs >> 32),
               material(fs).name.c_str(), c.game > 0 ? c.ours / c.game : 0.0, c.px / double(w.dbg_frames));
      line += buf;
    }
    log("[em] projected check over %llu frames: emissive hits on %.3f%% of the depth pixels; ours/game radiance %.3f over "
        "them; effects-target pixels brighter than rt_em_fx_thr with no emissive hit %.3f%% of %.0f (coverage %.1f%%); hits "
        "where the game shows < 2%% of ours %.3f%% of the hits",
        (unsigned long long)w.dbg_frames, w.dbg_depth ? 100.0 * double(w.dbg_hit) / double(w.dbg_depth) : 0.0,
        game > 0 ? ours / game : 0.0, w.dbg_bright ? 100.0 * double(w.dbg_miss) / double(w.dbg_bright) : 0.0,
        double(w.dbg_bright) / double(w.dbg_frames),
        w.dbg_bright ? 100.0 * (1.0 - double(w.dbg_miss) / double(w.dbg_bright)) : 0.0,
        w.dbg_hit ? 100.0 * double(w.dbg_dark) / double(w.dbg_hit) : 0.0);
    if (!line.empty()) log("[em] ours/game per family:%s", line.c_str());
  }
  g_win = Window{};
  g_fs_window.clear();
  g_stale = 0;
}

}  // namespace bg3
