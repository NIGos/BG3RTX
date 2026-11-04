// Shared state of the hot-reloadable core's translation units.
#pragma once
#include "../common/host_api.h"

extern const Bg3HostApi* host;
#include "../common/stats_window.h"
inline unsigned stats_period(unsigned native) { return bg3::stats_window::period(host->cfg_int, native); }
void water_stats_reset();
void em_stats_reset();

// gi.cpp
void gi_on_dispatch(VkCommandBuffer cmd, const Bg3DispatchInfo* d);
void gi_on_dispatch_pre(VkCommandBuffer cmd, const Bg3DispatchInfo* d);
void gi_on_pass_begin(VkCommandBuffer cmd, const Bg3PassInfo* p);
void gi_on_cmd_end(VkCommandBuffer cmd);
void gi_on_pass_end(VkCommandBuffer cmd, const Bg3PassInfo* p);
void gi_on_frame(uint64_t frame);
void gi_release();

// emissive.cpp (D-EM debug views, rt_debug 6-8): after the tiled lighting, in run_post.
void em_debug(VkCommandBuffer cmd, const Bg3Binding& cam, const Bg3Binding& vp, const Bg3Binding& depth,
              const Bg3Binding& diffuse, const Bg3Binding& specular, const Bg3Binding& cam2, uint32_t w, uint32_t h,
              uint32_t mode);
void em_release();
// em_policy.cpp (ABI 24): the D-EM record and calibration policy (Bg3CoreApi::em_*).
bool core_em_record(const Bg3EmRecordIn* in, Bg3EmRecordOut* out);
bool core_em_calib_update(Bg3EmCalib* cal, uint32_t first, double ratio, uint32_t flags, uint64_t frames, double bright);
void core_em_calib_apply(uint32_t* rec, const Bg3EmCalib* cal);
bool em_check_due();
// Runs of the projected check per full-screen check (rt_em_check_slices; 1 after a view):
// emissive.cpp notes it per run, em_policy.cpp holds it for the calibration.
void em_note_check_slices(uint32_t slices);
uint32_t em_check_slices();
// True once per pass when a supported water draw (water policy, em_record) was recorded in cmd
// since its last pass begin / take: the pass end that follows holds water.
// M5.4b (rt_water_ls_shadows): `lights` (7 entries, kWaterLsBindings order) receives the first water
// draw's own set 2 b0, b10, b11, b12, b26, b27, b28 (its froxel light list, queried at the draw), and
// `lights_ok` whether all seven were captured with the expected types.
inline constexpr uint32_t kWaterLsBindings[7] = {0, 10, 11, 12, 26, 27, 28};
// M5.5 (rt_water_refraction): `copy` receives the first water draw's set 2 b4, the colour copy its FS
// samples as the refraction (a sampled image), `copy_ok` whether it was captured.
bool core_water_pass_take(VkCommandBuffer cmd, Bg3Binding* lights = nullptr, bool* lights_ok = nullptr,
                          Bg3Binding* copy = nullptr, bool* copy_ok = nullptr);

// water.cpp (M5.2 water reflections, rt_water_reflections; docs/PLAN_M5_WATER.md): the trace in
// run_post (after the views), its views (rt_debug 21-24) at the end of the forward water pass.
struct WaterRfIn {
  Bg3Binding cam, vp, mat, dep, dif, spe, cam2;   // the lighting's (core/gi.cpp Snapshot)
  VkAccelerationStructureKHR scene_tlas;          // the TLAS + alpha set the RT passes used
  VkDescriptorSet alpha;
  const void* push;                               // core/gi.cpp Push (gi_common.hlsli), rt_stats 0
  uint32_t push_bytes;
  uint32_t w, h;
  // rt_water_rr_hitdist: RR's specular hit distance image (core/gi.cpp g_rf_hit, R16F) when D-RF wrote it
  // this frame and the key is on, else null (the variant is then not selected).
  uint64_t hit_identity=0;
  VkImageView rf_hit = VK_NULL_HANDLE;
};
bool water_rf_wanted();
void water_rf(VkCommandBuffer cmd, const WaterRfIn& in);
void water_on_pass_end(VkCommandBuffer cmd, const Bg3PassInfo* p);
void water_release();

// gi.cpp: background pipeline builds (common/pipeline_async.h), joined in gi_release. The compute
// pipeline of `req`:`entry` for `layout`: 1 ready (*out, *generation: the caller owns the pipeline),
// 0 building (logged once when it starts), -1 failed (retried after a cooldown).
int core_pipeline_async(const char* req, const char* entry, VkPipelineLayout layout, VkPipeline* out, uint64_t* generation,
                        const VkSpecializationInfo* specialization = nullptr);
// A pipeline layout a background build may still read: destroyed (defer_destroy) once none does.
void core_defer_layout_destroy(VkPipelineLayout layout);
