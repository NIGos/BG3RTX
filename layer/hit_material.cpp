#include "hit_material.h"
#include "omm_sampler.h"
#include "../common/native_hit_capability.h"
#include "../common/native_hit_join.h"
#include "../common/native_hit_order.h"
#include "../common/native_hit_inputs.h"
#include "../common/native_hit_tileset_inputs.h"
#include "../common/native_hit_resources.h"
#include "../common/native_hit_records.h"
#include "../common/native_hit_qualify.h"
#include "../common/native_hit_pool.h"
#include "../common/native_hit_instanced.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <memory>
#include <set>
namespace bg3 {bool geo_native_tileset_enabled();bool geo_native_tileset_material_inputs(VkPipeline,native_hit::TilesetInputs*);}
namespace bg3::native_hit {
namespace {
// XFB/basis storage. One granule is a deferred chunk and the source-stream rounding; each
// cap bounds one buffer kind (counted as overflow in "[hit material capacity]").
// Before 2026-10-31: one fixed 32 MB stream each (source copies at most 16 MB).
constexpr VkDeviceSize stream_bytes=32u<<20;
constexpr VkDeviceSize source_cap=128ull<<20;     // per scene slot: the copied prepass geometry
constexpr uint32_t deferred_chunks=4;             // per deferred packet: 4 x 32 MB XFB
constexpr VkDeviceSize query_cap=256ull<<20;      // per query: prepass + deferred basis copies
constexpr VkDeviceSize row_payload=16u+VkDeviceSize(record_bytes)*nh_material_rows;
constexpr VkDeviceSize row_bytes=row_payload+16u;
static_assert(sampler_slots==nh_sampler_slots && cache_slots==nh_cache_slots && material_slots==nh_deferred_rows);
static_assert(query_cap<=UINT32_MAX && source_cap<=UINT32_MAX && stream_bytes<=UINT32_MAX); // uint32 row offsets
constexpr VkDeviceSize packet_rows=16u+VkDeviceSize(record_bytes)*material_slots;
struct Buffer {VkBuffer b{};VkDeviceMemory m{};void* mapped{};};
struct Row {
 uint32_t valid{},triangles{},pre{},deferred{};
 uint32_t stride{},table{},sampler{},frame{};
 uint32_t normal{},albedo{},physical{},reserved{};
 float p0[4]{},p1[4]{},t[32]{},parameters[4]{};
 uint32_t padding[8]{}; // pool rows: the detail words (native_hit_records.h nh_pool_row_detail)
};
static_assert(sizeof(Row)==record_bytes);
static_assert(offsetof(Row,pre)==8 && offsetof(Row,deferred)==12 && offsetof(Row,stride)==16);
static_assert(offsetof(Row,p0)==48 && offsetof(Row,t)==80 && offsetof(Row,parameters)==208);
struct View {VkImageView view{};VkImage image{};VkImageLayout layout{};uint64_t id{};uint32_t image_id{};};
struct Pre {DrawKey key;AlphaSnapshot alpha;VkDeviceSize raw{};uint32_t stride{},triangles{},record{};uint64_t vs{},fs{};bool opaque{};uint32_t family{};};
// rt_hit_material_instanced (native_hit_instanced.h) counters per family (0 unused, 1 wind, 2 alpha-test 2S).
struct InstancedCount {uint32_t prepass{},seen{},pipeline{},bindings{},stream{},captured{},mapped{};uint64_t triangles{};};
struct Material {uint32_t source_raw{},deferred_raw{},snapshot{},chunk{},reflectance{};Row row;DrawKey key;View table,normal,albedo,physical;omm::BoundSampler sampler;Bg3Binding engine,physical_ubo,tilesets;std::array<View,9> tileset_views{};
 // LayoutDetail recipes (native_vt_surface.h, pool only): 2D detail normal 1:3, mask 1:8, sampler 0:7,
 // material UBO 1:1 strength/tiling/mask power/roughness add (bits).
 uint32_t layout{};View detail_normal,detail_mask;omm::BoundSampler detail_sampler;uint32_t detail[4]{};
 // LayoutIridescent (pool only): 2D views 1:2..1:6 (sampler 0:7 in `sampler`), 1:1 _m0.._m5 (bits).
 std::array<View,5> plain{};uint32_t plain_floats[6]{};
 // Instanced per-draw rows (native_hit_instanced.h): family, and whether the gather reads the
 // packet's staged engine/1:1 bytes (Deferred::staged at local x 512) instead of the draw's UBOs.
 uint32_t family{};bool staged{};};
struct Deferred {
 std::vector<Buffer> chunks;Buffer rows;uint64_t command{},recording{},frame{};VkBuffer camera{};VkDeviceSize camera_offset{},cursor{},total{};
 DrawJoin join;std::vector<Material> materials;std::vector<VkSampler> samplers;ResourceBudget resources;
 bool finalized{},valid{true};uint32_t seen{},audited_unique{},audited_duplicate{},family_declined{},pipeline_declined{},binding_declined{};
 uint32_t slot_overflow{},sampler_overflow{},cache_overflow{},stream_overflow{};
 FamilyCensus declined; // G-buffer draws of other native pairs (what to qualify next)
 uint32_t uncaptured_draws{},uncaptured_instanced{};uint64_t uncaptured_vertices{}; // audited FS, no geometry path (native_vt::CaptureNone)
 Buffer staged;InstancedCount instanced[3]; // rt_hit_material_instanced: staged gather inputs (host, 512 B per row)
};
struct Slot {
 Buffer data;VkDeviceSize data_bytes{};VkDescriptorSet set{};uint64_t command{},recording{},frame{};uint32_t generation{};
 uint32_t opaque_used{},opaque_overflow{},source_overflow{};bool capacity_logged{};uint32_t instanced_prepass[3]{};
 VkBuffer camera{};VkDeviceSize camera_offset{},cursor{};VkImageView dummy_array{};VkSampler dummy_sampler{};DrawJoin join;std::vector<Pre> pre;
 std::vector<uint64_t> handles,owners;bool attached{},prepared{};
};
struct Query {
 Buffer data,rows;VkDescriptorPool pool{};VkDescriptorSet set{};
 uint64_t command{},recording{},source{};uint32_t slot{},generation{};std::shared_ptr<Deferred> deferred;
 PendingPublication publication;std::vector<Material> materials;
 // Pool material rows (row.reserved 3, row.valid = slot + 1): their gather inputs (engine and
 // physical UBO images of the record's captured bytes, 512 B each) and the samplers they add.
 std::vector<Material> pool_rows;Buffer pool_ubo;std::vector<VkSampler> pool_samplers;VkDeviceSize rows_bytes{};
};
constexpr VkDeviceSize pool_rows_bytes=VkDeviceSize(nh_pool_rows)*record_bytes;
static_assert(nh_pool_row_base==row_bytes,"pool rows follow the publication word");
DeviceData* dd{};bool on{};std::mutex mutex;
// rt_hit_material_instanced: its startup value (the G-buffer VS frame patch), then the live key.
bool instanced_on{};
bool instanced_live(){return instanced_on && cfg_hot(CfgHot::rt_hit_material_instanced)!=0;}
struct Pending {uint64_t command{},recording{};uint32_t candidate_draws{},opaque_count{},opaque_overflow{};DrawJoin join;std::vector<Pre> pre;uint32_t instanced_prepass[3]{};};
std::unordered_map<uint64_t,Pending> pending;
std::unordered_map<uint64_t,RecordingOwner> owners;
std::unordered_map<uint64_t,VkQueue> submitted_queues;
std::unordered_map<VkCommandBuffer,uint64_t> marked;
std::unordered_map<uint64_t,std::shared_ptr<Deferred>> packets;
std::unordered_map<uint64_t,std::unique_ptr<Query>> queries;
Slot slots[kSceneSlots];
VkDescriptorSetLayout gather_dsl{},join_dsl{};VkPipelineLayout gather_layout{},join_layout{};VkPipeline gather_pipeline{},join_pipeline{};
VkPipeline tileset_gather_pipeline{};
// rt/mesh_extract_basis.hlsl (pool UV/basis captures): camera UBO, XFB, mesh pool, job status.
VkDescriptorSetLayout basis_dsl{};VkPipelineLayout basis_layout{};VkPipeline basis_pipeline{};
bool compute_ready(bool join,bool tileset=false){
 auto& pipeline=join?join_pipeline:(tileset?tileset_gather_pipeline:gather_pipeline);auto& dsl=join?join_dsl:gather_dsl;auto& layout=join?join_layout:gather_layout;
 if(pipeline)return true;
 Bg3ShaderBlob blob{};if(!dd->vk.CmdPushDescriptorSet || !shader_get(join?"rt/hit_material_join.hlsl":(tileset?"rt/hit_material_tileset_gather.hlsl":"rt/hit_material_gather.hlsl"),"main","cs_6_6",&blob))return false;
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
bool basis_ready(){
 if(basis_pipeline)return true;
 Bg3ShaderBlob blob{};if(!dd->vk.CmdPushDescriptorSet || !shader_get("rt/mesh_extract_basis.hlsl","main","cs_6_6",&blob))return false;
 VkDescriptorSetLayoutBinding bindings[4]{};for(uint32_t i=0;i<4;++i)bindings[i]={i,i?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT};
 VkDescriptorSetLayoutCreateInfo d{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};d.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;d.bindingCount=4;d.pBindings=bindings;
 if(!basis_dsl && dd->vk.CreateDescriptorSetLayout(dd->device,&d,nullptr,&basis_dsl)!=VK_SUCCESS)return false;
 VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,48};VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};l.setLayoutCount=1;l.pSetLayouts=&basis_dsl;l.pushConstantRangeCount=1;l.pPushConstantRanges=&range;
 if(!basis_layout && dd->vk.CreatePipelineLayout(dd->device,&l,nullptr,&basis_layout)!=VK_SUCCESS)return false;
 VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};m.codeSize=blob.size_bytes;m.pCode=blob.code;VkShaderModule module{};
 if(dd->vk.CreateShaderModule(dd->device,&m,nullptr,&module)!=VK_SUCCESS)return false;
 VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};p.layout=basis_layout;p.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main"};
 auto result=dd->vk.CreateComputePipelines(dd->device,VK_NULL_HANDLE,1,&p,nullptr,&basis_pipeline);dd->vk.DestroyShaderModule(dd->device,module,nullptr);return result==VK_SUCCESS;
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
bool tileset_active(){return tileset_material_enabled(geo_native_tileset_enabled(),cfg_hot(CfgHot::rt_native_tileset)!=0);}
DrawKey key(VkCommandBuffer cmd,const CmdState& s,const DrawArgs& a){
 DrawKey k;size_t i=0;for(uint64_t v:{uint64_t(a.kind),uint64_t(a.count),uint64_t(a.instances),uint64_t(a.first),uint64_t(uint32_t(a.vertex_offset)),uint64_t(a.first_instance)})k.words[i++]=v;
 k.words[i++]=uint64_t(s.ib);k.words[i++]=s.ib_offset;k.words[i++]=uint64_t(s.ib_type);
 // Legacy candidate for other shader families. The audited static pair below
 // resolves its actual active inputs instead of assuming vertex stream 0.
 // Neither CPU identity is a certificate: exact GPU clip/UV/parameter checks
 // remain mandatory, and duplicate candidate identities stay unsupported.
 const auto& v=s.vb[0];k.words[i++]=uint64_t(v.buffer);k.words[i++]=v.offset;k.words[i++]=v.stride;
 if((tileset_depth(s.vs,s.fs) || tileset_deferred(s.vs,s.fs)) && tileset_active()){
  TilesetInputs inputs{};std::array<InputStream,6> streams{};
  bool ready=geo_native_tileset_material_inputs(s.pipe[0],&inputs) && inputs.valid;
  for(uint32_t j=0;ready && j<6;++j){const auto& a=inputs.attributes[j];
   if(a.binding>=CmdState::kMaxVertexBindings){ready=false;break;}const auto& vb=s.vb[a.binding];auto stride=a.dynamic_stride?vb.stride:a.stride;
   ready=vb.buffer && stride && vb.offset<=UINT64_MAX-a.offset && vb.offset+a.offset<geo_buffer_size(vb.buffer);
   streams[j]={uint64_t(vb.buffer),vb.offset+a.offset,stride,uint32_t(a.format)};
  }
  DrawKey canonical{};if(ready && tileset_key(k,streams,canonical))return canonical;
  k.words[33]=0x4e485430u;k.words[34]=s.vs;return k;
 }
 // rt_hit_material_instanced: the depth draw (by VS) and the G-buffer draw (exact pair) of one
 // instanced wind / 2S mesh share the draw-word + index-range key (native_hit_instanced.h).
 if(instanced_live()){const auto f=instanced::deferred_family(s.vs,s.fs),d=instanced::depth_family(s.vs);
  if(f!=instanced::None || d!=instanced::None)return instanced::key(k,f!=instanced::None?f:d);}
 const bool depth=static_depth_family(s.vs,s.fs),deferred=audited_static_family(s.vs,s.fs);
 if(!depth && !deferred)return k;
 StaticInputs attributes{};StaticKeyInputs inputs{};Bg3Binding engine{},pulled{};
 const auto layout=depth_input_layout(s.vs,s.fs);const uint32_t engine_bytes=layout.engine_bytes,pull_offset=layout.pull_offset;
 bool available=geo_native_hit_inputs(s.pipe[0],&attributes) &&
  query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,0,0,&engine) && engine.buffer && engine.range>=engine_bytes && geo_uniform_buffer_range(engine.buffer,engine.offset,engine_bytes);
 if(available){
  inputs.engine_buffer=uint64_t(engine.buffer);inputs.engine_offset=engine.offset;
  inputs.pull_hint_known=read_mapped(engine.buffer,engine.offset+pull_offset,&inputs.pull_index,4);
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
 if(cfg_hot(CfgHot::rt_stats) && frame%120==0){if(logged_frame!=frame){logged_frame=frame;logged[0]=logged[1]=0;}
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
bool view(VkCommandBuffer cmd,uint32_t binding,VkImageViewType type,View& out,ViewFacts& fact,bool native2d=false){
 auto& b=fact.binding;fact.queried=query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,binding,0,&b);
 bool frozen=omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,binding,0,&fact.ordinary);
 if(!fact.queried || !b.view)return false;
 std::shared_lock lock(tracker().mu);auto i=tracker().views.find(b.view);if(i==tracker().views.end())return false;
 const auto& v=i->second;fact.tracked=true;fact.known=v.creation.known;fact.id=v.creation.id;fact.image_id=v.creation.image_id;
 fact.type=v.creation.type;fact.format=v.format;fact.flags=v.creation.flags;fact.extension=v.creation.extension;fact.range=v.range;
 auto image=tracker().images.find(v.image);if(image==tracker().images.end())return false;
 fact.current_image_id=image->second.id;fact.image_current=v.creation.image_id && v.creation.image_id==image->second.id;
 bool range=trace_view::resolve_count(v.range.baseMipLevel,v.range.levelCount,image->second.ci.mipLevels,fact.mips) &&
  trace_view::resolve_count(v.range.baseArrayLayer,v.range.layerCount,image->second.ci.arrayLayers,fact.layers);
 bool valid=frozen && (b.layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || b.layout==VK_IMAGE_LAYOUT_GENERAL) &&
  fact.known && fact.id && fact.type==type && !fact.flags && fact.extension==VK_STRUCTURE_TYPE_MAX_ENUM && fact.image_current &&
  v.range.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT && (native2d || (v.range.baseMipLevel==0 && v.range.baseArrayLayer==0)) && range &&
  (type!=VK_IMAGE_VIEW_TYPE_2D || (fact.layers==1 && (native2d || v.format==VK_FORMAT_R32G32B32A32_SFLOAT)));
 if(!valid)return false;out={b.view,v.image,b.layout,v.creation.id,image->second.id};return true;
}
bool current(const View& v){
 std::shared_lock lock(tracker().mu);auto i=tracker().views.find(v.view);if(i==tracker().views.end())return false;
 auto image=tracker().images.find(v.image);
 return i->second.image==v.image && i->second.creation.id==v.id && image!=tracker().images.end() && image->second.id==v.image_id;
}
bool material_current(const Material& m){
 if(m.row.reserved==2){for(const auto& v:m.tileset_views)if(!current(v))return false;return true;}
 return current(m.table)&&current(m.normal)&&current(m.albedo)&&current(m.physical);
}
bool capture_tileset(VkCommandBuffer cmd,Material& material){
 auto* state=cmd_state(cmd);if(!state || !tileset_deferred(state->vs,state->fs))return false;
 material.row.reserved=2;uint32_t failed=0;
 for(uint32_t n=0;n<2;++n){auto& out=n?material.physical_ubo:material.engine;
  bool known=query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,n,0,&out) &&
   omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,1,n,0) && out.buffer && out.range>=16 && geo_uniform_buffer_range(out.buffer,out.offset,16);
  if(!known)failed|=1u<<n;
 }
 ViewFacts facts[9]{};
 for(uint32_t i=0;i<9;++i)if(!view(cmd,2+i,VK_IMAGE_VIEW_TYPE_2D,material.tileset_views[i],facts[i],true))failed|=1u<<(2+i);
 if(!omm::bound_sampler(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,7,0,material.sampler))failed|=1u<<11;
 if(!omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,7,0) || !omm::ordinary_sampler_current(material.sampler))failed|=1u<<12;
 const auto& c=material.sampler.sampler;
 if(!c.complete || c.info.flags || c.info.compareEnable || c.info.unnormalizedCoordinates || c.reduction!=VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE)failed|=1u<<13;
 if(failed && cfg_hot(CfgHot::rt_stats) && frame_index()%120==0){
  static uint64_t frame=UINT64_MAX;static uint32_t count=0;if(frame!=frame_index()){frame=frame_index();count=0;}
  if(count++<2){log("[hit material tileset capture] frame %llu failures 0x%x (UBO bits0..1, native2D views2..10, sampler metadata/frozen/policy11..13); sampler generation %llu",(unsigned long long)frame,failed,(unsigned long long)c.id);
   for(uint32_t i=0;i<9;++i)if(failed&(1u<<(2+i))){const auto& f=facts[i];log("[hit material tileset view] binding %u queried/known/current %u/%u/%u format/type %u/%u mip/layer %u/%u extent %u/%u frozen set/layout/version %llu/%llu/%llu",
    i+2,unsigned(f.queried),unsigned(f.known),unsigned(f.image_current),unsigned(f.format),unsigned(f.type),f.range.baseMipLevel,f.range.baseArrayLayer,f.mips,f.layers,(unsigned long long)f.ordinary.set_id,(unsigned long long)f.ordinary.layout_id,(unsigned long long)f.ordinary.version);}
  }
 }
 return failed==0;
}
// recipe: a pool pair (native_hit_pool.h, persistent record); nullptr: the audited per-draw pair.
// recipe of an instanced pair (native_hit_instanced.h): the same LayoutBase bindings, per-draw row.
bool capture(VkCommandBuffer cmd,Material& material,const native_vt::Recipe* recipe=nullptr,uint32_t* failures=nullptr){
 auto* state=cmd_state(cmd);
 if(!state || !(recipe?(pool::pool_recipe(state->vs,state->fs)==recipe || instanced::recipe(state->vs,state->fs)==recipe):audited_static_family(state->vs,state->fs)))return false;
 omm::OrdinaryBindingFacts buffer_facts[3]{};bool queried[3]{};uint32_t failed=0;
 auto buffer=[&](uint32_t n,uint32_t set,uint32_t slot,VkDeviceSize minimum,Bg3Binding& out){
  queried[n]=query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,set,slot,0,&out);
  bool frozen=omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,set,slot,0,&buffer_facts[n]);
  if(out.range==VK_WHOLE_SIZE){auto size=geo_buffer_size(out.buffer);if(out.offset<=size)out.range=size-out.offset;}
  bool valid=queried[n] && out.buffer && out.range>=minimum && frozen;if(!valid)failed|=1u<<n;
 };
 // Reflectance mode of the pair (native_vt_surface.h): the 0.5 variants have no 1:1 binding.
 const auto* vt=recipe?recipe:native_vt::recipe(state->fs);material.reflectance=vt?vt->reflectance:native_vt::ReflectanceUbo;
 // LayoutIridescent reads no tile-set table (set 0:12) and only 2D views 1:2..1:6 with sampler 0:7.
 const bool plain=vt && vt->layout==native_vt::LayoutIridescent;
 buffer(0,1,0,recipe?pool::engine_bytes(*recipe):112u,material.engine);if(!plain)buffer(2,0,12,128,material.tilesets);
 // 1:1 bytes the recipe reads (FS 8e49cde8: its reflectance is byte 16 of 32); the per-draw pairs 16.
 const uint32_t ubo_bytes=vt?std::max(16u,native_vt::material_bytes(*vt)):16u;
 if(material.reflectance!=native_vt::ReflectanceConstant)buffer(1,1,1,ubo_bytes,material.physical_ubo);else material.physical_ubo={};
 // LayoutDetail: the VT table/normal/albedo/physical move to 1:4..7; 1:3 and 1:8 are the 2D detail
 // normal and mask, sampled with the set 0 binding 7 sampler (FS 613ead9c).
 const bool detail=vt && vt->layout==native_vt::LayoutDetail;const uint32_t first=detail?4u:3u;
 material.layout=detail?native_vt::LayoutDetail:plain?native_vt::LayoutIridescent:native_vt::LayoutBase;
 ViewFacts views[4]{};View* targets[4]={&material.table,&material.normal,&material.albedo,&material.physical};
 if(plain){ViewFacts f[5]{};for(uint32_t i=0;i<5;++i)if(!view(cmd,2+i,VK_IMAGE_VIEW_TYPE_2D,material.plain[i],f[i],true))failed|=1u<<(i<4?3+i:11);}
 else for(uint32_t i=0;i<4;++i)if(!view(cmd,first+i,i?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D,*targets[i],views[i]))failed|=1u<<(3+i);
 if(detail){
  ViewFacts d[2]{};
  if(!view(cmd,3,VK_IMAGE_VIEW_TYPE_2D,material.detail_normal,d[0],true) || !view(cmd,8,VK_IMAGE_VIEW_TYPE_2D,material.detail_mask,d[1],true))failed|=1u<<11;
  const bool bound=omm::bound_sampler(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,7,0,material.detail_sampler);const auto& ds=material.detail_sampler.sampler;
  if(!bound || !omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,7,0) || !omm::ordinary_sampler_current(material.detail_sampler) ||
     !ds.complete || ds.info.flags || ds.info.compareEnable || ds.info.unnormalizedCoordinates || ds.reduction!=VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE)failed|=1u<<12;
 }
 const uint32_t sampler_set=plain?0u:1u,sampler_slot=plain?7u:2u;
 bool sampler_bound=omm::bound_sampler(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,sampler_set,sampler_slot,0,material.sampler);
 omm::OrdinaryBindingFacts sampler_binding{};
 bool sampler_frozen=omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,sampler_set,sampler_slot,0,&sampler_binding);
 if(!sampler_bound)failed|=1u<<7;
 if(!sampler_frozen || !omm::ordinary_sampler_current(material.sampler))failed|=1u<<8;
 const auto& c=material.sampler.sampler;
 if(!c.complete || c.info.flags || c.info.compareEnable || c.info.unnormalizedCoordinates || c.reduction!=VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE)failed|=1u<<9;
 if(failures)*failures=failed;
 if(!failed)return true;
 if(recipe)return false; // pool failures are counted by bit in "[hit material pool]"
 // Two full prerequisite records per 120-frame window, not one missing-term
 // experiment at a time. These are actual draw bindings, not a native-proof claim.
 static uint64_t logged_frame=UINT64_MAX;static uint32_t logged=0;const auto frame=frame_index();
 if(cfg_hot(CfgHot::rt_stats) && frame%120==0){if(logged_frame!=frame){logged_frame=frame;logged=0;}
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
void destroy_query(Query& q){if(q.pool)dd->vk.DestroyDescriptorPool(dd->device,q.pool,nullptr);destroy(q.data);destroy(q.rows);destroy(q.pool_ubo);
 for(auto sampler:q.pool_samplers)dd->vk.DestroySampler(dd->device,sampler,nullptr);q.pool_samplers.clear();}
void destroy_packet(Deferred& p){for(auto sampler:p.samplers)dd->vk.DestroySampler(dd->device,sampler,nullptr);for(auto& c:p.chunks)destroy(c);p.chunks.clear();destroy(p.rows);destroy(p.staged);}
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
// Pool material records (common/native_hit_pool.h). pool_mutex is a leaf: it is taken under
// the BVH build lock (pool_lookup, pool_evicted) and never held while calling out, except the
// tracker/sampler currency checks (their own leaf locks).
struct PoolMaterial {
 View table,normal,albedo,physical;omm::BoundSampler sampler;uint32_t engine[12]{},reflectance{};
 const native_vt::Recipe* recipe{}; // the record's pair (offsets of engine[], reflectance mode)
 View detail_normal,detail_mask;omm::BoundSampler detail_sampler;uint32_t detail[4]{}; // LayoutDetail only
 bool detail_layout() const {return recipe && recipe->layout==native_vt::LayoutDetail;}
 std::array<View,5> plain{};uint32_t plain_floats[6]{}; // LayoutIridescent only (sampler 0:7 in `sampler`)
 bool plain_layout() const {return recipe && recipe->layout==native_vt::LayoutIridescent;}
 uint64_t checked{};bool current{}; // currency of the last check (once per frame per record)
 // Persistent across material refreshes: the pool mesh (from the BVH build) and its UV/basis.
 PoolMesh mesh{};pool::BasisTrack basis{};
};
// A refresh replaces the captured material, never the mesh/basis state.
void assign_material(PoolMaterial& to,const PoolMaterial& from){const auto mesh=to.mesh;const auto basis=to.basis;to=from;to.mesh=mesh;to.basis=basis;}
std::mutex pool_mutex;
pool::Table<PoolMaterial> pool_table;
pool::Slots pool_slots;
// One UV/basis capture (native_hit_pool.h): the G-buffer XFB of instance 0, the instance-0
// matrix (read at record, verified on the GPU by the position check) and the GPU status, in
// `host`: matrix 0..47, max error 48, invalid 52, done 56, ran 60.
struct PoolJob {
 uint64_t geometry{},gen{},recording{},frame{},epoch{};Buffer xfb,host;
 uint32_t tris{},xfb_stride{},pool_stride{};VkDeviceSize pool_off{},basis_off{},xfb_bytes{};Bg3Binding camera{};
 bool recorded{},resolved{};
};
std::vector<PoolJob> pool_jobs;
struct PoolBasisStats {uint64_t jobs{},results[size_t(pool::BasisResult::Count)]{},rows{},row_instances{},no_slot{};float err_max{};uint64_t frame{};uint32_t frame_jobs{};};
PoolBasisStats pool_basis;
// This frame's tile-set table binding (set 0 binding 12) of the STI G-buffer draws (pool rows' gather).
struct PoolTilesets {Bg3Binding binding{};uint64_t frame{};} pool_tilesets;
constexpr uint32_t kPoolDone=0x454e4f44u,kPoolRan=0x42564853u;
struct PoolObserved {uint64_t frame{};uint32_t draws{},recipe{},captured{},throttled{},no_key{},overflow{};uint32_t failures[13]{};};
PoolObserved pool_obs[2]; // this frame and the previous one (the G-buffer may record after the BVH build)
uint64_t pool_counter_frame{};
void pool_capture_bits(uint32_t bits,uint32_t (&out)[13]){for(uint32_t b=0;b<13;++b)if(bits&(1u<<b))++out[b];}
PoolObserved& pool_observed(uint64_t frame){
 if(pool_obs[0].frame!=frame){pool_obs[1]=pool_obs[0];pool_obs[0]={};pool_obs[0].frame=frame;}
 return pool_obs[0];
}
void pool_log(uint64_t frame){
 const auto& c=pool_table.counters();const auto& t=pool_table.totals();uint64_t draws=0,instances=0;
 for(size_t i=0;i<size_t(pool::Status::Count);++i){draws+=c.draws[i];instances+=c.instances[i];}
 const PoolObserved empty{};
 const PoolObserved* o=pool_obs[0].frame==frame?&pool_obs[0]:pool_obs[1].frame==frame?&pool_obs[1]:&empty;
 const auto q=size_t(pool::Status::Qualified);
 log("[hit material pool] frame %llu pool draws %llu instances %llu; qualified draws %llu instances %llu (%.1f%% of pool instances); records %zu stored %llu refreshed %llu conflicts %llu evicted %llu mesh replaced %llu invalidated (stale views) %llu overflow %llu; hits use a pool row once its UV/basis is ready ([hit material pool basis])",
  (unsigned long long)frame,(unsigned long long)draws,(unsigned long long)instances,(unsigned long long)c.draws[q],(unsigned long long)c.instances[q],
  instances?100.0*double(c.instances[q])/double(instances):0.0,pool_table.size(),(unsigned long long)t.stored,(unsigned long long)t.refreshed,(unsigned long long)t.conflicts,
  (unsigned long long)t.evicted,(unsigned long long)t.replaced,(unsigned long long)t.stale,(unsigned long long)t.overflow);
 char reasons[512];int n=0;
 for(size_t i=1;i<size_t(pool::Status::Count) && n>=0 && n<int(sizeof reasons);++i)
  n+=snprintf(reasons+n,sizeof reasons-n,"%s%s %llu/%llu",i>1?"; ":"",pool::kStatusName[i],(unsigned long long)c.draws[i],(unsigned long long)c.instances[i]);
 log("[hit material pool] frame %llu without record (draws/instances): %s",(unsigned long long)frame,reasons);
 log("[hit material pool] frame %llu G-buffer class A draws %u, STI pair %u, captured %u, refresh not due %u, no geometry key %u, table full %u; capture failures by bit (0..2 buffers, 3..6 views, 7..9 sampler, 10 parameters, 11/12 detail views/sampler or the fifth 2D view) %u/%u/%u %u/%u/%u/%u %u/%u/%u %u %u/%u",
  (unsigned long long)frame,o->draws,o->recipe,o->captured,o->throttled,o->no_key,o->overflow,o->failures[0],o->failures[1],o->failures[2],o->failures[3],o->failures[4],o->failures[5],o->failures[6],
  o->failures[7],o->failures[8],o->failures[9],o->failures[10],o->failures[11],o->failures[12]);
 uint64_t ranges=0,bytes=0,ready=0,pending=0,failed=0;
 pool_table.for_each([&](uint64_t,const auto& r){const auto& m=r.payload;if(m.mesh.basis_bytes){++ranges;bytes+=m.mesh.basis_bytes;}
  ready+=m.basis.state==pool::Basis::Ready;pending+=m.basis.state==pool::Basis::Pending;failed+=m.basis.state==pool::Basis::Failed;});
 const auto& b=pool_basis;
 log("[hit material pool basis] frame %llu UV/basis ranges %llu (%.1f/%.0f MB in the mesh pool) ready %llu pending %llu failed %llu; captures %llu: ok %llu position mismatch %llu invalid %llu lost %llu pool moved %llu, max error %.3f (1 = mesh cache tolerance); rows %u/%u slots, this frame instances with a pool row %llu (draws %llu), ready without slot %llu",
  (unsigned long long)frame,(unsigned long long)ranges,double(bytes)/1048576.0,double(pool::kBasisCapBytes)/1048576.0,(unsigned long long)ready,(unsigned long long)pending,(unsigned long long)failed,
  (unsigned long long)b.jobs,(unsigned long long)b.results[0],(unsigned long long)b.results[1],(unsigned long long)b.results[2],(unsigned long long)b.results[3],(unsigned long long)b.results[4],double(b.err_max),
  pool_slots.used(),nh_pool_rows,(unsigned long long)b.row_instances,(unsigned long long)b.rows,(unsigned long long)b.no_slot);
 // Records and ready UV/basis per pool pair (FS): which variant the qualified share comes from.
 char pairs[512];int k=0;
 for(const auto& r:native_vt::recipes){if(!pool::pool_recipe(r.vs,r.fs) || k<0 || k>=int(sizeof pairs))continue;uint64_t records=0,with_basis=0;
  pool_table.for_each([&](uint64_t,const auto& e){if(e.payload.recipe!=&r)return;++records;with_basis+=e.payload.basis.state==pool::Basis::Ready;});
  k+=snprintf(pairs+k,sizeof pairs-k,"%s%08llx/%08llx %llu/%llu",k?"; ":"",(unsigned long long)(r.vs>>32),(unsigned long long)(r.fs>>32),(unsigned long long)records,(unsigned long long)with_basis);}
 log("[hit material pool pairs] frame %llu records/ready UV-basis by pair (VS/FS): %s",(unsigned long long)frame,k>0?pairs:"none");
}
void pool_release_slot(PoolMaterial& m){if(m.basis.slot<nh_pool_rows)pool_slots.release(m.basis.slot);m.basis.slot=UINT32_MAX;}
// Results of recorded captures (under pool_mutex, once per frame from the build's first lookup).
// epoch: the mesh pool's replacement epoch now (the build holds its lock).
void pool_jobs_poll(uint64_t frame,uint64_t epoch){
 const uint64_t gpu=life_gpu_done();
 for(auto& j:pool_jobs){
  if(j.resolved)continue;
  const auto* st=static_cast<const volatile uint32_t*>(j.host.mapped);
  // Not done within 240 frames (never recorded, or its command buffer dropped): lost.
  const bool done=st[14]==kPoolDone,timeout=frame>=j.frame+240;
  if(!done && !timeout)continue;
  float err;const uint32_t bits=st[12];std::memcpy(&err,&bits,4);
  const auto result=pool::basis_result(done,st[15]==kPoolRan,j.epoch!=epoch,err,st[13]);
  ++pool_basis.results[size_t(result)];if(result==pool::BasisResult::Ok)pool_basis.err_max=std::max(pool_basis.err_max,err);
  if(auto* r=pool_table.find(j.geometry);r && r->payload.basis.gen==j.gen && r->payload.basis.state==pool::Basis::Pending){
   auto& m=r->payload;pool::basis_done(m.basis,result,frame);
   if(result==pool::BasisResult::Ok && m.basis.slot>=nh_pool_rows)m.basis.slot=pool_slots.acquire();
  }
  j.resolved=true;j.frame=frame; // destroyed once the GPU is past this frame
 }
 std::erase_if(pool_jobs,[&](PoolJob& j){if(!j.resolved || gpu<j.frame+2)return false;destroy(j.xfb);destroy(j.host);return true;});
}
// Counters cover one frame: the first lookup of a new frame logs the previous one (rt_stats,
// every 120 frames) and starts over.
void pool_rollover(uint64_t frame,uint64_t epoch){
 if(pool_counter_frame==frame)return;
 if(pool_counter_frame && pool_counter_frame%120==0 && cfg_hot(CfgHot::rt_stats))pool_log(pool_counter_frame);
 pool_table.end_frame();pool_counter_frame=frame;pool_basis.rows=pool_basis.row_instances=0;
 pool_jobs_poll(frame,epoch);
}
bool pool_current(PoolMaterial& m,uint64_t frame){
 if(m.checked!=frame){m.checked=frame;
  if(m.plain_layout()){m.current=omm::ordinary_sampler_current(m.sampler);for(const auto& v:m.plain)m.current=m.current&&current(v);return m.current;}
  m.current=current(m.table)&&current(m.normal)&&current(m.albedo)&&current(m.physical)&&omm::ordinary_sampler_current(m.sampler)&&
  (!m.detail_layout() || (current(m.detail_normal)&&current(m.detail_mask)&&omm::ordinary_sampler_current(m.detail_sampler)));}
 return m.current;
}
}
bool enabled(){return on;}
bool instanced_patch_vs(uint64_t vs){return instanced_on && instanced::frame_vs(vs);}
void device_created(DeviceData* device){std::lock_guard lock(mutex);if(dd)return;dd=device;on=cfg_hot(CfgHot::rt_hit_material)!=0 && cfg_int("rt_alpha",1)!=0 && cfg_int("rt_bvh_split",1)!=0;
 instanced_on=on && cfg_int("rt_hit_material_instanced",0)!=0;
 capability(static_cast<uint32_t*>(persist(instanced_capability_key,sizeof(uint32_t))),instanced_on);}
void device_destroyed(DeviceData* device){std::lock_guard lock(mutex);if(dd!=device)return;capability(static_cast<uint32_t*>(persist(instanced_capability_key,sizeof(uint32_t))),false);if(on)dd->vk.DeviceWaitIdle(dd->device);
 for(auto& [id,q]:queries)destroy_query(*q);queries.clear();
 for(auto& [id,p]:packets)destroy_packet(*p);packets.clear();
 for(auto& s:slots){destroy(s.data);s={};}
 for(auto pipeline:{gather_pipeline,join_pipeline,tileset_gather_pipeline,basis_pipeline})if(pipeline)dd->vk.DestroyPipeline(dd->device,pipeline,nullptr);
 for(auto layout:{gather_layout,join_layout,basis_layout})if(layout)dd->vk.DestroyPipelineLayout(dd->device,layout,nullptr);
 for(auto dsl:{gather_dsl,join_dsl,basis_dsl})if(dsl)dd->vk.DestroyDescriptorSetLayout(dd->device,dsl,nullptr);
 basis_pipeline={};basis_layout={};basis_dsl={};
 {std::lock_guard pool(pool_mutex);pool_table.clear();pool_slots.clear();for(auto& j:pool_jobs){destroy(j.xfb);destroy(j.host);}pool_jobs.clear();
  pool_basis={};pool_tilesets={};for(auto& o:pool_obs)o={};pool_counter_frame=0;}
 gather_pipeline={};join_pipeline={};tileset_gather_pipeline={};gather_layout={};join_layout={};gather_dsl={};join_dsl={};pending.clear();owners.clear();submitted_queues.clear();marked.clear();dd=nullptr;on=false;
}
void prepass_begin(VkCommandBuffer cmd){if(!on)return;auto* s=cmd_state(cmd);if(!one_time(s))return;std::lock_guard lock(mutex);collect();
 for(auto i=pending.begin();i!=pending.end();)if(current_recording(i->second.command)!=i->second.recording)i=pending.erase(i);else ++i;
 if(pending.size()>=128 && !pending.contains(s->recording))return;
 auto& p=pending[s->recording];p={};p.command=handle(cmd);p.recording=s->recording;p.join.begin(p.command,p.recording);
}
void note_prepass(VkCommandBuffer cmd,CmdState* state,const DrawArgs& args,uint32_t record,VkDeviceSize rawOffset,uint32_t stride,uint32_t triangles){
 if(!on || !state || args.kind>1 || record>=max_records)return;
 const bool tileset=tileset_depth(state->vs,state->fs);if(tileset && !tileset_active())return;uint32_t vertices=0;
 // rt_hit_material_instanced: an instanced wind / 2S depth draw keeps all its instances' rows.
 const auto family=!tileset && instanced_live()?instanced::depth_family(state->vs):instanced::None;
 if(!tileset_vertex_count(args.count,args.instances,vertices) ||
    !(family!=instanced::None?instanced::prepass_geometry_ok(vertices,args.instances,triangles,stride):prepass_geometry_ok(tileset,vertices,args.instances,triangles,stride)))return;
 AlphaSnapshot snapshot{};if(!alpha_snapshot(record,&snapshot))return;
 if(!prepass_alpha_ok(tileset,snapshot.rec.kind,snapshot.rec.channel,snapshot.rec.p0[1],snapshot.rec.p0[2]))return;
 Pre pre{key(cmd,*state,args),snapshot,rawOffset,stride,triangles,record,state->vs,state->fs};pre.family=family;std::lock_guard lock(mutex);auto p=pending.find(state->recording);if(p==pending.end())return;
 ++p->second.candidate_draws;
 if(p->second.join.note(handle(cmd),state->recording,pre.key,UINT32_MAX) && p->second.join.assign(handle(cmd),state->recording,pre.key,record)){
  p->second.pre.push_back(pre);if(family!=instanced::None)++p->second.instanced_prepass[family];}
}
uint32_t note_opaque_prepass(VkCommandBuffer cmd,CmdState* state,const DrawArgs& args,VkDeviceSize raw,uint32_t stride,uint32_t triangles){
 if(!on || !cfg_hot(CfgHot::rt_hit_material) || !one_time(state) || !opaque_depth_family(state->vs,state->fs) || !opaque_prepass_geometry_ok(args.kind,args.instances,args.count,triangles,stride))return UINT32_MAX;
 Pre pre{};pre.key=key(cmd,*state,args);pre.raw=raw;pre.stride=stride;pre.triangles=triangles;pre.vs=state->vs;pre.fs=state->fs;pre.opaque=true;
 std::lock_guard lock(mutex);auto p=pending.find(state->recording);if(p==pending.end())return UINT32_MAX;
 ++p->second.candidate_draws;
 if(!p->second.join.note(handle(cmd),state->recording,pre.key,UINT32_MAX))return UINT32_MAX;
 if(p->second.opaque_count>=nh_opaque_rows){++p->second.opaque_overflow;return UINT32_MAX;}
 if(!opaque_material_record(p->second.opaque_count,pre.record))return UINT32_MAX;++p->second.opaque_count;
 if(!p->second.join.assign(handle(cmd),state->recording,pre.key,pre.record))return UINT32_MAX;
 p->second.pre.push_back(pre);return pre.record;
}
void prepass_observe(VkCommandBuffer,CmdState*,const DrawArgs&){
 // Only actual note_prepass candidates reserve a material join identity.
}
void scene_prepare(VkCommandBuffer cmd,uint32_t index,const RtCapture& cap){
 if(!on || !cfg_hot(CfgHot::rt_hit_material) || index>=kSceneSlots)return;auto* state=cmd_state(cmd);if(!one_time(state))return;std::lock_guard lock(mutex);collect();
 auto& s=slots[index];s.set={};s.handles.clear();s.pre.clear();s.owners.clear();s.prepared=s.attached=false;s.capacity_logged=false;
 auto p=pending.find(state->recording);if(!cap.split || p==pending.end() || !p->second.join.current(handle(cmd),state->recording))return;
 s.command=handle(cmd);s.recording=state->recording;s.frame=frame_index();s.cursor=0;s.generation=0;
 s.camera=cap.ubo;s.camera_offset=cap.ubo_offset;s.join=p->second.join;if(!mark_owner(cmd,s))return;
 s.opaque_used=p->second.opaque_count;s.opaque_overflow=p->second.opaque_overflow;std::memcpy(s.instanced_prepass,p->second.instanced_prepass,sizeof s.instanced_prepass);
 // When deferred was recorded first, exact audited draw keys are already known.
 // Copy only their source records; unrelated VT records cannot consume the cap.
 auto deferred=packet_for(s);
 uint32_t filtered=0,duplicates=0,range_dropped=0;const auto source_draws=p->second.candidate_draws,source_entries=uint32_t(p->second.pre.size());
 // Plan first, then size the slot's stream to the actual need (the slot is retired here,
 // so its old buffer has no GPU user left), then copy.
 std::vector<const Pre*> accepted;VkDeviceSize need=0;
 for(const auto& pre:p->second.pre){
  uint32_t sourceRecord=0;if(!s.join.unique_source(pre.key,sourceRecord) || sourceRecord!=pre.record){++duplicates;continue;}
  if(deferred && std::none_of(deferred->materials.begin(),deferred->materials.end(),[&](const auto& m){return m.key==pre.key && deferred->join.unique(m.key,m.row.valid-1);})){++filtered;continue;}
  VkDeviceSize bytes=VkDeviceSize(pre.triangles)*3*pre.stride;
  if(!pre.stride || !byte_range(pre.raw,bytes,cap.xfb_size) || !byte_range(need,bytes,source_cap)){++range_dropped;continue;}
  accepted.push_back(&pre);need+=bytes;
 }
 s.source_overflow=range_dropped;
 const VkDeviceSize size=source_stream_bytes(need,stream_bytes,source_cap);
 if(s.data.b && (s.data_bytes<size || s.data_bytes>2*size)){destroy(s.data);s.data_bytes=0;}
 if(!size || !create(s.data,size,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT))return;
 s.data_bytes=size;
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
 for(const Pre* pre:accepted){
  VkDeviceSize bytes=VkDeviceSize(pre->triangles)*3*pre->stride;
  VkBufferCopy copy{pre->raw,s.cursor,bytes};dd->vk.CmdCopyBuffer(cmd,cap.xfb,s.data.b,1,&copy);auto saved=*pre;saved.raw=s.cursor;s.pre.push_back(saved);s.cursor+=bytes;
 }
 if(s.pre.empty() && deferred && !deferred->materials.empty() && cfg_hot(CfgHot::rt_stats) && s.frame%120==0){
  // Samples are explicitly NOT a claimed pair. They expose source shader and
  // raw argument/IB/VB domains if no exact key association can be constructed.
  auto sample=[&](const char* side,const DrawKey& key,uint64_t vs,uint64_t fs){const auto& w=key.words;
   log("[hit material key sample] frame %llu %s vs/fs %016llx/%016llx kind/count/instances/first/vertex/instance %llu/%llu/%llu/%llu/%llu/%llu IB/offset/type %llx/%llu/%llu schema 0x%llx; not a claimed pair",
    (unsigned long long)s.frame,side,(unsigned long long)vs,(unsigned long long)fs,(unsigned long long)w[0],(unsigned long long)w[1],(unsigned long long)w[2],(unsigned long long)w[3],(unsigned long long)w[4],(unsigned long long)w[5],
    (unsigned long long)w[6],(unsigned long long)w[7],(unsigned long long)w[8],(unsigned long long)(w[33]==0x4e485432u?w[33]:w[20]));
   if(w[33]==0x4e485432u)for(uint32_t location=0;location<6;++location)log("[hit material tileset key input] %s location %u buffer/offset/stride/format %llx/%llu/%llu/%llu",side,location,
    (unsigned long long)w[9+4*location],(unsigned long long)w[10+4*location],(unsigned long long)w[11+4*location],(unsigned long long)w[12+4*location]);
   if(w[20]==0x4e484b31u)log("[hit material key inputs] %s engine %llx+%llu pull %llu index hint %llu position buffer/offset/stride/format %llx/%llu/%llu/%llu UV buffer/offset/stride/format %llx/%llu/%llu/%llu",
    side,(unsigned long long)w[9],(unsigned long long)w[10],(unsigned long long)w[11],(unsigned long long)w[21],(unsigned long long)w[12],(unsigned long long)w[13],(unsigned long long)w[14],(unsigned long long)w[15],
    (unsigned long long)w[16],(unsigned long long)w[17],(unsigned long long)w[18],(unsigned long long)w[19]);
  };
  if(!p->second.pre.empty()){
   auto source=std::find_if(p->second.pre.begin(),p->second.pre.end(),[](const auto& pre){return static_depth_family(pre.vs,pre.fs);});
   if(source==p->second.pre.end())source=p->second.pre.begin();sample("source",source->key,source->vs,source->fs);
  }
  const auto& first=deferred->materials.front();sample("deferred",first.key,first.row.reserved==2?tileset_deferred_vs:0x3812bc980438747full,first.row.reserved==2?tileset_deferred_fs:0x8995da1e4060f53bull);
 }
 s.prepared=true;pending.erase(p);
 barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
 if(cfg_hot(CfgHot::rt_stats) && s.frame%120==0)log("[hit material source] frame %llu cmd %p recording %llu level %u flags 0x%x prepared %zu copied %llu bytes source draws/entries %u/%u duplicate keys %u range dropped %u filtered unrelated %u; deferred packet %u camera %p+%llu",
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
 if(!on || !cfg_hot(CfgHot::rt_hit_material) || !one_time(state))return false;
 if(tileset_deferred(state->vs,state->fs) && !tileset_active())return false;Bg3Binding camera{};if(!query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,2,0,&camera) || !camera.buffer)return false;
 auto draw=key(cmd,*state,args);std::lock_guard lock(mutex);collect();
 auto it=packets.find(state->recording);
 if(it==packets.end()){
  if(packets.size()>=kSceneSlots)return false;
  auto p=std::make_shared<Deferred>();p->command=handle(cmd);p->recording=state->recording;p->frame=frame_index();p->camera=camera.buffer;p->camera_offset=camera.offset;p->join.begin(p->command,p->recording);
  // XFB chunks are allocated on demand by the draws that need them.
  if(!create(p->rows,packet_rows,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT)){destroy_packet(*p);return false;}
  it=packets.emplace(state->recording,std::move(p)).first;
 }
 auto& p=*it->second;
 if(p.finalized || p.frame!=frame_index() || p.camera!=camera.buffer || p.camera_offset!=camera.offset){p.valid=false;return false;}
 const bool tileset=tileset_deferred(state->vs,state->fs);
 // rt_hit_material_instanced: the instanced wind / 2S pairs take the per-draw path, any instance count.
 const auto family=!tileset && instanced_live()?instanced::deferred_family(state->vs,state->fs):instanced::None;
 const auto* irecipe=family!=instanced::None?instanced::recipe(state->vs,state->fs):nullptr;
 const bool audited=audited_static_family(state->vs,state->fs)||tileset||irecipe;
 const auto qualify=irecipe?instanced::deferred_qualify(exactPipeline,args.kind,args.instances,stride):deferred_qualify(audited,tileset,exactPipeline,args.kind,args.instances,stride);
 if(qualify==DeferredQualify::FamilyDeclined){++p.family_declined;
  if(pool::uncaptured_recipe(state->vs,state->fs)){++p.uncaptured_draws;p.uncaptured_instanced+=args.instances>1;p.uncaptured_vertices+=uint64_t(args.count)*args.instances;}
  if(cfg_hot(CfgHot::rt_stats) && p.frame%120==0)p.declined.add(state->vs,state->fs,args.instances,uint64_t(args.count)*args.instances);return false;}++p.seen;
 auto* counts=irecipe?&p.instanced[family]:nullptr;if(counts)++counts->seen;
 if(qualify==DeferredQualify::PipelineDeclined){++p.pipeline_declined;if(counts)++counts->pipeline;return false;}
 bool unique=p.join.note(p.command,p.recording,draw,UINT32_MAX);
 if(!unique){++p.audited_duplicate;return false;}++p.audited_unique;
 // Count real audited duplicates even beyond the storage bound. Unrelated
 // shader families never reserve an audited candidate's geometry identity.
 if(p.materials.size()>=material_slots){++p.slot_overflow;return false;}
 Material material{};if(!(tileset?capture_tileset(cmd,material):capture(cmd,material,irecipe))){++p.binding_declined;if(counts)++counts->bindings;return false;}
 // Instanced pairs: the record-time engine (STI layout) and 1:1 bytes staged in the gather's layout.
 unsigned char stage[instanced::kStageBytes];
 if(irecipe){uint32_t engine[16]{},ubo[8]{};const uint32_t eb=pool::engine_bytes(*irecipe),mb=native_vt::material_bytes(*irecipe);
  if(eb>sizeof engine || mb>sizeof ubo || !read_mapped(material.engine.buffer,material.engine.offset,engine,eb) || !read_mapped(material.physical_ubo.buffer,material.physical_ubo.offset,ubo,mb) ||
     !instanced::stage(*irecipe,engine,eb/4u,ubo,mb/4u,stage)){++p.binding_declined;++counts->bindings;return false;}
  material.family=family;material.staged=true;
  const uint32_t flags=instanced::row_flags(*irecipe);std::memcpy(&material.row.parameters[3],&flags,4); // row word 220
 }
 uint32_t vertices=0;if(!tileset_vertex_count(args.count,args.instances,vertices))return false;
 *bytes=VkDeviceSize(vertices)*stride;ChunkPlacement place{};
 if(!place_in_chunks(uint32_t(p.chunks.size()),p.cursor,*bytes,stream_bytes,deferred_chunks,place)){++p.stream_overflow;if(counts)++counts->stream;return false;}
 if(irecipe && !create(p.staged,VkDeviceSize(material_slots)*instanced::kStageBytes,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,true))return false;
 if(!own(cmd))return false;
 auto identity=[](const View& v){return CacheIdentity{uint64_t(v.view),v.id,uint64_t(v.image),v.image_id,uint64_t(v.layout)};};
 // Plan without publishing; commit only after the chunk and the sampler clone exist.
 ResourcePlan plan{};
 if(!(tileset?p.resources.plan_sampler(material.sampler.sampler.id,plan):p.resources.plan(material.sampler.sampler.id,{identity(material.normal),identity(material.albedo),identity(material.physical)},plan))){
  if(plan.failure==ResourceFailure::Samplers)++p.sampler_overflow;else if(plan.failure==ResourceFailure::Caches)++p.cache_overflow;else if(plan.failure==ResourceFailure::Slots)++p.slot_overflow;
  return false;
 }
 if(place.new_chunk){Buffer chunk{};if(!create(chunk,stream_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT))return false;p.chunks.push_back(chunk);p.cursor=0;}
 if(plan.indices.new_sampler){
  auto sampler_info=material.sampler.sampler.info;sampler_info.pNext=nullptr;VkSampler sampler{};
  if(dd->vk.CreateSampler(dd->device,&sampler_info,nullptr,&sampler)!=VK_SUCCESS)return false;
  p.samplers.push_back(sampler);
 }
 p.resources.commit(plan);const auto& indices=plan.indices;uint32_t local=uint32_t(p.materials.size());
 material.key=draw;material.chunk=place.chunk;material.row.valid=local+1;material.row.triangles=vertices/3;material.row.deferred=uint32_t(place.offset);material.row.stride=stride;material.row.frame=uint32_t(p.frame);
 material.row.sampler=indices.sampler;material.row.normal=indices.normal;material.row.albedo=indices.albedo;material.row.physical=indices.physical;
 if(material.staged){std::memcpy(static_cast<unsigned char*>(p.staged.mapped)+VkDeviceSize(local)*instanced::kStageBytes,stage,sizeof stage);++counts->captured;}
 p.join.assign(p.command,p.recording,draw,local);p.materials.push_back(material);*buffer=p.chunks[place.chunk].b;*offset=place.offset;p.cursor=place.offset+*bytes;p.total+=*bytes;return true;
}
void pool_jobs_record(VkCommandBuffer,CmdState*);
void deferred_finish(VkCommandBuffer cmd,bool readonly){
 if(!on)return;auto* state=cmd_state(cmd);if(!one_time(state))return;pool_jobs_record(cmd,state);std::lock_guard lock(mutex);auto i=packets.find(state->recording);if(i==packets.end())return;auto& p=*i->second;
 if(p.finalized){p.valid=false;return;}p.finalized=true;p.valid=p.valid && readonly && cfg_hot(CfgHot::rt_hit_material)!=0 && compute_ready(false);
 if(!own(cmd)){p.valid=false;return;}
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
 dd->vk.CmdFillBuffer(cmd,p.rows.b,0,packet_rows,0);
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
 uint32_t captured=0;
 if(p.valid)for(auto& m:p.materials){uint32_t local=0;
  if(!p.join.claim(p.command,p.recording,m.key,local) || local!=m.row.valid-1 || !material_current(m) || !omm::ordinary_sampler_current(m.sampler))continue;
  VkDescriptorBufferInfo info[4]={{m.engine.buffer,m.engine.offset,m.engine.range},{m.physical_ubo.buffer,m.physical_ubo.offset,m.physical_ubo.range},{m.tilesets.buffer,m.tilesets.offset,m.tilesets.range},{p.rows.b,0,packet_rows}};
  VkWriteDescriptorSet writes[4]{};for(uint32_t n=0;n<4;++n){writes[n]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[n].dstBinding=n;writes[n].descriptorCount=1;writes[n].descriptorType=n<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[n].pBufferInfo=info+n;}
  const bool tileset=m.row.reserved==2;if(tileset && !compute_ready(false,true))continue;
  if(tileset){info[2]={p.rows.b,0,packet_rows};writes[2].pBufferInfo=info+2;}
  // Reflectance 0.5 (FS 3da860be): no 1:1 UBO; binding 1 gets the engine UBO only to stay valid
  // and hit_material_gather.hlsl stores the constant (push word 3 = 1).
  const bool constant=m.reflectance==native_vt::ReflectanceConstant;if(constant)info[1]={m.engine.buffer,m.engine.offset,16};
  // Instanced pairs (native_hit_instanced.h stage): the gather layout's engine and 1:1 bytes, staged.
  if(m.staged){const VkDeviceSize at=VkDeviceSize(local)*instanced::kStageBytes;info[0]={p.staged.b,at,112};info[1]={p.staged.b,at+instanced::kStagePhysical,16};}
  uint32_t push[4]={16u+local*record_bytes,local,1,constant?1u:0u};
  dd->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,tileset?tileset_gather_pipeline:gather_pipeline);dd->vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,gather_layout,0,4,writes);
  dd->vk.CmdPushConstants(cmd,gather_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,push);dd->vk.CmdDispatch(cmd,1,1,1);++captured;
 }
 barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
 if(p.valid)restore_bindings(cmd,VK_PIPELINE_BIND_POINT_COMPUTE);
 if(cfg_hot(CfgHot::rt_stats) && p.frame%120==0)log("[hit material deferred packet] frame %llu cmd %p recording %llu level %u flags 0x%x static seen %u audited unique/duplicate %u/%u other family %u pipeline declined %u binding declined %u captured %zu snapshots %u unique samplers/cache views %u/%u bytes %llu valid %u readonly %u camera %p+%llu; source preparation not required",
  (unsigned long long)p.frame,(void*)cmd,(unsigned long long)p.recording,unsigned(state->level),unsigned(state->begin_flags),p.seen,p.audited_unique,p.audited_duplicate,p.family_declined,p.pipeline_declined,p.binding_declined,p.materials.size(),captured,p.resources.sampler_count,p.resources.cache_count,(unsigned long long)p.total,unsigned(p.valid),unsigned(readonly),(void*)p.camera,(unsigned long long)p.camera_offset);
 if(cfg_hot(CfgHot::rt_stats) && p.frame%120==0 && p.declined.draws){
  // Coverage census of the declined families: instanced (Base_VT_STI_DEF) statics,
  // tileblend/terrain, skinned characters and decals each appear here as their own pair.
  log("[hit material families] frame %llu declined draws %u instanced %u VT STI (FS 14678345) %u pairs %zu (+%u unlisted); top by vertices:",
   (unsigned long long)p.frame,p.declined.draws,p.declined.instanced,p.declined.sti,p.declined.entries.size(),p.declined.other);
  for(const auto& e:p.declined.top(8))log("[hit material families]   vs/fs %016llx/%016llx draws %u instanced %u vertices %llu",
   (unsigned long long)e.vs,(unsigned long long)e.fs,e.draws,e.instanced,(unsigned long long)e.vertices);
 }
 // Audited FS decodes whose VS has no geometry path (native_vt::CaptureNone: not class A, never pooled).
 if(cfg_hot(CfgHot::rt_stats) && p.frame%120==0)log("[hit material families] frame %llu audited recipe, no capture path (VS not class A): draws %u instanced %u vertices %llu",
  (unsigned long long)p.frame,p.uncaptured_draws,p.uncaptured_instanced,(unsigned long long)p.uncaptured_vertices);
}
void slot_in_use(uint32_t index,uint64_t frame){if(!on || index>=kSceneSlots)return;std::lock_guard lock(mutex);for(auto h:slots[index].handles)life_extend(h,frame);}
bool pool_keys(const InstPipe& inst,const CmdState& s,const DrawArgs& args,uint64_t* geometry,uint64_t* position){
 *geometry=*position=0;if(!inst.ok || args.kind>1)return false;
 pool::Geometry g;g.indexed=args.kind==1;g.count=args.count;g.first=args.first;g.vertex_offset=g.indexed?args.vertex_offset:0;
 if(g.indexed){if(!s.ib)return false;g.ib=uint64_t(s.ib);g.ib_offset=s.ib_offset;g.ib_type=uint32_t(s.ib_type);}
 if(inst.pos_binding<CmdState::kMaxVertexBindings){const auto& vb=s.vb[inst.pos_binding];
  if(vb.buffer && vb.offset<=UINT64_MAX-inst.pos_offset){g.position_buffer=uint64_t(vb.buffer);g.position_offset=vb.offset+inst.pos_offset;}}
 if(!g.indexed && !g.position_buffer)return false;
 *geometry=pool::geometry_key(g);*position=pool::position_key(g);return true;
}
// A UV/basis capture is due for this draw's record (under pool_mutex; cheap, every STI draw).
PoolMaterial* pool_job_due(uint64_t geometry,uint64_t frame,const DrawArgs& args,uint32_t stride){
 auto* r=pool_table.find(geometry);if(!r || r->seen.status!=pool::Status::Qualified || r->conflict || r->prev_conflict)return nullptr;
 auto& m=r->payload;
 if(!m.mesh.basis_bytes || m.basis.gen!=m.mesh.gen || !pool::basis_due(m.basis,frame) || m.mesh.basis_bytes!=pool::basis_bytes(m.mesh.tris) ||
    args.count!=m.mesh.tris*3u || !args.instances || stride<64u || VkDeviceSize(m.mesh.tris)*3u*stride>pool::kJobXfbBytes)return nullptr;
 if(pool_basis.frame!=frame){pool_basis.frame=frame;pool_basis.frame_jobs=0;}
 return pool_basis.frame_jobs<pool::kJobsPerFrame?&m:nullptr;
}
// One capture (true: binds `xfb` for this draw). Its inputs are read without pool_mutex (the
// buffer-size and mapping lookups take other layer locks), then the record is checked again.
// Instance 0's matrix rows come from the game's mapped instance stream at record; a stale or
// different matrix fails the GPU position check (fail closed, retried later).
bool pool_job(VkCommandBuffer cmd,CmdState* s,const DrawArgs& args,const InstPipe& inst,uint64_t geometry,uint64_t frame,uint32_t stride,
              VkBuffer* xfb,VkDeviceSize* offset,VkDeviceSize* bytes){
 if(!one_time(s))return false;
 Bg3Binding camera{};if(!query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,2,0,&camera) || !camera.buffer)return false;
 if(camera.range==VK_WHOLE_SIZE){auto size=geo_buffer_size(camera.buffer);camera.range=camera.offset<size?size-camera.offset:0;}
 if(camera.range<224 || camera.offset%geo_ubo_align())return false; // 14 float4: _m2 rows at 128 (xfb_to_world.hlsl)
 float rows[12];
 for(uint32_t k=0;k<3;++k){
  if(inst.row_binding[k]>=CmdState::kMaxVertexBindings)return false;const auto& vb=s->vb[inst.row_binding[k]];
  const uint32_t st=inst.row_stride[k]?inst.row_stride[k]:uint32_t(vb.stride);const VkDeviceSize size=vb.buffer?geo_buffer_size(vb.buffer):0;
  const VkDeviceSize first=vb.offset+VkDeviceSize(args.first_instance)*st+inst.row_offset[k];
  const uint8_t* p=vb.buffer && st && first+16<=size?mapped_ptr(vb.buffer,first):nullptr;if(!p)return false;std::memcpy(rows+4*k,p,16);
 }
 PoolJob j{};j.geometry=geometry;j.recording=s->recording;j.frame=frame;j.xfb_stride=stride;j.camera=camera;
 j.tris=args.count/3u;j.xfb_bytes=VkDeviceSize(j.tris)*3u*stride;
 if(!create(j.xfb,j.xfb_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT) || !create(j.host,64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true)){destroy(j.xfb);destroy(j.host);return false;}
 std::memset(j.host.mapped,0,64);std::memcpy(j.host.mapped,rows,48);
 std::lock_guard lock(pool_mutex);auto* m=pool_job_due(geometry,frame,args,stride);
 if(!m || m->mesh.tris!=j.tris){destroy(j.xfb);destroy(j.host);return false;}
 j.gen=m->mesh.gen;j.pool_stride=m->mesh.stride;j.pool_off=m->mesh.pool_off;j.basis_off=m->mesh.basis_off;
 *xfb=j.xfb.b;*offset=0;*bytes=j.xfb_bytes;pool_jobs.push_back(j);
 pool::basis_pending(m->basis,frame);++pool_basis.frame_jobs;++pool_basis.jobs;return true;
}
bool pool_observe(VkCommandBuffer cmd,CmdState* state,const DrawArgs& args,const InstPipe& inst,bool poolPipeline,bool basisPipeline,
                  uint32_t stride,VkBuffer* xfb,VkDeviceSize* offset,VkDeviceSize* bytes){
 if(!on || !state || !cfg_hot(CfgHot::rt_hit_material))return false;const auto frame=frame_index();
 uint64_t geometry=0,position=0;const bool keyed=pool_keys(inst,*state,args,&geometry,&position);
 const auto* recipe=pool::pool_recipe(state->vs,state->fs);
 pool::Observation o{pool::mix(state->vs,state->fs),position,pool::Status::Family};PoolMaterial payload{};uint32_t failures=0;
 bool due=false,throttled=false;
 if(keyed && recipe && poolPipeline){
  Bg3Binding tilesets{};const bool table=query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,12,0,&tilesets) && tilesets.buffer;
  if(table && tilesets.range==VK_WHOLE_SIZE){auto size=geo_buffer_size(tilesets.buffer);tilesets.range=tilesets.offset<size?size-tilesets.offset:0;}
  // A current qualified record is not captured again before its refresh frame.
  std::lock_guard lock(pool_mutex);if(table)pool_tilesets={tilesets,frame};
  if(!pool_table.capture_due(geometry,frame)){auto& seen=pool_observed(frame);++seen.draws;++seen.recipe;++seen.throttled;
   due=basisPipeline && pool_job_due(geometry,frame,args,stride);throttled=true;}
 }
 if(throttled)return due && pool_job(cmd,state,args,inst,geometry,frame,stride,xfb,offset,bytes);
 if(keyed && recipe){
  if(!poolPipeline)o.status=pool::Status::Pipeline;
  else{
   // Same bindings as the audited pair (native_vt_surface.h). capture() keeps its own
   // statics, so it runs under the main mutex, never under pool_mutex.
   Material m{};bool ok;{std::lock_guard lock(mutex);ok=capture(cmd,m,recipe,&failures);}
   const uint32_t engine=pool::engine_bytes(*recipe);
   const bool constant=recipe->reflectance==native_vt::ReflectanceConstant;const float half=0.5f;
   if(ok && constant)std::memcpy(&payload.reflectance,&half,4); // FS 3df678c0: RT2.y is 0.5, no 1:1 UBO
   // The 1:1 bytes the recipe reads: the reflectance at its byte; LayoutDetail also its
   // strength/tiling (bytes 0/4), mask power and roughness add (12/16).
   uint32_t ubo[8]{};const uint32_t ubo_bytes=native_vt::material_bytes(*recipe);
   if(ok && !(read_mapped(m.engine.buffer,m.engine.offset,payload.engine,engine) && (constant || read_mapped(m.physical_ubo.buffer,m.physical_ubo.offset,ubo,ubo_bytes)))){ok=false;failures|=1u<<10;}
   if(ok && !constant)payload.reflectance=ubo[recipe->reflectance_offset/4u];
   payload.recipe=recipe;
   if(ok && payload.detail_layout()){payload.detail_normal=m.detail_normal;payload.detail_mask=m.detail_mask;payload.detail_sampler=m.detail_sampler;
    payload.detail[0]=ubo[0];payload.detail[1]=ubo[1];payload.detail[2]=ubo[3];payload.detail[3]=ubo[4];}
   // LayoutIridescent: its five 2D views and the 1:1 floats _m0.._m5 (native_vt_surface.h).
   if(ok && payload.plain_layout()){payload.plain=m.plain;std::memcpy(payload.plain_floats,ubo,24);}
   o.status=ok?pool::Status::Qualified:pool::Status::Binding;
   if(ok){
    payload.table=m.table;payload.normal=m.normal;payload.albedo=m.albedo;payload.physical=m.physical;payload.sampler=m.sampler;
    uint64_t h=pool::mix(o.identity,m.sampler.sampler.id);
    for(const View* v:{&m.table,&m.normal,&m.albedo,&m.physical})h=pool::mix(pool::mix(h,v->id),v->image_id);
    for(uint32_t w=0;w<engine/4u;++w)h=pool::mix(h,payload.engine[w]);
    if(payload.detail_layout()){h=pool::mix(h,m.detail_sampler.sampler.id);
     for(const View* v:{&m.detail_normal,&m.detail_mask})h=pool::mix(pool::mix(h,v->id),v->image_id);
     for(uint32_t w:payload.detail)h=pool::mix(h,w);}
    if(payload.plain_layout()){for(const auto& v:m.plain)h=pool::mix(pool::mix(h,v.id),v.image_id);for(uint32_t w:payload.plain_floats)h=pool::mix(h,w);}
    o.identity=pool::mix(h,payload.reflectance);
   }
  }
 }
 {std::lock_guard lock(pool_mutex);auto& seen=pool_observed(frame);++seen.draws;
  if(!keyed){++seen.no_key;return false;}
  if(recipe){++seen.recipe;pool_capture_bits(failures,seen.failures);seen.captured+=o.status==pool::Status::Qualified;}
  const auto stored=pool_table.observe(geometry,frame,o);
  if(stored==pool::Table<PoolMaterial>::Store::Full){++seen.overflow;return false;}
  if(stored==pool::Table<PoolMaterial>::Store::Fresh)assign_material(pool_table.find(geometry)->payload,payload);
  due=recipe && basisPipeline && pool_job_due(geometry,frame,args,stride);
 }
 return due && pool_job(cmd,state,args,inst,geometry,frame,stride,xfb,offset,bytes);
}
PoolUse pool_lookup(uint64_t geometry,uint64_t position,uint32_t instances,const PoolMesh& mesh){
 PoolUse use{uint32_t(pool::Status::NoDeferred),UINT32_MAX,false};
 if(!on || !cfg_hot(CfgHot::rt_hit_material))return use;const auto frame=frame_index();
 std::lock_guard lock(pool_mutex);pool_rollover(frame,mesh.epoch);
 const auto status=pool_table.lookup(geometry,mesh.gen,position,frame,instances,[&](PoolMaterial& m){return pool_current(m,frame);});
 use.status=uint32_t(status);
 auto* r=pool_table.find(geometry);if(!r)return use;
 auto& m=r->payload;
 if(const uint32_t old=pool::basis_rebind(m.basis,mesh.gen);old<nh_pool_rows)pool_slots.release(old);
 m.mesh=mesh;
 if(status!=pool::Status::Qualified)return use;
 use.wants_basis=!mesh.basis_bytes;
 if(m.basis.state==pool::Basis::Ready){
  if(m.basis.slot>=nh_pool_rows)m.basis.slot=pool_slots.acquire();
  if(m.basis.slot<nh_pool_rows){use.slot=m.basis.slot;++pool_basis.rows;pool_basis.row_instances+=instances;}else ++pool_basis.no_slot;
 }
 return use;
}
void pool_evicted(uint64_t geometry){if(!on || !geometry)return;std::lock_guard lock(pool_mutex);pool_table.evicted(geometry,[](PoolMaterial& m){pool_release_slot(m);});}
// The captures recorded in this G-buffer command buffer (after its render pass): mode 0 maps the
// XFB to object space and checks the positions, mode 1 writes the basis header only when that
// check passed (fail closed: the shader also checks it). Takes no hit-material lock while asking
// the BVH split for its pool (its lock is taken before pool_mutex in the build).
void pool_jobs_record(VkCommandBuffer cmd,CmdState* state){
 {std::lock_guard pool(pool_mutex);if(std::none_of(pool_jobs.begin(),pool_jobs.end(),[&](const PoolJob& j){return !j.recorded && j.recording==state->recording;}))return;}
 VkBuffer pool_buffer{};VkDeviceSize pool_bytes=0;uint64_t epoch=0;const bool have_pool=bvh_pool_target(&pool_buffer,&pool_bytes,&epoch);
 bool ready;{std::lock_guard lock(mutex);ready=basis_ready();}
 std::vector<PoolJob> work;
 {std::lock_guard pool(pool_mutex);
  for(auto& j:pool_jobs)if(!j.recorded && j.recording==state->recording){
   j.recorded=true;j.epoch=epoch;
   const bool fits=have_pool && byte_range(j.pool_off,VkDeviceSize(j.tris)*3u*j.pool_stride,pool_bytes) && byte_range(j.basis_off,pool::basis_bytes(j.tris),pool_bytes);
   if(ready && fits)work.push_back(j);
   else static_cast<uint32_t*>(j.host.mapped)[14]=kPoolDone; // never runs: lost at the next poll (no ran marker)
  }
 }
 if(work.empty())return;
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
 dd->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,basis_pipeline);
 const float tol_abs=cfg_float("rt_mesh_tol_abs",2e-3f),tol_rel=cfg_float("rt_mesh_tol_rel",4e-5f);
 for(uint32_t mode=0;mode<2;++mode){
  for(const auto& j:work){
   VkDescriptorBufferInfo info[4]={{j.camera.buffer,j.camera.offset,224},{j.xfb.b,0,j.xfb_bytes},{pool_buffer,0,pool_bytes},{j.host.b,0,64}};
   VkWriteDescriptorSet writes[4]{};for(uint32_t n=0;n<4;++n){writes[n]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[n].dstBinding=n;writes[n].descriptorCount=1;writes[n].descriptorType=n?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;writes[n].pBufferInfo=info+n;}
   const uint32_t groups=mode?1u:std::min((j.tris+255u)/256u,1024u);
   uint32_t push[12]={j.tris,groups*256u,j.xfb_stride,j.pool_stride,uint32_t(j.pool_off),uint32_t(j.basis_off),uint32_t(j.gen),mode,0,0,0,0};
   std::memcpy(push+8,&tol_abs,4);std::memcpy(push+9,&tol_rel,4);
   dd->vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,basis_layout,0,4,writes);
   dd->vk.CmdPushConstants(cmd,basis_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,48,push);dd->vk.CmdDispatch(cmd,groups,1,1);
  }
  barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_HOST_READ_BIT);
 }
 restore_bindings(cmd,VK_PIPELINE_BIND_POINT_COMPUTE);
}
bool slot_reusable(uint32_t index,uint64_t gpuDone){if(!on || index>=kSceneSlots)return true;std::lock_guard lock(mutex);
 for(auto id:slots[index].owners){auto i=owners.find(id);if(i==owners.end() || !i->second.retired(current_recording(i->second.command),gpuDone))return false;}return true;
}
bool register_use(VkCommandBuffer cmd,uint32_t index,VkDescriptorSet* query_alpha){
 if(query_alpha)*query_alpha={};
 if(!on || !query_alpha || !cfg_hot(CfgHot::rt_hit_material) || index>=kSceneSlots)return false;
 auto* state=cmd_state(cmd);if(!one_time(state)){
  if(cfg_hot(CfgHot::rt_stats) && frame_index()%120==0)log("[hit material query reject] recording domain frame %llu level %u flags 0x%x",
    (unsigned long long)frame_index(),state?unsigned(state->level):UINT32_MAX,state?unsigned(state->begin_flags):0u);return false;
 }
 std::lock_guard lock(mutex);collect();auto& slot=slots[index];
 if(!slot.prepared || !slot.attached || slot.frame!=frame_index()){
  if(cfg_hot(CfgHot::rt_stats) && frame_index()%120==0)log("[hit material query reject] source epoch current %llu source %llu prepared %u attached %u",
   (unsigned long long)frame_index(),(unsigned long long)slot.frame,unsigned(slot.prepared),unsigned(slot.attached));return false;
 }
 Bg3Binding camera{};if(!query_binding(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,0,2,0,&camera) || camera.buffer!=slot.camera || camera.offset!=slot.camera_offset){
  if(cfg_hot(CfgHot::rt_stats) && slot.frame%120==0)log("[hit material query reject] camera frame %llu source %p+%llu query %p+%llu",
   (unsigned long long)slot.frame,(void*)slot.camera,(unsigned long long)slot.camera_offset,(void*)camera.buffer,(unsigned long long)camera.offset);return false;
 }
 auto existing=queries.find(state->recording);
 if(existing!=queries.end()){
  auto& q=*existing->second;if(q.slot!=index || q.generation!=slot.generation || q.command!=handle(cmd))return false;
  *query_alpha=q.set;return mark_owner(cmd,slot);
 }
 auto packet=packet_for(slot);if(!packet || queries.size()>=2u*kSceneSlots || !compute_ready(true)){
  static uint64_t logged=UINT64_MAX;if(cfg_hot(CfgHot::rt_stats) && slot.frame%120==0 && logged!=slot.frame){logged=slot.frame;log("[hit material query] unavailable frame %llu scene %u packet %u pending packets %zu queries %zu source camera %p+%llu",
   (unsigned long long)slot.frame,slot.generation,unsigned(bool(packet)),packets.size(),queries.size(),(void*)slot.camera,(unsigned long long)slot.camera_offset);}return false;
 }
 auto query=std::make_unique<Query>();auto& q=*query;q.command=handle(cmd);q.recording=state->recording;q.source=slot.recording;q.slot=index;q.generation=slot.generation;q.deferred=packet;
 // Candidate identity narrows the pair; exact GPU clip+UV comparison remains
 // mandatory. Neither CPU recording order nor a matching handle certifies it.
 VkDeviceSize cursor=0;uint32_t query_overflow=0,table_overflow=0,tables_used=0;
 const uint32_t table_free=alpha_hit_table_free(slot.set);
 // Rows used vs capacity and every overflow, one line per 120-frame window (live check).
 auto log_capacity=[&](const char* outcome){
  if(!cfg_hot(CfgHot::rt_stats) || slot.frame%120!=0 || slot.capacity_logged)return;slot.capacity_logged=true;const double mb=1.0/1048576.0;
  log("[hit material capacity] frame %llu %s; opaque rows %u/%u overflow %u; deferred rows %zu/%u overflow %u; samplers %u/%u overflow %u; cache views %u/%u overflow %u; "
      "source stream %.1f/%.0f MB overflow %u; deferred stream %.1f MB in %zu/%u chunks overflow %u; query mapped %zu basis %.1f/%.0f MB overflow %u; tables %u/%u overflow %u",
   (unsigned long long)slot.frame,outcome,slot.opaque_used,nh_opaque_rows,slot.opaque_overflow,packet->materials.size(),material_slots,packet->slot_overflow,
   packet->resources.sampler_count,sampler_slots,packet->sampler_overflow,packet->resources.cache_count,cache_slots,packet->cache_overflow,
   double(slot.cursor)*mb,double(source_cap)*mb,slot.source_overflow,double(packet->total)*mb,packet->chunks.size(),deferred_chunks,packet->stream_overflow,
   q.materials.size(),double(cursor)*mb,double(query_cap)*mb,query_overflow,tables_used,table_free,table_overflow);
 };
 // rt_hit_material_instanced (native_hit_instanced.h): per family, the prepass alpha candidates, the
 // G-buffer draws (declined by pipeline / bindings / stream, captured) and the rows this query maps.
 auto log_instanced=[&](){
  if(!instanced_on || !cfg_hot(CfgHot::rt_stats) || slot.frame%120!=0)return;
  InstancedCount c[3]{};for(uint32_t f=1;f<3;++f){c[f]=packet->instanced[f];c[f].prepass=slot.instanced_prepass[f];}
  for(const auto& m:q.materials)if(m.family>0 && m.family<3){++c[m.family].mapped;c[m.family].triangles+=m.row.triangles;}
  log("[hit material instanced] frame %llu key %u; wind: prepass %u, G-buffer %u (pipeline %u, bindings %u, stream %u) captured %u, mapped %u rows %llu triangles; "
      "alpha-test 2S: prepass %u, G-buffer %u (pipeline %u, bindings %u, stream %u) captured %u, mapped %u rows %llu triangles",
   (unsigned long long)slot.frame,unsigned(instanced_live()),c[1].prepass,c[1].seen,c[1].pipeline,c[1].bindings,c[1].stream,c[1].captured,c[1].mapped,(unsigned long long)c[1].triangles,
   c[2].prepass,c[2].seen,c[2].pipeline,c[2].bindings,c[2].stream,c[2].captured,c[2].mapped,(unsigned long long)c[2].triangles);
 };
 for(const auto& deferred:packet->materials){
  uint32_t record=0;if(!join_packets(slot.join,packet->join,deferred.key,deferred.row.valid-1,record))continue;
  auto source=std::find_if(slot.pre.begin(),slot.pre.end(),[&](const auto& pre){return pre.record==record;});
  if(source==slot.pre.end() || source->stride!=deferred.row.stride || source->triangles!=deferred.row.triangles ||
     (deferred.row.reserved==2 ? (source->alpha.rec.kind!=5 || source->alpha.rec.p0[2]!=0.f || source->alpha.rec.p0[1]!=0.f || source->alpha.tex!=deferred.tileset_views[3].view || source->alpha.tex_id!=deferred.tileset_views[3].image_id) :
      (!source->opaque && (source->alpha.tex!=deferred.table.view || source->alpha.cache!=deferred.albedo.view))) || !material_current(deferred))continue;
  VkDeviceSize bytes=VkDeviceSize(source->triangles)*3*source->stride;
  if(!byte_range(cursor,bytes*2,query_cap)){++query_overflow;continue;}
  auto m=deferred;m.snapshot=deferred.row.valid-1;m.source_raw=uint32_t(source->raw);m.deferred_raw=deferred.row.deferred;
  m.row.valid=record+1;m.row.pre=uint32_t(cursor);m.row.deferred=uint32_t(cursor+bytes);m.row.reserved=deferred.row.reserved==2?2u:source->opaque?1u:0u;
  if(!source->opaque && m.row.reserved!=2){m.row.table=source->alpha.rec.tex;std::memcpy(m.row.p0,source->alpha.rec.p0,32);std::memcpy(m.row.t,source->alpha.rec.t,128);}
  cursor+=bytes*2;q.materials.push_back(m);
 }
 // Pool rows (native_hit_pool.h step 3): every ready record with a slot whose views and
 // sampler are current, against this frame's tile-set table. Same 256 B row; its t/p0/p1,
 // reflectance and channel come from the gather below (the record's captured engine bytes).
 Bg3Binding pool_table_binding{};uint32_t pool_overflow=0;
 std::vector<std::array<uint32_t,12>> pool_engine;
 if(compute_ready(false)){
  std::lock_guard pool(pool_mutex);
  if(pool_tilesets.frame==slot.frame)pool_table_binding=pool_tilesets.binding;
  if(pool_table_binding.buffer && pool_table_binding.range>=128)pool_table.for_each([&](uint64_t,auto& r){auto& m=r.payload;const auto* recipe=m.recipe;
   if(!recipe || m.basis.state!=pool::Basis::Ready || m.basis.slot>=nh_pool_rows || m.basis.gen!=m.mesh.gen || !m.mesh.basis_bytes ||
      r.seen.status!=pool::Status::Qualified || r.conflict || r.prev_conflict || !pool_current(m,frame_index()))return;
   Material x{};x.table=m.table;x.normal=m.normal;x.albedo=m.albedo;x.physical=m.physical;x.sampler=m.sampler;
   if(m.detail_layout()){x.layout=native_vt::LayoutDetail;x.detail_normal=m.detail_normal;x.detail_mask=m.detail_mask;x.detail_sampler=m.detail_sampler;
    std::memcpy(x.detail,m.detail,16);}
   if(m.plain_layout()){x.layout=native_vt::LayoutIridescent;x.plain=m.plain;std::memcpy(x.plain_floats,m.plain_floats,24);}
   x.row.valid=m.basis.slot+1;x.row.triangles=m.mesh.tris;x.row.pre=uint32_t(m.mesh.pool_off);x.row.deferred=uint32_t(m.mesh.basis_off);
   x.row.stride=nh_pool_basis_vertex;x.row.frame=uint32_t(slot.frame);x.row.reserved=3;
   const uint32_t gen=uint32_t(m.mesh.gen);std::memcpy(&x.row.parameters[2],&gen,4);
   // Row word 220: reflectance mode (rt_hit_material_pool: 2 mixes the normal layer's x).
   std::memcpy(&x.row.parameters[3],&recipe->reflectance,4);
   std::array<uint32_t,12> e{};const uint32_t t=recipe->transform_offset/4u;
   for(uint32_t w=0;w<8;++w)e[w]=m.engine[t+w];e[8]=m.engine[recipe->tileset_index_offset/4u];e[9]=m.engine[recipe->channel_offset/4u];e[10]=m.reflectance;
   // LayoutIridescent: no UV transform/tile set (the gather skips them, push word 3 = 2); the
   // row's p0/p1 carry its 1:1 floats instead (strength, tiling, iridescence, lo; hi, thickness).
   if(m.plain_layout()){std::memcpy(x.row.p0,m.plain_floats,16);std::memcpy(x.row.p1,m.plain_floats+4,8);for(uint32_t w=0;w<9;++w)e[w]=0;}
   q.pool_rows.push_back(x);pool_engine.push_back(e);
  });
 }
 // Their sampler/cache indices extend the packet's (bindings 14/15); new samplers are cloned per query.
 if(!q.pool_rows.empty()){
  ResourceBudget budget=packet->resources;budget.material_count=0;
  auto identity=[](const View& v){return CacheIdentity{uint64_t(v.view),v.id,uint64_t(v.image),v.image_id,uint64_t(v.layout)};};
  size_t w=0;
  for(size_t i=0;i<q.pool_rows.size();++i){auto& m=q.pool_rows[i];ResourcePlan plan{};
   // LayoutIridescent: a sampler slot only (its views are 2D texture-table entries, no VT caches).
   const bool plain=m.layout==native_vt::LayoutIridescent;
   if(!(plain?budget.plan_sampler(m.sampler.sampler.id,plan):budget.plan(m.sampler.sampler.id,{identity(m.normal),identity(m.albedo),identity(m.physical)},plan))){++pool_overflow;continue;}
   // LayoutDetail: its detail sampler (set 0:7) gets a slot too; new clones are appended in index order.
   const bool detail=m.layout==native_vt::LayoutDetail;ResourcePlan second{};
   if(detail && !budget.plan_second_sampler(plan,m.detail_sampler.sampler.id,second)){++pool_overflow;continue;}
   VkSampler made[2]{};bool failed=false;
   for(uint32_t n=0;n<2 && !failed;++n){
    if(!(n?detail && second.indices.new_sampler:plan.indices.new_sampler))continue;
    auto info=(n?m.detail_sampler:m.sampler).sampler.info;info.pNext=nullptr;failed=dd->vk.CreateSampler(dd->device,&info,nullptr,&made[n])!=VK_SUCCESS;
   }
   if(failed){for(auto s:made)if(s)dd->vk.DestroySampler(dd->device,s,nullptr);++pool_overflow;continue;}
   for(auto s:made)if(s)q.pool_samplers.push_back(s);
   budget.commit(plan);if(detail){budget.commit_sampler(second);m.row.padding[3]=second.indices.sampler;}
   m.row.sampler=plan.indices.sampler;m.row.normal=plan.indices.normal;m.row.albedo=plan.indices.albedo;m.row.physical=plan.indices.physical;
   q.pool_rows[w]=m;pool_engine[w]=pool_engine[i];++w;
  }
  q.pool_rows.resize(w);pool_engine.resize(w);
 }
 if(q.materials.empty() && q.pool_rows.empty()){
  log_capacity("no exact mapping");log_instanced();
  if(cfg_hot(CfgHot::rt_stats) && slot.frame%120==0)log("[hit material query] no exact mapping frame %llu source records %zu deferred records %zu; camera %p+%llu",
   (unsigned long long)slot.frame,slot.pre.size(),packet->materials.size(),(void*)slot.camera,(unsigned long long)slot.camera_offset);return false;
 }
 // Appended texture-table views (opaque: VT table; tileset: its 9 native 2D views). A
 // material whose new views would not fit the scene set's free table range is dropped
 // whole (counted), instead of failing the whole query.
 std::vector<VkDescriptorImageInfo> tables;std::vector<View> table_views;
 auto same=[](const View& a,const View& b){return a.view==b.view && a.id==b.id && a.image_id==b.image_id && a.layout==b.layout;};
 auto fits=[&](const Material& m){
  if(m.row.reserved!=1 && m.row.reserved!=2 && m.row.reserved!=3)return true;
  std::vector<const View*> added;auto need=[&](const View& v){
   if(std::any_of(table_views.begin(),table_views.end(),[&](const View& t){return same(t,v);}) || std::any_of(added.begin(),added.end(),[&](const View* t){return same(*t,v);}))return;
   added.push_back(&v);};
  if(m.layout==native_vt::LayoutIridescent)for(const auto& v:m.plain)need(v);
  else if(m.row.reserved!=2)need(m.table);else for(const auto& v:m.tileset_views)need(v);
  if(m.layout==native_vt::LayoutDetail){need(m.detail_normal);need(m.detail_mask);}
  if(table_views.size()+added.size()>table_free)return false;
  for(const View* v:added){table_views.push_back(*v);tables.push_back({VK_NULL_HANDLE,v->view,v->layout});}
  return true;
 };
 std::erase_if(q.materials,[&](const Material& m){if(fits(m))return false;++table_overflow;return true;});
 {size_t w=0;for(size_t i=0;i<q.pool_rows.size();++i)if(fits(q.pool_rows[i])){q.pool_rows[w]=q.pool_rows[i];pool_engine[w]=pool_engine[i];++w;}else ++table_overflow;
  q.pool_rows.resize(w);pool_engine.resize(w);}
 auto table_index=[&](const View& v){return uint32_t(std::find_if(table_views.begin(),table_views.end(),[&](const View& t){return same(t,v);})-table_views.begin());};
 for(auto& m:q.materials){
  if(m.row.reserved==1)m.row.table=table_index(m.table);
  if(m.row.reserved==2)for(uint32_t n=0;n<9;++n){uint32_t index=table_index(m.tileset_views[n]);std::memcpy(m.row.t+n,&index,4);}
 }
 for(auto& m:q.pool_rows){
  // LayoutIridescent words (native_hit_records.h): layout 2, then the five 2D table indices.
  if(m.layout==native_vt::LayoutIridescent){m.row.table=0;m.row.padding[0]=native_vt::LayoutIridescent;for(uint32_t n=0;n<5;++n)m.row.padding[1+n]=table_index(m.plain[n]);continue;}
  m.row.table=table_index(m.table);
  // Detail words (native_hit_records.h nh_pool_row_detail): layout, the two 2D table indices,
  // the detail sampler slot (set above), then strength, tiling, mask power, roughness add.
  if(m.layout==native_vt::LayoutDetail){m.row.padding[0]=native_vt::LayoutDetail;m.row.padding[1]=table_index(m.detail_normal);m.row.padding[2]=table_index(m.detail_mask);
   std::memcpy(m.row.padding+4,m.detail,16);}}
 tables_used=uint32_t(tables.size());log_capacity(q.materials.empty() && q.pool_rows.empty()?"tables full":"mapped");log_instanced();
 if(q.materials.empty() && q.pool_rows.empty())return false;
 uint32_t first_table=0;q.rows_bytes=q.pool_rows.empty()?row_bytes:row_bytes+pool_rows_bytes;
 if(!create(q.rows,q.rows_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,true) ||
    !create(q.data,std::max<VkDeviceSize>(cursor,256),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) || !alpha_clone_hit_set(slot.set,&q.pool,&q.set,tables,&first_table) ||
    (!q.pool_rows.empty() && !create(q.pool_ubo,VkDeviceSize(q.pool_rows.size())*512u,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,true))){
  destroy_query(q);return false;
 }
 for(auto& m:q.materials){if(m.row.reserved==1)m.row.table+=first_table;
  if(m.row.reserved==2)for(uint32_t n=0;n<9;++n){uint32_t index;std::memcpy(&index,m.row.t+n,4);index+=first_table;std::memcpy(m.row.t+n,&index,4);}
 }
 for(auto& m:q.pool_rows){
  if(m.layout==native_vt::LayoutIridescent){for(uint32_t n=0;n<5;++n)m.row.padding[1+n]+=first_table;continue;}
  m.row.table+=first_table;if(m.layout==native_vt::LayoutDetail){m.row.padding[1]+=first_table;m.row.padding[2]+=first_table;}}
 // Gather inputs in the layout of hit_material_gather.hlsl's Engine (v[4] p0, v[5] p1, words.y
 // tile-set index, words.z channel) and Physical (x reflectance).
 for(size_t i=0;i<q.pool_rows.size();++i){auto* at=static_cast<unsigned char*>(q.pool_ubo.mapped)+i*512u;std::memset(at,0,512);
  std::memcpy(at+64,pool_engine[i].data(),32);std::memcpy(at+100,&pool_engine[i][8],8);std::memcpy(at+256,&pool_engine[i][10],4);}
 publish_word(q,0); // initialized before any GPU use; never rewritten after success.
 if(!mark_owner(cmd,slot)){destroy_query(q);return false;}
 if(std::find(slot.owners.begin(),slot.owners.end(),packet->recording)==slot.owners.end())slot.owners.push_back(packet->recording);
 std::vector<VkDescriptorImageInfo> caches(cache_slots,{VK_NULL_HANDLE,slot.dummy_array,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}),samplers(sampler_slots,{slot.dummy_sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED});
 for(const auto& m:q.materials){
  if(m.row.reserved!=2){caches[m.row.normal]={VK_NULL_HANDLE,m.normal.view,m.normal.layout};caches[m.row.albedo]={VK_NULL_HANDLE,m.albedo.view,m.albedo.layout};caches[m.row.physical]={VK_NULL_HANDLE,m.physical.view,m.physical.layout};}
  samplers[m.row.sampler]={packet->samplers[m.row.sampler],VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
  std::vector<View> retained;if(m.row.reserved==2)retained.assign(m.tileset_views.begin(),m.tileset_views.end());else retained={m.table,m.normal,m.albedo,m.physical};
  for(const auto& view:retained){slot.handles.push_back(uint64_t(view.view));slot.handles.push_back(uint64_t(view.image));life_note(cmd,uint64_t(view.view));life_note(cmd,uint64_t(view.image));}
 }
 for(const auto& m:q.pool_rows){
  if(m.layout!=native_vt::LayoutIridescent){caches[m.row.normal]={VK_NULL_HANDLE,m.normal.view,m.normal.layout};caches[m.row.albedo]={VK_NULL_HANDLE,m.albedo.view,m.albedo.layout};caches[m.row.physical]={VK_NULL_HANDLE,m.physical.view,m.physical.layout};}
  const size_t shared=packet->samplers.size();
  auto sampler_at=[&](uint32_t i){return VkDescriptorImageInfo{i<shared?packet->samplers[i]:q.pool_samplers[i-shared],VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};};
  samplers[m.row.sampler]=sampler_at(m.row.sampler);
  if(m.layout==native_vt::LayoutDetail)samplers[m.row.padding[3]]=sampler_at(m.row.padding[3]);
  for(const View* view:{&m.table,&m.normal,&m.albedo,&m.physical,&m.detail_normal,&m.detail_mask,&m.plain[0],&m.plain[1],&m.plain[2],&m.plain[3],&m.plain[4]}){if(!view->view)continue;
   slot.handles.push_back(uint64_t(view->view));slot.handles.push_back(uint64_t(view->image));life_note(cmd,uint64_t(view->view));life_note(cmd,uint64_t(view->image));}
 }
 if(!q.pool_rows.empty())life_note(cmd,uint64_t(pool_table_binding.buffer));
 VkDescriptorBufferInfo buffers[2]={{q.rows.b,0,q.rows_bytes},{q.data.b,0,std::max<VkDeviceSize>(cursor,256)}};
 VkWriteDescriptorSet writes[4]{};for(uint32_t i=0;i<4;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=q.set;writes[i].dstBinding=12+i;writes[i].descriptorCount=1;}
 for(uint32_t i=0;i<2;++i){writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=buffers+i;}
 writes[2].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[2].descriptorCount=cache_slots;writes[2].pImageInfo=caches.data();
 writes[3].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;writes[3].descriptorCount=sampler_slots;writes[3].pImageInfo=samplers.data();dd->vk.UpdateDescriptorSets(dd->device,4,writes,0,nullptr);
 // Only this query writes its rows/basis. Invalid submit order leaves its
 // publication word zero, so the assembly cannot read unfinished producers.
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
 dd->vk.CmdFillBuffer(cmd,q.rows.b,0,row_payload,0);
 if(!q.pool_rows.empty())dd->vk.CmdFillBuffer(cmd,q.rows.b,nh_pool_row_base,pool_rows_bytes,0);
 uint32_t header[4]={uint32_t(slot.frame),slot.generation,uint32_t(q.materials.size()),1};dd->vk.CmdUpdateBuffer(cmd,q.rows.b,0,16,header);
 for(const auto& m:q.materials){auto staged=m.row;staged.valid=0;dd->vk.CmdUpdateBuffer(cmd,q.rows.b,16+VkDeviceSize(m.row.valid-1)*record_bytes,record_bytes,&staged);}
 for(const auto& m:q.pool_rows){auto staged=m.row;staged.valid=0;dd->vk.CmdUpdateBuffer(cmd,q.rows.b,nh_pool_row_base+VkDeviceSize(m.row.valid-1)*record_bytes,record_bytes,&staged);}
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
 VkDescriptorBufferInfo inputs[5]={{slot.data.b,0,slot.data_bytes},{VK_NULL_HANDLE,0,stream_bytes},{packet->rows.b,0,packet_rows},{q.rows.b,0,row_bytes},{q.data.b,0,std::max<VkDeviceSize>(cursor,256)}};
 VkWriteDescriptorSet join_writes[5]{};for(uint32_t i=0;i<5;++i){join_writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};join_writes[i].dstBinding=i;join_writes[i].descriptorCount=1;join_writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;join_writes[i].pBufferInfo=inputs+i;}
 dd->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,join_pipeline);uint32_t bound_chunk=UINT32_MAX;
 for(const auto& m:q.materials){uint32_t bytes=m.row.triangles*3u*m.row.stride;
  // Binding 1 is the material's deferred XFB chunk; re-pushed only when it changes.
  if(m.chunk!=bound_chunk){bound_chunk=m.chunk;inputs[1].buffer=packet->chunks[m.chunk].b;dd->vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,join_layout,0,5,join_writes);}
  uint32_t push[8]={16u+(m.row.valid-1)*record_bytes,16u+m.snapshot*record_bytes,m.source_raw,m.deferred_raw,bytes,uint32_t(row_payload),m.row.valid-1,m.snapshot};
  dd->vk.CmdPushConstants(cmd,join_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,32,push);dd->vk.CmdDispatch(cmd,(bytes/4u+63u)/64u,1,1);
 }
 // Pool rows: hit_material_gather.hlsl in its packet mode (reserved0 1) on the record's bytes and
 // this frame's tile-set table; it publishes valid = slot + 1 only with a finite reflectance.
 if(!q.pool_rows.empty())dd->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,gather_pipeline);
 for(size_t i=0;i<q.pool_rows.size();++i){const auto& m=q.pool_rows[i];
  VkDescriptorBufferInfo info[4]={{q.pool_ubo.b,i*512u,112},{q.pool_ubo.b,i*512u+256u,16},{pool_table_binding.buffer,pool_table_binding.offset,pool_table_binding.range},{q.rows.b,0,q.rows_bytes}};
  VkWriteDescriptorSet writes[4]{};for(uint32_t n=0;n<4;++n){writes[n]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[n].dstBinding=n;writes[n].descriptorCount=1;writes[n].descriptorType=n<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[n].pBufferInfo=info+n;}
  uint32_t push[4]={uint32_t(nh_pool_row_base+VkDeviceSize(m.row.valid-1)*record_bytes),m.row.valid-1,1,m.layout==native_vt::LayoutIridescent?2u:0u};
  dd->vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,gather_layout,0,4,writes);
  dd->vk.CmdPushConstants(cmd,gather_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,push);dd->vk.CmdDispatch(cmd,1,1,1);
 }
 barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);restore_bindings(cmd,VK_PIPELINE_BIND_POINT_COMPUTE);
 *query_alpha=q.set;
 if(cfg_hot(CfgHot::rt_stats) && slot.frame%120==0)log("[hit material query] frame %llu scene %u query cmd %p recording %llu source %llu deferred %llu mapped %zu pool rows %zu (resource overflow %u) bytes %llu; publication awaits actual submit order",
  (unsigned long long)slot.frame,slot.generation,(void*)cmd,(unsigned long long)q.recording,(unsigned long long)q.source,(unsigned long long)packet->recording,q.materials.size(),q.pool_rows.size(),pool_overflow,(unsigned long long)cursor);
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
  if(cfg_hot(CfgHot::rt_stats) && slots[q.slot].frame%120==0)log("[hit material submit] frame %llu query %llu source %llu deferred %llu ordered %u queue %p graphics %p",
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
