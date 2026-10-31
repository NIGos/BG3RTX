#pragma once
#include "host.h"
#include "hit_material_submit.h"
#include "../common/native_hit_records.h"
namespace bg3::native_hit {
// Internal main-AlphaDSL extension. Never part of the public host/core ABI.
inline constexpr uint32_t cache_slots=nh_cache_slots,sampler_slots=nh_sampler_slots,record_bytes=256,max_records=nh_alpha_rows;
bool enabled();
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
}
