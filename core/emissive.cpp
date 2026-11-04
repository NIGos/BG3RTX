// D-EM debug views (rt_debug 16-18 (modes 6-8), docs/TRACK_D_RT.md 13): primary rays against the
// host's emissive scene (Bg3HostApi::rt_emissive, shaders/rt/em_common.hlsli), written
// over the lit diffuse at the end of the tiled lighting like the other rt_debug views,
// and compared with the game's own effects target (shaders/rt/em_debug.hlsl):
//   6 the emission our data gives at the primary hit (black elsewhere; compose then adds
//     the game's effects target on top, so emissive surfaces show about twice as bright
//     where the two agree);
//   7 black: the final image is the game's effects target alone (the reference for 6);
//   8 ratio ours/game where both see emission: green within 25%, red ours brighter, blue
//     ours darker; magenta where the game shows emission and our data has none.
// The statistics go into the build's buffer; the host logs them ("[em] projected check").
// Mode 0 is the calibration's projected check (rt_em_calib: the approximate families' scale;
// GI hit emission skips an approximate family until it is calibrated). It runs on one
// interleaved slice of the pixels per run (rt_em_check_slices), so that a full screen is
// covered every rt_em_check_frames frames without a frame that traces them all.
// The shader's counters go to g_dev (device-local, group/wave reduced), then its `flush`
// entry adds them into the host's statistics region. Timed on its own: "[em] check GPU cost".
#include <algorithm>
#include <cstring>
#include <vector>

#include "core.h"

namespace {

VkDescriptorSetLayout g_push = VK_NULL_HANDLE, g_empty = VK_NULL_HANDLE, g_em_layout = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE;
VkPipeline g_pipe = VK_NULL_HANDLE, g_flush = VK_NULL_HANDLE;
uint64_t g_gen = 0, g_flush_gen = 0;
bool g_logged = false;

// emStats' layout (layer/emissive.cpp kStatWords, shaders/rt/em_debug.hlsl).
constexpr uint32_t kStatWords = 16 + 1024 * 16;
constexpr VkDeviceSize kStatBytes = VkDeviceSize(kStatWords) * 4;
VkBuffer g_dev = VK_NULL_HANDLE;
VkDeviceMemory g_dev_mem = VK_NULL_HANDLE;
bool g_dev_failed = false;

// Timestamps: per run, before the clear, after the check pass, after the flush.
constexpr uint32_t kTsRing = 8;
VkQueryPool g_qp = VK_NULL_HANDLE;
uint64_t g_ts_frame[kTsRing] = {};   // frame the slot was written (0: free)

// The schedule: every `per` frames one slice of `slices` (a power of two up to 64).
struct Sched { uint32_t slices = 0, per = 0; };

// The log window (rt_timing_frames frames).
struct Window {
  uint64_t start = 0;
  uint32_t runs = 0, views = 0;
  double pixels = 0;
  std::vector<float> ms, check_ms;
};
Window g_win;

struct Push {
  uint32_t full[2];
  uint32_t mode;
  float tol;        // relative depth agreement of a visible emissive hit
  float thr;        // effects-target luminance counted as emission (rt_em_fx_thr)
  uint32_t has_cam2;   // binding 6 holds the lighting's set 0 b2 (absolute camera position)
  float pad[2];
  uint32_t fam;      // rt_em_dbg_fam: views 19/20 (modes 9/10)
  uint32_t overlap;  // rt_em_overlap_check
  uint32_t grid;     // the slice: every grid.x-th column, grid.y-th row (x | y << 16)
  uint32_t origin;   // from origin (x | y << 16)
};

// A device-local buffer (the core/water.cpp pattern: the memory types from BG3_DEVICE_INFO_KEY).
bool make_device_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& b, VkDeviceMemory& m) {
  const VkDeviceFuncs& vk = *host->vk;
  const auto* d = static_cast<const Bg3DeviceInfo*>(host->persist(BG3_DEVICE_INFO_KEY, sizeof(Bg3DeviceInfo)));
  if (!d || d->version < 1) return false;
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  if (vk.CreateBuffer(host->device, &bci, nullptr, &b) != VK_SUCCESS) return b = VK_NULL_HANDLE, false;
  VkMemoryRequirements req;
  vk.GetBufferMemoryRequirements(host->device, b, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = UINT32_MAX;
  for (uint32_t i = 0; i < d->memory.memoryTypeCount && mai.memoryTypeIndex == UINT32_MAX; ++i)
    if ((req.memoryTypeBits & (1u << i)) && (d->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      mai.memoryTypeIndex = i;
  if (mai.memoryTypeIndex == UINT32_MAX || vk.AllocateMemory(host->device, &mai, nullptr, &m) != VK_SUCCESS ||
      vk.BindBufferMemory(host->device, b, m, 0) != VK_SUCCESS) {
    vk.DestroyBuffer(host->device, b, nullptr);
    if (m) vk.FreeMemory(host->device, m, nullptr);
    b = VK_NULL_HANDLE, m = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

bool ensure_dev() {
  if (g_dev) return true;
  if (g_dev_failed) return false;
  if (!make_device_buffer(kStatBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, g_dev,
                          g_dev_mem)) {
    g_dev_failed = true;
    host->log("em_debug: device-local statistics buffer unavailable: the check and the views do not run");
    return false;
  }
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qi.queryCount = kTsRing * 3;
  if (host->vk->CreateQueryPool(host->device, &qi, nullptr, &g_qp) != VK_SUCCESS) g_qp = VK_NULL_HANDLE;
  return true;
}

uint32_t floor_pow2(uint32_t v) {
  uint32_t p = 1;
  while (p * 2 <= v) p *= 2;
  return p;
}

Sched sched() {
  Sched s;
  if (!host->cfg_int("rt_em_calib", 1)) return s;
  const int every = host->cfg_int("rt_em_check_frames", 16);
  if (every <= 0) return s;
  const int want = (std::min)((std::max)(host->cfg_int("rt_em_check_slices", 16), 1), 64);
  s.slices = floor_pow2(uint32_t((std::min)(want, every)));
  s.per = (std::max)(uint32_t(every) / s.slices, 1u);
  return s;
}

double median(std::vector<float> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

// Reads finished timestamps; at the end of a window of rt_timing_frames frames, the log line.
void tick() {
  const uint64_t now = host->frame_index();
  const double period = host->cfg_float("gpu_timestamp_period_ns", 1.0f) * 1e-6;
  for (uint32_t s = 0; s < kTsRing && g_qp && bg3::logging::stats_enabled(host->cfg_int); ++s) {
    if (!g_ts_frame[s] || g_ts_frame[s] + 4 > now) continue;
    uint64_t t[6] = {};
    const VkResult r = host->vk->GetQueryPoolResults(host->device, g_qp, s * 3, 3, sizeof t, t, 16,
                                                     VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    const bool done = r == VK_SUCCESS && t[1] && t[3] && t[5];
    if (!done && g_ts_frame[s] + 60 > now) continue;
    if (done && t[4] >= t[0] && t[2] >= t[0]) {
      const double ms = double(t[4] - t[0]) * period, check = double(t[2] - t[0]) * period;
      if (ms >= 0 && ms < 1000) g_win.ms.push_back(float(ms)), g_win.check_ms.push_back(float(check));
    }
    g_ts_frame[s] = 0;
  }
  const uint64_t every = uint64_t(stats_period(unsigned((std::max)(host->cfg_int("rt_timing_frames", 300), 30))));
  if (!g_win.start) g_win.start = now;
  if (now < g_win.start + every) return;
  const uint64_t frames = now - g_win.start;
  if (host->cfg_int("rt_emissive", 0) && host->rt_emissive) {
    const Sched s = sched();
    double sum = 0, mx = 0;
    for (float v : g_win.ms) sum += v, mx = (std::max)(mx, double(v));
    // ms/frame: the timed runs' mean x the runs per frame (every run, timed or not).
    const double per_frame = g_win.ms.empty() ? 0.0 : sum / double(g_win.ms.size()) * double(g_win.runs) / double(frames);
    log_stats("[em] check GPU cost (%llu frames): %u runs (%u views), %.3f ms per run median (check pass %.3f, max %.3f, "
              "%zu timed), %.3f ms/frame; %.0f pixels per run; schedule rt_em_check_frames %d, slice 1/%u every %u "
              "frames%s",
              (unsigned long long)frames, g_win.runs, g_win.views, median(g_win.ms), median(g_win.check_ms), mx,
              g_win.ms.size(), per_frame, g_win.runs ? g_win.pixels / g_win.runs : 0.0,
              host->cfg_int("rt_em_check_frames", 16), s.slices, s.per, s.slices ? "" : " (check off)");
  }
  g_win = Window{};
  g_win.start = now;
}

bool make_pipe(VkPipeline& pipe, uint64_t& gen, const char* entry);

bool ensure(VkDescriptorSetLayout em_layout) {
  const VkDeviceFuncs& vk = *host->vk;
  VkDevice dev = host->device;
  if (!g_layout || g_em_layout != em_layout) {
    VkDescriptorSetLayout push = VK_NULL_HANDLE, empty = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayoutBinding b[8] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    ci.bindingCount = 8;
    ci.pBindings = b;
    if (vk.CreateDescriptorSetLayout(dev, &ci, nullptr, &push) != VK_SUCCESS) return false;
    VkDescriptorSetLayoutCreateInfo ei{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    if (vk.CreateDescriptorSetLayout(dev, &ei, nullptr, &empty) != VK_SUCCESS) {
      vk.DestroyDescriptorSetLayout(dev, push, nullptr);
      return false;
    }
    VkDescriptorSetLayout sets[4] = {push, empty, empty, em_layout};
    VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 4;
    li.pSetLayouts = sets;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    if (vk.CreatePipelineLayout(dev, &li, nullptr, &layout) != VK_SUCCESS) {
      vk.DestroyDescriptorSetLayout(dev, push, nullptr);
      vk.DestroyDescriptorSetLayout(dev, empty, nullptr);
      return false;
    }
    host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)g_pipe);
    host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)g_flush);
    host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_layout);
    host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_push);
    host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_empty);
    g_pipe = g_flush = VK_NULL_HANDLE;
    g_push = push;
    g_empty = empty;
    g_layout = layout;
    g_em_layout = em_layout;
    g_gen = g_flush_gen = 0;
  }
  return make_pipe(g_pipe, g_gen, "main") && make_pipe(g_flush, g_flush_gen, "flush");
}

bool make_pipe(VkPipeline& pipe, uint64_t& gen, const char* entry) {
  const VkDeviceFuncs& vk = *host->vk;
  VkDevice dev = host->device;
  Bg3ShaderBlob blob{};
  if (!host->get_shader("rt/em_debug.hlsl", entry, "cs_6_6", &blob)) return pipe != VK_NULL_HANDLE;
  if (pipe && blob.generation == gen) return true;
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  VkShaderModule mod = VK_NULL_HANDLE;
  if (vk.CreateShaderModule(dev, &mci, nullptr, &mod) != VK_SUCCESS) return pipe != VK_NULL_HANDLE;
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, entry,
               nullptr};
  pci.layout = g_layout;
  VkPipeline p = VK_NULL_HANDLE;
  VkResult r = vk.CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr, &p);
  vk.DestroyShaderModule(dev, mod, nullptr);
  if (r != VK_SUCCESS) {
    host->log("em_debug: pipeline creation failed (%s, %d)", entry, r);
    return pipe != VK_NULL_HANDLE;
  }
  host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)pipe);
  pipe = p;
  gen = blob.generation;
  return true;
}

void layout_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  host->vk->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                               0, nullptr, 1, &b);
}

}  // namespace

void em_debug(VkCommandBuffer cmd, const Bg3Binding& cam, const Bg3Binding& vp, const Bg3Binding& depth,
              const Bg3Binding& diffuse, const Bg3Binding& specular, const Bg3Binding& cam2, uint32_t w, uint32_t h,
              uint32_t mode) {
  if (!host->rt_emissive || (mode != 0 && (mode < 6 || mode > 10))) return;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
  Bg3EmissiveInfo info{};
  if (!host->rt_emissive(cmd, &layout, &set, &info) || !set || !layout || !info.fx_view || !info.fx_image ||
      info.fx_width != w || info.fx_height != h || info.built_frame + 3 < host->frame_index())
    return;
  // The effects target is sampled, then left as its last pass left it (unknown: no view).
  const VkImageLayout fx_from = info.fx_layout;
  if (fx_from == VK_IMAGE_LAYOUT_UNDEFINED || info.fx_image == diffuse.image || info.fx_image == specular.image ||
      !ensure_dev() || !ensure(layout))
    return;
  // The slice: mode 0 one of `slices` interleaved sets of pixels (grid sx x sy), the views all.
  uint32_t sx = 1, sy = 1, ox = 0, oy = 0;
  if (mode == 0) {
    const Sched s = sched();
    if (!s.slices) return;
    uint32_t lg = 0;
    while ((1u << lg) < s.slices) ++lg;
    sx = 1u << ((lg + 1) / 2);
    sy = s.slices / sx;
    const uint32_t slice = uint32_t((host->frame_index() / s.per) % s.slices);
    ox = slice % sx;
    oy = slice / sx;
  }
  em_note_check_slices(sx * sy);
  const VkDeviceFuncs& vk = *host->vk;
  const bool move = fx_from != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && fx_from != VK_IMAGE_LAYOUT_GENERAL;
  const VkImageLayout fx_in = move ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : fx_from;
  if (move) layout_barrier(cmd, info.fx_image, fx_from, fx_in);
  VkDescriptorBufferInfo cb{cam.buffer, cam.offset, cam.range}, vb{vp.buffer, vp.offset, vp.range};
  VkDescriptorImageInfo img[4] = {{VK_NULL_HANDLE, depth.view, depth.layout},
                                  {VK_NULL_HANDLE, diffuse.view, VK_IMAGE_LAYOUT_GENERAL},
                                  {VK_NULL_HANDLE, specular.view, VK_IMAGE_LAYOUT_GENERAL},
                                  {VK_NULL_HANDLE, info.fx_view, fx_in}};
  // The lighting's set 0 b2 (absolute camera position at byte 208), else the camera CB as a stand-in.
  const bool has_cam2 = cam2.buffer && cam2.range >= 224;
  VkDescriptorBufferInfo c2 = has_cam2 ? VkDescriptorBufferInfo{cam2.buffer, cam2.offset, 224} : cb;
  VkDescriptorBufferInfo devb{g_dev, 0, kStatBytes};
  VkWriteDescriptorSet wr[8];
  for (uint32_t i = 0; i < 8; ++i) {
    wr[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr[i].dstBinding = i;
    wr[i].descriptorCount = 1;
    if (i == 7) {
      wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      wr[i].pBufferInfo = &devb;
    } else if (i < 2 || i == 6) {
      wr[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      wr[i].pBufferInfo = i == 6 ? &c2 : i ? &vb : &cb;
    } else {
      wr[i].descriptorType = (i == 3 || i == 4) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      wr[i].pImageInfo = &img[i - 2];
    }
  }
  Push pc{{w, h}, mode, host->cfg_float("rt_em_tol", 0.01f), host->cfg_float("rt_em_fx_thr", 0.05f),
          has_cam2 ? 1u : 0u, {0.0f, 0.0f},
          // The family's FS hash, top 32 bits, as two 16-bit decimal keys (cfg_int is decimal):
          // 304be1c7 is rt_em_dbg_fam_hi=12363 (0x304b), rt_em_dbg_fam_lo=57799 (0xe1c7).
          (uint32_t(host->cfg_int("rt_em_dbg_fam_hi", 0) & 0xffff) << 16) | uint32_t(host->cfg_int("rt_em_dbg_fam_lo", 0) & 0xffff),
          host->cfg_int("rt_em_overlap_check", 0) != 0 ? 1u : 0u, sx | (sy << 16), ox | (oy << 16)};
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  VkMemoryBarrier rb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};   // an earlier flush's reads of g_dev before the clear
  rb.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  rb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  // Timestamps: a free slot of the ring, else this run is not timed.
  const uint64_t now = host->frame_index();
  uint32_t ts = kTsRing;
  for (uint32_t s = 0; s < kTsRing && g_qp && bg3::logging::stats_enabled(host->cfg_int) && ts == kTsRing; ++s)
    if (!g_ts_frame[s]) ts = s;
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &rb, 0, nullptr, 0,
                        nullptr);
  if (ts < kTsRing) {
    vk.CmdResetQueryPool(cmd, g_qp, ts * 3, 3);
    vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_qp, ts * 3);
    g_ts_frame[ts] = now ? now : 1;
  }
  vk.CmdFillBuffer(cmd, g_dev, 0, kStatBytes, 0);
  // The clear, and the scene built by the host's own submission after the previous frame's (AS builds, compute).
  vk.CmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe);
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_layout, 0, 8, wr);
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_layout, 3, 1, &set, 0, nullptr);
  vk.CmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  const uint32_t gw = (w + sx - 1) / sx, gh = (h + sy - 1) / sy;
  vk.CmdDispatch(cmd, (gw + 7) / 8, (gh + 7) / 8, 1);
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0,
                        nullptr, 0, nullptr);
  if (ts < kTsRing) vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_qp, ts * 3 + 1);
  // emStats += g_dev (the same layout and sets stay bound).
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_flush);
  vk.CmdDispatch(cmd, (kStatWords + 255) / 256, 1, 1);
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0, nullptr,
                        0, nullptr);
  if (ts < kTsRing) vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_qp, ts * 3 + 2);
  ++g_win.runs;
  if (mode != 0) ++g_win.views;
  g_win.pixels += double(gw) * double(gh);
  tick();
  if (move) layout_barrier(cmd, info.fx_image, fx_in, fx_from);
  if (!g_logged) {
    g_logged = true;
    host->log("em_debug: mode %u at %ux%u; %u records, %u instances, %u lights; effects target layout %d", mode, w, h,
              info.records, info.instances, info.lights, fx_from);
  }
}

// The projected check without a view (mode 0: statistics only) for the host's per-family
// calibration (rt_em_calib): one slice of rt_em_check_slices every rt_em_check_frames / slices
// frames, i.e. every pixel once per rt_em_check_frames frames. Called once per lighting pass.
bool em_check_due() {
  tick();
  const Sched s = sched();
  return s.slices && host->frame_index() % s.per == 0;
}

void em_release() {
  for (uint32_t s = 0; s < kTsRing; ++s) g_ts_frame[s] = 0;
  if (g_qp) host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_qp);
  g_qp = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)g_dev);
  host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_dev_mem);
  g_dev = VK_NULL_HANDLE;
  g_dev_mem = VK_NULL_HANDLE;
  g_dev_failed = false;
  g_win = Window{};
  em_note_check_slices(1);
  host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)g_flush);
  g_flush = VK_NULL_HANDLE;
  g_flush_gen = 0;
  host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)g_pipe);
  host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_layout);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_push);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_empty);
  g_pipe = VK_NULL_HANDLE;
  g_layout = VK_NULL_HANDLE;
  g_push = g_empty = g_em_layout = VK_NULL_HANDLE;
  g_logged = false;
}

void em_stats_reset() {
  g_win = Window{}; g_win.start = host->frame_index();
  std::fill_n(g_ts_frame, kTsRing, uint64_t(0));
}
