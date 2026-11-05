#include "../common/native_hit_gi_bindings.h"
#include "../common/native_hit_rf_bindings.h"
#include "../common/native_hit_tileset_fields.h"
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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include "../common/native_hair_origin.h"
#include "../common/hair_cost_epoch.h"
#include "../common/shader_variant.h"
#include "../common/pipeline_async.h"
#include "../common/pipeline_specs.h"
#include <chrono>
#include "../common/ls_exact_k.h"
#include "../common/occ_stats.h"
#include "../common/native_hit_capability.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core.h"
#include "../common/camera_motion.h"
#include "../common/hair_primary.h"
#include "../common/native_scalp_capability.h"
#include "../common/native_tileset_capability.h"
#include "../common/omm_scene_capability.h"
#include "../common/omm_scene_binding.h"
#include "ls_lights.h"
#include "sharc.h"
#include "sun_trust.h"

// D-EM (Bg3HostApi::rt_emissive, Bg3EmissiveInfo): the SHaRC injection adds the game's effects-target
// emission. It follows BG3RTX_HOST_HAS_RT_EMISSIVE, which the D-EM host_api.h is to define (an ABI
// number cannot say it: main's ABI 8 is rt_scene_set); CMake BG3RTX_SHARC_EM=ON/OFF forces it.
#ifndef BG3RTX_SHARC_EM
#ifdef BG3RTX_HOST_HAS_RT_EMISSIVE
#define BG3RTX_SHARC_EM 1
#else
#define BG3RTX_SHARC_EM 0
#endif
#endif

namespace {

struct Snapshot {
  uint64_t omm_identity=0;
  Bg3Binding cam, vp, nrm, alb, mat, dep, ao, shadow, dif, spe;
  // D-LS: the lighting's light list (set 1 b0, b13, b23, b24, b25, b39, b40, b41; set 0 b2).
  Bg3Binding l_flags, l_scale, l_clusters, l_index, l_lights, l_consts, l_shadows, l_atlas, l_cam2;
  bool ls_ok;                        // all of them are bound
  bool native_direct_validation = false;
  // D-RF: the lighting's own set 1 (bound as our set 2: probes, cubes, DFG LUT, samplers).
  VkDescriptorSet game_set1;
  VkDescriptorSetLayout game_set1_layout;
  uint32_t game_set1_dyn[16], game_set1_ndyn;
  bool box_ready;                     // native box-light storage buffer is bound
  bool boxes_bound;                   // set 1 b29 is a bound storage buffer (rf_hit_shade reads it, any ls_box)
  VkAccelerationStructureKHR tlas;   // the TLAS the ray-traced passes used (Track D)
  VkDescriptorSet alpha;             // the host's set 1 for them (alpha-tested geometry, phase 3)
  bool gi;                           // the GI passes ran (gi=1); otherwise only the sun shadow work
  bool hair_physical_origin=false;
  bool rt_gi_applied = false;         // diagnostic: RT GI commands actually recorded in run_pre
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
  bool pan = false;
  // SHaRC radiance cache (sharc=1, core/sharc.h): resolved and queried by the GI trace in
  // run_pre; D-RF queries it (rf_sharc) and the injection writes it in run_post.
  bool sharc, rf_sharc;
  SharcFrame sh;
};

struct Pipe {
  VkPipeline pipeline = VK_NULL_HANDLE;
  uint64_t generation = 0;
  VkPipelineLayout active_layout = VK_NULL_HANDLE;
  // Cache by complete request, specialization values and layout. Live toggles retain both states,
  // including values which previously collided in the same bit-indexed variant slot.
  struct Variant { VkPipeline pipeline = VK_NULL_HANDLE; uint64_t generation = 0; std::string file; };
  std::unordered_map<std::string, Variant> variants;
};

// Background pipeline builds (common/pipeline_async.h): two workers below normal priority. Joined in
// gi_release (before FreeLibrary: never from the DLL's static destructors).
extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread();
extern "C" __declspec(dllimport) int __stdcall SetThreadPriority(void* thread, int priority);
bg3::pipe_async::Builds g_builds(
    2, [](const bg3::pipe_async::Result& r) { host->defer_destroy(BG3_DEFER_PIPELINE, r.pipeline); },
    [] { SetThreadPriority(GetCurrentThread(), -1); });   // THREAD_PRIORITY_BELOW_NORMAL
std::recursive_mutex g_prewarm_mu;
std::unordered_map<uint64_t, std::unordered_set<std::string>> g_prewarmed;
uint64_t g_prewarm_count = 0;
// A pipeline layout a background build may still read is destroyed once none does.
void defer_layout_destroy(uint64_t layout) {
  std::lock_guard<std::recursive_mutex> lock(g_prewarm_mu);
  if (!layout) return;
  g_prewarmed.erase(layout);
  g_builds.park(layout, [layout] { host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, layout); });
}
// On a worker: the SPIR-V (the layer's cache or DXC) and the compute pipeline of `req` for `layout`.
using bg3::pipe_async::PipelineSpecs;
bg3::pipe_async::Result build_pipeline(const std::string& req, const std::string& entry, VkPipelineLayout layout, const PipelineSpecs& specs) {
  bg3::pipe_async::Result r{};
  Bg3ShaderBlob blob{};
  if (!host->get_shader(req.c_str(), entry.c_str(), "cs_6_6", &blob)) return r;
  VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  mci.codeSize = blob.size_bytes;
  mci.pCode = blob.code;
  // A real module: chaining VkShaderModuleCreateInfo with a null module needs
  // maintenance5, which the game's device does not enable (VUID-...-stage-08771).
  VkShaderModule mod = VK_NULL_HANDLE;
  if (host->vk->CreateShaderModule(host->device, &mci, nullptr, &mod) != VK_SUCCESS) {
    host->log("gi: %s shader module creation failed", req.c_str());
    return r;
  }
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, entry.c_str(),
               nullptr};
  pci.layout = layout;
  const VkSpecializationInfo info{specs.count, specs.entries, specs.count * sizeof(uint32_t), specs.data};
  if (specs.count) pci.stage.pSpecializationInfo = &info;
  VkPipeline np = VK_NULL_HANDLE;
  const auto t_pipe = std::chrono::steady_clock::now();
  const VkResult pr = host->vk->CreateComputePipelines(host->device, VK_NULL_HANDLE, 1, &pci, nullptr, &np);
  r.pipeline_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_pipe).count();
  host->vk->DestroyShaderModule(host->device, mod, nullptr);
  if (pr != VK_SUCCESS) {
    host->log("gi: %s pipeline creation failed (%d)", req.c_str(), int(pr));
    return r;
  }
  r.pipeline = uint64_t(np);
  r.generation = blob.generation;
  r.ok = true;
  return r;
}

std::string build_key(const std::string& req, const std::string& entry, VkPipelineLayout layout, const PipelineSpecs& specs) {
  return req + specs.key() + "|" + entry + "|" + std::to_string(uint64_t(layout));
}

// Called when a pass first has its real layout (game descriptor layouts do not exist at DLL load).
void prewarm_experimental(const std::string& req, const std::string& entry, VkPipelineLayout layout, const PipelineSpecs& base) {
  std::lock_guard<std::recursive_mutex> lock(g_prewarm_mu);
  if (!g_prewarmed[uint64_t(layout)].insert(req + "|" + entry).second) return;
  unsigned count = 0;
  for (const auto& specs : bg3::pipe_async::experimental_specs(
           base, host->cfg_int("dev", 0) != 0)) {
    const auto key = build_key(req, entry, layout, specs);
    count += g_builds.prewarm(key, uint64_t(layout), [req, entry, layout, specs] {
      return build_pipeline(req, entry, layout, specs);
    }) ? 1u : 0u;
  }
  g_prewarm_count += count;
  if (count) host->log("gi: Experimental prewarm %s %s: %u extra pipelines, %llu startup total (below normal priority)",
                       req.c_str(), entry.c_str(), count, (unsigned long long)g_prewarm_count);
}

// Bound pipeline capability, including a first-frame capability publication race.
std::unordered_set<VkPipeline> g_omm_pipelines;
bool g_hair_origin_cap=false,g_hair_origin_init=false,g_hair_origin_frame=false;
uint32_t g_hair_origin_last_mode=UINT32_MAX;
bool g_hair_origin_warned=false;
void initialize_hair_origin(){
 if(g_hair_origin_init)return;g_hair_origin_init=true;
 const bool requested=host->cfg_int("rt_hair_physical_origin",0)!=0 && host->cfg_int("rt_native_hair",0)!=0;
 g_hair_origin_cap=bg3::hair_origin::capability(static_cast<uint32_t*>(host->persist(bg3::hair_origin::capability_key,sizeof(uint32_t))),requested);
 host->log("[hair origin] startup auxiliary capability %u; runtime A/B key rt_hair_physical_origin (off startup requires restart)",unsigned(g_hair_origin_cap));
}

struct Push {
  uint32_t full[2], half[2];
  uint32_t frame;
  float strength, radius, thickness;
  uint32_t rays, steps, debug;
  float history, ao;
  float cs_length, cs_strength;
  uint32_t cs_steps;
  float mv_scale[2];
  uint32_t has_mv, bounce_spec;   // bounce_spec: unused, 0 (gi_bounce_spec removed 2026-10-31)
  float rt_bias, rt_tmax;
  uint32_t rt_debug;
  float rt_strength;
  float rt_ao_radius;   // rt_hit_emission dispatches only: rt_em_tol (native_hit_emission.hlsli HIT_EM_TOL); else unused, 0
  uint32_t rt_ao_rays;  // hit_ao_rays: AO rays at gi_hit_direct / rf_hit_shade hits (native_hit_direct.hlsli native_hit_ao)
  uint32_t ls_kmode, sharc_fx;   // sharc_fx: sharc_inject.hlsl, the D-EM effects target is bound (49); ls_kmode: rt_hit_emission
                                 // dispatches: bit 0 screen hits too (HIT_EM_FLAGS); the D-LS dispatch: ls_k / ls_rest_samples
                                 // (common/ls_exact_k.h, read only by the LS_K variants); else 0
  float gi_rt_radius, gi_rt_tol;
  uint32_t gi_rt_fallback, rt_stats;
  float sun_radius;
  uint32_t res_shift;
  uint32_t qa_zero;   // QA light-off traces (docs/QA.md): bit 0 sun, 1 local lights, 2 ambient AO
  uint32_t rt_max_cand;   // QA (rt_max_candidates): ray-query candidate cap, 0 = 4096 (rt_common.hlsli)
  float ls_source_radius, ls_clear, ls_min_radius;
  uint32_t ls_samples, ls_shadowed, ls_debug, ls_stats, ls_pass, ls_soft;
  float ls_strength;
  uint32_t ls_has_mask, ls_flags;   // ls_flags: bit 0 per-light data (ls_lights), bits 8-12 owners traced (ls_owners),
                                    // bit 24 hit_ls_policy (the screen's light policy at gi_hit_direct / rf_hit_shade hits)
  float rf_max_rough, rf_mirror_rough, rf_tmax, rf_depth_tol, rf_edge, rf_strength;
  uint32_t rf_debug, rf_sample, rf_so, rf_validate, rf_models, rf_stats;   // rf_so: unused, 0 (removed 2026-10-31)
  // RT audit fixes (docs/archive/AUDIT_RT.md)
  uint32_t rt_backlit, mat_classes, ao_direct, rt_origin;
};
static_assert(sizeof(Push) == bg3::native_hit_gi::push_bytes, "Push must match gi_common.hlsli");

constexpr uint32_t kBindings = 18;
// The ray-traced passes add the TLAS (18), their visibility image (19) and a
// statistics buffer (20, rt_stats=1).
constexpr uint32_t kRtBindings = kBindings + 3;
// D-LS adds the game's light list: 21 set1/b0 flags (UBO), 22 b13 scale, 23 b23
// clusters, 24 b24 index list, 25 b25 lights (SSBOs), 26 b39 constants (UBO),
// 27 b40 shadow views (SSBO), 28 b41 shadow atlas (sampled), 29 set0/b2 (UBO).
constexpr uint32_t kLsBindings = kRtBindings + 9;
// D-RF adds full-grid hit distance (30). Half-grid delta/distance reuses GI's
// unused RGBA16F binding 8, keeping the push layout at 32 descriptors.
constexpr uint32_t kRfBindings = kLsBindings + 1;

// Bindings past the contiguous ranges (docs/archive/AUDIT_RT.md fixes), in every layout whose
// bit is set: 1 base, 2 ray-traced, 4 D-LS.
struct ExtraBinding {
  uint32_t binding;
  VkDescriptorType type;
  uint32_t layouts;
};
constexpr ExtraBinding kExtras[] = {
    // rtOrigin: geometric normal + surface offset (rt_origin=1). Also D-RF (8): its push set
    // is then 31 + 1 = 32 descriptors, exactly the RTX 5090's maxPushDescriptors (and the
    // minimum VK_KHR_push_descriptor guarantees), so D-RF can take no further extra.
    {40, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 | 4 | 8},
    {41, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4},       // RT3, set 1 b4 (class parameters)
    {42, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},      // SSS profiles, set 1 b28
    {43, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 7},      // set 0 b0 frame constants
    {44, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 7},       // gAux: .x the game's own AO before the fold (ao_direct)
    {45, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},      // D-LS per-light data: runtime lights uploaded (core/ls_lights.cpp)
    {46, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4},       // D-LS per-light data: one record per light array slot
    {47, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},       // rt_sun_replace: trust parameters, roof volumes (core/sun_trust.h)
    {48, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},      // set 0 b2: the absolute camera (rt_sun_replace, SHaRC)
    {49, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2},       // SHaRC injection: the D-EM effects target (else the albedo)
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
// The set is rewritten on the host (vkUpdateDescriptorSets) when its turn comes; a
// set a pending D-LS dispatch still binds must not be: the ring is deep (32) and a
// slot used fewer than kSunMinAge frames ago is never rewritten. A fresh slot is
// taken instead, and if all are that young D-LS is skipped this frame (counted and
// logged: "ls: sun set ring").
constexpr uint32_t kSunRing = 32;
constexpr uint64_t kSunMinAge = 8;
VkDescriptorSetLayout g_sun_dsl = VK_NULL_HANDLE;
VkDescriptorPool g_sun_pool = VK_NULL_HANDLE;
VkDescriptorSet g_sun_sets[kSunRing] = {};
uint64_t g_sun_used[kSunRing] = {};
uint64_t g_sun_full = 0;
uint32_t g_sun_slot = 0;
bool g_sun_slots[kSunRing] = {};   // the set's b7 holds the per-light data image (else the 1x1 fallback)
// b8: the sun trust image (core/sun_trust.h: header and roof volumes) for the hidden-hit sun's
// roof rule (rt/native_hit_sun.hlsli NATIVE_HIT_SUN_ROOF: rf_hit_shade, gi_hit_sun). D-LS ignores it.
constexpr uint32_t kSunBindings = 9;
VkDescriptorType sun_binding_type(uint32_t i) {
  return i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : i == 6 ? VK_DESCRIPTOR_TYPE_SAMPLER
       : i == 7 || i == 8 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
}
constexpr uint32_t kNumExtras = sizeof kExtras / sizeof kExtras[0];

std::mutex g_mu;
std::unordered_map<VkCommandBuffer, Snapshot> g_pending;   // pre done, post owed
VkDescriptorSetLayout g_set_layout = VK_NULL_HANDLE, g_rt_set_layout = VK_NULL_HANDLE, g_ls_set_layout = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE, g_rt_layout = VK_NULL_HANDLE, g_ls_layout = VK_NULL_HANDLE;
Pipe g_trace, g_temporal, g_ao_apply, g_composite, g_contact, g_rt_shadow, g_rt_debug, g_trace_rt, g_ls, g_ls_prep;
Pipe g_hit_material, g_hit_direct, g_hit_two_bounce, g_hit_gi, g_hit_gi_sharc;
Pipe g_hit_gi_sun, g_hit_gi_sun_sharc;   // gi_hit_sun (gi_trace_rt_hit_sun.hlsl): + the sun set 3
Pipe g_pan_primary, g_pan_ls;
Pipe g_rt_origin;   // robust ray origins (rt_origin=1, AUDIT_RT fix 6)
Pipe g_ls_match;    // D-LS per-light data: joins the runtime lights to the light array (core/ls_lights.h)
// D-LS variants (docs/TRACK_D_RT.md 9.9): g_ls serves the default settings only, g_ls_full every
// mode (ls_samples, ls_shadowed, ls_debug, ls_stats). A/B: ls_impl=0 runs the frozen pre-optimisation
// pass (shaders/gi/ls_ref, tools/lsref); ls_check=1 runs it and the live variant on the same pixels
// and counts those whose outputs differ (stats 80-83).
Pipe g_ls_full, g_ls_ref, g_ls_check, g_ls_full_check;
// D-LS box (projector) lights (ls_box=1, docs/TRACK_D_RT.md 9.12): the same pass compiled with
// LS_BOX=1, which also reads the lighting's own set 1 (b29 box records, b45 cookies, b16 sampler)
// bound as set 3. Its pipeline layout holds the game's set layout, so it is rebuilt if that changes.
Pipe g_ls_box, g_ls_full_box;
Pipe g_ls_native_full, g_ls_native_full_box;
// D-LS exact-k + RIS remainder (ls_k > 0, docs/TRACK_D_LOCAL_LIGHTS.md 9.11): the same four passes
// compiled with LS_K=1 (gi/rt_local_shadows*_k.hlsl); with ls_k=0 they are never built.
Pipe g_ls_k, g_ls_box_k, g_ls_full_k, g_ls_full_box_k;
VkPipelineLayout g_ls_box_layout = VK_NULL_HANDLE;
VkDescriptorSetLayout g_ls_box_game = VK_NULL_HANDLE;
// The box statistics (set 4 b0 of g_ls_box_layout): device-local counters, never host-visible
// atomics (META_AUDIT 2026-11-01 5: the m5w2/m5w3 lesson). One set, written once.
constexpr uint32_t kLsBoxXCount = 32;   // rt_local_shadows.hlsl kBxN
constexpr VkDeviceSize kLsBoxXBytes = VkDeviceSize(kLsBoxXCount) * 64 * 4;
VkBuffer g_bx_buf = VK_NULL_HANDLE;
VkDeviceMemory g_bx_mem = VK_NULL_HANDLE;
// rt_occ_stats (common/occ_stats.h, docs/DIALOGUE_SHADOWS.md): the occluder counters, device-local,
// set 4 binding 1 of the box layout and of g_occ_layout (the same set as the box statistics); zeroed
// before gi/rt_occ_stats.hlsl, copied into a host ring after the D-LS pass (outside both timed
// regions), read kOccRing - 1 frames later and logged once per 60 frames ("occ stats").
namespace occ = bg3::occ_stats;
constexpr uint32_t kOccRing = 8;
constexpr VkDeviceSize kOccBytes = occ::kOccBytes;
VkBuffer g_occ_buf = VK_NULL_HANDLE, g_occ_host = VK_NULL_HANDLE;
VkDeviceMemory g_occ_mem = VK_NULL_HANDLE, g_occ_host_mem = VK_NULL_HANDLE;
const uint32_t* g_occ_map = nullptr;
uint64_t g_occ_frame[kOccRing] = {};
uint32_t g_occ_slot = 0;
uint64_t g_occ_armed = 0;   // the frame whose counters the D-LS pass adds to (0: none)
occ::OccWindow g_occ_win;
bool g_occ_logged = false;
VkPipelineLayout g_occ_layout = VK_NULL_HANDLE;
Pipe g_occ;               // gi/rt_occ_stats.hlsl: the sun and AO occluders
Pipe g_ls_full_box_occ;   // gi/rt_local_shadows_full_box.hlsl (constant 1011 = 1): the point/spot and box occluders
Pipe g_hair_fibre;        // gi/hair_fibre.hlsl (rt_hair_fibre): strand pixels with the fibre BSDF, after D-LS
Pipe g_composite_fibre;   // M4.1: same GI layout; extra buffer-address capability only in its own request
VkDescriptorSetLayout g_bx_dsl = VK_NULL_HANDLE;
VkDescriptorPool g_bx_pool = VK_NULL_HANDLE;
VkDescriptorSet g_bx_set = VK_NULL_HANDLE;
bool g_ls_box_missing_logged = false, g_ls_box_active = false;
int g_ls_box_mode = 0;   // the ls_box value of the active box pass (1, 2), for the timing window
bool g_ls_k_active = false, g_ls_k_missing_logged = false;   // the D-LS dispatch ran an LS_K variant (ls_k > 0)
LslFrame g_lsl{};   // this frame's upload (bindings 45, 46)
bool g_lsl_ok = false;
SunTrustFrame g_sunt{};   // rt_sun_replace: this frame's trust slot (bindings 47, 48)
bool g_sunt_ok = false;
Pipe g_sun_resolve;       // rt_sun_replace: writes the trusted traced visibility (gi/rt_sun_resolve.hlsl)
// SHaRC (sharc=1, core/sharc.h): the ray-traced push set, the host's set 1, empty sets 2-3 (D-RF: the
// game's set 2) and the cache's set 4.
VkDescriptorSetLayout g_empty_dsl = VK_NULL_HANDLE;
VkDescriptorSetLayout g_hit_gi_set_layout=VK_NULL_HANDLE,g_hit_reference_set_layout=VK_NULL_HANDLE;
VkPipelineLayout g_hit_gi_plain_layout=VK_NULL_HANDLE,g_hit_reference_layout=VK_NULL_HANDLE;
VkPipelineLayout g_rt_sharc_layout = VK_NULL_HANDLE, g_rf_sharc_layout = VK_NULL_HANDLE, g_hit_gi_layout=VK_NULL_HANDLE;
VkPipelineLayout g_hit_gi_sun_layout=VK_NULL_HANDLE,g_hit_gi_sun_sharc_layout=VK_NULL_HANDLE;
bool g_hit_gi_sun_logged=false;
bool g_gi_far_logged=false,g_gi_far_nosun_logged=false;   // gi_far: the last logged state
Pipe g_trace_rt_sharc, g_rf_sharc, g_sharc_resolve, g_sharc_inject, g_sharc_inject_lock, g_sharc_debug, g_sharc_roof;
bool g_sharc_logged = false, g_sharc_missing_logged = false, g_sharc_paused_logged = false, g_sharc_em_logged = false;
bool g_ls_logged = false, g_ls_missing_logged = false;
// D-RF: its pipeline is built against the game's set layout of the lighting's set 1
// (bound as set 2), so it is rebuilt if that layout ever changes.
VkDescriptorSetLayout g_rf_set_layout = VK_NULL_HANDLE, g_rf_game_layout = VK_NULL_HANDLE;
VkPipelineLayout g_rf_layout = VK_NULL_HANDLE;
Pipe g_rf;
// rf_hit_shade (M4.2 tier b, common/native_hit_rf_bindings.h): D-RF's push set, the material
// AlphaDSL as set 1, the game's lighting set 1 as set 2, the D-LS sun set as set 3 (+ SHaRC set 4).
// Built over g_rf_set_layout and g_rf_game_layout: ensure_rf_layout drops them with those.
bool g_rf_hit_logged = false, g_rf_hit_contract_logged = false;
// rt_hit_emission (M4.4, rt/native_hit_emission.hlsli): the hit-GI and rf_hit_shade variants with the
// host's D-EM emissive scene (Bg3HostApi::rt_emissive) as one more set: GI set 2 (the empty one),
// D-RF set 5. Index: GI [2 x sun + sharc], D-RF [sharc]. Built over the host's emissive set layout
// (g_hit_em_dsl): dropped if it changes, and the D-RF ones with ensure_rf_layout's.
Pipe g_hit_gi_em[4];
VkPipelineLayout g_hit_gi_em_layout[4] = {};
VkDescriptorSetLayout g_hit_em_dsl = VK_NULL_HANDLE;
bool g_hit_em_logged = false;
// rf_hit_gi (docs/TRACK_D_REFLECTIONS.md 14 "Indirect diffuse at hits"): the rf_hit_shade passes compiled
// with RF_SPEC_HIT_GI=1 (constant 1013), index [2 x emission + sharc]; layout: rf_hit_shade's
// sets 0-3, set 4 SHaRC's or empty, set 5 the emissive scene or empty, set 6 the device-local counters
// (ensure_rg; common/native_hit_rf_bindings.h kSetRfGi). Dropped with the rf_hit_shade / emission layouts.
Pipe g_rf_shade_gi[4];
VkPipelineLayout g_rf_shade_gi_layout[4] = {};
constexpr uint32_t kRfGiCount = 8;   // rt_reflections.hlsl kRgN
constexpr VkDeviceSize kRfGiBytes = VkDeviceSize(kRfGiCount) * 64 * 4;
VkBuffer g_rg_buf = VK_NULL_HANDLE;
VkDeviceMemory g_rg_mem = VK_NULL_HANDLE;
VkDescriptorSetLayout g_rg_dsl = VK_NULL_HANDLE;
VkDescriptorPool g_rg_pool = VK_NULL_HANDLE;
VkDescriptorSet g_rg_set = VK_NULL_HANDLE;
bool g_rg_ran = false;   // this frame's D-RF ran an RF_HIT_GI variant (its counters are copied after the pass)
int g_rg_logged = -1;   // the logged (mode, built) state: mode x 2 + built, -1 none
void rf_gi_drop() {
  for (Pipe& p : g_rf_shade_gi) {
    for (const auto& [slot, v] : p.variants) host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)v.pipeline);
    p = Pipe{};
  }
  for (VkPipelineLayout& l : g_rf_shade_gi_layout) {
    defer_layout_destroy(uint64_t(l));
    l = VK_NULL_HANDLE;
  }
}
bool g_rf_logged = false, g_rf_missing_logged = false;
Bg3Image g_rf_hit{};   // R16F specular hit distance for RR (world units; 65504 = no traced hit)
Bg3Image g_raw{}, g_hist[2]{}, g_raw_depth{}, g_hist_depth[2]{}, g_prev_radiance{}, g_rt_vis{};
Bg3Image g_hair_origin{}; // Physical normal + radial tBase, RGBA32F; full resolution at startup opt-in, otherwise 1x1.
Bg3Image g_origin{}, g_aux{};   // extras 40 and 44 (full resolution, RGBA16F)
Bg3Image g_ls_nodata{};          // extra 46 when there is no per-light data: 1x1 RGBA32UI (the shaders' type)
bool g_x_logged = false;
uint32_t g_w = 0, g_h = 0, g_hw = 0, g_hh = 0, g_parity = 0, g_shift = 1;

// RR mode (docs/QUALITY.md fix 3): with Ray Reconstruction denoising the frame,
// the GI uses the preset's ray count and keeps no history of its own, the
// input RR is designed for. Ultra uses two rays. gi_rr: -1 follows rr, 0 off, 1 on.
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
uint64_t g_slot_hair_epoch[kRing] = {};
HairCostEpoch g_hair_timing_epoch;
void hair_timing_update();
double g_ns_per_tick = 1.0, g_ms_sum = 0;
uint32_t g_ms_count = 0;

void timing_init() {
  if (!bg3::logging::stats_enabled(host->cfg_int)) return;
  if (g_queries) return;
  VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qi.queryCount = kRing * 4;
  host->vk->CreateQueryPool(host->device, &qi, nullptr, &g_queries);
  g_ns_per_tick = host->cfg_float("gpu_timestamp_period_ns", 1.0f);
}

void timestamp(VkCommandBuffer cmd, uint32_t index) {
  if (!bg3::logging::stats_enabled(host->cfg_int)) return;
  if (!g_queries) return;
  if (index == 0) g_slot_hair_epoch[g_slot] = g_hair_timing_epoch.generation;
  uint32_t q = g_slot * 4 + index;
  host->vk->CmdResetQueryPool(cmd, g_queries, q, 1);
  host->vk->CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_queries, q);
}

// Reads the oldest ring slot if the GPU is done with it.
void timing_collect() {
  if (!bg3::logging::stats_enabled(host->cfg_int)) return;
  hair_timing_update();
  if (!g_queries) return;
  uint32_t old = (g_slot + 1) % kRing;
  if (!g_slot_written[old]) return;
  if (!g_hair_timing_epoch.accepts(g_slot_hair_epoch[old])) { g_slot_written[old]=false; return; }
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
  if (g_ms_count >= stats_period(300)) {
    log_stats("gi: GPU cost %.3f ms/frame (average of %u frames)", g_ms_sum / g_ms_count, g_ms_count);
    g_ms_sum = 0;
    g_ms_count = 0;
  }
}

Bg3Image* const all_images[] = {&g_raw, &g_hist[0], &g_hist[1], &g_raw_depth, &g_hist_depth[0], &g_hist_depth[1],
                                &g_prev_radiance, &g_rt_vis, &g_rf_hit, &g_origin, &g_aux, &g_ls_nodata};

// Log-only subdivisions of the enclosing GI timer. Hit direct/sun/emission and
// far bounce run INSIDE gi_trace_rt, so the existing trace timer includes them.
// These pairs measure dispatches only; image transitions/join barriers stay in
// the enclosing timer. Never infer these costs by subtracting summary medians.
enum GiPass { kOrigin, kTemporal, kAoFold, kSunTrace, kSunResolve, kContact, kComposite, kGiPasses };
constexpr const char* kGiPassNames[kGiPasses] = {
  "origin", "temporal", "AO fold", "sun trace", "sun resolve", "contact", "composite"};
VkQueryPool g_pass_queries = VK_NULL_HANDLE;
uint32_t g_pass_written[kRing] = {};
double g_pass_sum[kGiPasses] = {};
uint32_t g_pass_count[kGiPasses] = {};

void pass_timing_reset() {
  std::fill_n(g_pass_written, kRing, 0u);
  std::fill_n(g_pass_sum, kGiPasses, 0.0);
  std::fill_n(g_pass_count, kGiPasses, 0u);
}

void pass_timing_collect() {
  if (!host->cfg_int("detailed_logging", 0)) return;
  if (!g_pass_queries) return;
  const uint32_t old = (g_slot + 1) % kRing;
  for (uint32_t pass = 0; pass < kGiPasses; ++pass) {
    if (!(g_pass_written[old] & (1u << pass))) continue;
    uint64_t t[2];
    const uint32_t q = (old * kGiPasses + pass) * 2;
    if (host->vk->GetQueryPoolResults(host->device, g_pass_queries, q, 2, sizeof t, t,
          sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) continue;
    g_pass_written[old] &= ~(1u << pass);
    const double ms = double(t[1] - t[0]) * g_ns_per_tick * 1e-6;
    if (ms >= 0 && ms < 1000) { g_pass_sum[pass] += ms; ++g_pass_count[pass]; }
    if (g_pass_count[pass] < stats_period(300)) continue;
    host->log("gi: %s pass GPU cost %.3f ms/frame (average of %u dispatched frames)",
      kGiPassNames[pass], g_pass_sum[pass] / g_pass_count[pass], g_pass_count[pass]);
    g_pass_sum[pass] = 0; g_pass_count[pass] = 0;
  }
}

void pass_timestamp(VkCommandBuffer cmd, GiPass pass, uint32_t edge, bool dispatched = true) {
  if (!host->cfg_int("detailed_logging", 0)) return;
  if (!g_pass_queries) {
    if (edge != 0) return;
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kRing * kGiPasses * 2;
    if (host->vk->CreateQueryPool(host->device, &qi, nullptr, &g_pass_queries) != VK_SUCCESS) return;
  }
  const uint32_t q = (g_slot * kGiPasses + pass) * 2 + edge;
  host->vk->CmdResetQueryPool(cmd, g_pass_queries, q, 1);
  host->vk->CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, g_pass_queries, q);
  // Missing/failed/partly recorded passes are not zero-cost samples.
  if (edge == 1 && dispatched) g_pass_written[g_slot] |= 1u << pass;
}

// Track D: GPU time of the ray-traced shadow pass, 2 timestamps per frame.
VkQueryPool g_rt_queries = VK_NULL_HANDLE;
uint32_t g_rt_slot = 0;
bool g_rt_written[kRing] = {};
uint64_t g_rt_hair_epoch[kRing] = {};
double g_rt_ms_sum = 0;
uint32_t g_rt_ms_count = 0;
uint64_t g_rt_frames_traced = 0, g_rt_frames_stale = 0, g_rt_frames_missing = 0;
bool g_rt_logged = false;

void hair_timing_update() {
  if (!g_hair_timing_epoch.update(hair_cost_mask([](const char* key,int fallback) { return host->cfg_int(key,fallback); }))) return;
  std::fill_n(g_slot_written,kRing,false);
  std::fill_n(g_rt_written,kRing,false);
  g_ms_sum = g_rt_ms_sum = 0;
  g_ms_count = g_rt_ms_count = 0;
  g_rt_frames_traced = g_rt_frames_stale = g_rt_frames_missing = 0;
  pass_timing_reset();
}

void rt_timing_collect() {
  if (!bg3::logging::stats_enabled(host->cfg_int)) return;
  hair_timing_update();
  if (!g_rt_queries) return;
  uint32_t old = (g_rt_slot + 1) % kRing;
  if (!g_rt_written[old]) return;
  if (!g_hair_timing_epoch.accepts(g_rt_hair_epoch[old])) { g_rt_written[old]=false; return; }
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
  if (g_rt_ms_count >= stats_period(300)) {
    log_stats("rt passes: GPU cost %.3f ms/frame (average of %u frames); TLAS this frame %llu, previous frame %llu, "
              "none %llu", g_rt_ms_sum / g_rt_ms_count, g_rt_ms_count, (unsigned long long)g_rt_frames_traced,
              (unsigned long long)g_rt_frames_stale, (unsigned long long)g_rt_frames_missing);
    g_rt_ms_sum = 0;
    g_rt_ms_count = 0;
    g_rt_frames_traced = g_rt_frames_stale = g_rt_frames_missing = 0;
  }
}

void rt_timestamp(VkCommandBuffer cmd, uint32_t index) {
  if (!bg3::logging::stats_enabled(host->cfg_int)) return;
  if (!g_rt_queries) return;
  if (index == 0) g_rt_hair_epoch[g_rt_slot] = g_hair_timing_epoch.generation;
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
  if (!bg3::logging::stats_enabled(host->cfg_int)) return;
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
  if (!bg3::logging::stats_enabled(host->cfg_int)) return;
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
    if (v.size() < stats_period(300)) continue;
    std::sort(v.begin(), v.end());
    const int rr_setting=host->cfg_int("gi_rr",-1);
    bool rr_mode=rr_setting<0?host->rr_active():rr_setting!=0;
    log_stats("gi: trace pass (%s, %u rays/sample, %s) GPU median %.3f ms, p10 %.3f, p90 %.3f (%zu frames)",
              k ? "ray-traced, gi_rt=1" : "screen-space",
              uint32_t(rr_mode ? host->cfg_int("gi_rr_rays", 1) : host->cfg_int("gi_rays", 4)),
              rr_mode ? (g_shift?"half res, spatial resolve + RR":"full res, RR denoises") : "half res, own accumulation", v[v.size() / 2], v[v.size() / 10],
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
  if (!s.ls_ok && !qb(0, 2, s.l_cam2)) s.l_cam2 = Bg3Binding{};   // rt_sun_replace needs it alone
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
  Bg3Binding boxes{};
  s.boxes_bound = s.game_set1 && q(1, 29, boxes) && boxes.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && boxes.buffer;
  s.box_ready = host->cfg_int("ls_box", 0) && s.boxes_bound && boxes.range >= 128;
  return s.nrm.width == s.dif.width && s.dep.width == s.dif.width && s.ao.width == s.dif.width && s.dif.width > 0;
}

void release_images() {
  if(g_hair_origin.image){
    host->defer_destroy(BG3_DEFER_IMAGE_VIEW,uint64_t(g_hair_origin.view));
    host->defer_destroy(BG3_DEFER_IMAGE,uint64_t(g_hair_origin.image));
    host->defer_destroy(BG3_DEFER_MEMORY,uint64_t(g_hair_origin.memory));
    g_hair_origin={};
  }
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
  if(ok) {
    ok=host->alloc_image(g_hair_origin_cap?w:1u,g_hair_origin_cap?h:1u,VK_FORMAT_R32G32B32A32_SFLOAT,use,&g_hair_origin);
    if(ok && g_hair_origin_cap)host->log("[hair origin] auxiliary image %ux%u, %llu bytes (16 bytes/pixel); GPU cost pending",w,h,(unsigned long long)(uint64_t(w)*h*16));
  }
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
  if (i >= kLsBindings) return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;   // D-RF hit distance
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
  VkDescriptorSetLayoutBinding b[kRfBindings + kNumExtras + 1];
  const uint32_t mask = layout_mask(count);
  uint32_t n = 0;
  for (uint32_t i = 0; i < count; ++i)
    if ((mask != 4u || ls_uses(i)) && bg3::hair_origin::binding_enabled(g_hair_origin_cap,mask,i)) b[n++] = {i, binding_type(i), 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  for (const ExtraBinding& x : kExtras)
    if (x.layouts & mask) b[n++] = {x.binding, x.type, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  if(mask!=1u)b[n++]={50,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
  VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  sci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
  sci.bindingCount = n;
  sci.pBindings = b;
  VkDescriptorSetLayout next_set = VK_NULL_HANDLE;
  if (host->vk->CreateDescriptorSetLayout(host->device, &sci, nullptr, &next_set) != VK_SUCCESS) return false;
  VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
  VkDescriptorSetLayout sets[3] = {next_set, set1, set2};
  VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lci.setLayoutCount = set2 ? 3 : set1 ? 2 : 1;
  lci.pSetLayouts = sets;
  lci.pushConstantRangeCount = 1;
  lci.pPushConstantRanges = &pc;
  VkPipelineLayout next_layout = VK_NULL_HANDLE;
  if (host->vk->CreatePipelineLayout(host->device, &lci, nullptr, &next_layout) != VK_SUCCESS) {
    host->vk->DestroyDescriptorSetLayout(host->device, next_set, nullptr);
    return false;
  }
  *set_layout = next_set;
  *layout = next_layout;
  return true;
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
  VkDescriptorSetLayout next_set = VK_NULL_HANDLE;
  VkPipelineLayout next_layout = VK_NULL_HANDLE;
  if (!make_layout(kRfBindings, &next_set, &next_layout, host_layout, game)) return false;
  if (g_rf_layout) {
    for (const auto& [slot, v] : g_rf.variants) host->defer_destroy(BG3_DEFER_PIPELINE, uint64_t(v.pipeline));
    defer_layout_destroy(uint64_t(g_rf_layout));
    host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_rf_set_layout);
    g_rf = Pipe{};
    g_rf_layout = VK_NULL_HANDLE;
    g_rf_set_layout = VK_NULL_HANDLE;
    for (const auto& [slot, v] : g_rf_sharc.variants) host->defer_destroy(BG3_DEFER_PIPELINE, uint64_t(v.pipeline));
    defer_layout_destroy(uint64_t(g_rf_sharc_layout));
    g_rf_sharc = Pipe{};
    g_rf_sharc_layout = VK_NULL_HANDLE;
    rf_gi_drop();
    host->log("rf: the lighting's set 1 layout changed (%p -> %p); rebuilding", (void*)g_rf_game_layout, (void*)game);
  }
  g_rf_set_layout = next_set;
  g_rf_layout = next_layout;
  g_rf_game_layout = game;
  return true;
}

// SHaRC: a pipeline layout over existing set layouts, set 4 the cache's, the gaps empty sets.
bool make_sharc_layout(VkDescriptorSetLayout s0, VkDescriptorSetLayout s1, VkDescriptorSetLayout s2, VkPipelineLayout* out) {
  if (*out) return true;
  if (!g_empty_dsl) {
    VkDescriptorSetLayoutCreateInfo ei{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    if (host->vk->CreateDescriptorSetLayout(host->device, &ei, nullptr, &g_empty_dsl) != VK_SUCCESS) return false;
  }
  VkDescriptorSetLayout sh = sharc_set_layout();
  if (!s0 || !s1 || !sh) return false;
  VkDescriptorSetLayout sets[5] = {s0, s1, s2 ? s2 : g_empty_dsl, g_empty_dsl, sh};
  VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
  VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lci.setLayoutCount = 5;
  lci.pSetLayouts = sets;
  lci.pushConstantRangeCount = 1;
  lci.pPushConstantRanges = &pc;
  return host->vk->CreatePipelineLayout(host->device, &lci, nullptr, out) == VK_SUCCESS;
}

bool ensure_rt_sharc_layout() {
  VkDescriptorSetLayout host_layout = VK_NULL_HANDLE;
  VkDescriptorSet unused;
  if (!g_rt_set_layout || !host->rt_alpha || !host->rt_alpha(&host_layout, &unused)) return false;
  return make_sharc_layout(g_rt_set_layout, host_layout, VK_NULL_HANDLE, &g_rt_sharc_layout);
}

bool ensure_rf_sharc_layout() {
  VkDescriptorSetLayout host_layout = VK_NULL_HANDLE;
  VkDescriptorSet unused;
  if (!g_rf_set_layout || !g_rf_game_layout || !host->rt_alpha || !host->rt_alpha(&host_layout, &unused)) return false;
  return make_sharc_layout(g_rf_set_layout, host_layout, g_rf_game_layout, &g_rf_sharc_layout);
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
    // b7: the D-LS per-light data (lsSlots) for rf_hit_shade's light policy (D-RF's push set is
    // full; common/native_hit_rf_bindings.h sun). D-LS reads it at push b46 and never uses b7.
    VkDescriptorSetLayoutBinding b[kSunBindings];
    for (uint32_t i = 0; i < kSunBindings; ++i)
      b[i] = {i, sun_binding_type(i), 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sci.bindingCount = kSunBindings;
    sci.pBindings = b;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet sets[kSunRing] = {};
    if (host->vk->CreateDescriptorSetLayout(host->device, &sci, nullptr, &dsl) != VK_SUCCESS) return false;
    VkDescriptorPoolSize ps[4] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSunRing},
                                  {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 5 * kSunRing},
                                  {VK_DESCRIPTOR_TYPE_SAMPLER, kSunRing},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * kSunRing}};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = kSunRing;
    pci.poolSizeCount = 4;
    pci.pPoolSizes = ps;
    if (host->vk->CreateDescriptorPool(host->device, &pci, nullptr, &pool) != VK_SUCCESS) {
      host->vk->DestroyDescriptorSetLayout(host->device, dsl, nullptr);
      return false;
    }
    VkDescriptorSetLayout ls[kSunRing];
    for (auto& l : ls) l = dsl;
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool;
    ai.descriptorSetCount = kSunRing;
    ai.pSetLayouts = ls;
    if (host->vk->AllocateDescriptorSets(host->device, &ai, sets) != VK_SUCCESS) {
      host->vk->DestroyDescriptorPool(host->device, pool, nullptr);
      host->vk->DestroyDescriptorSetLayout(host->device, dsl, nullptr);
      return false;
    }
    g_sun_dsl = dsl;
    g_sun_pool = pool;
    std::copy_n(sets, kSunRing, g_sun_sets);
  }
  VkDescriptorSetLayout host_layout = VK_NULL_HANDLE;
  VkDescriptorSet unused;
  if (!host->rt_alpha || !host->rt_alpha(&host_layout, &unused) || !host_layout) return false;
  return make_layout(kLsBindings, &g_ls_set_layout, &g_ls_layout, host_layout, g_sun_dsl);
}

// The box statistics buffer: device-local (the core/water.cpp pattern, memory types from
// BG3_DEVICE_INFO_KEY), its set layout and its one descriptor set (set 4 of g_ls_box_layout).
bool ensure_bx() {
  if (g_bx_set) return true;
  const VkDeviceFuncs& vk = *host->vk;
  const auto* d = static_cast<const Bg3DeviceInfo*>(host->persist(BG3_DEVICE_INFO_KEY, sizeof(Bg3DeviceInfo)));
  if (!d || d->version < 1) return false;
  if (!g_occ_buf) {   // rt_occ_stats: set 4 binding 1 (40 KB, always present so the set is complete)
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = kOccBytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vk.CreateBuffer(host->device, &bci, nullptr, &g_occ_buf) != VK_SUCCESS) return g_occ_buf = VK_NULL_HANDLE, false;
    VkMemoryRequirements req;
    vk.GetBufferMemoryRequirements(host->device, g_occ_buf, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < d->memory.memoryTypeCount && mai.memoryTypeIndex == UINT32_MAX; ++i)
      if ((req.memoryTypeBits & (1u << i)) && (d->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        mai.memoryTypeIndex = i;
    if (mai.memoryTypeIndex == UINT32_MAX || vk.AllocateMemory(host->device, &mai, nullptr, &g_occ_mem) != VK_SUCCESS ||
        vk.BindBufferMemory(host->device, g_occ_buf, g_occ_mem, 0) != VK_SUCCESS) {
      vk.DestroyBuffer(host->device, g_occ_buf, nullptr);
      if (g_occ_mem) vk.FreeMemory(host->device, g_occ_mem, nullptr);
      g_occ_buf = VK_NULL_HANDLE, g_occ_mem = VK_NULL_HANDLE;
      return false;
    }
  }
  if (!g_bx_buf) {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = kLsBoxXBytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vk.CreateBuffer(host->device, &bci, nullptr, &g_bx_buf) != VK_SUCCESS) return g_bx_buf = VK_NULL_HANDLE, false;
    VkMemoryRequirements req;
    vk.GetBufferMemoryRequirements(host->device, g_bx_buf, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < d->memory.memoryTypeCount && mai.memoryTypeIndex == UINT32_MAX; ++i)
      if ((req.memoryTypeBits & (1u << i)) && (d->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        mai.memoryTypeIndex = i;
    if (mai.memoryTypeIndex == UINT32_MAX || vk.AllocateMemory(host->device, &mai, nullptr, &g_bx_mem) != VK_SUCCESS ||
        vk.BindBufferMemory(host->device, g_bx_buf, g_bx_mem, 0) != VK_SUCCESS) {
      vk.DestroyBuffer(host->device, g_bx_buf, nullptr);
      if (g_bx_mem) vk.FreeMemory(host->device, g_bx_mem, nullptr);
      g_bx_buf = VK_NULL_HANDLE, g_bx_mem = VK_NULL_HANDLE;
      return false;
    }
  }
  if (!g_bx_dsl) {
    // b0 the box statistics, b1 the rt_occ_stats counters (rt/occ_stats.hlsli occStats)
    VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                         {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sci.bindingCount = 2;
    sci.pBindings = b;
    if (vk.CreateDescriptorSetLayout(host->device, &sci, nullptr, &g_bx_dsl) != VK_SUCCESS) return g_bx_dsl = VK_NULL_HANDLE, false;
  }
  if (!g_bx_pool) {
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 1;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &ps;
    if (vk.CreateDescriptorPool(host->device, &pci, nullptr, &g_bx_pool) != VK_SUCCESS) return g_bx_pool = VK_NULL_HANDLE, false;
  }
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = g_bx_pool;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &g_bx_dsl;
  if (vk.AllocateDescriptorSets(host->device, &ai, &g_bx_set) != VK_SUCCESS) return g_bx_set = VK_NULL_HANDLE, false;
  VkDescriptorBufferInfo bi[2] = {{g_bx_buf, 0, kLsBoxXBytes}, {g_occ_buf, 0, kOccBytes}};
  VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
  for (uint32_t i = 0; i < 2; ++i) {
    w[i].dstSet = g_bx_set;
    w[i].dstBinding = i;
    w[i].descriptorCount = 1;
    w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[i].pBufferInfo = &bi[i];
  }
  vk.UpdateDescriptorSets(host->device, 2, w, 0, nullptr);
  return true;
}

// rf_hit_gi's counters (rt_reflections.hlsl RF_HIT_GI rfGiStats, set 6 b0): device-local like the box
// statistics, their own layout and one descriptor set; zeroed before the D-RF pass, copied into rtStats
// kRfGiFirst after its end timestamp (run_post).
bool ensure_rg() {
  if (g_rg_set) return true;
  const VkDeviceFuncs& vk = *host->vk;
  const auto* d = static_cast<const Bg3DeviceInfo*>(host->persist(BG3_DEVICE_INFO_KEY, sizeof(Bg3DeviceInfo)));
  if (!d || d->version < 1) return false;
  if (!g_rg_buf) {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = kRfGiBytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vk.CreateBuffer(host->device, &bci, nullptr, &g_rg_buf) != VK_SUCCESS) return g_rg_buf = VK_NULL_HANDLE, false;
    VkMemoryRequirements req;
    vk.GetBufferMemoryRequirements(host->device, g_rg_buf, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < d->memory.memoryTypeCount && mai.memoryTypeIndex == UINT32_MAX; ++i)
      if ((req.memoryTypeBits & (1u << i)) && (d->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        mai.memoryTypeIndex = i;
    if (mai.memoryTypeIndex == UINT32_MAX || vk.AllocateMemory(host->device, &mai, nullptr, &g_rg_mem) != VK_SUCCESS ||
        vk.BindBufferMemory(host->device, g_rg_buf, g_rg_mem, 0) != VK_SUCCESS) {
      vk.DestroyBuffer(host->device, g_rg_buf, nullptr);
      if (g_rg_mem) vk.FreeMemory(host->device, g_rg_mem, nullptr);
      g_rg_buf = VK_NULL_HANDLE, g_rg_mem = VK_NULL_HANDLE;
      return false;
    }
  }
  if (!g_rg_dsl) {
    const auto& rb = bg3::native_hit_rf::rf_gi_stats;
    VkDescriptorSetLayoutBinding b{rb.binding, rb.type, rb.count, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sci.bindingCount = 1;
    sci.pBindings = &b;
    if (vk.CreateDescriptorSetLayout(host->device, &sci, nullptr, &g_rg_dsl) != VK_SUCCESS) return g_rg_dsl = VK_NULL_HANDLE, false;
  }
  if (!g_rg_pool) {
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 1;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &ps;
    if (vk.CreateDescriptorPool(host->device, &pci, nullptr, &g_rg_pool) != VK_SUCCESS) return g_rg_pool = VK_NULL_HANDLE, false;
  }
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = g_rg_pool;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &g_rg_dsl;
  if (vk.AllocateDescriptorSets(host->device, &ai, &g_rg_set) != VK_SUCCESS) return g_rg_set = VK_NULL_HANDLE, false;
  VkDescriptorBufferInfo bi{g_rg_buf, 0, kRfGiBytes};
  VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  w.dstSet = g_rg_set;
  w.dstBinding = bg3::native_hit_rf::rf_gi_stats.binding;
  w.descriptorCount = 1;
  w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  w.pBufferInfo = &bi;
  vk.UpdateDescriptorSets(host->device, 1, &w, 0, nullptr);
  return true;
}

// ls_box: D-LS's push set 0, the host's set 1, the sun set 2, the game's layout of the
// lighting's set 1 as set 3, and the device-local box statistics as set 4 (ensure_bx). A
// different game layout rebuilds the box pipelines.
// rt_occ_stats: the host ring and the sun/AO pass's layout (the RT push set 0, the host's set 1,
// empty sets 2-3, the box statistics set 4 whose binding 1 is the occluder counters).
bool ensure_occ() {
  if (g_occ_layout && g_occ_host) return true;
  if (!ensure_rt_layout() || !ensure_bx() || !host->alloc_host_buffer) return false;
  if (!g_occ_host) {
    void* m = nullptr;
    if (!host->alloc_host_buffer(kOccBytes * kOccRing, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &g_occ_host, &g_occ_host_mem, &m))
      return g_occ_host = VK_NULL_HANDLE, false;
    g_occ_map = static_cast<const uint32_t*>(m);
    std::memset(g_occ_frame, 0, sizeof g_occ_frame);
  }
  if (!g_empty_dsl) {
    VkDescriptorSetLayoutCreateInfo ei{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    if (host->vk->CreateDescriptorSetLayout(host->device, &ei, nullptr, &g_empty_dsl) != VK_SUCCESS) return false;
  }
  VkDescriptorSetLayout host_layout = VK_NULL_HANDLE;
  VkDescriptorSet unused;
  if (!host->rt_alpha || !host->rt_alpha(&host_layout, &unused) || !host_layout) return false;
  VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
  VkDescriptorSetLayout sets[5] = {g_rt_set_layout, host_layout, g_empty_dsl, g_empty_dsl, g_bx_dsl};
  VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lci.setLayoutCount = 5;
  lci.pSetLayouts = sets;
  lci.pushConstantRangeCount = 1;
  lci.pPushConstantRanges = &pc;
  if (host->vk->CreatePipelineLayout(host->device, &lci, nullptr, &g_occ_layout) != VK_SUCCESS) return g_occ_layout = VK_NULL_HANDLE, false;
  return true;
}

bool ensure_ls_box_layout(VkDescriptorSetLayout game) {
  if (!game || !ensure_ls_layout() || !ensure_bx()) return false;
  if (g_ls_box_layout && g_ls_box_game == game) return true;
  VkDescriptorSetLayout host_layout = VK_NULL_HANDLE;
  VkDescriptorSet unused;
  if (!host->rt_alpha || !host->rt_alpha(&host_layout, &unused) || !host_layout) return false;
  if (g_ls_box_layout) {
    for (Pipe* p : {&g_ls_box, &g_ls_full_box, &g_ls_native_full_box, &g_ls_box_k, &g_ls_full_box_k, &g_ls_full_box_occ}) {
      host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)p->pipeline);
      *p = Pipe{};
    }
    defer_layout_destroy(uint64_t(g_ls_box_layout));
    g_ls_box_layout = VK_NULL_HANDLE;
    host->log("ls: the lighting's set 1 layout changed (%p -> %p); rebuilding the box-light pipelines",
              (void*)g_ls_box_game, (void*)game);
  }
  VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
  VkDescriptorSetLayout sets[5] = {g_ls_set_layout, host_layout, g_sun_dsl, game, g_bx_dsl};
  VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lci.setLayoutCount = 5;
  lci.pSetLayouts = sets;
  lci.pushConstantRangeCount = 1;
  lci.pPushConstantRanges = &pc;
  if (host->vk->CreatePipelineLayout(host->device, &lci, nullptr, &g_ls_box_layout) != VK_SUCCESS) {
    g_ls_box_layout = VK_NULL_HANDLE;
    return false;
  }
  g_ls_box_game = game;
  return true;
}

// rf_hit_shade: D-RF's push set 0, the host alpha layout (the material AlphaDSL has it) as set 1,
// the game's lighting layout as set 2, the sun set 3 (g_sun_dsl), the SHaRC set 4. The push set
// is D-RF's own: the contract's push table must be exactly what binding_type() builds.
bool ensure_rf_hit_contract() {
  namespace rfh = bg3::native_hit_rf;
  if (!g_rf_set_layout || !g_rf_game_layout || !ensure_ls_layout() || !g_sun_dsl) return false;
  for (const auto& b : rfh::push) {
    const VkDescriptorType t = b.binding < kRfBindings ? binding_type(b.binding)
                             : b.binding == 40u || b.binding == 50u ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                                    : VK_DESCRIPTOR_TYPE_MAX_ENUM;
    if (t != b.type || b.count != 1u) {
      if (!g_rf_hit_contract_logged) host->log("rf hit shade: push binding %u differs from the D-RF layout; off", b.binding);
      g_rf_hit_contract_logged = true;
      return false;
    }
  }
  return true;
}

bool refresh(Pipe& p, const char* file, VkPipelineLayout layout = VK_NULL_HANDLE) {
  std::lock_guard<std::recursive_mutex> lock(g_prewarm_mu);
  namespace sv = bg3::shader_variant;
  // rt_cascade_casters (docs/TRACK_D_RT.md 6.7 item 2): the TLAS holds the sun cascade's
  // casters as instance mask 0x02; these variants trace 0x01 except the sun ray
  // (RT_CASCADE_MASKS, shaders/rt/rt_common.hlsli). rt_sun_resolve traces nothing.
  // ls_box_cascade_old=1 (diagnostic A/B, live): the box passes trace 0xFF again, as before the 2026-11-02 fix, so
  // the stale cascade copy shadows local/box lights (rt_occ_stats "cascade" kind under local/box shows it).
  const bool box_old = std::strncmp(file, "gi/rt_local_shadows", 19) == 0 && std::strstr(file, "_box") &&
                       host->cfg_int("ls_box_cascade_old", 0) != 0;
  const bool cascade = sv::cascade_entry(file) && host->cfg_int("rt_cascade_casters", 0) != 0 && !box_old;
  // Include box/native/check helpers too: each may reach rt_common through
  // includes even when it is outside the variant list.
  const bool scalp = std::strncmp(file, "gi/", 3) == 0 && bg3::native_scalp::has_capability(static_cast<uint32_t*>(
      host->persist(bg3::native_scalp::capability_key, sizeof(uint32_t))));
  // Constant 1013 selects the hit GI program; its value joins the pipeline key.
  const bool occ_stats = &p == &g_ls_full_box_occ;
  const bool rf_gi = sv::rf_hit_gi_entry(file) && host->cfg_int("rf_hit_gi", 0) > 0;
  const bool omm=std::strncmp(file,"gi/",3)==0 && bg3::omm::scene_capability(static_cast<uint32_t*>(host->persist(bg3::omm::scene_capability_key,sizeof(uint32_t))));
  if(omm)bg3::omm::publish_scene_consumer(static_cast<uint32_t*>(host->persist(bg3::omm::scene_capability_key,sizeof(uint32_t))));
  const bool physical_origin=scalp && g_hair_origin_frame && bg3::hair_origin::alternate_entry(file);
  const bool tileset=std::strncmp(file,"gi/",3)==0 && bg3::native_tileset::enabled(static_cast<uint32_t*>(host->persist(bg3::native_tileset::capability_key,sizeof(uint32_t))));
  // rt_hit_material_instanced at startup (the layer's capability word): RT_HIT_INSTANCED on the consumers.
  const bool instanced=sv::hit_material_entry(file) && bg3::native_hit::capable(static_cast<uint32_t*>(host->persist(bg3::native_hit::instanced_capability_key,sizeof(uint32_t))));
  // gi_far (docs/TRACK_C_GI.md "Far-field sun bounce"): the gi_hit_sun files with RT_GI_FAR, live.
  const bool far_on=sv::gi_far_entry(file) && host->cfg_int("gi_far",0)!=0;
  // rt_hit_material_wind_rgb at startup: its clip counters (RT_HIT_WIND_RGB). Constant for the session,
  // so it shares the instanced variant slot (the request string is compared below).
  const bool wind_rgb=instanced && bg3::native_hit::capable(static_cast<uint32_t*>(host->persist(bg3::native_hit::wind_rgb_capability_key,sizeof(uint32_t))));
  const bool hair_groom = scalp && host->cfg_int("rt_hair_groom", 0) != 0;
  const unsigned variant = sv::hair_groom_slot(file, hair_groom) | (far_on?128u:0u) | (instanced?64u:0u) | (tileset?32u:0u) | (physical_origin?16u:0u) | (cascade ? 2u : 0u) | (scalp ? 4u : 0u) | (omm ? 8u : 0u);
  // rt_spec_cascade (docs/BUILD_AND_PACKAGE_EFFICIENCY.md 1, S1, qualified): one RT_SPEC_CASCADE module for both
  // cascade values; specialization constant 1000 carries `cascade` (each value keeps its own slot and pipeline).
  const bool spec_cascade = sv::cascade_entry(file);   // the only cascade path since qualification
  // The base file plus the variant's defines (common/shader_variant.h, shaders/variants.txt).
  const std::string selected = sv::request(file, (variant & 63u) | (instanced ? unsigned(sv::kHitInstanced) : 0u) |
                                                 (far_on ? unsigned(sv::kGiFar) : 0u) |
                                                 (rf_gi ? unsigned(sv::kRfHitGi) : 0u) | (wind_rgb ? unsigned(sv::kHitWindRgb) : 0u) |
                                                 (spec_cascade ? unsigned(sv::kSpecCascade) : 0u) |
                                                 (&p == &g_composite_fibre ? unsigned(sv::kHairFibreGi) : 0u));
  std::string slot_key = selected;
  if(sv::occ_stats_entry(file)) slot_key += occ_stats ? "#occ=1" : "#occ=0";
  if(sv::rf_hit_gi_entry(file)) slot_key += rf_gi ? "#rfgi=1" : "#rfgi=0";
  // Step S2a: the instanced hit-material families (RT_SPEC_HIT_FAMILY) take wind RGB as constant 1004.
  const bool hit_family = sv::hit_material_entry(file);   // S2c: RT_SPEC_HIT_INST on every hit-material file
  if (hit_family) slot_key += std::string("#wr=") + char('0' + wind_rgb);
  // A request never built in this slot compiles and creates its pipeline on a worker (common/pipeline_async.h):
  // the previous pipeline of this pass/layout runs until it is ready.
  // A built one asks the layer for its generation (in memory: no compile here; hot reload rebuilt it already)
  // and a new generation is built in the background too, the current pipeline kept meanwhile.
  const VkPipelineLayout lay = layout ? layout : g_layout;
  if (p.active_layout != lay) { p.pipeline = VK_NULL_HANDLE; p.active_layout = lay; }
  const uint32_t spec_value = sv::spec_cascade_value(file, cascade ? unsigned(sv::kCascade) : 0u);
  PipelineSpecs specs;
  if (sv::occ_stats_entry(file)) specs.add(sv::kSpecIdOccStats, occ_stats ? 1u : 0u);
  if (std::strncmp(file, "gi/rt_reflections", 17) == 0)
    // The GI-disabled specialization gained a register in the offline trial; leave it unchanged.
    specs.add(sv::kSpecIdRfCompactProbeState, rf_gi && host->cfg_int("rf_compact_probe_state", 0) != 0 ? 1u : 0u);
  if (sv::rf_hit_gi_entry(file)) specs.add(sv::kSpecIdRfHitGi, rf_gi ? 1u : 0u);
  if (spec_cascade) specs.add(sv::kSpecIdCascade, spec_value);
  if (hit_family) {
    specs.add(sv::kSpecIdHitInstanced, instanced ? 1u : 0u);
    specs.add(sv::kSpecIdWindRgb, wind_rgb ? 1u : 0u);
  }
  // Step S2b: gi_far of the gi_hit_sun files (RT_SPEC_GI_FAR); far_on is in the variant index (bit 128), so each
  // value keeps its own slot and pipeline, as the cascade constant does.
  if (sv::gi_far_entry(file)) specs.add(sv::kSpecIdGiFar, far_on ? 1u : 0u);
  if (std::strncmp(file, "gi/gi_trace_rt", 14) == 0)
    specs.add(sv::kSpecIdCompactHitState, host->cfg_int("rt_compact_hit_state", 1) != 0 ? 1u : 0u);
  if (std::strncmp(file, "gi/", 3) == 0) {
    specs.add(sv::kSpecIdTileset, tileset ? 1u : 0u);
    specs.add(sv::kSpecIdScalpHair, scalp ? 1u : 0u);
    if (bg3::hair_origin::alternate_entry(file)) specs.add(sv::kSpecIdHairOrigin, physical_origin ? 1u : 0u);
    // rt_hair_groom (docs/RTX_HAIR_INTEGRATION.md, live after a startup with it on): the strand groom in the
    // secondary queries; nothing changes until the layer publishes a groom TLAS in the scene stamp.
    specs.add(sv::kSpecIdHairGroom, hair_groom ? 1u : 0u);
    specs.add(sv::kSpecIdHairGroomShadowT, host->cfg_int("rt_hair_groom_shadow_t", 0) != 0 ? 1u : 0u);
  }
  if (std::strcmp(file, "gi/hair_fibre.hlsl") == 0 || &p == &g_composite_fibre) {   // layer's hair primary header
    const auto* hp = static_cast<const hair_primary::Persist*>(host->persist(hair_primary::kPersistKey, sizeof(hair_primary::Persist)));
    const uint64_t a = hp ? hp->header : 0;
    specs.add(hair_primary::kSpecRecordLo, uint32_t(a));
    specs.add(hair_primary::kSpecRecordHi, uint32_t(a >> 32));
  }
  slot_key += specs.key();
  Pipe::Variant& slot = p.variants[slot_key + "|" + std::to_string(uint64_t(lay))];
  const bool known = slot.pipeline && slot.file == slot_key;
  // Keep the last working specialization while a live replacement builds.
  const VkPipeline previous = bg3::pipe_async::select_pipeline(bg3::pipe_async::State::kBuilding,
      VkPipeline{}, p.pipeline);
  p.pipeline = known ? slot.pipeline : previous;
  if (known) {
    Bg3ShaderBlob blob{};
    if (!host->get_shader(selected.c_str(), "main", "cs_6_6", &blob)) {   // a failed hot reload: the built pipeline stays
      p.pipeline = slot.pipeline;
      return true;
    }
    if (slot.generation == blob.generation) {
      p.pipeline = slot.pipeline; p.generation = blob.generation;
      return true;
    }
  }
  bg3::pipe_async::Result r{};
  const std::string key = build_key(selected, "main", lay, specs);
  const auto st = g_builds.poll(key, uint64_t(lay), [req = selected, lay, specs] { return build_pipeline(req, "main", lay, specs); }, &r);
  prewarm_experimental(selected, "main", lay, specs);
  if (st == bg3::pipe_async::State::kStarted)
    host->log("gi: %s building in the background (%s)", selected.c_str(),
              p.pipeline ? "the previous pipeline meanwhile" : "the pass waits");
  if (st != bg3::pipe_async::State::kReady) {
    p.pipeline = bg3::pipe_async::select_pipeline(st, VkPipeline{}, p.pipeline);
    return p.pipeline != VK_NULL_HANDLE;
  }
  g_omm_pipelines.erase(slot.pipeline);
  if(omm)g_omm_pipelines.insert(VkPipeline(r.pipeline));
  host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)slot.pipeline);
  slot.pipeline = VkPipeline(r.pipeline);
  slot.generation = r.generation;
  slot.file = slot_key;
  p.pipeline = slot.pipeline;
  p.generation = r.generation;
  host->log("gi: %s generation %llu%s, pipeline %.1f ms", selected.c_str(), (unsigned long long)r.generation,
            spec_cascade ? (spec_value ? " spec cascade=1" : " spec cascade=0") : "",
            r.pipeline_ms);
  return true;
}

// Live developer controls can select another entry rather than another specialization.
void prewarm_pass(Pipe& p, const char* file, VkPipelineLayout layout) {
  std::lock_guard<std::recursive_mutex> lock(g_prewarm_mu);
  if (!g_prewarmed[uint64_t(layout)].insert(std::string("entry:") + file).second) return;
  const auto before = g_builds.started_total(), alternatives_before = g_prewarm_count;
  refresh(p, file, layout);
  const auto extra = g_builds.started_total() - before - (g_prewarm_count - alternatives_before);
  g_prewarm_count += extra;
  if (extra) host->log("gi: Experimental prewarm entry %s: %llu extra, %llu startup total", file,
                      (unsigned long long)extra, (unsigned long long)g_prewarm_count);
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
  uint64_t omm_identity=0;
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
  VkWriteDescriptorSet hair_origin_write{};
  VkDescriptorImageInfo hair_origin_image{};
  VkWriteDescriptorSet xw[kNumExtras];
  VkDescriptorImageInfo ximg[kNumExtras];
  VkDescriptorBufferInfo xbuf[kNumExtras];
};

bool gather_hit_gi_writes(const Bound&,VkWriteDescriptorSet*,uint32_t&,bool reference=false);

// The extras' descriptors, with fallbacks when the game did not bind a source
// (the shaders are told through Push::mat_classes bits 1-3).
void bind_extras(Bound& b, const Snapshot& s) {
  {
    b.hair_origin_image={VK_NULL_HANDLE,g_hair_origin.view,VK_IMAGE_LAYOUT_GENERAL};
    b.hair_origin_write={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};b.hair_origin_write.dstBinding=50;
    b.hair_origin_write.descriptorCount=1;b.hair_origin_write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b.hair_origin_write.pImageInfo=&b.hair_origin_image;
  }
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
      // rt_sun_replace (R32_UINT); rt_shadow.hlsl only runs with the trust image (g_sunt_ok),
      // the fallback keeps the other passes' descriptors valid.
      case 47: b.ximg[i] = {VK_NULL_HANDLE, g_sunt_ok ? g_sunt.view : g_ls_nodata.view, G}; break;
      case 48:
        b.xbuf[i] = s.l_cam2.buffer ? VkDescriptorBufferInfo{s.l_cam2.buffer, s.l_cam2.offset, (std::min)(s.l_cam2.range, VkDeviceSize(65536))}
                                    : VkDescriptorBufferInfo{s.cam.buffer, s.cam.offset, (std::min)(s.cam.range, VkDeviceSize(65536))};
        break;
      case 49: b.ximg[i] = {VK_NULL_HANDLE, s.alb.view, s.alb.layout}; break;
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
    if ((mask != 4u || ls_uses(i)) && bg3::hair_origin::binding_enabled(g_hair_origin_cap,mask,i)) out[n++] = b.w[i];
  for (uint32_t i = 0; i < kNumExtras; ++i)
    if (kExtras[i].layouts & mask) out[n++] = b.xw[i];
  if(mask!=1u)out[n++]=b.hair_origin_write;
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
// [118..120] guards, always counted (rt_common.hlsli guard_count): reads of the game's
// light list / of set 1 out of range, and rays not traced because their origin,
// direction or extent was invalid (NaN, Inf, zero direction); logged as "[core] guard:".
// [121] loop caps hit: a froxel list over 1024 entries or a ray with over 4096 candidates.
// [122..126] rt_primary_fade_class (gi/pan_primary_fade.hlsl): hits on camera-fade draws, in front
// inside / outside the cut envelope, inside but not in front, frames with a capsule. [127] hit material
// lookups of status 1 (base) on an instance without any material record (native_hit_material.hlsli
// hit_material_count). [134..135] GI hit direct supported but zero: no positive light term / all occluded.
// [253..262] hit material reasons, counted by every consumer under its stats key: rt_hit_material.hlsl
// and gi_trace_rt (rt_stats), rf_hit_shade (rf_stats; run_post's push has rt_stats 0).
// [128..133] ray queries aborted by the candidate cap with their answer unknown, per pass
// (rt_common.hlsli kStatUnknown + g_rt_pass): other (origins, debug), sun, D-LS, D-RF, RT AO,
// hybrid GI; always counted like the guards and logged in the "[core] guard:" lines. Each
// such query fell back to the game's own term (or counted as occluded): not a leak.
// 136..152: primary pool/soup cohorts (HEAD); box diagnostics 200..252 (shifted +64 from the box branch).
// [289..303] rf_hit_shade (rt_reflections.hlsl RF_HIT_SHADE, rf_stats=1): tier (b) hidden hits
// asked / shaded / unknown material / unsupported lighting / local probes listed / sum L_b; the
// tier (a) check: complete / unsupported / sums L_b, L_a, |L_b - L_a| and the L_b terms direct,
// sun, ambient; clipped values. Luminance x 8192 (fxh, clamped at 8). Logged every 300 frames ("rf hit shade").
// [160..171] GI / [176..187] rf_hit_shade light diagnostics (native_hit_direct.hlsli g_hd_*), [191..195] rf sun.
// [304..311] the tier (a) check's per-term breakdown (rt_reflections.hlsl): L_b direct from lights at the
// game's visibility, ambient diffuse, ambient specular, L_b at AO 1, the hit AO A; the screen's AO at
// the hit's pixel; L_a diffuse part, L_a specular part.
// [312..319] hit light policy / AO (native_hit_direct.hlsli g_hd_pbase): GI 312.., rf_hit_shade 316..:
// lights left at the game's visibility, traced with a per-light R, hits with an AO estimate, sum A x 8192.
// [320..328] gi_hit_sun (rt/native_hit_sun.hlsli NATIVE_HIT_SUN_STAT_BASE 320, gi_trace_rt.hlsl): sun
// evaluated, zero incident, N.L <= 0, traced lit, traced occluded, roof-unsupported, unknown
// visibility, other unsupported (sun inputs missing, normal domain, invalid), added luminance x 8192.
// [329..330] rf_hit_shade's sun (native_hit_sun.hlsli NATIVE_HIT_SUN_XBASE 329): roof-unsupported, unknown visibility.
constexpr uint32_t kRfHitFirst = 289, kRfHitCount = 23;
constexpr uint32_t kHitPolicyFirst = kRfHitFirst + kRfHitCount, kHitPolicyCount = 8;
constexpr double kRfHitScale = 8192.0;   // fxh in rt_reflections.hlsl
static_assert(bg3::transport::NtsStatsCount == kRfHitFirst, "rf hit shade counters follow the tileset ones");
constexpr uint32_t kGiSunFirst = kHitPolicyFirst + kHitPolicyCount, kGiSunCount = 11;
static_assert(kGiSunFirst == 320, "native_hit_sun.hlsli NATIVE_HIT_SUN_STAT_BASE 320 (gi_trace_rt.hlsl RT_HIT_GI_SUN)");
// [331..349] rt_hit_emission (rt/native_hit_emission.hlsli kHitEmStatFirst): per domain (GI hidden hits,
// GI screen hits, D-RF tier b, D-RF tier a) lookups / emissive hits used / skipped by the record policy /
// added luminance x 1024 (clamped at 64); excluded-family hits, unvalidated-family hits, clamped values.
constexpr uint32_t kHitEmFirst = kGiSunFirst + kGiSunCount, kHitEmCount = 19;
static_assert(kHitEmFirst == 331, "native_hit_emission.hlsli kHitEmStatFirst 331");
// [350..355] D-LS exact-k (ls_k, ls_stats=1; common/ls_exact_k.h kLkStatFirst): candidates, px over the cap,
// remainder lights, remainder rays, their occluded ones, the remainder's weight share on capped px (x256).
constexpr uint32_t kLsKFirst = kHitEmFirst + kHitEmCount, kLsKCount = bg3::ls_exact_k::kLkStatCount;
static_assert(kLsKFirst == bg3::ls_exact_k::kLkStatFirst, "common/ls_exact_k.h kLkStatFirst 350");
// [356..371] rt_hit_material_instanced clip check per family (native_hit_material.hlsli hit_material_count,
// RT_HIT_INSTANCED variants; wind 356.., alpha-test 2S 364..): checked, bit-exact, within the derived
// tolerance, UV differs, beyond x1-16, x16-256, over x256, rigid offset. Logged as "hit material instanced clip".
constexpr uint32_t kHitClipFirst = kLsKFirst + kLsKCount, kHitClipCount = 16;
static_assert(kHitClipFirst == 356, "native_hit_material.hlsli hit_material_count base 356");
// [372..382] gi_far (gi_trace_rt.hlsl RT_GI_FAR kGiFarStatFirst; per 8x8 group): rays; beyond the base radius
// screen hits, qualified sun-lit hits, of which outside the froxel volume, hits kept as native ambient;
// x 8192: occlusion sum, the old rule's on the same rays, bounce luminance sum, the old rule's, ambient
// kept at qualified hits (w x rho); qualified sun-lit hits beyond the base radius left out by the roof rule.
// Logged as "GI far".
constexpr uint32_t kGiFarFirst = kHitClipFirst + kHitClipCount, kGiFarCount = 11;
static_assert(kGiFarFirst == 372, "gi_trace_rt.hlsl kGiFarStatFirst 372");
// [383..384] rf_hit_shade bias breakdown (rt_reflections.hlsl NATIVE_HIT_BOX_TERM): L_b's projector (box) part
// x 8192 at checked tier (a) hits / at shaded tier (b) hits (logged with the rf hit shade window).
constexpr uint32_t kRfBoxFirst = kGiFarFirst + kGiFarCount, kRfBoxCount = 2;
static_assert(kRfBoxFirst == 383, "rt_reflections.hlsl stat_addv(383/384) rf hit shade box part");
// [385..416] D-LS box lights (ls_box, ls_stats=1; rt_local_shadows.hlsl LS_BOX && LS_FULL, kBx*): counted in a
// device-local buffer (set 4 b0 of the box layout), reduced per 8x8 group, copied into this range of the
// host slot after the pass's end timestamp. Logged as "ls box trace" and "ls box bound".
constexpr uint32_t kLsBoxXFirst = kRfBoxFirst + kRfBoxCount;
static_assert(kLsBoxXFirst == 385, "rt_local_shadows.hlsl box counters copied to rtStats 385..416");
// [417..418] qualified-hit GI non-finite guard (gi_trace_rt.hlsl kGiHitNfFirst, RT_HIT_GI_DIRECT variants):
// rays whose hit term was not finite and >= 0 (they took fallback0), pixels whose mean bounce exceeded the
// RGBA16F maximum (stored as 65504). On the "GI hit direct" line (frame_replay prints the same).
constexpr uint32_t kGiHitNfFirst = kLsBoxXFirst + kLsBoxXCount, kGiHitNfCount = 2;
static_assert(kGiHitNfFirst == 417, "gi_trace_rt.hlsl kGiHitNfFirst 417");
// [419..426] rf_hit_gi (rt_reflections.hlsl RF_HIT_GI, rf_stats=1): counted in a device-local buffer (set 6
// b0, ensure_rg), reduced per 8x8 group, copied into this range after the D-RF end timestamp. Tier (a)
// checks, their GI term, the screen's GI term at the same pixels, checks shaded from the screen's GI
// (rf_hit_gi=2), the GI term of shaded tier (b) hits (x 8192), AO-ray hits, with a screen / cache source.
// Logged with the rf hit shade window as "rf hit gi".
constexpr uint32_t kRfGiFirst = kGiHitNfFirst + kGiHitNfCount;
static_assert(kRfGiFirst == 419, "rt_reflections.hlsl rf_hit_gi counters copied to rtStats 419..426");
// [427..434] rt_hit_material_wind_rgb: the clip check of the third instanced family (wind RGB 2S translucency,
// native_hit_material.hlsli hit_material_count family 3, RT_HIT_INSTANCED variants), as the wind/2S ranges above:
// checked, bit-exact, within a tolerance (always 0: no tolerance is derived), UV differs, beyond x1-16, x16-256,
// over x256, rigid offset. Logged as "hit material instanced clip wind RGB 2S".
constexpr uint32_t kHitClip3First = kRfGiFirst + kRfGiCount, kHitClip3Count = 8;
static_assert(kHitClip3First == 427, "native_hit_material.hlsli hit_material_count family 3 base 427");
// [435..442] Retired IP grass counters remain reserved.
constexpr uint32_t kRetiredIpGrassFirst = kHitClip3First + kHitClip3Count, kRetiredIpGrassCount = 8;
// [443..447] OMM same-ray check (rt/omm_scene_guard.hlsli kOmmCheckFirst, RT_OMM_SCENE variants, rt_stats=1):
// visibility / closest-hit queries compared in the accelerated and the reference scene (both answers known),
// queries not compared (accelerated scene not current, or an unknown answer), violations, candidates of the
// compared queries in the accelerated / reference trace. Logged as "omm check".
constexpr uint32_t kOmmCheckFirst = kRetiredIpGrassFirst + kRetiredIpGrassCount, kOmmCheckCount = 5;
static_assert(kOmmCheckFirst == 443, "omm_scene_guard.hlsli kOmmCheckFirst 443");
// rtStats counter map (64 bins x uint each; a new range starts at the previous kStatsCounters and moves it):
//   0..15 core RT/GI (pixels, primary check, hybrid GI tiers)  16..38 D-LS      39..47 rt_shadow materials
//   48..63 D-RF                 64..75 RT audit fixes          80..111 alpha candidates   112..117 ls_check
//   118..121 guards / loop caps 122..126 primary fade class    127 material w/o record    128..133 unknown per pass
//   134..135 GI hit direct zero 136..152 pool/soup cohorts     160..175 GI hit lights      176..199 rf hit lights / sun
//   200..252 box diagnostics    253..268 hit material reasons  269..281 reference           282..286 GI hit direct
//   287..288 tileset            289..311 rf hit shade          312..319 hit policy / AO     320..330 GI hit sun, rf sun roof
//   331..349 hit emission       350..355 ls exact-k            356..371 instanced clip      372..382 gi_far
//   383..384 rf box part        385..416 ls box (device-local) 417..418 GI hit non-finite
//   419..426 rf hit gi (device-local)          427..434 instanced clip wind RGB 2S   435..442 reserved
//   443..447 omm same-ray check
//   (448..511: frame_replay HM_BLOCKERS only, never in game)
constexpr uint32_t kStatsCounters = kOmmCheckFirst + kOmmCheckCount;
static_assert(kStatsCounters <= 512, "frame_replay reserves 512 stats counters");
static_assert(kStatsCounters <= 448, "frame_replay's HM_BLOCKERS slots start at counter 448");
static_assert(kHitPolicyFirst == 312, "native_hit_direct.hlsli g_hd_pbase 312 (GI) / 316 (rf_hit_shade)");
constexpr uint32_t kUnknownFirst = 128, kUnknownPasses = 6;
const char* const kUnknownName[kUnknownPasses] = {"other", "sun", "D-LS", "D-RF", "AO", "GI"};
uint64_t g_unk_sum[kUnknownPasses] = {}, g_unk_total[kUnknownPasses] = {};
uint64_t g_guard_sum[4] = {}, g_guard_total[4] = {};
uint32_t g_guard_frames = 0;
uint64_t g_guard_folds = 0;   // frames whose counters were read back (heartbeat)
constexpr VkDeviceSize kStatsSlot = kStatsCounters * 64 * 4;
VkBuffer g_stats_buf = VK_NULL_HANDLE;
VkDeviceMemory g_stats_mem = VK_NULL_HANDLE;
uint32_t* g_stats_map = nullptr;
uint32_t g_stats_slot = 0;
uint64_t g_stats_frame[kStatsRing] = {};
bool g_stats_scene_current[kStatsRing] = {};
uint64_t g_stats_sum[kStatsCounters] = {};
float g_ls_raw[4] = {};   // set1/b0 m0.x, m0.y, m0.w, b13[0] as last written
float g_frame_m4 = 0;     // set0/b0 _m4 (skin shaded white), raw counter 54
uint32_t g_stats_n = 0;
CameraMotionWindow g_epoch_motion;
// rf hit shade: its own 300-frame window, with the per-frame mean (L_b - L_a) of the tier (a)
// check for a frame-to-frame standard error (the live gate: mean within z x that error).
struct RfHitWindow {
  uint64_t sum[kRfHitCount] = {};
  uint64_t box[kRfBoxCount] = {};
  uint64_t gi[kRfGiCount] = {};   // rf_hit_gi (kRfGiFirst; zero without the RF_HIT_GI variant)
  double d_sum = 0, d_sq = 0;
  uint32_t d_frames = 0, frames = 0;
} g_rfh;

void rf_hit_fold(const uint32_t* c) {
  uint64_t f[kRfHitCount] = {};
  for (uint32_t i = 0; i < kRfHitCount; ++i)
    for (int b = 0; b < 64; ++b) f[i] += c[(kRfHitFirst + i) * 64 + b];
  for (uint32_t i = 0; i < kRfHitCount; ++i) g_rfh.sum[i] += f[i];
  for (uint32_t i = 0; i < kRfBoxCount; ++i)
    for (int b = 0; b < 64; ++b) g_rfh.box[i] += c[(kRfBoxFirst + i) * 64 + b];
  for (uint32_t i = 0; i < kRfGiCount; ++i)
    for (int b = 0; b < 64; ++b) g_rfh.gi[i] += c[(kRfGiFirst + i) * 64 + b];
  if (f[6]) {   // checked tier (a) hits this frame
    const double d = (double(f[8]) - double(f[9])) / kRfHitScale / double(f[6]);
    g_rfh.d_sum += d;
    g_rfh.d_sq += d * d;
    ++g_rfh.d_frames;
  }
  if (++g_rfh.frames < stats_period(300)) return;
  const uint64_t* t = g_rfh.sum;
  if (t[0] || t[6] || t[7]) {
    const double q = 1.0 / kRfHitScale, n = double(g_rfh.d_frames);
    const double mean = n > 0 ? g_rfh.d_sum / n : 0.0;
    const double se = n > 1 ? std::sqrt((std::max)(g_rfh.d_sq / n - mean * mean, 0.0) / (n - 1.0)) : 0.0;
    const double ck = t[6] ? double(t[6]) : 1.0;
    host->log("rf hit shade (%u frames): tier b hidden hits %llu, shaded %llu (%.1f%%), unknown material %llu, "
              "unsupported lighting %llu, shaded with local probes listed %llu, shaded mean luminance %.5f; "
              "tier a check: %llu hits (%llu unsupported), mean L_b %.5f, mean L_a %.5f, mean (L_b - L_a) per frame "
              "%.5f, standard error %.5f (%u frames), mean |L_b - L_a| %.5f; L_b terms direct %.5f, sun %.5f, "
              "ambient %.5f; clipped %llu",
              g_rfh.frames, (unsigned long long)t[0], (unsigned long long)t[1], t[0] ? 100.0 * t[1] / double(t[0]) : 0.0,
              (unsigned long long)t[2], (unsigned long long)t[3], (unsigned long long)t[4],
              t[1] ? t[5] * q / double(t[1]) : 0.0, (unsigned long long)t[6], (unsigned long long)t[7], t[8] * q / ck,
              t[9] * q / ck, mean, se, g_rfh.d_frames, t[10] * q / ck, t[11] * q / ck, t[12] * q / ck, t[13] * q / ck,
              (unsigned long long)t[14]);
    // Term by term (the live question: which term carries the residual). L_a cannot be split into
    // direct / sun / ambient; its diffuse / specular shares and the screen's AO there can.
    if (t[6])
      host->log("rf hit shade terms (%u frames, %llu tier a hits): L_b direct %.5f (of it lights at the game's visibility %.5f), "
                "sun %.5f, ambient diffuse %.5f, ambient specular %.5f; L_b at AO 1 %.5f; AO at the hit %.4f vs the screen's AO "
                "there %.4f; L_a diffuse %.5f, specular %.5f; ratio L_b / L_a %.3f (at AO 1 %.3f)",
                g_rfh.frames, (unsigned long long)t[6], t[11] * q / ck, t[15] * q / ck, t[12] * q / ck, t[16] * q / ck,
                t[17] * q / ck, t[18] * q / ck, t[19] * q / ck, t[20] * q / ck, t[21] * q / ck, t[22] * q / ck,
                t[9] ? double(t[8]) / double(t[9]) : 0.0, t[9] ? double(t[18]) / double(t[9]) : 0.0);
    // The projector (box) part of L_b (after its AO-on-direct factor), the bias breakdown's missing term:
    // a share of "L_b direct" above (tier a) and of the shaded mean (tier b).
    host->log("rf hit shade boxes (%u frames): tier a L_b projector lights %.5f of L_b %.5f; tier b shaded L_b projector "
              "lights %.5f of %.5f",
              g_rfh.frames, g_rfh.box[0] * q / ck, t[8] * q / ck, t[1] ? g_rfh.box[1] * q / double(t[1]) : 0.0,
              t[1] ? t[5] * q / double(t[1]) : 0.0);
    // rf_hit_gi: the hit's indirect diffuse (the GI composite's term) at tier (a) checks against the
    // screen's own at the same pixels, L_b / L_a with and without it, and the estimator's AO-ray sources.
    const uint64_t* g = g_rfh.gi;
    if (g[0] || g[4] || g[5]) {
      const double gk = g[0] ? double(g[0]) : 1.0;
      host->log("rf hit gi (%u frames, %llu tier a hits, %llu of them from the screen's GI): tier a GI term %.5f vs the "
                "screen's GI term there %.5f; ratio L_b / L_a %.3f (without the GI term %.3f); tier b shaded GI term %.5f; "
                "AO-ray hits %llu, screen source %llu (%.1f%%), cache source %llu (%.1f%%)",
                g_rfh.frames, (unsigned long long)g[0], (unsigned long long)g[3], g[1] * q / gk, g[2] * q / gk,
                t[9] ? double(t[8]) / double(t[9]) : 0.0, t[9] ? (double(t[8]) - double(g[1])) / double(t[9]) : 0.0,
                t[1] ? g[4] * q / double(t[1]) : 0.0, (unsigned long long)g[5], (unsigned long long)g[6],
                g[5] ? 100.0 * g[6] / double(g[5]) : 0.0, (unsigned long long)g[7], g[5] ? 100.0 * g[7] / double(g[5]) : 0.0);
    }
  }
  g_rfh = RfHitWindow{};
}

// rt_hit_emission: its own 300-frame window (GI counts under rt_stats, D-RF under rf_stats).
struct HitEmWindow {
  uint64_t sum[kHitEmCount] = {};
  uint32_t frames = 0;
} g_hem;

void hit_em_fold(const uint32_t* c) {
  for (uint32_t i = 0; i < kHitEmCount; ++i)
    for (int b = 0; b < 64; ++b) g_hem.sum[i] += c[(kHitEmFirst + i) * 64 + b];
  if (++g_hem.frames < stats_period(300)) return;
  const uint64_t* t = g_hem.sum;
  if (t[0] | t[4] | t[8] | t[12]) {
    auto u = [&](uint32_t i) { return (unsigned long long)t[i]; };
    const double q = 1.0 / 1024.0;
    const int pulse = host->cfg_int("rt_em_fx_pulse", 0);
    host->log("hit emission (%u frames): GI hidden hits: lookups %llu, emissive %llu, skipped %llu, added weighted luminance %.4f; "
              "GI screen hits: lookups %llu, emissive %llu, skipped %llu, added %.4f; rf tier b: lookups %llu, emissive %llu, "
              "skipped %llu, added luminance %.4f; rf tier a: lookups %llu, emissive %llu, skipped %llu, added %.4f; "
              "skipped by family: excluded %llu, approximate uncalibrated %llu; clamped %llu; capture exclusion "
              "rt_em_fx_pulse=%d (304be1c7 %s; its draws are on the [em] soup line)",
              g_hem.frames, u(0), u(1), u(2), t[3] * q, u(4), u(5), u(6), t[7] * q, u(8), u(9), u(10), t[11] * q, u(12), u(13),
              u(14), t[15] * q, u(16), u(17), u(18), pulse, pulse ? "captured" : "not captured");
  }
  g_hem = HitEmWindow{};
}

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
void stats_begin(VkCommandBuffer cmd, bool alpha_test, bool full, bool scene_current) {
  uint64_t now = host->frame_index();
  uint32_t slot = uint32_t(now % kStatsRing);
  if (g_stats_frame[slot] && g_stats_frame[slot] + kStatsRing - 1 <= now) {
    const uint32_t* c = g_stats_map + slot * (kStatsSlot / 4);
    uint64_t got[4] = {};
    for (int g = 0; g < 4; ++g)
      for (int b = 0; b < 64; ++b) got[g] += c[(118 + g) * 64 + b];
    for (int g = 0; g < 4; ++g) g_guard_sum[g] += got[g];
    for (uint32_t u = 0; u < kUnknownPasses; ++u)
      for (int b = 0; b < 64; ++b) g_unk_sum[u] += c[(kUnknownFirst + u) * 64 + b];
    // "unknown (aborted by the candidate cap) queries: sun a, D-LS b, ..." (this window / so far)
    auto unk_text = [](char* out, size_t n, const uint64_t* a, const uint64_t* b) {
      size_t k = 0;
      for (uint32_t u = 0; u < kUnknownPasses && k < n; ++u)
        k += (size_t)std::snprintf(out + k, n - k, "%s%s %llu", u ? ", " : "", kUnknownName[u],
                                   (unsigned long long)(a[u] + (b ? b[u] : 0)));
    };
    char unk[256] = {};
    // Heartbeat: the first read-back and every 3000 frames, also when 0 (the counters
    // are read). The total includes this frame's values.
    if (g_guard_folds++ % 3000 == 0) {
      unk_text(unk, sizeof unk, g_unk_total, g_unk_sum);
      host->log("[core] guard: counters read back (frame %llu, %llu frames read so far): light list %llu, set 1 %llu "
                "out-of-range reads skipped, %llu invalid rays not traced, %llu loop caps hit, in total; "
                "unknown (candidate cap) queries: %s",
                (unsigned long long)now, (unsigned long long)g_guard_folds,
                (unsigned long long)(g_guard_total[0] + g_guard_sum[0]), (unsigned long long)(g_guard_total[1] + g_guard_sum[1]),
                (unsigned long long)(g_guard_total[2] + g_guard_sum[2]), (unsigned long long)(g_guard_total[3] + g_guard_sum[3]),
                unk);
    }
    if (++g_guard_frames >= stats_period(300)) {
      bool any_unk = false;
      for (uint32_t u = 0; u < kUnknownPasses; ++u) any_unk = any_unk || g_unk_sum[u] != 0;
      if (g_guard_sum[0] || g_guard_sum[1] || g_guard_sum[2] || g_guard_sum[3] || any_unk) {
        for (int g = 0; g < 4; ++g) g_guard_total[g] += g_guard_sum[g];
        for (uint32_t u = 0; u < kUnknownPasses; ++u) g_unk_total[u] += g_unk_sum[u];
        unk_text(unk, sizeof unk, g_unk_sum, nullptr);
        host->log("[core] guard: in the last %u frames %llu reads of the game's light list and %llu set-1 reads were out "
                  "of range and skipped, %llu invalid rays (NaN/Inf/zero direction or extent) not traced, %llu loop caps "
                  "hit (%llu / %llu / %llu / %llu so far); unknown (candidate cap) queries, fallen back to the "
                  "game's term or occluded: %s",
                  g_guard_frames, (unsigned long long)g_guard_sum[0], (unsigned long long)g_guard_sum[1],
                  (unsigned long long)g_guard_sum[2], (unsigned long long)g_guard_sum[3], (unsigned long long)g_guard_total[0],
                  (unsigned long long)g_guard_total[1], (unsigned long long)g_guard_total[2], (unsigned long long)g_guard_total[3],
                  unk);
      }
      g_guard_sum[0] = g_guard_sum[1] = g_guard_sum[2] = g_guard_sum[3] = 0;
      for (uint32_t u = 0; u < kUnknownPasses; ++u) g_unk_sum[u] = 0;
      g_guard_frames = 0;
    }
    for (int i = 0; i < int(kStatsCounters); ++i)
      if ((i < 27 || i > 30) && i != 70 && (i < 144 || i > 150) && (i < 155 || i > 157) && i < int(kOmmCheckFirst))
        for (int b = 0; b < 64; ++b) g_stats_sum[i] += c[i * 64 + b];
    bg3::omm::fold_check_stats(g_stats_sum + kOmmCheckFirst, c + kOmmCheckFirst * 64, g_stats_scene_current[slot]);
    for (int i = 27; i <= 30; ++i) std::memcpy(&g_ls_raw[i - 27], &c[i * 64], 4);   // raw floats (latest)
    rf_hit_fold(c);
    hit_em_fold(c);
    std::memcpy(&g_frame_m4, &c[70 * 64], 4);
    if (c[152 * 64]) {
      float pose[7];
      float origin[3];
      for (int i = 0; i < 7; ++i) std::memcpy(pose+i,&c[(144+i)*64],4);
      for (int i = 0; i < 3; ++i) std::memcpy(origin+i,&c[(155+i)*64],4);
      g_epoch_motion.add(pose,c[151*64] != 0,origin);
    } else { g_epoch_motion.have_previous = false; g_epoch_motion.have_origin = false; }
    ++g_stats_n;
  }
  g_stats_slot = slot;
  g_stats_frame[slot] = now;
  g_stats_scene_current[slot] = scene_current;
  host->vk->CmdFillBuffer(cmd, g_stats_buf, VkDeviceSize(slot) * kStatsSlot, kStatsSlot, 0);
  memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  if (!full) {   // only the guards are wanted: the statistics report stays off
    g_epoch_motion.reset_window();
    g_stats_n = 0;
    std::memset(g_stats_sum, 0, sizeof g_stats_sum);
    return;
  }
  if (g_stats_n < stats_period(120)) return;
  const uint64_t* t = g_stats_sum;
  if(g_hair_origin_cap && t[188])host->log("[hair origin] %u frames: matched virtual Hair pixels %llu, physical origin available %llu, unresolved %llu",g_stats_n,(unsigned long long)t[188],(unsigned long long)t[189],(unsigned long long)t[190]);
  if (t[0]) {
    double px = double(t[0]);
    host->log("rt stats (%u frames, alpha test %s): primary-ray check match %.3f%%, in front %.3f%% (opaque geometry "
              "%.3f, vt cutouts %.3f, tex cutouts %.3f), behind %.3f%% (through vt %.3f, tex %.3f), miss %.3f%%",
              g_stats_n, alpha_test ? "on" : "off", 100.0 * t[1] / px, 100.0 * t[2] / px, 100.0 * t[5] / px,
              100.0 * t[6] / px, 100.0 * t[7] / px, 100.0 * t[3] / px, 100.0 * t[13] / px, 100.0 * t[14] / px,
              100.0 * t[4] / px);
  }
  if (t[0] && t[126]) {   // gi/pan_primary_fade.hlsl (rt_primary_fade_class): diagnostics only
    // A TLAS hit in front of the raster on a draw whose camera-fade cut is enabled, inside the
    // capsule's cut envelope, is the wall the game cut for the camera only: a separate class.
    const double px = double(t[0]), rest = double(t[0] > t[123] ? t[0] - t[123] : 1);
    host->log("rt fade class (%u frames, capsule in %llu): camera-fade draws hit by %.3f%% of px; in front %.3f%% "
              "inside the cut envelope (faded occluder, expected), %.3f%% outside; inside the envelope but not in "
              "front %.3f%%; excluding faded occluders: primary match %.3f%%, in front %.3f%%, behind %.3f%%, miss %.3f%%",
              g_stats_n, (unsigned long long)t[126], 100.0 * t[122] / px, 100.0 * t[123] / px, 100.0 * t[124] / px,
              100.0 * t[125] / px, 100.0 * t[1] / rest, 100.0 * (t[2] - (std::min)(t[2], t[123])) / rest,
              100.0 * t[3] / rest, 100.0 * t[4] / rest);
  }
  if (g_epoch_motion.frames) {
    constexpr double to_deg = 57.29577951308232;
    const double steps = double(g_epoch_motion.steps ? g_epoch_motion.steps : 1);
    host->log("rt epoch (%u frames): GPU camera frames %u, scene snapshots %u; camera motion mean %.6f deg/frame, "
              "max %.6f deg/frame, signed yaw %.6f deg/frame; scene rotation max %.6f deg, translation max %.6f m; "
              "pool hit match %.3f%% of %llu, soup hit match %.3f%% of %llu; camera translation motion max %.6f m/frame, "
              "precision bound %.6f m/frame; lighting forward [%.9g, %.9g, %.9g], origin [%.9g, %.9g, %.9g]; "
              "misses remain in the global primary check",
              g_stats_n,g_epoch_motion.frames,g_epoch_motion.scene_frames,g_epoch_motion.angle_sum*to_deg/steps,
              g_epoch_motion.angle_max*to_deg,g_epoch_motion.yaw_sum*to_deg/steps,
              g_epoch_motion.scene_angle_max*to_deg,g_epoch_motion.translation_max,
              t[136] ? 100.0*t[137]/double(t[136]) : 0.0,(unsigned long long)t[136],
              t[140] ? 100.0*t[141]/double(t[140]) : 0.0,(unsigned long long)t[140],
              g_epoch_motion.origin_motion_max,g_epoch_motion.origin_precision_bound,
              g_epoch_motion.previous[0],g_epoch_motion.previous[1],g_epoch_motion.previous[2],
              g_epoch_motion.previous_origin[0],g_epoch_motion.previous_origin[1],g_epoch_motion.previous_origin[2]);
    // INN_ROTATION_MATCH next test: front/behind split by pool (static) and soup (per-draw) hits.
    host->log("rt epoch split (%u frames): pool hits %llu, in front %.3f%%, behind %.3f%%; soup hits %llu, "
              "in front %.3f%%, behind %.3f%% (shares of each class's hits)",
              g_stats_n,(unsigned long long)t[136],t[136] ? 100.0*t[138]/double(t[136]) : 0.0,
              t[136] ? 100.0*t[139]/double(t[136]) : 0.0,(unsigned long long)t[140],
              t[140] ? 100.0*t[142]/double(t[140]) : 0.0,t[140] ? 100.0*t[143]/double(t[140]) : 0.0);
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
  if(bg3::native_tileset::requested(host->cfg_int("rt_native_tileset",0)!=0) && host->cfg_int("rt_hit_material",0))
    host->log("hit material tileset (%u frames): qualified family2 %llu; native primary matches %llu",g_stats_n,
      (unsigned long long)t[bg3::transport::NtsStatQualified],(unsigned long long)t[bg3::transport::NtsStatPrimaryMatch]);
  if(t[282])host->log("GI hit direct (%u frames): candidates %llu qualified %llu direct supported %llu nonzero %llu; added weighted luminance sum %.7f; fallback0 ambient occlusion unchanged; supported zero: no positive light term %llu, all occluded %llu; non-finite hit terms (fallback0) %llu, bounce over the half-float max (stored 65504) %llu px",g_stats_n,t[282],t[283],t[284],t[285],double(t[286])/8192.0,t[134],t[135],t[kGiHitNfFirst],t[kGiHitNfFirst+1]);
  // gi_hit_sun: the sun at the same hits (native_hit_sun.hlsli, counters 320-328). Evaluated = qualified
  // hits with a supported point/spot term whose sun inputs are bound; the added luminance is the
  // sun's share of the GI bounce sum (x the ray fade w, as the direct sum above).
  if(t[kGiSunFirst]|t[kGiSunFirst+7])host->log("GI hit sun (%u frames): evaluated %llu, zero sun colour x clouds x b13 %llu, N.L <= 0 %llu, traced lit %llu, traced occluded %llu, "
    "roof-unsupported (no cut-away roofs in the TLAS) %llu, unknown visibility %llu, other unsupported %llu; added weighted luminance sum %.7f",
    g_stats_n,t[kGiSunFirst],t[kGiSunFirst+1],t[kGiSunFirst+2],t[kGiSunFirst+3],t[kGiSunFirst+4],t[kGiSunFirst+5],t[kGiSunFirst+6],t[kGiSunFirst+7],
    double(t[kGiSunFirst+8])/8192.0);
  // gi_far: ON (this frame's rule) and the old rule on the same rays, as means per ray: the ambient
  // visibility the AO fold gets (1 - occlusion) and the bounce luminance (before the composite's albedo).
  if(t[kGiFarFirst]){const double n=double(t[kGiFarFirst]),q=8192.0*n;const unsigned f=kGiFarFirst;
    host->log("GI far (%u frames): rays %llu; beyond the base radius: screen hits %llu, qualified sun-lit %llu (outside the froxels %llu), "
      "kept as native ambient %llu (of them roof rule %llu); per ray: ambient visibility %.5f (old rule %.5f), bounce %.7f (old rule %.7f), ambient kept at qualified hits %.5f",
      g_stats_n,t[f],t[f+1],t[f+2],t[f+3],t[f+4],t[f+10],1.0-double(t[f+5])/q,1.0-double(t[f+6])/q,double(t[f+7])/q,double(t[f+8])/q,double(t[f+9])/q);}
  // native_hit_direct.hlsli diagnostics: why a hit's point/spot term is zero or unsupported.
  // x: the 4 more counters (g_hd_xbase): blocker within 1 m of the light, rays to lights of
  // radius >= 10 m, projector entries listed / evaluated (rf_hit_shade; gi_hit_direct skips them).
  auto hd_log=[&](const char* who,unsigned b,unsigned x){
    if(!(t[b+6]|t[b+9]|t[b+10]|t[b+11]|t[b+5]|t[x+2]))return;
    host->log("%s lights (%u frames): light rays traced %llu, lit %llu, occluded %llu (first hit within 1 cm %llu, on the hit's own triangle %llu, "
              "within 1 m of the light %llu), unknown %llu; rays to lights of radius >= 10 m %llu; "
              "froxel lists no point/spot %llu; lights evaluated %llu, zero before the cosine (channel/range/cone) %llu, N.L <= 0 %llu; "
              "projector entries %llu, evaluated %llu, skipped %llu; "
              "unsupported: source (frustum/froxel slice/index) %llu, projector %llu, other %llu",who,g_stats_n,t[b],t[b+1],t[b]-t[b+1]-t[b+4],t[b+2],t[b+3],
              t[x],t[b+4],t[x+1],t[b+5],t[b+6],t[b+7],t[b+8],t[x+2],t[x+3],t[x+2]-t[x+3],t[b+9],t[b+10],t[b+11]);};
  hd_log("GI hit direct",160u,172u);hd_log("rf hit shade",176u,196u);
  // The hit light policy and AO (hit_ls_policy, hit_ao_rays; native_hit_direct.hlsli g_hd_pbase).
  for(unsigned k=0;k<2;++k){const unsigned p=kHitPolicyFirst+4u*k;
    if(t[p]|t[p+1]|t[p+2])host->log("%s policy (%u frames): lights at the game's visibility (unshadowed, as D-LS leaves them) %llu, "
      "traced with a per-light radius %llu; AO estimates %llu, mean AO %.4f",k?"rf hit shade":"GI hit direct",g_stats_n,
      t[p],t[p+1],t[p+2],t[p+2]?double(t[p+3])/8192.0/double(t[p+2]):0.0);}
  if(t[191])host->log("rf hit shade sun (%u frames): evaluated %llu, zero sun colour x clouds x b13 %llu, N.L <= 0 %llu, traced lit %llu, traced occluded %llu, "
                      "roof-unsupported (no cut-away roofs in the TLAS) %llu, unknown visibility %llu",
                      g_stats_n,t[191],t[192],t[193],t[194],t[195],t[kGiSunFirst+9],t[kGiSunFirst+10]);
  uint64_t material_total=0;for(unsigned i=253;i<263;++i)material_total+=t[i];
  if(material_total)host->log("hit material (%u frames): attempted %llu qualified %llu unsupported %llu; reasons base/epoch/row/clip/geometry/sampling/fields/frame/parameters %llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu; of base, instance without a material record %llu",
    g_stats_n,material_total,t[253],material_total-t[253],t[254],t[255],t[256],t[257],t[258],t[259],t[260],t[261],t[262],t[127]);
  // The instanced rows' clip check per family; rejected = UV differs + beyond the tolerance (reason clip).
  if(material_total && (t[kHitClipFirst]|t[kHitClipFirst+8])){
    const uint64_t* w=t+kHitClipFirst;const uint64_t* a=t+kHitClipFirst+8;
    const uint64_t rejected=w[3]+w[4]+w[5]+w[6]+a[3]+a[4]+a[5]+a[6];
    host->log("hit material instanced clip (%u frames): wind: checked %llu, bit-exact %llu, within the derived tolerance %llu, UV differs %llu, beyond x1-16 %llu, "
              "x16-256 %llu, over x256 %llu, rigid offset %llu; alpha-test 2S: checked %llu, bit-exact %llu, UV differs %llu, differs %llu, rigid offset %llu; "
              "rejected %llu = %.4f%% of all lookups",g_stats_n,w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7],a[0],a[1],a[3],a[4]+a[5]+a[6],a[7],
              rejected,100.0*double(rejected)/double(material_total));
  }
  // OMM same-ray check: violations must be 0; the candidate ratio is the traversal work the micromaps removed.
  if(t[kOmmCheckFirst]|t[kOmmCheckFirst+1]){const uint64_t* o=t+kOmmCheckFirst;
    host->log("omm check (%u frames): compared %llu, not compared (accelerated scene not current or an unknown answer) %llu; "
              "violations %llu; candidates accelerated %llu, reference %llu (%.4f%% fewer); rt_omm_reference %d",
              g_stats_n,o[0],o[1],o[2],o[3],o[4],o[4]?100.0*(double(o[4])-double(o[3]))/double(o[4]):0.0,host->cfg_int("rt_omm_reference",0));}
  if(material_total && t[kHitClip3First]){
    const uint64_t* r=t+kHitClip3First;const uint64_t rejected=r[3]+r[4]+r[5]+r[6];
    host->log("hit material instanced clip wind RGB 2S (%u frames): checked %llu, bit-exact %llu, UV differs %llu, beyond x1-16 %llu, x16-256 %llu, over x256 %llu, "
              "rigid offset %llu; rejected %llu = %.4f%% of all lookups",g_stats_n,r[0],r[1],r[3],r[4],r[5],r[6],r[7],rejected,100.0*double(rejected)/double(material_total));
  }
  if(t[263])host->log("hit material native compare (%u frames): matched class0 %llu; mean raw albedo/normal/roughness/reflectance/metalness %.7f/%.7f/%.7f/%.7f/%.7f; sum quantization1/256; cone vs raster footprint, NOT equality gate",
    g_stats_n,t[263],double(t[264])/t[263]/256.0,double(t[265])/t[263]/256.0,double(t[266])/t[263]/256.0,double(t[267])/t[263]/256.0,double(t[268])/t[263]/256.0);
  uint64_t bounce_total=0;for(unsigned i=269;i<=280;++i)bounce_total+=t[i];
  if(bounce_total)host->log("hit reference two-surface (%u frames): paths %llu depth2 complete %llu (positive indirect %llu), BSDF null %llu; incomplete primary material/direct %llu/%llu, sample/domain %llu/%llu, traversal/no secondary %llu/%llu, secondary material/direct %llu/%llu, numeric/primary mismatch %llu/%llu; direct-prefix fallback is NOT complete",
    g_stats_n,bounce_total,t[269],t[281],t[270],t[271],t[272],t[273],t[274],t[275],t[276],t[277],t[278],t[279],t[280]);
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
                100.0 * t[79] / ev, host->cfg_int("ls_owners", 0x1F), 100.0 * t[78] / ev);
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
              "luminance (mean %.4f); validation (%s): game %.4f, recomputed %.4f, mean |diff| %.5f (%.3f%% of game), recompute above game %.5f (%.3f%%; %.3f%% of px); "
              "set1/b0 m0.x %g m0.y (AO on direct) %g m0.w %g, b13[0] %g; projector lights listed %.2f/px (box active %d)",
              g_stats_n, px / n * 1e-6, t[26] / px, t[17] / px, t[18] / px, t[19] / px, t[20] / px, 100.0 * t[25] / px,
              t[21] ? 100.0 * t[24] / double(t[21]) : 0.0, t[24] / px / 256.0,
              g_ls_box_active ? "all pixels including projectors" : "px without projector lights", t[21] / px / 256.0, t[22] / px / 256.0,
              t[15] / px / 256.0, t[21] ? 100.0 * t[15] / double(t[21]) : 0.0, t[23] / px / 256.0, t[21] ? 100.0 * t[23] / double(t[21]) : 0.0, 100.0 * t[20] / px, g_ls_raw[0], g_ls_raw[1], g_ls_raw[2],
              g_ls_raw[3], t[31] / px, int(g_ls_box_active));
  }
  if (t[16] && t[kLsKFirst]) {   // D-LS exact-k (rt_local_shadows.hlsl ls_main_k; TRACK_D_LOCAL_LIGHTS.md 9.11)
    const double px = double(t[16]), hit = double(t[kLsKFirst + 1] ? t[kLsKFirst + 1] : 1),
                 rr = double(t[kLsKFirst + 3] ? t[kLsKFirst + 3] : 1);
    host->log("ls exact-k stats (%u frames): ls_k %d, ls_rest_samples %d: traced lights (ls_k=0 would trace) %.2f/px; "
              "rays %.2f/px (exact %.2f, remainder %.2f), occluded %.2f/px; over the cap (k-hit) %.1f%% of px; "
              "remainder %.2f lights/px, %.2f per capped px; remainder rays occluded %.1f%%; remainder share of the "
              "weight on capped px %.1f%%",
              g_stats_n, host->cfg_int("ls_k", 0), host->cfg_int("ls_rest_samples", 1), t[kLsKFirst] / px, t[19] / px,
              (t[19] - t[kLsKFirst + 3]) / px, t[kLsKFirst + 3] / px, t[20] / px, 100.0 * t[kLsKFirst + 1] / px,
              t[kLsKFirst + 2] / px, t[kLsKFirst + 2] / hit, 100.0 * t[kLsKFirst + 4] / rr, 100.0 * t[kLsKFirst + 5] / 256.0 / hit);
  }
  if (t[16] && (t[200] || t[208])) {
    const double px = double(t[16]);
    host->log("ls box stats (%u frames): evaluated %.3f/px, shadow-mapped %.3f/px, traced %.3f/px, "
              "occluded %.3f/px, unknown %.3f/px, cookie %.3f/px; game box light %.3f%% of lit luminance, "
              "removed %.3f%%; projector validation px %llu: game %.6f, recomputed %.6f, mean |diff| %.6f, "
              "recompute above game %.6f, box share %.3f%%; counts traced %llu occluded %llu unknown %llu cookie %llu",
              g_stats_n, t[200]/px, t[201]/px, t[202]/px, t[203]/px, t[204]/px, t[207]/px,
              t[21] ? 100.0*t[205]/double(t[21]) : 0.0, t[21] ? 100.0*t[206]/double(t[21]) : 0.0,
              (unsigned long long)t[208], t[208] ? t[209]/double(t[208])/256.0 : 0.0,
              t[208] ? t[210]/double(t[208])/256.0 : 0.0, t[208] ? t[211]/double(t[208])/256.0 : 0.0,
              t[208] ? t[212]/double(t[208])/256.0 : 0.0, t[210] ? 100.0*t[214]/double(t[210]) : 0.0,
              (unsigned long long)t[202],(unsigned long long)t[203],(unsigned long long)t[204],(unsigned long long)t[207]);
  }
  if(t[208]) {
    auto mean24=[&](uint32_t i) {
      return (double(t[i])+4294967296.0*double(t[i+1]))/16777216.0/double(t[208]);
    };
    host->log("ls box precise (%u frames): px %llu native nonzero %llu recomputed nonzero %llu invalid %llu; "
              "raw game %.9g recomputed %.9g abs_diff %.9g over %.9g box %.9g; mean quantization <= %.9g",
              g_stats_n,(unsigned long long)t[208],(unsigned long long)t[227],(unsigned long long)t[228],
              (unsigned long long)t[226],mean24(216),mean24(218),mean24(220),mean24(222),mean24(224),
              0.5/16777216.0);

  }
  {   // D-LS box lights, device-local counters (rt_local_shadows.hlsl kBx*; ls_box with ls_stats=1)
    const uint64_t* x = t + kLsBoxXFirst;
    auto q24 = [&](uint32_t i) { return (double(x[i]) + 4294967296.0 * double(x[i + 1])) / 16777216.0; };
    const double lit = t[21] ? double(t[21]) / 256.0 : 0.0;   // the game's lit luminance (stat 21, 1/256 units)
    auto pct = [](double a, double b) { return b > 0.0 ? 100.0 * a / b : 0.0; };
    if (x[5] | x[9] | x[11] | x[12])
      host->log("ls box trace (%u frames): mode %d; shadow-mapped traced %llu, occluded %llu (%.2f%%), occluded where the "
                "game's atlas term is 1 %llu, segment cut at the shadow view's near plane %llu; unshadowed traced %llu, "
                "occluded %llu (%.2f%%), receiver in the light-side fade-in (not traced) %llu; unknown %llu; removed "
                "unshadowed %.3f%%, shadow-mapped %.3f%% of the lit luminance",
                g_stats_n, g_ls_box_mode, (unsigned long long)x[5], (unsigned long long)x[6], pct(double(x[6]), double(x[5])),
                (unsigned long long)x[7], (unsigned long long)x[8], (unsigned long long)x[9], (unsigned long long)x[10],
                pct(double(x[10]), double(x[9])), (unsigned long long)x[11], (unsigned long long)x[12], pct(q24(24), lit),
                pct(q24(26), lit));
    if (x[0])
      host->log("ls box bound (%u frames): px %llu (a box light evaluated, not hair, debug 0); recompute at or above the "
                "game's stored value's upper neighbour (B10G11R11) on %llu px (%.4f%%), channels %llu of %llu; "
                "inconclusive %llu px; mean game %.9g, recompute %.9g (%.2f%% of game), box %.9g (%.2f%%), excess %.9g "
                "(Q24 half-step %.9g)",
                g_stats_n, (unsigned long long)x[0], (unsigned long long)x[1], pct(double(x[1]), double(x[0])),
                (unsigned long long)x[4], (unsigned long long)x[3], (unsigned long long)x[2], q24(16) / double(x[0]),
                q24(18) / double(x[0]), pct(q24(18), q24(16)), q24(20) / double(x[0]), pct(q24(20), q24(16)),
                q24(22) / double(x[0]), 0.5 / 16777216.0);
  }
  if(t[229]) {
    double excess=(double(t[235])+4294967296.0*double(t[236]))/16777216.0/double(t[229]);
    host->log("ls native format (%u frames): px %llu invalid %llu unbounded %llu failing %llu; "
              "bounded channels %llu violating %llu; mean excess outside UFLOAT interval %.9g (Q24 half-step %.9g)",
              g_stats_n,(unsigned long long)t[229],(unsigned long long)t[230],(unsigned long long)t[231],
              (unsigned long long)t[232],(unsigned long long)t[233],(unsigned long long)t[234],excess,
              0.5/16777216.0);
    host->log("ls native classes (%u frames): population %llu %llu %llu %llu %llu %llu %llu %llu; "
              "failing %llu %llu %llu %llu %llu %llu %llu %llu",
              g_stats_n,(unsigned long long)t[237],(unsigned long long)t[238],(unsigned long long)t[239],
              (unsigned long long)t[240],(unsigned long long)t[241],(unsigned long long)t[242],
              (unsigned long long)t[243],(unsigned long long)t[244],
              (unsigned long long)t[245],(unsigned long long)t[246],(unsigned long long)t[247],
              (unsigned long long)t[248],(unsigned long long)t[249],(unsigned long long)t[250],
              (unsigned long long)t[251],(unsigned long long)t[252]);
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
    host->log("ls validation fit (%s): game = %.4f x recomputed + %.5f over %.2fM lit px "
              "(mean recomputed %.5f, game %.5f); game where no local light reaches %.5f (%.2fM px)",
              g_ls_box_active ? "all pixels including projectors" : "px without projector lights",
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
  g_epoch_motion.reset_window();
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
  b.omm_identity=s.omm_identity;
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
  if(s.native_direct_validation)pc.ls_flags|=1u<<19;
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
  pc.rays = uint32_t(host->cfg_int("gi_rays", 4));
  pc.res_shift = g_shift;
  pc.qa_zero = uint32_t(host->cfg_int("qa_zero", 0));
  // QA: a lower candidate cap for every ray query (forces the "unknown" fallbacks; 0 = 4096).
  pc.rt_max_cand = uint32_t((std::max)(host->cfg_int("rt_max_candidates", 0), 0));
  // Half-angle of the sun's disc (rad): the game's own sun specular clamps R into
  // a cone of sin = 0.0045900648 around L (f469db78:1428), i.e. 0.263 deg.
  pc.sun_radius = host->cfg_float("rt_sun_radius", 0.00459f);
  pc.steps = uint32_t(host->cfg_int("gi_steps", 16));
  pc.debug = uint32_t(host->cfg_int("gi_debug", 0));
  pc.history = host->cfg_float("gi_history", 0.1f);
  pc.ao = host->cfg_float("gi_ao", 1.0f);
  int rr_mode = host->cfg_int("gi_rr", -1);
  if (rr_mode < 0 ? host->rr_active() : rr_mode != 0) {   // RR mode: 1 spp, no accumulation of our own
    pc.rays = uint32_t(host->cfg_int("gi_rr_rays", 1));
    pc.history = 1.0f;
  }
  pc.cs_length = host->cfg_float("cs_length", 0.02f);
  pc.cs_strength = host->cfg_float("cs_strength", 1.0f);
  pc.cs_steps = uint32_t(host->cfg_int("cs_steps", 12));
  pc.mv_scale[0] = g_mv_scale[0];
  pc.mv_scale[1] = g_mv_scale[1];
  pc.has_mv = g_mv_view && host->cfg_int("gi_reproject", 1) ? 1u : 0u;
  // QUALITY R3: the bounce source is view-independent radiance (the diffuse), not the
  // specular the camera sees (sun glints: sparse, very bright 1-spp hits that RR leaves
  // as blotches). 1 restores diffuse + specular for an A/B.
  pc.rt_bias = host->cfg_float("rt_bias", 0.002f);
  pc.rt_tmax = host->cfg_float("rt_tmax", 300.0f);
  pc.rt_debug = uint32_t(host->cfg_int("rt_debug", 0));
  pc.rt_strength = host->cfg_float("rt_strength", 1.0f);
  pc.gi_rt_radius = (std::max)(host->cfg_float("gi_rt_radius", 1.0f), 0.01f);
  // Multiplier on the derived on-screen-hit depth tolerance (rt/depth_tol.hlsli), 1 = as
  // derived. New key: an old gi_rt_depth_tol=0.03 read as k would reject nearly every hit.
  pc.gi_rt_tol = host->cfg_float("gi_rt_depth_k", 1.0f);
  pc.gi_rt_fallback = uint32_t(host->cfg_int("gi_rt_fallback", 0));
  // D-LS. Soft shadows only with RR (a denoiser for the 1-spp penumbra).
  pc.ls_source_radius = (std::max)(host->cfg_float("ls_source_radius", 0.05f), 0.0f);
  pc.ls_clear = (std::max)(host->cfg_float("ls_clear", 0.2f), 0.0f);
  pc.ls_min_radius = host->cfg_float("ls_min_radius", 0.0f);
  pc.ls_samples = uint32_t((std::min)((std::max)(host->cfg_int("ls_samples", 0), 0), 2));
  pc.ls_shadowed = uint32_t((std::min)((std::max)(host->cfg_int("ls_shadowed", 3), 0), 3));
  pc.ls_debug = uint32_t(host->cfg_int("ls_debug", 0));
  pc.ls_stats = host->cfg_int("ls_stats", 0) ? 1u : 0u;
  int soft = host->cfg_int("ls_soft", -1);   // -1 follows rr
  pc.ls_soft = (soft < 0 ? host->rr_active() : soft != 0) ? 1u : 0u;
  pc.ls_strength = host->cfg_float("ls_strength", 1.0f);
  // Per-light data (docs/SE_BRIDGE.md): bit 0 set in run_post when the join ran; bits 8-12
  // the owners whose lights are traced (bit 8 + Bg3RtLightOwner; default 0x1F, every owner:
  // the owner is no reason to skip a light, TRACK_D 9.10); bits 16-17 the art-intent rule
  // ls_noshadow (rt_local_shadows.hlsl ls_skip_light).
  pc.ls_flags = (uint32_t(host->cfg_int("ls_owners", 0x1F)) & 0x1Fu) << 8;
  pc.ls_flags |= uint32_t((std::min)((std::max)(host->cfg_int("ls_noshadow", 1), 0), 2)) << 16;
  // Bit 18: ls_giveback_cap (gi_common.hlsli): the AO-on-direct give-back bounded by the game's pixel.
  pc.ls_flags |= host->cfg_int("ls_giveback_cap", 1) ? (1u << 18) : 0u;
  // Diagnostic A/B: HPO stays active; only competing-receiver rejection changes.
  pc.ls_flags |= host->cfg_int("rt_hair_receiver_guard", 0) ? (1u << 21) : 0u;
  // Bit 24: hit_ls_policy (native_hit_direct.hlsli NATIVE_HIT_DLS_POLICY): gi_hit_direct and
  // rf_hit_shade hits follow D-LS's light policy (lights it leaves to the game unshadowed, stop
  // short by max(R, ls_clear)). Only those variants read it.
  pc.ls_flags |= host->cfg_int("hit_ls_policy", 1) ? (1u << 24) : 0u;
  // Bit 27: ls_box=2 bounded segments (shadow-view near plane, or the legacy fixture limit).
  // Bit 29 separately permits legacy unshadowed boxes in LS_BOX variants only.
  pc.ls_flags |= host->cfg_int("ls_box", 0) >= 2 ? (1u << 27) : 0u;
  // Bit 29: legacy unshadowed-box tracing for the within-run art-intent A/B only.
  pc.ls_flags |= host->cfg_int("ls_box", 0) >= 2 && !host->cfg_int("ls_box_honour_shadow", 1)
                     ? (1u << 29) : 0u;
  // Bit 28: rt_omm_reference (live A/B, rt/omm_scene_guard.hlsli): RT_OMM_SCENE variants keep the scene
  // guard but trace the reference scene (no micromaps). Only those variants read it.
  {
    static int last_omm_reference = -1;
    const int omm_reference = host->cfg_int("rt_omm_reference", 0) ? 1 : 0;
    if (omm_reference != last_omm_reference && (last_omm_reference >= 0 || omm_reference))
      host->log("gi: rt_omm_reference %d (OMM variants trace the %s scene)", omm_reference, omm_reference ? "reference" : "accelerated");
    last_omm_reference = omm_reference;
    pc.ls_flags |= omm_reference ? (1u << 28) : 0u;
  }
  // hit_ao_rays: AO rays at those hits (native_hit_ao; 0 = AO 1, the old L_b).
  pc.rt_ao_rays = uint32_t((std::min)((std::max)(host->cfg_int("hit_ao_rays", 2), 0), 8));
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
  pc.rf_depth_tol = host->cfg_float("rf_depth_k", 1.0f);   // as gi_rt_depth_k, for D-RF
  pc.rf_edge = host->cfg_float("rf_edge", 0.05f);
  pc.rf_strength = host->cfg_float("rf_strength", 1.0f);
  pc.rf_debug = uint32_t(host->cfg_int("rf_debug", 0));
  pc.rf_validate = host->cfg_int("rf_validate", 0) ? 1u : 0u;
  pc.rf_models = uint32_t(host->cfg_int("rf_models", 3)) & 255u;
  pc.rf_stats = host->cfg_int("rf_stats", 0) ? 1u : 0u;
  return pc;
}

bg3::omm::SceneBinding<VkAccelerationStructureKHR> prepare_omm_traversal(VkCommandBuffer cmd,Bound& b,VkPipeline pipeline,bool active=true){
  const bool required=active && bg3::omm::scene_capability(static_cast<uint32_t*>(host->persist(bg3::omm::scene_capability_key,sizeof(uint32_t))));
  return bg3::omm::SceneBinding<VkAccelerationStructureKHR>(b.tlas,required,[&](auto selected,auto& reference,uint64_t& identity){
    const bool fast=host->rt_omm_scene_reference && host->rt_omm_scene_reference(cmd,selected,&reference,&identity);
    if(!reference || (fast && !identity))host->log("[omm scene] traversal tuple unavailable; dispatch rejected (not a quality pass)");
    return fast && g_omm_pipelines.contains(pipeline);
  },b.omm_identity);
}

bool dispatch(VkCommandBuffer cmd, Pipe& p, Bound& b, const Push& pc, uint32_t w, uint32_t h, bool rt = false,
               VkPipelineLayout private_layout=VK_NULL_HANDLE) {
  const VkDeviceFuncs& vk = *host->vk;
  if(!p.pipeline)return false;
  auto traversal=prepare_omm_traversal(cmd,b,p.pipeline,rt);
  if(!traversal.valid())return false;
  VkDescriptorSet selected_alpha=b.alpha;
  if(&p==&g_hit_material && (!host->rt_hit_material_use || !host->rt_hit_material_use(cmd,b.tlas,b.alpha,b.omm_identity,&selected_alpha)))return false;
  VkPipelineLayout layout = private_layout?private_layout:rt ? g_rt_layout : g_layout;
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
  VkWriteDescriptorSet wr[kLsBindings + kNumExtras];
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, gather_writes(b, rt ? kRtBindings : kBindings, wr), wr);
  if (rt) vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &selected_alpha, 0, nullptr);
  vk.CmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  vk.CmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
  return true;
}

constexpr VkPipelineStageFlags kCS = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags kAll = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

// This frame's sun inputs into the next set of the ring (set 2 of the D-LS layout).
VkDescriptorSet write_sun_set(const Snapshot& s) {
  const uint64_t now = host->frame_index();
  uint32_t pick = kSunRing;
  for (uint32_t k = 0; k < kSunRing && pick == kSunRing; ++k) {
    const uint32_t i = (g_sun_slot + k) % kSunRing;
    if (!g_sun_used[i] || g_sun_used[i] + kSunMinAge <= now) pick = i;
  }
  if (pick == kSunRing) {
    if (g_sun_full++ < 5 || (g_sun_full & 1023) == 0)
      host->log("ls: sun set ring: every set was used in the last %llu frames (%llu times so far): D-LS skipped this frame",
                (unsigned long long)kSunMinAge, (unsigned long long)g_sun_full);
    return VK_NULL_HANDLE;
  }
  VkDescriptorSet set = g_sun_sets[pick];
  g_sun_used[pick] = now;
  g_sun_slot = (pick + 1) % kSunRing;
  VkDescriptorBufferInfo ubo = s.x_atmo.buffer ? VkDescriptorBufferInfo{s.x_atmo.buffer, s.x_atmo.offset, (std::min)(s.x_atmo.range, VkDeviceSize(65536))}
                                               : VkDescriptorBufferInfo{s.cam.buffer, s.cam.offset, s.cam.range};
  const Bg3Binding* src[5] = {&s.x_tlut, &s.x_ca0, &s.x_ca1, &s.x_cb0, &s.x_cb1};
  VkDescriptorImageInfo img[8];
  for (int i = 0; i < 5; ++i)
    img[i] = src[i]->view ? VkDescriptorImageInfo{VK_NULL_HANDLE, src[i]->view, src[i]->layout}
                          : VkDescriptorImageInfo{VK_NULL_HANDLE, s.alb.view, s.alb.layout};
  img[5] = {g_linear_clamp, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
  // b7: the per-light data image once a join has created and initialised it (it persists; the
  // shader checks each slot's key), else the 1x1 fallback, never read (Push::ls_flags bit 0 clear).
  g_sun_slots[pick] = g_lsl.slots_view != VK_NULL_HANDLE;
  img[6] = {VK_NULL_HANDLE, g_sun_slots[pick] ? g_lsl.slots_view : g_ls_nodata.view, VK_IMAGE_LAYOUT_GENERAL};
  // b8: this frame's trust slot (staged by sunt_begin before the GI), else the 1x1 fallback, never
  // read as roofs (Push::ls_flags bit 25 clear: the hit's sun is then unsupported, not lit).
  img[7] = {VK_NULL_HANDLE, g_sunt_ok && g_sunt.view ? g_sunt.view : g_ls_nodata.view, VK_IMAGE_LAYOUT_GENERAL};
  VkWriteDescriptorSet w[kSunBindings];
  uint32_t n = 0;
  for (uint32_t i = 0; i < kSunBindings; ++i) {
    if (i > 0 && !img[i - 1].imageView && sun_binding_type(i) == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) continue;
    w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w[n].dstSet = set;
    w[n].dstBinding = i;
    w[n].descriptorCount = 1;
    w[n].descriptorType = sun_binding_type(i);
    if (i == 0) w[n].pBufferInfo = &ubo;
    else w[n].pImageInfo = &img[i - 1];
    ++n;
  }
  host->vk->UpdateDescriptorSets(host->device, n, w, 0, nullptr);
  return set;
}

bool ls_dispatch(VkCommandBuffer cmd, Bound& b, const Push& pc, VkDescriptorSet sun, VkPipeline pipe, uint32_t gx,
                 uint32_t gy, const Snapshot* box = nullptr, bool material_use = false) {
  if(!pipe)return false;
  auto traversal=prepare_omm_traversal(cmd,b,pipe);
  if(!traversal.valid())return false;
  const VkDeviceFuncs& vk = *host->vk;
  VkDescriptorSet selected_alpha=b.alpha;
  if(material_use && (!host->rt_hit_material_use ||
     !host->rt_hit_material_use(cmd,b.tlas,b.alpha,b.omm_identity,&selected_alpha)))return false;
  const VkPipelineLayout layout = material_use ? g_hit_reference_layout : box ? g_ls_box_layout : g_ls_layout;
  if(!layout || (material_use && box))return false;
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
  VkWriteDescriptorSet w[kLsBindings + kNumExtras];
  uint32_t count=0;
  if(material_use){if(!gather_hit_gi_writes(b,w,count,true))return false;}
  else count=gather_writes(b,kLsBindings,w);
  vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,count,w);
  VkDescriptorSet sets[2] = {selected_alpha, sun};
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, material_use ? 1u : 2u, sets, 0, nullptr);
  if (box) vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 3, 1, &box->game_set1,
                                   box->game_set1_ndyn, box->game_set1_ndyn ? box->game_set1_dyn : nullptr);
  if (box) vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 4, 1, &g_bx_set, 0, nullptr);
  vk.CmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  vk.CmdDispatch(cmd, gx, gy, 1);
  return true;
}

// D-RF: push set 0 (bindings 0-30), the host's set 1, the game's lighting set 1 as set 2.
// With rf_sharc: the variant reading the radiance cache, its layout adding set 4.
bool rf_dispatch(VkCommandBuffer cmd, Bound& b, const Snapshot& s, const Push& pc) {
  if(!(s.rf_sharc?g_rf_sharc.pipeline:g_rf.pipeline))return false;
  auto traversal=prepare_omm_traversal(cmd,b,s.rf_sharc?g_rf_sharc.pipeline:g_rf.pipeline);
  if(!traversal.valid())return false;
  const VkDeviceFuncs& vk = *host->vk;
  const VkPipelineLayout layout = s.rf_sharc ? g_rf_sharc_layout : g_rf_layout;
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.rf_sharc ? g_rf_sharc.pipeline : g_rf.pipeline);
  VkWriteDescriptorSet w[kRfBindings + kNumExtras];   // bindings 0-30 and the D-RF extras (40)
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, gather_writes(b, kRfBindings, w), w);
  VkDescriptorSet sets[2] = {b.alpha, s.game_set1};
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 2, sets, s.game_set1_ndyn,
                           s.game_set1_ndyn ? s.game_set1_dyn : nullptr);
  if (s.rf_sharc) vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 4, 1, &s.sh.set, 0, nullptr);
  vk.CmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  vk.CmdDispatch(cmd, (g_w + 7) / 8, (g_h + 7) / 8, 1);
  return true;
}

// ---- rt_hit_emission (M4.4): the emissive scene set for the hit-GI / rf_hit_shade variants ----
void hit_em_drop() {
  for (Pipe* p : {&g_hit_gi_em[0], &g_hit_gi_em[1], &g_hit_gi_em[2], &g_hit_gi_em[3]}) {
    for (const auto& [slot, v] : p->variants) host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)v.pipeline);
    *p = Pipe{};
  }
  for (VkPipelineLayout* l : {&g_hit_gi_em_layout[0], &g_hit_gi_em_layout[1], &g_hit_gi_em_layout[2], &g_hit_gi_em_layout[3]}) {
    defer_layout_destroy(uint64_t(*l));
    *l = VK_NULL_HANDLE;
  }
  g_hit_em_dsl = VK_NULL_HANDLE;
  rf_gi_drop();   // its emission layouts hold g_hit_em_dsl (the others are rebuilt on demand)
}

// The host's emissive scene to bind in `cmd` when rt_hit_emission is on (needs rt_emissive=1): the latest
// build's set when it is at most 3 frames old (em_debug's rule), else VK_NULL_HANDLE (the variants
// without emission run, as with the key off). Asking keeps the scene's game images alive until `cmd` ran.
VkDescriptorSet hit_em_set(VkCommandBuffer cmd) {
  if (host->cfg_int("rt_hit_emission", 0) <= 0 || !host->rt_emissive) return VK_NULL_HANDLE;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
  Bg3EmissiveInfo info{};
  if (!host->rt_emissive(cmd, &layout, &set, &info) || !set || !layout || info.built_frame + 3 < host->frame_index())
    return VK_NULL_HANDLE;
  if (g_hit_em_dsl != layout) {   // the host's layout never changes in practice
    if (g_hit_em_dsl) host->log("hit emission: the emissive set layout changed; rebuilding its variants");
    hit_em_drop();
    g_hit_em_dsl = layout;
  }
  return set;
}

// The push fields the emission variants read (native_hit_emission.hlsli HIT_EM_FLAGS / HIT_EM_TOL), and
// the dependency on the emissive build (the host's own submission after the previous frame's), as em_debug.
void hit_em_prepare(VkCommandBuffer cmd, Push& hp) {
  hp.ls_kmode = host->cfg_int("rt_hit_emission", 0) >= 2 ? 1u : 0u;
  hp.rt_ao_radius = host->cfg_float("rt_em_tol", 0.01f);
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  host->vk->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

bool make_em_layout(VkDescriptorSetLayout* sets, uint32_t n, VkPipelineLayout& out) {
  if (out) return true;
  VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
  VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lci.setLayoutCount = n;
  lci.pSetLayouts = sets;
  lci.pushConstantRangeCount = 1;
  lci.pPushConstantRanges = &pc;
  if (host->vk->CreatePipelineLayout(host->device, &lci, nullptr, &out) != VK_SUCCESS) {
    out = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

bool ensure_empty_dsl() {
  if (g_empty_dsl) return true;
  VkDescriptorSetLayoutCreateInfo ei{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  return host->vk->CreateDescriptorSetLayout(host->device, &ei, nullptr, &g_empty_dsl) == VK_SUCCESS;
}

// rf_hit_gi: rf_hit_shade's sets 0-3, set 4 SHaRC's (or empty), set 5 the emissive scene (or empty), set 6
// the device-local counters.
bool ensure_rf_hit_gi_layout(bool sharc, bool em) {
  namespace rfh = bg3::native_hit_rf;
  static_assert(rfh::kSetPush == 0 && rfh::kSetMaterial == 1 && rfh::kSetGame == 2 && rfh::kSetSun == 3 &&
                rfh::kSetSharc == 4 && rfh::kSetEmissive == 5 && rfh::kSetRfGi == 6);
  VkPipelineLayout& out = g_rf_shade_gi_layout[(em ? 2 : 0) + (sharc ? 1 : 0)];
  if (out) return true;
  VkDescriptorSetLayout host_layout = VK_NULL_HANDLE;
  VkDescriptorSet unused;
  if ((em && !g_hit_em_dsl) || !ensure_rg() || !ensure_rf_hit_contract() || !ensure_empty_dsl() || !host->rt_alpha ||
      !host->rt_alpha(&host_layout, &unused) || !host_layout)
    return false;
  VkDescriptorSetLayout sh = sharc ? sharc_set_layout() : g_empty_dsl;
  if (!sh) return false;
  VkDescriptorSetLayout sets[7] = {g_rf_set_layout, host_layout, g_rf_game_layout, g_sun_dsl, sh, em ? g_hit_em_dsl : g_empty_dsl,
                                   g_rg_dsl};
  return make_em_layout(sets, 7, out);
}

// rf_hit_shade (M4.2 tier b): the D-RF dispatch with the material AlphaDSL (rt_hit_material_use)
// as set 1 and the sun set as set 3. False before anything is dispatched: the caller then runs the
// ordinary D-RF, so a missing input keeps today's reflections exactly.
bool rf_hit_dispatch(VkCommandBuffer cmd, Bound& b, const Snapshot& s, const Push& pc, VkDescriptorSet sun) {
  // The variant reads the box (projector) lights of set 1 b29 at the hits (native_hit_direct.hlsli
  // NATIVE_HIT_BOXES): without that buffer the ordinary D-RF runs.
  // The sun set's b8 (roof volumes, native_hit_sun.hlsli) must hold an image: the trust slot or the fallback.
  if (!sun || !s.boxes_bound || !host->rt_hit_material_use || !((g_sunt_ok && g_sunt.view) || g_ls_nodata.view) ||
      !bg3::native_hit::capable(static_cast<uint32_t*>(host->persist(bg3::native_hit::capability_key, sizeof(uint32_t)))))
    return false;
  const bool sharc = s.rf_sharc;
  // Every hit-shade module has set 6, including specialization 1013 = 0.
  VkDescriptorSet em = hit_em_set(cmd);
  auto select = [&](bool with_em) -> Pipe* {
    if(!ensure_rf_hit_gi_layout(sharc, with_em)) return nullptr;
    const unsigned gk = (with_em ? 2u : 0u) + (sharc ? 1u : 0u);
    const char* gf = with_em ? (sharc ? "gi/rt_reflections_sharc_hit_shade_em.hlsl" : "gi/rt_reflections_hit_shade_em.hlsl")
                            : (sharc ? "gi/rt_reflections_sharc_hit_shade.hlsl" : "gi/rt_reflections_hit_shade.hlsl");
    return refresh(g_rf_shade_gi[gk], gf, g_rf_shade_gi_layout[gk]) ? &g_rf_shade_gi[gk] : nullptr;
  };
  Pipe* selected = em ? select(true) : nullptr;
  if(!selected) {
    em = VK_NULL_HANDLE;
    selected = select(false);
  }
  if(!selected) return false;
  const unsigned gk = (em ? 2u : 0u) + (sharc ? 1u : 0u);
  VkPipelineLayout layout = g_rf_shade_gi_layout[gk];
  Pipe& p = *selected;
  const uint32_t gi_mode = uint32_t((std::min)((std::max)(host->cfg_int("rf_hit_gi", 0), 0), 2));
  g_rg_ran = gi_mode != 0;
  if(gi_mode && g_rg_logged != int(gi_mode * 2 + 1)) {
    g_rg_logged = int(gi_mode * 2 + 1);
    host->log("rf hit gi: mode %u %s (sharc %d, emission %d)", gi_mode,
              "active", sharc, em != VK_NULL_HANDLE);
  } else if(!gi_mode && g_rg_logged > 0) {
    g_rg_logged = 0;
    host->log("rf hit gi: off");
  }
  auto traversal = prepare_omm_traversal(cmd, b, p.pipeline);
  if (!traversal.valid()) return false;
  VkDescriptorSet material_alpha = b.alpha;
  if (!host->rt_hit_material_use(cmd, b.tlas, b.alpha, b.omm_identity, &material_alpha) || !material_alpha) return false;
  const VkDeviceFuncs& vk = *host->vk;
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
  VkWriteDescriptorSet w[kRfBindings + kNumExtras];
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, gather_writes(b, kRfBindings, w), w);
  VkDescriptorSet sets[3] = {material_alpha, s.game_set1, sun};
  // Dynamic offsets: only the game's set has dynamic bindings (as rf_dispatch).
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 3, sets, s.game_set1_ndyn,
                           s.game_set1_ndyn ? s.game_set1_dyn : nullptr);
  if (sharc) vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 4, 1, &s.sh.set, 0, nullptr);
  if (em) vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, bg3::native_hit_rf::kSetEmissive, 1, &em, 0, nullptr);
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, bg3::native_hit_rf::kSetRfGi, 1, &g_rg_set, 0, nullptr);
  // The hit light policy reads the per-light data from the sun set's b7 (ls_flags bit 0) when that
  // set holds the image and a join has run.
  Push hp = pc;
  bool slots = false;
  for (uint32_t i = 0; i < kSunRing; ++i)
    if (g_sun_sets[i] == sun) slots = g_sun_slots[i];
  hp.ls_flags = (hp.ls_flags & ~1u) | (slots && g_lsl_ok && host->cfg_int("ls_lights", 1) ? 1u : 0u);
  // Bit 25: the sun set's b8 holds this frame's trust slot (roof volumes; native_hit_sun.hlsli).
  hp.ls_flags |= g_sunt_ok && g_sunt.view ? (1u << 25) : 0u;
  if (em) hit_em_prepare(cmd, hp);
  if (g_rg_ran) {
    // Push::rf_so (no other D-RF reader since rf_so was removed) carries the mode; the GI composite's
    // bounce exists only when it runs this frame (strength 0 otherwise: no indirect diffuse at hits).
    hp.rf_so = gi_mode;
    if (!s.gi) hp.strength = 0.0f;
  }
  vk.CmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof hp, &hp);
  vk.CmdDispatch(cmd, (g_w + 7) / 8, (g_h + 7) / 8, 1);
  if (!g_hit_em_logged && host->cfg_int("rt_hit_emission", 0) > 0) {
    g_hit_em_logged = true;
    host->log("hit emission: first rf_hit_shade dispatch %s the emissive scene (sharc %d)",
              em ? "with" : "without (no fresh D-EM build: needs rt_emissive=1)", sharc);
  }
  return true;
}

// SHaRC passes: the ray-traced push set 0, the host's set 1, the cache's set 4; `gx`, `gy` groups.
bool sharc_dispatch(VkCommandBuffer cmd, Pipe& p, Bound& b, const Push& pc, VkDescriptorSet set, uint32_t gx, uint32_t gy) {
  if(!p.pipeline)return false;
  auto traversal=prepare_omm_traversal(cmd,b,p.pipeline);
  if(!traversal.valid())return false;
  const VkDeviceFuncs& vk = *host->vk;
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
  VkWriteDescriptorSet wr[kLsBindings + kNumExtras];
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_rt_sharc_layout, 0, gather_writes(b, kRtBindings, wr), wr);
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_rt_sharc_layout, 1, 1, &b.alpha, 0, nullptr);
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_rt_sharc_layout, 4, 1, &set, 0, nullptr);
  vk.CmdPushConstants(cmd, g_rt_sharc_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  vk.CmdDispatch(cmd, gx, gy, 1);
  return true;
}

bool ensure_hit_gi_layout(VkDescriptorSetLayout alpha,bool sharc,bool reference=false){
 if(reference && sharc)return false; // No reference shader/descriptor set4 contract.
 auto& set_layout=reference?g_hit_reference_set_layout:g_hit_gi_set_layout;
 auto& plain_layout=reference?g_hit_reference_layout:g_hit_gi_plain_layout;
 if(!set_layout){
  VkDescriptorSetLayoutBinding entries[bg3::native_hit_gi::bindings.size()];uint32_t n=0;
  for(const auto& b:bg3::native_hit_gi::slots(reference))if(bg3::native_hit_gi::enabled(b,g_hair_origin_cap))entries[n++]={b.binding,b.type,b.count,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
  if(n>32u)return false;
  VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};ci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;ci.bindingCount=n;ci.pBindings=entries;
  if(host->vk->CreateDescriptorSetLayout(host->device,&ci,nullptr,&set_layout)!=VK_SUCCESS)return false;
 }
 if(sharc)return make_sharc_layout(set_layout,alpha,VK_NULL_HANDLE,&g_hit_gi_layout);
 if(plain_layout)return true;
 VkDescriptorSetLayout sets[2]={set_layout,alpha};VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push)};
 VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};ci.setLayoutCount=2;ci.pSetLayouts=sets;ci.pushConstantRangeCount=1;ci.pPushConstantRanges=&push;
 return host->vk->CreatePipelineLayout(host->device,&ci,nullptr,&plain_layout)==VK_SUCCESS;
}
bool gather_hit_gi_writes(const Bound& b,VkWriteDescriptorSet* writes,uint32_t& n,bool reference){
 n=0;for(const auto& slot:bg3::native_hit_gi::slots(reference)){
  if(!bg3::native_hit_gi::enabled(slot,g_hair_origin_cap))continue;
  const VkWriteDescriptorSet* write=nullptr;
  if(slot.binding<kLsBindings)write=&b.w[slot.binding];
  else if(slot.binding==50u)write=&b.hair_origin_write;
  else for(uint32_t i=0;i<kNumExtras;++i)if(kExtras[i].binding==slot.binding)write=&b.xw[i];
  if(!write || write->dstBinding!=slot.binding || write->descriptorType!=slot.type || write->descriptorCount!=slot.count)return false;
  writes[n++]=*write;
 }
 return n==bg3::native_hit_gi::descriptor_count(g_hair_origin_cap,reference);
}

bool ensure_hit_reference_layout(){
 VkDescriptorSetLayout alpha{};VkDescriptorSet unused{};
 return host->rt_alpha && host->rt_alpha(&alpha,&unused) && alpha && ensure_hit_gi_layout(alpha,false,true);
}

// gi_hit_sun: the GI hit push set, the material AlphaDSL set 1, an empty set 2, the D-LS sun set 3
// (g_sun_dsl; common/native_hit_gi_bindings.h kSetSun), SHaRC's set 4 in the SHaRC variant.
bool ensure_hit_gi_sun_layout(VkDescriptorSetLayout alpha,bool sharc){
 VkPipelineLayout& out=sharc?g_hit_gi_sun_sharc_layout:g_hit_gi_sun_layout;
 if(out)return true;
 if(!ensure_hit_gi_layout(alpha,sharc) || !g_hit_gi_set_layout || !ensure_ls_layout() || !g_sun_dsl)return false;
 if(!g_empty_dsl){
  VkDescriptorSetLayoutCreateInfo ei{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  if(host->vk->CreateDescriptorSetLayout(host->device,&ei,nullptr,&g_empty_dsl)!=VK_SUCCESS)return false;
 }
 VkDescriptorSetLayout sh=sharc?sharc_set_layout():VK_NULL_HANDLE;
 if(sharc && !sh)return false;
 static_assert(bg3::native_hit_gi::kSetSun==3);
 VkDescriptorSetLayout sets[5]={g_hit_gi_set_layout,alpha,g_empty_dsl,g_sun_dsl,sh};
 VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push)};
 VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};ci.setLayoutCount=sharc?5u:4u;ci.pSetLayouts=sets;
 ci.pushConstantRangeCount=1;ci.pPushConstantRanges=&push;
 if(host->vk->CreatePipelineLayout(host->device,&ci,nullptr,&out)!=VK_SUCCESS){out=VK_NULL_HANDLE;return false;}
 return true;
}

// The hit GI + emission: the GI hit push set, the material AlphaDSL set 1, the emissive scene set 2
// (common/native_hit_gi_bindings.h kSetEmissive), the sun set 3 (gi_hit_sun) and SHaRC's set 4.
bool ensure_hit_gi_em_layout(VkDescriptorSetLayout alpha, bool sun, bool sharc) {
  static_assert(bg3::native_hit_gi::kSetEmissive == 2 && bg3::native_hit_gi::kSetSun == 3);
  VkPipelineLayout& out = g_hit_gi_em_layout[(sun ? 2 : 0) + (sharc ? 1 : 0)];
  if (out) return true;
  if (!g_hit_em_dsl || !ensure_hit_gi_layout(alpha, sharc) || !g_hit_gi_set_layout || !ensure_empty_dsl()) return false;
  if (sun && (!ensure_ls_layout() || !g_sun_dsl)) return false;
  VkDescriptorSetLayout sh = sharc ? sharc_set_layout() : VK_NULL_HANDLE;
  if (sharc && !sh) return false;
  VkDescriptorSetLayout sets[5] = {g_hit_gi_set_layout, alpha, g_hit_em_dsl, sun ? g_sun_dsl : g_empty_dsl, sh};
  return make_em_layout(sets, sharc ? 5u : sun ? 4u : 3u, out);
}

// replay_pack (diagnostics, tools/frame_replay.md "Live writer"): a positive serial asks the host for
// one replay pack of the next base-variant gi_hit_direct dispatch (once per serial, across core
// reloads); -1 takes one with every trace. Capture only: called after the dispatch is recorded.
// The defines of the variant `p` runs (its request string after '?', "A=1;B=2"; "" for the base file): the
// pack must compile what ran (RT_CASCADE_MASKS: GI rays trace 0x01, never the cascade instance).
std::string pipe_defines(const Pipe& p){
 if(!p.pipeline)return {};
 for(const auto& [slot,v]:p.variants)if(v.pipeline==p.pipeline){
  const std::string& f=v.file;const size_t q=f.find('?');
  return q==std::string::npos?std::string():f.substr(q+1);
 }
 return {};
}
void replay_after_hit_gi(VkCommandBuffer cmd,const VkWriteDescriptorSet* writes,uint32_t n,VkDescriptorSet set1,
                         VkAccelerationStructureKHR tlas,const Push& hp,const char* variant,const std::string& defines){
 if(!host->replay_pack)return;
 const int key=host->cfg_int("replay_pack",0);
 if(key==0 || key<-1)return;
 int* done=static_cast<int*>(host->persist("bg3rtx.replay_pack.done",sizeof(int)));
 if(key>0 && *done==key)return;
 if(variant){
  if(key>0 && std::strcmp(variant,"emission")==0)host->log("[replay] pack %d failed: gi_hit_direct runs the rt_hit_emission variant (set 2): emission variant not supported by the pack contract",key);
  else if(key>0)host->log("[replay] pack %d failed: gi_hit_direct runs the %s variant; the pack contract is the base variant (gi_hit_sun=0, sharc=0)",key,variant);
  if(key>0)*done=key;
  return;
 }
 static const uint32_t outputs[2]={8u,13u};   // giRaw, giRawDepth (common/replay_pack.h kGiHitDirect)
 Bg3ReplayDispatch d{};
 d.serial=key>0?key:0;d.pass="gi_hit_direct";d.defines=defines.c_str();d.frame=hp.frame;
 d.groups[0]=(g_hw+7)/8;d.groups[1]=(g_hh+7)/8;d.groups[2]=1;
 d.push=&hp;d.push_bytes=sizeof hp;d.set0=writes;d.set0_count=n;d.outputs=outputs;d.output_count=2;
 d.set1=set1;d.tlas=tlas;
 const int r=host->replay_pack(cmd,&d);
 if(key>0 && r!=0)*done=key;
}

// Qualified hidden-hit GI uses native local-light inputs, material set1 and
// optional SHaRC set4. gi_hit_sun adds the D-LS sun set as set 3 (this frame's write_sun_set);
// without it (ring full, layout failure) the direct-only variant runs, as gi_hit_sun=0.
bool hit_gi_dispatch(VkCommandBuffer cmd,Bound& b,const Snapshot& s,const Push& pc){
 if(!bg3::native_hit::capable(static_cast<uint32_t*>(host->persist(bg3::native_hit::capability_key,sizeof(uint32_t)))))return false;
 VkDescriptorSetLayout alpha_layout{};VkDescriptorSet unused{};
 if(!s.ls_ok || !host->rt_hit_material_use || !host->rt_alpha ||
    !host->rt_alpha(&alpha_layout,&unused) || !alpha_layout || !ensure_hit_gi_layout(alpha_layout,s.sharc))return false;
 VkDescriptorSet sun=VK_NULL_HANDLE;
 if(host->cfg_int("gi_hit_sun",0)){
  if(((g_sunt_ok && g_sunt.view) || g_ls_nodata.view) && ensure_hit_gi_sun_layout(alpha_layout,s.sharc))sun=write_sun_set(s);
  if(!sun && !g_hit_gi_sun_logged)host->log("gi: gi_hit_sun: no sun set or layout this frame; the direct-only hit GI runs");
  g_hit_gi_sun_logged=g_hit_gi_sun_logged || !sun;
 }
 // rt_hit_emission: the same variant with the emissive scene as set 2; any missing piece runs it without.
 const unsigned em_index=(sun?2u:0u)+(s.sharc?1u:0u);
 static const char* const kEmFiles[4]={"gi/gi_trace_rt_hit_direct_em.hlsl","gi/gi_trace_rt_sharc_hit_direct_em.hlsl",
                                       "gi/gi_trace_rt_hit_sun_em.hlsl","gi/gi_trace_rt_sharc_hit_sun_em.hlsl"};
 VkDescriptorSet em=hit_em_set(cmd);
 if(em && !(ensure_hit_gi_em_layout(alpha_layout,sun!=VK_NULL_HANDLE,s.sharc) && refresh(g_hit_gi_em[em_index],kEmFiles[em_index],g_hit_gi_em_layout[em_index])))em=VK_NULL_HANDLE;
 VkPipelineLayout layout=em?g_hit_gi_em_layout[em_index]:sun?(s.sharc?g_hit_gi_sun_sharc_layout:g_hit_gi_sun_layout):(s.sharc?g_hit_gi_layout:g_hit_gi_plain_layout);
 Pipe& p=em?g_hit_gi_em[em_index]:sun?(s.sharc?g_hit_gi_sun_sharc:g_hit_gi_sun):(s.sharc?g_hit_gi_sharc:g_hit_gi);
 if(!em && !refresh(p,sun?(s.sharc?"gi/gi_trace_rt_sharc_hit_sun.hlsl":"gi/gi_trace_rt_hit_sun.hlsl")
                  :(s.sharc?"gi/gi_trace_rt_sharc_hit_direct.hlsl":"gi/gi_trace_rt_hit_direct.hlsl"),layout))return false;
 auto traversal=prepare_omm_traversal(cmd,b,p.pipeline);if(!traversal.valid())return false;
 VkDescriptorSet material_alpha=b.alpha;
 if(!host->rt_hit_material_use(cmd,b.tlas,b.alpha,b.omm_identity,&material_alpha))return false;
 bind_ls(b,s);const auto& vk=*host->vk;VkWriteDescriptorSet writes[bg3::native_hit_gi::bindings.size()];uint32_t write_count=0;
 if(!gather_hit_gi_writes(b,writes,write_count))return false;
 vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);
 vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,write_count,writes);
 vk.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,1,1,&material_alpha,0,nullptr);
 if(sun)vk.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,bg3::native_hit_gi::kSetSun,1,&sun,0,nullptr);
 if(em)vk.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,bg3::native_hit_gi::kSetEmissive,1,&em,0,nullptr);
 if(s.sharc)vk.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,4,1,&s.sh.set,0,nullptr);
 // The hit light policy's per-light data (b46): the latest join's, the GI runs before this
 // frame's (native_hit_direct.hlsli checks each slot's key).
 Push hp=pc;hp.ls_flags=(hp.ls_flags&~1u)|(g_lsl_ok && host->cfg_int("ls_lights",1)?1u:0u);
 // gi_far (the sun files' RT_GI_FAR variant, refresh): rays of gi_far_m in Push::radius (gi_trace's march
 // radius, unused by this pass); gi_rt_radius stays the base radius. Without the sun set it is not run.
 const bool far_on=sun && host->cfg_int("gi_far",0)!=0;
 if(far_on)hp.radius=(std::min)((std::max)(host->cfg_float("gi_far_m",6.0f),0.01f),64.0f);
 if(far_on!=g_gi_far_logged){g_gi_far_logged=far_on;
  host->log("gi: gi_far %s (rays %.1f m, base radius %.1f m, sharc %d)",far_on?"on":"off",far_on?hp.radius:pc.gi_rt_radius,pc.gi_rt_radius,s.sharc);}
 if(!sun && host->cfg_int("gi_far",0) && !g_gi_far_nosun_logged){g_gi_far_nosun_logged=true;
  host->log("gi: gi_far needs gi_hit_sun and the sun set; the base hit GI runs");}
 // gi_hit_sun: bit 25, the sun set b8 holds this frame's trust slot (header and roof volumes).
 if(sun)hp.ls_flags|=g_sunt_ok && g_sunt.view?(1u<<25):0u;
 if(em)hit_em_prepare(cmd,hp);
 vk.CmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof hp,&hp);
 vk.CmdDispatch(cmd,(g_hw+7)/8,(g_hh+7)/8,1);
 if(!g_hit_em_logged && host->cfg_int("rt_hit_emission",0)>0){
  g_hit_em_logged=true;
  host->log("hit emission: first hit-GI dispatch %s the emissive scene (sun %d, sharc %d)",
            em?"with":"without (no fresh D-EM build: needs rt_emissive=1)",sun!=VK_NULL_HANDLE,s.sharc);
 }
 // The pack contract is the base variant: an emission dispatch (extra set 2) is refused, not packed.
 replay_after_hit_gi(cmd,writes,write_count,material_alpha,b.tlas,hp,em?"emission":sun?"gi_hit_sun":s.sharc?"SHaRC":nullptr,
                     host->replay_pack && host->cfg_int("replay_pack",0)!=0?pipe_defines(p):std::string());
 return true;
}

// D-RF pass alone, 2 timestamps per frame, medians over 300 frames.
VkQueryPool g_rf_queries = VK_NULL_HANDLE;
uint32_t g_rf_slot = 0;
bool g_rf_written[kRing] = {};
std::vector<float> g_rf_ms;

void rf_timestamp(VkCommandBuffer cmd, uint32_t index) {
  if (!bg3::logging::stats_enabled(host->cfg_int)) return;
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
  if (!bg3::logging::stats_enabled(host->cfg_int)) return;
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
  if (g_rf_ms.size() < stats_period(300)) return;
  auto& v = g_rf_ms;
  std::sort(v.begin(), v.end());
  int rf_rr = host->cfg_int("rf_rr", -1);
  bool noisy = rf_rr < 0 ? host->rr_active() : rf_rr != 0;
  log_stats("rf: reflection pass GPU median %.3f ms, p10 %.3f, p90 %.3f (%zu frames; %s, max roughness %.2f, debug %d, "
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
  if (!bg3::logging::stats_enabled(host->cfg_int)) return;
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
  if (!bg3::logging::stats_enabled(host->cfg_int)) return;
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
  if (g_ls_ms.size() < stats_period(300)) return;
  auto& v = g_ls_ms;
  auto& j = g_ls_join_ms;
  std::sort(v.begin(), v.end());
  std::sort(j.begin(), j.end());
  log_stats("ls: local-light shadow pass GPU median %.3f ms, p10 %.3f, p90 %.3f, of which join %.3f (%zu frames; "
            "samples %d, exact-k %d + %d (variant %d), soft %d, shadowed mode %d, debug %d, per-light data %d (join %s), owners 0x%X, impl %d, box %d)",
            v[v.size() / 2], v[v.size() / 10], v[v.size() * 9 / 10], j[j.size() / 2], v.size(),
            host->cfg_int("ls_samples", 0), host->cfg_int("ls_k", 0), host->cfg_int("ls_rest_samples", 1), int(g_ls_k_active),
            host->cfg_int("ls_soft", -1), host->cfg_int("ls_shadowed", 3),
            host->cfg_int("ls_debug", 0), host->cfg_int("ls_lights", 1), g_lsl_ok ? "ran" : "off",
            host->cfg_int("ls_owners", 0x1F), host->cfg_int("ls_impl", 1), g_ls_box_active ? g_ls_box_mode : 0);
  v.clear();
  j.clear();
}

// The frame's TLAS if ray-traced shadows or AO are on and the host built one
// this frame or the previous one: the game records the G-buffer of a frame
// before presenting the previous one and the lighting after, so the build is
// usually stamped one frame earlier. The TLAS and set 1 come as one pair from
// the same host snapshot (rt_scene_set, ABI 8): the passes never trace one
// build's TLAS with another's InstInfo, records, soup or pool.
VkAccelerationStructureKHR rt_tlas(VkCommandBuffer cmd, VkDescriptorSet* alpha,uint64_t* omm_identity, bool* scene_current) {
  *scene_current=false;
  *omm_identity=0;
  if (!host->rt_scene || (!host->cfg_int("rt_shadows", 1) && !host->cfg_int("gi_rt", 1) &&
                          !host->cfg_int("rt_local_shadows", 1) && !host->cfg_int("rt_reflections", 1)))
    return VK_NULL_HANDLE;
  VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
  uint64_t built = 0, now = host->frame_index();
  *alpha = VK_NULL_HANDLE;
  const bool scene_ok = host->rt_scene_set && host->rt_scene_set(&tlas, alpha, &built);
  if (!scene_ok || !tlas || built + 1 < now || !*alpha) {
    ++g_rt_frames_missing;
    return VK_NULL_HANDLE;
  }
  if(bg3::omm::scene_capability(static_cast<uint32_t*>(host->persist(bg3::omm::scene_capability_key,sizeof(uint32_t)))) ||
     bg3::native_hit::capable(static_cast<uint32_t*>(host->persist(bg3::native_hit::capability_key,sizeof(uint32_t))))){
    VkAccelerationStructureKHR reference{};
    const bool fast=host->rt_omm_scene_reference && host->rt_omm_scene_reference(cmd,tlas,&reference,omm_identity);
    if(!reference || !*omm_identity){++g_rt_frames_missing;return VK_NULL_HANDLE;}
    if(!fast)tlas=reference;
  }
  ++(built == now ? g_rt_frames_traced : g_rt_frames_stale);
  *scene_current=built == now;
  if (host->cfg_int("rt_order_trace", 0) > 0 && now > 600 && now % 600 < uint64_t(host->cfg_int("rt_order_trace", 0)))
    host->log("[order] rt passes cmd %p frame %llu scene built %llu", (void*)cmd, (unsigned long long)now,
              (unsigned long long)built);
  return tlas;
}

// rt_occ_stats: the window over the read-back slots (the slot written kOccRing - 1 frames ago is
// folded before this frame reuses it), one "occ stats" line per 60 frames read.
void occ_fold_slot(uint64_t now) {
  const uint32_t slot = uint32_t(now % kOccRing);
  if (g_occ_map && g_occ_frame[slot] && g_occ_frame[slot] + kOccRing - 1 <= now) {
    occ::occ_fold(g_occ_win, g_occ_map + size_t(slot) * (kOccBytes / 4));
    // rt_occ_stats_frames: the window (60; 1-4 for frame-by-frame occluder kind and distance of a moving shadow)
    if (g_occ_win.frames >= stats_period(unsigned((std::max)(host->cfg_int("rt_occ_stats_frames", 60), 1)))) {
      host->log("%s", occ::occ_format(g_occ_win).c_str());
      g_occ_win = occ::OccWindow{};
    }
  }
  g_occ_frame[slot] = 0;
  g_occ_slot = slot;
}

// The counters of this frame into its host slot (after the passes that add to them).
void occ_copy(VkCommandBuffer cmd) {
  memory_barrier(cmd, kCS, VK_PIPELINE_STAGE_TRANSFER_BIT);
  const VkBufferCopy cp{0, VkDeviceSize(g_occ_slot) * kOccBytes, kOccBytes};
  host->vk->CmdCopyBuffer(cmd, g_occ_buf, g_occ_host, 1, &cp);
  memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kAll | VK_PIPELINE_STAGE_HOST_BIT);
}

// gi/rt_occ_stats.hlsl over this frame's TLAS, after the sun pass and the GI trace (outside the timed
// RT region). flags: bit 0 rtVis holds this frame's sun trace, bit 1 giRaw this frame's RT GI.
bool occ_run(VkCommandBuffer cmd, Bound& b, const Push& base, uint32_t flags) {
  const VkDeviceFuncs& vk = *host->vk;
  const uint64_t now = host->frame_index();
  occ_fold_slot(now);
  auto traversal = prepare_omm_traversal(cmd, b, g_occ.pipeline, true);
  if (!traversal.valid()) return false;
  memory_barrier(cmd, kAll, VK_PIPELINE_STAGE_TRANSFER_BIT);   // the previous frame's copy out of it first
  vk.CmdFillBuffer(cmd, g_occ_buf, 0, kOccBytes, 0);
  memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | kCS, kCS);
  Push pc = base;
  pc.bounce_spec = flags;   // unused by every other pass (gi_common.hlsli)
  vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_occ.pipeline);
  VkWriteDescriptorSet wr[kLsBindings + kNumExtras];
  vk.CmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_occ_layout, 0, gather_writes(b, kRtBindings, wr), wr);
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_occ_layout, 1, 1, &b.alpha, 0, nullptr);
  vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_occ_layout, 4, 1, &g_bx_set, 0, nullptr);
  vk.CmdPushConstants(cmd, g_occ_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
  vk.CmdDispatch(cmd, (g_w + 7) / 8, (g_h + 7) / 8, 1);
  occ_copy(cmd);   // complete without D-LS; the D-LS pass's copy (run_post) replaces it
  g_occ_frame[g_occ_slot] = now;
  g_occ_armed = now;
  if (!g_occ_logged) {
    g_occ_logged = true;
    host->log("occ stats: active (sun traced %u, RT GI %u; D-LS occluders need ls_box > 0; the sun replica needs "
              "ordinary sun shadows)", flags & 1u, (flags >> 1) & 1u);
  }
  return true;
}

bool run_pre(VkCommandBuffer cmd, Snapshot& s) {
  initialize_hair_origin();
  if(!g_hair_origin_cap && host->cfg_int("rt_hair_physical_origin",0) && !g_hair_origin_warned){g_hair_origin_warned=true;host->log("[hair origin] enable rejected: restart with rt_hair_physical_origin=1 and rt_native_hair=1 to reserve resources");}
  g_hair_origin_frame=s.hair_physical_origin=g_hair_origin_cap && host->cfg_int("rt_hair_physical_origin",0)!=0 && host->cfg_int("rt_origin",1)!=0;
  s.rt_gi_applied=false;
  if (!ensure_layout() || !ensure_images(s.dif.width, s.dif.height)) return false;
  bool ok = refresh(g_trace, "gi/gi_trace.hlsl") & refresh(g_temporal, "gi/gi_temporal.hlsl") &
            refresh(g_ao_apply, "gi/gi_ao_apply.hlsl") & refresh(g_composite, "gi/gi_composite.hlsl") &
            refresh(g_contact, "gi/gi_contact.hlsl");
  if (!ok) return false;
  // Warmup and actual primary dispatch share one Pipe/variant cache slot.
  // Select once so identity/epoch preparation cannot replace it with base
  // every frame and force two pipeline creations at an unchanged generation.
  // rt_primary_fade_class (with rt_primary_epoch): the epoch check plus the camera-fade class.
  const char* primary_shader=host->cfg_int("rt_primary_identity",0)?"gi/pan_primary_identity.hlsl":
                             !host->cfg_int("rt_primary_epoch",0)?"gi/pan_primary.hlsl":
                             host->cfg_int("rt_primary_fade_class",0)?"gi/pan_primary_fade.hlsl":"gi/pan_primary_epoch.hlsl";
  if (host->cfg_int("pan_prepare", 0) && g_rt_layout) {
    refresh(g_pan_primary, primary_shader, g_rt_layout);
    if (g_ls_layout) refresh(g_pan_ls, "gi/pan_local.hlsl", g_ls_layout);
  }
  if (!g_images_ready) {
    for (Bg3Image* im : all_images)
      image_layout(cmd, im->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkClearColorValue zero{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (Bg3Image* im : all_images) host->vk->CmdClearColorImage(cmd, im->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
    {
      image_layout(cmd,g_hair_origin.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
      VkClearColorValue invalid{};invalid.float32[3]=0.0f;
      host->vk->CmdClearColorImage(cmd,g_hair_origin.image,VK_IMAGE_LAYOUT_GENERAL,&invalid,1,&range);
    }
    memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kCS);
    g_images_ready = true;
  }
  timing_init();
  timing_collect();
  pass_timing_collect();
  g_pass_written[g_slot] = 0;
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
  // rt_sun_replace: this frame's trust slot (binding 47), before the descriptors are gathered.
  g_sunt_ok = host->cfg_int("rt_shadows", 1) && sunt_begin(cmd, &g_sunt, s.l_cam2.buffer != VK_NULL_HANDLE);
  Bound b;
  bind_all(b, s);
  Push pc = make_push();
  mat_bits(pc, s, false);
  // Track D: the frame's TLAS for the ray-traced passes.
  VkDescriptorSet alpha = VK_NULL_HANDLE;
  s.omm_identity=0;
  bool scene_current=false;
  VkAccelerationStructureKHR tlas = rt_tlas(cmd, &alpha,&s.omm_identity, &scene_current);
  b.omm_identity=s.omm_identity;
  if (tlas && (!ensure_rt_layout() || !refresh(g_rt_shadow, "gi/rt_shadow.hlsl", g_rt_layout) ||
               !refresh(g_rt_debug, "gi/rt_debug.hlsl", g_rt_layout) ||
               !refresh(g_trace_rt, "gi/gi_trace_rt.hlsl", g_rt_layout) ||
               !refresh(g_rt_origin, "gi/rt_origin.hlsl", g_rt_layout) || !stats_init()))
    tlas = VK_NULL_HANDLE;   // (the statistics buffer is binding 20 and must exist)
  s.gi = host->cfg_int("gi", 1) != 0;
  // Hybrid GI: ray queries replace the screen-space march (and do the AO rays too).
  const bool gi_rt = tlas && s.gi && host->cfg_int("gi_rt", 1);
  const bool rt_shadows = tlas && s.shadow.view && host->cfg_int("rt_shadows", 1) && g_sunt_ok;
  // D-LS runs after the lighting (run_post); it needs the TLAS and the light list.
  s.ls = tlas && host->cfg_int("rt_local_shadows", 1) && s.ls_ok && ensure_ls_layout() &&
         refresh(g_ls, "gi/rt_local_shadows.hlsl", g_ls_layout);
  if (tlas && host->cfg_int("rt_local_shadows", 1) && !s.ls_ok && !g_ls_missing_logged) {
    g_ls_missing_logged = true;
    host->log("ls: the lighting dispatch lacks part of the light list (set1 b0/b13/b23/b24/b25/b39/b40/b41, set0 b2); "
              "rt_local_shadows skipped");
  }
  // D-RF runs after the lighting too: the TLAS, the light list (froxels, constants),
  // the lighting's own set 1 (probes) and last frame's radiance (written by the GI
  // composite, so gi=1).
  const bool rf_want = tlas && host->cfg_int("rt_reflections", 1);
  s.rf = rf_want && s.ls_ok && s.game_set1 && s.gi && ensure_rf_layout(s.game_set1_layout) &&
         refresh(g_rf, "gi/rt_reflections.hlsl", g_rf_layout);
  if (rf_want && !s.rf && !g_rf_missing_logged) {
    g_rf_missing_logged = true;
    host->log("rf: rt_reflections skipped: light list %d, lighting set 1 %p (layout %p), gi %d, pipeline %d", s.ls_ok,
              (void*)s.game_set1, (void*)s.game_set1_layout, s.gi, g_rf.pipeline != VK_NULL_HANDLE);
  }
  s.rf_validate = s.rf && host->cfg_int("rf_validate", 0) != 0 && s.ls_ok;
  // SHaRC radiance cache (sharc=1, core/sharc.h): with the hybrid RT GI and the absolute camera.
  s.sharc = false;
  s.rf_sharc = false;
  if (gi_rt && host->cfg_int("sharc", 0)) {
    s.sharc = s.l_cam2.buffer && s.l_cam2.range >= 224 && ensure_rt_sharc_layout() &&
              refresh(g_sharc_resolve, "gi/sharc_resolve.hlsl", g_rt_sharc_layout) &&
              refresh(g_trace_rt_sharc, "gi/gi_trace_rt_sharc.hlsl", g_rt_sharc_layout) && sharc_begin(cmd, &s.sh);
    if (!s.sharc && !g_sharc_missing_logged) {
      g_sharc_missing_logged = true;
      host->log("sharc: off this frame: absolute camera (set0 b2) %d, layout %d, pipelines resolve %d trace %d "
                "(SHaRC headers and -enable-16bit-types: see [shaders])",
                s.l_cam2.buffer != VK_NULL_HANDLE, g_rt_sharc_layout != VK_NULL_HANDLE,
                g_sharc_resolve.pipeline != VK_NULL_HANDLE, g_trace_rt_sharc.pipeline != VK_NULL_HANDLE);
    }
    s.rf_sharc = s.sharc && s.rf && (s.sh.query & 4u) && ensure_rf_sharc_layout() &&
                 refresh(g_rf_sharc, "gi/rt_reflections_sharc.hlsl", g_rf_sharc_layout);
    if (s.sharc) {
      // Room-scale rays: hits beyond the screen now return the cache's radiance, not just occlusion.
      pc.gi_rt_radius = (std::max)(host->cfg_float("sharc_gi_radius", 4.0f), 0.01f);
      if (!g_sharc_logged) {
        g_sharc_logged = true;
        host->log("sharc: first frame (GI radius %.1f m, query 0x%x, reflections %d, inject stride %d)", pc.gi_rt_radius,
                  s.sh.query, s.rf_sharc, host->cfg_int("sharc_inject_stride", 2));
      }
    }
  }
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
    // Every traced frame: the out-of-range guards are always counted.
    stats_begin(cmd, host->cfg_int("rt_alpha_test", 1) != 0,
                pc.rt_stats || pc.ls_stats || pc.rf_stats || host->cfg_int("ls_check", 0) || host->cfg_int("rt_primary_epoch",0), scene_current);
    bind_rt(b, tlas, alpha);
    if (!g_rt_logged) {
      g_rt_logged = true;
      host->log("rt: first traced frame at %ux%u (TLAS %p, shadows %d, GI %d)", g_w, g_h, (void*)tlas, rt_shadows,
                gi_rt);
    }
  }
  bool rt_started = false;
  static int previous_primary_mode = -1;
  const int primary_mode = host->cfg_int("rt_primary_check",0) ? (host->cfg_int("rt_primary_epoch",0) ? 2 : 1) : 0;
  if (primary_mode != previous_primary_mode) {
    host->log("rt cost scope: primary_check=%d epoch=%d rt_origin=%u; interval starts at first RT pre-pass and "
              "ends after GI trace/temporal/AO and sun resolve; local-light and reflection passes are timed separately",
              primary_mode != 0,primary_mode == 2,pc.rt_origin);
    previous_primary_mode = primary_mode;
  }
  auto rt_begin = [&] {
    if (!rt_started) rt_timestamp(cmd, 0);
    rt_started = true;
  };
  // Depth, normals and AO come from earlier graphics passes.
  memory_barrier(cmd, kAll, kCS);
  s.pan = host->pan_capture && host->pan_capture(cmd, "depth", &s.dep);
  if (s.pan) {
    host->pan_capture(cmd, "normal", &s.nrm);
    host->pan_capture(cmd, "albedo", &s.alb);
    host->pan_capture(cmd, "material", &s.mat);
  }
  // Robust ray origins first: every ray-traced pass (and D-LS after the lighting) reads them.
  if (tlas && pc.rt_origin != 0) {
    rt_begin();
    pass_timestamp(cmd, kOrigin, 0);
    if(!dispatch(cmd, g_rt_origin, b, pc, g_w, g_h, true))return false;
    pass_timestamp(cmd, kOrigin, 1);
    if(g_hair_origin_cap && g_hair_origin_last_mode!=uint32_t(s.hair_physical_origin)){
      g_hair_origin_last_mode=uint32_t(s.hair_physical_origin);
      host->log("[hair origin] frame %llu secondary origin mode %u; virtual primary/history unchanged",(unsigned long long)host->frame_index(),g_hair_origin_last_mode);
    }
    memory_barrier(cmd, kCS, kCS);
  }
  if ((s.pan || host->cfg_int("rt_primary_check", 0)) && tlas &&
      refresh(g_pan_primary, primary_shader, g_rt_layout)) {
    if(!dispatch(cmd, g_pan_primary, b, pc, g_w, g_h, true))return false;
    Bg3Binding im{};
    im.image = g_aux.image; im.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    im.layout = VK_IMAGE_LAYOUT_GENERAL; im.width = g_w; im.height = g_h;
    if (s.pan) host->pan_capture(cmd, "primary", &im);
    memory_barrier(cmd, kAll, kCS);
  }
  // SHaRC: last frame's injections into the cache before this frame's GI rays read it.
  if (s.sharc) {
    rt_begin();
    sharc_timestamp(cmd, 0);
    if(!sharc_dispatch(cmd, g_sharc_resolve, b, pc, s.sh.set, s.sh.resolve_groups[0], s.sh.resolve_groups[1])){sharc_end(cmd);s.sharc=false;s.rf_sharc=false;}
    sharc_timestamp(cmd, 1);
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
      const bool hit_direct=host->cfg_int("gi_hit_direct",0) && host->cfg_int("rt_hit_material",0) && pc.gi_rt_fallback==0u;
      s.rt_gi_applied=hit_direct && hit_gi_dispatch(cmd,b,s,pc);
      if(!s.rt_gi_applied)s.rt_gi_applied=s.sharc?sharc_dispatch(cmd,g_trace_rt_sharc,b,pc,s.sh.set,(g_hw+7)/8,(g_hh+7)/8):
                                  dispatch(cmd,g_trace_rt,b,pc,g_hw,g_hh,true);
      if(!s.rt_gi_applied){if(s.sharc)sharc_end(cmd);s.sharc=false;s.rf_sharc=false;dispatch(cmd,g_trace,b,pc,g_hw,g_hh);}
    } else {
      dispatch(cmd, g_trace, b, pc, g_hw, g_hh);
    }
    trace_timestamp(cmd, 1, s.rt_gi_applied);
    memory_barrier(cmd, kCS, kCS);
    pass_timestamp(cmd, kTemporal, 0);
    const bool temporal_applied = dispatch(cmd, g_temporal, b, pc, g_hw, g_hh);
    pass_timestamp(cmd, kTemporal, 1, temporal_applied);
    if (mv_move) image_layout(cmd, g_mv_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, g_mv_layout, kCS, kAll);
    memory_barrier(cmd, kCS, kCS);
    if (pc.ao > 0.0f || pc.debug == 3 || (pc.qa_zero & 4u)) {   // qa_zero bit 2: gi_ao_apply writes AO 0
      s.aux_ao = true;
      image_layout(cmd, s.ao.image, s.ao.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
      pass_timestamp(cmd, kAoFold, 0);
      const bool ao_applied = dispatch(cmd, g_ao_apply, b, pc, g_w, g_h);
      pass_timestamp(cmd, kAoFold, 1, ao_applied);
      image_layout(cmd, s.ao.image, VK_IMAGE_LAYOUT_GENERAL, s.ao.layout, kCS, kCS);
    }
  }
  // Ray-traced sun shadows (Track D) when the host has a scene, otherwise
  // screen-space contact shadows; both fold into the game's sun shadow mask.
  s.tlas = tlas;   // the post pass's debug views
  s.alpha = alpha;
  s.rt_sun = false;
  if (rt_shadows) {
    rt_begin();
    image_layout(cmd, s.shadow.image, s.shadow.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    pass_timestamp(cmd, kSunTrace, 0);
    s.rt_sun=dispatch(cmd,g_rt_shadow,b,pc,g_w,g_h,true);
    pass_timestamp(cmd, kSunTrace, 1, s.rt_sun);
    if (s.rt_sun && g_sunt.replace && pc.rt_debug != 3 && pc.rt_debug != 5 && pc.rt_debug != 8 &&
        refresh(g_sun_resolve, "gi/rt_sun_resolve.hlsl", g_rt_layout)) {
      memory_barrier(cmd, kCS, kCS);
      pass_timestamp(cmd, kSunResolve, 0);
      s.rt_sun=dispatch(cmd, g_sun_resolve, b, pc, g_w, g_h, true);
      pass_timestamp(cmd, kSunResolve, 1, s.rt_sun);
    }
    sunt_end(cmd);
    image_layout(cmd, s.shadow.image, VK_IMAGE_LAYOUT_GENERAL, s.shadow.layout, kCS, kCS);
    if (s.pan && s.rt_sun) {
      Bg3Binding im{};
      im.image = g_rt_vis.image; im.format = VK_FORMAT_R8G8B8A8_UNORM;
      im.layout = VK_IMAGE_LAYOUT_GENERAL; im.width = g_w; im.height = g_h;
      host->pan_capture(cmd, "sun", &im);
    }
  } else if (s.shadow.view && host->cfg_int("contact_shadows", 1)) {
    image_layout(cmd, s.shadow.image, s.shadow.layout, VK_IMAGE_LAYOUT_GENERAL, kAll, kCS);
    pass_timestamp(cmd, kContact, 0);
    const bool contact_applied = dispatch(cmd, g_contact, b, pc, g_w, g_h);
    pass_timestamp(cmd, kContact, 1, contact_applied);
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
    g_rt_written[g_rt_slot] = g_rt_queries && bg3::logging::stats_enabled(host->cfg_int);
    g_rt_slot = (g_rt_slot + 1) % kRing;
  }
  timestamp(cmd, 1);
  // rt_occ_stats (diagnostic windows only): after the timed region, on this frame's TLAS.
  g_occ_armed = 0;
  if (tlas && host->cfg_int("rt_occ_stats", 0)) {
    const bool sun_ok = s.rt_sun;
    memory_barrier(cmd, kCS, kCS);
    if (!ensure_occ() || !refresh(g_occ, "gi/rt_occ_stats.hlsl", g_occ_layout) ||
        !occ_run(cmd, b, pc, (sun_ok ? 1u : 0u) | (s.rt_gi_applied ? 2u : 0u))) {
      if (!g_occ_logged && g_builds.pending() == 0) {
        host->log("occ stats: requested but the pass is unavailable (layout, pipeline or buffers)");
        g_occ_logged = true;
      }
    }
  } else if (g_occ_win.frames || g_occ_logged) {
    g_occ_win = occ::OccWindow{};   // a later window starts clean
    std::memset(g_occ_frame, 0, sizeof g_occ_frame);
    g_occ_logged = false;
  }
  host->restore_bindings(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
  return true;
}

// SHaRC after the GI composite: inject this frame's lit diffuse (+ the D-EM emission) into the
// cache, then the cache's debug views over the lit diffuse (sharc_debug).
void sharc_post(VkCommandBuffer cmd, const Snapshot& s, const Push& base) {
  Bound b;
  bind_all(b, s);
  bind_rt(b, s.tlas, s.alpha);
  Push pc = base;
  pc.sharc_fx = 0;
  memory_barrier(cmd, kCS, kCS);   // the composite wrote the lit diffuse
  // Debug views and validation passes that rewrite the lit diffuse (or the lighting's inputs)
  // would be injected as radiance: the cache keeps what it has meanwhile.
  const bool paused = pc.debug != 0 || pc.rf_debug != 0 || pc.ls_debug != 0 || s.rf_validate;
  if (paused && !g_sharc_paused_logged) {
    g_sharc_paused_logged = true;
    host->log("sharc: injection paused while gi_debug/rf_debug/ls_debug/rf_validate rewrite the lit diffuse");
  }
  Pipe& inj = s.sh.lock ? g_sharc_inject_lock : g_sharc_inject;
  if (!paused && refresh(inj, s.sh.lock ? "gi/sharc_inject_lock.hlsl" : "gi/sharc_inject.hlsl", g_rt_sharc_layout)) {
    // D-EM: the game's effects target of this frame (its emissive draws), at render resolution.
    VkImage fx_image = VK_NULL_HANDLE;
    VkImageLayout fx_from = VK_IMAGE_LAYOUT_UNDEFINED, fx_in = VK_IMAGE_LAYOUT_UNDEFINED;
#if BG3RTX_SHARC_EM
    if (host->cfg_float("sharc_em", 1.0f) > 0.0f && host->rt_emissive) {
      VkDescriptorSetLayout el = VK_NULL_HANDLE;
      VkDescriptorSet es = VK_NULL_HANDLE;
      Bg3EmissiveInfo info{};
      if (host->rt_emissive(cmd, &el, &es, &info) && info.fx_view && info.fx_image && info.fx_width == g_w &&
          info.fx_height == g_h && info.fx_layout != VK_IMAGE_LAYOUT_UNDEFINED && info.built_frame + 3 >= host->frame_index() &&
          info.fx_image != s.dif.image && info.fx_image != s.spe.image) {
        fx_image = info.fx_image;
        fx_from = info.fx_layout;
        fx_in = (fx_from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || fx_from == VK_IMAGE_LAYOUT_GENERAL)
                    ? fx_from
                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        for (uint32_t i = 0; i < kNumExtras; ++i)
          if (kExtras[i].binding == 49) b.ximg[i] = {VK_NULL_HANDLE, info.fx_view, fx_in};
        pc.sharc_fx = 1;
      }
      if (!g_sharc_em_logged) {
        g_sharc_em_logged = true;
        host->log("sharc: D-EM effects target %s (layout %d, %ux%u)", pc.sharc_fx ? "injected" : "not available",
                  info.fx_layout, info.fx_width, info.fx_height);
      }
    }
#else
    if (!g_sharc_em_logged) {
      g_sharc_em_logged = true;
      host->log("sharc: built without D-EM (no Bg3HostApi::rt_emissive): no emission injected beyond the lit diffuse");
    }
#endif
    if (fx_image && fx_in != fx_from) image_layout(cmd, fx_image, fx_from, fx_in, kAll, kCS);
    const uint32_t st = (std::max)(uint32_t(host->cfg_int("sharc_inject_stride", 2)), 1u);
    const uint32_t bw = (g_w + st - 1) / st, bh = (g_h + st - 1) / st;
    sharc_timestamp(cmd, 2);
    const bool injected=sharc_dispatch(cmd, inj, b, pc, s.sh.set, (bw + 7) / 8, (bh + 7) / 8);
    sharc_timestamp(cmd, 3);
    if (fx_image && fx_in != fx_from) image_layout(cmd, fx_image, fx_in, fx_from, kCS, kAll);
    if(!injected){sharc_end(cmd);return;}
    memory_barrier(cmd, kCS, kCS);
  }
  // Views (sharc_debug 1-6); 5, the QA roof mask, needs rt_sun_replace's roof volumes (binding 47).
  if (s.sh.debug >= 1 && s.sh.debug <= 6) {
    if (s.sh.debug == 5 && g_sunt_ok && refresh(g_sharc_roof, "gi/sharc_roof.hlsl", g_rt_layout)) {
      dispatch(cmd, g_sharc_roof, b, pc, g_w, g_h, true);
    } else if (refresh(g_sharc_debug, "gi/sharc_debug.hlsl", g_rt_sharc_layout)) {
      sharc_dispatch(cmd, g_sharc_debug, b, pc, s.sh.set, (g_w + 7) / 8, (g_h + 7) / 8);
    }
    memory_barrier(cmd, kCS, kCS);
  }
  sharc_end(cmd);
}

void run_post(VkCommandBuffer cmd, const Snapshot& s) {
  g_hair_origin_frame=s.hair_physical_origin;
  bool local_applied=false, reflection_applied=false, ls_box_applied=false;
  Bound b;
  bind_all(b, s);
  Push pc = make_push();
  mat_bits(pc, s, true);
  // The tiled lighting just wrote diffuse/specular from compute.
  memory_barrier(cmd, kCS, kCS);
  timestamp(cmd, 2);
  // D-LS first: the GI composite keeps diffuse + specular as next frame's bounce source.
  // (the sun set first: none free means no D-LS this frame, before any transition)
  if (host->cfg_int("dev", 0) && s.ls && g_ls_layout) {
    prewarm_pass(g_ls_k, "gi/rt_local_shadows_k.hlsl", g_ls_layout);
    prewarm_pass(g_ls_full_k, "gi/rt_local_shadows_full_k.hlsl", g_ls_layout);
    if (s.box_ready && ensure_ls_box_layout(s.game_set1_layout)) {
      prewarm_pass(g_ls_box_k, "gi/rt_local_shadows_box_k.hlsl", g_ls_box_layout);
      prewarm_pass(g_ls_full_box_k, "gi/rt_local_shadows_full_box_k.hlsl", g_ls_box_layout);
    }
  }
  const VkDescriptorSet sun_set = s.ls && s.tlas && g_ls.pipeline ? write_sun_set(s) : VK_NULL_HANDLE;
  if (sun_set) {
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
    const VkDescriptorSet sun = sun_set;
    // The box statistics (ls_box with ls_stats): zero the device-local counters before the timed
    // region; the previous frame's copy out of them (earlier in the queue) comes first.
    const bool bx_stats = lp.ls_stats && host->cfg_int("ls_box", 0) && g_bx_buf && g_stats_buf;
    if (bx_stats) {
      memory_barrier(cmd, kAll, VK_PIPELINE_STAGE_TRANSFER_BIT);
      host->vk->CmdFillBuffer(cmd, g_bx_buf, 0, kLsBoxXBytes, 0);
      memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kCS);
    }
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
      if(!ls_dispatch(cmd, lb, mp, sun, g_ls_match.pipeline, kLslSlots / 64, 1))lp.ls_flags &= ~1u;
      memory_barrier(cmd, kCS, kCS);
    }
    ls_timestamp(cmd, 1);
    // The default-settings variant, or the one with every mode (more registers, docs/TRACK_D_RT.md 9.9).
    // rt_occ_stats: the every-mode box pass with the occluder counters (RT_OCC_STATS), this frame armed.
    const bool occ_on = g_occ_armed == host->frame_index() && g_occ_buf && host->cfg_int("ls_box", 0);
    const bool full = lp.ls_samples || lp.ls_debug || lp.ls_stats || occ_on;
    VkPipeline lsp = g_ls.pipeline;
    bool box = false, kv = false, k_ran = false;
    lp.ls_pass = host->cfg_int("ls_check", 0) == 2 ? 2u : 0u;   // reference against itself
    if(s.native_direct_validation) {
      lsp=VK_NULL_HANDLE;
      if(host->cfg_int("ls_box",0)) {
        if(s.box_ready && ensure_ls_box_layout(s.game_set1_layout) &&
           refresh(g_ls_native_full_box,"gi/rt_local_shadows_native_full_box.hlsl",g_ls_box_layout)) {
          lsp=g_ls_native_full_box.pipeline;box=true;
        }
      } else if(refresh(g_ls_native_full,"gi/rt_local_shadows_native_full.hlsl",g_ls_layout))lsp=g_ls_native_full.pipeline;
      if(!lsp)host->log("[ls-native-direct] REFUSED owned validation pipeline unavailable");
    } else if (host->cfg_int("ls_check", 0)) {
      Pipe& c = full ? g_ls_full_check : g_ls_check;
      if (refresh(c, full ? "gi/rt_local_shadows_full_check.hlsl" : "gi/rt_local_shadows_check.hlsl", g_ls_layout))
        lsp = c.pipeline;
    } else if (!host->cfg_int("ls_impl", 1)) {
      if (refresh(g_ls_ref, "gi/rt_local_shadows_ref.hlsl", g_ls_layout)) lsp = g_ls_ref.pipeline;
    } else {
      // ls_k > 0 (TRACK_D_LOCAL_LIGHTS.md 9.11): the LS_K twin of the variant below; ls_k=0 never builds it.
      // A twin that fails to build leaves the every-light variant (same expected output, logged).
      kv = !occ_on && bg3::ls_exact_k::use_variant(host->cfg_int("ls_k", 0), int(lp.ls_samples), int(lp.ls_debug), 0, 1, false);
      if (kv) lp.ls_kmode = bg3::ls_exact_k::kmode(host->cfg_int("ls_k", 0), host->cfg_int("ls_rest_samples", 1));
      if (host->cfg_int("ls_box", 0)) {
        if (s.box_ready && ensure_ls_box_layout(s.game_set1_layout)) {
          Pipe& kvariant = full ? g_ls_full_box_k : g_ls_box_k;
          if (kv && refresh(kvariant, full ? "gi/rt_local_shadows_full_box_k.hlsl" : "gi/rt_local_shadows_box_k.hlsl", g_ls_box_layout)) {
            lsp = kvariant.pipeline;
            box = k_ran = true;
          }
          Pipe& variant = full ? (occ_on ? g_ls_full_box_occ : g_ls_full_box) : g_ls_box;
          if (!box && refresh(variant, full ? "gi/rt_local_shadows_full_box.hlsl" : "gi/rt_local_shadows_box.hlsl", g_ls_box_layout)) {
            lsp = variant.pipeline;
            box = true;
          }
        }
        if (!box && !g_ls_box_missing_logged && g_builds.pending() == 0) {
          host->log("ls box: requested but native set/layout or pipeline unavailable; retaining point/spot pass");
          g_ls_box_missing_logged = true;
        }
      }
      Pipe& kvariant = full ? g_ls_full_k : g_ls_k;
      if (!box && kv && refresh(kvariant, full ? "gi/rt_local_shadows_full_k.hlsl" : "gi/rt_local_shadows_k.hlsl", g_ls_layout)) {
        lsp = kvariant.pipeline;
        k_ran = true;
      } else if (!box && full && refresh(g_ls_full, "gi/rt_local_shadows_full.hlsl", g_ls_layout)) lsp = g_ls_full.pipeline;
    }
    k_ran = k_ran && !s.pan;   // the pan capture below replaces the pass (pan_local.hlsl, LS_K=0)
    if (k_ran != g_ls_k_active || (kv && !k_ran && !s.pan && !g_ls_k_missing_logged)) {
      g_ls_k_active = k_ran;
      const bool missing = kv && !k_ran && !s.pan;
      g_ls_k_missing_logged = missing;
      host->log("ls exact-k: %s (ls_k %d, ls_rest_samples %d%s)", k_ran ? "active" : "inactive", host->cfg_int("ls_k", 0),
                host->cfg_int("ls_rest_samples", 1), missing ? "; LS_K pipeline UNAVAILABLE, tracing every light" : "");
      g_ls_ms.clear();g_ls_join_ms.clear();
    }
    const int box_mode = box ? (std::min)(host->cfg_int("ls_box", 0), 2) : 0;
    if (box != g_ls_box_active || box_mode != g_ls_box_mode) {
      g_ls_box_active = box;
      g_ls_box_mode = box_mode;
      host->log("ls box: %s (mode %d; native lighting set %p, dynamic offsets %u)", box ? "active" : "inactive", box_mode,
                (void*)s.game_set1, s.game_set1_ndyn);
      g_ls_ms.clear();g_ls_join_ms.clear();
    }
    if (s.pan && refresh(g_pan_ls, "gi/pan_local.hlsl", g_ls_layout)) lsp = g_pan_ls.pipeline;
    local_applied=lsp && ls_dispatch(cmd, lb, lp, sun, lsp, (g_w + 7) / 8, (g_h + 7) / 8, box ? &s : nullptr);
    ls_box_applied = box && local_applied && !s.pan;   // the screen traced its box lights this frame
    // rt_hair_fibre (docs/RTX_HAIR_PRIMARY.md 3b-2b): the visible strands' sun and local lights with the fibre BSDF,
    // on top of D-LS's result (same sets and push constants; inside D-LS's timed region).
    if (local_applied && !s.pan && host->cfg_int("rt_hair_fibre", 0) && host->cfg_int("rt_hair_groom_primary", 0) &&
        refresh(g_hair_fibre, "gi/hair_fibre.hlsl", g_ls_layout)) {
      memory_barrier(cmd, kCS, kCS);
      const bool ran = ls_dispatch(cmd, lb, lp, sun, g_hair_fibre.pipeline, (g_w + 7) / 8, (g_h + 7) / 8);
      // Logged when it starts (the first frame after a frame without it) and every 300 frames while on.
      static uint64_t s_hair_fibre_last = 0, s_hair_fibre_run = 0;
      if (ran) {
        const uint64_t now = host->frame_index();
        s_hair_fibre_run = now == s_hair_fibre_last + 1 ? s_hair_fibre_run + 1 : 0;
        s_hair_fibre_last = now;
        if (s_hair_fibre_run % 300 == 0)
          host->log("[hair fibre] strand pixels lit by the fibre BSDF after D-LS (%llu frames in this run)",
                    (unsigned long long)s_hair_fibre_run);
      }
    }
    if (s.pan && local_applied) {
      Bg3Binding im{};
      im.image = g_aux.image; im.format = VK_FORMAT_R16G16B16A16_SFLOAT;
      im.layout = VK_IMAGE_LAYOUT_GENERAL; im.width = g_w; im.height = g_h;
      host->pan_capture(cmd, "local", &im);
    }
    ls_timestamp(cmd, 2);
    if (occ_on && ls_box_applied && lsp == g_ls_full_box_occ.pipeline) occ_copy(cmd);   // outside the timed region
    if (bx_stats && ls_box_applied) {   // after the end timestamp: outside the timed region
      memory_barrier(cmd, kCS, VK_PIPELINE_STAGE_TRANSFER_BIT);
      const VkBufferCopy cp{0, VkDeviceSize(g_stats_slot) * kStatsSlot + VkDeviceSize(kLsBoxXFirst) * 64 * 4, kLsBoxXBytes};
      host->vk->CmdCopyBuffer(cmd, g_bx_buf, g_stats_buf, 1, &cp);
      memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kAll | VK_PIPELINE_STAGE_HOST_BIT);
    }
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
    // Bit 26: the screen's D-LS box pass ran (ls_box=1): rf_hit_shade's boxes then follow its
    // give-back; clear, the screen keeps the game's box x AO and the hit applies the same factor
    // (native_hit_direct.hlsli hd_box_game).
    if (ls_box_applied) rp.ls_flags |= 1u << 26;
    rp.rf_validate = s.rf_validate ? 1u : 0u;
    rf_timing_collect();
    // The reprojection samples the MVs in SHADER_READ_ONLY (as the GI traces do).
    bool mv_move = rp.has_mv && g_mv_image && g_mv_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (mv_move) image_layout(cmd, g_mv_image, g_mv_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kAll, kCS);
    // rf_hit_gi's device-local counters: zeroed before the timed region (rf_stats only).
    const bool rg_stats = rp.rf_stats && host->cfg_int("rf_hit_gi", 0) && host->cfg_int("rf_hit_shade", 0) && g_stats_buf &&
                          ensure_rg();
    if (rg_stats) {
      memory_barrier(cmd, kAll, VK_PIPELINE_STAGE_TRANSFER_BIT);
      host->vk->CmdFillBuffer(cmd, g_rg_buf, 0, kRfGiBytes, 0);
      memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kCS);
    }
    g_rg_ran = false;
    rf_timestamp(cmd, 0);
    // rf_hit_shade (M4.2): tier (b) hits shaded with their native material and lighting. It
    // needs the startup material provider and the sun set (D-LS's of this frame, else a fresh
    // one); without them, or when the variant cannot run, the ordinary D-RF runs unchanged.
    bool hit_shade = false;
    if (host->cfg_int("rf_hit_shade", 0) && host->cfg_int("rt_hit_material", 0) && !s.rf_validate) {
      const VkDescriptorSet sun = sun_set ? sun_set : (ensure_ls_layout() ? write_sun_set(s) : VK_NULL_HANDLE);
      hit_shade = rf_hit_dispatch(cmd, rb, s, rp, sun);
      if (!g_rf_hit_logged) {
        g_rf_hit_logged = true;
        host->log("rf hit shade: first request: %s (sun set %p, sharc %d)", hit_shade ? "tier (b) variant dispatched"
                  : "variant unavailable, ordinary D-RF", (void*)sun, s.rf_sharc);
      }
    }
    reflection_applied = hit_shade || rf_dispatch(cmd, rb, s, rp);
    rf_timestamp(cmd, 1);
    if (rg_stats && hit_shade && g_rg_ran) {   // after the end timestamp: outside the timed region
      memory_barrier(cmd, kCS, VK_PIPELINE_STAGE_TRANSFER_BIT);
      const VkBufferCopy cp{0, VkDeviceSize(g_stats_slot) * kStatsSlot + VkDeviceSize(kRfGiFirst) * 64 * 4, kRfGiBytes};
      host->vk->CmdCopyBuffer(cmd, g_rg_buf, g_stats_buf, 1, &cp);
      memory_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kAll | VK_PIPELINE_STAGE_HOST_BIT);
    }
    if (mv_move) image_layout(cmd, g_mv_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, g_mv_layout, kCS, kAll);
    memory_barrier(cmd, kCS, kCS);
    if (host->set_spec_hit_distance)
      host->set_spec_hit_distance(reflection_applied?g_rf_hit.image:VK_NULL_HANDLE,reflection_applied?g_rf_hit.view:VK_NULL_HANDLE,
                                  reflection_applied?VK_FORMAT_R16_SFLOAT:VK_FORMAT_UNDEFINED,reflection_applied?g_w:0,reflection_applied?g_h:0);
    if (!g_rf_logged) {
      g_rf_logged = true;
      host->log("rf: first frame at %ux%u; lighting set 1 %p layout %p (%u dynamic offsets), validate %d", g_w, g_h,
                (void*)s.game_set1, (void*)s.game_set1_layout, s.game_set1_ndyn, s.rf_validate);
    }
  }
  if (s.gi) {
    const bool fibre = host->cfg_int("rt_hair_fibre_gi", 0) && host->cfg_int("rt_hair_groom_primary", 0) &&
                       refresh(g_composite_fibre, "gi/gi_composite.hlsl");
    pass_timestamp(cmd, kComposite, 0);
    const bool composite_applied = dispatch(cmd, fibre ? g_composite_fibre : g_composite, b, pc, g_w, g_h);
    pass_timestamp(cmd, kComposite, 1, composite_applied);
  }
  if (host->cfg_int("rt_primary_epoch",0)) {
    // Command-application counts, not expensive per-ray atomics. Existing GPU
    // timestamps and static debug captures independently prove execution/output.
    static uint32_t frames=0, sun=0, gi=0, local=0, reflection=0;
    static int reset_token=0;
    if(bg3::stats_window::reset(host->cfg_int,reset_token)) frames=sun=gi=local=reflection=0;
    sun+=s.rt_sun; gi+=s.rt_gi_applied; local+=local_applied; reflection+=reflection_applied;
    if (++frames>=stats_period(120)) {
      host->log("rt applied (%u frames): sun %u GI %u local %u reflections %u (recorded dispatches; GPU timings separate)",
                frames,sun,gi,local,reflection);
      frames=sun=gi=local=reflection=0;
    }
  }
  if (s.pan) {
    Bg3Binding im{};
    im.image = g_hist[g_parity ^ 1].image; im.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    im.layout = VK_IMAGE_LAYOUT_GENERAL; im.width = g_hw; im.height = g_hh;
    if (s.gi) host->pan_capture(cmd, "gi", &im);
  }
  if (s.sharc && s.tlas) sharc_post(cmd, s, pc);
  if (s.tlas && pc.rt_debug >= 1 && pc.rt_debug <= 8) {
    memory_barrier(cmd, kCS, kCS);
    bind_rt(b, s.tlas, s.alpha);
    if(!dispatch(cmd, g_rt_debug, b, pc, g_w, g_h, true))return;
  }
  if(s.tlas && pc.rt_debug==9 && host->cfg_int("rt_hit_material",0) &&
     bg3::native_hit::capable(static_cast<uint32_t*>(host->persist(bg3::native_hit::capability_key,sizeof(uint32_t)))) &&
     refresh(g_hit_material,"gi/rt_hit_material.hlsl",g_rt_layout)){
    memory_barrier(cmd,kCS,kCS);bind_rt(b,s.tlas,s.alpha);
    // run_post has its own zero-initialized Push; pre-pass statistics flags
    // are not carried here. Enable only this optional provider view's counters.
    auto material_pc=pc;material_pc.rt_stats=host->cfg_int("rt_stats",0)?1u:0u;
    (void)dispatch(cmd,g_hit_material,b,material_pc,g_w,g_h,true); // optional: unsupported keeps remaining passes
  }
  // One cache-free reference renderer: direct or finite two-surface subset.
  // Normal RF/probe replacement remains separate and unchanged.
  if (host->cfg_int("dev", 0) && s.tlas && s.ls_ok && host->cfg_int("rt_hit_material", 0) &&
      bg3::native_hit::capable(static_cast<uint32_t*>(host->persist(bg3::native_hit::capability_key, sizeof(uint32_t)))) &&
      ensure_hit_reference_layout()) {
    prewarm_pass(g_hit_direct, "gi/rt_hit_direct.hlsl", g_hit_reference_layout);
    prewarm_pass(g_hit_two_bounce, "gi/rt_hit_two_bounce.hlsl", g_hit_reference_layout);
  }
  const int reference_mode=host->cfg_int("rt_hit_reference",0);
  if(s.tlas && s.ls_ok && (pc.rf_debug==7 || reference_mode==1 || reference_mode==2) && host->cfg_int("rt_hit_material",0) &&
     bg3::native_hit::capable(static_cast<uint32_t*>(host->persist(bg3::native_hit::capability_key,sizeof(uint32_t)))) && ensure_hit_reference_layout()){
    const bool two=reference_mode==2;
    Pipe& reference=two?g_hit_two_bounce:g_hit_direct;
    if(refresh(reference,two?"gi/rt_hit_two_bounce.hlsl":"gi/rt_hit_direct.hlsl",g_hit_reference_layout)){
      Bound direct{};bind_all(direct,s);bind_rt(direct,s.tlas,s.alpha);bind_ls(direct,s);bind_extras(direct,s);
      memory_barrier(cmd,kCS,kCS);
      auto reference_pc=pc;reference_pc.rt_stats=host->cfg_int("rt_stats",0)?1u:0u;
      (void)ls_dispatch(cmd,direct,reference_pc,VK_NULL_HANDLE,reference.pipeline,(g_w+7)/8,(g_h+7)/8,nullptr,true);
    }
  }
  // Capture after the RT debug overlay so debug 8 bursts contain its QA encoding.
  if (s.pan) host->pan_capture(cmd, "diffuse", &s.dif);
  // D-EM (core/emissive.cpp): rt_debug 16-18 are its views (its modes 6-8), else the check.
  const bool em_view = pc.rt_debug >= 16 && pc.rt_debug <= 20;
  if (em_view || em_check_due())
    em_debug(cmd, s.cam, s.vp, s.dep, s.dif, s.spe, s.l_cam2, g_w, g_h, em_view ? pc.rt_debug - 10 : 0);
  // Water reflections and forward composition (core/water.cpp, rt_water_reflections).
  if (s.tlas && s.alpha && water_rf_wanted()) {
    Push wpc = pc;
    wpc.rt_stats = 0;
    WaterRfIn win{s.cam, s.vp, s.mat, s.dep, s.dif, s.spe, s.l_cam2, s.tlas, s.alpha, &wpc, uint32_t(sizeof wpc), g_w, g_h};
    if (reflection_applied && host->cfg_int("rt_water_rr_hitdist", 0)) win.rf_hit = g_rf_hit.view;
    water_rf(cmd, win);
  }
  timestamp(cmd, 3);
  g_slot_written[g_slot] = g_queries && bg3::logging::stats_enabled(host->cfg_int);
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
  if (!d->indirect || (!host->cfg_int("gi", 1) && !host->cfg_int("rt_shadows", 1) && !host->cfg_int("rt_local_shadows", 1) &&
                       !host->cfg_int("rt_reflections", 1)))
    return;
  describe(cmd, d);
  std::lock_guard<std::mutex> lock(g_mu);
  uint64_t f = host->frame_index();
  const bool validation=host->cfg_int("ls_native_direct_validate",0)!=0;
  const int validation_view=host->cfg_int("ls_debug",0);
  const bool actual=(d->native_validation&1u)!=0u;
  if(validation || actual) {
    Snapshot candidate{};
    if(snapshot(cmd,candidate) && (!validation || !actual || (validation_view!=4 && validation_view!=5) ||
       host->cfg_int("ls_check",0) || !host->cfg_int("ls_impl",1))) {
      // Never mix transformed native output and the ordinary recompute. This
      // also exposes unsupported/inline native modules and live key changes.
      static uint64_t refused=0;
      if(++refused<=8 || refused%300==0)host->log("[ls-native-direct] REFUSED frame %llu cs %016llx requested %u actual %u view %d shadow %u",
          (unsigned long long)f,(unsigned long long)d->cs_hash,unsigned(validation),unsigned(actual),validation_view,unsigned(candidate.shadow.view!=VK_NULL_HANDLE));
      for(auto& entry:g_pending)if(entry.second.native_direct_validation)entry.second.ls=false;
      return;
    }
  }
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
  s.native_direct_validation=validation && actual;
  if(s.native_direct_validation) {
    static uint64_t accepted=0;
    if(++accepted<=4 || accepted%300==0)host->log("[ls-native-direct] active frame %llu cs %016llx matched native module; AO0/direct1/sun0 validation",
        (unsigned long long)f,(unsigned long long)d->cs_hash);
  }
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

// The background builds' status (common/pipeline_async.h Monitor): a line every rt_timing_frames presents and
// one when a burst of builds is done (the gates of tools/live_batches/hair_qual).
void gi_on_frame(uint64_t) {
  static bg3::pipe_async::Monitor mon;
  static int reset_token = 0;
  if (bg3::stats_window::reset(host->cfg_int, reset_token)) {
    std::lock_guard<std::mutex> lock(g_mu);
    // Discard in-flight readbacks as well as sums: pre-readiness GPU work must
    // never enter the first complete measurement window.
    std::fill_n(g_slot_written, kRing, false); std::fill_n(g_rt_written, kRing, false);
    std::fill_n(g_tr_written, kRing, false); std::fill_n(g_rf_written, kRing, false);
    std::fill_n(g_ls_written, kRing, false); std::fill_n(g_stats_frame, kStatsRing, uint64_t(0));
    std::fill_n(g_occ_frame, kOccRing, uint64_t(0));
    g_ms_sum = g_rt_ms_sum = 0; g_ms_count = g_rt_ms_count = 0;
    pass_timing_reset();
    g_rt_frames_traced = g_rt_frames_stale = g_rt_frames_missing = 0;
    g_tr_ms[0].clear(); g_tr_ms[1].clear(); g_rf_ms.clear(); g_ls_ms.clear(); g_ls_join_ms.clear();
    std::fill_n(g_stats_sum, kStatsCounters, uint64_t(0)); g_stats_n = 0;
    std::fill_n(g_guard_sum, 4, uint64_t(0)); std::fill_n(g_unk_sum, kUnknownPasses, uint64_t(0)); g_guard_frames = 0;
    g_epoch_motion = CameraMotionWindow{}; g_rfh = RfHitWindow{}; g_hem = HitEmWindow{}; g_occ_win = occ::OccWindow{};
    mon.reset_window(g_builds.started_total(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count());
    water_stats_reset(); em_stats_reset();
  }
  const double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
  bg3::pipe_async::Monitor::Window w;
  bg3::pipe_async::Monitor::Burst b;
  const int native_frames = host->cfg_int("rt_timing_frames", 120);
  const int frames = int(stats_period(unsigned(native_frames > 0 ? native_frames : 120)));
  const unsigned ev = mon.tick(g_builds.pending(), g_builds.started_total(), now, unsigned(frames > 0 ? frames : 120), &w, &b);
  if (ev & bg3::pipe_async::Monitor::kWindow)
    log_stats("gi: background builds (%u frames): %u frames waiting, %zu pending, %u started", w.frames, w.waiting,
              w.pending, w.started);
  if (ev & bg3::pipe_async::Monitor::kBurst)
    host->log("gi: background builds done: %u builds in %.0f ms over %llu presents, longest present interval %.0f ms",
              b.builds, b.ms, (unsigned long long)b.frames, b.longest_ms);
}

void gi_release() {
  g_builds.join();   // the background builds read the layouts released below; their unclaimed pipelines are discarded
  g_prewarmed.clear();
  g_prewarm_count = 0;
  g_omm_pipelines.clear();
  bg3::omm::release_scene_consumer(static_cast<uint32_t*>(host->persist(bg3::omm::scene_capability_key,sizeof(uint32_t))));
  std::lock_guard<std::mutex> lock(g_mu);
  release_images();
  lsl_release();
  g_lsl_ok = false;
  sunt_release();
  g_sunt_ok = false;
  for (Pipe* p : {&g_hit_gi, &g_hit_gi_sharc, &g_hit_gi_sun, &g_hit_gi_sun_sharc, &g_hit_two_bounce, &g_hit_direct, &g_hit_material, &g_pan_primary, &g_pan_ls, &g_trace, &g_temporal, &g_ao_apply, &g_composite, &g_contact, &g_rt_shadow, &g_rt_debug,
                  &g_trace_rt, &g_ls, &g_ls_prep, &g_hair_fibre, &g_composite_fibre, &g_rf, &g_rt_origin, &g_ls_match, &g_ls_full, &g_ls_ref, &g_ls_check, &g_ls_full_check, &g_ls_box, &g_ls_full_box, &g_ls_native_full, &g_ls_native_full_box,
                  &g_ls_k, &g_ls_box_k, &g_ls_full_k, &g_ls_full_box_k, &g_occ, &g_ls_full_box_occ,
                  &g_sun_resolve, &g_trace_rt_sharc, &g_rf_sharc, &g_sharc_resolve, &g_sharc_inject, &g_sharc_inject_lock,
                  &g_sharc_debug, &g_sharc_roof}) {
    for (const auto& [slot, variant] : p->variants) host->defer_destroy(BG3_DEFER_PIPELINE, (uint64_t)variant.pipeline);
    *p = Pipe{};
  }
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_rf_queries);
  g_rf_queries = VK_NULL_HANDLE;
  g_rf_ms.clear();
  defer_layout_destroy(uint64_t(g_rf_layout));
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_rf_set_layout);
  g_rf_layout = VK_NULL_HANDLE;
  g_rf_set_layout = VK_NULL_HANDLE;
  g_rf_game_layout = VK_NULL_HANDLE;
  g_rf_hit_logged = g_rf_hit_contract_logged = false;
  rf_gi_drop();
  g_rg_logged = -1;
  g_rg_ran = false;
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_POOL, (uint64_t)g_rg_pool);   // frees g_rg_set
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_rg_dsl);
  host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)g_rg_buf);
  host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_rg_mem);
  g_rg_pool = VK_NULL_HANDLE, g_rg_dsl = VK_NULL_HANDLE, g_rg_buf = VK_NULL_HANDLE, g_rg_mem = VK_NULL_HANDLE;
  g_rg_set = VK_NULL_HANDLE;
  hit_em_drop();
  g_hit_em_logged = false;
  g_hem = HitEmWindow{};
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_queries);
  g_queries = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_pass_queries);
  g_pass_queries = VK_NULL_HANDLE;
  pass_timing_reset();
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_rt_queries);
  g_rt_queries = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_tr_queries);
  g_tr_queries = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_ls_queries);
  g_ls_queries = VK_NULL_HANDLE;
  g_ls_ms.clear();
  defer_layout_destroy(uint64_t(g_ls_layout));
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_ls_set_layout);
  g_ls_layout = VK_NULL_HANDLE;
  g_ls_set_layout = VK_NULL_HANDLE;
  defer_layout_destroy(uint64_t(g_ls_box_layout));
  g_ls_box_layout = VK_NULL_HANDLE;
  g_ls_box_game = VK_NULL_HANDLE;
  g_ls_box_active = g_ls_box_missing_logged = false;
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_POOL, (uint64_t)g_bx_pool);   // frees g_bx_set
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_bx_dsl);
  host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)g_bx_buf);
  host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_bx_mem);
  g_bx_pool = VK_NULL_HANDLE, g_bx_dsl = VK_NULL_HANDLE, g_bx_buf = VK_NULL_HANDLE, g_bx_mem = VK_NULL_HANDLE;
  g_bx_set = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_PIPELINE_LAYOUT, (uint64_t)g_occ_layout);
  for (VkBuffer* bf : {&g_occ_buf, &g_occ_host}) host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)*bf), *bf = VK_NULL_HANDLE;
  for (VkDeviceMemory* m : {&g_occ_mem, &g_occ_host_mem}) host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)*m), *m = VK_NULL_HANDLE;
  g_occ_layout = VK_NULL_HANDLE;
  g_occ_map = nullptr;
  g_occ_armed = 0;
  g_occ_win = occ::OccWindow{};
  g_occ_logged = false;
  std::memset(g_occ_frame, 0, sizeof g_occ_frame);
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
  defer_layout_destroy(uint64_t(g_layout));
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_set_layout);
  defer_layout_destroy(uint64_t(g_rt_layout));
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_rt_set_layout);
  g_layout = VK_NULL_HANDLE;
  g_set_layout = VK_NULL_HANDLE;
  g_rt_layout = VK_NULL_HANDLE;
  g_rt_set_layout = VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT,uint64_t(g_hit_gi_set_layout));g_hit_gi_set_layout=VK_NULL_HANDLE;
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT,uint64_t(g_hit_reference_set_layout));g_hit_reference_set_layout=VK_NULL_HANDLE;
  for (VkPipelineLayout* l : {&g_hit_reference_layout, &g_hit_gi_plain_layout, &g_hit_gi_layout, &g_hit_gi_sun_layout, &g_hit_gi_sun_sharc_layout, &g_rt_sharc_layout, &g_rf_sharc_layout}) {
    defer_layout_destroy(uint64_t(*l));
    *l = VK_NULL_HANDLE;
  }
  host->defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT, (uint64_t)g_empty_dsl);
  g_empty_dsl = VK_NULL_HANDLE;
  sharc_release();
  g_sharc_logged = g_sharc_missing_logged = g_sharc_paused_logged = g_sharc_em_logged = false;
  g_pending.clear();
}

// The background builds for the other core passes (core.h; core/water.cpp make_pipe).
int core_pipeline_async(const char* req, const char* entry, VkPipelineLayout layout, VkPipeline* out, uint64_t* generation,
                        const VkSpecializationInfo* specialization) {
  bg3::pipe_async::Result r{};
  PipelineSpecs specs;
  if (specialization) {
    for (uint32_t i = 0; i < specialization->mapEntryCount; ++i) {
      const auto& m = specialization->pMapEntries[i];
      uint32_t value;
      std::memcpy(&value, static_cast<const char*>(specialization->pData) + m.offset, sizeof(value));
      specs.add(m.constantID, value);
    }
  }
  std::lock_guard<std::recursive_mutex> lock(g_prewarm_mu);
  const std::string q = req, e = entry;
  const std::string key = build_key(q, e, layout, specs);
  const auto st = g_builds.poll(key, uint64_t(layout), [q, e, layout, specs] { return build_pipeline(q, e, layout, specs); }, &r);
  prewarm_experimental(q, e, layout, specs);
  if (st == bg3::pipe_async::State::kStarted) host->log("%s %s: building in the background", req, entry);
  if (st == bg3::pipe_async::State::kReady) {
    *out = VkPipeline(r.pipeline);
    *generation = r.generation;
    return 1;
  }
  return st == bg3::pipe_async::State::kFailed ? -1 : 0;
}
void core_defer_layout_destroy(VkPipelineLayout layout) { defer_layout_destroy(uint64_t(layout)); }
