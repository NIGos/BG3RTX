#include "hit_material.h"
#include "omm_sampler.h"
#include "../common/native_hit_join.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <memory>
#include <set>
namespace bg3::native_hit {
namespace {
constexpr VkDeviceSize stream_bytes=32u<<20;
constexpr VkDeviceSize row_bytes=16u+VkDeviceSize(record_bytes)*max_records;
struct Buffer {VkBuffer b{};VkDeviceMemory m{};};
struct Row {
 uint32_t valid{},triangles{},pre{},deferred{};
 uint32_t stride{},table{},sampler{},frame{};
 uint32_t normal{},albedo{},physical{},reserved{};
 float p0[4]{},p1[4]{},t[32]{},parameters[4]{};
 uint32_t padding[8]{};
};
static_assert(sizeof(Row)==record_bytes);
static_assert(offsetof(Row,p0)==48 && offsetof(Row,t)==80 && offsetof(Row,parameters)==208);
struct View {VkImageView view{};VkImage image{};VkImageLayout layout{};uint64_t id{};uint32_t image_id{};};
struct Pre {DrawKey key;AlphaSnapshot alpha;VkDeviceSize raw{};uint32_t stride{},triangles{},record{};};
struct Material {Row row;DrawKey key;View table,normal,albedo,physical;omm::BoundSampler sampler;Bg3Binding engine,physical_ubo,tilesets;};
struct Slot {
 Buffer data,rows;VkDescriptorSet set{};uint64_t command{},recording{},frame{};uint32_t generation{};
 VkBuffer camera{};VkDeviceSize camera_offset{};DrawJoin join;
 uint64_t deferred_recording{};
 VkDeviceSize cursor{};std::vector<Pre> pre;std::vector<Material> materials;
 std::vector<uint64_t> handles;std::vector<VkSampler> owned_samplers;bool attached{},prepared{},finalized{};
 std::vector<uint64_t> owners;
 uint32_t static_seen{},claimed{},captured{};
};
DeviceData* dd{};bool on{};std::mutex mutex;
struct Pending {uint64_t command{},recording{};DrawJoin join;std::vector<Pre> pre;};
std::unordered_map<uint64_t,Pending> pending;
std::unordered_map<uint64_t,RecordingOwner> owners;
std::unordered_map<VkCommandBuffer,uint64_t> marked;
std::unordered_map<uint64_t,uint32_t> deferred_slots;
Slot slots[kSceneSlots];
VkDescriptorSetLayout gather_dsl{};VkPipelineLayout gather_layout{};VkPipeline gather_pipeline{};
bool gather_ready(){
 if(gather_pipeline)return true;
 Bg3ShaderBlob blob{};if(!dd->vk.CmdPushDescriptorSet || !shader_get("rt/hit_material_gather.hlsl","main","cs_6_6",&blob))return false;
 VkDescriptorSetLayoutBinding b[4]={{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT},{1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT},
 {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT},{3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT}};
 VkDescriptorSetLayoutCreateInfo d{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};d.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;d.bindingCount=4;d.pBindings=b;
 if(!gather_dsl && dd->vk.CreateDescriptorSetLayout(dd->device,&d,nullptr,&gather_dsl)!=VK_SUCCESS)return false;
 VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,16};VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};l.setLayoutCount=1;l.pSetLayouts=&gather_dsl;l.pushConstantRangeCount=1;l.pPushConstantRanges=&range;
 if(!gather_layout && dd->vk.CreatePipelineLayout(dd->device,&l,nullptr,&gather_layout)!=VK_SUCCESS)return false;
 VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};m.codeSize=blob.size_bytes;m.pCode=blob.code;VkShaderModule module{};
 if(dd->vk.CreateShaderModule(dd->device,&m,nullptr,&module)!=VK_SUCCESS)return false;
 VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};p.layout=gather_layout;p.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main"};
 auto result=dd->vk.CreateComputePipelines(dd->device,VK_NULL_HANDLE,1,&p,nullptr,&gather_pipeline);dd->vk.DestroyShaderModule(dd->device,module,nullptr);return result==VK_SUCCESS;
}
uint64_t handle(VkCommandBuffer cmd){return uint64_t(reinterpret_cast<uintptr_t>(cmd));}
bool one_time(const CmdState* state){return state && state->level==VK_COMMAND_BUFFER_LEVEL_PRIMARY && (state->begin_flags&VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)!=0 && state->recording;}
bool mark_owner(VkCommandBuffer cmd,Slot& slot){
 auto* state=cmd_state(cmd);if(!one_time(state))return false;
 auto& owner=owners[state->recording];
 if(!owner.recording){owner.command=handle(cmd);owner.recording=state->recording;}
 if(owner.command!=handle(cmd) || owner.poisoned)return false;
 if(std::find(slot.owners.begin(),slot.owners.end(),state->recording)==slot.owners.end())slot.owners.push_back(state->recording);
 marked[cmd]=state->recording;return true;
}
uint64_t current_recording(uint64_t command){return cmd_recording(reinterpret_cast<VkCommandBuffer>(uintptr_t(command)));}
int deferred_slot(VkCommandBuffer cmd,CmdState* state){
 if(!one_time(state))return -1;
 Bg3Binding camera{};if(!query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,2,0,&camera) || !camera.buffer)return -1;
 int found=-1;
 for(uint32_t i=0;i<kSceneSlots;++i){const auto& slot=slots[i];
  if(slot.prepared && slot.attached && slot.frame==frame_index() && slot.camera==camera.buffer && slot.camera_offset==camera.offset){if(found>=0)return -1;found=int(i);}
 }
 return found;
}
DrawKey key(const CmdState& s,const DrawArgs& a){
 DrawKey k;size_t i=0;for(uint64_t v:{uint64_t(a.kind),uint64_t(a.count),uint64_t(a.instances),uint64_t(a.first),uint64_t(uint32_t(a.vertex_offset)),uint64_t(a.first_instance)})k.words[i++]=v;
 k.words[i++]=uint64_t(s.ib);k.words[i++]=s.ib_offset;k.words[i++]=uint64_t(s.ib_type);
 // Canonical vertex stream0 + index/draw identity select candidates. Unused
 // higher binding state differs across depth/deferred pipelines and is NOT a
 // geometry certificate. Exact post-VS clip/UV comparison is the certificate;
 // duplicate candidate identities remain unsupported.
 const auto& v=s.vb[0];k.words[i++]=uint64_t(v.buffer);k.words[i++]=v.offset;k.words[i++]=v.stride;
 return k;
}
bool create(Buffer& out,VkDeviceSize bytes,VkBufferUsageFlags usage){
 if(out.b)return true;
 VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;ci.usage=usage;
 if(dd->vk.CreateBuffer(dd->device,&ci,nullptr,&out.b)!=VK_SUCCESS)return false;
 VkMemoryRequirements req{};dd->vk.GetBufferMemoryRequirements(dd->device,out.b,&req);
 VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=find_memory_type(dd,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
 if(ai.memoryTypeIndex==UINT32_MAX || dd->vk.AllocateMemory(dd->device,&ai,nullptr,&out.m)!=VK_SUCCESS || dd->vk.BindBufferMemory(dd->device,out.b,out.m,0)!=VK_SUCCESS){
  dd->vk.DestroyBuffer(dd->device,out.b,nullptr);if(out.m)dd->vk.FreeMemory(dd->device,out.m,nullptr);out={};return false;
 }return true;
}
void destroy(Buffer& b){if(b.b)dd->vk.DestroyBuffer(dd->device,b.b,nullptr);if(b.m)dd->vk.FreeMemory(dd->device,b.m,nullptr);b={};}
void barrier(VkCommandBuffer cmd,VkPipelineStageFlags source,VkAccessFlags access,VkPipelineStageFlags target,VkAccessFlags next){
 VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=access;b.dstAccessMask=next;
 dd->vk.CmdPipelineBarrier(cmd,source,target,0,1,&b,0,nullptr,0,nullptr);
}
bool view(VkCommandBuffer cmd,uint32_t binding,VkImageViewType type,View& out){
 Bg3Binding b{};
 if(!query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,binding,0,&b) || !b.view ||
    !omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,binding,0) ||
    (b.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && b.layout!=VK_IMAGE_LAYOUT_GENERAL))return false;
 std::shared_lock lock(tracker().mu);auto i=tracker().views.find(b.view);if(i==tracker().views.end())return false;
 const auto& v=i->second;auto image=tracker().images.find(v.image);
 if(image==tracker().images.end() || !v.creation.known || !v.creation.id || v.creation.type!=type || v.creation.flags ||
    v.creation.extension!=VK_STRUCTURE_TYPE_MAX_ENUM || !v.creation.image_id || v.creation.image_id!=image->second.id ||
    v.range.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT || v.range.baseMipLevel!=0 || v.range.baseArrayLayer!=0)return false;
 uint32_t mips=0,layers=0;
 if(!trace_view::resolve_count(0,v.range.levelCount,image->second.ci.mipLevels,mips) ||
    !trace_view::resolve_count(0,v.range.layerCount,image->second.ci.arrayLayers,layers))return false;
 if(type==VK_IMAGE_VIEW_TYPE_2D && layers!=1)return false;
 if(type==VK_IMAGE_VIEW_TYPE_2D && v.format!=VK_FORMAT_R32G32B32A32_SFLOAT)return false;
 out={b.view,v.image,b.layout,v.creation.id,image->second.id};return true;
}
bool current(const View& v){
 std::shared_lock lock(tracker().mu);auto i=tracker().views.find(v.view);if(i==tracker().views.end())return false;
 auto image=tracker().images.find(v.image);
 return i->second.image==v.image && i->second.creation.id==v.id && image!=tracker().images.end() && image->second.id==v.image_id;
}
bool capture(VkCommandBuffer cmd,Material& material){
 // The exact non-instanced Deferred family only. No depth-binding guesses.
 auto* s=cmd_state(cmd);
 if(!s || s->vs!=0x3812bc980438747full || s->fs!=0x8995da1e4060f53bull)return false;
 auto binding=[&](uint32_t set,uint32_t slot,VkDeviceSize minimum,Bg3Binding& out){
  if(!query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,set,slot,0,&out) || !out.buffer)return false;
  if(out.range==VK_WHOLE_SIZE){auto size=geo_buffer_size(out.buffer);if(out.offset>size)return false;out.range=size-out.offset;}
  return out.range>=minimum && omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,set,slot,0);
 };
 if(!binding(1,0,112,material.engine) || !binding(1,1,16,material.physical_ubo) || !binding(0,12,128,material.tilesets))return false;
 if(!view(cmd,3,VK_IMAGE_VIEW_TYPE_2D,material.table) || !view(cmd,4,VK_IMAGE_VIEW_TYPE_2D_ARRAY,material.normal) ||
    !view(cmd,5,VK_IMAGE_VIEW_TYPE_2D_ARRAY,material.albedo) || !view(cmd,6,VK_IMAGE_VIEW_TYPE_2D_ARRAY,material.physical))return false;
 if(!omm::bound_sampler(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,2,0,material.sampler) || !material.sampler.immutable ||
    !omm::immutable_sampler_current(material.sampler))return false;
 const auto& c=material.sampler.sampler;
 if(!c.complete || c.info.flags || c.info.compareEnable || c.info.unnormalizedCoordinates ||
    c.reduction!=VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE)return false;
 return true;
}
}
bool enabled(){return on;}
void device_created(DeviceData* device){std::lock_guard lock(mutex);if(dd)return;dd=device;on=cfg_int("rt_hit_material",0)!=0 && cfg_int("rt_alpha",1)!=0 && cfg_int("rt_bvh_split",1)!=0;}
void device_destroyed(DeviceData* device){std::lock_guard lock(mutex);if(dd!=device)return;
 if(on)dd->vk.DeviceWaitIdle(dd->device);
 for(auto& s:slots){for(auto sampler:s.owned_samplers)dd->vk.DestroySampler(dd->device,sampler,nullptr);destroy(s.data);destroy(s.rows);s={};}
 if(gather_pipeline)dd->vk.DestroyPipeline(dd->device,gather_pipeline,nullptr);if(gather_layout)dd->vk.DestroyPipelineLayout(dd->device,gather_layout,nullptr);if(gather_dsl)dd->vk.DestroyDescriptorSetLayout(dd->device,gather_dsl,nullptr);
 gather_pipeline={};gather_layout={};gather_dsl={};pending.clear();owners.clear();marked.clear();deferred_slots.clear();dd=nullptr;on=false;
}
void prepass_begin(VkCommandBuffer cmd){if(!on)return;auto* s=cmd_state(cmd);if(!s)return;std::lock_guard lock(mutex);
 if(!one_time(s))return;
 // Pending entries own CPU metadata only, never GPU buffers. Drop known resets.
 for(auto i=pending.begin();i!=pending.end();)if(current_recording(i->second.command)!=i->second.recording)i=pending.erase(i);else ++i;
 if(pending.size()>=128 && !pending.contains(s->recording))return;
 auto& p=pending[s->recording];p={};p.command=handle(cmd);p.recording=s->recording;p.join.begin(p.command,p.recording);
}
void note_prepass(VkCommandBuffer cmd,CmdState* state,const DrawArgs& args,uint32_t record,VkDeviceSize rawOffset,uint32_t stride,uint32_t triangles){
 if(!on || !state || args.kind>1 || args.instances!=1 || args.count!=uint64_t(triangles)*3 || record>=max_records)return;
 AlphaSnapshot snapshot{};if(!alpha_snapshot(record,&snapshot) || snapshot.rec.kind!=1 || snapshot.rec.channel!=0)return;
 Pre pre{key(*state,args),snapshot,rawOffset,stride,triangles,record};
 std::lock_guard lock(mutex);auto p=pending.find(state->recording);if(p==pending.end())return;
 if(p->second.join.assign(handle(cmd),state->recording,pre.key,record))p->second.pre.push_back(pre);
}
void prepass_observe(VkCommandBuffer cmd,CmdState* state,const DrawArgs& args){
 if(!on || !state)return;std::lock_guard lock(mutex);auto p=pending.find(state->recording);if(p!=pending.end())p->second.join.note(handle(cmd),state->recording,key(*state,args),UINT32_MAX);
}
void scene_prepare(VkCommandBuffer cmd,uint32_t index,const RtCapture& cap){
 if(!on || index>=kSceneSlots)return;auto* state=cmd_state(cmd);if(!state)return;std::lock_guard lock(mutex);
 auto& s=slots[index];s.set={};s.handles.clear();s.materials.clear();s.pre.clear();s.prepared=false;s.attached=false;s.finalized=false;
 s.static_seen=s.claimed=s.captured=0;
 for(auto sampler:s.owned_samplers)dd->vk.DestroySampler(dd->device,sampler,nullptr);s.owned_samplers.clear();
 s.owners.clear();s.deferred_recording=0;
 // Owners are shared only by the bounded live slot set or a real submit pin.
 // ONE_TIME means a completed recording cannot legally be replayed later.
 std::set<uint64_t> live;for(const auto& slot:slots)live.insert(slot.owners.begin(),slot.owners.end());
 for(auto i=owners.begin();i!=owners.end();)if(!live.contains(i->first) && i->second.retired(current_recording(i->second.command),life_gpu_done()))i=owners.erase(i);else ++i;
 for(auto i=marked.begin();i!=marked.end();)if(!owners.contains(i->second))i=marked.erase(i);else ++i;
 auto p=pending.find(state->recording);
 if(!cap.split || p==pending.end() || !p->second.join.current(handle(cmd),state->recording) || !one_time(state)){
  if(cfg_int("rt_stats",0) && frame_index()%120==0)log("[hit material source] unsupported frame %llu level %u begin flags 0x%x pending %u split %u",
   (unsigned long long)frame_index(),unsigned(state->level),unsigned(state->begin_flags),unsigned(p!=pending.end()),unsigned(cap.split));return;
 }
 if(!create(s.data,stream_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT) ||
    !create(s.rows,row_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT))return;
 s.command=handle(cmd);s.recording=state->recording;s.frame=frame_index();s.cursor=0;s.generation=0;
 s.camera=cap.ubo;s.camera_offset=cap.ubo_offset;s.join=p->second.join;
 if(!mark_owner(cmd,s))return;
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
 dd->vk.CmdFillBuffer(cmd,s.rows.b,0,row_bytes,0);
 for(const auto& pre:p->second.pre){
  VkDeviceSize bytes=VkDeviceSize(pre.triangles)*3*pre.stride;
  if(!pre.stride || !byte_range(pre.raw,bytes,cap.xfb_size) || !byte_range(s.cursor,bytes,stream_bytes/2))continue;
  VkBufferCopy copy{pre.raw,s.cursor,bytes};dd->vk.CmdCopyBuffer(cmd,cap.xfb,s.data.b,1,&copy);
  auto saved=pre;saved.raw=s.cursor;s.pre.push_back(saved);s.cursor+=bytes;
 }
 s.prepared=true;pending.erase(p);
 if(cfg_int("rt_stats",0) && s.frame%120==0)log("[hit material source] frame %llu cmd %p recording %llu level %u begin flags 0x%x prepared %zu copied %llu bytes camera %p+%llu",
  (unsigned long long)s.frame,(void*)cmd,(unsigned long long)s.recording,unsigned(state->level),unsigned(state->begin_flags),s.pre.size(),(unsigned long long)s.cursor,(void*)s.camera,(unsigned long long)s.camera_offset);
 barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT);
}
bool attach(VkCommandBuffer cmd,uint32_t index,VkDescriptorSet set,VkBuffer dummyBuffer,VkImageView dummyArray,VkSampler dummySampler,uint32_t generation){
 if(!on || index>=kSceneSlots)return false;std::lock_guard lock(mutex);auto& s=slots[index];
 if(!set || !generation)return false;
 s.set=set;s.generation=generation;s.attached=true;
 // A256-byte ordinary dummy makes the consumer fail its row-size check before
 // any read, with no GPU initialization or ownerless writes on rejected paths.
 VkDescriptorBufferInfo buffers[2]={{s.prepared?s.rows.b:dummyBuffer,0,s.prepared?row_bytes:256},{s.prepared?s.data.b:dummyBuffer,0,s.prepared?stream_bytes:256}};
 std::vector<VkDescriptorImageInfo> caches(cache_slots,{VK_NULL_HANDLE,dummyArray,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
 std::vector<VkDescriptorImageInfo> samplers(sampler_slots,{dummySampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED});
 VkWriteDescriptorSet writes[4]{};for(uint32_t i=0;i<4;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=12+i;writes[i].descriptorCount=1;}
 for(uint32_t i=0;i<2;++i){writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=buffers+i;}
 writes[2].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[2].descriptorCount=cache_slots;writes[2].pImageInfo=caches.data();
 writes[3].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;writes[3].descriptorCount=sampler_slots;writes[3].pImageInfo=samplers.data();
 dd->vk.UpdateDescriptorSets(dd->device,4,writes,0,nullptr);return true;
}
bool deferred_begin(VkCommandBuffer cmd,CmdState* state,const DrawArgs& args,bool exactPipeline,uint32_t stride,VkBuffer* buffer,VkDeviceSize* offset,VkDeviceSize* bytes){
 if(!on || !state)return false;
 auto draw=key(*state,args);uint32_t record=0;int index=-1;
 {std::lock_guard lock(mutex);index=deferred_slot(cmd,state);if(index<0){
   static uint64_t logged=UINT64_MAX;
   if(state->vs==0x3812bc980438747full && state->fs==0x8995da1e4060f53bull && cfg_int("rt_stats",0) && frame_index()%120==0 && logged!=frame_index()){
    logged=frame_index();log("[hit material deferred] no unique source frame %llu cmd %p level %u begin flags 0x%x",(unsigned long long)logged,(void*)cmd,unsigned(state->level),unsigned(state->begin_flags));
   }return false;
  }
  auto& s=slots[index];deferred_slots[state->recording]=uint32_t(index);
  if(state->vs==0x3812bc980438747full && state->fs==0x8995da1e4060f53bull)++s.static_seen;
  if(s.deferred_recording && s.deferred_recording!=state->recording)return false;
  if(!s.deferred_recording)s.deferred_recording=state->recording;
  if(!s.join.claim(s.command,s.recording,draw,record))return false;
  ++s.claimed;
 }
 // Count unsupported matching deferred draws too, so they can invalidate an
 // earlier claim at finalization instead of silently choosing the audited draw.
 if(!cfg_int("rt_hit_material",0) || !exactPipeline || args.kind>1 || args.instances!=1 || stride<64)return false;
 Material material{};if(!capture(cmd,material))return false;
 std::lock_guard lock(mutex);auto& s=slots[index];
 if(!s.join.unique(draw,record) || !s.attached || s.finalized || s.deferred_recording!=state->recording || s.frame!=frame_index())return false;
 auto p=std::find_if(s.pre.begin(),s.pre.end(),[&](const auto& pre){return pre.record==record;});if(p==s.pre.end() || p->stride!=stride || args.count!=uint64_t(p->triangles)*3)return false;
 if(material.table.view!=p->alpha.tex || material.albedo.view!=p->alpha.cache)return false;
 // Hints only; execution-time GPU gather must match before publishing valid.
 std::memcpy(material.row.p0,p->alpha.rec.p0,32);std::memcpy(material.row.t,p->alpha.rec.t,128);
 *bytes=VkDeviceSize(p->triangles)*3*stride;if(!byte_range(s.cursor,*bytes,stream_bytes))return false;
 material.key=draw;material.row.valid=record+1;material.row.triangles=p->triangles;material.row.pre=uint32_t(p->raw);material.row.deferred=uint32_t(s.cursor);
 material.row.stride=stride;material.row.table=p->alpha.rec.tex;material.row.frame=uint32_t(s.frame);
 // alpha_snapshot's texture index belongs to this same freshly committed scene.
 material.row.sampler=uint32_t(s.materials.size());
 if(s.materials.size()>=sampler_slots || s.materials.size()*3+2>=cache_slots)return false;
 if(!mark_owner(cmd,s))return false;
 // The accepted reduction mode is exactly the core default weighted average;
 // omitting its redundant pNext also works on non-minmax devices.
 auto sampler_info=material.sampler.sampler.info;sampler_info.pNext=nullptr;VkSampler owned{};
 if(dd->vk.CreateSampler(dd->device,&sampler_info,nullptr,&owned)!=VK_SUCCESS)return false;
 s.owned_samplers.push_back(owned);
 material.row.normal=uint32_t(s.materials.size()*3);material.row.albedo=material.row.normal+1;material.row.physical=material.row.normal+2;
 *buffer=s.data.b;*offset=s.cursor;s.cursor+=*bytes;s.materials.push_back(material);++s.captured;return true;
}
void deferred_finish(VkCommandBuffer cmd,bool noGraphicsStorageWrites){
 if(!on)return;auto* state=cmd_state(cmd);if(!state)return;std::lock_guard lock(mutex);
 auto target=deferred_slots.find(state->recording);if(target==deferred_slots.end())return;auto& s=slots[target->second];deferred_slots.erase(target);
 if(!s.attached || s.frame!=frame_index() || !mark_owner(cmd,s))return;
 if(s.finalized || s.deferred_recording!=state->recording){
  dd->vk.CmdFillBuffer(cmd,s.rows.b,0,16,0);
  barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);return;
 }
 s.finalized=true;
 const bool requested=cfg_int("rt_hit_material",0)!=0 && noGraphicsStorageWrites && gather_ready();
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
 std::vector<VkDescriptorImageInfo> caches,samplers;uint32_t accepted=0;
 for(auto& m:s.materials){
  bool valid=requested && s.join.unique(m.key,m.row.valid-1) && current(m.table) && current(m.normal) && current(m.albedo) && current(m.physical) && omm::immutable_sampler_current(m.sampler);
  caches.push_back({VK_NULL_HANDLE,m.normal.view,m.normal.layout});caches.push_back({VK_NULL_HANDLE,m.albedo.view,m.albedo.layout});caches.push_back({VK_NULL_HANDLE,m.physical.view,m.physical.layout});
  samplers.push_back({s.owned_samplers[samplers.size()],VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED});
  for(const auto& v:{m.table,m.normal,m.albedo,m.physical}){s.handles.push_back(uint64_t(v.view));s.handles.push_back(uint64_t(v.image));}
  // Invalid rows stay zero: never publish stale/null descriptors as live records.
  if(!valid)continue;
  auto record=m.row.valid-1;auto staged=m.row;staged.valid=0;
  dd->vk.CmdUpdateBuffer(cmd,s.rows.b,16+VkDeviceSize(record)*record_bytes,record_bytes,&staged);++accepted;
 }
 // All descriptor objects referenced by the array must remain alive, even rows
 // subsequently invalidated by a duplicate. Decline the entire update if stale.
 bool descriptors=true;for(const auto& m:s.materials)descriptors&=current(m.normal)&&current(m.albedo)&&current(m.physical)&&omm::immutable_sampler_current(m.sampler);
 if(!descriptors){
  dd->vk.CmdFillBuffer(cmd,s.rows.b,0,row_bytes,0);
  barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);return;
 }
 if(!caches.empty()){
  VkWriteDescriptorSet writes[2]{};for(unsigned i=0;i<2;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=s.set;writes[i].dstBinding=14+i;}
  writes[0].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[0].descriptorCount=uint32_t(caches.size());writes[0].pImageInfo=caches.data();
  writes[1].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;writes[1].descriptorCount=uint32_t(samplers.size());writes[1].pImageInfo=samplers.data();dd->vk.UpdateDescriptorSets(dd->device,2,writes,0,nullptr);
 }
 uint32_t header[4]={uint32_t(s.frame),s.generation,accepted,1};dd->vk.CmdUpdateBuffer(cmd,s.rows.b,0,16,header);
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
 if(requested)for(auto& m:s.materials){
  if(!s.join.unique(m.key,m.row.valid-1) || !current(m.table) || !current(m.normal) || !current(m.albedo) || !current(m.physical) || !omm::immutable_sampler_current(m.sampler))continue;
  VkDescriptorBufferInfo info[4]={{m.engine.buffer,m.engine.offset,m.engine.range},{m.physical_ubo.buffer,m.physical_ubo.offset,m.physical_ubo.range},
   {m.tilesets.buffer,m.tilesets.offset,m.tilesets.range},{s.rows.b,0,row_bytes}};
  VkWriteDescriptorSet writes[4]{};for(uint32_t i=0;i<4;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=info+i;}
  uint32_t push[4]={16u+(m.row.valid-1)*record_bytes,m.row.valid-1,0,0};
  dd->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,gather_pipeline);dd->vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,gather_layout,0,4,writes);
  dd->vk.CmdPushConstants(cmd,gather_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,push);dd->vk.CmdDispatch(cmd,1,1,1);
 }
 barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
 if(requested)restore_bindings(cmd,VK_PIPELINE_BIND_POINT_COMPUTE);
 for(auto h:s.handles)life_note(cmd,h);
 if(cfg_int("rt_stats",0) && s.frame%120==0)log("[hit material] static VT scene %u frame %llu deferred cmd %p recording %llu level %u begin flags 0x%x; prepared %zu static seen %u claimed %u captured %u CPU-qualified %u; GPU gather/clip/UV/sampling still required",
  s.generation,(unsigned long long)s.frame,(void*)cmd,(unsigned long long)state->recording,unsigned(state->level),unsigned(state->begin_flags),s.pre.size(),s.static_seen,s.claimed,s.captured,accepted);
}
void slot_in_use(uint32_t index,uint64_t frame){if(!on || index>=kSceneSlots)return;std::lock_guard lock(mutex);for(auto h:slots[index].handles)life_extend(h,frame);}
bool slot_reusable(uint32_t index,uint64_t gpuDone){
 if(!on || index>=kSceneSlots)return true;std::lock_guard lock(mutex);
 for(auto id:slots[index].owners){auto owner=owners.find(id);if(owner==owners.end() || !owner->second.retired(current_recording(owner->second.command),gpuDone))return false;}
 return true;
}
bool register_use(VkCommandBuffer cmd,uint32_t index){
 if(!on || !cfg_int("rt_hit_material",0) || index>=kSceneSlots)return false;
 auto* state=cmd_state(cmd);if(!one_time(state))return false;
 std::lock_guard lock(mutex);auto& slot=slots[index];
 if(!slot.prepared || !slot.attached || slot.frame!=frame_index())return false;
 Bg3Binding camera{};if(!query_binding(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,0,2,0,&camera) || camera.buffer!=slot.camera || camera.offset!=slot.camera_offset)return false;
 return mark_owner(cmd,slot);
}
bool recording_needs_submit_promotion(VkCommandBuffer cmd){
 if(!on)return false;auto recording=cmd_recording(cmd);if(!recording)return false;
 std::lock_guard lock(mutex);auto i=marked.find(cmd);return i!=marked.end() && i->second==recording;
}
PreparedSubmission before_submit(std::span<const VkCommandBuffer> commands){
 PreparedSubmission prepared;if(!on)return prepared;std::lock_guard lock(mutex);std::set<uint64_t> unique;
 for(auto cmd:commands){auto recording=cmd_recording(cmd);if(!recording)continue;auto i=marked.find(cmd);
  if(i!=marked.end() && i->second==recording)unique.insert(i->second);
 }
 for(auto id:unique){auto i=owners.find(id);if(i!=owners.end() && i->second.reserve())prepared.owners.push_back(id);}
 return prepared;
}
void after_submit(const PreparedSubmission& prepared,VkQueue queue,bool success){
 if(!on)return;std::lock_guard lock(mutex);
 for(auto id:prepared.owners){auto i=owners.find(id);if(i==owners.end())continue;
  i->second.finish(success,frame_index());
  if(success && queue!=dd->graphics_queue){i->second.poisoned=true;log("[hit material] unsupported foreign queue: retain slot resources until device idle");}
 }
}
}
