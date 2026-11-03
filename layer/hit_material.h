#pragma once
#include "host.h"
#include "hit_material_submit.h"
#include "../common/native_hit_records.h"
namespace bg3::native_hit {
// Internal main-AlphaDSL extension. Never part of the public host/core ABI.
inline constexpr uint32_t cache_slots=nh_cache_slots,sampler_slots=nh_sampler_slots,record_bytes=256,max_records=nh_alpha_rows;
bool enabled();
// Invalidate retained pak UV candidates before a game buffer handle can be reused.
void pak_uv_forget_buffer(VkBuffer);
// Bytes the recycled-buffer pool retains (rt_vram_guard breakdown and yield 2).
uint64_t recycled_bytes();
// rt_hit_material_instanced at startup: the G-buffer VS of an instanced wind / 2S pair gets the
// N/B/T XFB frame patch (common/native_hit_instanced.h).
bool instanced_patch_vs(uint64_t vs);
// Its N location (UV one below); rt_hit_material_generated at startup: also the generated VSs (native_hit_generated.h).
uint32_t instanced_frame_location(uint64_t vs);
void device_created(DeviceData*);
void device_destroyed(DeviceData*);
void prepass_begin(VkCommandBuffer);
void prepass_observe(VkCommandBuffer,CmdState*,const DrawArgs&);
uint32_t note_opaque_prepass(VkCommandBuffer,CmdState*,const DrawArgs&,VkDeviceSize rawOffset,uint32_t stride,uint32_t triangles);
void note_prepass(VkCommandBuffer,CmdState*,const DrawArgs&,uint32_t record,VkDeviceSize rawOffset,uint32_t stride,uint32_t triangles);
// Called only after rtscene selects an actually retired slot, before conversion.
void scene_prepare(VkCommandBuffer,uint32_t slot,const RtCapture&);
bool attach(VkCommandBuffer,uint32_t slot,VkDescriptorSet,VkBuffer dummyBuffer,VkImageView dummyArray,VkSampler dummySampler,uint32_t sceneGeneration);
bool deferred_begin(VkCommandBuffer,CmdState*,const DrawArgs&,bool exactPipeline,uint32_t stride,
                    VkBuffer* buffer,VkDeviceSize* offset,VkDeviceSize* bytes);
void deferred_finish(VkCommandBuffer,bool noGraphicsStorageWrites);
void slot_in_use(uint32_t slot,uint64_t frame);
bool slot_reusable(uint32_t slot,uint64_t gpuDone);
bool register_use(VkCommandBuffer,uint32_t slot,VkDescriptorSet* query_alpha);
// Pool material records (common/native_hit_pool.h, PLAN_M4 "Hit material on pool
// instances"). Geometry/position keys of a class A draw (false: not keyable).
bool pool_keys(const InstPipe&,const CmdState&,const DrawArgs&,uint64_t* geometry,uint64_t* position);
// Every G-buffer draw of a class A pipeline; poolPipeline: pool::pool_material_pipeline;
// basisPipeline: it also has the XFB patch with the UV + N/B/T outputs (stride >= 64). True:
// the caller binds and begins this XFB range for the draw (one UV/basis capture of instance 0).
bool pool_observe(VkCommandBuffer,CmdState*,const DrawArgs&,const InstPipe&,bool poolPipeline,bool basisPipeline,
                  uint32_t stride,VkBuffer* xfb,VkDeviceSize* offset,VkDeviceSize* bytes);
// The pool mesh of a cached draw as the BVH build has it (epoch: bvh_pool_epoch, under its lock).
struct PoolMesh {uint64_t gen{};VkDeviceSize pool_off{};uint32_t tris{},stride{};VkDeviceSize basis_off{},basis_bytes{};uint64_t epoch{};};
// status: pool::Status (counted per instance); slot: its pool material row (nh_pool_rows: none);
// wants_basis: qualified without a UV/basis range (the build allocates one in the pool).
struct PoolUse {uint32_t status{};uint32_t slot{UINT32_MAX};bool wants_basis{};};
PoolUse pool_lookup(uint64_t geometry,uint64_t position,uint32_t instances,const PoolMesh&);
void pool_evicted(uint64_t geometry);
// BVH owns the outer lock. Freeze prevents new basis jobs while an immutable
// pool copy is pending; it succeeds only after all prior jobs are retired.
bool pool_compact_freeze();
void pool_compact_thaw();
void pool_compact_scene(uint32_t slot);
void pool_compact_rebase(uint64_t geometry, uint64_t gen, VkDeviceSize position,
                         VkDeviceSize basis, uint64_t epoch);
}
