// RT sun replace: the upload side of the trust test (core/sun_trust.h).
#include "sun_trust.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kRing = 8;   // frames in flight are far fewer
constexpr VkDeviceSize kSlotBytes = 16384;
static_assert(kSunHeader + kSunMaxVolumes * sizeof(Bg3DbVolume) <= kSlotBytes, "trust slot too small");

// Header (bytes): 0 volume count, 4 flags (bit 0 replace, 1 roof volumes known, 2 off-screen
// test, 3 counters), 8 rise (m), 12 scene top (absolute y; -1e30 none), 16 frustum margin
// (uv), 20 ramp start (fraction of the needed path inside the frustum at which trust starts),
// 24 rt_sun_roof_mode (0 min(game, RT), 1 game mask, 2 shadow), 28 rt_sun_near (m; 0 off;
// only the edge/far split of the in-frame A/B debug class, never trust).
// GPU-written: 32 scene top this frame (order-preserving uint), 36 camera position
// (absolute, 3 floats), 48 the farthest pixel that needs the roof test (m, float bits;
// rt_sun_resolve.hlsl), 64-191 counters (32 uints: rt_shadow.hlsl, rt_sun_resolve.hlsl).
enum : uint32_t { kFReplace = 1, kFRoofs = 2, kFOffscreen = 4, kFStats = 8 };
constexpr uint32_t kCounters = 32;   // bytes 64-191

// The host ring is only staged through: the shaders read and count in a device-local
// R32_UINT image (one texel per 4 header bytes), copied from the slot before the sun
// passes and back into it (bytes 32-191) after them. Per-pixel reads and atomics on
// host-visible memory cost 14 ms a frame.
constexpr uint32_t kSlotTexels = uint32_t(kSlotBytes / 4);
VkBuffer g_buf = VK_NULL_HANDLE;
VkDeviceMemory g_mem = VK_NULL_HANDLE;
uint8_t* g_map = nullptr;
Bg3Image g_dev{};
bool g_dev_init = false;
bool g_failed = false;
uint32_t g_ring = 0;
uint64_t g_calls = 0;
bool g_have_cam = false;
float g_cam[3] = {}, g_last_query[3] = {1e30f, 1e30f, 1e30f};
float g_top = -1e30f;   // absolute y of the highest visible point (latest readback)
float g_dmax = -1.0f;   // farthest pixel needing the roof test (m, latest readback; < 0 none yet)
float g_radius = 0.0f;  // roof query radius of the latest refresh
int32_t g_over_logged = -2;   // the level of the last roof-overflow log line
uint64_t g_over_frames = 0, g_over_dropped = 0;   // stats window: frames over kSunMaxVolumes, volumes dropped
uint32_t g_over_max = 0;
std::vector<Bg3DbVolume> g_vol;
uint32_t g_total = 0;
int32_t g_level = -2;
uint64_t g_sum[kCounters] = {};
uint32_t g_sum_n = 0;

const Bg3AssetDbApi* api() {
  const Bg3AssetDbApi* a = host->abi_version >= 6 ? host->assetdb : nullptr;
  return a && a->version >= 3 && a->struct_size >= sizeof(Bg3AssetDbApi) && a->roof_volumes ? a : nullptr;
}

float from_ordered(uint32_t u) {
  u = (u & 0x80000000u) ? (u & 0x7FFFFFFFu) : ~u;
  float f;
  memcpy(&f, &u, 4);
  return f;
}

bool ensure() {
  if (g_buf && g_dev.image) return true;
  if (g_failed) return false;
  void* m = nullptr;
  if (!host->alloc_host_buffer ||
      !host->alloc_host_buffer(kSlotBytes * kRing, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               &g_buf, &g_mem, &m) ||
      !host->alloc_image(kSlotTexels, 1, VK_FORMAT_R32_UINT,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         &g_dev)) {
    host->log("sun: trust buffer allocation failed; rt_shadows off");
    sunt_release();
    g_failed = true;
    return false;
  }
  g_dev_init = false;
  g_map = static_cast<uint8_t*>(m);
  memset(g_map, 0, size_t(kSlotBytes) * kRing);
  for (uint32_t i = 0; i < kRing; ++i) {   // camera: NaN until the shader has run in that slot
    float nan[3] = {NAN, NAN, NAN};
    memcpy(g_map + i * kSlotBytes + 36, nan, sizeof nan);
  }
  return true;
}

// The roof query radius. A roof volume matters to a pixel only if that pixel's sun ray
// meets it. The pixel is at most g_dmax from the camera (the farthest pixel whose trust
// still depends on the roof test, measured on the GPU), and its ray counts for rt_tmax
// (the trace's own reach, at which the off-screen test also stops), so every volume that
// matters has a point within g_dmax + rt_tmax of the camera and passes api_roof_volumes'
// test (centre distance minus bounding radius <= radius). Before the first readback the
// pixels are taken to be within rt_tmax (the scene-top readback's own range): 2 rt_tmax.
// rt_sun_roof_radius > 0 overrides it (manual experiments only).
float roof_radius() {
  const float manual = host->cfg_float("rt_sun_roof_radius", 0.0f);
  if (manual > 0.0f) return manual;
  const float tmax = host->cfg_float("rt_tmax", 300.0f);
  return (g_dmax >= 0.0f ? g_dmax : tmax) + tmax;
}

// Re-reads the roof volumes around the camera, nearest first (api_roof_volumes sorts by
// distance). Beyond kSunMaxVolumes the farthest are dropped: logged once per level with
// the count, and counted in the "sun stats" line.
void refresh(const Bg3AssetDbApi* a, float pad_xz, float pad_y) {
  const float radius = roof_radius();
  g_radius = radius;
  g_vol.resize(kSunMaxVolumes);
  g_total = a->roof_volumes(g_cam, radius, g_vol.data(), kSunMaxVolumes);
  g_vol.resize(g_total < kSunMaxVolumes ? g_total : kSunMaxVolumes);
  if (g_total > kSunMaxVolumes) {
    const int32_t level = a->current_level(nullptr, nullptr);
    if (level != g_over_logged) {
      g_over_logged = level;
      host->log("sun: WARNING %u roof volumes within %.0f m of the camera, only the nearest %u uploaded "
                "(kSunMaxVolumes): %u dropped, pixels under them can be trusted (leak)",
                g_total, radius, kSunMaxVolumes, g_total - kSunMaxVolumes);
    }
  }
  for (Bg3DbVolume& v : g_vol) {   // padding: eaves overhang the trigger, the roof sits above it
    float up[3];
    const float q[4] = {v.rot[0], v.rot[1], v.rot[2], v.rot[3]};
    // local +y in world: rotate (0, 1, 0) by q
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    up[0] = 2 * (x * y - w * z), up[1] = 1 - 2 * (x * x + z * z), up[2] = 2 * (y * z + w * x);
    v.half[0] += pad_xz, v.half[2] += pad_xz, v.half[1] += 0.5f * pad_y;
    for (int k = 0; k < 3; ++k) v.center[k] += up[k] * 0.5f * pad_y;
  }
  memcpy(g_last_query, g_cam, sizeof g_cam);
}

}  // namespace

bool sunt_begin(VkCommandBuffer cmd, SunTrustFrame* f, bool have_cam2) {
  if (!ensure()) return false;
  g_ring = (g_ring + 1) % kRing;
  uint8_t* slot = g_map + size_t(g_ring) * kSlotBytes;
  // What the shader wrote into this slot kRing frames ago.
  float cam[3];
  memcpy(cam, slot + 36, sizeof cam);
  if (std::isfinite(cam[0]) && std::isfinite(cam[1]) && std::isfinite(cam[2])) {
    memcpy(g_cam, cam, sizeof g_cam);
    g_have_cam = true;
    uint32_t top;
    memcpy(&top, slot + 32, 4);
    if (top != 0) g_top = from_ordered(top);
    float dmax;   // 0: the resolve pass did not run (rt_sun_replace=0) or no pixel needed roofs
    memcpy(&dmax, slot + 48, 4);
    if (std::isfinite(dmax) && dmax > 0.0f) g_dmax = dmax;
    const uint32_t* c = reinterpret_cast<const uint32_t*>(slot + 64);
    for (uint32_t i = 0; i < kCounters; ++i) g_sum[i] += c[i];
    ++g_sum_n;
  }
  const bool replace = host->cfg_int("rt_sun_replace", 1) != 0;
  const bool want_roofs = host->cfg_int("rt_sun_roofs", 1) != 0;
  const Bg3AssetDbApi* a = api();
  const char* lname = "";
  int32_t level = a && a->available() ? a->current_level(&lname, nullptr) : -1;
  const bool roofs = want_roofs && level >= 0 && g_have_cam && have_cam2;
  if (roofs) {
    const float dx = g_cam[0] - g_last_query[0], dy = g_cam[1] - g_last_query[1], dz = g_cam[2] - g_last_query[2];
    if (level != g_level || g_calls % 30 == 0 || dx * dx + dy * dy + dz * dz > 25.0f)
      refresh(a, host->cfg_float("rt_sun_roof_pad", 1.0f), host->cfg_float("rt_sun_roof_up", 3.0f));
    if (level != g_level)
      host->log("sun: rt_sun_replace %d: level %s, %u roof volumes within %.0f m of the camera (%zu uploaded)", replace,
                lname, g_total, g_radius, g_vol.size());
    g_level = level;
    if (g_total > kSunMaxVolumes) {
      ++g_over_frames;
      g_over_dropped += g_total - kSunMaxVolumes;
      if (g_total > g_over_max) g_over_max = g_total;
    }
  } else {
    g_vol.clear();
  }
  ++g_calls;
  // Without the roofs (no database, level unknown, or no camera yet), nothing may be
  // trusted unless rt_sun_roofs=0 says so: interiors would leak.
  const bool active = replace && have_cam2 && (roofs || !want_roofs);
  uint32_t flags = (active ? kFReplace : 0u) | (roofs ? kFRoofs : 0u) |
                   (host->cfg_int("rt_sun_offscreen", 1) ? kFOffscreen : 0u) | kFStats;
  const uint32_t n = uint32_t(g_vol.size());
  const float pad_top = host->cfg_float("rt_sun_top_pad", 2.0f);
  // rt_sun_top=0: the ray need only rise rt_sun_rise above the pixel (the scene top, the
  // highest visible point, keeps nearly every outdoor pixel off screen behind tree tops).
  const bool use_top = host->cfg_int("rt_sun_top", 0) != 0;
  float hf[6] = {host->cfg_float("rt_sun_rise", 6.0f), use_top && g_top > -1e29f ? g_top + pad_top : -1e30f,
                 host->cfg_float("rt_sun_margin", 0.0f), host->cfg_float("rt_sun_ramp", 0.75f), 0.0f, 0.0f};
  uint32_t mode = uint32_t(host->cfg_int("rt_sun_roof_mode", 0));
  const float near_m = host->cfg_float("rt_sun_near", 0.0f);   // A/B debug split only
  memcpy(slot + 0, &n, 4);
  memcpy(slot + 4, &flags, 4);
  memcpy(slot + 8, hf, 16);
  memcpy(slot + 24, &mode, 4);
  memcpy(slot + 28, &near_m, 4);
  memset(slot + 32, 0, 4);
  float nan[3] = {NAN, NAN, NAN};
  memcpy(slot + 36, nan, sizeof nan);
  memset(slot + 48, 0, 4);
  memset(slot + 64, 0, kCounters * 4);
  if (n) memcpy(slot + kSunHeader, g_vol.data(), n * sizeof(Bg3DbVolume));
  // Stage into the device-local image (header + this frame's volumes).
  const VkDeviceFuncs& vk = *host->vk;
  const VkPipelineStageFlags kXfer = VK_PIPELINE_STAGE_TRANSFER_BIT, kCS = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  ib.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
  ib.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  ib.oldLayout = g_dev_init ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
  ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  ib.image = g_dev.image;
  ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vk.CmdPipelineBarrier(cmd, kCS | kXfer, kXfer, 0, 0, nullptr, 0, nullptr, 1, &ib);
  g_dev_init = true;
  VkBufferImageCopy r{};
  r.bufferOffset = VkDeviceSize(g_ring) * kSlotBytes;
  r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  r.imageExtent = {(kSunHeader + n * uint32_t(sizeof(Bg3DbVolume))) / 4, 1, 1};
  vk.CmdCopyBufferToImage(cmd, g_buf, g_dev.image, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
  ib.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  ib.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  vk.CmdPipelineBarrier(cmd, kXfer, kCS, 0, 0, nullptr, 0, nullptr, 1, &ib);
  f->view = g_dev.view;
  f->replace = active;
  f->volumes = n;

  if (g_sum_n >= 600) {   // counters, as fractions of the traced pixels
    const double px = double(g_sum[0] ? g_sum[0] : 1), dis = double(g_sum[2] ? g_sum[2] : 1);
    host->log("sun stats (%u frames, replace %d, %zu roof volumes, scene top %.1f): traced %.1f%% of px, lit %.1f%%; "
              "game dark & RT lit %.2f%% of traced px: replaced (lit) %.1f%%, partly %.1f%%, kept by roofs %.1f%%, "
              "kept off screen %.1f%%; px under roofs %.1f%%, off-screen ramp %.1f%%; game lit & RT shadowed %.2f%%; "
              "mean brightening %.4f per traced px; roof query %.0f m (farthest roof-tested px %.0f m), over "
              "kSunMaxVolumes on %llu frames (max %u volumes, %.1f dropped per such frame)",
              g_sum_n, int(active), g_vol.size(), g_top, 100.0 * g_sum[0] / double(g_sum[15] ? g_sum[15] : 1),
              100.0 * g_sum[1] / px, 100.0 * g_sum[2] / px, 100.0 * g_sum[3] / dis, 100.0 * g_sum[4] / dis,
              100.0 * g_sum[5] / dis, 100.0 * g_sum[6] / dis, 100.0 * g_sum[7] / px, 100.0 * g_sum[8] / px,
              100.0 * g_sum[9] / px, g_sum[10] / 255.0 / px, g_radius, g_dmax, (unsigned long long)g_over_frames,
              g_over_max, g_over_frames ? double(g_over_dropped) / double(g_over_frames) : 0.0);
    // In-frame A/B (rt_sun_resolve.hlsl): the written mask against min(game, traced) of the
    // same pixel and frame, by at least one R8 code (1/255). "At the edge": the game's own
    // mask is partly lit there, or lit within half of rt_sun_near (a debug split only).
    const double all = double(g_sum[15] ? g_sum[15] : 1), br = double(g_sum[16] ? g_sum[16] : 1);
    host->log("sun check (%u frames, replace %d): brightened by >= 1/255 against min(game, RT) on %.3f%% of px (%.0f "
              "per frame): at the game's shadow edge %.1f%%, farther %.1f%% (far: %.3f%% of px); darker than min "
              "%.4f%% of px; mean gain %.5f per px (%.1f%% of it at the edge)",
              g_sum_n, int(active), 100.0 * g_sum[16] / all, double(g_sum[16]) / g_sum_n, 100.0 * g_sum[17] / br,
              100.0 * g_sum[18] / br, 100.0 * g_sum[18] / all, 100.0 * g_sum[19] / all, g_sum[10] / 255.0 / all,
              100.0 * g_sum[20] / double(g_sum[10] ? g_sum[10] : 1));
    memset(g_sum, 0, sizeof g_sum);
    g_sum_n = 0;
    g_over_frames = g_over_dropped = 0;
    g_over_max = 0;
  }
  return true;
}

void sunt_end(VkCommandBuffer cmd) {
  if (!g_buf || !g_dev.image || !g_dev_init) return;
  const VkDeviceFuncs& vk = *host->vk;
  VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  ib.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  ib.oldLayout = ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  ib.image = g_dev.image;
  ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                        nullptr, 1, &ib);
  VkBufferImageCopy r{};   // bytes 32-191: scene top, camera, counters
  r.bufferOffset = VkDeviceSize(g_ring) * kSlotBytes + 32;
  r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  r.imageOffset = {8, 0, 0};
  r.imageExtent = {8 + kCounters, 1, 1};   // texels 8 .. 16 + kCounters
  vk.CmdCopyImageToBuffer(cmd, g_dev.image, VK_IMAGE_LAYOUT_GENERAL, g_buf, 1, &r);
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0,
                        nullptr);
}

void sunt_release() {
  if (g_buf) {
    host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)g_buf);
    host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_mem);
  }
  if (g_dev.image) {
    host->defer_destroy(BG3_DEFER_IMAGE_VIEW, (uint64_t)g_dev.view);
    host->defer_destroy(BG3_DEFER_IMAGE, (uint64_t)g_dev.image);
    host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_dev.memory);
  }
  g_dev = Bg3Image{};
  g_dev_init = false;
  g_buf = VK_NULL_HANDLE;
  g_mem = VK_NULL_HANDLE;
  g_map = nullptr;
  g_failed = g_have_cam = false;
  g_vol.clear();
  g_total = 0;
  g_level = -2;
  g_top = -1e30f;
  g_dmax = -1.0f;
  g_radius = 0.0f;
  g_over_logged = -2;
  g_over_frames = g_over_dropped = 0;
  g_over_max = 0;
  memset(g_sum, 0, sizeof g_sum);
  g_sum_n = 0;
}
