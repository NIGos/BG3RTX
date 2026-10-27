// Track C: screen-space GI for BG3 (docs/TRACK_C_GI.md).
//
// BG3's direct lighting is a tiled compute pass (about 15 dispatch_indirect
// permutations). The core recognises it from what is bound (set 1: RG16F
// normals b1, albedo b2, material b3, R32F linear depth b5, RGB10A2 AO b6,
// R11G11B10F diffuse/specular storage outputs b10/b11; set 0: camera UBO b1,
// viewport UBO b4) and re-uses those exact descriptors.
//
//  pre  (before the first lighting dispatch): trace + temporal at half res,
//       then fold visibility into the game's AO so its own ambient (diffuse
//       and specular) is occluded by it.
//  post (end of the lighting's command buffer, or the next render pass):
//       add one-bounce radiance into the diffuse buffer before SSS and fog,
//       and keep the lit radiance for next frame's ray hits.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core.h"
#include "ls_lights.h"

namespace {

struct Snapshot {
  Bg3Binding cam, vp, nrm, alb, mat, dep, ao, shadow, dif, spe;
  // D-LS: the lighting's light list (set 1 b0, b13, b23, b24, b25, b39, b40, b41; set 0 b2).
  Bg3Binding l_flags, l_scale, l_clusters, l_index, l_lights, l_consts, l_shadows, l_atlas, l_cam2;
  bool ls_ok;                        // all of them are bound
  // D-RF: the lighting's own set 1 (bound as our set 2: probes, cubes, DFG LUT, samplers).
  VkDescriptorSet game_set1;
  VkDescriptorSetLayout game_set1_layout;
  uint32_t game_set1_dyn[16], game_set1_ndyn;
  VkAccelerationStructureKHR tlas;   // the TLAS the ray-traced passes used (Track D)
  VkDescriptorSet alpha;             // the host's set 1 for them (alpha-tested geometry, phase 3)
  bool gi;                           // the GI passes ran (gi=1); otherwise only the sun shadow work
  bool ls;                           // D-LS runs after the lighting
  bool rf;                           // D-RF runs after the lighting
  bool rf_validate;                  // D-RF EN4 check: the prep pass ran (AO 1, no sun), D-LS skipped
  // RT audit fixes: material class parameters (set 1 b4 RT3, set 1 b28 SSS profiles)
  // and the lighting's frame constants (set 0 b0: _m4 = SSS diffuse colour 1).
  Bg3Binding x_rt3, x_prof, x_frame;
  // The sun's inputs for the D-LS pass's AO-on-direct compensation (ao_direct):
  // set 5 b0 atmosphere constants, b1 transmittance LUT, b4/b5 and set 1 b42/b43 cloud shadows.
  Bg3Binding x_atmo, x_tlut, x_ca0, x_ca1, x_cb0, x_cb1;
  bool rt_sun;   // the RT sun shadows were folded into the mask this frame
  bool aux_ao;   // gi_ao_apply kept the game's AO in gAux.x this frame
};

struct Pipe {
  VkPipeline pipeline = VK_NULL_HANDLE;
  uint64_t generation = 0;
};

struct Push {
  uint32_t full[2], half[2];
  uint32_t frame;
  float strength, radius, thickness;
  uint32_t rays, steps, debug;
  float history, ao;
  float cs_length, cs_strength;
  uint32_t cs_steps;
  float mv_scale[2];
  uint32_t has_mv, pad;
  float rt_bias, rt_tmax;
  uint32_t rt_debug;
  float rt_strength;
  float rt_ao_radius;
  uint32_t rt_ao_rays;
  uint32_t pad2[2];
  float gi_rt_radius, gi_rt_tol;
  uint32_t gi_rt_fallback, rt_stats;
  float sun_radius;
  uint32_t res_shift;
  uint32_t pad3[2];
  float ls_source_radius, ls_clear, ls_min_radius;
  uint32_t ls_samples, ls_shadowed, ls_debug, ls_stats, ls_pass, ls_soft;
  float ls_strength;
  uint32_t ls_has_mask, ls_flags;   // ls_flags: bit 0 per-light data (ls_lights), bits 8-12 owners traced (ls_owners)
  float rf_max_rough, rf_mirror_rough, rf_tmax, rf_depth_tol, rf_edge, rf_strength;
  uint32_t rf_debug, rf_sample, rf_so, rf_validate, rf_models, rf_stats;
  // RT audit fixes (docs/AUDIT_RT.md)
  uint32_t rt_backlit, mat_classes, ao_direct, rt_origin;
};
static_assert(sizeof(Push) == 256, "Push must match gi_common.hlsli");

constexpr uint32_t kBindings = 18;
// The ray-traced passes add the TLAS (18), their visibility image (19) and a
// statistics buffer (20, rt_stats=1).
constexpr uint32_t kRtBindings = kBindings + 3;
// D-LS adds the game's light list: 21 set1/b0 flags (UBO), 22 b13 scale, 23 b23
// clusters, 24 b24 index list, 25 b25 lights (SSBOs), 26 b39 constants (UBO),
// 27 b40 shadow views (SSBO), 28 b41 shadow atlas (sampled), 29 set0/b2 (UBO).
constexpr uint32_t kLsBindings = kRtBindings + 9;
// D-RF adds its hit distance image (30; RR's DLSSD.SpecularHitDistance). 31 push
// descriptors, within the 32 every device must support.
constexpr uint32_t kRfBindings = kLsBindings + 1;

// Bindings past the contiguous ranges (docs/AUDIT_RT.md fixes), in every layout whose
// bit is set: 1 base, 2 ray-traced, 4 D-LS.
struct ExtraBinding {
  uint32_t binding;
  VkDescriptorType type;
  uint32_t layouts;
};
constexpr ExtraBinding kExtras[] = {
    {40, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 | 4},   // rtOrigin: geometric normal + surface offset (rt_origin=1)
    {41, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4},       // RT3, set 1 b4 (class parameters)
    {42, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},      // SSS profiles, set 1 b28
    {43, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 7},      // set 0 b0 frame constants
    {44, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 7},       // gAux: .x the game's own AO before the fold (ao_direct)
    {45, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},      // D-LS per-light data: runtime lights uploaded (core/ls_lights.cpp)
    {46, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4},       // D-LS per-light data: one record per light array slot
};
VkSampler g_linear_clamp = VK_NULL_HANDLE;

// maxPushDescriptors is 32 on the RTX 5090: the D-LS push layout holds only the
// bindings rt_local_shadows.hlsl uses (26), and the sun's inputs (rt/bg3_sun.hlsli)
// go into an ordinary set 2, a ring of kSunRing sets written per frame:
//   0 set 5 b0 atmosphere constants (UBO), 1 set 5 b1 transmittance LUT, 2/3 set 5
//   b4/b5 cloud shadow layer A, 4/5 set 1 b42/b43 layer B, 6 our linear-clamp sampler.
// Which of the layouts (kExtras bits) a contiguous binding count is: 1 base, 2 ray-traced, 4 D-LS, 8 D-RF.
uint32_t layout_mask(uint32_t count) {
  return count == kBindings ? 1u : count == kRtBindings ? 2u : count == kLsBindings ? 4u : 8u;
}
bool ls_uses(uint32_t i) { return i <= 7 || i == 12 || i == 16 || i == 18 || i >= 20; }
constexpr uint32_t kSunRing = 8;
VkDescriptorSetLayout g_sun_dsl = VK_NULL_HANDLE;
VkDescriptorPool g_sun_pool = VK_NULL_HANDLE;
VkDescriptorSet g_sun_sets[kSunRing] = {};
uint32_t g_sun_slot = 0;
constexpr uint32_t kNumExtras = sizeof kExtras / sizeof kExtras[0];

std::mutex g_mu;
std::unordered_map<VkCommandBuffer, Snapshot> g_pending;   // pre done, post owed
VkDescriptorSetLayout g_set_layout = VK_NULL_HANDLE, g_rt_set_layout = VK_NULL_HANDLE, g_ls_set_layout = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE, g_rt_layout = VK_NULL_HANDLE, g_ls_layout = VK_NULL_HANDLE;
Pipe g_trace, g_temporal, g_ao_apply, g_composite, g_contact, g_rt_shadow, g_rt_debug, g_rt_ao, g_trace_rt, g_ls, g_ls_prep;
Pipe g_rt_origin;   // robust ray origins (rt_origin=1, AUDIT_RT fix 6)
Pipe g_ls_match;    // D-LS per-light data: joins the runtime lights to the light array (core/ls_lights.h)
// D-LS variants (docs/TRACK_D_RT.md 9.9): g_ls serves the default settings only, g_ls_full every
// mode (ls_samples, ls_shadowed, ls_debug, ls_stats). A/B: ls_impl=0 runs the frozen pre-optimisation
// pass (shaders/gi/ls_ref, tools/lsref); ls_check=1 runs it and the live variant on the same pixels
// and counts those whose outputs differ (stats 80-83).
Pipe g_ls_full, g_ls_ref, g_ls_check, g_ls_full_check;
LslFrame g_lsl{};   // this frame's upload (bindings 45, 46)
bool g_lsl_ok = false;
bool g_ls_logged = false, g_ls_missing_logged = false;
// D-RF: its pipeline is built against the game's set layout of the lighting's set 1
// (bound as set 2), so it is rebuilt if that layout ever changes.
VkDescriptorSetLayout g_rf_set_layout = VK_NULL_HANDLE, g_rf_game_layout = VK_NULL_HANDLE;
VkPipelineLayout g_rf_layout = VK_NULL_HANDLE;
Pipe g_rf;
bool g_rf_logged = false, g_rf_missing_logged = false;
Bg3Image g_rf_hit{};   // R16F specular hit distance for RR (world units; 65504 = no traced hit)
Bg3Image g_raw{}, g_hist[2]{}, g_raw_depth{}, g_hist_depth[2]{}, g_prev_radiance{}, g_rt_vis{};
Bg3Image g_origin{}, g_aux{};   // extras 40 and 44 (full resolution, RGBA16F)
Bg3Image g_ls_nodata{};          // extra 46 when there is no per-light data: 1x1 RGBA32UI (the shaders' type)
bool g_x_logged = false;
uint32_t g_w = 0, g_h = 0, g_hw = 0, g_hh = 0, g_parity = 0, g_shift = 1;

// RR mode (docs/QUALITY.md fix 3): with Ray Reconstruction denoising the frame,
// the GI traces 1 ray per full-res pixel and keeps no history of its own, the
// input RR is designed for. gi_rr: -1 follows rr, 0 off, 1 on.
uint32_t gi_res_shift() {
  int m = host->cfg_int("gi_rr", -1);
  bool rr = m < 0 ? host->rr_active() : m != 0;
  return rr ? 0u : 1u;
}
bool g_images_ready = false, g_logged = false;
uint64_t g_last_pre_frame = 0;

// Motion vectors (from the NGX hook) and the layout their render pass leaves them in.
VkImage g_mv_image = VK_NULL_HANDLE;
VkImageView g_mv_view = VK_NULL_HANDLE;
VkImageLayout g_mv_layout = VK_IMAGE_LAYOUT_UNDEFINED;
float g_mv_scale[2] = {0, 0};
uint64_t g_mv_frame = 0;   // frame in which the velocity pass was last seen writing the MVs

// GPU timing: 4 timestamps per frame (pre start/end, post start/end) in a ring
// of 8 frames, read back a few frames later without waiting.
constexpr uint32_t kRing = 8;
VkQueryPool g_queries = VK_NULL_HANDLE;
uint32_t g_slot = 0;
bool g_slot_written[kRing] = {};
double g_ns_per_tick = 1.0, g_ms_sum = 0;
uint32_t g_ms_count = 0;

void timing_init() {
  if (g_queries) return;
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qi.queryCount = kRing * 4;
  host->vk->CreateQueryPool(host->device, &qi, nullptr, &g_queries);
  g_ns_per_tick = host->cfg_float("gpu_timestamp_period_ns", 1.0f);
}

void timestamp(VkCommandBuffer cmd, uint32_t index) {
  if (!g_queries) return;
  uint32_t q = g_slot * 4 + index;
  host->vk->CmdResetQueryPool(cmd, g_queries, q, 1);
  host->vk->CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_queries, q);
}

// Reads the oldest ring slot if the GPU is done with it.
void timing_collect() {
  if (!g_queries) return;
  uint32_t old = (g_slot + 1) % kRing;
  if (!g_slot_written[old]) return;
  uint64_t t[4];
  if (host->vk->GetQueryPoolResults(host->device, g_queries, old * 4, 4, sizeof t, t, sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    return;
  g_slot_written[old] = false;
  double ms = (double(t[1] - t[0]) + double(t[3] - t[2])) * g_ns_per_tick * 1e-6;
  if (ms >= 0 && ms < 1000) {
    g_ms_sum += ms;
    ++g_ms_count;
  }
  if (g_ms_count >= 300) {
    host->log("gi: GPU cost %.3f ms/frame (average of %u frames)", g_ms_sum / g_ms_count, g_ms_count);
    g_ms_sum = 0;
    g_ms_count = 0;
  }
}

Bg3Image* const all_images[] = {&g_raw, &g_hist[0], &g_hist[1], &g_raw_depth, &g_hist_depth[0], &g_hist_depth[1],
                                &g_prev_radiance, &g_rt_vis, &g_rf_hit, &g_origin, &g_aux, &g_ls_nodata};

// Track D: GPU time of the ray-traced shadow pass, 2 timestamps per frame.
VkQueryPool g_rt_queries = VK_NULL_HANDLE;
uint32_t g_rt_slot = 0;
bool g_rt_written[kRing] = {};
double g_rt_ms_sum = 0;
uint32_t g_rt_ms_count = 0;
uint64_t g_rt_frames_traced = 0, g_rt_frames_stale = 0, g_rt_frames_missing = 0;
bool g_rt_logged = false;

void rt_timing_collect() {
  if (!g_rt_queries) return;
  uint32_t old = (g_rt_slot + 1) % kRing;
  if (!g_rt_written[old]) return;
  uint64_t t[2];
  if (host->vk->GetQueryPoolResults(host->device, g_rt_queries, old * 2, 2, sizeof t, t, sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    return;
  g_rt_written[old] = false;
  double ms = double(t[1] - t[0]) * g_ns_per_tick * 1e-6;
  if (ms >= 0 && ms < 1000) {
    g_rt_ms_sum += ms;
    ++g_rt_ms_count;
  }
  if (g_rt_ms_count >= 300) {
    host->log("rt passes: GPU cost %.3f ms/frame (average of %u frames); TLAS this frame %llu, previous frame %llu, "
              "none %llu", g_rt_ms_sum / g_rt_ms_count, g_rt_ms_count, (unsigned long long)g_rt_frames_traced,
              (unsigned long long)g_rt_frames_stale, (unsigned long long)g_rt_frames_missing);
    g_rt_ms_sum = 0;
    g_rt_ms_count = 0;
    g_rt_frames_traced = g_rt_frames_stale = g_rt_frames_missing = 0;
  }
}

void rt_timestamp(VkCommandBuffer cmd, uint32_t index) {
  if (!g_rt_queries) return;
  uint32_t q = g_rt_slot * 2 + index;
  host->vk->CmdResetQueryPool(cmd, g_rt_queries, q, 1);
  host->vk->CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_rt_queries, q);
}

// GI trace pass alone (screen-space march or ray queries), 2 timestamps per frame.
VkQueryPool g_tr_queries = VK_NULL_HANDLE;
uint32_t g_tr_slot = 0;
bool g_tr_written[kRing] = {}, g_tr_rt[kRing] = {};
std::vector<float> g_tr_ms[2];   // [0] screen-space, [1] ray-traced

void trace_timestamp(VkCommandBuffer cmd, uint32_t index, bool rt = false) {
  if (!g_tr_queries) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kRing * 2;
    if (host->vk->CreateQueryPool(host->device, &qi, nullptr, &g_tr_queries) != VK_SUCCESS) return;
  }
  uint32_t q = g_tr_slot * 2 + index;
  host->vk->CmdResetQueryPool(cmd, g_tr_queries, q, 1);
  host->vk->CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_tr_queries, q);
  if (index == 1) {
    g_tr_written[g_tr_slot] = true;
    g_tr_rt[g_tr_slot] = rt;
    g_tr_slot = (g_tr_slot + 1) % kRing;
  }
}

void trace_timing_collect() {
  if (!g_tr_queries) return;
  uint32_t old = (g_tr_slot + 1) % kRing;
  if (!g_tr_written[old]) return;
  uint64_t t[2];
  if (host->vk->GetQueryPoolResults(host->device, g_tr_queries, old * 2, 2, sizeof t, t, sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    return;
  g_tr_written[old] = false;
  double ms = double(t[1] - t[0]) * g_ns_per_tick * 1e-6;
  if (ms >= 0 && ms < 1000) g_tr_ms[g_tr_rt[old] ? 1 : 0].push_back(float(ms));
  for (int k = 0; k < 2; ++k) {
    auto& v = g_tr_ms[k];
    if (v.size() < 300) continue;
    std::sort(v.begin(), v.end());
    bool rr_mode = g_shift == 0;
    host->log("gi: trace pass (%s, %u rays/sample, %s) GPU median %.3f ms, p10 %.3f, p90 %.3f (%zu frames)",
              k ? "ray-traced, gi_rt=1" : "screen-space",
              uint32_t(rr_mode ? host->cfg_int("gi_rr_rays", 1) : host->cfg_int("gi_rays", 2)),
              rr_mode ? "full res, RR denoises" : "half res, own accumulation", v[v.size() / 2], v[v.size() / 10],
              v[v.size() * 9 / 10], v.size());
    v.clear();
  }
}

bool snapshot(VkCommandBuffer cmd, Snapshot& s) {
  auto q = [&](uint32_t set, uint32_t b, Bg3Binding& o) {
    return host->query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, set, b, 0, &o);
  };
  if (!q(1, 10, s.dif) || s.dif.format != VK_FORMAT_B10G11R11_UFLOAT_PACK32 || !s.dif.view) return false;
  if (!q(1, 11, s.spe) || s.spe.format != VK_FORMAT_B10G11R11_UFLOAT_PACK32 || !s.spe.view) return false;
  if (!q(1, 1, s.nrm) || s.nrm.format != VK_FORMAT_R16G16_SFLOAT) return false;
  if (!q(1, 2, s.alb) || !q(1, 3, s.mat) || !q(1, 5, s.dep) || s.dep.format != VK_FORMAT_R32_SFLOAT) return false;
  if (!q(1, 6, s.ao) || s.ao.format != VK_FORMAT_A2B10G10R10_UNORM_PACK32) return false;
  if (!q(1, 7, s.shadow) || s.shadow.format != VK_FORMAT_R8_UNORM) s.shadow = Bg3Binding{};
  if (!q(0, 1, s.cam) || !s.cam.buffer || !q(0, 4, s.vp) || !s.vp.buffer) return false;
  // D-LS: the light list (optional; the pass is skipped without it).
  auto qb = [&](uint32_t set, uint32_t b, Bg3Binding& o) { return q(set, b, o) && o.buffer && o.range > 0; };
  s.ls_ok = qb(1, 0, s.l_flags) && qb(1, 13, s.l_scale) && qb(1, 23, s.l_clusters) && qb(1, 24, s.l_index) &&
            qb(1, 25, s.l_lights) && qb(1, 39, s.l_consts) && qb(1, 40, s.l_shadows) && qb(0, 2, s.l_cam2) &&
            q(1, 41, s.l_atlas) && s.l_atlas.view;
  // Optional (the passes fall back to class 0 without them).
  if (!q(1, 4, s.x_rt3) || !s.x_rt3.view || s.x_rt3.width != s.dif.width) s.x_rt3 = Bg3Binding{};
  if (!qb(1, 28, s.x_prof)) s.x_prof = Bg3Binding{};
  if (!qb(0, 0, s.x_frame) || s.x_frame.range < 32) s.x_frame = Bg3Binding{};
  if (!qb(5, 0, s.x_atmo) || s.x_atmo.range < 8) s.x_atmo = Bg3Binding{};
  auto qi = [&](uint32_t set, uint32_t b, Bg3Binding& o) {
    if (!q(set, b, o) || !o.view) o = Bg3Binding{};
  };
  qi(5, 1, s.x_tlut);
  qi(5, 4, s.x_ca0);
  qi(5, 5, s.x_ca1);
  qi(1, 42, s.x_cb0);
  qi(1, 43, s.x_cb1);
  // D-RF: the lighting's set 1 itself (optional; the pass is skipped without it).
  s.game_set1_ndyn = 16;
  if (!host->query_set ||
      !host->query_set(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, 1, &s.game_set1, &s.game_set1_layout, s.game_set1_dyn,
                       &s.game_set1_ndyn)) {
    s.game_set1 = VK_NULL_HANDLE;
    s.game_set1_layout = VK_NULL_HANDLE;
    s.game_set1_ndyn = 0;
  }
  return s.nrm.width == s.dif.width && s.dep.width == s.dif.width && s.ao.width == s.dif.width && s.dif.width > 0;
}

void release_images() {
  // RR must stop using the hit distance image before it goes.
  if (host->set_spec_hit_distance) host->set_spec_hit_distance(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED, 0, 0);
  for (Bg3Image* im : all_images) {
    host->defer_destroy(BG3_DEFER_IMAGE_VIEW, (uint64_t)im->view);
    host->defer_destroy(BG3_DEFER_IMAGE, (uint64_t)im->image);
    host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)im->memory);
    *im = Bg3Image{};
  }
  g_images_ready = false;
  g_w = g_h = 0;
}

bool ensure_images(uint32_t w, uint32_t h) {
  uint32_t shift = gi_res_shift();
  if (w == g_w && h == g_h && shift == g_shift && g_raw.image) return true;
  release_images();
  g_shift = shift;
  uint32_t hw = shift ? (w + 1) / 2 : w, hh = shift ? (h + 1) / 2 : h;
  VkImageUsageFlags use = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  bool ok = host->alloc_image(hw, hh, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_raw) &&
            host->alloc_image(hw, hh, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_hist[0]) &&
            host->alloc_image(hw, hh, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_hist[1]) &&
            host->alloc_image(hw, hh, VK_FORMAT_R32_SFLOAT, use, &g_raw_depth) &&
            host->alloc_image(hw, hh, VK_FORMAT_R32_SFLOAT, use, &g_hist_depth[0]) &&
            host->alloc_image(hw, hh, VK_FORMAT_R32_SFLOAT, use, &g_hist_depth[1]) &&
            host->alloc_image(w, h, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_prev_radiance) &&
            host->alloc_image(w, h, VK_FORMAT_R8G8B8A8_UNORM, use, &g_rt_vis) &&
            host->alloc_image(w, h, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_origin) &&
            host->alloc_image(w, h, VK_FORMAT_R16G16B16A16_SFLOAT, use, &g_aux) &&
            host->alloc_image(1, 1, VK_FORMAT_R32G32B32A32_UINT, use, &g_ls_nodata) &&
            host->alloc_image(w, h, VK_FORMAT_R16_SFLOAT, use | VK_IMAGE_USAGE_SAMPLED_BIT, &g_rf_hit);
  if (!ok) {
    host->log("gi: image allocation failed");
    release_images();
    return false;
  }
  g_w = w, g_h = h, g_hw = hw, g_hh = hh;
  host->log("gi: resources for %ux%u (GI at %ux%u)", w, h, hw, hh);
  return true;
}

VkDescriptorType binding_type(uint32_t i) {
  if (i == kLsBindings) return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;   // D-RF hit distance
  if (i >= kRtBindings)
    return (i == 21 || i == 26 || i == 29) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
           : i == 28                       ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                           : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  return i <= 1                ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
         : (i <= 5 || i == 17) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
         : i == 18             ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
         : i == 20             ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                               : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
}

// `set1`: the host's set layout for the ray-traced passes (alpha-tested geometry).
bool make_layout(uint32_t count, VkDescriptorSetLayout* set_layout, VkPipelineLayout* layout,
                 VkDescriptorSetLayout set1 = VK_NULL_HANDLE, VkDescriptorSetLayout set2 = VK_NULL_HANDLE) {
  VkDescriptorSetLayoutBinding b[kRfBindings + kNumExtras];
  const uint32_t mask = layout_mask(count);
  uint32_t n = 0;
  for (uint32_t i = 0; i < count; ++i)
    if (mask != 4u || ls_uses(i)) b[n++] = {i, binding_type(i), 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  for (const ExtraBinding& x : kExtras)
    if (x.layouts & mask) b[n++] = {x.binding, x.type, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  sci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
  sci.bindingCount = n;
  sci.pBindings = b;
  if (host->vk->CreateDescriptorSetLayout(host->device, &sci, nullptr, set_layout) != VK_SUCCESS) return false;
  VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
  VkDescriptorSetLayout sets[3] = {*set_layout, set1, set2};
  VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lci.setLayoutCount = set2 ? 3 : set1 ? 2 : 1;
  lci.pSetLayouts = sets;
  lci.pushConstantRangeCount = 1;
  lci.pPushConstantRanges = &pc;
  return host->vk->CreatePipelineLayout(host->device, &lci, nullptr, layout) == VK_SUCCESS;
}

bool ensure_layout() {
  if (g_layout) return true;
  return make_layout(kBindings, &g_set_layout, &g_layout);
}

// Only on a device with ray tracing (rt=1): the layout has an AS binding, and
// set 1 is the host's (alpha-tested geometry, Track D phase 3).
bool ensure_rt_layout() {
  if (g_rt_layout) return true;
  VkDescriptorSetLayout host_layout = VK_NULL_HANDLE;
  VkDescriptorSet unused;
  if (!host->rt_alpha || !host->rt_alpha(&host_layout, &unused) || !host_layout) return false;
  return make_layout(kRtBindings, &g_rt_set_layout, &g_rt_layout, host_layout);
}

// D-RF: push set 0..30, the host's set 1, and the game's own layout of the
// lighting's set 1 as set 2. A different game layout rebuilds the pipeline.
bool ensure_rf_layout(VkDescriptorSetLayout game) {
  if (g_rf_layout && g_rf_game_layout == game) return true;
  VkDescriptorSetLayout host_layout = VK_NULL_HANDLE;
  VkDescriptorSet unused;
  if (!game || !host->rt_alpha || !host->rt_alpha(&host_layout, &unused) || !host_layout) return false;
  if (g_rf_layout) {
    host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)g_rf.pipeline);
    host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_rf_layout);
    host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_rf_set_layout);
    g_rf = Pipe{};
    g_rf_layout = VK_NULL_HANDLE;
    g_rf_set_layout = VK_NULL_HANDLE;
    host->log("rf: the lighting's set 1 layout changed (%p -> %p); rebuilding", (void*)g_rf_game_layout, (void*)game);
  }
  if (!make_layout(kRfBindings, &g_rf_set_layout, &g_rf_layout, host_layout, game)) {
    g_rf_layout = VK_NULL_HANDLE;
    return false;
  }
  g_rf_game_layout = game;
  return true;
}

bool ensure_ls_layout() {
  if (g_ls_layout) return true;
  if (!g_linear_clamp) {
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 16.0f;
    if (host->vk->CreateSampler(host->device, &si, nullptr, &g_linear_clamp) != VK_SUCCESS) return false;
  }
  if (!g_sun_dsl) {
    VkDescriptorSetLayoutBinding b[7];
    for (uint32_t i = 0; i < 7; ++i)
      b[i] = {i, i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : i == 6 ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
              1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sci.bindingCount = 7;
    sci.pBindings = b;
    if (host->vk->CreateDescriptorSetLayout(host->device, &sci, nullptr, &g_sun_dsl) != VK_SUCCESS) return false;
    VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSunRing},
                                  {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 5 * kSunRing},
                                  {VK_DESCRIPTOR_TYPE_SAMPLER, kSunRing}};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = kSunRing;
    pci.poolSizeCount = 3;
    pci.pPoolSizes = ps;
    if (host->vk->CreateDescriptorPool(host->device, &pci, nullptr, &g_sun_pool) != VK_SUCCESS) return false;
    VkDescriptorSetLayout ls[kSunRing];
    for (auto& l : ls) l = g_sun_dsl;
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = g_sun_pool;
    ai.descriptorSetCount = kSunRing;
    ai.pSetLayouts = ls;
    if (host->vk->AllocateDescriptorSets(host->device, &ai, g_sun_sets) != VK_SUCCESS) return false;
  }
  VkDescriptorSetLayout host_layout = VK_NULL_HANDLE;
  VkDescriptorSet unused;
  if (!host->rt_alpha || !host->rt_alpha(&host_layout, &unused) || !host_layout) return false;
  return make_layout(kLsBindings, &g_ls_set_layout, &g_ls_layout, host_layout, g_sun_dsl);
}

bool refresh(Pipe& p, const char* file, VkPipelineLayout layout = VK_NULL_HANDLE) {
  Bg3ShaderBlob blob{};
  if (!host->get_shader(file, "main", "cs_6_6", &blob)) return p.pipeline != VK_NULL_HANDLE;
  if (p.pipeline && blob.generation == p.generation) return true;
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  // A real module: chaining VkShaderModuleCreateInfo with a null module needs
  // maintenance5, which the game's device does not enable (VUID-...-stage-08771).
  VkShaderModule mod = VK_NULL_HANDLE;
  if (host->vk->CreateShaderModule(host->device, &mci, nullptr, &mod) != VK_SUCCESS) {
    host->log("gi: %s shader module creation failed; keeping the previous pipeline", file);
    return p.pipeline != VK_NULL_HANDLE;
  }
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main",
               nullptr};
  pci.layout = layout ? layout : g_layout;
  VkPipeline np = VK_NULL_HANDLE;
  VkResult pr = host->vk->CreateComputePipelines(host->device, VK_NULL_HANDLE, 1, &pci, nullptr, &np);
  host->vk->DestroyShaderModule(host->device, mod, nullptr);
  if (pr != VK_SUCCESS) {
    host->log("gi: %s pipeline creation failed; keeping the previous one", file);
    return p.pipeline != VK_NULL_HANDLE;
  }
  host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)p.pipeline);
  p.pipeline = np;
  p.generation = blob.generation;
  host->log("gi: %s generation %llu", file, (unsigned long long)blob.generation);
  return true;
}

void memory_barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst) {
  // MEMORY_* access is valid with every stage mask.
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  host->vk->CmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void image_layout(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags src,
                  VkPipelineStageFlags dst) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcAccessMask = (src & VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT) ? 0 : VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  host->vk->CmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// Everything the passes need, bound through one push-descriptor layout.
struct Bound {
  VkDescriptorBufferInfo cam, vp, stats;
  VkDescriptorBufferInfo ls_buf[8];
  VkDescriptorImageInfo ls_atlas;
  VkDescriptorImageInfo img[kRtBindings - 2];
  VkDescriptorImageInfo rf_hit;
  VkWriteDescriptorSet w[kRfBindings];
  VkWriteDescriptorSetAccelerationStructureKHR as;
  VkAccelerationStructureKHR tlas;
  VkDescriptorSet alpha;
  // kExtras, in table order
  VkWriteDescriptorSet xw[kNumExtras];
  VkDescriptorImageInfo ximg[kNumExtras];
  VkDescriptorBufferInfo xbuf[kNumExtras];
};

// The extras' descriptors, with fallbacks when the game did not bind a source
// (the shaders are told through Push::mat_classes bits 1-3).
void bind_extras(Bound& b, const Snapshot& s) {
  const VkImageLayout G = VK_IMAGE_LAYOUT_GENERAL;
  for (uint32_t i = 0; i < kNumExtras; ++i) {
    b.xw[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    b.xw[i].dstBinding = kExtras[i].binding;
    b.xw[i].descriptorCount = 1;
    b.xw[i].descriptorType = kExtras[i].type;
    b.ximg[i] = {};
    b.xbuf[i] = {};
    switch (kExtras[i].binding) {
      case 40: b.ximg[i] = {VK_NULL_HANDLE, g_origin.view, G}; break;
      case 41:
        b.ximg[i] = s.x_rt3.view ? VkDescriptorImageInfo{VK_NULL_HANDLE, s.x_rt3.view, s.x_rt3.layout}
                                 : VkDescriptorImageInfo{VK_NULL_HANDLE, s.alb.view, s.alb.layout};
        break;
      case 42:
        b.xbuf[i] = s.x_prof.buffer     ? VkDescriptorBufferInfo{s.x_prof.buffer, s.x_prof.offset, s.x_prof.range}
                    : s.l_lights.buffer ? VkDescriptorBufferInfo{s.l_lights.buffer, s.l_lights.offset, s.l_lights.range}
                                        : VkDescriptorBufferInfo{s.l_scale.buffer, s.l_scale.offset, s.l_scale.range};
        break;
      case 43:
        b.xbuf[i] = s.x_frame.buffer
                        ? VkDescriptorBufferInfo{s.x_frame.buffer, s.x_frame.offset, (std::min)(s.x_frame.range, VkDeviceSize(65536))}
                        : VkDescriptorBufferInfo{s.cam.buffer, s.cam.offset, s.cam.range};
        break;
      case 44: b.ximg[i] = {VK_NULL_HANDLE, g_aux.view, G}; break;
      // D-LS per-light data; without it (allocation failed) any valid descriptor of the
      // type: the passes then never read them (ls_flags bit 0 clear, no match pass).
      case 45: b.xbuf[i] = g_lsl_ok ? g_lsl.known : VkDescriptorBufferInfo{s.l_lights.buffer, s.l_lights.offset, s.l_lights.range}; break;
      // (a UINT view either way: VUID-vkCmdDispatch-format-07753 checks the static use)
      case 46: b.ximg[i] = {VK_NULL_HANDLE, g_lsl_ok ? g_lsl.slots_view : g_ls_nodata.view, G}; break;
    }
    const bool buf = kExtras[i].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || kExtras[i].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b.xw[i].pImageInfo = buf ? nullptr : &b.ximg[i];
    b.xw[i].pBufferInfo = buf ? &b.xbuf[i] : nullptr;
  }
}

// The writes for one layout (`count` contiguous bindings + its extras) into `out`.
uint32_t gather_writes(const Bound& b, uint32_t count, VkWriteDescriptorSet* out) {
  const uint32_t mask = layout_mask(count);
  uint32_t n = 0;
  for (uint32_t i = 0; i < count; ++i)
    if (mask != 4u || ls_uses(i)) out[n++] = b.w[i];
  for (uint32_t i = 0; i < kNumExtras; ++i)
    if (kExtras[i].layouts & mask) out[n++] = b.xw[i];
  return n;
}

// Statistics of the ray-traced passes (rt_stats=1), counted on the GPU with
// wave-reduced atomics into a host-visible ring, read kStatsRing frames later:
//   [0] pixels with depth, [1..4] primary-ray check (rt_debug=3/5): match, hit
//   in front, hit behind, miss; [5..7] in front on opaque geometry / a vt
//   cutout / a tex cutout; [13, 14] behind through a vt / tex cutout;
//   [8..12] hybrid GI rays: traced, hits, hits shaded from the screen, on
//   screen but hidden, off screen.
// Each counter has 64 bins (spread atomics); the slot holds 16 x 64 uints.
constexpr uint32_t kStatsRing = 8;
// [16..38] D-LS (rt_local_shadows.hlsl), [39..47] materials (rt_shadow.hlsl), [48..63] D-RF
// (rt_reflections.hlsl), [64..75] RT audit fixes (D-LS per class, rt_origin, ao_direct),
// [80..111] alpha-tested candidates of visibility / shadow rays (rt_common.hlsli alpha_stat),
// [112..117] D-LS bit-exactness checks (ls_check=1): the pass, the join's kept slots.
constexpr uint32_t kStatsCounters = 118;
constexpr VkDeviceSize kStatsSlot = kStatsCounters * 64 * 4;
VkBuffer g_stats_buf = VK_NULL_HANDLE;
VkDeviceMemory g_stats_mem = VK_NULL_HANDLE;
uint32_t* g_stats_map = nullptr;
uint32_t g_stats_slot = 0;
uint64_t g_stats_frame[kStatsRing] = {};
uint64_t g_stats_sum[kStatsCounters] = {};
float g_ls_raw[4] = {};   // set1/b0 m0.x, m0.y, m0.w, b13[0] as last written
float g_frame_m4 = 0;     // set0/b0 _m4 (skin shaded white), raw counter 54
uint32_t g_stats_n = 0;

bool stats_init() {
  if (g_stats_buf) return true;
  if (!host->alloc_host_buffer) return false;
  void* m = nullptr;
  if (!host->alloc_host_buffer(kStatsSlot * kStatsRing, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               &g_stats_buf, &g_stats_mem, &m))
    return false;
  g_stats_map = static_cast<uint32_t*>(m);
  return true;
}

// Picks this frame's slot (cleared on the GPU) after folding in the one written
// kStatsRing - 1 frames ago.
void stats_begin(VkCommandBuffer cmd, bool alpha_test) {
  uint64_t now = host->frame_index();
  uint32_t slot = uint32_t(now % kStatsRing);
  if (g_stats_frame[slot] && g_stats_frame[slot] + kStatsRing - 1 <= now) {
    const uint32_t* c = g_stats_map + slot * (kStatsSlot / 4);
    for (int i = 0; i < int(kStatsCounters); ++i)
      if ((i < 27 || i > 30) && i != 70)   // (70: a raw float)
        for (int b = 0; b < 64; ++b) g_stats_sum[i] += c[i * 64 + b];
    for (int i = 27; i <= 30; ++i) std::memcpy(&g_ls_raw[i - 27], &c[i * 64], 4);   // raw floats (latest)
    std::memcpy(&g_frame_m4, &c[70 * 64], 4);
    ++g_stats_n;
  }
  g_stats_slot = slot;
  g_stats_frame[slot] = now;
  host->vk->CmdFillBuffer(cmd, g_stats_buf, VkDeviceSize(slot) * kStatsSlot, kStatsSlot, 0);
  memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  if (g_stats_n < 120) return;
  const uint64_t* t = g_stats_sum;
  if (t[0]) {
    double px = double(t[0]);
    host->log("rt stats (%u frames, alpha test %s): primary-ray check match %.3f%%, in front %.3f%% (opaque geometry "
              "%.3f, vt cutouts %.3f, tex cutouts %.3f), behind %.3f%% (through vt %.3f, tex %.3f), miss %.3f%%",
              g_stats_n, alpha_test ? "on" : "off", 100.0 * t[1] / px, 100.0 * t[2] / px, 100.0 * t[5] / px,
              100.0 * t[6] / px, 100.0 * t[7] / px, 100.0 * t[3] / px, 100.0 * t[13] / px, 100.0 * t[14] / px,
              100.0 * t[4] / px);
  }
  if (t[8]) {
    double r = double(t[8]), h = double(t[9] ? t[9] : 1);
    host->log("rt stats (%u frames): hybrid GI %.2fM rays/frame, %.1f%% hit; of the hits %.1f%% shaded from the screen, "
              "%.1f%% on screen but hidden, %.1f%% off screen",
              g_stats_n, r / g_stats_n * 1e-6, 100.0 * t[9] / r, 100.0 * t[10] / h, 100.0 * t[11] / h, 100.0 * t[12] / h);
  }
  if (t[39]) {   // rt_shadow.hlsl: material classes and back-lit transmission (AUDIT_RT fix 2)
    double px = double(t[39]), tr = double(t[40] ? t[40] : 1);
    host->log("rt stats (%u frames): material classes: non-standard %.3f%% of px (skin %.3f%%, translucent %.3f%%, "
              "hair %.3f%%); transmitting px with N.L < 0.2588: %.3f%% of px, game sun mask lit on %.1f%% of them, "
              "traced lit %.1f%%, game lit but traced occluded %.1f%%",
              g_stats_n, 100.0 * t[47] / px, 100.0 * t[45] / px, 100.0 * t[44] / px, 100.0 * t[46] / px,
              100.0 * t[40] / px, 100.0 * t[41] / tr, 100.0 * t[42] / tr, 100.0 * t[43] / tr);
  }
  if (t[16]) {   // rt_local_shadows.hlsl, ao_direct (AUDIT_RT fix 4)
    double px = double(t[16]), lit = double(t[21] ? t[21] : 1);
    host->log("ls stats (%u frames): ao_direct %d gives back %.5f per px (%.2f%% of the lit luminance); the sun as "
              "recomputed: %.5f per px",
              g_stats_n, host->cfg_int("ao_direct", 1), t[74] / px / 256.0, 100.0 * t[74] / lit, t[75] / px / 256.0);
    if (t[76] + t[79]) {   // per-light data (rt_local_shadows_match.hlsl), per light evaluation
      double ev = double(t[76] + t[79]);
      host->log("ls stats (%u frames): per-light data: %.2f light evaluations per px; matched %.1f%% (bridge %.1f%%, "
                "database only %.1f%%), unmatched %.1f%%; not traced by owner (ls_owners=0x%X) %.2f%% of evaluations",
                g_stats_n, ev / px, 100.0 * t[76] / ev, 100.0 * t[77] / ev, 100.0 * (t[76] - t[77]) / ev,
                100.0 * t[79] / ev, host->cfg_int("ls_owners", 0x0F), 100.0 * t[78] / ev);
    }
  }
  if (t[112]) {   // ls_check=1: the reference D-LS (shaders/gi/ls_ref) against the live one, same frame and inputs
    host->log("ls check (%u frames): %llu px written by the reference; outputs differ in any bit on %llu px, by more "
              "than 1e-5 relative on %llu, written by only one of them on %llu",
              g_stats_n, (unsigned long long)t[112], (unsigned long long)t[113], (unsigned long long)t[114],
              (unsigned long long)t[115]);
    host->log("ls check (%u frames): join: %llu slot results kept from the previous frame, %llu of them differ from "
              "a new search",
              g_stats_n, (unsigned long long)t[116], (unsigned long long)t[117]);
  }
  for (int c = 0; c < 2; ++c) {   // alpha-tested candidates (rt_common.hlsli alpha_stat)
    const uint64_t* a = t + 80 + 16 * c;
    if (!a[0]) continue;
    double n = double(a[0]), e = double(a[6] + a[7] + a[8] + a[9] ? a[6] + a[7] + a[8] + a[9] : 1);
    host->log("rt stats (%u frames): alpha candidates of %s rays %.3fM/frame; solid unevaluated: no record %.3f%%, no "
              "UV %.3f%%, prim out of range %.3f%%, non-finite %.3f%%, vt tile set not resident %.3f%%; evaluated: vt "
              "solid %.2f%% clear %.2f%%, tex solid %.2f%% clear %.2f%%; first cut-off < 0.1: %.2f%% of evaluated (solid "
              "%.2f%% of those); blur level >= 2 %.2f%%, >= 4 %.2f%%; stochastic %.2f%%; vt page from a coarser level %.2f%%",
              g_stats_n, c ? "shadow" : "visibility", n / g_stats_n * 1e-6, 100.0 * a[1] / n, 100.0 * a[2] / n,
              100.0 * a[3] / n, 100.0 * a[4] / n, 100.0 * a[5] / n, 100.0 * a[6] / e, 100.0 * a[7] / e,
              100.0 * a[8] / e, 100.0 * a[9] / e, 100.0 * a[10] / e, a[10] ? 100.0 * a[11] / double(a[10]) : 0.0,
              100.0 * a[12] / e, 100.0 * a[13] / e, 100.0 * a[14] / e, 100.0 * a[15] / e);
  }
  if (t[71]) {   // rt_origin.hlsl (AUDIT_RT fix 6)
    host->log("rt stats (%u frames): ray origins: TLAS surface found under %.2f%% of px (else depth normal); "
              "geometric and shading normal differ by > 25 deg on %.2f%%",
              g_stats_n, 100.0 * t[72] / double(t[71]), 100.0 * t[73] / double(t[71]));
  }
  if (t[16]) {
    double px = double(t[16]), n = double(g_stats_n);
    host->log("ls stats (%u frames): %.2fM px/frame; lights per pixel: listed %.2f, evaluated %.2f (with a game shadow "
              "%.2f), traced %.2f, occluded %.2f; %.1f%% of px lit by a local light; light removed %.2f%% of the lit "
              "luminance (mean %.4f); validation (px without projector lights): game %.4f, recomputed %.4f, mean |diff| %.5f (%.3f%% of game), recompute above game %.5f (%.3f%%; %.3f%% of px); "
              "set1/b0 m0.x %g m0.y (AO on direct) %g m0.w %g, b13[0] %g; projector lights listed %.2f/px (not handled)",
              g_stats_n, px / n * 1e-6, t[26] / px, t[17] / px, t[18] / px, t[19] / px, t[20] / px, 100.0 * t[25] / px,
              t[21] ? 100.0 * t[24] / double(t[21]) : 0.0, t[24] / px / 256.0, t[21] / px / 256.0, t[22] / px / 256.0,
              t[15] / px / 256.0, t[21] ? 100.0 * t[15] / double(t[21]) : 0.0, t[23] / px / 256.0, t[21] ? 100.0 * t[23] / double(t[21]) : 0.0, 100.0 * t[20] / px, g_ls_raw[0], g_ls_raw[1], g_ls_raw[2],
              g_ls_raw[3], t[31] / px);
  }
  if (t[69]) {   // D-LS on non-standard material classes (rt_local_shadows.hlsl, AUDIT_RT fix 3)
    host->log("ls stats (%u frames): non-standard material px %.4fM/frame; validation on them (lit, no projector "
              "lights; ls_debug=4): %.4fM px, game %.4f, recomputed %.4f, mean |diff| %.5f, recompute above game %.5f; "
              "set0/b0 _m4 (skin shaded white) %g",
              g_stats_n, t[69] / double(g_stats_n) * 1e-6, t[64] / double(g_stats_n) * 1e-6,
              t[64] ? t[65] / double(t[64]) / 256.0 : 0.0, t[64] ? t[66] / double(t[64]) / 256.0 : 0.0,
              t[64] ? t[68] / double(t[64]) / 256.0 : 0.0, t[64] ? t[67] / double(t[64]) / 256.0 : 0.0, g_frame_m4);
  }
  if (t[32] > 100) {
    double N = double(t[32]), sm = t[33] * 1e-5, sg = t[34] * 1e-5, smm = t[35] * 1e-7, smg = t[36] * 1e-7;
    double k = (N * smg - sm * sg) / (std::max)(N * smm - sm * sm, 1e-30);
    host->log("ls validation fit (px without projector lights): game = %.4f x recomputed + %.5f over %.2fM lit px "
              "(mean recomputed %.5f, game %.5f); game where no local light reaches %.5f (%.2fM px)",
              k, (sg - k * sm) / N, N / g_stats_n * 1e-6, sm / N, sg / N, t[37] ? t[38] * 1e-5 / double(t[37]) : 0.0,
              double(t[37]) / g_stats_n * 1e-6);
  }
  if (t[48]) {
    double px = double(t[48]), n = double(g_stats_n), q = 1.0 / 4096.0;
    host->log("rf stats (%u frames): %.2fM px/frame, %.1f%% traced (%.2fM rays/frame), %.1f%% other shading model; of "
              "the traced: on-screen hit (tier a) %.1f%%, hidden on screen %.1f%%, off screen %.1f%%, miss %.1f%%, below "
              "horizon %.1f%%; local probes per px %.2f; mean luminance: game specular %.5f, probe term %.5f, probe term "
              "after D-RF %.5f",
              g_stats_n, px / n * 1e-6, 100.0 * t[49] / px, t[49] / n * 1e-6, 100.0 * t[55] / px,
              t[49] ? 100.0 * t[50] / double(t[49]) : 0.0, t[49] ? 100.0 * t[51] / double(t[49]) : 0.0,
              t[49] ? 100.0 * t[52] / double(t[49]) : 0.0, t[49] ? 100.0 * t[53] / double(t[49]) : 0.0,
              t[49] ? 100.0 * t[54] / double(t[49]) : 0.0, t[59] / px, t[58] * q / px, t[56] * q / px, t[57] * q / px);
    if (t[60]) {
      double v = double(t[60]);
      host->log("rf validation (AO 1, no sun; %.2fM px/frame without projector lights): game specular %.5f, local lights "
                "+ probe term recomputed %.5f, mean |residual| %.5f (%.2f%% of the game's), recompute above the game "
                "%.5f (%.2f%%)",
                v / n * 1e-6, t[58] * q / v, t[61] * q / v, t[62] * q / v, t[58] ? 100.0 * t[62] / double(t[58]) : 0.0,
                t[63] * q / v, t[58] ? 100.0 * t[63] / double(t[58]) : 0.0);
    }
  }
  std::memset(g_stats_sum, 0, sizeof g_stats_sum);
  g_stats_n = 0;
}

// The light list for the D-LS pass (bindings 21-29).
void bind_ls(Bound& b, const Snapshot& s) {
  const Bg3Binding* src[9] = {&s.l_flags, &s.l_scale, &s.l_clusters, &s.l_index, &s.l_lights,
                              &s.l_consts, &s.l_shadows, nullptr, &s.l_cam2};
  uint32_t k = 0;
  for (uint32_t i = 0; i < 9; ++i) {
    uint32_t bind = kRtBindings + i;
    b.w[bind] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    b.w[bind].dstBinding = bind;
    b.w[bind].descriptorCount = 1;
    b.w[bind].descriptorType = binding_type(bind);
    if (!src[i]) {
      b.ls_atlas = {VK_NULL_HANDLE, s.l_atlas.view, s.l_atlas.layout};
      b.w[bind].pImageInfo = &b.ls_atlas;
      continue;
    }
    VkDeviceSize range = src[i]->range;
    if (b.w[bind].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) range = (std::min)(range, VkDeviceSize(65536));
    b.ls_buf[k] = {src[i]->buffer, src[i]->offset, range};
    b.w[bind].pBufferInfo = &b.ls_buf[k++];
  }
}

// Adds the TLAS and the visibility image (bindings 18, 19) for the RT passes.
void bind_rt(Bound& b, VkAccelerationStructureKHR tlas, VkDescriptorSet alpha) {
  b.tlas = tlas;
  b.alpha = alpha;
  b.as = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
  b.as.accelerationStructureCount = 1;
  b.as.pAccelerationStructures = &b.tlas;
  b.w[18] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  b.w[18].pNext = &b.as;
  b.w[18].dstBinding = 18;
  b.w[18].descriptorCount = 1;
  b.w[18].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  b.img[17] = {VK_NULL_HANDLE, g_rt_vis.view, VK_IMAGE_LAYOUT_GENERAL};
  b.w[19] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  b.w[19].dstBinding = 19;
  b.w[19].descriptorCount = 1;
  b.w[19].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  b.w[19].pImageInfo = &b.img[17];
  b.stats = {g_stats_buf, VkDeviceSize(g_stats_slot) * kStatsSlot, kStatsSlot};
  b.w[20] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  b.w[20].dstBinding = 20;
  b.w[20].descriptorCount = 1;
  b.w[20].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  b.w[20].pBufferInfo = &b.stats;
}

void bind_all(Bound& b, const Snapshot& s) {
  Bg3Image& hin = g_hist[g_parity];
  Bg3Image& hout = g_hist[g_parity ^ 1];
  Bg3Image& din = g_hist_depth[g_parity];
  Bg3Image& dout = g_hist_depth[g_parity ^ 1];
  b.cam = {s.cam.buffer, s.cam.offset, s.cam.range};
  b.vp = {s.vp.buffer, s.vp.offset, s.vp.range};
  const VkImageLayout G = VK_IMAGE_LAYOUT_GENERAL;
  const VkDescriptorImageInfo img[kBindings - 2] = {
      {VK_NULL_HANDLE, s.nrm.view, s.nrm.layout}, {VK_NULL_HANDLE, s.alb.view, s.alb.layout},
      {VK_NULL_HANDLE, s.mat.view, s.mat.layout}, {VK_NULL_HANDLE, s.dep.view, s.dep.layout},
      {VK_NULL_HANDLE, s.dif.view, G},           {VK_NULL_HANDLE, s.spe.view, G},
      {VK_NULL_HANDLE, g_raw.view, G},           {VK_NULL_HANDLE, hin.view, G},
      {VK_NULL_HANDLE, hout.view, G},            {VK_NULL_HANDLE, g_prev_radiance.view, G},
      {VK_NULL_HANDLE, s.ao.view, G},            {VK_NULL_HANDLE, g_raw_depth.view, G},
      {VK_NULL_HANDLE, din.view, G},             {VK_NULL_HANDLE, dout.view, G},
      {VK_NULL_HANDLE, s.shadow.view ? s.shadow.view : s.ao.view, G},
      {VK_NULL_HANDLE, g_mv_view ? g_mv_view : s.nrm.view,
       g_mv_view ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : s.nrm.layout}};
  memcpy(b.img, img, sizeof img);
  for (uint32_t i = 0; i < kBindings; ++i) {
    b.w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    b.w[i].dstBinding = i;
    b.w[i].descriptorCount = 1;
    if (i <= 1) {
      b.w[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      b.w[i].pBufferInfo = i == 0 ? &b.cam : &b.vp;
    } else {
      b.w[i].descriptorType = (i <= 5 || i == 17) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      b.w[i].pImageInfo = &b.img[i - 2];
    }
  }
  bind_extras(b, s);
}

// Tells the shaders which material inputs exist (Push::mat_classes bits 1-3), and logs it once.
void mat_bits(Push& pc, const Snapshot& s, bool post) {
  if (pc.mat_classes) pc.mat_classes |= (s.x_rt3.view ? 2u : 0u) | (s.x_prof.buffer ? 4u : 0u) | (s.x_frame.buffer ? 8u : 0u);
  // ao_direct bits: 0-1 mode, 2 the sun was ray traced this frame, 3 the sun's
  // inputs are bound (atmosphere LUT only needed with the atmosphere on), 4 gAux.x holds the game's AO.
  const bool sun_in = s.x_atmo.buffer && s.x_tlut.view && s.x_ca0.view && s.x_ca1.view && s.x_cb0.view && s.x_cb1.view;
  pc.ao_direct = (pc.ao_direct & 3u) | (s.rt_sun ? 4u : 0u) | (sun_in ? 8u : 0u) | (s.aux_ao ? 16u : 0u);
  if (post && !g_x_logged) {
    g_x_logged = true;
    host->log("gi: material inputs: RT3 (set1 b4) %s, SSS profiles (set1 b28) %s (%llu bytes), frame constants (set0 b0) %s; "
              "sun inputs (set5 b0/b1/b4/b5, set1 b42/b43): %d%d%d%d%d%d",
              s.x_rt3.view ? "found" : "missing", s.x_prof.buffer ? "found" : "missing",
              (unsigned long long)s.x_prof.range, s.x_frame.buffer ? "found" : "missing", s.x_atmo.buffer != VK_NULL_HANDLE,
              s.x_tlut.view != VK_NULL_HANDLE, s.x_ca0.view != VK_NULL_HANDLE, s.x_ca1.view != VK_NULL_HANDLE,
              s.x_cb0.view != VK_NULL_HANDLE, s.x_cb1.view != VK_NULL_HANDLE);
  }
}

Push make_push() {
  Push pc{};
  pc.full[0] = g_w, pc.full[1] = g_h, pc.half[0] = g_hw, pc.half[1] = g_hh;
  pc.frame = uint32_t(host->frame_index());
  pc.strength = host->cfg_float("gi_strength", 1.0f);
  pc.radius = host->cfg_float("gi_radius", 0.15f);
  pc.thickness = host->cfg_float("gi_thickness", 0.03f);
  pc.rays = uint32_t(host->cfg_int("gi_rays", 2));
  pc.res_shift = g_shift;
  // Half-angle of the sun's disc (rad): the game's own sun specular clamps R into
  // a cone of sin = 0.0045900648 around L (f469db78:1428), i.e. 0.263 deg.
  pc.sun_radius = host->cfg_float("rt_sun_radius", 0.00459f);
  pc.steps = uint32_t(host->cfg_int("gi_steps", 12));
  pc.debug = uint32_t(host->cfg_int("gi_debug", 0));
  pc.history = host->cfg_float("gi_history", 0.1f);
  pc.ao = host->cfg_float("gi_ao", 1.0f);
  if (g_shift == 0) {   // RR mode: 1 spp, no accumulation of our own
    pc.rays = uint32_t(host->cfg_int("gi_rr_rays", 1));
    pc.history = 1.0f;
  }
  pc.cs_length = host->cfg_float("cs_length", 0.02f);
  pc.cs_strength = host->cfg_float("cs_strength", 1.0f);
  pc.cs_steps = uint32_t(host->cfg_int("cs_steps", 12));
  pc.mv_scale[0] = g_mv_scale[0];
  pc.mv_scale[1] = g_mv_scale[1];
  pc.has_mv = g_mv_view && host->cfg_int("gi_reproject", 1) ? 1u : 0u;
  pc.rt_bias = host->cfg_float("rt_bias", 0.002f);
  pc.rt_tmax = host->cfg_float("rt_tmax", 300.0f);
  pc.rt_debug = uint32_t(host->cfg_int("rt_debug", 0));
  pc.rt_strength = host->cfg_float("rt_strength", 1.0f);
  pc.rt_ao_radius = host->cfg_float("rt_ao_radius", 1.0f);
  pc.rt_ao_rays = uint32_t((std::max)(host->cfg_int("rt_ao_rays", 2), 1));
  pc.gi_rt_radius = (std::max)(host->cfg_float("gi_rt_radius", 1.0f), 0.01f);
  pc.gi_rt_tol = host->cfg_float("gi_rt_depth_tol", 0.03f);
  pc.gi_rt_fallback = uint32_t(host->cfg_int("gi_rt_fallback", 0));
  // D-LS. Soft shadows only with RR (a denoiser for the 1-spp penumbra).
  pc.ls_source_radius = (std::max)(host->cfg_float("ls_source_radius", 0.05f), 0.0f);
  pc.ls_clear = (std::max)(host->cfg_float("ls_clear", 0.2f), 0.0f);
  pc.ls_min_radius = host->cfg_float("ls_min_radius", 0.0f);
  pc.ls_samples = uint32_t((std::min)((std::max)(host->cfg_int("ls_samples", 0), 0), 2));
  pc.ls_shadowed = uint32_t((std::min)((std::max)(host->cfg_int("ls_shadowed", 0), 0), 2));
  pc.ls_debug = uint32_t(host->cfg_int("ls_debug", 0));
  pc.ls_stats = host->cfg_int("ls_stats", 0) ? 1u : 0u;
  int soft = host->cfg_int("ls_soft", -1);   // -1 follows rr
  pc.ls_soft = (soft < 0 ? host->rr_active() : soft != 0) ? 1u : 0u;
  pc.ls_strength = host->cfg_float("ls_strength", 1.0f);
  // Per-light data (docs/SE_BRIDGE.md): bit 0 set in run_post when the join ran; bits 8-12
  // the owners whose lights are traced (bit 8 + Bg3RtLightOwner). Default 0x0F: unknown,
  // placed, item and scenery lights, not the character lights (TRACK_D 9.8).
  pc.ls_flags = (uint32_t(host->cfg_int("ls_owners", 0x0F)) & 0x1Fu) << 8;
  pc.rt_backlit = host->cfg_int("rt_backlit", 1) ? 1u : 0u;
  pc.mat_classes = host->cfg_int("mat_classes", 1) ? 1u : 0u;
  pc.ao_direct = uint32_t((std::min)((std::max)(host->cfg_int("ao_direct", 1), 0), 2));
  pc.rt_origin = host->cfg_int("rt_origin", 1) ? 1u : 0u;
  // D-RF. GGX samples only with RR (rf_rr=-1 follows rr): without a denoiser,
  // mirror rays for smooth surfaces only.
  int rf_rr = host->cfg_int("rf_rr", -1);
  bool rf_noisy = rf_rr < 0 ? host->rr_active() : rf_rr != 0;
  pc.rf_sample = rf_noisy ? 1u : 0u;
  pc.rf_max_rough = rf_noisy ? host->cfg_float("rf_max_rough", 0.6f) : host->cfg_float("rf_max_rough_norr", 0.3f);
  pc.rf_mirror_rough = host->cfg_float("rf_mirror_rough", 0.1f);
  pc.rf_tmax = (std::max)(host->cfg_float("rf_tmax", 100.0f), 0.01f);
  pc.rf_depth_tol = host->cfg_float("rf_depth_tol", 0.03f);
  pc.rf_edge = host->cfg_float("rf_edge", 0.05f);
  pc.rf_strength = host->cfg_float("rf_strength", 1.0f);
  pc.rf_debug = uint32_t(host->cfg_int("rf_debug", 0));
  pc.rf_so = host->cfg_int("rf_so", 0) ? 1u : 0u;
  pc.rf_validate = host->cfg_int("rf_validate", 0) ? 1u : 0u;
  pc.rf_models = uint32_t(host->cfg_int("rf_models", 3)) & 255u;
  pc.rf_stats = host->cfg_int("rf_stats", 0) ? 1u : 0u;
  return pc;
}

void dispatch(VkCommandBuffer cmd, Pipe& p, Bound& b, const Push& pc, uint32_t w, uint32_t h, bool rt = false) {
  const VkDeviceFuncs& vk = *host->vk;
  VkPipelineLayout layout = rt ? g_rt_layout : g_layout;
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
  VkWriteDescriptorSet wr[kLsBindings + kNumExtras];
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, gather_writes(b, rt ? kRtBindings : kBindings, wr), wr);
  if (rt) vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &b.alpha, 0, nullptr);
  vk.CmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  vk.CmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
}

constexpr VkPipelineStageFlags kCS = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags kAll = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

// This frame's sun inputs into the next set of the ring (set 2 of the D-LS layout).
VkDescriptorSet write_sun_set(const Snapshot& s) {
  VkDescriptorSet set = g_sun_sets[g_sun_slot];
  g_sun_slot = (g_sun_slot + 1) % kSunRing;
  VkDescriptorBufferInfo ubo = s.x_atmo.buffer ? VkDescriptorBufferInfo{s.x_atmo.buffer, s.x_atmo.offset, (std::min)(s.x_atmo.range, VkDeviceSize(65536))}
                                               : VkDescriptorBufferInfo{s.cam.buffer, s.cam.offset, s.cam.range};
  const Bg3Binding* src[5] = {&s.x_tlut, &s.x_ca0, &s.x_ca1, &s.x_cb0, &s.x_cb1};
  VkDescriptorImageInfo img[6];
  for (int i = 0; i < 5; ++i)
    img[i] = src[i]->view ? VkDescriptorImageInfo{VK_NULL_HANDLE, src[i]->view, src[i]->layout}
                          : VkDescriptorImageInfo{VK_NULL_HANDLE, s.alb.view, s.alb.layout};
  img[5] = {g_linear_clamp, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
  VkWriteDescriptorSet w[7];
  for (uint32_t i = 0; i < 7; ++i) {
    w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w[i].dstSet = set;
    w[i].dstBinding = i;
    w[i].descriptorCount = 1;
    w[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : i == 6 ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    if (i == 0) w[i].pBufferInfo = &ubo;
    else w[i].pImageInfo = &img[i - 1];
  }
  host->vk->UpdateDescriptorSets(host->device, 7, w, 0, nullptr);
  return set;
}

void ls_dispatch(VkCommandBuffer cmd, Bound& b, const Push& pc, VkDescriptorSet sun, VkPipeline pipe, uint32_t gx,
                 uint32_t gy) {
  const VkDeviceFuncs& vk = *host->vk;
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
  VkWriteDescriptorSet w[kLsBindings + kNumExtras];
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_ls_layout, 0, gather_writes(b, kLsBindings, w), w);
  VkDescriptorSet sets[2] = {b.alpha, sun};
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_ls_layout, 1, 2, sets, 0, nullptr);
  vk.CmdPushConstants(cmd, g_ls_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  vk.CmdDispatch(cmd, gx, gy, 1);
}

// D-RF: push set 0 (bindings 0-30), the host's set 1, the game's lighting set 1 as set 2.
void rf_dispatch(VkCommandBuffer cmd, Bound& b, const Snapshot& s, const Push& pc) {
  const VkDeviceFuncs& vk = *host->vk;
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_rf.pipeline);
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_rf_layout, 0, kRfBindings, b.w);
  VkDescriptorSet sets[2] = {b.alpha, s.game_set1};
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_rf_layout, 1, 2, sets, s.game_set1_ndyn,
                           s.game_set1_ndyn ? s.game_set1_dyn : nullptr);
  vk.CmdPushConstants(cmd, g_rf_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  vk.CmdDispatch(cmd, (g_w + 7) / 8, (g_h + 7) / 8, 1);
}

// D-RF pass alone, 2 timestamps per frame, medians over 300 frames.
VkQueryPool g_rf_queries = VK_NULL_HANDLE;
uint32_t g_rf_slot = 0;
bool g_rf_written[kRing] = {};
std::vector<float> g_rf_ms;

void rf_timestamp(VkCommandBuffer cmd, uint32_t index) {
  if (!g_rf_queries) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kRing * 2;
    if (host->vk->CreateQueryPool(host->device, &qi, nullptr, &g_rf_queries) != VK_SUCCESS) return;
  }
  uint32_t q = g_rf_slot * 2 + index;
  host->vk->CmdResetQueryPool(cmd, g_rf_queries, q, 1);
  host->vk->CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_rf_queries, q);
  if (index == 1) {
    g_rf_written[g_rf_slot] = true;
    g_rf_slot = (g_rf_slot + 1) % kRing;
  }
}

void rf_timing_collect() {
  if (!g_rf_queries) return;
  uint32_t old = (g_rf_slot + 1) % kRing;
  if (!g_rf_written[old]) return;
  uint64_t t[2];
  if (host->vk->GetQueryPoolResults(host->device, g_rf_queries, old * 2, 2, sizeof t, t, sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    return;
  g_rf_written[old] = false;
  double ms = double(t[1] - t[0]) * g_ns_per_tick * 1e-6;
  if (ms >= 0 && ms < 1000) g_rf_ms.push_back(float(ms));
  if (g_rf_ms.size() < 300) return;
  auto& v = g_rf_ms;
  std::sort(v.begin(), v.end());
  int rf_rr = host->cfg_int("rf_rr", -1);
  bool noisy = rf_rr < 0 ? host->rr_active() : rf_rr != 0;
  host->log("rf: reflection pass GPU median %.3f ms, p10 %.3f, p90 %.3f (%zu frames; %s, max roughness %.2f, debug %d, "
            "validate %d)",
            v[v.size() / 2], v[v.size() / 10], v[v.size() * 9 / 10], v.size(),
            noisy ? "GGX VNDF, RR denoises" : "mirror rays, no denoiser",
            noisy ? host->cfg_float("rf_max_rough", 0.6f) : host->cfg_float("rf_max_rough_norr", 0.3f),
            host->cfg_int("rf_debug", 0), host->cfg_int("rf_validate", 0));
  v.clear();
}

// D-LS: 3 timestamps per frame (start, after the join pass, end), medians over 300 frames.
VkQueryPool g_ls_queries = VK_NULL_HANDLE;
uint32_t g_ls_slot = 0;
bool g_ls_written[kRing] = {};
std::vector<float> g_ls_ms, g_ls_join_ms;

void ls_timestamp(VkCommandBuffer cmd, uint32_t index) {
  if (!g_ls_queries) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kRing * 3;
    if (host->vk->CreateQueryPool(host->device, &qi, nullptr, &g_ls_queries) != VK_SUCCESS) return;
  }
  uint32_t q = g_ls_slot * 3 + index;
  host->vk->CmdResetQueryPool(cmd, g_ls_queries, q, 1);
  host->vk->CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_ls_queries, q);
  if (index == 2) {
    g_ls_written[g_ls_slot] = true;
    g_ls_slot = (g_ls_slot + 1) % kRing;
  }
}

void ls_timing_collect() {
  if (!g_ls_queries) return;
  uint32_t old = (g_ls_slot + 1) % kRing;
  if (!g_ls_written[old]) return;
  uint64_t t[3];
  if (host->vk->GetQueryPoolResults(host->device, g_ls_queries, old * 3, 3, sizeof t, t, sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    return;
  g_ls_written[old] = false;
  double ms = double(t[2] - t[0]) * g_ns_per_tick * 1e-6, join = double(t[1] - t[0]) * g_ns_per_tick * 1e-6;
  if (ms >= 0 && ms < 1000 && join >= 0 && join <= ms) {
    g_ls_ms.push_back(float(ms));
    g_ls_join_ms.push_back(float(join));
  }
  if (g_ls_ms.size() < 300) return;
  auto& v = g_ls_ms;
  auto& j = g_ls_join_ms;
  std::sort(v.begin(), v.end());
  std::sort(j.begin(), j.end());
  host->log("ls: local-light shadow pass GPU median %.3f ms, p10 %.3f, p90 %.3f, of which join %.3f (%zu frames; "
            "samples %d, soft %d, shadowed mode %d, debug %d, per-light data %d (join %s), owners 0x%X, impl %d)",
            v[v.size() / 2], v[v.size() / 10], v[v.size() * 9 / 10], j[j.size() / 2], v.size(),
            host->cfg_int("ls_samples", 0), host->cfg_int("ls_soft", -1), host->cfg_int("ls_shadowed", 0),
            host->cfg_int("ls_debug", 0), host->cfg_int("ls_lights", 1), g_lsl_ok ? "ran" : "off",
            host->cfg_int("ls_owners", 0x0F), host->cfg_int("ls_impl", 1));
  v.clear();
  j.clear();
}

// The frame's TLAS if ray-traced shadows or AO are on and the host built one
// this frame or the previous one: the game records the G-buffer of a frame
// before presenting the previous one and the lighting after, so the build is
// usually stamped one frame earlier. The GPU still runs it first (submission
// order), and the handle is the same object every frame.
VkAccelerationStructureKHR rt_tlas(VkDescriptorSet* alpha) {
  if (!host->rt_scene || (!host->cfg_int("rt_shadows", 0) && !host->cfg_int("rt_ao", 0) && !host->cfg_int("gi_rt", 0) &&
                          !host->cfg_int("rt_local_shadows", 0) && !host->cfg_int("rt_reflections", 0)))
    return VK_NULL_HANDLE;
  VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
  VkDescriptorSetLayout layout;
  uint64_t built = 0, now = host->frame_index();
  if (!host->rt_scene(&tlas, &built) || !tlas || built + 1 < now || !host->rt_alpha || !host->rt_alpha(&layout, alpha) ||
      !*alpha) {
    ++g_rt_frames_missing;
    return VK_NULL_HANDLE;
  }
  ++(built == now ? g_rt_frames_traced : g_rt_frames_stale);
  return tlas;
}

bool run_pre(VkCommandBuffer cmd, Snapshot& s) {
  if (!ensure_layout() || !ensure_images(s.dif.width, s.dif.height)) return false;
  bool ok = refresh(g_trace, "gi/gi_trace.hlsl") & refresh(g_temporal, "gi/gi_temporal.hlsl") &
            refresh(g_ao_apply, "gi/gi_ao_apply.hlsl") & refresh(g_composite, "gi/gi_composite.hlsl") &
            refresh(g_contact, "gi/gi_contact.hlsl");
  if (!ok) return false;
  if (!g_images_ready) {
    for (Bg3Image* im : all_images)
      image_layout(cmd, im->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkClearColorValue zero{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (Bg3Image* im : all_images) host->vk->CmdClearColorImage(cmd, im->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
    memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kCS);
    g_images_ready = true;
  }
  timing_init();
  timing_collect();
  timestamp(cmd, 0);
  {
    VkImage im;
    VkImageView v;
    VkFormat f;
    float sc[2];
    // Only an image seen being written this frame is known to be alive.
    if (host->dlss_motion_vectors && host->dlss_motion_vectors(&im, &v, &f, sc) && f == VK_FORMAT_R16G16_SFLOAT &&
        g_mv_layout != VK_IMAGE_LAYOUT_UNDEFINED && g_mv_frame == host->frame_index()) {
      g_mv_image = im, g_mv_view = v, g_mv_scale[0] = sc[0], g_mv_scale[1] = sc[1];
    } else {
      g_mv_image = VK_NULL_HANDLE, g_mv_view = VK_NULL_HANDLE;
    }
  }
  Bound b;
  bind_all(b, s);
  Push pc = make_push();
  mat_bits(pc, s, false);
  // Track D: the frame's TLAS for the ray-traced passes.
  VkDescriptorSet alpha = VK_NULL_HANDLE;
  VkAccelerationStructureKHR tlas = rt_tlas(&alpha);
  if (tlas && (!ensure_rt_layout() || !refresh(g_rt_shadow, "gi/rt_shadow.hlsl", g_rt_layout) ||
               !refresh(g_rt_debug, "gi/rt_debug.hlsl", g_rt_layout) || !refresh(g_rt_ao, "gi/rt_ao.hlsl", g_rt_layout) ||
               !refresh(g_trace_rt, "gi/gi_trace_rt.hlsl", g_rt_layout) ||
               !refresh(g_rt_origin, "gi/rt_origin.hlsl", g_rt_layout) || !stats_init()))
    tlas = VK_NULL_HANDLE;   // (the statistics buffer is binding 20 and must exist)
  s.gi = host->cfg_int("gi", 0) != 0;
  // Hybrid GI: ray queries replace the screen-space march (and do the AO rays too).
  const bool gi_rt = tlas && s.gi && host->cfg_int("gi_rt", 0);
  const bool rt_ao = tlas && s.gi && !gi_rt && host->cfg_int("rt_ao", 0);
  const bool rt_shadows = tlas && s.shadow.view && host->cfg_int("rt_shadows", 0);
  // D-LS runs after the lighting (run_post); it needs the TLAS and the light list.
  s.ls = tlas && host->cfg_int("rt_local_shadows", 0) && s.ls_ok && ensure_ls_layout() &&
         refresh(g_ls, "gi/rt_local_shadows.hlsl", g_ls_layout);
  if (tlas && host->cfg_int("rt_local_shadows", 0) && !s.ls_ok && !g_ls_missing_logged) {
    g_ls_missing_logged = true;
    host->log("ls: the lighting dispatch lacks part of the light list (set1 b0/b13/b23/b24/b25/b39/b40/b41, set0 b2); "
              "rt_local_shadows skipped");
  }
  // D-RF runs after the lighting too: the TLAS, the light list (froxels, constants),
  // the lighting's own set 1 (probes) and last frame's radiance (written by the GI
  // composite, so gi=1).
  const bool rf_want = tlas && host->cfg_int("rt_reflections", 0);
  s.rf = rf_want && s.ls_ok && s.game_set1 && s.gi && ensure_rf_layout(s.game_set1_layout) &&
         refresh(g_rf, "gi/rt_reflections.hlsl", g_rf_layout);
  if (rf_want && !s.rf && !g_rf_missing_logged) {
    g_rf_missing_logged = true;
    host->log("rf: rt_reflections skipped: light list %d, lighting set 1 %p (layout %p), gi %d, pipeline %d", s.ls_ok,
              (void*)s.game_set1, (void*)s.game_set1_layout, s.gi, g_rf.pipeline != VK_NULL_HANDLE);
  }
  s.rf_validate = s.rf && host->cfg_int("rf_validate", 0) != 0 && s.ls_ok;
  if (s.rf_validate) s.ls = false;   // the game's own specular must reach D-RF untouched
  if (tlas) {
    if (!g_rt_queries) {
      VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
      qi.queryCount = kRing * 2;
      host->vk->CreateQueryPool(host->device, &qi, nullptr, &g_rt_queries);
    }
    rt_timing_collect();
    pc.rt_stats = host->cfg_int("rt_stats", 0) ? 1u : 0u;
    if (pc.rt_stats || pc.ls_stats || pc.rf_stats || host->cfg_int("ls_check", 0)) stats_begin(cmd, host->cfg_int("rt_alpha_test", 1) != 0);
    bind_rt(b, tlas, alpha);
    if (!g_rt_logged) {
      g_rt_logged = true;
      host->log("rt: first traced frame at %ux%u (TLAS %p, shadows %d, AO %d, GI %d)", g_w, g_h, (void*)tlas, rt_shadows,
                rt_ao, gi_rt);
    }
  }
  bool rt_started = false;
  auto rt_begin = [&] {
    if (!rt_started) rt_timestamp(cmd, 0);
    rt_started = true;
  };
  // Depth, normals and AO come from earlier graphics passes.
  memory_barrier(cmd, kAll, kCS);
  // Robust ray origins first: every ray-traced pass (and D-LS after the lighting) reads them.
  if (tlas && pc.rt_origin != 0) {
    rt_begin();
    dispatch(cmd, g_rt_origin, b, pc, g_w, g_h, true);
    memory_barrier(cmd, kCS, kCS);
  }
  if (s.gi) {
    // The MVs are sampled in SHADER_READ_ONLY by the traces (bounce
    // reprojection) and the temporal pass; their pass may leave them elsewhere.
    bool mv_move = pc.has_mv && g_mv_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (mv_move) image_layout(cmd, g_mv_image, g_mv_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kAll, kCS);
    trace_timing_collect();
    trace_timestamp(cmd, 0);
    if (gi_rt) {
      rt_begin();
      dispatch(cmd, g_trace_rt, b, pc, g_hw, g_hh, true);
    } else {
      dispatch(cmd, g_trace, b, pc, g_hw, g_hh);
    }
    trace_timestamp(cmd, 1, gi_rt);
    memory_barrier(cmd, kCS, kCS);
    if (rt_ao) {
      // Ray-traced visibility replaces the screen-space one before accumulation.
      rt_begin();
      dispatch(cmd, g_rt_ao, b, pc, g_hw, g_hh, true);
      memory_barrier(cmd, kCS, kCS);
    }
    dispatch(cmd, g_temporal, b, pc, g_hw, g_hh);
    if (mv_move) image_layout(cmd, g_mv_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, g_mv_layout, kCS, kAll);
    memory_barrier(cmd, kCS, kCS);
    if (pc.ao > 0.0f || pc.debug == 3) {
      s.aux_ao = true;
      image_layout(cmd, s.ao.image, s.ao.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
      dispatch(cmd, g_ao_apply, b, pc, g_w, g_h);
      image_layout(cmd, s.ao.image, VK_IMAGE_LAYOUT_GENERAL, s.ao.layout, kCS, kCS);
    }
  }
  // Ray-traced sun shadows (Track D) when the host has a scene, otherwise
  // screen-space contact shadows; both fold into the game's sun shadow mask.
  s.tlas = tlas;   // the post pass's debug views
  s.alpha = alpha;
  s.rt_sun = rt_shadows;
  if (rt_shadows) {
    rt_begin();
    image_layout(cmd, s.shadow.image, s.shadow.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    dispatch(cmd, g_rt_shadow, b, pc, g_w, g_h, true);
    image_layout(cmd, s.shadow.image, VK_IMAGE_LAYOUT_GENERAL, s.shadow.layout, kCS, kCS);
  } else if (s.shadow.view && host->cfg_int("contact_shadows", 1)) {
    image_layout(cmd, s.shadow.image, s.shadow.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    dispatch(cmd, g_contact, b, pc, g_w, g_h);
    image_layout(cmd, s.shadow.image, VK_IMAGE_LAYOUT_GENERAL, s.shadow.layout, kCS, kCS);
  }
  // D-LS validation (ls_debug=4/5): the lighting then computes only direct local light.
  // D-RF validation (rf_validate=1): AO 1 and no sun, so its specular is local lights + probes.
  const bool prep = (s.ls && pc.ls_debug >= 4 && pc.ls_debug <= 7) || s.rf_validate;
  if (prep && refresh(g_ls_prep, "gi/rt_local_shadows_prep.hlsl")) {
    Push vp = pc;
    if (s.rf_validate) vp.ls_debug = 6;
    vp.ls_has_mask = s.shadow.view ? 1u : 0u;
    image_layout(cmd, s.ao.image, s.ao.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    if (s.shadow.view) image_layout(cmd, s.shadow.image, s.shadow.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    dispatch(cmd, g_ls_prep, b, vp, g_w, g_h);
    image_layout(cmd, s.ao.image, VK_IMAGE_LAYOUT_GENERAL, s.ao.layout, kCS, kCS);
    if (s.shadow.view) image_layout(cmd, s.shadow.image, VK_IMAGE_LAYOUT_GENERAL, s.shadow.layout, kCS, kCS);
  }
  if (rt_started) {
    rt_timestamp(cmd, 1);
    g_rt_written[g_rt_slot] = true;
    g_rt_slot = (g_rt_slot + 1) % kRing;
  }
  timestamp(cmd, 1);
  host->restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  return true;
}

void run_post(VkCommandBuffer cmd, const Snapshot& s) {
  Bound b;
  bind_all(b, s);
  Push pc = make_push();
  mat_bits(pc, s, true);
  // The tiled lighting just wrote diffuse/specular from compute.
  memory_barrier(cmd, kCS, kCS);
  timestamp(cmd, 2);
  // D-LS first: the GI composite keeps diffuse + specular as next frame's bounce source.
  if (s.ls && s.tlas && g_ls.pipeline) {
    Bound lb;
    bind_all(lb, s);
    bind_rt(lb, s.tlas, s.alpha);
    bind_ls(lb, s);
    ls_timing_collect();
    image_layout(cmd, s.ao.image, s.ao.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);   // read as storage (binding 12)
    // The sun mask the lighting read (binding 16), for the sun's AO-on-direct compensation.
    if (s.shadow.view) image_layout(cmd, s.shadow.image, s.shadow.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    Push lp = pc;
    lp.ls_has_mask = s.shadow.view ? 1u : 0u;
    const VkDescriptorSet sun = write_sun_set(s);
    ls_timestamp(cmd, 0);   // the timing covers the join pass and the shadow pass
    // Per-light data: the join pass writes one record per light array slot.
    g_lsl_ok = lsl_begin(&g_lsl) && refresh(g_ls_match, "gi/rt_local_shadows_match.hlsl", g_ls_layout);
    if (g_lsl_ok) {
      if (g_lsl.needs_init) {
        image_layout(cmd, g_lsl.slots_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, kCS);
        lsl_initialized();
      }
      bind_extras(lb, s);
      lp.ls_flags |= host->cfg_int("ls_lights", 1) ? 1u : 0u;
      Push mp = lp;
      mp.ls_pass = host->cfg_int("ls_check", 0) ? 1u : 0u;   // the join's own check (rtStats 84-85)
      ls_dispatch(cmd, lb, mp, sun, g_ls_match.pipeline, kLslSlots / 64, 1);
      memory_barrier(cmd, kCS, kCS);
    }
    ls_timestamp(cmd, 1);
    // The default-settings variant, or the one with every mode (more registers, docs/TRACK_D_RT.md 9.9).
    const bool full = lp.ls_samples || lp.ls_shadowed || lp.ls_debug || lp.ls_stats;
    VkPipeline lsp = g_ls.pipeline;
    lp.ls_pass = host->cfg_int("ls_check", 0) == 2 ? 2u : 0u;   // reference against itself
    if (host->cfg_int("ls_check", 0)) {
      Pipe& c = full ? g_ls_full_check : g_ls_check;
      if (refresh(c, full ? "gi/rt_local_shadows_full_check.hlsl" : "gi/rt_local_shadows_check.hlsl", g_ls_layout))
        lsp = c.pipeline;
    } else if (!host->cfg_int("ls_impl", 1)) {
      if (refresh(g_ls_ref, "gi/rt_local_shadows_ref.hlsl", g_ls_layout)) lsp = g_ls_ref.pipeline;
    } else if (full) {
      // Not before the first frame that needs it: an unused pipeline is not compiled.
      if (refresh(g_ls_full, "gi/rt_local_shadows_full.hlsl", g_ls_layout)) lsp = g_ls_full.pipeline;
    }
    ls_dispatch(cmd, lb, lp, sun, lsp, (g_w + 7) / 8, (g_h + 7) / 8);
    ls_timestamp(cmd, 2);
    if (s.shadow.view) image_layout(cmd, s.shadow.image, VK_IMAGE_LAYOUT_GENERAL, s.shadow.layout, kCS, kAll);
    image_layout(cmd, s.ao.image, VK_IMAGE_LAYOUT_GENERAL, s.ao.layout, kCS, kAll);
    memory_barrier(cmd, kCS, kCS);
    if (!g_ls_logged) {
      g_ls_logged = true;
      host->log("ls: first frame at %ux%u; light array %llu bytes (%llu lights), clusters %llu bytes, index %llu bytes, "
                "shadow views %llu bytes (%llu), atlas %ux%u fmt %d layout %d",
                g_w, g_h, (unsigned long long)s.l_lights.range, (unsigned long long)(s.l_lights.range / 80),
                (unsigned long long)s.l_clusters.range, (unsigned long long)s.l_index.range,
                (unsigned long long)s.l_shadows.range, (unsigned long long)(s.l_shadows.range / 96), s.l_atlas.width,
                s.l_atlas.height, s.l_atlas.format, s.l_atlas.layout);
    }
  }
  // D-RF after D-LS and before the composite: it reads last frame's radiance, and the
  // composite then keeps this frame's (reflections included) for the next.
  if (s.rf && s.tlas && g_rf.pipeline && g_rf_layout) {
    Bound rb;
    bind_all(rb, s);
    bind_rt(rb, s.tlas, s.alpha);
    bind_ls(rb, s);
    rb.rf_hit = {VK_NULL_HANDLE, g_rf_hit.view, VK_IMAGE_LAYOUT_GENERAL};
    rb.w[kLsBindings] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    rb.w[kLsBindings].dstBinding = kLsBindings;
    rb.w[kLsBindings].descriptorCount = 1;
    rb.w[kLsBindings].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    rb.w[kLsBindings].pImageInfo = &rb.rf_hit;
    Push rp = pc;
    rp.rf_validate = s.rf_validate ? 1u : 0u;
    rf_timing_collect();
    // The reprojection samples the MVs in SHADER_READ_ONLY (as the GI traces do).
    bool mv_move = rp.has_mv && g_mv_image && g_mv_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (mv_move) image_layout(cmd, g_mv_image, g_mv_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kAll, kCS);
    rf_timestamp(cmd, 0);
    rf_dispatch(cmd, rb, s, rp);
    rf_timestamp(cmd, 1);
    if (mv_move) image_layout(cmd, g_mv_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, g_mv_layout, kCS, kAll);
    memory_barrier(cmd, kCS, kCS);
    if (host->set_spec_hit_distance)
      host->set_spec_hit_distance(g_rf_hit.image, g_rf_hit.view, VK_FORMAT_R16_SFLOAT, g_w, g_h);
    if (!g_rf_logged) {
      g_rf_logged = true;
      host->log("rf: first frame at %ux%u; lighting set 1 %p layout %p (%u dynamic offsets), validate %d", g_w, g_h,
                (void*)s.game_set1, (void*)s.game_set1_layout, s.game_set1_ndyn, s.rf_validate);
    }
  }
  if (s.gi) dispatch(cmd, g_composite, b, pc, g_w, g_h);
  if (s.tlas && pc.rt_debug >= 1 && pc.rt_debug <= 5) {
    memory_barrier(cmd, kCS, kCS);
    bind_rt(b, s.tlas, s.alpha);
    dispatch(cmd, g_rt_debug, b, pc, g_w, g_h, true);
  }
  if (pc.rt_debug >= 6 || em_check_due())
    em_debug(cmd, s.cam, s.vp, s.dep, s.dif, s.spe, s.l_cam2, g_w, g_h, pc.rt_debug >= 6 ? pc.rt_debug : 0);   // D-EM (core/emissive.cpp)
  timestamp(cmd, 3);
  g_slot_written[g_slot] = true;
  g_slot = (g_slot + 1) % kRing;
  // The SSS pass samples the diffuse buffer in its fragment shader.
  memory_barrier(cmd, kCS, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | kCS);
  if (s.gi) g_parity ^= 1;
  host->restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  if (!g_logged) {
    g_logged = true;
    host->log("gi: first frame done at %ux%u; AO layout %d, diffuse layout %d, cam UBO +%llu", g_w, g_h, s.ao.layout,
              s.dif.layout, (unsigned long long)s.cam.offset);
  }
}

// Logs what a few indirect dispatches have bound, to diagnose recognition.
void describe(VkCommandBuffer cmd, const Bg3DispatchInfo* d) {
  static int n = 0;
  if (n >= host->cfg_int("gi_describe", 0)) return;
  ++n;
  char line[1024];
  int len = snprintf(line, sizeof line, "gi: dispatch cs %016llx:", (unsigned long long)d->cs_hash);
  const uint32_t probes[][2] = {{0, 1}, {0, 2}, {0, 4}, {1, 0}, {1, 1}, {1, 2}, {1, 3},
                                {1, 5}, {1, 6}, {1, 7}, {1, 9}, {1, 10}, {1, 11}};
  for (auto& p : probes) {
    Bg3Binding b{};
    bool ok = host->query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p[0], p[1], 0, &b);
    len += snprintf(line + len, sizeof line - len, " s%ub%u=%s", p[0], p[1], !ok ? "-" : b.buffer ? "buf" : b.view ? "img" : "?");
    if (ok && b.view) len += snprintf(line + len, sizeof line - len, "(f%d %ux%u L%d)", b.format, b.width, b.height, b.layout);
    if (ok && b.buffer) len += snprintf(line + len, sizeof line - len, "(t%d +%llu)", b.type, (unsigned long long)b.offset);
  }
  host->log("%s", line);
}

void flush(VkCommandBuffer cmd) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_pending.find(cmd);
  if (it == g_pending.end()) return;
  Snapshot s = it->second;
  g_pending.erase(it);
  run_post(cmd, s);
}

}  // namespace

void gi_on_dispatch_pre(VkCommandBuffer cmd, const Bg3DispatchInfo* d) {
  // GI, or just the ray-traced sun shadows (rt_shadows works with gi=0).
  if (!d->indirect || (!host->cfg_int("gi", 0) && !host->cfg_int("rt_shadows", 0) && !host->cfg_int("rt_local_shadows", 0) &&
                       !host->cfg_int("rt_reflections", 0)))
    return;
  describe(cmd, d);
  std::lock_guard<std::mutex> lock(g_mu);
  uint64_t f = host->frame_index();
  if (f == g_last_pre_frame) {
    // Later lighting permutations of the same frame: only the material ones bind
    // RT3 (set 1 b4) and the SSS profiles (b28), which the post passes need.
    auto it = g_pending.find(cmd);
    if (it == g_pending.end() || (it->second.x_rt3.view && it->second.x_prof.buffer)) return;
    Snapshot& s = it->second;
    Bg3Binding dif{}, b{};
    if (!host->query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, 1, 10, 0, &dif) || dif.view != s.dif.view) return;
    if (!s.x_rt3.view && host->query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, 1, 4, 0, &b) && b.view &&
        b.width == s.dif.width && b.height == s.dif.height)
      s.x_rt3 = b;
    b = Bg3Binding{};
    if (!s.x_prof.buffer && host->query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, 1, 28, 0, &b) && b.buffer && b.range > 0) {
      s.x_prof = b;
      static VkDeviceSize logged_range = 0;
      static int n_logged = 0;
      if (b.range != logged_range && n_logged < 8) {
        ++n_logged;
        logged_range = b.range;
        host->log("gi: SSS profile list (set1 b28): %llu bytes (%llu profiles)", (unsigned long long)b.range,
                  (unsigned long long)(b.range / 64));
      }
    }
    return;
  }
  Snapshot s{};
  if (!snapshot(cmd, s)) return;
  g_last_pre_frame = f;
  if (run_pre(cmd, s)) g_pending[cmd] = s;
}

void gi_on_dispatch(VkCommandBuffer, const Bg3DispatchInfo*) {}

// Learns the layout the game's velocity pass leaves the motion vectors in.
void gi_on_pass_end(VkCommandBuffer, const Bg3PassInfo* p) {
  if (p->color_count != 1 || p->color_formats[0] != VK_FORMAT_R16G16_SFLOAT || !host->dlss_motion_vectors) return;
  VkImage im;
  VkImageView v;
  VkFormat f;
  float sc[2];
  bool have = host->dlss_motion_vectors(&im, &v, &f, sc);
  static int seen = 0;
  if (seen < host->cfg_int("gi_describe_mv", 0)) {
    ++seen;
    host->log("gi: RG16F pass: image %p layout %d draws %u fs %016llx | DLSS MV %s %p", (void*)p->color_images[0],
              p->color_layouts[0], p->draw_count, (unsigned long long)p->first_fs_hash, have ? "image" : "none",
              have ? (void*)im : nullptr);
  }
  if (!have || im != p->color_images[0]) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_mv_layout != p->color_layouts[0])
    host->log("gi: motion vectors found (image %p), left in layout %d by their pass", (void*)im, p->color_layouts[0]);
  g_mv_layout = p->color_layouts[0];
  g_mv_frame = host->frame_index();
}

// The lighting and the SSS pass that reads its output are in different
// command buffers: finish at the end of the lighting's own buffer, or at the
// first render pass after it if that comes first.
void gi_on_pass_begin(VkCommandBuffer cmd, const Bg3PassInfo* p) {
  if (p->indirect_dispatches_before) flush(cmd);
}

void gi_on_cmd_end(VkCommandBuffer cmd) { flush(cmd); }

void gi_on_frame(uint64_t) {}

void gi_release() {
  std::lock_guard<std::mutex> lock(g_mu);
  release_images();
  lsl_release();
  g_lsl_ok = false;
  for (Pipe* p : {&g_trace, &g_temporal, &g_ao_apply, &g_composite, &g_contact, &g_rt_shadow, &g_rt_debug, &g_rt_ao,
                  &g_trace_rt, &g_ls, &g_ls_prep, &g_rf, &g_rt_origin, &g_ls_match, &g_ls_full, &g_ls_ref, &g_ls_check, &g_ls_full_check}) {
    host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)p->pipeline);
    *p = Pipe{};
  }
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_rf_queries);
  g_rf_queries = VK_NULL_HANDLE;
  g_rf_ms.clear();
  host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_rf_layout);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_rf_set_layout);
  g_rf_layout = VK_NULL_HANDLE;
  g_rf_set_layout = VK_NULL_HANDLE;
  g_rf_game_layout = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_queries);
  g_queries = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_rt_queries);
  g_rt_queries = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_tr_queries);
  g_tr_queries = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_ls_queries);
  g_ls_queries = VK_NULL_HANDLE;
  g_ls_ms.clear();
  host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_ls_layout);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_ls_set_layout);
  g_ls_layout = VK_NULL_HANDLE;
  g_ls_set_layout = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_SAMPLER, (uint64_t)g_linear_clamp);
  g_linear_clamp = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_POOL, (uint64_t)g_sun_pool);   // frees the sets
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_sun_dsl);
  g_sun_pool = VK_NULL_HANDLE;
  g_sun_dsl = VK_NULL_HANDLE;
  std::memset(g_sun_sets, 0, sizeof g_sun_sets);
  host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)g_stats_buf);
  host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_stats_mem);
  g_stats_buf = VK_NULL_HANDLE;
  g_stats_mem = VK_NULL_HANDLE;
  g_stats_map = nullptr;
  std::memset(g_stats_frame, 0, sizeof g_stats_frame);
  host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_layout);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_set_layout);
  host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_rt_layout);
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_rt_set_layout);
  g_layout = VK_NULL_HANDLE;
  g_set_layout = VK_NULL_HANDLE;
  g_rt_layout = VK_NULL_HANDLE;
  g_rt_set_layout = VK_NULL_HANDLE;
  g_pending.clear();
}
