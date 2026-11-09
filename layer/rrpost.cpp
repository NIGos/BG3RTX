// DLSS Ray Reconstruction at the start of post-processing (rr_post=1).
//
// BG3 feeds DLSS its *graded* image: god rays, depth of field, a dither
// resolve, exposure, bloom and a PQ-space 3D-LUT grade all run before the
// evaluate (docs/FRAME_MAP.md, docs/archive/AUDIT_RT.md part 8). The RR guide wants RR
// "as close to the start of post-processing as possible", and DoF after it.
//
// This file follows the game's post chain while it is recorded and carries it
// to the output resolution as a per-pixel affine map of the sharp colour,
//   chain(X) = A * X + C          (render resolution, image g_ac: rgb C, a A)
// with the steps of shaders/rr_post.hlsl, recorded in the game's command buffer
// right where the game's own step is:
//   first post step (god rays a7466e4e / DoF composite 96bb806d / 7401d785):
//                            start: Xs = X (the step's input), A = 1, C = 0
//   DoF composite 96bb806d:  dof: A *= the composite's own blend weights
//   dispatch 7401d785:       resolve: Xr = the game's dither resolve of Xs (RR's
//                            colour input), C = U - A * Xs (U = the game's chain
//                            value: god rays and the blurred DoF layers, exact)
//   exposure 2cdd9f08:       exposure: A, C *= b0[1] * b1[0] (the game's buffers)
//   bloom 0463a527:          bloom: C += 0.2 * bloom
//   grade 0592680b:          the grade's UBO and LUT are remembered
// At the DLSS evaluate (layer/ngx.cpp) RR gets Xr and writes our RGBA16F
// output; `final` then writes the game's DLSS output image with
//   grade(A * RR + C)
// so the tonemap and UI see what they always saw, minus the noise.
// Anything out of order breaks the chain for that frame and the evaluate falls
// back to RR on the game's graded colour (logged).
#include "host.h"
#include "recording_init.h"
#include "../common/pipeline_async.h"
#include "../common/hair_primary.h"

#include <cmath>
#include <cstring>

namespace bg3 {

namespace {

constexpr uint64_t kFsCompose = 0x5c3fdabc477f316aull;
constexpr uint64_t kFsSss1 = 0x6f5350759c4ad056ull;   // SSS blur 1: reads the diffuse light before SSS (set 1 b3)
constexpr uint64_t kFsGodRays = 0xa7466e4ef8df23b6ull;
constexpr uint64_t kFsDof = 0x96bb806dece0ad36ull;
constexpr uint64_t kCsResolve = 0x7401d78564eb8bacull;
constexpr uint64_t kFsExposure = 0x2cdd9f08314642a3ull;
constexpr uint64_t kFsBloom = 0x0463a5279d9589c2ull;
constexpr uint64_t kFsGrade = 0x0592680bc336dca3ull;
// Every other pass or dispatch the game records between the first post step
// and the grade. Anything not in this list is a post effect the chain does not
// model (seen: a tint vignette 3b556695 with its mask abfdaa7e, a 3D overlay
// a1e30a20, chromatic aberration b9f5a8ab, all in one Goblin Camp state): the
// frame then falls back to RR on the game's graded colour, so the look never
// changes.
constexpr uint64_t kKnownInChain[] = {
    0xf4b1495e5e81a27eull,  // god-ray mask (half res)
    0x126243ea812375e2ull, 0xea7685a91b8621d6ull, 0x22a45f323076c7eeull, 0x0963a38316d723ceull,  // DoF
    0xb0b686139cc06ff7ull, 0x061a44c27a2cd65bull, 0x05bac4b30d959fd1ull, 0x6d4e162f416a5e66ull,
    0x3f6186576f49de56ull, 0x784f260b3b7195bdull,
    0x38f22d8be6074b47ull,  // exposure measure (reads only)
    0x3434e56117615cf9ull, 0x6bfc74d75d384c8aull, 0x779cbf3adb06ab7eull,  // bloom down/up
    // Colour-LUT bakes (32x32 groups writing a 3D rgba32f image), recorded in the
    // post command buffer while the camera moves between atmosphere areas. They
    // only rebuild the LUTs the grade and tonemap read later.
    0x304fe7e0211939a9ull, 0xdd9110b27471f886ull, 0x66cc9cb83c097230ull,
};
constexpr uint64_t kCsLighting = 0xf469db7865d7c524ull;  // tiled lighting, standard class
constexpr uint64_t kCsAdapt = 0xbd9db92d9abc7fb7ull;     // exposure adaptation

struct Img {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkFormat fmt = VK_FORMAT_UNDEFINED;
  uint32_t w = 0, h = 0;
};

// Matches rr_post.hlsl's Push (96 bytes).
struct Push {
  uint32_t size[2], src[2];
  float uvmap[4], uvclamp[4], uvscale[2];
  int32_t dof_m0;
  uint32_t flags;
  float grade[4];
  uint32_t bmin[2], bmax[2];
};
static_assert(sizeof(Push) == 96, "Push must match rr_post.hlsl");

// rr_hair_strand_aa: rr_strand_aa.hlsl's push (the same 96-byte range), sent through run() in a Push's bytes.
struct StrandPush {
  float vp[16];        // this frame's G-buffer camera-relative jittered view-projection (row-major, v * M)
  float cam[3];        // its camera position
  uint32_t jitter;     // half2 jitter in render pixels
  uint32_t bmin, bmax; // x | y << 15; bmin bits 30-31 the mode
  uint32_t header[2];  // hair primary header address
};
static_assert(sizeof(StrandPush) == sizeof(Push), "StrandPush must match rr_strand_aa.hlsl");

struct Binds {
  VkImageView in[3] = {};
  VkImageLayout in_l[3] = {};
  VkImageView lut = VK_NULL_HANDLE;
  VkImageLayout lut_l = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageView stencil = VK_NULL_HANDLE;
  VkImageLayout stencil_l = VK_IMAGE_LAYOUT_UNDEFINED;
  bool ac = false;
  VkImageView temporal_in[4] = {}; // depth, previous AC, EB, depth/MV
  VkImageLayout depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageView temporal_ac = VK_NULL_HANDLE, temporal_eb = VK_NULL_HANDLE, temporal_meta = VK_NULL_HANDLE;
  VkImageView out11 = VK_NULL_HANDLE, out16 = VK_NULL_HANDLE, out_r16 = VK_NULL_HANDLE;
  VkDescriptorBufferInfo buf[3] = {};
  bool has_buf[3] = {};
  bool probe = false;
  VkDescriptorBufferInfo probe_out = {}; // optional writable storage target (QA hold)
};

struct Chain {
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  uint64_t frame = 0;
  uint64_t recording = 0;   // CmdState::recording: one chain per recording of a command buffer
  int stage = 0;  // 1 started, 2 dof, 3 resolved, 4 exposed, 5 bloom, 6 graded
  bool broken = false;
  std::string why;
  uint32_t w = 0, h = 0;
  VkImageView lut = VK_NULL_HANDLE;
  VkImageLayout lut_l = VK_IMAGE_LAYOUT_UNDEFINED;
  float grade[4] = {};
  bool dof = false;
};

std::mutex g_mu;
Chain g_ch;
Img g_xs, g_xr, g_ac, g_eb, g_check, g_cbt, g_z, g_sss_before, g_sss;
// Fillers for every binding a step does not use: a reused ring set must never
// keep a stale descriptor of a game image or buffer (see run()).
Img g_dummy2d, g_dummy3d;
Img g_hist_ac[2], g_hist_eb[2], g_hist_meta[2];
struct History {
  PostHistoryProducer producer;
  uint64_t frame = 0;
  uint32_t ow = 0, oh = 0;
  float jitter[2] = {};
};
History g_history[2];
bool g_hist_valid = false, g_temporal_active = false;
uint32_t g_hist_index = 0, g_hist_ow = 0, g_hist_oh = 0;
uint64_t g_hist_frame = 0;
float g_hist_jitter[2] = {}, g_final_jitter[2] = {};
VkBuffer g_dummy_buf = VK_NULL_HANDLE;
VkDeviceMemory g_dummy_buf_mem = VK_NULL_HANDLE;
void* g_dummy_buf_map = nullptr;
uint64_t g_sss_before_frame = 0, g_sss_frame = 0;
uint64_t g_cbt_frame = 0;
VkDescriptorSetLayout g_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_pl = VK_NULL_HANDLE;
VkDescriptorPool g_pool = VK_NULL_HANDLE;
constexpr uint32_t kRing = 256;
VkDescriptorSet g_sets[kRing] = {};
uint32_t g_next_set = 0;
DescriptorRecordingLease g_set_owners[kRing];
struct SetPage {
  VkDescriptorPool pool = VK_NULL_HANDLE;
  VkDescriptorSet sets[kRing] = {};
  DescriptorRecordingLease owners[kRing];
};
std::vector<SetPage> g_set_pages; // grow at the executable-recording high water mark

VkSampler g_sampler = VK_NULL_HANDLE;
struct Pipe {
  const char* entry;
  VkPipeline pipe = VK_NULL_HANDLE;
  uint64_t gen = 0;
};
Pipe g_pipes[] = {{"start"}, {"dof"}, {"resolve"}, {"exposure"}, {"bloom"}, {"final"}, {"check"}, {"probe"}, {"copy"}, {"sss"}, {"hold"}, {"temporal"}, {"temporal_final"}, {"strand_resolve"}};
enum { P_START, P_DOF, P_RESOLVE, P_EXPOSURE, P_BLOOM, P_FINAL, P_CHECK, P_PROBE, P_COPY, P_SSS, P_HOLD, P_TEMPORAL, P_TEMPORAL_FINAL, P_STRAND };
std::string g_last_logged_why;
uint64_t g_completed = 0, g_broken_frames = 0;

// GPU timing: two timestamps per recorded dispatch, render resolution steps
// and the output-resolution final separately.
constexpr uint32_t kTs = 128;
VkQueryPool g_qp = VK_NULL_HANDLE;
float g_ts_period = 1.0f;
uint32_t g_ts_next = 0;
bool g_ts_pending[kTs] = {};
int g_ts_kind[kTs] = {};
double g_ts_acc[3] = {};
uint32_t g_ts_frames[3] = {};
uint64_t g_ts_last_frame[3] = {};
ULONGLONG g_ts_tick = 0;

// Exposure probe (s1b13[0] of the lighting vs the exposure buffers).
VkBuffer g_probe_buf = VK_NULL_HANDLE;
VkDeviceMemory g_probe_mem = VK_NULL_HANDLE;
float* g_probe_map = nullptr;
constexpr uint32_t kProbeFloats = 16;
Bg3Binding g_b13{};
uint64_t g_b13_frame = 0;
Bg3Binding g_adapt_out{};
std::atomic<float> g_pre_exposure{0.0f};   // lighting s1b13[0], read back a few frames late
uint64_t g_probe_logged = 0;
bool g_ids_logged = false;
// QA exposure hold (qa_exposure_hold=1, docs/QA.md "Exposure hold"): cross-trace comparisons
// (hard_dark's light-off floors above all) need one exposure for every trace, but the game's
// eye adaptation re-exposes a darker trace within the 240 presents before it is captured
// (QA 20261027-173531: the ambient-off floor brighter than the image with the ambient). The
// adaptation pass bd9db92d writes [0] = 0.8333/2^EV, [1] = 1.2*2^EV, [2] = EV, ... into one of
// two buffers that swap each frame (TRACK_A_RR "Pre-exposure"); with the hold on, its output is
// overwritten right after it runs with the values the lighting used when the hold was switched
// on (the probe's b0), so from two frames later the pre-exposure (s1b13) and the exposure
// pass's re-exposure (b0[1] * b1[0] = 1) stay fixed. Needs rr_post (the probe and these hooks).
float g_last_b0[6] = {};
bool g_last_valid = false;
float g_hold[6] = {};
bool g_hold_on = false;
uint64_t g_hold_writes = 0;

uint32_t mem_type(DeviceData* dd, uint32_t bits) {
  VkPhysicalDeviceMemoryProperties props{};
  dd->inst->GetPhysicalDeviceMemoryProperties(dd->physical, &props);
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return i;
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
    if (bits & (1u << i)) return i;
  return UINT32_MAX;
}

void img_destroy(Img& g) {
  if (g.view) defer_destroy(BG3_DEFER_IMAGE_VIEW, (uint64_t)g.view);
  if (g.image) {
    defer_destroy(BG3_DEFER_IMAGE, (uint64_t)g.image);
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().images.erase(g.image);
    tracker().views.erase(g.view);
  }
  if (g.mem) defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g.mem);
  g = Img{};
}

// Device-local image, registered with the tracker so traces can dump it.
bool img_ensure(DeviceData* dd, Img& g, VkFormat fmt, uint32_t w, uint32_t h, const char* name,
                VkImageType type = VK_IMAGE_TYPE_2D) {
  if (g.image && g.w == w && g.h == h && g.fmt == fmt) return true;
  img_destroy(g);
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.imageType = type;
  ci.format = fmt;
  ci.extent = {w, h, 1};
  ci.mipLevels = 1;
  ci.arrayLayers = 1;
  ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
             VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (dd->vk.CreateImage(dd->device, &ci, nullptr, &g.image) != VK_SUCCESS) return g = Img{}, false;
  VkMemoryRequirements req;
  dd->vk.GetImageMemoryRequirements(dd->device, g.image, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = mem_type(dd, req.memoryTypeBits);
  if (dd->vk.AllocateMemory(dd->device, &mai, nullptr, &g.mem) != VK_SUCCESS) {
    dd->vk.DestroyImage(dd->device, g.image, nullptr);
    return g = Img{}, false;
  }
  if (dd->vk.BindImageMemory(dd->device, g.image, g.mem, 0) != VK_SUCCESS) {
    dd->vk.DestroyImage(dd->device, g.image, nullptr);
    dd->vk.FreeMemory(dd->device, g.mem, nullptr);
    return g = Img{}, false;
  }
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = g.image;
  vi.viewType = type == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
  vi.format = fmt;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (dd->vk.CreateImageView(dd->device, &vi, nullptr, &g.view) != VK_SUCCESS) {
    dd->vk.DestroyImage(dd->device, g.image, nullptr);
    dd->vk.FreeMemory(dd->device, g.mem, nullptr);
    return g = Img{}, false;
  }
  g.fmt = fmt;
  g.w = w;
  g.h = h;
  ImageRec rec{tracker().next_image_id++, ci, false};
  {
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    tracker().images[g.image] = rec;
    tracker().views[g.view] = ViewRec{g.image, fmt, vi.subresourceRange};
  }
  log("[rrpost] %s %ux%u fmt %d: i%u", name, w, h, int(fmt), rec.id);
  return true;
}

bool ensure_layout(DeviceData* dd) {
  if (g_dsl && g_pl && g_pool && g_sampler && g_dummy_buf) return true;
  auto failed = [&]() {
    img_destroy(g_dummy2d);
    img_destroy(g_dummy3d);
    if (g_dummy_buf) dd->vk.DestroyBuffer(dd->device, g_dummy_buf, nullptr);
    if (g_dummy_buf_mem) dd->vk.FreeMemory(dd->device, g_dummy_buf_mem, nullptr);
    if (g_sampler) dd->vk.DestroySampler(dd->device, g_sampler, nullptr);
    if (g_pool) dd->vk.DestroyDescriptorPool(dd->device, g_pool, nullptr);
    if (g_pl) dd->vk.DestroyPipelineLayout(dd->device, g_pl, nullptr);
    if (g_dsl) dd->vk.DestroyDescriptorSetLayout(dd->device, g_dsl, nullptr);
    g_dummy_buf = VK_NULL_HANDLE;
    g_dummy_buf_mem = VK_NULL_HANDLE;
    g_dummy_buf_map = nullptr;
    g_sampler = VK_NULL_HANDLE;
    g_pool = VK_NULL_HANDLE;
    g_pl = VK_NULL_HANDLE;
    g_dsl = VK_NULL_HANDLE;
    memset(g_sets, 0, sizeof g_sets);
    return false;
  };
  VkDescriptorSetLayoutBinding b[20] = {};
  const VkDescriptorType types[20] = {
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_DESCRIPTOR_TYPE_SAMPLER,
      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
  for (uint32_t i = 0; i < 20; ++i) {
    b[i].binding = i;
    b[i].descriptorType = types[i];
    b[i].descriptorCount = 1;
    b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dci.bindingCount = 20;
  dci.pBindings = b;
  if (dd->vk.CreateDescriptorSetLayout(dd->device, &dci, nullptr, &g_dsl) != VK_SUCCESS) return failed();
  VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
  VkPipelineLayoutCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pci.setLayoutCount = 1;
  pci.pSetLayouts = &g_dsl;
  pci.pushConstantRangeCount = 1;
  pci.pPushConstantRanges = &pcr;
  if (dd->vk.CreatePipelineLayout(dd->device, &pci, nullptr, &g_pl) != VK_SUCCESS) return failed();
  VkDescriptorPoolSize sizes[4] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 9 * kRing},
                                   {VK_DESCRIPTOR_TYPE_SAMPLER, kRing},
                                   {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 6 * kRing},
                                   {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 * kRing}};
  VkDescriptorPoolCreateInfo ppi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  ppi.maxSets = kRing;
  ppi.poolSizeCount = 4;
  ppi.pPoolSizes = sizes;
  if (dd->vk.CreateDescriptorPool(dd->device, &ppi, nullptr, &g_pool) != VK_SUCCESS) return failed();
  std::vector<VkDescriptorSetLayout> layouts(kRing, g_dsl);
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = g_pool;
  ai.descriptorSetCount = kRing;
  ai.pSetLayouts = layouts.data();
  if (dd->vk.AllocateDescriptorSets(dd->device, &ai, g_sets) != VK_SUCCESS) return failed();
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = si.minFilter = VK_FILTER_LINEAR;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  si.maxLod = 0.0f;
  if (dd->vk.CreateSampler(dd->device, &si, nullptr, &g_sampler) != VK_SUCCESS) return failed();
  if (!img_ensure(dd, g_dummy2d, VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, "dummy 2D (unused bindings)") ||
      !img_ensure(dd, g_dummy3d, VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, "dummy 3D (unused bindings)", VK_IMAGE_TYPE_3D) ||
      !alloc_host_buffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &g_dummy_buf, &g_dummy_buf_mem, &g_dummy_buf_map))
    return failed();
  if (dd->vk.CreateQueryPool) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kTs * 2;
    if (dd->vk.CreateQueryPool(dd->device, &qi, nullptr, &g_qp) != VK_SUCCESS) g_qp = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props;
    dd->inst->GetPhysicalDeviceProperties(dd->physical, &props);
    g_ts_period = props.limits.timestampPeriod;
  }
  return true;
}

// The shader file that defines an entry. The build and the per-frame generation check must agree:
// asking rr_post.hlsl for a temporal entry fails to compile ("missing entry point definition").
const char* entry_source(const char* entry) {
  if (strcmp(entry, "temporal") == 0 || strcmp(entry, "temporal_final") == 0) return "rr_post_temporal.hlsl";
  if (strcmp(entry, "strand_resolve") == 0) return "rr_strand_aa.hlsl";
  return "rr_post.hlsl";
}

VkPipeline compile_pipeline(DeviceData* dd, Pipe& p) {
  Bg3ShaderBlob blob{};
  if (!shader_get(entry_source(p.entry), p.entry, "cs_6_0", &blob)) return p.pipe;
  if (p.pipe && blob.generation == p.gen) return p.pipe;
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  VkShaderModule mod = VK_NULL_HANDLE;
  if (dd->vk.CreateShaderModule(dd->device, &mci, nullptr, &mod) != VK_SUCCESS) return p.pipe;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, p.entry};
  ci.layout = g_pl;
  VkPipeline np = VK_NULL_HANDLE;
  VkResult r = dd->vk.CreateComputePipelines(dd->device, VK_NULL_HANDLE, 1, &ci, nullptr, &np);
  dd->vk.DestroyShaderModule(dd->device, mod, nullptr);
  if (r != VK_SUCCESS) {
    log("[rrpost] rr_post.hlsl:%s pipeline creation failed (%d)", p.entry, r);
    return p.pipe;
  }
  if (p.pipe) defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)p.pipe);
  p.pipe = np;
  p.gen = blob.generation;
  log("[rrpost] rr_post.hlsl:%s pipeline built (generation %llu)", p.entry, (unsigned long long)p.gen);
  return np;
}

bg3::pipe_async::Builds g_builds(1,
    [](const bg3::pipe_async::Result& r) { defer_destroy(BG3_DEFER_PIPELINE, r.pipeline); },
    [] { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); });
bool g_prewarmed = false;
bg3::pipe_async::Result compile_background(DeviceData* dd, const char* entry) {
  Pipe fresh{entry};
  auto p = compile_pipeline(dd, fresh);
  return {uint64_t(p), fresh.gen, p != VK_NULL_HANDLE};
}
VkPipeline pipeline(DeviceData* dd, Pipe& p) {
  if (p.pipe) {
    Bg3ShaderBlob blob{};
    if (!shader_get(entry_source(p.entry), p.entry, "cs_6_0", &blob) ||
        blob.generation == p.gen) return p.pipe;
  }
  bg3::pipe_async::Result result;
  const auto st = g_builds.poll(p.entry, uint64_t(g_pl), [dd, entry=p.entry] {
    return compile_background(dd, entry);
  }, &result);
  if (st == bg3::pipe_async::State::kReady) {
    if (p.pipe) defer_destroy(BG3_DEFER_PIPELINE, uint64_t(p.pipe));
    p.pipe = VkPipeline(result.pipeline); p.gen = result.generation;
  }
  return p.pipe;
}

void barrier(DeviceData* dd, VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst, VkAccessFlags sa,
             VkAccessFlags da) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = sa;
  mb.dstAccessMask = da;
  dd->vk.CmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

// Our images are fully rewritten where they are produced, so they are
// (re)declared UNDEFINED -> GENERAL there: valid whatever order the command
// buffers were recorded in.
void to_general(DeviceData* dd, VkCommandBuffer cmd, std::initializer_list<const Img*> imgs) {
  VkImageMemoryBarrier b[6];
  uint32_t n = 0;
  for (const Img* g : imgs) {
    if (!g->image || n == 6) continue;
    b[n] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b[n].srcAccessMask = 0;
    b[n].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    b[n].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b[n].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b[n].srcQueueFamilyIndex = b[n].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b[n].image = g->image;
    b[n].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    ++n;
  }
  if (n)
    dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                              0, nullptr, n, b);
}

void ts_harvest(DeviceData* dd, uint32_t slot) {
  if (!g_ts_pending[slot]) return;
  uint64_t r[4] = {};
  VkResult res = dd->vk.GetQueryPoolResults(dd->device, g_qp, slot * 2, 2, sizeof r, r, 2 * sizeof(uint64_t),
                                            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  if (res == VK_SUCCESS && r[1] && r[3] && r[2] >= r[0]) {
    g_ts_acc[g_ts_kind[slot]] += double(r[2] - r[0]) * g_ts_period * 1e-6;
  }
  g_ts_pending[slot] = false;
}

// Records one rr_post.hlsl entry. Returns false if the pipeline or set is missing.
VkDescriptorSet acquire_set(DeviceData* dd, VkCommandBuffer cmd) {
  const auto recording = cmd_recording(cmd);
  if (!recording) return VK_NULL_HANDLE;
  auto find = [&](auto& sets, auto& owners) -> VkDescriptorSet {
    for (uint32_t i = 0; i < kRing; ++i) {
      const auto index = (g_next_set + i) % kRing;
      if (owners[index].recording ||
          !owners[index].reusable(life_before_destroy(uint64_t(sets[index]), "RR descriptor", false))) continue;
      owners[index].record(recording);
      g_next_set = (index + 1) % kRing;
      life_note(cmd, uint64_t(sets[index]));
      return sets[index];
    }
    return VK_NULL_HANDLE;
  };
  if (auto set = find(g_sets, g_set_owners)) return set;
  for (auto& page : g_set_pages)
    if (auto set = find(page.sets, page.owners)) return set;
  // Completion alone cannot release an executable set. Allocate another page
  // instead of silently dropping temporal/final when the old ring fills.
  SetPage page;
  VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 9 * kRing},
    {VK_DESCRIPTOR_TYPE_SAMPLER, kRing}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 6 * kRing},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 * kRing}};
  VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pi.maxSets = kRing; pi.poolSizeCount = 4; pi.pPoolSizes = sizes;
  if (dd->vk.CreateDescriptorPool(dd->device, &pi, nullptr, &page.pool) != VK_SUCCESS) return VK_NULL_HANDLE;
  std::vector<VkDescriptorSetLayout> layouts(kRing, g_dsl);
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = page.pool; ai.descriptorSetCount = kRing; ai.pSetLayouts = layouts.data();
  if (dd->vk.AllocateDescriptorSets(dd->device, &ai, page.sets) != VK_SUCCESS) {
    dd->vk.DestroyDescriptorPool(dd->device, page.pool, nullptr); return VK_NULL_HANDLE;
  }
  g_set_pages.push_back(page);
  return find(g_set_pages.back().sets, g_set_pages.back().owners);
}

bool run(DeviceData* dd, VkCommandBuffer cmd, int which, const Binds& b, const Push& pc, uint32_t gx, uint32_t gy) {
  VkPipeline pipe = pipeline(dd, g_pipes[which]);
  if (!pipe) return false;
  VkDescriptorSet set = acquire_set(dd, cmd);
  if (!set) return false;
  VkDescriptorImageInfo ii[18] = {};
  VkWriteDescriptorSet w[20] = {};
  uint32_t n = 0, k = 0;
  uint32_t written = 0;   // bindings this step writes
  auto img = [&](uint32_t binding, VkDescriptorType t, VkImageView v, VkImageLayout l) {
    if (!v) return;
    written |= 1u << binding;
    ii[k] = {VK_NULL_HANDLE, v, l};
    w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w[n].dstSet = set;
    w[n].dstBinding = binding;
    w[n].descriptorCount = 1;
    w[n].descriptorType = t;
    w[n].pImageInfo = &ii[k];
    ++n, ++k;
  };
  for (uint32_t i = 0; i < 3; ++i) img(i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, b.in[i], b.in_l[i]);
  img(3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, b.lut, b.lut_l);
  img(4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, b.stencil, b.stencil_l);
  ii[k] = {g_sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
  w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  w[n].dstSet = set;
  w[n].dstBinding = 5;
  w[n].descriptorCount = 1;
  w[n].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  w[n].pImageInfo = &ii[k];
  ++n, ++k;
  if (b.temporal_ac) img(6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, b.temporal_ac, VK_IMAGE_LAYOUT_GENERAL);
  if (b.temporal_eb) img(14, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, b.temporal_eb, VK_IMAGE_LAYOUT_GENERAL);
  for (uint32_t i = 0; i < 4; ++i)
    img(15 + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, b.temporal_in[i], i == 0 ? b.depth_layout : VK_IMAGE_LAYOUT_GENERAL);
  img(19, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, b.temporal_meta, VK_IMAGE_LAYOUT_GENERAL);
  if (b.ac) img(6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, g_ac.view, VK_IMAGE_LAYOUT_GENERAL);
  if (b.ac) img(14, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, g_eb.view, VK_IMAGE_LAYOUT_GENERAL);
  img(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, b.out11, VK_IMAGE_LAYOUT_GENERAL);
  img(8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, b.out16, VK_IMAGE_LAYOUT_GENERAL);
  img(13, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, b.out_r16, VK_IMAGE_LAYOUT_GENERAL);
  VkDescriptorBufferInfo bi[4];
  for (uint32_t i = 0; i < 3; ++i)
    if (b.has_buf[i]) {
      written |= 1u << (9 + i);
      bi[i] = b.buf[i];
      w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[n].dstSet = set;
      w[n].dstBinding = 9 + i;
      w[n].descriptorCount = 1;
      w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w[n].pBufferInfo = &bi[i];
      ++n;
    }
  if (b.probe || b.probe_out.buffer) {
    written |= 1u << 12;
    bi[3] = b.probe_out.buffer ? b.probe_out : VkDescriptorBufferInfo{g_probe_buf, 0, VK_WHOLE_SIZE};
    w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w[n].dstSet = set;
    w[n].dstBinding = 12;
    w[n].descriptorCount = 1;
    w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[n].pBufferInfo = &bi[3];
    ++n;
  }
  dd->vk.UpdateDescriptorSets(dd->device, n, w, 0, nullptr);
  // The sets are a ring reused every few dozen frames, and a step writes only
  // the bindings it reads. Whatever a step leaves alone would keep the view or
  // buffer an earlier step wrote there: a game image the game may have
  // destroyed since. The validation layer then counts it as used by every
  // pending command buffer that binds the set (VUID-vkDestroyImage-image-01000
  // at level loads), and by the spec the game's destroy invalidates that
  // command buffer. So every other binding gets a filler of ours.
  {
    VkDescriptorImageInfo fi[2] = {{VK_NULL_HANDLE, g_dummy2d.view, VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, g_dummy3d.view, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo fb{g_dummy_buf, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet fw[20] = {};
    uint32_t fn = 0;
    for (uint32_t i = 0; i < 20; ++i) {
      if (i == 5 || (written & (1u << i))) continue;   // 5: the sampler, always written
      VkWriteDescriptorSet& x = fw[fn++];
      x = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      x.dstSet = set;
      x.dstBinding = i;
      x.descriptorCount = 1;
      if (i <= 4 || (i >= 15 && i <= 18)) x.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, x.pImageInfo = &fi[i == 3 ? 1 : 0];
      else if (i >= 9 && i <= 12) x.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, x.pBufferInfo = &fb;
      else x.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, x.pImageInfo = &fi[0];
    }
    if (fn) dd->vk.UpdateDescriptorSets(dd->device, fn, fw, 0, nullptr);
  }

  uint32_t slot = UINT32_MAX;
  if (logging::stats_enabled(cfg_int) && g_qp && which != P_PROBE && which != P_CHECK && which != P_HOLD) {
    slot = g_ts_next++ % kTs;
    ts_harvest(dd, slot);
    dd->vk.CmdResetQueryPool(cmd, g_qp, slot * 2, 2);
    dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_qp, slot * 2);
  }
  {
    // The fillers are never read; they only need a valid layout (contents discarded).
    static uint64_t fill_rec = 0;
    CmdState* cs = cmd_state(cmd);
    if (cs && cs->recording != fill_rec) {
      fill_rec = cs->recording;
      to_general(dd, cmd, {&g_dummy2d, &g_dummy3d});
    }
  }
  barrier(dd, cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
  dd->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
  dd->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl, 0, 1, &set, 0, nullptr);
  dd->vk.CmdPushConstants(cmd, g_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  dd->vk.CmdDispatch(cmd, gx, gy, 1);
  if (CmdState* cs = cmd_state(cmd)) cs->ours_since_event = true;   // (cmd.cpp CmdWaitEvents)
  barrier(dd, cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_WRITE_BIT,
          VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
  if (slot != UINT32_MAX) {
    dd->vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qp, slot * 2 + 1);
    g_ts_pending[slot] = true;
    g_ts_kind[slot] = which == P_TEMPORAL ? 2 : (which == P_FINAL || which == P_TEMPORAL_FINAL) ? 1 : 0;
    int kind = g_ts_kind[slot];
    if (g_ts_last_frame[kind] != frame_index()) {
      g_ts_last_frame[kind] = frame_index();
      ++g_ts_frames[kind];
    }
  }
  restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  return true;
}

// Our own views of the game's colour images (mip 0, layer 0, identity
// swizzle), destroyed deferred when the game destroys the image. Our descriptor
// sets never reference a game view, which the game may destroy while a command
// buffer that recorded our work is still pending (BG3 binds most of its images
// through push descriptors). Depth/stencil views stay the game's (the stencil
// read relies on the game's view).
struct ViewKey {
  VkImage image;
  VkFormat format;
  bool operator==(const ViewKey& o) const { return image == o.image && format == o.format; }
};
struct ViewKeyHash {
  size_t operator()(const ViewKey& k) const { return std::hash<uint64_t>()((uint64_t)k.image ^ (uint64_t(k.format) << 48)); }
};
std::unordered_map<ViewKey, VkImageView, ViewKeyHash> g_own_views;
// The step that last read each game image through our view, for the log when
// the game destroys it (docs/TRACK_A_RR.md, "Lifetime of the game's images").
const char* g_step = "?";
std::unordered_map<VkImage, std::pair<const char*, uint64_t>> g_view_use;   // step, frame

VkImageView own_view(DeviceData* dd, VkImage image, VkFormat fmt) {
  g_view_use[image] = {g_step, frame_index()};
  auto it = g_own_views.find({image, fmt});
  if (it != g_own_views.end()) return it->second;
  VkImageType type = VK_IMAGE_TYPE_2D;
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    auto rec = tracker().images.find(image);
    if (rec == tracker().images.end()) return VK_NULL_HANDLE;
    type = rec->second.ci.imageType;
  }
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = image;
  vi.viewType = type == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
  vi.format = fmt;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView v = VK_NULL_HANDLE;
  if (dd->vk.CreateImageView(dd->device, &vi, nullptr, &v) != VK_SUCCESS) return VK_NULL_HANDLE;
  g_own_views[{image, fmt}] = v;
  return v;
}

bool is_depth_format(VkFormat f) {
  return f == VK_FORMAT_D16_UNORM || f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT ||
         f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_X8_D24_UNORM_PACK32 || f == VK_FORMAT_S8_UINT ||
         f == VK_FORMAT_D16_UNORM_S8_UINT;
}

bool image_binding(VkCommandBuffer cmd, VkPipelineBindPoint bp, uint32_t set, uint32_t binding, VkImageView* v,
                   VkImageLayout* l, VkImage* img = nullptr, uint32_t* w = nullptr, uint32_t* h = nullptr) {
  Bg3Binding b{};
  if (!query_binding(cmd, bp, set, binding, 0, &b) || !b.view) return false;
  *v = b.view;
  DeviceData* dd = device_of(cmd);
  VkFormat imgfmt = VK_FORMAT_UNDEFINED;
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    auto rec = tracker().images.find(b.image);
    if (rec != tracker().images.end()) imgfmt = rec->second.ci.format;
  }
  if (dd && b.image && !is_depth_format(imgfmt) && !is_depth_format(b.format))
    if (VkImageView own = own_view(dd, b.image, b.format)) *v = own;
  *l = b.layout;
  if (img) *img = b.image;
  if (w) *w = b.width;
  if (h) *h = b.height;
  return true;
}

bool buffer_binding(VkCommandBuffer cmd, VkPipelineBindPoint bp, uint32_t set, uint32_t binding,
                    VkDescriptorBufferInfo* out, Bg3Binding* raw = nullptr) {
  Bg3Binding b{};
  if (!query_binding(cmd, bp, set, binding, 0, &b) || !b.buffer) return false;
  *out = {b.buffer, b.offset, b.range ? b.range : VK_WHOLE_SIZE};
  if (raw) *raw = b;
  return true;
}

// Reads `size` bytes of a uniform buffer the game has bound, from its mapped memory.
bool read_ubo(VkCommandBuffer cmd, VkPipelineBindPoint bp, uint32_t set, uint32_t binding, void* dst, size_t size) {
  Bg3Binding b{};
  if (!query_binding(cmd, bp, set, binding, 0, &b) || !b.buffer) return false;
  if (b.range != VK_WHOLE_SIZE && b.range < size) return false;
  std::shared_lock<std::shared_mutex> lock(tracker().mu);
  auto bm = tracker().buffer_mem.find(b.buffer);
  if (bm == tracker().buffer_mem.end()) return false;
  auto mp = tracker().mapped.find(bm->second.first);
  if (mp == tracker().mapped.end() || !mp->second) return false;
  memcpy(dst, mp->second + bm->second.second + b.offset, size);
  return true;
}

// The fullscreen VS uv mapping (set 0 b4, std140: _m0 @0, _m1 @8, _m3 @24, _m4 @32,
// _m11 @80, _m12 @88, _m15 @112) into the push constants.
bool read_globals(VkCommandBuffer cmd, VkPipelineBindPoint bp, Push& pc) {
  float g[30];
  if (!read_ubo(cmd, bp, 0, 4, g, sizeof g)) return false;
  memcpy(pc.uvmap, &g[0], 16);
  memcpy(pc.uvclamp, &g[20], 16);
  memcpy(pc.uvscale, &g[28], 8);
  uint32_t u[10];
  memcpy(u, g, sizeof u);
  pc.bmin[0] = u[6], pc.bmin[1] = u[7];
  pc.bmax[0] = u[8], pc.bmax[1] = u[9];
  static int logged = 0;
  if (logged++ < 1)
    log("[rrpost] post globals: uv = uv01 * (%g %g) + (%g %g), clamp (%g %g)-(%g %g), scale (%g %g), bounds (%u %u)-(%u %u)",
        pc.uvmap[0], pc.uvmap[1], pc.uvmap[2], pc.uvmap[3], pc.uvclamp[0], pc.uvclamp[1], pc.uvclamp[2], pc.uvclamp[3],
        pc.uvscale[0], pc.uvscale[1], pc.bmin[0], pc.bmin[1], pc.bmax[0], pc.bmax[1]);
  return true;
}

void breaks(const char* why) {
  if (g_ch.broken) return;
  g_ch.broken = true;
  g_ch.why = why;
  ++g_broken_frames;
  if (g_last_logged_why != why) {
    g_last_logged_why = why;
    log("[rrpost] post chain not followed this frame (%s): RR falls back to the game's graded colour", why);
  }
}

// A new chain when a post step arrives in another command buffer or frame.
void touch(VkCommandBuffer cmd) {
  CmdState* cs = cmd_state(cmd);
  const uint64_t rec = cs ? cs->recording : 0;
  if (g_ch.cmd == cmd && g_ch.recording == rec) return;
  g_ch = Chain{};
  g_ch.cmd = cmd;
  g_ch.frame = frame_index();
  g_ch.recording = rec;
}

bool start(DeviceData* dd, VkCommandBuffer cmd, VkImageView x, VkImageLayout xl, uint32_t w, uint32_t h) {
  if (!img_ensure(dd, g_xs, VK_FORMAT_B10G11R11_UFLOAT_PACK32, w, h, "Xs (post input snapshot)") ||
      !img_ensure(dd, g_xr, VK_FORMAT_B10G11R11_UFLOAT_PACK32, w, h, "Xr (RR colour: dither-resolved X)") ||
      !img_ensure(dd, g_ac, VK_FORMAT_R16G16B16A16_SFLOAT, w, h, "AC (post chain: rgb C, a A)") ||
      !img_ensure(dd, g_eb, VK_FORMAT_R16G16B16A16_SFLOAT, w, h, "EB (post chain: rgb 0.2 bloom, a exposure)"))
    return false;
  to_general(dd, cmd, {&g_xs, &g_ac, &g_eb});
  Binds b;
  b.in[0] = x;
  b.in_l[0] = xl;
  b.ac = true;
  b.out11 = g_xs.view;
  Push pc{};
  pc.size[0] = w, pc.size[1] = h;
  if (!run(dd, cmd, P_START, b, pc, (w + 7) / 8, (h + 7) / 8)) return false;
  g_ch.stage = 1;
  g_ch.w = w, g_ch.h = h;
  return true;
}

void on_godrays(DeviceData* dd, VkCommandBuffer cmd, const Bg3PassInfo& p) {
  g_step = "god rays (X)";
  touch(cmd);
  if (g_ch.broken || g_ch.stage != 0) {
    if (g_ch.stage != 0) breaks("god rays after another post step");
    return;
  }
  VkImageView x;
  VkImageLayout xl;
  if (!image_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 3, &x, &xl)) return breaks("god rays input not bound");
  if (!start(dd, cmd, x, xl, p.width, p.height)) return breaks("could not start the chain");
}

void on_dof(DeviceData* dd, VkCommandBuffer cmd, const Bg3PassInfo& p) {
  g_step = "dof (X, CoC)";
  touch(cmd);
  if (g_ch.broken) return;
  if (g_ch.stage == 0) {
    VkImageView x;
    VkImageLayout xl;
    if (!image_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 1, &x, &xl)) return breaks("DoF composite input not bound");
    if (!start(dd, cmd, x, xl, p.width, p.height)) return breaks("could not start the chain");
  } else if (g_ch.stage != 1) {
    return breaks("DoF composite out of order");
  }
  Binds b;
  if (!image_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 5, &b.in[0], &b.in_l[0]) ||
      !image_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 4, &b.in[1], &b.in_l[1]))
    return breaks("DoF CoC images not bound");
  Push pc{};
  int32_t m0 = 0;
  if (!read_globals(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pc) ||
      !read_ubo(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 2, &m0, sizeof m0) || m0 <= 0)
    return breaks("DoF constants unreadable");
  pc.size[0] = g_ch.w, pc.size[1] = g_ch.h;
  pc.dof_m0 = m0;
  b.ac = true;
  if (!run(dd, cmd, P_DOF, b, pc, (g_ch.w + 7) / 8, (g_ch.h + 7) / 8)) return breaks("dof pipeline");
  static int logged = 0;
  if (logged++ < 2) log("[rrpost] DoF composite seen: CoC factor m0 = %d; RR now runs before it", m0);
  g_ch.dof = true;
  g_ch.stage = 2;
}

// rr_hair_strand_aa (docs/RTX_HAIR_PRIMARY.md 5m): with strands on screen, the dither resolve with the strand footprint
// untagged (1), plus the analytic strand coverage (2), rr_strand_aa.hlsl. Needs the hair primary header and this frame's
// G-buffer camera; without them (or with the key off) the game's resolve runs as before. Live.
uint32_t half_bits(float v) {
  const uint32_t f = [&] { uint32_t u; memcpy(&u, &v, 4); return u; }();
  const uint32_t sign = (f >> 16) & 0x8000u;
  const int e = int((f >> 23) & 0xFF) - 127 + 15;
  if (e <= 0) return sign;                       // |v| < 2^-14: flushed (a jitter below 6e-5 px)
  if (e >= 31) return sign | 0x7BFFu;
  return sign | (uint32_t(e) << 10) | ((f >> 13) & 0x3FFu);
}
uint64_t g_strand_frames = 0, g_strand_missing = 0, g_strand_logged_at = 0;
int g_strand_mode_logged = -1;
bool strand_resolve(DeviceData* dd, VkCommandBuffer cmd, const Binds& b, const Push& pc, const uint32_t ext[2]) {
  const int mode = std::min(cfg_int("rr_hair_strand_aa", 0), 2);
  if (mode != g_strand_mode_logged) {
    g_strand_mode_logged = mode;
    log("[rrpost] rr_hair_strand_aa=%d: %s", mode, mode == 0 ? "the game's dither resolve" :
        mode == 1 ? "dither resolve with the strand footprint untagged" : "strand footprint untagged + analytic strand coverage");
  }
  if (mode <= 0) return false;
  const auto* hp = static_cast<const hair_primary::Persist*>(persist(hair_primary::kPersistKey, sizeof(hair_primary::Persist)));
  StrandPush sp{};
  float jit[2] = {};
  const bool ok = hp && hp->header && pc.bmax[0] < 32768u && pc.bmax[1] < 32768u &&
                  ngx_gbuffer_camera(sp.vp, sp.cam, jit, g_ch.w, g_ch.h);
  if (ok) {
    sp.jitter = half_bits(jit[0]) | (half_bits(jit[1]) << 16);
    sp.bmin = pc.bmin[0] | (pc.bmin[1] << 15) | (uint32_t(mode) << 30);
    sp.bmax = pc.bmax[0] | (pc.bmax[1] << 15);
    sp.header[0] = uint32_t(hp->header);
    sp.header[1] = uint32_t(hp->header >> 32);
    Push raw;
    memcpy(&raw, &sp, sizeof raw);
    if (!run(dd, cmd, P_STRAND, b, raw, (ext[0] + 15) / 16, (ext[1] + 15) / 16)) return false;   // not built yet: the game's
    ++g_strand_frames;
  } else {
    ++g_strand_missing;
  }
  if (frame_index() >= g_strand_logged_at + 600) {
    g_strand_logged_at = frame_index();
    log("[rrpost] strand resolve (rr_hair_strand_aa=%d): %llu frames, %llu without the hair header or the G-buffer camera",
        mode, (unsigned long long)g_strand_frames, (unsigned long long)g_strand_missing);
    g_strand_frames = g_strand_missing = 0;
  }
  return ok;
}

void on_resolve(DeviceData* dd, VkCommandBuffer cmd) {
  g_step = "resolve (U)";
  touch(cmd);
  if (g_ch.broken) return;
  VkImageView u;
  VkImageLayout ul;
  uint32_t w = 0, h = 0;
  if (!image_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, 1, 0, &u, &ul, nullptr, &w, &h))
    return breaks("7401d785 input not bound");
  if (g_ch.stage == 0) {
    if (!start(dd, cmd, u, ul, w, h)) return breaks("could not start the chain");
  } else if (g_ch.stage != 1 && g_ch.stage != 2) {
    return breaks("7401d785 out of order");
  }
  Binds b;
  b.in[0] = u;
  b.in_l[0] = ul;
  b.in[1] = g_xs.view;
  b.in_l[1] = VK_IMAGE_LAYOUT_GENERAL;
  if (!image_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, 1, 1, &b.stencil, &b.stencil_l))
    return breaks("7401d785 stencil not bound");
  Push pc{};
  if (!read_globals(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pc)) return breaks("7401d785 bounds unreadable");
  pc.size[0] = g_ch.w, pc.size[1] = g_ch.h;
  b.ac = true;
  b.out11 = g_xr.view;
  to_general(dd, cmd, {&g_xr});
  uint32_t ext[2] = {pc.bmax[0] - pc.bmin[0] + 1, pc.bmax[1] - pc.bmin[1] + 1};
  if (strand_resolve(dd, cmd, b, pc, ext)) { g_ch.stage = 3; return; }
  if (!run(dd, cmd, P_RESOLVE, b, pc, (ext[0] + 15) / 16, (ext[1] + 15) / 16)) return breaks("resolve pipeline");
  g_ch.stage = 3;
}

void on_exposure(DeviceData* dd, VkCommandBuffer cmd) {
  touch(cmd);
  if (g_ch.broken) return;
  if (g_ch.stage != 3) return breaks("exposure before the dither resolve");
  Binds b;
  Bg3Binding ra{}, rb{};
  if (!buffer_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 0, &b.buf[0], &ra) ||
      !buffer_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 1, &b.buf[1], &rb))
    return breaks("exposure buffers not bound");
  b.has_buf[0] = b.has_buf[1] = true;
  b.ac = true;
  Push pc{};
  pc.size[0] = g_ch.w, pc.size[1] = g_ch.h;
  if (!run(dd, cmd, P_EXPOSURE, b, pc, (g_ch.w + 7) / 8, (g_ch.h + 7) / 8)) return breaks("exposure pipeline");
  g_ch.stage = 4;

  // Exposure probe: what the lighting's s1b13 holds against these buffers.
  if (!g_ids_logged) {
    g_ids_logged = true;
    log("[rrpost] exposure buffers: 2cdd9f08 b0 %p+%llu, b1 %p+%llu; adaptation writes %p+%llu; lighting s1b13 %p+%llu",
        (void*)ra.buffer, (unsigned long long)ra.offset, (void*)rb.buffer, (unsigned long long)rb.offset,
        (void*)g_adapt_out.buffer, (unsigned long long)g_adapt_out.offset, (void*)g_b13.buffer,
        (unsigned long long)g_b13.offset);
  }
  if (g_probe_map && g_b13.buffer && g_b13_frame + 1 >= frame_index()) {
    Binds pb;
    pb.buf[0] = b.buf[0], pb.buf[1] = b.buf[1];
    pb.buf[2] = {g_b13.buffer, g_b13.offset, g_b13.range ? g_b13.range : VK_WHOLE_SIZE};
    pb.has_buf[0] = pb.has_buf[1] = pb.has_buf[2] = true;
    pb.probe = true;
    Push ppc{};
    run(dd, cmd, P_PROBE, pb, ppc, 1, 1);   // overwritten every frame; read at present
  }
}

void on_bloom(DeviceData* dd, VkCommandBuffer cmd) {
  g_step = "bloom";
  touch(cmd);
  if (g_ch.broken) return;
  if (g_ch.stage != 4) return breaks("bloom composite out of order");
  Binds b;
  if (!image_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 2, &b.in[0], &b.in_l[0])) return breaks("bloom not bound");
  Push pc{};
  if (!read_globals(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pc)) return breaks("bloom constants unreadable");
  pc.size[0] = g_ch.w, pc.size[1] = g_ch.h;
  b.ac = true;
  if (!run(dd, cmd, P_BLOOM, b, pc, (g_ch.w + 7) / 8, (g_ch.h + 7) / 8)) return breaks("bloom pipeline");
  g_ch.stage = 5;
}

void on_grade(DeviceData* dd, VkCommandBuffer cmd) {
  g_step = "grade (LUT)";
  touch(cmd);
  if (g_ch.broken) return;
  if (g_ch.stage != 4 && g_ch.stage != 5) return breaks("grade out of order");
  if (!image_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 2, &g_ch.lut, &g_ch.lut_l)) return breaks("LUT not bound");
  if (!read_ubo(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 0, g_ch.grade, sizeof g_ch.grade))
    return breaks("grade constants unreadable");
  for (float f : g_ch.grade)
    if (!std::isfinite(f)) return breaks("non-finite grade constants");
  static float last[4] = {-1, -1, -1, -1};
  if (memcmp(last, g_ch.grade, sizeof last) != 0) {
    memcpy(last, g_ch.grade, sizeof last);
    log("[rrpost] grade constants: saturation %g, hue shift %g, %g, mix %g", g_ch.grade[0], g_ch.grade[1], g_ch.grade[2],
        g_ch.grade[3]);
  }
  g_ch.stage = 6;
  ++g_completed;
  CmdState* cs = cmd_state(cmd);
  if (cfg_int("rr_post_check", 0) || (cs && cs->tracing)) {
    if (!img_ensure(dd, g_check, VK_FORMAT_R16G16B16A16_SFLOAT, g_ch.w, g_ch.h, "check (grade(A*Xr+C), render res)"))
      return;
    to_general(dd, cmd, {&g_check});
    Binds b;
    b.in[0] = g_xr.view;
    b.in_l[0] = VK_IMAGE_LAYOUT_GENERAL;
    b.in[1] = g_ac.view;
    b.in_l[1] = VK_IMAGE_LAYOUT_GENERAL;
    b.in[2] = g_eb.view;
    b.in_l[2] = VK_IMAGE_LAYOUT_GENERAL;
    b.lut = g_ch.lut;
    b.lut_l = g_ch.lut_l;
    b.out16 = g_check.view;
    Push pc{};
    pc.size[0] = g_ch.w, pc.size[1] = g_ch.h;
    memcpy(pc.grade, g_ch.grade, sizeof pc.grade);
    pc.flags = cfg_int("rr_post_quantise", 1) ? 2u : 0u;
    run(dd, cmd, P_CHECK, b, pc, (g_ch.w + 7) / 8, (g_ch.h + 7) / 8);
  }
}

// SSS guide, step 1: the diffuse light before the SSS blur (set 1 b3 of the
// first SSS pass 6f535075; that pass does not write it).
void on_sss1(DeviceData* dd, VkCommandBuffer cmd, const Bg3PassInfo& p) {
  g_step = "sss1 (diffuse before)";
  if (!cfg_int("rr_sss", 1)) return;
  Binds b;
  if (!image_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 3, &b.in[0], &b.in_l[0])) return;
  if (!img_ensure(dd, g_sss_before, VK_FORMAT_B10G11R11_UFLOAT_PACK32, p.width, p.height, "SSS before (diffuse light)"))
    return;
  to_general(dd, cmd, {&g_sss_before});
  b.out11 = g_sss_before.view;
  Push pc{};
  pc.size[0] = p.width, pc.size[1] = p.height;
  if (run(dd, cmd, P_COPY, b, pc, (p.width + 7) / 8, (p.height + 7) / 8)) g_sss_before_frame = frame_index();
}

// SSS guide, step 2 (at the compose, which reads the diffuse light after SSS
// at set 1 b2): lum(after - before * albedo), 0 where SSS did not run.
void on_sss2(DeviceData* dd, VkCommandBuffer cmd, const Bg3PassInfo& p) {
  g_step = "sss2 (diffuse after)";
  if (!cfg_int("rr_sss", 1) || !g_sss_before.image || g_sss_before_frame + 1 < frame_index()) return;
  if (g_sss_before.w != p.width || g_sss_before.h != p.height) return;
  VkImageView albedo = VK_NULL_HANDLE;
  if (!ngx_diffuse_albedo(p.width, p.height, &albedo)) return;
  Binds b;
  if (!image_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 2, &b.in[0], &b.in_l[0])) return;
  b.in[1] = g_sss_before.view;
  b.in_l[1] = VK_IMAGE_LAYOUT_GENERAL;
  b.in[2] = albedo;
  b.in_l[2] = VK_IMAGE_LAYOUT_GENERAL;
  if (!img_ensure(dd, g_sss, VK_FORMAT_R16_SFLOAT, p.width, p.height, "SSS guide (R16F)")) return;
  to_general(dd, cmd, {&g_sss});
  b.out_r16 = g_sss.view;
  Push pc{};
  pc.size[0] = p.width, pc.size[1] = p.height;
  if (run(dd, cmd, P_SSS, b, pc, (p.width + 7) / 8, (p.height + 7) / 8)) g_sss_frame = frame_index();
}

// Colour before transparency (RR guide 3.4.11): the compose output, before the
// forward pass draws into the same image. Copied out at the compose pass end.
void on_compose(DeviceData* dd, VkCommandBuffer cmd, const Bg3PassInfo& p, VkImageLayout layout) {
  if (!cfg_int("rr_cbt", 1) || p.color_count < 1 || !p.color_images[0]) return;
  if (layout == VK_IMAGE_LAYOUT_UNDEFINED) return;
  if (p.color_formats[0] != VK_FORMAT_B10G11R11_UFLOAT_PACK32) return;
  if (!img_ensure(dd, g_cbt, VK_FORMAT_B10G11R11_UFLOAT_PACK32, p.width, p.height, "CBT (colour before transparency)"))
    return;
  VkImageMemoryBarrier b[2] = {{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}, {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}};
  b[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b[0].oldLayout = layout;
  b[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b[0].image = p.color_images[0];
  b[1].srcAccessMask = 0;
  b[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  b[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b[1].image = g_cbt.image;
  for (auto& x : b) {
    x.srcQueueFamilyIndex = x.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  }
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                            nullptr, 2, b);
  VkImageCopy r{};
  r.srcSubresource = r.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  r.extent = {p.width, p.height, 1};
  dd->vk.CmdCopyImage(cmd, p.color_images[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_cbt.image,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
  b[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  std::swap(b[0].oldLayout, b[0].newLayout);
  b[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                            nullptr, 2, b);
  g_cbt_frame = frame_index();
}

}  // namespace

// ---------------------------------------------------------------------------
// Hooks (layer/cmd.cpp)
// ---------------------------------------------------------------------------

// A pass or dispatch inside a running chain (this recording, after its first
// step, before the grade) that is neither a chain step nor a known helper.
void check_unknown(VkCommandBuffer cmd, uint64_t hash, bool compute) {
  CmdState* cs = cmd_state(cmd);
  if (!cs || g_ch.cmd != cmd || g_ch.recording != cs->recording || g_ch.broken) return;
  if (g_ch.stage < 1 || g_ch.stage >= 6) return;
  for (uint64_t k : kKnownInChain)
    if (k == hash) return;
  char why[96];
  snprintf(why, sizeof why, "unmodelled post effect %s %016llx", compute ? "cs" : "fs", (unsigned long long)hash);
  breaks(why);
}

void rrpost_pass_end(VkCommandBuffer cmd, const Bg3PassInfo& p, const VkImageLayout* layouts) {
  if (!cfg_int("rr", 1) || !cfg_int("rr_post", 1) || p.draw_count == 0) return;
  const uint64_t fs = p.first_fs_hash;
  if (fs != kFsCompose && fs != kFsSss1 && fs != kFsGodRays && fs != kFsDof && fs != kFsExposure && fs != kFsBloom && fs != kFsGrade) {
    std::lock_guard<std::mutex> lock(g_mu);
    check_unknown(cmd, fs, false);
    if (p.last_fs_hash != fs) check_unknown(cmd, p.last_fs_hash, false);
    return;
  }
  DeviceData* dd = device_of(cmd);
  if (!dd) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!ensure_layout(dd)) return;
  if (fs == kFsCompose) {
    on_sss2(dd, cmd, p);
    on_compose(dd, cmd, p, layouts[0]);
  } else if (fs == kFsSss1) on_sss1(dd, cmd, p);
  else if (fs == kFsGodRays) on_godrays(dd, cmd, p);
  else if (fs == kFsDof) on_dof(dd, cmd, p);
  else if (fs == kFsExposure) on_exposure(dd, cmd);
  else if (fs == kFsBloom) on_bloom(dd, cmd);
  else if (fs == kFsGrade) on_grade(dd, cmd);
}

void rrpost_dispatch_pre(VkCommandBuffer cmd, uint64_t cs) {
  if (cs == kCsLighting && cfg_int("rr", 1)) {
    if (auto* dd = device_of(cmd)) rrpost_prewarm(dd);
  }
  if (!cfg_int("rr", 1) || !cfg_int("rr_post", 1)) return;
  if (cs != kCsResolve && cs != kCsLighting && cs != kCsAdapt) {
    std::lock_guard<std::mutex> lock(g_mu);
    check_unknown(cmd, cs, true);
    return;
  }
  DeviceData* dd = device_of(cmd);
  if (!dd) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (cs == kCsLighting) {
    Bg3Binding b{};
    if (query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, 1, 13, 0, &b) && b.buffer) {
      g_b13 = b;
      g_b13_frame = frame_index();
    }
    return;
  }
  if (cs == kCsAdapt) {
    Bg3Binding b{};
    if (query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, 1, 1, 0, &b) && b.buffer) g_adapt_out = b;
    else g_adapt_out = {};
    return;
  }
  if (!ensure_layout(dd)) return;
  if (!g_probe_buf && !alloc_host_buffer(kProbeFloats * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                         &g_probe_buf, &g_probe_mem, reinterpret_cast<void**>(&g_probe_map)))
    g_probe_map = nullptr;
  on_resolve(dd, cmd);
}

// ---------------------------------------------------------------------------
// The evaluate (layer/ngx.cpp)
// ---------------------------------------------------------------------------

// True when this command buffer carried the whole post chain of this frame at
// render resolution w x h. Fills RR's colour input, our output image and the
// colour-before-transparency guide (null when stale).
bool rrpost_take(VkCommandBuffer cmd, uint32_t w, uint32_t h, uint32_t ow, uint32_t oh, RrPostInputs* out) {
  std::lock_guard<std::mutex> lock(g_mu);
  *out = RrPostInputs{};
  auto fail = [&](const char* why) {
    snprintf(out->why, sizeof out->why, "%s", why);
    return false;
  };
  CmdState* cs = cmd_state(cmd);
  if (g_ch.cmd != cmd || !cs || g_ch.recording != cs->recording) return fail("no post chain in this command buffer");
  if (g_ch.broken) return fail(g_ch.why.c_str());
  if (g_ch.stage != 6) return fail("post chain incomplete (no grade pass)");
  if (g_ch.w != w || g_ch.h != h) return fail("post chain at another resolution");
  DeviceData* dd = device_of(cmd);
  if (!dd || !img_ensure(dd, g_z, VK_FORMAT_R16G16B16A16_SFLOAT, ow, oh, "Z (RR output before the grade)"))
    return fail("output image");
  if (!pipeline(dd, g_pipes[P_FINAL])) return fail("rr_post.hlsl has not compiled");
  to_general(dd, cmd, {&g_z});
  out->color = {g_xr.image, g_xr.view, g_xr.fmt, w, h};
  out->output = {g_z.image, g_z.view, g_z.fmt, ow, oh};
  if (g_cbt.image && g_cbt.w == w && g_cbt.h == h && g_cbt_frame + 1 >= frame_index())
    out->cbt = {g_cbt.image, g_cbt.view, g_cbt.fmt, w, h};
  if (g_sss.image && g_sss.w == w && g_sss.h == h && g_sss_frame + 1 >= frame_index())
    out->sss = {g_sss.image, g_sss.view, g_sss.fmt, w, h};
  out->dof = g_ch.dof;
  return true;
}

void rrpost_prewarm(DeviceData* dd) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_prewarmed || !ensure_layout(dd)) return;
  g_prewarmed = true;
  unsigned count = 0;
  for (const auto& p : g_pipes)
    if (strcmp(p.entry, "strand_resolve") != 0 || cfg_int("rr_hair_strand_aa", 0) != 0)
      count += g_builds.prewarm(p.entry, uint64_t(g_pl), [dd, entry=p.entry] {
        return compile_background(dd, entry);
      }) ? 1u : 0u;
  log("[rrpost] Experimental prewarm: %u startup pipelines (RR order, below normal priority)", count);
}
void rrpost_join_prewarm() { g_builds.join(); g_prewarmed = false; }

void rrpost_forget_image(VkImage image) {
  std::lock_guard<std::mutex> lock(g_mu);
  for (auto it = g_own_views.begin(); it != g_own_views.end();) {
    if (it->first.image == image) {
      static int logged = 0;
      auto u = g_view_use.find(image);
      if (logged++ < 16)
        log("[rrpost] the game destroys image %p that our post step '%s' read in frame %llu (now frame %llu; last "
            "chain recorded in frame %llu)",
            (void*)image, u != g_view_use.end() ? u->second.first : "?",
            (unsigned long long)(u != g_view_use.end() ? u->second.second : 0), (unsigned long long)frame_index(),
            (unsigned long long)g_ch.frame);
      defer_destroy(BG3_DEFER_IMAGE_VIEW, (uint64_t)it->second);
      it = g_own_views.erase(it);
    } else ++it;
  }
  g_view_use.erase(image);
}

// Runs after bloom and NGX, while the exact RR depth/MV views are still live.
// History is never redeclared UNDEFINED when read. Only the next ping-pong output is discarded.
void rrpost_temporal(VkCommandBuffer cmd, const Bg3Binding& depth, const Bg3Binding& mv,
                     const float jitter[2], const float scale[2], bool reset, uint32_t ow, uint32_t oh) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_temporal_active = false;
  DeviceData* dd = device_of(cmd);
  if (!cfg_int("rr_post_temporal", 0)) { g_hist_valid = false; return; }
  const auto recording = cmd_recording(cmd);
  if (!recording) return;
  // Submission validity and current-frame dispatch are independent. A delayed
  // producer gives a fresh sample, never a skipped temporal or output grade.
  g_hist_valid = g_history[g_hist_index].producer.readable();
  // A discarded newer recording must not hide the other submitted slot.
  for (uint32_t i = 0; i < 2; ++i)
    if (g_history[i].producer.readable() && g_history[i].frame + 1 == frame_index()) {
      g_hist_index = i;
      g_hist_frame = g_history[i].frame;
      g_hist_ow = g_history[i].ow; g_hist_oh = g_history[i].oh;
      memcpy(g_hist_jitter, g_history[i].jitter, sizeof g_hist_jitter);
      g_hist_valid = true;
    }
  const uint32_t w = g_ch.w, h = g_ch.h;
  if (!dd || !depth.view || !mv.view || depth.width != w || depth.height != h ||
      mv.width != w || mv.height != h || !std::isfinite(jitter[0]) || !std::isfinite(jitter[1]) ||
      !std::isfinite(scale[0]) || !std::isfinite(scale[1])) {
    g_hist_valid = false;
    log("[rrpost] temporal REFUSED: missing/invalid RR depth, motion or jitter"); return;
  }
  const bool resize = g_hist_ac[0].w != w || g_hist_ac[0].h != h || g_hist_ow != ow || g_hist_oh != oh;
  if (resize || reset) for (auto& history : g_history) {
    history.producer.recording = 0; history.producer.submitted = false;
  }
  if (resize || reset || g_hist_frame + 1 != frame_index()) g_hist_valid = false;
  bool allocated = true;
  for (uint32_t i = 0; i < 2; ++i) {
    allocated = img_ensure(dd, g_hist_ac[i], VK_FORMAT_R32G32B32A32_SFLOAT, w, h, "temporal AC") && allocated;
    allocated = img_ensure(dd, g_hist_eb[i], VK_FORMAT_R32G32B32A32_SFLOAT, w, h, "temporal EB") && allocated;
    allocated = img_ensure(dd, g_hist_meta[i], VK_FORMAT_R32G32B32A32_SFLOAT, w, h, "temporal depth/MV") && allocated;
  }
  if (!allocated || !pipeline(dd, g_pipes[P_TEMPORAL_FINAL])) {
    g_hist_valid = false; log("[rrpost] temporal REFUSED: resources/pipeline"); return;
  }
  const uint32_t next = 1 - g_hist_index;
  to_general(dd, cmd, {&g_hist_ac[next], &g_hist_eb[next], &g_hist_meta[next]});
  Binds b;
  b.in[0] = g_ac.view; b.in[1] = g_eb.view; b.in[2] = mv.view;
  b.in_l[0] = b.in_l[1] = VK_IMAGE_LAYOUT_GENERAL; b.in_l[2] = mv.layout;
  b.temporal_in[0] = depth.view; b.depth_layout = depth.layout;
  // With flags=0 the shader never samples history. Bind the initialized output
  // slot instead of transitioning (and discarding) another pending producer.
  const auto previous = g_hist_valid ? g_hist_index : next;
  b.temporal_in[1] = g_hist_ac[previous].view;
  b.temporal_in[2] = g_hist_eb[previous].view;
  b.temporal_in[3] = g_hist_meta[previous].view;
  b.temporal_ac = g_hist_ac[next].view; b.temporal_eb = g_hist_eb[next].view;
  b.temporal_meta = g_hist_meta[next].view;
  Push pc{}; pc.size[0] = w; pc.size[1] = h;
  pc.flags = g_hist_valid ? 1u : 0u;
  pc.uvmap[0] = g_hist_jitter[0] - jitter[0]; pc.uvmap[1] = g_hist_jitter[1] - jitter[1];
  pc.uvscale[0] = scale[0]; pc.uvscale[1] = scale[1];
  if (!run(dd, cmd, P_TEMPORAL, b, pc, (w + 7) / 8, (h + 7) / 8)) {
    g_hist_valid = false; log("[rrpost] temporal REFUSED: dispatch"); return;
  }
  g_history[next].producer.record(recording);
  g_history[next].frame = frame_index();
  g_history[next].ow = ow; g_history[next].oh = oh;
  memcpy(g_history[next].jitter, jitter, sizeof g_history[next].jitter);
  g_hist_index = next; g_hist_valid = false; g_temporal_active = true; g_hist_frame = frame_index();
  g_hist_ow = ow; g_hist_oh = oh;
  memcpy(g_hist_jitter, jitter, sizeof g_hist_jitter);
  memcpy(g_final_jitter, jitter, sizeof g_final_jitter);
}

// After the RR evaluate: grade(A * RR + C) into the game's DLSS output image.
bool rrpost_final(VkCommandBuffer cmd, VkImage out_image, VkFormat out_fmt, uint32_t ow, uint32_t oh) {
  std::lock_guard<std::mutex> lock(g_mu);
  DeviceData* dd = device_of(cmd);
  if (!dd || !out_image) return false;
  g_step = "final (DLSS output)";
  VkImageView game_out = own_view(dd, out_image, out_fmt);
  if (!game_out) return false;
  life_note(cmd, (uint64_t)out_image);   // the game's DLSS output, written through our view
  Binds b;
  b.in[0] = g_z.view;
  b.in_l[0] = VK_IMAGE_LAYOUT_GENERAL;
  b.in[1] = g_temporal_active ? g_hist_ac[g_hist_index].view : g_ac.view;
  b.in_l[1] = VK_IMAGE_LAYOUT_GENERAL;
  b.in[2] = g_temporal_active ? g_hist_eb[g_hist_index].view : g_eb.view;
  b.in_l[2] = VK_IMAGE_LAYOUT_GENERAL;
  b.lut = g_ch.lut;
  b.lut_l = g_ch.lut_l;
  b.out11 = game_out;
  Push pc{};
  pc.size[0] = ow, pc.size[1] = oh;
  pc.src[0] = g_ch.w, pc.src[1] = g_ch.h;
  if (cfg_int("pan_post", 0) && cmd_state(cmd) && cmd_state(cmd)->tracing)
    log("[rrpost] energy jitter %g %g, render %ux%u, temporal %d", g_temporal_active ? g_final_jitter[0] : 0.0f,
        g_temporal_active ? g_final_jitter[1] : 0.0f, g_ch.w, g_ch.h, int(g_temporal_active));
  if (cfg_int("pan_post", 0) && cfg_int("pan_capture", 0) > 0) {
    const Img* images[] = {g_temporal_active ? &g_hist_ac[g_hist_index] : &g_ac,
                           g_temporal_active ? &g_hist_eb[g_hist_index] : &g_eb};
    const char* names[] = {"rr_post_ac", "rr_post_eb"};
    for (unsigned i = 0; i < 2; ++i) {
      Bg3Binding image{}; image.image = images[i]->image; image.view = images[i]->view;
      image.format = images[i]->fmt; image.width = images[i]->w; image.height = images[i]->h;
      image.layout = VK_IMAGE_LAYOUT_GENERAL; pan_capture(cmd, names[i], &image);
    }
  }
  memcpy(pc.grade, g_ch.grade, sizeof pc.grade);
  pc.flags = cfg_int("rr_post_quantise", 1) ? 2u : 0u;
  pc.uvmap[0] = g_final_jitter[0]; pc.uvmap[1] = g_final_jitter[1];
  return run(dd, cmd, g_temporal_active ? P_TEMPORAL_FINAL : P_FINAL, b, pc, (ow + 7) / 8, (oh + 7) / 8);
}

// Images worth dumping in a trace, after the evaluate.
int rrpost_trace_images(VkImage* out, int max) {
  std::lock_guard<std::mutex> lock(g_mu);
  int n = 0;
  for (const Img* g : {&g_xr, g_temporal_active ? &g_hist_ac[g_hist_index] : &g_ac, &g_z, &g_check, &g_cbt, &g_sss, g_temporal_active ? &g_hist_eb[g_hist_index] : &g_eb})
    if (g->image && n < max) out[n++] = g->image;
  return n;
}

float rrpost_pre_exposure() { return g_pre_exposure.load(); }

// After the game's adaptation dispatch: with the exposure hold on, overwrite what it wrote.
void rrpost_dispatch_post(VkCommandBuffer cmd, uint64_t cs) {
  if (cs != kCsAdapt) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_hold_on) return;
  DeviceData* dd = device_of(cmd);
  if (!dd) return;
  // This native buffer has STORAGE_BUFFER usage, not TRANSFER_DST. Query the
  // actual dispatch target (including dynamic offset), then write its six
  // floats with the same legal storage access as the adaptation shader.
  Bg3Binding target{};
  const bool valid = query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, 1, 1, 0, &target) && target.buffer &&
      (target.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || target.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) &&
      target.range >= sizeof g_hold;
  bool wrote = false;
  if (valid && ensure_layout(dd)) {
    Binds b;
    b.probe_out = {target.buffer, target.offset, sizeof g_hold};
    Push pc{};
    std::memcpy(pc.uvmap, g_hold, 4 * sizeof(float));
    std::memcpy(pc.uvclamp, g_hold + 4, 2 * sizeof(float));
    // run() supplies shader-write dependencies on both sides and restores the
    // game's compute bindings. No change to buffer usage, allocation or ABI.
    wrote = run(dd, cmd, P_HOLD, b, pc, 1, 1);
  }
  if (wrote) ++g_hold_writes;
  else {
    static uint64_t refused = 0;
    if (++refused <= 4 || refused % 300 == 0)
      log("[rrpost] exposure hold REFUSED: actual storage target/range or pipeline unavailable");
  }
}

void rrpost_on_present() {
  std::lock_guard<std::mutex> lock(g_mu);
  // Exposure probe: the latest write (a frame or two old; a diagnostic).
  if (g_probe_map) {
    float v[kProbeFloats];
    memcpy(v, g_probe_map, sizeof v);
    if (std::isfinite(v[12]) && v[12] > 0.0f) g_pre_exposure.store(v[12]);
    if (std::isfinite(v[2]) && v[0] > 0.0f) {   // b0: the buffer the lighting used (s1b13)
      memcpy(g_last_b0, v, sizeof g_last_b0);
      g_last_valid = true;
    }
    if (v[0] != 0.0f && (g_probe_logged < 3 || trace_recording() || frame_index() % 1800 == 0)) {
      ++g_probe_logged;
      log("[rrpost] exposure probe (frame %llu): 2cdd9f08 b0 = [%g %g %g %g %g %g], b1 = [%g %g %g %g %g %g], "
          "lighting s1b13 = [%g %g %g %g]; applied k = b0[1]*b1[0] = %g; 0.8333/2^b0[2] = %g, 0.8333/2^b1[2] = %g",
          (unsigned long long)frame_index(), v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11],
          v[12], v[13], v[14], v[15], v[1] * v[6], 0.8333333f / std::exp2(v[2]), 0.8333333f / std::exp2(v[8]));
    }
  }
  // QA exposure hold: latch on the switch, release when it goes off.
  const bool want = cfg_int("qa_exposure_hold", 0) != 0;
  if (want && !g_hold_on && g_last_valid) {
    memcpy(g_hold, g_last_b0, sizeof g_hold);
    g_hold_on = true;
    g_hold_writes = 0;
    log("[rrpost] exposure held (qa_exposure_hold=1) at EV %g: [%g %g %g %g %g %g] (frame %llu)", g_hold[2], g_hold[0],
        g_hold[1], g_hold[2], g_hold[3], g_hold[4], g_hold[5], (unsigned long long)frame_index());
  } else if (want && !g_hold_on && frame_index() % 600 == 0) {
    log("[rrpost] exposure hold requested but no exposure read back yet (needs rr=1, rr_post=1)");
  } else if (!want && g_hold_on) {
    g_hold_on = false;
    log("[rrpost] exposure hold released after %llu writes", (unsigned long long)g_hold_writes);
  } else if (g_hold_on && frame_index() % 600 == 0) {
    log("[rrpost] exposure held at EV %g: %llu writes so far; probe b0 EV %g, b1 EV %g", g_hold[2],
        (unsigned long long)g_hold_writes, g_last_b0[2], g_probe_map ? g_probe_map[8] : 0.0f);
  }
  ULONGLONG now = GetTickCount64();
  static int reset_token = 0;
  static uint64_t stats_start = 0;
  if (stats_window::reset(cfg_int, reset_token)) {
    stats_start = frame_index(); g_ts_tick = now;
    for (auto& pending : g_ts_pending) pending = false;
    for (auto& v : g_ts_acc) v = 0;
    for (auto& v : g_ts_frames) v = 0;
  }
  if (!g_ts_tick) g_ts_tick = now;
  if (cfg_int("rt_stats_period_frames", 0) > 0 ? frame_index() - stats_start < stats_period(1) : now - g_ts_tick < 5000) return;
  stats_start = frame_index();
  g_ts_tick = now;
  if (g_ts_frames[0] || g_ts_frames[1])
    log_stats("[rrpost] gpu: render-res chain %.3f ms/frame, output-res grade %.3f ms/frame; chains completed %llu, fell back "
        "%llu",
        g_ts_frames[0] ? g_ts_acc[0] / g_ts_frames[0] : 0.0, g_ts_frames[1] ? g_ts_acc[1] / g_ts_frames[1] : 0.0,
        (unsigned long long)g_completed, (unsigned long long)g_broken_frames);
  if (g_ts_frames[2]) log_stats("[rrpost] gpu temporal: %.3f ms/frame; active %d", g_ts_acc[2] / g_ts_frames[2], int(g_temporal_active));
  g_ts_acc[2] = 0; g_ts_frames[2] = 0;
  g_ts_acc[0] = g_ts_acc[1] = 0;
  g_ts_frames[0] = g_ts_frames[1] = 0;
}

}  // namespace bg3

namespace bg3 {
void rrpost_discard_recording(uint64_t recording) {
  std::lock_guard lock(g_mu);
  for (auto& owner : g_set_owners) owner.discard(recording);
  for (auto& page : g_set_pages) for (auto& owner : page.owners) owner.discard(recording);
  for (auto& history : g_history) history.producer.discard(recording);
}
void rrpost_submit_recording(uint64_t recording) {
  std::lock_guard lock(g_mu);
  for (auto& history : g_history) history.producer.submit(recording);
}
}
