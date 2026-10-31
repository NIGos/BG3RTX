// Shared state of the hot-reloadable core's translation units.
#pragma once
#include "../common/host_api.h"

extern const Bg3HostApi* host;

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
