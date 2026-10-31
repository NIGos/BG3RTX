// Current native-cascade sun scene. All Vulkan work is recorded privately and
// inserted ONLY after exact producer generations are found earlier in the SAME
// submit entry. A rejected insertion leaves a zero ready stamp and native fallback.
#include "host.h"
#include "../common/camera_epoch.h"
#include "../common/cascade_casters.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace bg3 {
namespace {
DeviceData* g_sun_dd=nullptr;
std::mutex g_sun_mutex;
struct Work {
  VkCommandPool pool=VK_NULL_HANDLE;
  VkCommandBuffer build=VK_NULL_HANDLE, consumer=VK_NULL_HANDLE;
  uint64_t consumer_record=0;
  SunBaseCapture base;
  std::array<SunCascadeCapture,4> cascades;
  SunReceiverCapture receiver;
  sun_coverage::NativeEpoch lighting_epoch;
  VkBuffer lighting_frame=VK_NULL_HANDLE,lighting_camera_buffer=VK_NULL_HANDLE;
  VkDeviceSize lighting_offset=0,lighting_camera_offset=0;
  float lighting_camera[56]={};
  Bg3SunSceneInfo info{};
  bool prepared=false, inserted=false;
};
Work g_work[kSceneSlots];
SunReceiverCapture g_receiver[16];
uint32_t g_receiver_next=0;
VkDescriptorSetLayout g_packet_set=VK_NULL_HANDLE;
VkPipelineLayout g_packet_layout=VK_NULL_HANDLE;
VkPipeline g_packet_pipeline=VK_NULL_HANDLE;
uint64_t g_packet_generation=0;
VkDescriptorSetLayout g_validate_set=VK_NULL_HANDLE;
VkPipelineLayout g_validate_layout=VK_NULL_HANDLE;
VkPipeline g_validate_pipeline=VK_NULL_HANDLE;
uint64_t g_validate_generation=0;
uint64_t g_calls=0,g_prepared=0,g_inserted=0,g_failed_epoch=0,g_failed_material=0,g_failed_order=0;

void barrier(VkCommandBuffer cmd,VkPipelineStageFlags src,VkAccessFlags sa,VkPipelineStageFlags dst,VkAccessFlags da) {
  VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=sa;b.dstAccessMask=da;
  g_sun_dd->vk.CmdPipelineBarrier(cmd,src,dst,0,1,&b,0,nullptr,0,nullptr);
}
bool ensure_validation() {
  Bg3ShaderBlob blob{};
  if(!shader_get("rt/sun_caster_validate.hlsl","main","cs_6_6",&blob)) return false;
  if(g_validate_pipeline && g_validate_generation==blob.generation) return true;
  const auto& vk=g_sun_dd->vk;
  if(!g_validate_set) {
    VkDescriptorSetLayoutBinding bindings[4]={{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
      {3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    si.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;si.bindingCount=4;si.pBindings=bindings;
    if(vk.CreateDescriptorSetLayout(g_sun_dd->device,&si,nullptr,&g_validate_set)!=VK_SUCCESS) return false;
  }
  if(!g_validate_layout) {
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,16};
    VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pi.setLayoutCount=1;pi.pSetLayouts=&g_validate_set;pi.pushConstantRangeCount=1;pi.pPushConstantRanges=&push;
    if(vk.CreatePipelineLayout(g_sun_dd->device,&pi,nullptr,&g_validate_layout)!=VK_SUCCESS) return false;
  }
  VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=blob.size_bytes;mi.pCode=blob.code;
  VkShaderModule module=VK_NULL_HANDLE;
  if(vk.CreateShaderModule(g_sun_dd->device,&mi,nullptr,&module)!=VK_SUCCESS) return false;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.layout=g_validate_layout;ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=module;ci.stage.pName="main";
  VkPipeline pipeline=VK_NULL_HANDLE;
  const auto result=vk.CreateComputePipelines(g_sun_dd->device,VK_NULL_HANDLE,1,&ci,nullptr,&pipeline);
  vk.DestroyShaderModule(g_sun_dd->device,module,nullptr);
  if(result==VK_SUCCESS) {
    if(g_validate_pipeline) defer_destroy(BG3_DEFER_PIPELINE,uint64_t(g_validate_pipeline));
    g_validate_pipeline=pipeline;g_validate_generation=blob.generation;
  }
  return result==VK_SUCCESS;
}
bool ensure_packet_pipeline() {
  Bg3ShaderBlob blob{};
  if(!shader_get("rt/sun_receiver_packet.hlsl","main","cs_6_6",&blob)) return false;
  if(g_packet_pipeline && g_packet_generation==blob.generation) return true;
  const auto& vk=g_sun_dd->vk;
  if(!g_packet_set) {
    VkDescriptorSetLayoutBinding bindings[13]{};
    for(unsigned i=0;i<13;++i) bindings[i]={i,i==12?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;ci.bindingCount=13;ci.pBindings=bindings;
    if(vk.CreateDescriptorSetLayout(g_sun_dd->device,&ci,nullptr,&g_packet_set)!=VK_SUCCESS) return false;
  }
  if(!g_packet_layout) {
    VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};ci.setLayoutCount=1;ci.pSetLayouts=&g_packet_set;
    if(vk.CreatePipelineLayout(g_sun_dd->device,&ci,nullptr,&g_packet_layout)!=VK_SUCCESS) return false;
  }
  VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=blob.size_bytes;mi.pCode=blob.code;
  VkShaderModule module=VK_NULL_HANDLE;
  if(vk.CreateShaderModule(g_sun_dd->device,&mi,nullptr,&module)!=VK_SUCCESS) return false;
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.layout=g_packet_layout;
  ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
  ci.stage.module=module;ci.stage.pName="main";
  VkPipeline pipeline=VK_NULL_HANDLE;auto result=vk.CreateComputePipelines(g_sun_dd->device,VK_NULL_HANDLE,1,&ci,nullptr,&pipeline);
  vk.DestroyShaderModule(g_sun_dd->device,module,nullptr);
  if(result==VK_SUCCESS) {
    if(g_packet_pipeline) defer_destroy(BG3_DEFER_PIPELINE,uint64_t(g_packet_pipeline));
    g_packet_pipeline=pipeline;g_packet_generation=blob.generation;
  }
  return result==VK_SUCCESS;
}
bool frame_still_matches(VkBuffer b,VkDeviceSize off,const sun_coverage::NativeEpoch& epoch) {
  sun_coverage::NativeEpoch now;
  now.available=b && read_mapped(b,off,now.words.data(),sizeof now.words);
  return sun_coverage::same_epoch(now,epoch);
}
bool camera_still_matches(VkBuffer b,VkDeviceSize off,const float* expected) {
  float now[56];return b && read_mapped(b,off,now,sizeof now) && std::memcmp(now,expected,sizeof now)==0;
}
bool compatible_submit(const void* chain) {
  for(auto* p=static_cast<const VkBaseInStructure*>(chain);p;p=p->pNext) {
    if(p->sType==VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO) return false;
    if(p->sType==VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO &&
       reinterpret_cast<const VkProtectedSubmitInfo*>(p)->protectedSubmit) return false;
  }
  return true;
}
VkCommandBuffer take_build(VkCommandBuffer consumer,std::span<const VkCommandBuffer> entry) {
  std::vector<uint64_t> keys;keys.reserve(entry.size());
  for(auto c:entry) keys.push_back(reinterpret_cast<uint64_t>(c));
  for(Work& w:g_work) {
    if(!w.prepared || w.inserted || w.consumer!=consumer) continue;
    bool order=bvh_rec_matches_command(w.base.base.record,w.base.producer) &&
      bvh_rec_matches_command(w.consumer_record,consumer);
    for(const auto& cascade:w.cascades) order=order &&
      sun_coverage::producers_before(keys,reinterpret_cast<uint64_t>(w.base.producer),
        reinterpret_cast<uint64_t>(cascade.soup.producer),reinterpret_cast<uint64_t>(consumer)) &&
      bvh_rec_matches_command(cascade.soup.record,cascade.soup.producer);
    order=order && sun_coverage::producers_before(keys,reinterpret_cast<uint64_t>(w.base.producer),
      reinterpret_cast<uint64_t>(w.receiver.producer),reinterpret_cast<uint64_t>(consumer)) &&
      bvh_rec_matches_command(w.receiver.record,w.receiver.producer);
    if(!order) {++g_failed_order;continue;}
    // Native upload-ring contents can be written late. Revalidate the actual
    // source/consumer packets and projection bytes at submission, not just record.
    if(!frame_still_matches(w.base.native_frame_buffer,w.base.native_frame_offset,w.base.native_epoch) ||
       !frame_still_matches(w.lighting_frame,w.lighting_offset,w.lighting_epoch) ||
       !camera_still_matches(w.base.camera_buffer,w.base.camera_offset,w.base.camera) ||
       !camera_still_matches(w.lighting_camera_buffer,w.lighting_camera_offset,w.lighting_camera)) {
      ++g_failed_epoch;continue;
    }
    bool receiver_owned=false;
    for(const auto& source:g_receiver) if(source.record==w.receiver.record && source.producer==w.receiver.producer) receiver_owned=true;
    bool fresh=receiver_owned && geo_sun_captures_owned(w.cascades);
    for(const auto& cascade:w.cascades) fresh=fresh &&
      frame_still_matches(cascade.native_frame_buffer,cascade.native_frame_offset,cascade.native_epoch) &&
      (!cascade.camera_mapped || camera_still_matches(cascade.camera_buffer,cascade.camera_offset,cascade.camera));
    if(!fresh) {++g_failed_epoch;continue;}
    w.inserted=true;++g_inserted;
    return w.build;
  }
  return VK_NULL_HANDLE;
}
void stats() {
  if(g_calls && g_calls%300==0)
    log("[sun current] calls %llu, recorded %llu, same-submit inserted %llu; rejected epoch/late-write %llu, material/geometry %llu, order/generation %llu; full finite-ray volume test required",
      (unsigned long long)g_calls,(unsigned long long)g_prepared,(unsigned long long)g_inserted,
      (unsigned long long)g_failed_epoch,(unsigned long long)g_failed_material,(unsigned long long)g_failed_order);
}
} // namespace

void sun_note_native_draw(VkCommandBuffer cmd,const CmdState* state) {
  if(!state || !state->in_pass || state->fs!=0x0687fa327aed7f8eull || !cfg_int("rt_sun_cascade_current",0)) return;
  SunReceiverCapture capture;
  capture.producer=cmd;
  auto packet=[&](uint32_t set,uint32_t binding,uint32_t offset,uint32_t size,SunReceiverPacket& p) {
    p.size=size;
    if(!query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,set,binding,0,&p.source) || !p.source.buffer ||
       (p.source.type!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && p.source.type!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) ||
       (p.source.range!=VK_WHOLE_SIZE && (offset>p.source.range || size>p.source.range-offset))) return false;
    p.source.offset+=offset;
    if(p.source.range!=VK_WHOLE_SIZE) p.source.range-=offset;
    p.mapped=read_mapped(p.source.buffer,p.source.offset,p.bytes.data(),size);
    return true;
  };
  capture.sources_valid=packet(0,0,0,32,capture.frame);
  capture.sources_valid=packet(0,2,0,224,capture.camera) && capture.sources_valid;
  capture.sources_valid=packet(2,26,0,544,capture.sun) && capture.sources_valid;
  capture.sources_valid=packet(1,0,0,144,capture.auxiliary) && capture.sources_valid;
  capture.sources_valid=packet(0,1,112,48,capture.ray_basis) && capture.sources_valid;
  capture.sources_valid=query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,2,8,0,&capture.csm) &&
    capture.csm.image && capture.csm.view && capture.sources_valid;
  // Merely bound auxiliary textures are NOT evidence that native auxiliary
  // shadows are enabled. Preserve actual enable/selection bytes above.
  query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,3,0,&capture.auxiliary_near);
  query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,4,0,&capture.auxiliary_far);
  if(capture.frame.mapped) {
    std::memcpy(capture.epoch.words.data(),capture.frame.bytes.data(),32);
    capture.epoch.available=true;
  }
  capture.record=bvh_rec_register(cmd);
  std::lock_guard<std::mutex> lock(g_sun_mutex);
  g_receiver[g_receiver_next++%std::size(g_receiver)]=std::move(capture);
}
bool sun_native_receiver(const sun_coverage::NativeEpoch& epoch,SunReceiverCapture* out) {
  if(!out || !sun_coverage::valid(epoch)) return false;
  *out={};
  std::lock_guard<std::mutex> lock(g_sun_mutex);
  const SunReceiverCapture* best=nullptr;
  for(const auto& source:g_receiver)
    if(source.sources_valid && sun_coverage::same_epoch(source.epoch,epoch) &&
       bvh_rec_matches_command(source.record,source.producer) && (!best || source.record>best->record)) best=&source;
  if(!best) return false;
  *out=*best;return true;
}

bool rt_sun_scene(VkCommandBuffer cmd,VkAccelerationStructureKHR selected_tlas,VkDescriptorSet selected_alpha,
                   Bg3SunSceneInfo* out) {
  if(!out) return false;*out={};
  if(!cfg_int("rt_sun_cascade_current",0) || !cfg_int("rt_cascade_casters",0) || !cmd) return false;
  std::unique_lock<std::mutex> lock(g_sun_mutex);
  ++g_calls;stats();
  DeviceData* dd=main_device();if(!dd || dd->graphics_family==UINT32_MAX) return false;
  if(g_sun_dd && g_sun_dd!=dd) return false;g_sun_dd=dd;
  SunBaseCapture base;
  sun_coverage::NativeEpoch lighting_epoch;
  VkBuffer lighting_frame=VK_NULL_HANDLE;VkDeviceSize lighting_offset=0;
  Bg3Binding lighting_cb{};float lighting_camera[56];
  if(!rt_sun_base_capture(selected_tlas,selected_alpha,&base) ||
     !geo_native_frame_epoch(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,&lighting_epoch,&lighting_frame,&lighting_offset) ||
     !sun_coverage::same_epoch(base.native_epoch,lighting_epoch) ||
     !query_binding(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,0,2,0,&lighting_cb) || !lighting_cb.buffer ||
     (lighting_cb.range!=VK_WHOLE_SIZE && lighting_cb.range<224) ||
     !read_mapped(lighting_cb.buffer,lighting_cb.offset,lighting_camera,sizeof lighting_camera) ||
     !same_camera_pose(base.camera,lighting_camera)) {++g_failed_epoch;return false;}
  std::array<SunCascadeCapture,4> cascades;
  // geo_sun_cascades may wait (rt_scene_wait_ms) for the cascade passes recorded on other
  // threads: g_sun_mutex is not held across that wait (every graphics submit takes it in
  // sun_prepare_submit). Only locals carry over; the device is checked again after it.
  lock.unlock();
  const bool have_cascades=geo_sun_cascades(lighting_epoch,&cascades);
  lock.lock();
  if(g_sun_dd!=dd) return false;
  if(!have_cascades) {++g_failed_epoch;return false;}
  for(const auto& c:cascades) {
    if(!c.geometry_complete || !c.materials_complete || !c.finite_native_clip || !c.native_raster_valid || !c.fresh_depth || !c.camera_buffer) {++g_failed_material;return false;}
    if(base.producer==cmd || c.soup.producer==cmd) {++g_failed_order;return false;}
  }
  const SunReceiverCapture* native=nullptr;
  bool native_epoch_match=false;
  for(const auto& source:g_receiver) {
    if(!source.sources_valid || source.csm.image!=cascades[0].depth_image ||
       !bvh_rec_matches_command(source.record,source.producer)) continue;
    const bool match=sun_coverage::same_epoch(source.epoch,lighting_epoch);
    if(!native || (match && !native_epoch_match) || (match==native_epoch_match && source.record>native->record)) {
      native=&source;native_epoch_match=match;
    }
  }
  if(!native || native->producer==cmd) {++g_failed_epoch;return false;}
  // Exact sampled layer/mip mapping, not just an image handle with a different
  // array-view offset. GPU packet matching below validates actual UBO contents.
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    auto view=tracker().views.find(native->csm.view);
    if(view==tracker().views.end() || view->second.range.baseMipLevel!=0 ||
       view->second.range.baseArrayLayer!=0 || view->second.range.layerCount<4) {++g_failed_material;return false;}
  }
  Bg3Binding lighting_rays{};
  if(!query_binding(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,0,1,0,&lighting_rays) || !lighting_rays.buffer ||
     (lighting_rays.range!=VK_WHOLE_SIZE && lighting_rays.range<160) || native->ray_basis.source.offset<112) return false;
  Work& w=g_work[base.slot];
  if(w.prepared && w.base.base.record==base.base.record) {
    if(w.consumer!=cmd || w.receiver.record!=native->record || !bvh_rec_matches_command(w.consumer_record,cmd)) return false;
    for(unsigned layer=0;layer<4;++layer) if(w.cascades[layer].soup.record!=cascades[layer].soup.record) return false;
    *out=w.info;return true;
  }
  VkPhysicalDeviceProperties physical{};
  if(!dd->inst || !dd->inst->GetPhysicalDeviceProperties) return false;
  dd->inst->GetPhysicalDeviceProperties(dd->physical,&physical);
  const auto alignment=physical.limits.minUniformBufferOffsetAlignment;
  if(!alignment || physical.limits.maxPerStageDescriptorUniformBuffers<12 || physical.limits.maxDescriptorSetUniformBuffers<12 ||
     physical.limits.maxPerStageDescriptorStorageBuffers<1 || physical.limits.maxUniformBufferRange<544) return false;
  auto valid_uniform=[&](VkBuffer buffer,VkDeviceSize offset,VkDeviceSize bytes) {
    return offset%alignment==0 && geo_uniform_buffer_range(buffer,offset,bytes);
  };
  for(const auto* p:{&native->sun,&native->auxiliary,&native->camera,&native->frame})
    if(!valid_uniform(p->source.buffer,p->source.offset,p->size)) return false;
  if(!valid_uniform(native->ray_basis.source.buffer,native->ray_basis.source.offset-112,160) ||
     !valid_uniform(lighting_cb.buffer,lighting_cb.offset,224) || !valid_uniform(lighting_frame,lighting_offset,32) ||
     !valid_uniform(lighting_rays.buffer,lighting_rays.offset,160)) return false;
  for(const auto& c:cascades) if(!valid_uniform(c.camera_buffer,c.camera_offset,224)) return false;
  auto props2=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(dd->inst->gipa(dd->inst->instance,"vkGetPhysicalDeviceProperties2"));
  if(!props2) return false;
  VkPhysicalDevicePushDescriptorPropertiesKHR push_limits{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR};
  VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&push_limits;
  props2(dd->physical,&properties);if(push_limits.maxPushDescriptors<13) return false;
  if(!ensure_validation() || !ensure_packet_pipeline()) return false;
  const auto& vk=dd->vk;
  if(!w.pool) {
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex=dd->graphics_family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if(vk.CreateCommandPool(dd->device,&pi,nullptr,&w.pool)!=VK_SUCCESS) return false;
  }
  if(!w.build) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool=w.pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    if(vk.AllocateCommandBuffers(dd->device,&ai,&w.build)!=VK_SUCCESS) return false;
    if(dd->set_loader_data) dd->set_loader_data(dd->device,w.build);
  }
  if(!w.build) return false;
  // A different base record in this same slot means the SceneSlot owner already
  // proved retirement. Preserve the source slot and all BLAS/alpha references.
  w.prepared=false;w.inserted=false;bvh_cmd_reset(w.build);
  if(vk.ResetCommandBuffer(w.build,0)!=VK_SUCCESS) return false;
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if(vk.BeginCommandBuffer(w.build,&begin)!=VK_SUCCESS) return false;
  AlphaSunClone alpha;
  if(!alpha_prepare_sun_clone(w.build,base.slot,selected_alpha,cascades,&alpha)) return false;
  barrier(w.build,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  std::array<uint32_t,308> parameters{};
  parameters[1200/4]=0;parameters[1204/4]=15;parameters[1208/4]=native->csm.width;parameters[1212/4]=native->csm.height;
  for(unsigned layer=0;layer<4;++layer) if(cascades[layer].native_viewport.height<0) parameters[1216/4]|=1u<<layer;
  vk.CmdUpdateBuffer(w.build,alpha.parameters,0,sizeof parameters,parameters.data());
  barrier(w.build,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  const auto& n=*native;
  VkDescriptorBufferInfo packet_buffers[13]={
    {n.sun.source.buffer,n.sun.source.offset,544},{n.auxiliary.source.buffer,n.auxiliary.source.offset,144},
    {n.camera.source.buffer,n.camera.source.offset,224},{n.frame.source.buffer,n.frame.source.offset,32},
    {n.ray_basis.source.buffer,n.ray_basis.source.offset-112,160},
    {lighting_cb.buffer,lighting_cb.offset,224},{lighting_frame,lighting_offset,32},
    {lighting_rays.buffer,lighting_rays.offset,160}};
  for(unsigned layer=0;layer<4;++layer) packet_buffers[8+layer]={cascades[layer].camera_buffer,cascades[layer].camera_offset,192};
  packet_buffers[12]={alpha.parameters,0,1232};
  VkWriteDescriptorSet packet_writes[13]{};
  for(unsigned i=0;i<13;++i) {
    packet_writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};packet_writes[i].dstBinding=i;
    packet_writes[i].descriptorCount=1;packet_writes[i].descriptorType=i==12?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    packet_writes[i].pBufferInfo=&packet_buffers[i];
  }
  vk.CmdBindPipeline(w.build,VK_PIPELINE_BIND_POINT_COMPUTE,g_packet_pipeline);
  vk.CmdPushDescriptorSet(w.build,VK_PIPELINE_BIND_POINT_COMPUTE,g_packet_layout,0,13,packet_writes);
  vk.CmdDispatch(w.build,1,1,1);
  barrier(w.build,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
  BvhSunCloneResult result;
  if(!bvh_build_sun_clone(w.build,base.slot,base.base,cascades,base.camera+52,g_validate_pipeline,g_validate_layout,alpha.stamp,alpha.parameters,&result)) return false;
  if(!alpha_sun_bind_scene(base.slot,result.tlas)) return false;
  const auto& cascade=cascades[0];
  Bg3SunSceneInfo info{};
  info.tlas=result.tlas;info.reference_tlas=result.tlas;info.alpha_set=alpha.set;info.alpha_layout=alpha.layout;info.build_identity=result.record;
  info.camera_record=base.base.record;info.cascade_record=cascade.soup.record;
  info.flags=BG3_SUN_SCENE_RECORDED|BG3_SUN_SCENE_GEOMETRY_COMPLETE|BG3_SUN_SCENE_MATERIALS_COMPLETE|
             BG3_SUN_SCENE_VOLUME_VALID|BG3_SUN_SCENE_NATIVE_EPOCH_VALID;
  std::memcpy(info.native_epoch,lighting_epoch.words.data(),sizeof info.native_epoch);
  float offset_error=0;
  constexpr double u=std::numeric_limits<float>::epsilon()/2.;
  constexpr double gamma=12.*u/(1.-12.*u);
  for(unsigned axis=0;axis<3;++axis) {
    double offset=cascade.matrix[3][axis],magnitude=std::abs(offset);
    for(unsigned k=0;k<3;++k) {
      const double c=cascade.matrix[k][axis];info.clip_from_lighting[axis][k]=float(c);
      offset+=c*(double(lighting_camera[52+k])-cascade.soup.origin[k]);
      magnitude+=std::abs(c)*(std::abs(double(lighting_camera[52+k]))+std::abs(double(cascade.soup.origin[k])));
    }
    info.clip_from_lighting[axis][3]=float(offset);
    offset_error=(std::max)(offset_error,std::nextafter(float(gamma*magnitude),INFINITY));
    if(!std::isfinite(offset_error) || !std::isfinite(info.clip_from_lighting[axis][3])) return false;
  }
  vk.CmdUpdateBuffer(w.build,alpha.stamp,24,4,&info.flags);
  vk.CmdUpdateBuffer(w.build,alpha.stamp,28,4,&offset_error);
  vk.CmdUpdateBuffer(w.build,alpha.stamp,32,sizeof info.clip_from_lighting,info.clip_from_lighting);
  vk.CmdUpdateBuffer(w.build,alpha.stamp,80,sizeof info.native_epoch,info.native_epoch);
  vk.CmdFillBuffer(w.build,alpha.stamp,20,4,alpha.expected_generation);
  barrier(w.build,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
    VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
  if(vk.EndCommandBuffer(w.build)!=VK_SUCCESS) return false;
  w.consumer=cmd;w.consumer_record=bvh_rec_register(cmd);w.base=base;w.cascades=cascades;w.receiver=*native;
  w.lighting_epoch=lighting_epoch;w.lighting_frame=lighting_frame;w.lighting_offset=lighting_offset;
  w.lighting_camera_buffer=lighting_cb.buffer;w.lighting_camera_offset=lighting_cb.offset;
  std::memcpy(w.lighting_camera,lighting_camera,sizeof lighting_camera);
  w.info=info;w.prepared=true;++g_prepared;*out=info;return true;
}

void sun_prepare_submit(DeviceData* dd,VkQueue queue,uint32_t count,const VkSubmitInfo* input,SunSubmitBatch* out) {
  if(!out || !count || !input || !dd || !cfg_int("rt_sun_cascade_current",0) || queue!=dd->graphics_queue) return;
  std::lock_guard<std::mutex> lock(g_sun_mutex);
  if(dd!=g_sun_dd) return;
  out->legacy.assign(input,input+count);out->legacy_commands.resize(count);
  for(uint32_t i=0;i<count;++i) {
    const auto& s=input[i];auto& commands=out->legacy_commands[i];
    const std::span<const VkCommandBuffer> native(s.pCommandBuffers,s.commandBufferCount);
    for(auto consumer:native) {
      if(compatible_submit(s.pNext)) if(auto build=take_build(consumer,native)) {commands.push_back(build);out->modified=true;}
      commands.push_back(consumer);
    }
    out->legacy[i].commandBufferCount=uint32_t(commands.size());out->legacy[i].pCommandBuffers=commands.data();
    out->commands.insert(out->commands.end(),commands.begin(),commands.end());
  }
}
void sun_prepare_submit2(DeviceData* dd,VkQueue queue,uint32_t count,const VkSubmitInfo2* input,SunSubmitBatch* out) {
  if(!out || !count || !input || !dd || !cfg_int("rt_sun_cascade_current",0) || queue!=dd->graphics_queue) return;
  std::lock_guard<std::mutex> lock(g_sun_mutex);
  if(dd!=g_sun_dd) return;
  out->modern.assign(input,input+count);out->modern_commands.resize(count);out->modern_signals.resize(count);
  for(uint32_t i=0;i<count;++i) {
    const auto& s=input[i];auto& commands=out->modern_commands[i];std::vector<VkCommandBuffer> native;
    for(uint32_t j=0;j<s.commandBufferInfoCount;++j) native.push_back(s.pCommandBufferInfos[j].commandBuffer);
    bool modified=false;
    for(uint32_t j=0;j<s.commandBufferInfoCount;++j) {
      const auto& c=s.pCommandBufferInfos[j];
      if(!(s.flags&VK_SUBMIT_PROTECTED_BIT) && compatible_submit(s.pNext) && !(c.deviceMask&~1u))
        if(auto build=take_build(c.commandBuffer,native)) {
          VkCommandBufferSubmitInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};b.commandBuffer=build;b.deviceMask=c.deviceMask;
          commands.push_back(b);modified=out->modified=true;
        }
      commands.push_back(c);
    }
    out->modern[i].commandBufferInfoCount=uint32_t(commands.size());out->modern[i].pCommandBufferInfos=commands.data();
    if(modified && s.signalSemaphoreInfoCount) {
      auto& signals=out->modern_signals[i];signals.assign(s.pSignalSemaphoreInfos,s.pSignalSemaphoreInfos+s.signalSemaphoreInfoCount);
      for(auto& signal:signals) signal.stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
      out->modern[i].pSignalSemaphoreInfos=signals.data();
    }
    for(const auto& c:commands) out->commands.push_back(c.commandBuffer);
  }
}
void sun_device_destroyed(DeviceData* dd) {
  std::lock_guard<std::mutex> lock(g_sun_mutex);if(g_sun_dd && dd!=g_sun_dd) return;
  for(auto& receiver:g_receiver) receiver=SunReceiverCapture{};
  for(Work& w:g_work) {if(w.pool) defer_destroy(BG3_DEFER_COMMAND_POOL,uint64_t(w.pool));w=Work{};}
  if(g_packet_pipeline) defer_destroy(BG3_DEFER_PIPELINE,uint64_t(g_packet_pipeline));
  if(g_packet_layout) defer_destroy(BG3_DEFER_PIPELINE_LAYOUT,uint64_t(g_packet_layout));
  if(g_packet_set) defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT,uint64_t(g_packet_set));
  g_packet_pipeline=VK_NULL_HANDLE;g_packet_layout=VK_NULL_HANDLE;g_packet_set=VK_NULL_HANDLE;g_packet_generation=0;g_receiver_next=0;
  if(g_validate_pipeline) defer_destroy(BG3_DEFER_PIPELINE,uint64_t(g_validate_pipeline));
  if(g_validate_layout) defer_destroy(BG3_DEFER_PIPELINE_LAYOUT,uint64_t(g_validate_layout));
  if(g_validate_set) defer_destroy(BG3_DEFER_DESCRIPTOR_SET_LAYOUT,uint64_t(g_validate_set));
  g_validate_pipeline=VK_NULL_HANDLE;g_validate_generation=0;g_validate_layout=VK_NULL_HANDLE;g_validate_set=VK_NULL_HANDLE;g_sun_dd=nullptr;
}
} // namespace bg3
