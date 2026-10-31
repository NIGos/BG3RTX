#include "hit_material.h"
#include "omm_sampler.h"
#include "../common/native_hit_join.h"
#include "../common/native_hit_order.h"
#include "../common/native_hit_inputs.h"
#include "../common/native_hit_resources.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <memory>
#include <set>
namespace bg3::native_hit {
namespace {
constexpr VkDeviceSize stream_bytes=32u<<20;
constexpr VkDeviceSize row_payload=16u+VkDeviceSize(record_bytes)*max_records;
constexpr VkDeviceSize row_bytes=row_payload+16u;
static_assert(ResourceBudget{}.samplers.size()==sampler_slots && ResourceBudget{}.caches.size()==cache_slots);
constexpr VkDeviceSize packet_rows=16u+VkDeviceSize(record_bytes)*material_slots;
struct Buffer {VkBuffer b{};VkDeviceMemory m{};void* mapped{};};
struct Row {
 uint32_t valid{},triangles{},pre{},deferred{};
 uint32_t stride{},table{},sampler{},frame{};
 uint32_t normal{},albedo{},physical{},reserved{};
 float p0[4]{},p1[4]{},t[32]{},parameters[4]{};
 uint32_t padding[8]{};
};
static_assert(sizeof(Row)==record_bytes);
static_assert(offsetof(Row,pre)==8 && offsetof(Row,deferred)==12 && offsetof(Row,stride)==16);
static_assert(offsetof(Row,p0)==48 && offsetof(Row,t)==80 && offsetof(Row,parameters)==208);
struct View {VkImageView view{};VkImage image{};VkImageLayout layout{};uint64_t id{};uint32_t image_id{};};
struct Pre {DrawKey key;AlphaSnapshot alpha;VkDeviceSize raw{};uint32_t stride{},triangles{},record{};uint64_t vs{},fs{};};
struct Material {uint32_t source_raw{},deferred_raw{},snapshot{};Row row;DrawKey key;View table,normal,albedo,physical;omm::BoundSampler sampler;Bg3Binding engine,physical_ubo,tilesets;};
struct Deferred {
 Buffer data,rows;uint64_t command{},recording{},frame{};VkBuffer camera{};VkDeviceSize camera_offset{},cursor{};
 DrawJoin join;std::vector<Material> materials;std::vector<VkSampler> samplers;ResourceBudget resources;
 bool finalized{},valid{true};uint32_t seen{},audited_unique{},audited_duplicate{},family_declined{},pipeline_declined{},binding_declined{};
};
struct Slot {
 Buffer data;VkDescriptorSet set{};uint64_t command{},recording{},frame{};uint32_t generation{};
 VkBuffer camera{};VkDeviceSize camera_offset{},cursor{};VkImageView dummy_array{};VkSampler dummy_sampler{};DrawJoin join;std::vector<Pre> pre;
 std::vector<uint64_t> handles,owners;bool attached{},prepared{};
};
struct Query {
 Buffer data,rows;VkDescriptorPool pool{};VkDescriptorSet set{};
 uint64_t command{},recording{},source{};uint32_t slot{},generation{};std::shared_ptr<Deferred> deferred;
 PendingPublication publication;std::vector<Material> materials;
};
DeviceData* dd{};bool on{};std::mutex mutex;
struct Pending {uint64_t command{},recording{};uint32_t candidate_draws{};DrawJoin join;std::vector<Pre> pre;};
std::unordered_map<uint64_t,Pending> pending;
std::unordered_map<uint64_t,RecordingOwner> owners;
std::unordered_map<uint64_t,VkQueue> submitted_queues;
std::unordered_map<VkCommandBuffer,uint64_t> marked;
std::unordered_map<uint64_t,std::shared_ptr<Deferred>> packets;
std::unordered_map<uint64_t,std::unique_ptr<Query>> queries;
Slot slots[kSceneSlots];
VkDescriptorSetLayout gather_dsl{},join_dsl{};VkPipelineLayout gather_layout{},join_layout{};VkPipeline gather_pipeline{},join_pipeline{};
bool compute_ready(bool join){
 auto& pipeline=join?join_pipeline:gather_pipeline;auto& dsl=join?join_dsl:gather_dsl;auto& layout=join?join_layout:gather_layout;
 if(pipeline)return true;
 Bg3ShaderBlob blob{};if(!dd->vk.CmdPushDescriptorSet || !shader_get(join?"rt/hit_material_join.hlsl":"rt/hit_material_gather.hlsl","main","cs_6_6",&blob))return false;
 VkDescriptorSetLayoutBinding bindings[5]{};uint32_t count=join?5u:4u;
 for(uint32_t i=0;i<count;++i)bindings[i]={i,(!join && i<2)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT};
 VkDescriptorSetLayoutCreateInfo d{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};d.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;d.bindingCount=count;d.pBindings=bindings;
 if(!dsl && dd->vk.CreateDescriptorSetLayout(dd->device,&d,nullptr,&dsl)!=VK_SUCCESS)return false;
 VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,join?32u:16u};VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};l.setLayoutCount=1;l.pSetLayouts=&dsl;l.pushConstantRangeCount=1;l.pPushConstantRanges=&range;
 if(!layout && dd->vk.CreatePipelineLayout(dd->device,&l,nullptr,&layout)!=VK_SUCCESS)return false;
 VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};m.codeSize=blob.size_bytes;m.pCode=blob.code;VkShaderModule module{};
 if(dd->vk.CreateShaderModule(dd->device,&m,nullptr,&module)!=VK_SUCCESS)return false;
 VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};p.layout=layout;p.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main"};
 auto result=dd->vk.CreateComputePipelines(dd->device,VK_NULL_HANDLE,1,&p,nullptr,&pipeline);dd->vk.DestroyShaderModule(dd->device,module,nullptr);return result==VK_SUCCESS;
}
uint64_t handle(VkCommandBuffer cmd){return uint64_t(reinterpret_cast<uintptr_t>(cmd));}
uint64_t queue_handle(VkQueue queue){return uint64_t(reinterpret_cast<uintptr_t>(queue));}
bool one_time(const CmdState* state){return state && state->level==VK_COMMAND_BUFFER_LEVEL_PRIMARY && (state->begin_flags&VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)!=0 && state->recording;}
bool own(VkCommandBuffer cmd){
 auto* state=cmd_state(cmd);if(!one_time(state))return false;
 auto& owner=owners[state->recording];if(!owner.recording){owner.command=handle(cmd);owner.recording=state->recording;}
 if(owner.command!=handle(cmd) || owner.poisoned)return false;marked[cmd]=state->recording;return true;
}
bool mark_owner(VkCommandBuffer cmd,Slot& slot){
 if(!own(cmd))return false;auto id=cmd_state(cmd)->recording;
 if(std::find(slot.owners.begin(),slot.owners.end(),id)==slot.owners.end())slot.owners.push_back(id);return true;
}
uint64_t current_recording(uint64_t command){return cmd_recording(reinterpret_cast<VkCommandBuffer>(uintptr_t(command)));}
DrawKey key(VkCommandBuffer cmd,const CmdState& s,const DrawArgs& a){
 DrawKey k;size_t i=0;for(uint64_t v:{uint64_t(a.kind),uint64_t(a.count),uint64_t(a.instances),uint64_t(a.first),uint64_t(uint32_t(a.vertex_offset)),uint64_t(a.first_instance)})k.words[i++]=v;
 k.words[i++]=uint64_t(s.ib);k.words[i++]=s.ib_offset;k.words[i++]=uint64_t(s.ib_type);
 // Legacy candidate for other shader families. The audited static pair below
 // resolves its actual active inputs instead of assuming vertex stream 0.
 // Neither CPU identity is a certificate: exact GPU clip/UV/parameter checks
 // remain mandatory, and duplicate candidate identities stay unsupported.
 const auto& v=s.vb[0];k.words[i++]=uint64_t(v.buffer);k.words[i++]=v.offset;k.words[i++]=v.stride;
 const bool depth=static_depth_family(s.vs,s.fs),deferred=audited_static_family(s.vs,s.fs);
 if(!depth && !deferred)return k;
 StaticInputs attributes{};StaticKeyInputs inputs{};Bg3Binding engine{},pulled{};
 bool available=geo_native_hit_inputs(s.pipe[0],&attributes) &&
  query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,0,0,&engine) && engine.buffer && engine.range>=112 && geo_uniform_buffer_range(engine.buffer,engine.offset,112);
 if(available){
  inputs.engine_buffer=uint64_t(engine.buffer);inputs.engine_offset=engine.offset;
  inputs.pull_hint_known=read_mapped(engine.buffer,engine.offset+96,&inputs.pull_index,4);
  if(inputs.pull_hint_known && inputs.pull_index>0 && query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,13,0,&pulled)){
   inputs.pull_buffer=uint64_t(pulled.buffer);inputs.pull_offset=pulled.offset;inputs.pull_range=pulled.range;
  }
  auto stream=[&](const InputAttribute& attr,InputStream& out){
   if(attr.binding>=CmdState::kMaxVertexBindings)return false;const auto& bound=s.vb[attr.binding];
   const auto stride=attr.dynamic_stride?bound.stride:attr.stride;
   if(!bound.buffer || !stride || bound.offset>UINT64_MAX-attr.offset || bound.offset+attr.offset>=geo_buffer_size(bound.buffer))return false;
   out={uint64_t(bound.buffer),bound.offset+attr.offset,stride,uint32_t(attr.format)};return true;
  };
  bool position=stream(attributes.position,inputs.position),uv=stream(attributes.uv,inputs.uv);
  available=uv && (position || (inputs.pull_hint_known && inputs.pull_index>0));
 }
 DrawKey canonical{};bool ready=available && canonical_static_key(k,inputs,canonical);
 // Mapped index is a candidate hint, never an execution-time certificate.
 // Unknown/zero retains the actual position attribute. GPU clip/UV/parameter
 // equality remains mandatory even when an inactive position stream is omitted.
 static thread_local uint64_t logged_frame=UINT64_MAX;static thread_local uint32_t logged[2]{};const auto frame=frame_index();
 if(cfg_int("rt_stats",0) && frame%120==0){if(logged_frame!=frame){logged_frame=frame;logged[0]=logged[1]=0;}
  if(logged[deferred?1:0]++<4)log("[hit material canonical inputs] frame %llu %s ready %u input layout known %u engine %p+%llu range %llu pull hint known/index %u/%u SSBO0.13 %p+%llu range %llu; position location0 binding/format/stride %u/%u/%llu UV location1 binding/format/stride %u/%u/%llu; draw count %u first %u vertex %d; CPU hints only, GPU clip/UV/params required",
   (unsigned long long)frame,depth?"source":"deferred",unsigned(ready),unsigned(attributes.valid),(void*)engine.buffer,(unsigned long long)engine.offset,(unsigned long long)engine.range,unsigned(inputs.pull_hint_known),inputs.pull_index,(void*)pulled.buffer,(unsigned long long)pulled.offset,(unsigned long long)pulled.range,
   attributes.position.binding,unsigned(attributes.position.format),(unsigned long long)inputs.position.stride,attributes.uv.binding,unsigned(attributes.uv.format),(unsigned long long)inputs.uv.stride,a.count,a.first,a.vertex_offset);
 }
 if(ready)return canonical;
 // A missing audited input tuple cannot accidentally join through the old
 // coarse VB0 key. Different shader hashes make this explicitly unsupported.
 k.words[20]=0x4e484b30u;k.words[21]=s.vs;return k;
}
bool create(Buffer& out,VkDeviceSize bytes,VkBufferUsageFlags usage,bool host=false){
 if(out.b)return true;
 VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;ci.usage=usage;
 if(dd->vk.CreateBuffer(dd->device,&ci,nullptr,&out.b)!=VK_SUCCESS)return false;
 VkMemoryRequirements req{};dd->vk.GetBufferMemoryRequirements(dd->device,out.b,&req);
 VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=find_memory_type(dd,req.memoryTypeBits,host?(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT):VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
 if(host && ai.memoryTypeIndex==UINT32_MAX)ai.memoryTypeIndex=find_memory_type(dd,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
 if(ai.memoryTypeIndex==UINT32_MAX || dd->vk.AllocateMemory(dd->device,&ai,nullptr,&out.m)!=VK_SUCCESS || dd->vk.BindBufferMemory(dd->device,out.b,out.m,0)!=VK_SUCCESS){
  dd->vk.DestroyBuffer(dd->device,out.b,nullptr);if(out.m)dd->vk.FreeMemory(dd->device,out.m,nullptr);out={};return false;
 }
 if(host && dd->vk.MapMemory(dd->device,out.m,0,VK_WHOLE_SIZE,0,&out.mapped)!=VK_SUCCESS){dd->vk.DestroyBuffer(dd->device,out.b,nullptr);dd->vk.FreeMemory(dd->device,out.m,nullptr);out={};return false;}
 return true;
}
void destroy(Buffer& b){if(b.mapped)dd->vk.UnmapMemory(dd->device,b.m);if(b.b)dd->vk.DestroyBuffer(dd->device,b.b,nullptr);if(b.m)dd->vk.FreeMemory(dd->device,b.m,nullptr);b={};}
void barrier(VkCommandBuffer cmd,VkPipelineStageFlags source,VkAccessFlags access,VkPipelineStageFlags target,VkAccessFlags next){
 VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=access;b.dstAccessMask=next;
 dd->vk.CmdPipelineBarrier(cmd,source,target,0,1,&b,0,nullptr,0,nullptr);
}
struct ViewFacts {
 Bg3Binding binding{};omm::OrdinaryBindingFacts ordinary{};bool queried{},tracked{},known{},image_current{};
 uint64_t id{};uint32_t image_id{},current_image_id{},flags{},mips{},layers{};
 VkImageViewType type{VK_IMAGE_VIEW_TYPE_MAX_ENUM};VkFormat format{VK_FORMAT_UNDEFINED};
 VkStructureType extension{VK_STRUCTURE_TYPE_MAX_ENUM};VkImageSubresourceRange range{};
};
bool view(VkCommandBuffer cmd,uint32_t binding,VkImageViewType type,View& out,ViewFacts& fact){
 auto& b=fact.binding;fact.queried=query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,binding,0,&b);
 bool frozen=omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,binding,0,&fact.ordinary);
 if(!fact.queried || !b.view)return false;
 std::shared_lock lock(tracker().mu);auto i=tracker().views.find(b.view);if(i==tracker().views.end())return false;
 const auto& v=i->second;fact.tracked=true;fact.known=v.creation.known;fact.id=v.creation.id;fact.image_id=v.creation.image_id;
 fact.type=v.creation.type;fact.format=v.format;fact.flags=v.creation.flags;fact.extension=v.creation.extension;fact.range=v.range;
 auto image=tracker().images.find(v.image);if(image==tracker().images.end())return false;
 fact.current_image_id=image->second.id;fact.image_current=v.creation.image_id && v.creation.image_id==image->second.id;
 bool range=trace_view::resolve_count(0,v.range.levelCount,image->second.ci.mipLevels,fact.mips) &&
  trace_view::resolve_count(0,v.range.layerCount,image->second.ci.arrayLayers,fact.layers);
 bool valid=frozen && (b.layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || b.layout==VK_IMAGE_LAYOUT_GENERAL) &&
  fact.known && fact.id && fact.type==type && !fact.flags && fact.extension==VK_STRUCTURE_TYPE_MAX_ENUM && fact.image_current &&
  v.range.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT && v.range.baseMipLevel==0 && v.range.baseArrayLayer==0 && range &&
  (type!=VK_IMAGE_VIEW_TYPE_2D || (fact.layers==1 && v.format==VK_FORMAT_R32G32B32A32_SFLOAT));
 if(!valid)return false;out={b.view,v.image,b.layout,v.creation.id,image->second.id};return true;
}
bool current(const View& v){
 std::shared_lock lock(tracker().mu);auto i=tracker().views.find(v.view);if(i==tracker().views.end())return false;
 auto image=tracker().images.find(v.image);
 return i->second.image==v.image && i->second.creation.id==v.id && image!=tracker().images.end() && image->second.id==v.image_id;
}
bool capture(VkCommandBuffer cmd,Material& material){
 auto* state=cmd_state(cmd);if(!state || !audited_static_family(state->vs,state->fs))return false;
 omm::OrdinaryBindingFacts buffer_facts[3]{};bool queried[3]{};uint32_t failed=0;
 auto buffer=[&](uint32_t n,uint32_t set,uint32_t slot,VkDeviceSize minimum,Bg3Binding& out){
  queried[n]=query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,set,slot,0,&out);
  bool frozen=omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,set,slot,0,&buffer_facts[n]);
  if(out.range==VK_WHOLE_SIZE){auto size=geo_buffer_size(out.buffer);if(out.offset<=size)out.range=size-out.offset;}
  bool valid=queried[n] && out.buffer && out.range>=minimum && frozen;if(!valid)failed|=1u<<n;
 };
 buffer(0,1,0,112,material.engine);buffer(1,1,1,16,material.physical_ubo);buffer(2,0,12,128,material.tilesets);
 ViewFacts views[4]{};View* targets[4]={&material.table,&material.normal,&material.albedo,&material.physical};
 for(uint32_t i=0;i<4;++i)if(!view(cmd,3+i,i?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D,*targets[i],views[i]))failed|=1u<<(3+i);
 bool sampler_bound=omm::bound_sampler(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,2,0,material.sampler);
 omm::OrdinaryBindingFacts sampler_binding{};
 bool sampler_frozen=omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,2,0,&sampler_binding);
 if(!sampler_bound)failed|=1u<<7;
 if(!sampler_frozen || !omm::ordinary_sampler_current(material.sampler))failed|=1u<<8;
 const auto& c=material.sampler.sampler;
 if(!c.complete || c.info.flags || c.info.compareEnable || c.info.unnormalizedCoordinates || c.reduction!=VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE)failed|=1u<<9;
 if(!failed)return true;
 // Two full prerequisite records per 120-frame window, not one missing-term
 // experiment at a time. These are actual draw bindings, not a native-proof claim.
 static uint64_t logged_frame=UINT64_MAX;static uint32_t logged=0;const auto frame=frame_index();
 if(cfg_int("rt_stats",0) && frame%120==0){if(logged_frame!=frame){logged_frame=frame;logged=0;}
  if(logged++<2){
   log("[hit material capture] frame %llu failures 0x%x (bits0..2 buffers,3..6 views,7 sampler metadata,8 frozen sampler,9 sampler policy) pipe %u",
    (unsigned long long)frame,failed,state->pipe_id[0]);
   Bg3Binding* buffers[3]={&material.engine,&material.physical_ubo,&material.tilesets};
   for(uint32_t i=0;i<3;++i){const auto& f=buffer_facts[i];const auto& v=*buffers[i];
    log("[hit material capture buffer] %u queried %u %p+%llu range %llu type %u; push %u allocated %u supported %u complete %u set/layout/version %llu/%llu/%llu flags 0x%x count %u",
     i,unsigned(queried[i]),(void*)v.buffer,(unsigned long long)v.offset,(unsigned long long)v.range,unsigned(v.type),unsigned(f.push),unsigned(f.allocated),unsigned(f.supported),unsigned(f.layout_complete),
     (unsigned long long)f.set_id,(unsigned long long)f.layout_id,(unsigned long long)f.version,unsigned(f.flags),f.count);
   }
   for(uint32_t i=0;i<4;++i){const auto& f=views[i];const auto& o=f.ordinary;
    log("[hit material capture view] binding %u queried %u tracked %u known %u format/type %u/%u id %llu image ids %u/%u flags 0x%x extension %u layout %u base mip/layer %u/%u resolved %u/%u; push %u set supported/complete %u/%u binding flags 0x%x",
     i+3,unsigned(f.queried),unsigned(f.tracked),unsigned(f.known),unsigned(f.format),unsigned(f.type),(unsigned long long)f.id,f.image_id,f.current_image_id,f.flags,unsigned(f.extension),unsigned(f.binding.layout),
     f.range.baseMipLevel,f.range.baseArrayLayer,f.mips,f.layers,unsigned(o.push),unsigned(o.supported),unsigned(o.layout_complete),unsigned(o.flags));
   }
   log("[hit material capture sampler] bound %u status %u immutable %u frozen %u complete %u flags 0x%x reduction %u compare %u unnormalized %u extension %u set/layout/version/sampler %llu/%llu/%llu/%llu; push %u supported %u layout complete %u binding flags 0x%x",
    unsigned(sampler_bound),unsigned(material.sampler.status),unsigned(material.sampler.immutable),unsigned(sampler_frozen),unsigned(c.complete),unsigned(c.info.flags),unsigned(c.reduction),unsigned(c.info.compareEnable),unsigned(c.info.unnormalizedCoordinates),unsigned(c.unsupportedStructure),
    (unsigned long long)material.sampler.descriptorSetId,(unsigned long long)material.sampler.layoutId,(unsigned long long)material.sampler.writeVersion,(unsigned long long)c.id,
    unsigned(sampler_binding.push),unsigned(sampler_binding.supported),unsigned(sampler_binding.layout_complete),unsigned(sampler_binding.flags));
  }
 }
 return false;
}
void destroy_query(Query& q){if(q.pool)dd->vk.DestroyDescriptorPool(dd->device,q.pool,nullptr);destroy(q.data);destroy(q.rows);}
void destroy_packet(Deferred& p){for(auto sampler:p.samplers)dd->vk.DestroySampler(dd->device,sampler,nullptr);destroy(p.data);destroy(p.rows);}
bool retired(uint64_t id){auto i=owners.find(id);return i!=owners.end() && i->second.retired(current_recording(i->second.command),life_gpu_done());}
void collect(){
 for(auto i=queries.begin();i!=queries.end();)if(retired(i->first)){destroy_query(*i->second);i=queries.erase(i);}else ++i;
 for(auto i=packets.begin();i!=packets.end();)if(i->second.use_count()==1 && retired(i->first)){destroy_packet(*i->second);i=packets.erase(i);}else ++i;
 std::set<uint64_t> live;for(const auto& slot:slots)live.insert(slot.owners.begin(),slot.owners.end());
 for(const auto& [id,p]:packets)live.insert(id);for(const auto& [id,q]:queries){live.insert(id);live.insert(q->source);live.insert(q->deferred->recording);}
 for(auto i=owners.begin();i!=owners.end();)if(!live.contains(i->first) && retired(i->first)){submitted_queues.erase(i->first);i=owners.erase(i);}else ++i;
 for(auto i=marked.begin();i!=marked.end();)if(!owners.contains(i->second))i=marked.erase(i);else ++i;
}
std::shared_ptr<Deferred> packet_for(const Slot& slot){
 std::shared_ptr<Deferred> found;
 for(const auto& [id,p]:packets)if(p->frame==slot.frame && p->camera==slot.camera && p->camera_offset==slot.camera_offset){
  if(found || !p->finalized || !p->valid)return {};found=p;
 }return found;
}
void publish_word(Query& query,uint32_t value){
 auto* word=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(query.rows.mapped)+row_payload);
 std::atomic_ref<uint32_t>(*word).store(value,std::memory_order_release);
}
}
bool enabled(){return on;}
void device_created(DeviceData* device){std::lock_guard lock(mutex);if(dd)return;dd=device;on=cfg_int("rt_hit_material",0)!=0 && cfg_int("rt_alpha",1)!=0 && cfg_int("rt_bvh_split",1)!=0;}
void device_destroyed(DeviceData* device){std::lock_guard lock(mutex);if(dd!=device)return;if(on)dd->vk.DeviceWaitIdle(dd->device);
 for(auto& [id,q]:queries)destroy_query(*q);queries.clear();
 for(auto& [id,p]:packets)destroy_packet(*p);packets.clear();
 for(auto& s:slots){destroy(s.data);s={};}
 for(auto pipeline:{gather_pipeline,join_pipeline})if(pipeline)dd->vk.DestroyPipeline(dd->device,pipeline,nullptr);
 for(auto layout:{gather_layout,join_layout})if(layout)dd->vk.DestroyPipelineLayout(dd->device,layout,nullptr);
 for(auto dsl:{gather_dsl,join_dsl})if(dsl)dd->vk.DestroyDescriptorSetLayout(dd->device,dsl,nullptr);
 gather_pipeline={};join_pipeline={};gather_layout={};join_layout={};gather_dsl={};join_dsl={};pending.clear();owners.clear();submitted_queues.clear();marked.clear();dd=nullptr;on=false;
}
void prepass_begin(VkCommandBuffer cmd){if(!on)return;auto* s=cmd_state(cmd);if(!one_time(s))return;std::lock_guard lock(mutex);collect();
 for(auto i=pending.begin();i!=pending.end();)if(current_recording(i->second.command)!=i->second.recording)i=pending.erase(i);else ++i;
 if(pending.size()>=128 && !pending.contains(s->recording))return;
 auto& p=pending[s->recording];p={};p.command=handle(cmd);p.recording=s->recording;p.join.begin(p.command,p.recording);
}
void note_prepass(VkCommandBuffer cmd,CmdState* state,const DrawArgs& args,uint32_t record,VkDeviceSize rawOffset,uint32_t stride,uint32_t triangles){
 if(!on || !state || args.kind>1 || args.instances!=1 || args.count!=uint64_t(triangles)*3 || record>=max_records)return;
 AlphaSnapshot snapshot{};if(!alpha_snapshot(record,&snapshot) || snapshot.rec.kind!=1 || snapshot.rec.channel!=0)return;
 Pre pre{key(cmd,*state,args),snapshot,rawOffset,stride,triangles,record,state->vs,state->fs};std::lock_guard lock(mutex);auto p=pending.find(state->recording);if(p==pending.end())return;
 ++p->second.candidate_draws;
 if(p->second.join.note(handle(cmd),state->recording,pre.key,UINT32_MAX) && p->second.join.assign(handle(cmd),state->recording,pre.key,record))p->second.pre.push_back(pre);
}
void prepass_observe(VkCommandBuffer,CmdState*,const DrawArgs&){
 // Only actual note_prepass candidates reserve a material join identity.
}
void scene_prepare(VkCommandBuffer cmd,uint32_t index,const RtCapture& cap){
 if(!on || !cfg_int("rt_hit_material",0) || index>=kSceneSlots)return;auto* state=cmd_state(cmd);if(!one_time(state))return;std::lock_guard lock(mutex);collect();
 auto& s=slots[index];s.set={};s.handles.clear();s.pre.clear();s.owners.clear();s.prepared=s.attached=false;
 auto p=pending.find(state->recording);if(!cap.split || p==pending.end() || !p->second.join.current(handle(cmd),state->recording))return;
 if(!create(s.data,stream_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT))return;
 s.command=handle(cmd);s.recording=state->recording;s.frame=frame_index();s.cursor=0;s.generation=0;
 s.camera=cap.ubo;s.camera_offset=cap.ubo_offset;s.join=p->second.join;if(!mark_owner(cmd,s))return;
 // When deferred was recorded first, exact audited draw keys are already known.
 // Copy only their source records; unrelated VT records cannot consume the cap.
 auto deferred=packet_for(s);
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
 uint32_t filtered=0,duplicates=0,range_dropped=0;const auto source_draws=p->second.candidate_draws,source_entries=uint32_t(p->second.pre.size());
 for(const auto& pre:p->second.pre){
  uint32_t sourceRecord=0;if(!s.join.unique_source(pre.key,sourceRecord) || sourceRecord!=pre.record){++duplicates;continue;}
  if(deferred && std::none_of(deferred->materials.begin(),deferred->materials.end(),[&](const auto& m){return m.key==pre.key && deferred->join.unique(m.key,m.row.valid-1);})){++filtered;continue;}
  VkDeviceSize bytes=VkDeviceSize(pre.triangles)*3*pre.stride;
  if(!pre.stride || !byte_range(pre.raw,bytes,cap.xfb_size) || !byte_range(s.cursor,bytes,stream_bytes/2)){++range_dropped;continue;}
  VkBufferCopy copy{pre.raw,s.cursor,bytes};dd->vk.CmdCopyBuffer(cmd,cap.xfb,s.data.b,1,&copy);auto saved=pre;saved.raw=s.cursor;s.pre.push_back(saved);s.cursor+=bytes;
 }
 if(s.pre.empty() && deferred && !deferred->materials.empty() && cfg_int("rt_stats",0) && s.frame%120==0){
  // Samples are explicitly NOT a claimed pair. They expose source shader and
  // raw argument/IB/VB domains if no exact key association can be constructed.
  auto sample=[&](const char* side,const DrawKey& key,uint64_t vs,uint64_t fs){const auto& w=key.words;
   log("[hit material key sample] frame %llu %s vs/fs %016llx/%016llx kind/count/instances/first/vertex/instance %llu/%llu/%llu/%llu/%llu/%llu IB/offset/type %llx/%llu/%llu schema 0x%llx; not a claimed pair",
    (unsigned long long)s.frame,side,(unsigned long long)vs,(unsigned long long)fs,(unsigned long long)w[0],(unsigned long long)w[1],(unsigned long long)w[2],(unsigned long long)w[3],(unsigned long long)w[4],(unsigned long long)w[5],
    (unsigned long long)w[6],(unsigned long long)w[7],(unsigned long long)w[8],(unsigned long long)w[20]);
   if(w[20]==0x4e484b31u)log("[hit material key inputs] %s engine %llx+%llu pull %llu index hint %llu position buffer/offset/stride/format %llx/%llu/%llu/%llu UV buffer/offset/stride/format %llx/%llu/%llu/%llu",
    side,(unsigned long long)w[9],(unsigned long long)w[10],(unsigned long long)w[11],(unsigned long long)w[21],(unsigned long long)w[12],(unsigned long long)w[13],(unsigned long long)w[14],(unsigned long long)w[15],
    (unsigned long long)w[16],(unsigned long long)w[17],(unsigned long long)w[18],(unsigned long long)w[19]);
  };
  if(!p->second.pre.empty()){
   auto source=std::find_if(p->second.pre.begin(),p->second.pre.end(),[](const auto& pre){return static_depth_family(pre.vs,pre.fs);});
   if(source==p->second.pre.end())source=p->second.pre.begin();sample("source",source->key,source->vs,source->fs);
  }
  sample("deferred",deferred->materials.front().key,0x3812bc980438747full,0x8995da1e4060f53bull);
 }
 s.prepared=true;pending.erase(p);
 barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
 if(cfg_int("rt_stats",0) && s.frame%120==0)log("[hit material source] frame %llu cmd %p recording %llu level %u flags 0x%x prepared %zu copied %llu bytes source draws/entries %u/%u duplicate keys %u range dropped %u filtered unrelated %u; deferred packet %u camera %p+%llu",
  (unsigned long long)s.frame,(void*)cmd,(unsigned long long)s.recording,unsigned(state->level),unsigned(state->begin_flags),s.pre.size(),(unsigned long long)s.cursor,source_draws,source_entries,duplicates,range_dropped,filtered,unsigned(bool(deferred)),(void*)s.camera,(unsigned long long)s.camera_offset);
}
bool attach(VkCommandBuffer,uint32_t index,VkDescriptorSet set,VkBuffer dummyBuffer,VkImageView dummyArray,VkSampler dummySampler,uint32_t generation){
 if(!on || index>=kSceneSlots || !set || !generation)return false;std::lock_guard lock(mutex);auto& s=slots[index];s.set=set;s.generation=generation;s.attached=true;s.dummy_array=dummyArray;s.dummy_sampler=dummySampler;
 // Baseline AlphaDSL stays intentionally unsupported for material consumers.
 // Only the typed ABI21 callback returns a query-owned supported tuple.
 VkDescriptorBufferInfo buffers[2]={{dummyBuffer,0,256},{dummyBuffer,0,256}};
 std::vector<VkDescriptorImageInfo> caches(cache_slots,{VK_NULL_HANDLE,dummyArray,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}),samplers(sampler_slots,{dummySampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED});
 VkWriteDescriptorSet writes[4]{};for(uint32_t i=0;i<4;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=12+i;writes[i].descriptorCount=1;}
 for(uint32_t i=0;i<2;++i){writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=buffers+i;}
 writes[2].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[2].descriptorCount=cache_slots;writes[2].pImageInfo=caches.data();
 writes[3].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;writes[3].descriptorCount=sampler_slots;writes[3].pImageInfo=samplers.data();dd->vk.UpdateDescriptorSets(dd->device,4,writes,0,nullptr);return true;
}
bool deferred_begin(VkCommandBuffer cmd,CmdState* state,const DrawArgs& args,bool exactPipeline,uint32_t stride,VkBuffer* buffer,VkDeviceSize* offset,VkDeviceSize* bytes){
 if(!on || !cfg_int("rt_hit_material",0) || !one_time(state))return false;Bg3Binding camera{};if(!query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,2,0,&camera) || !camera.buffer)return false;
 auto draw=key(cmd,*state,args);std::lock_guard lock(mutex);collect();
 auto it=packets.find(state->recording);
 if(it==packets.end()){
  if(packets.size()>=kSceneSlots)return false;
  auto p=std::make_shared<Deferred>();p->command=handle(cmd);p->recording=state->recording;p->frame=frame_index();p->camera=camera.buffer;p->camera_offset=camera.offset;p->join.begin(p->command,p->recording);
  if(!create(p->data,stream_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT) ||
     !create(p->rows,packet_rows,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT)){destroy_packet(*p);return false;}
  it=packets.emplace(state->recording,std::move(p)).first;
 }
 auto& p=*it->second;
 if(p.finalized || p.frame!=frame_index() || p.camera!=camera.buffer || p.camera_offset!=camera.offset){p.valid=false;return false;}
 const bool audited=audited_static_family(state->vs,state->fs);
 if(!audited){++p.family_declined;return false;}++p.seen;
 if(!exactPipeline || args.kind>1 || args.instances!=1 || stride<64){++p.pipeline_declined;return false;}
 bool unique=observe_static_deferred(p.join,p.command,p.recording,draw,state->vs,state->fs,true);
 if(!unique){++p.audited_duplicate;return false;}++p.audited_unique;
 // Count real audited duplicates even beyond the storage bound. Unrelated
 // shader families never reserve an audited candidate's geometry identity.
 if(p.materials.size()>=material_slots)return false;
 Material material{};if(!capture(cmd,material)){++p.binding_declined;return false;}
 *bytes=VkDeviceSize(args.count)*stride;if(!args.count || args.count%3 || !byte_range(p.cursor,*bytes,stream_bytes))return false;
 if(!own(cmd))return false;
 auto identity=[](const View& v){return CacheIdentity{uint64_t(v.view),v.id,uint64_t(v.image),v.image_id,uint64_t(v.layout)};};
 auto resources=p.resources;ResourceIndices indices{};
 if(!resources.reserve(material.sampler.sampler.id,{identity(material.normal),identity(material.albedo),identity(material.physical)},indices))return false;
 if(indices.new_sampler){
  auto sampler_info=material.sampler.sampler.info;sampler_info.pNext=nullptr;VkSampler sampler{};
  if(dd->vk.CreateSampler(dd->device,&sampler_info,nullptr,&sampler)!=VK_SUCCESS)return false;
  p.samplers.push_back(sampler);
 }
 p.resources=resources;uint32_t local=uint32_t(p.materials.size());
 material.key=draw;material.row.valid=local+1;material.row.triangles=args.count/3;material.row.deferred=uint32_t(p.cursor);material.row.stride=stride;material.row.frame=uint32_t(p.frame);
 material.row.sampler=indices.sampler;material.row.normal=indices.normal;material.row.albedo=indices.albedo;material.row.physical=indices.physical;
 p.join.assign(p.command,p.recording,draw,local);p.materials.push_back(material);*buffer=p.data.b;*offset=p.cursor;p.cursor+=*bytes;return true;
}
void deferred_finish(VkCommandBuffer cmd,bool readonly){
 if(!on)return;auto* state=cmd_state(cmd);if(!one_time(state))return;std::lock_guard lock(mutex);auto i=packets.find(state->recording);if(i==packets.end())return;auto& p=*i->second;
 if(p.finalized){p.valid=false;return;}p.finalized=true;p.valid=p.valid && readonly && cfg_int("rt_hit_material",0)!=0 && compute_ready(false);
 if(!own(cmd)){p.valid=false;return;}
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
 dd->vk.CmdFillBuffer(cmd,p.rows.b,0,packet_rows,0);
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
 uint32_t captured=0;
 if(p.valid)for(auto& m:p.materials){uint32_t local=0;
  if(!p.join.claim(p.command,p.recording,m.key,local) || local!=m.row.valid-1 || !current(m.table) || !current(m.normal) || !current(m.albedo) || !current(m.physical) || !omm::ordinary_sampler_current(m.sampler))continue;
  VkDescriptorBufferInfo info[4]={{m.engine.buffer,m.engine.offset,m.engine.range},{m.physical_ubo.buffer,m.physical_ubo.offset,m.physical_ubo.range},{m.tilesets.buffer,m.tilesets.offset,m.tilesets.range},{p.rows.b,0,packet_rows}};
  VkWriteDescriptorSet writes[4]{};for(uint32_t n=0;n<4;++n){writes[n]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[n].dstBinding=n;writes[n].descriptorCount=1;writes[n].descriptorType=n<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[n].pBufferInfo=info+n;}
  uint32_t push[4]={16u+local*record_bytes,local,1,0};
  dd->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,gather_pipeline);dd->vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,gather_layout,0,4,writes);
  dd->vk.CmdPushConstants(cmd,gather_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,push);dd->vk.CmdDispatch(cmd,1,1,1);++captured;
 }
 barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
 if(p.valid)restore_bindings(cmd,VK_PIPELINE_BIND_POINT_COMPUTE);
 if(cfg_int("rt_stats",0) && p.frame%120==0)log("[hit material deferred packet] frame %llu cmd %p recording %llu level %u flags 0x%x static seen %u audited unique/duplicate %u/%u other family %u pipeline declined %u binding declined %u captured %zu snapshots %u unique samplers/cache views %u/%u bytes %llu valid %u readonly %u camera %p+%llu; source preparation not required",
  (unsigned long long)p.frame,(void*)cmd,(unsigned long long)p.recording,unsigned(state->level),unsigned(state->begin_flags),p.seen,p.audited_unique,p.audited_duplicate,p.family_declined,p.pipeline_declined,p.binding_declined,p.materials.size(),captured,p.resources.sampler_count,p.resources.cache_count,(unsigned long long)p.cursor,unsigned(p.valid),unsigned(readonly),(void*)p.camera,(unsigned long long)p.camera_offset);
}
void slot_in_use(uint32_t index,uint64_t frame){if(!on || index>=kSceneSlots)return;std::lock_guard lock(mutex);for(auto h:slots[index].handles)life_extend(h,frame);}
bool slot_reusable(uint32_t index,uint64_t gpuDone){if(!on || index>=kSceneSlots)return true;std::lock_guard lock(mutex);
 for(auto id:slots[index].owners){auto i=owners.find(id);if(i==owners.end() || !i->second.retired(current_recording(i->second.command),gpuDone))return false;}return true;
}
bool register_use(VkCommandBuffer cmd,uint32_t index,VkDescriptorSet* query_alpha){
 if(query_alpha)*query_alpha={};
 if(!on || !query_alpha || !cfg_int("rt_hit_material",0) || index>=kSceneSlots)return false;
 auto* state=cmd_state(cmd);if(!one_time(state)){
  if(cfg_int("rt_stats",0) && frame_index()%120==0)log("[hit material query reject] recording domain frame %llu level %u flags 0x%x",
    (unsigned long long)frame_index(),state?unsigned(state->level):UINT32_MAX,state?unsigned(state->begin_flags):0u);return false;
 }
 std::lock_guard lock(mutex);collect();auto& slot=slots[index];
 if(!slot.prepared || !slot.attached || slot.frame!=frame_index()){
  if(cfg_int("rt_stats",0) && frame_index()%120==0)log("[hit material query reject] source epoch current %llu source %llu prepared %u attached %u",
   (unsigned long long)frame_index(),(unsigned long long)slot.frame,unsigned(slot.prepared),unsigned(slot.attached));return false;
 }
 Bg3Binding camera{};if(!query_binding(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,0,2,0,&camera) || camera.buffer!=slot.camera || camera.offset!=slot.camera_offset){
  if(cfg_int("rt_stats",0) && slot.frame%120==0)log("[hit material query reject] camera frame %llu source %p+%llu query %p+%llu",
   (unsigned long long)slot.frame,(void*)slot.camera,(unsigned long long)slot.camera_offset,(void*)camera.buffer,(unsigned long long)camera.offset);return false;
 }
 auto existing=queries.find(state->recording);
 if(existing!=queries.end()){
  auto& q=*existing->second;if(q.slot!=index || q.generation!=slot.generation || q.command!=handle(cmd))return false;
  *query_alpha=q.set;return mark_owner(cmd,slot);
 }
 auto packet=packet_for(slot);if(!packet || queries.size()>=2u*kSceneSlots || !compute_ready(true)){
  static uint64_t logged=UINT64_MAX;if(cfg_int("rt_stats",0) && slot.frame%120==0 && logged!=slot.frame){logged=slot.frame;log("[hit material query] unavailable frame %llu scene %u packet %u pending packets %zu queries %zu source camera %p+%llu",
   (unsigned long long)slot.frame,slot.generation,unsigned(bool(packet)),packets.size(),queries.size(),(void*)slot.camera,(unsigned long long)slot.camera_offset);}return false;
 }
 auto query=std::make_unique<Query>();auto& q=*query;q.command=handle(cmd);q.recording=state->recording;q.source=slot.recording;q.slot=index;q.generation=slot.generation;q.deferred=packet;
 // Candidate identity narrows the pair; exact GPU clip+UV comparison remains
 // mandatory. Neither CPU recording order nor a matching handle certifies it.
 VkDeviceSize cursor=0;
 for(const auto& deferred:packet->materials){
  uint32_t record=0;if(!join_packets(slot.join,packet->join,deferred.key,deferred.row.valid-1,record))continue;
  auto source=std::find_if(slot.pre.begin(),slot.pre.end(),[&](const auto& pre){return pre.record==record;});
  if(source==slot.pre.end() || source->stride!=deferred.row.stride || source->triangles!=deferred.row.triangles ||
     source->alpha.tex!=deferred.table.view || source->alpha.cache!=deferred.albedo.view ||
     !current(deferred.table) || !current(deferred.normal) || !current(deferred.albedo) || !current(deferred.physical))continue;
  VkDeviceSize bytes=VkDeviceSize(source->triangles)*3*source->stride;
  if(!byte_range(cursor,bytes*2,stream_bytes))continue;
  auto m=deferred;m.snapshot=deferred.row.valid-1;m.source_raw=uint32_t(source->raw);m.deferred_raw=deferred.row.deferred;
  m.row.valid=record+1;m.row.pre=uint32_t(cursor);m.row.deferred=uint32_t(cursor+bytes);m.row.table=source->alpha.rec.tex;
  std::memcpy(m.row.p0,source->alpha.rec.p0,32);std::memcpy(m.row.t,source->alpha.rec.t,128);cursor+=bytes*2;q.materials.push_back(m);
 }
 if(q.materials.empty()){
  if(cfg_int("rt_stats",0) && slot.frame%120==0)log("[hit material query] no exact mapping frame %llu source records %zu deferred records %zu; camera %p+%llu",
   (unsigned long long)slot.frame,slot.pre.size(),packet->materials.size(),(void*)slot.camera,(unsigned long long)slot.camera_offset);return false;
 }
 if(!create(q.rows,row_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,true) ||
    !create(q.data,cursor,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) || !alpha_clone_hit_set(slot.set,&q.pool,&q.set)){
  destroy_query(q);return false;
 }
 publish_word(q,0); // initialized before any GPU use; never rewritten after success.
 if(!mark_owner(cmd,slot)){destroy_query(q);return false;}
 if(std::find(slot.owners.begin(),slot.owners.end(),packet->recording)==slot.owners.end())slot.owners.push_back(packet->recording);
 std::vector<VkDescriptorImageInfo> caches(cache_slots,{VK_NULL_HANDLE,slot.dummy_array,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}),samplers(sampler_slots,{slot.dummy_sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED});
 for(const auto& m:q.materials){
  caches[m.row.normal]={VK_NULL_HANDLE,m.normal.view,m.normal.layout};caches[m.row.albedo]={VK_NULL_HANDLE,m.albedo.view,m.albedo.layout};caches[m.row.physical]={VK_NULL_HANDLE,m.physical.view,m.physical.layout};
  samplers[m.row.sampler]={packet->samplers[m.row.sampler],VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
  for(const auto& view:{m.table,m.normal,m.albedo,m.physical}){slot.handles.push_back(uint64_t(view.view));slot.handles.push_back(uint64_t(view.image));life_note(cmd,uint64_t(view.view));life_note(cmd,uint64_t(view.image));}
 }
 VkDescriptorBufferInfo buffers[2]={{q.rows.b,0,row_bytes},{q.data.b,0,cursor}};
 VkWriteDescriptorSet writes[4]{};for(uint32_t i=0;i<4;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=q.set;writes[i].dstBinding=12+i;writes[i].descriptorCount=1;}
 for(uint32_t i=0;i<2;++i){writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=buffers+i;}
 writes[2].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[2].descriptorCount=cache_slots;writes[2].pImageInfo=caches.data();
 writes[3].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;writes[3].descriptorCount=sampler_slots;writes[3].pImageInfo=samplers.data();dd->vk.UpdateDescriptorSets(dd->device,4,writes,0,nullptr);
 // Only this query writes its rows/basis. Invalid submit order leaves its
 // publication word zero, so the assembly cannot read unfinished producers.
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
 dd->vk.CmdFillBuffer(cmd,q.rows.b,0,row_payload,0);
 uint32_t header[4]={uint32_t(slot.frame),slot.generation,uint32_t(q.materials.size()),1};dd->vk.CmdUpdateBuffer(cmd,q.rows.b,0,16,header);
 for(const auto& m:q.materials){auto staged=m.row;staged.valid=0;dd->vk.CmdUpdateBuffer(cmd,q.rows.b,16+VkDeviceSize(m.row.valid-1)*record_bytes,record_bytes,&staged);}
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
 VkDescriptorBufferInfo inputs[5]={{slot.data.b,0,stream_bytes},{packet->data.b,0,stream_bytes},{packet->rows.b,0,packet_rows},{q.rows.b,0,row_bytes},{q.data.b,0,cursor}};
 VkWriteDescriptorSet join_writes[5]{};for(uint32_t i=0;i<5;++i){join_writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};join_writes[i].dstBinding=i;join_writes[i].descriptorCount=1;join_writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;join_writes[i].pBufferInfo=inputs+i;}
 dd->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,join_pipeline);dd->vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,join_layout,0,5,join_writes);
 for(const auto& m:q.materials){uint32_t bytes=m.row.triangles*3u*m.row.stride;
  uint32_t push[8]={16u+(m.row.valid-1)*record_bytes,16u+m.snapshot*record_bytes,m.source_raw,m.deferred_raw,bytes,uint32_t(row_payload),m.row.valid-1,m.snapshot};
  dd->vk.CmdPushConstants(cmd,join_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,32,push);dd->vk.CmdDispatch(cmd,(bytes/4u+63u)/64u,1,1);
 }
 barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);restore_bindings(cmd,VK_PIPELINE_BIND_POINT_COMPUTE);
 *query_alpha=q.set;
 if(cfg_int("rt_stats",0) && slot.frame%120==0)log("[hit material query] frame %llu scene %u query cmd %p recording %llu source %llu deferred %llu mapped %zu bytes %llu; publication awaits actual submit order",
  (unsigned long long)slot.frame,slot.generation,(void*)cmd,(unsigned long long)q.recording,(unsigned long long)q.source,(unsigned long long)packet->recording,q.materials.size(),(unsigned long long)cursor);
 queries.emplace(q.recording,std::move(query));return true;
}
bool recording_needs_submit_promotion(VkCommandBuffer cmd){if(!on)return false;auto recording=cmd_recording(cmd);if(!recording)return false;std::lock_guard lock(mutex);auto i=marked.find(cmd);return i!=marked.end() && i->second==recording;}
PreparedSubmission before_submit(VkQueue queue,std::span<const VkCommandBuffer> commands){
 PreparedSubmission prepared;if(!on)return prepared;std::lock_guard lock(mutex);std::set<uint64_t> unique;std::vector<uint64_t> ordered;
 for(auto cmd:commands){auto recording=cmd_recording(cmd);ordered.push_back(recording);auto i=marked.find(cmd);if(recording && i!=marked.end() && i->second==recording)unique.insert(recording);}
 for(auto id:unique){auto i=owners.find(id);if(i!=owners.end() && i->second.reserve())prepared.owners.push_back(id);}
 for(auto id:unique){auto i=queries.find(id);if(i==queries.end())continue;auto& q=*i->second;if(q.publication.sealed)continue;
  auto evidence=[&](uint64_t producer){Evidence result{};result.recording=producer;auto owner=owners.find(producer);
   if(owner!=owners.end() && !owner->second.poisoned){result.submitted=owner->second.submitted;auto submitted=submitted_queues.find(producer);if(submitted!=submitted_queues.end())result.queue=queue_handle(submitted->second);}
   // Source preparation and deferred finalization are already recorded before
   // register_use. Equal ID proves their local command order, not mere handles.
   result.recorded_before_query=producer==q.recording;return result;};
  auto healthy=[&](uint64_t owner){auto found=owners.find(owner);return found!=owners.end() && !found->second.poisoned;};
  bool valid=healthy(q.recording) && healthy(q.source) && healthy(q.deferred->recording) &&
   q.deferred->valid && q.deferred->finalized && slots[q.slot].generation==q.generation &&
   ordered_before_query(evidence(q.source),evidence(q.deferred->recording),q.recording,queue_handle(dd->graphics_queue),queue_handle(queue),ordered);
  uint32_t publication=q.publication.prepare(valid);publish_word(q,publication);prepared.queries.push_back(id);
  if(cfg_int("rt_stats",0) && slots[q.slot].frame%120==0)log("[hit material submit] frame %llu query %llu source %llu deferred %llu ordered %u queue %p graphics %p",
   (unsigned long long)slots[q.slot].frame,(unsigned long long)q.recording,(unsigned long long)q.source,(unsigned long long)q.deferred->recording,publication,(void*)queue,(void*)dd->graphics_queue);
 }
 return prepared;
}
void after_submit(const PreparedSubmission& prepared,VkQueue queue,bool success){
 if(!on)return;std::lock_guard lock(mutex);
 for(auto id:prepared.owners){auto i=owners.find(id);if(i==owners.end())continue;i->second.finish(success,frame_index());if(success){submitted_queues[id]=queue;if(queue!=dd->graphics_queue)i->second.poisoned=true;}}
 for(auto id:prepared.queries){auto i=queries.find(id);if(i==queries.end())continue;auto& q=*i->second;q.publication.finish(success);if(!success)publish_word(q,0);}
}
}
