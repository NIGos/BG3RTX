// Native DLSS 5 NR, on the existing Vulkan NGX owner, between RR and rrpost.
// Codec/guide math is ported from the user's DCS Control. No Init/Shutdown,
// D3D12 interop, event park, unsigned-runtime workaround or validation filter.
#include "dlss5.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <set>
#include <utility>

namespace bg3 {
namespace {
constexpr unsigned kSlots=8;
struct CodecConstants {
  uint32_t size[2],source_size[2],source_base[2],proxy_size[2];
  float paper_white_scale,transfer_strength,color_strength;uint32_t hdr_mode;
  uint32_t region_base[2],region_size[2];
  float foveation_width,foveation_height,foveation_roundness,foveation_feather;
  uint32_t show_alignment_border,mask_count,padding[2];float mask_bounds[4][4];
};
static_assert(sizeof(CodecConstants)==160);
struct Image {
  VkImage image{};VkImageView view{};VkDeviceMemory memory{};VkFormat format{};
  uint32_t w{},h{};NVSDK_NGX_Resource_VK ngx{};
};
struct Uniform {VkBuffer buffer{};VkDeviceMemory memory{};void* map{};};
Dlss5Ngx ngx{};
DeviceData* device{};
std::mutex mutex;
bool failed{},history{},capabilities_checked{},need_reset=true;
std::set<VkDevice> enabled_devices;
std::atomic<bool> retired_owner{};
uint64_t last_recorded_frame{},guide_signature{},history_recording{};
VkCommandBuffer history_cmd{};bool history_submitted{};
float previous_jitter[2]{};
std::string why;
int last_bypass=-1,reset_seen{};
uint64_t stats_epoch{},stats_start{};
double sums[4]{};unsigned measurements{};
void state(const char* message) {
  if(why!=message){why=message;log("[dlss5] NR skipped: %s",message);}
  need_reset=true;history=false;
}
void fail(const char* operation,unsigned result=0) {
  log("[dlss5] NR disabled for session: %s (NGX 0x%08x); RR output retained",operation,result);
  failed=true;need_reset=true;history=false;
}
void config_state() {
  int bypass=cfg_int("dlss5_nr_bypass",0)!=0;
  if(last_bypass!=bypass){last_bypass=bypass;log("[dlss5] NR bypass=%d",bypass);need_reset=true;history=false;}
  if(stats_window::reset(cfg_int,reset_seen)) {
    ++stats_epoch;std::fill(std::begin(sums),std::end(sums),0.0);measurements=0;stats_start=frame_index();
  }
}
void image_destroy(Image& im) {
  if(!device)return;
  if(im.view)device->vk.DestroyImageView(device->device,im.view,nullptr);
  if(im.image)device->vk.DestroyImage(device->device,im.image,nullptr);
  if(im.memory)device->vk.FreeMemory(device->device,im.memory,nullptr);
  im={};
}
bool image_create(Image& im,uint32_t w,uint32_t h,VkFormat fmt) {
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=fmt;
  ci.extent={w,h,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;
  ci.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if(device->vk.CreateImage(device->device,&ci,nullptr,&im.image)!=VK_SUCCESS)return false;
  VkMemoryRequirements req{};device->vk.GetImageMemoryRequirements(device->device,im.image,&req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;
  ai.memoryTypeIndex=find_memory_type(device,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if(ai.memoryTypeIndex==UINT32_MAX || device->vk.AllocateMemory(device->device,&ai,nullptr,&im.memory)!=VK_SUCCESS ||
     device->vk.BindImageMemory(device->device,im.image,im.memory,0)!=VK_SUCCESS){image_destroy(im);return false;}
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=im.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=fmt;
  vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  if(device->vk.CreateImageView(device->device,&vi,nullptr,&im.view)!=VK_SUCCESS){image_destroy(im);return false;}
  im.w=w;im.h=h;im.format=fmt;im.ngx.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;im.ngx.ReadWrite=true;
  auto& r=im.ngx.Resource.ImageViewInfo;r.Image=im.image;r.ImageView=im.view;r.SubresourceRange=vi.subresourceRange;r.Format=fmt;r.Width=w;r.Height=h;
  return true;
}
void uniform_destroy(Uniform& u) {
  if(u.map)device->vk.UnmapMemory(device->device,u.memory);
  if(u.buffer)device->vk.DestroyBuffer(device->device,u.buffer,nullptr);
  if(u.memory)device->vk.FreeMemory(device->device,u.memory,nullptr);u={};
}
bool uniform_create(Uniform& u) {
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=160;ci.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  if(device->vk.CreateBuffer(device->device,&ci,nullptr,&u.buffer)!=VK_SUCCESS)return false;
  VkMemoryRequirements r{};device->vk.GetBufferMemoryRequirements(device->device,u.buffer,&r);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=r.size;
  ai.memoryTypeIndex=find_memory_type(device,r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if(ai.memoryTypeIndex==UINT32_MAX || device->vk.AllocateMemory(device->device,&ai,nullptr,&u.memory)!=VK_SUCCESS ||
     device->vk.BindBufferMemory(device->device,u.buffer,u.memory,0)!=VK_SUCCESS ||
     device->vk.MapMemory(device->device,u.memory,0,160,0,&u.map)!=VK_SUCCESS){uniform_destroy(u);return false;}
  return true;
}
struct Pipelines {
  VkDescriptorSetLayout descriptors{};VkPipelineLayout layout{};VkPipeline pipes[3]{};uint64_t generations[3]{};
  ~Pipelines(){if(!device)return;for(auto p:pipes)if(p)device->vk.DestroyPipeline(device->device,p,nullptr);
    if(layout)device->vk.DestroyPipelineLayout(device->device,layout,nullptr);
    if(descriptors)device->vk.DestroyDescriptorSetLayout(device->device,descriptors,nullptr);}
};
// No GPU/NGX calls from CRT static destructors during DLL_PROCESS_DETACH.
// Explicit device teardown drains and clears these; abrupt process exit leaves
// them to the OS while the existing NGX owner remains pinned.
auto& pipelines=*new std::shared_ptr<Pipelines>;
bool get_pipelines() {
  const char* entries[]{"EncodeMain","DecodeMain","GuidesMain"};Bg3ShaderBlob code[3]{};
  for(unsigned i=0;i<3;++i)if(!shader_get("dlss5_codec.hlsl",entries[i],"cs_6_6",&code[i]))return false;
  bool fresh=!pipelines;
  if(pipelines)for(unsigned i=0;i<3;++i)fresh|=pipelines->generations[i]!=code[i].generation;
  if(!fresh)return true;
  auto next=std::make_shared<Pipelines>();VkDescriptorSetLayoutBinding bindings[9]{};
  for(unsigned i=0;i<9;++i){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[i].descriptorType=i<3?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:(i==5||i==8)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;}
  VkDescriptorSetLayoutCreateInfo d{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};d.bindingCount=9;d.pBindings=bindings;
  if(device->vk.CreateDescriptorSetLayout(device->device,&d,nullptr,&next->descriptors)!=VK_SUCCESS)return false;
  VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};l.setLayoutCount=1;l.pSetLayouts=&next->descriptors;
  if(device->vk.CreatePipelineLayout(device->device,&l,nullptr,&next->layout)!=VK_SUCCESS)return false;
  for(unsigned i=0;i<3;++i){
    VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};m.codeSize=code[i].size_bytes;m.pCode=code[i].code;VkShaderModule module{};
    if(device->vk.CreateShaderModule(device->device,&m,nullptr,&module)!=VK_SUCCESS)return false;
    VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};p.layout=next->layout;
    p.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,entries[i]};
    auto result=device->vk.CreateComputePipelines(device->device,VK_NULL_HANDLE,1,&p,nullptr,&next->pipes[i]);
    device->vk.DestroyShaderModule(device->device,module,nullptr);if(result!=VK_SUCCESS)return false;next->generations[i]=code[i].generation;
  }
  pipelines=next;need_reset=true;history=false;log("[dlss5] native codec/guides pipelines ready (3 fixed entries)");return true;
}
struct Feature {
  NVSDK_NGX_Handle* handle{};NVSDK_NGX_Parameter* parameters{};uint32_t w{},h{};bool inverted{};
  ~Feature(){if(handle&&ngx.release)ngx.release(handle);if(parameters&&ngx.destroy)ngx.destroy(parameters);}
};
auto& feature=*new std::shared_ptr<Feature>;
struct Slot {
  VkCommandBuffer cmd{};uint64_t recording{},submitted_frame{},epoch{};VkQueue queue{};bool submitted{},measured{},query_valid{};dlss5_frame::LayoutState layout;
  uint32_t w{},h{};Image original,proxy,neural,motion,depth;Uniform constants[3];VkDescriptorPool pool{};VkDescriptorSet sets[3]{};VkQueryPool query{};
  std::shared_ptr<Feature> feature;std::shared_ptr<Pipelines> pipelines;
};
auto& slots=*new std::array<Slot,kSlots>;
void slot_destroy(Slot& s) {
  if(s.query)device->vk.DestroyQueryPool(device->device,s.query,nullptr);
  if(s.pool)device->vk.DestroyDescriptorPool(device->device,s.pool,nullptr);
  for(auto& u:s.constants)uniform_destroy(u);
  for(auto* im:{&s.original,&s.proxy,&s.neural,&s.motion,&s.depth})image_destroy(*im);
  s={};
}
bool complete(const Slot& s){return s.submitted && life_before_destroy(uint64_t(s.original.image),"NR recording slot completion",false);}
void harvest(Slot& s) {
  if(!s.query || !s.query_valid || s.measured || !complete(s))return;
  uint64_t values[12]{};
  auto r=device->vk.GetQueryPoolResults(device->device,s.query,0,6,sizeof values,values,2*sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  if(r!=VK_SUCCESS)return;for(unsigned i=0;i<6;++i)if(!values[i*2+1])return;s.measured=true;
  if(s.epoch!=stats_epoch)return;
  VkPhysicalDeviceProperties p{};device->inst->GetPhysicalDeviceProperties(device->physical,&p);
  auto ms=[&](unsigned a,unsigned b){return double(values[b*2]-values[a*2])*p.limits.timestampPeriod/1e6;};
  sums[0]+=ms(2,3);sums[1]+=ms(0,1);sums[2]+=ms(4,5);sums[3]+=ms(0,5);++measurements;
}
Slot* reserve(VkCommandBuffer cmd,uint64_t recording,uint32_t w,uint32_t h) {
  for(auto& s:slots){harvest(s);if(s.cmd==cmd&&s.recording==recording){state("multiple RR evaluates in one recording");return nullptr;}}
  for(auto& s:slots){
    if(s.recording && (cmd_recording(s.cmd)==s.recording || (s.submitted&&!complete(s))))continue;
    auto old_pipelines=s.pipelines;s.feature.reset();s.cmd=cmd;s.recording=recording;s.submitted=false;s.measured=false;s.query_valid=false;s.layout.begin_recording();s.epoch=stats_epoch;
    if(s.w!=w||s.h!=h){slot_destroy(s);s.cmd=cmd;s.recording=recording;s.epoch=stats_epoch;
      s.w=w;s.h=h;
      if(!image_create(s.original,w,h,VK_FORMAT_R16G16B16A16_SFLOAT)||!image_create(s.proxy,w,h,VK_FORMAT_R16G16B16A16_SFLOAT)||
         !image_create(s.neural,w,h,VK_FORMAT_R16G16B16A16_SFLOAT)||!image_create(s.motion,w,h,VK_FORMAT_R32G32_SFLOAT)||
         !image_create(s.depth,w,h,VK_FORMAT_R32_SFLOAT)){slot_destroy(s);fail("private images");return nullptr;}
      for(auto& u:s.constants)if(!uniform_create(u)){slot_destroy(s);fail("codec uniform buffer");return nullptr;}
      log("[dlss5] persistent recording slot %ux%u: %.1f MiB private images (up to %u slots, plus NGX history)",w,h,double(uint64_t(w)*h*36)/(1024*1024),kSlots);
    }
    // Layout generations may differ after shader hot reload. This slot has no
    // live executable recording and its last actual submission completed.
    if(s.pool && old_pipelines!=pipelines){device->vk.DestroyDescriptorPool(device->device,s.pool,nullptr);s.pool=VK_NULL_HANDLE;}
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,9},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,12},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,6}};
    VkDescriptorPoolCreateInfo p{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};p.maxSets=3;p.poolSizeCount=3;p.pPoolSizes=sizes;
    if(!s.pool && device->vk.CreateDescriptorPool(device->device,&p,nullptr,&s.pool)!=VK_SUCCESS){fail("descriptor pool");return nullptr;}
    VkDescriptorSetLayout layouts[]{pipelines->descriptors,pipelines->descriptors,pipelines->descriptors};
    VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};a.descriptorPool=s.pool;a.descriptorSetCount=3;a.pSetLayouts=layouts;
    if((old_pipelines!=pipelines || !s.sets[0]) && device->vk.AllocateDescriptorSets(device->device,&a,s.sets)!=VK_SUCCESS){fail("descriptor sets");return nullptr;}
    s.pipelines=pipelines;
    if(logging::stats_enabled(cfg_int) && !s.query){VkQueryPoolCreateInfo q{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};q.queryType=VK_QUERY_TYPE_TIMESTAMP;q.queryCount=6;
      if(device->vk.CreateQueryPool(device->device,&q,nullptr,&s.query)!=VK_SUCCESS)s.query=VK_NULL_HANDLE;}
    return &s;
  }
  state("all recording slots are retained or pending completion");return nullptr;
}
void barrier(VkCommandBuffer cmd,VkImage image,VkImageLayout before,VkImageLayout after,VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=before;b.newLayout=after;
  b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={aspect,0,1,0,1};
  b.srcAccessMask=before==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  device->vk.CmdPipelineBarrier(cmd,before==VK_IMAGE_LAYOUT_UNDEFINED?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
}
VkImageAspectFlags aspect(VkFormat f){return f==VK_FORMAT_D24_UNORM_S8_UINT||f==VK_FORMAT_D32_SFLOAT_S8_UINT||f==VK_FORMAT_D16_UNORM_S8_UINT?
  VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:f==VK_FORMAT_D32_SFLOAT||f==VK_FORMAT_D16_UNORM?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;}
void descriptors(Slot& s,unsigned direction,const Dlss5Frame& frame) {
  VkImageView input[3]{frame.color.view,frame.color.view,frame.color.view};
  if(direction==1){input[0]=s.original.view;input[1]=s.proxy.view;input[2]=s.neural.view;}
  if(direction==2){input[0]=frame.motion.view;input[1]=frame.depth.view;}
  VkDescriptorImageInfo images[7]{};VkWriteDescriptorSet writes[9]{};
  for(unsigned i=0;i<7;++i){images[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
    images[i].imageView=i<3?input[i]:i==3?(direction==1?frame.color.view:s.original.view):i==4?s.proxy.view:i==5?s.motion.view:s.depth.view;
    const unsigned binding=i<5?i:i+1;writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=s.sets[direction];writes[i].dstBinding=binding;
    writes[i].descriptorCount=1;writes[i].descriptorType=i<3?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;writes[i].pImageInfo=&images[i];}
  VkDescriptorBufferInfo buffers[]={{s.constants[direction==2?0:direction].buffer,0,160},{s.constants[2].buffer,0,80}};
  for(unsigned i=0;i<2;++i){auto& w=writes[7+i];w={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=s.sets[direction];w.dstBinding=i?8:5;
    w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&buffers[i];}
  device->vk.UpdateDescriptorSets(device->device,9,writes,0,nullptr);
}
void dispatch(VkCommandBuffer cmd,Slot& s,unsigned p){
  device->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s.pipelines->pipes[p]);
  device->vk.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s.pipelines->layout,0,1,&s.sets[p],0,nullptr);
  unsigned block=p==2?8:16;device->vk.CmdDispatch(cmd,(s.w+block-1)/block,(s.h+block-1)/block,1);
}
unsigned scaling(NVSDK_NGX_Parameter* p){p->Set("DLSSNR.ScalingRatio",1.0f);return 1;}
bool create_feature(VkCommandBuffer cmd,Slot& slot,bool inverted) {
  if(feature && feature->w==slot.w&&feature->h==slot.h&&feature->inverted==inverted){slot.feature=feature;return true;}
  auto next=std::make_shared<Feature>();next->w=slot.w;next->h=slot.h;next->inverted=inverted;
  if(ngx.allocate(&next->parameters)!=NVSDK_NGX_Result_Success || !next->parameters){fail("AllocateParameters");return false;}
  auto* p=next->parameters;for(auto key:{"Width","OutWidth","DLSSNR.Width","DLSSNR.InputWidth","DLSSNR.OutputWidth","DLSSNR.Output.Width"})p->Set(key,slot.w);
  for(auto key:{"Height","OutHeight","DLSSNR.Height","DLSSNR.InputHeight","DLSSNR.OutputHeight","DLSSNR.Output.Height"})p->Set(key,slot.h);
  p->Set("DLSSNR.Enabled",1u);p->Set("DLSSNR.ScalingRatio",1.0f);p->Set("DLSSNR.Scale",1.0f);p->Set("DLSSNR.Upscaling",0u);
  p->Set("PerfQualityValue",0u);p->Set("DLSS.Feature.Create.Flags",unsigned(NVSDK_NGX_DLSS_Feature_Flags_IsHDR)|(inverted?unsigned(NVSDK_NGX_DLSS_Feature_Flags_DepthInverted):0u));
  p->Set("CreationNodeMask",1u);p->Set("VisibilityNodeMask",1u);p->Set("DLSSNR.Hint.Render.Preset",0u);p->Set("DLSSNR.Intensity",1.0f);
  p->Set("DLSSNR.LocalToneStrength",1.0f);p->Set("DLSSNR.LocalStructureStrength",1.0f);p->Set("DLSSNR.SkinStructureStrength",1.0f);
  p->Set("DLSSNR.ControlMask",static_cast<void*>(nullptr));p->Set("DLSSNR.UseAutoMask",0u);p->Set("DLSSNR.Style",0u);p->Set("DLSSNR.UICorrection",0u);
  p->Set("DLSSNRComputeScalingRatioCallback",reinterpret_cast<void*>(&scaling));
  // Retain even a failed feature and parameter block with this recording: NGX
  // may have recorded initialization work before returning a failure.
  slot.feature=next;
  log("[dlss5] creating feature 18 on existing NGX owner, %ux%u",slot.w,slot.h);
  auto r=ngx.create(device->device,cmd,static_cast<NVSDK_NGX_Feature>(18),p,&next->handle);
  if(r!=NVSDK_NGX_Result_Success || !next->handle){fail("CreateFeature(18)",unsigned(r));return false;}
  feature=next;need_reset=true;history=false;log("[dlss5] NR created: feature 18, %ux%u",slot.w,slot.h);return true;
}
} // namespace

void dlss5_set_ngx(const Dlss5Ngx& api){std::lock_guard<std::mutex> lock(mutex);ngx=api;}
void dlss5_evaluate(DeviceData* dd,VkCommandBuffer cmd,const Dlss5Frame& frame) {
  if(!cfg_int("dlss5_nr",0))return;
  std::lock_guard<std::mutex> lock(mutex);config_state();
  auto* cs=cmd_state(cmd);bool guides=frame.depth.image&&frame.depth.view&&frame.motion.image&&frame.motion.view;
  auto gate=dlss5_frame::boundary(true,last_bypass!=0,frame.rr_success,frame.use_post,cs&&cs->in_pass,cs&&cs->level==VK_COMMAND_BUFFER_LEVEL_PRIMARY,guides,frame.debug);
  const char* reasons[]{"disabled","bypass","RR evaluate failed","RR before-post chain unavailable","inside a render pass","secondary or untracked recording","missing depth or motion guides","debug view"};
  if(gate==dlss5_frame::Boundary::Bypass){why="bypass";need_reset=true;history=false;return;}
  if(gate!=dlss5_frame::Boundary::Ready){state(reasons[unsigned(gate)]);return;}
  if(failed)return;
  if(!cs->game_alloc || !cs->recording || (cs->begin_flags&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)){
    state("unsupported command recording lifetime (simultaneous or untracked)");return;}
  if(!dd || !frame.color.image || !frame.color.view || frame.color.format!=VK_FORMAT_R16G16B16A16_SFLOAT){state("missing RGBA16F pre-post RR output");return;}
  if(!enabled_devices.count(dd->device)){state("NR not enabled on this device at startup (restart required)");return;}
  if(device&&device!=dd){state("different Vulkan device");return;}device=dd;
  if(!ngx.create||!ngx.evaluate||!ngx.release||!ngx.allocate||!ngx.destroy){state("existing NGX callbacks unavailable");return;}
  if(!capabilities_checked){
    NVSDK_NGX_Parameter* caps{};const auto result=ngx.capabilities?ngx.capabilities(&caps):NVSDK_NGX_Result_FAIL_NotImplemented;
    int available=0,init_result=0;const auto known=caps?caps->Get("DLSSNR.Available",&available):NVSDK_NGX_Result_FAIL_MissingInput;
    if(caps)caps->Get("DLSSNR.FeatureInitResult",&init_result);
    log("[dlss5] existing NGX capabilities: result 0x%08x, DLSSNR.Available %d (key 0x%08x), FeatureInitResult 0x%08x",unsigned(result),available,unsigned(known),unsigned(init_result));
    if(caps)ngx.destroy(caps);capabilities_checked=true;
    if(result!=NVSDK_NGX_Result_Success || known!=NVSDK_NGX_Result_Success || !available){fail("NR unavailable on existing NGX owner",unsigned(init_result));return;}
  }
  dlss5_frame::Guides guide{};auto input=frame.guides;
  uint64_t signature=1469598103934665603ull;
  auto append=[&](uint32_t v){signature^=v;signature*=1099511628211ull;};
  for(unsigned axis=0;axis<2;++axis){for(auto v:{input.render[axis],input.output[axis],input.depth_extent[axis],input.motion_extent[axis],input.depth_base[axis],input.motion_base[axis],input.motion_size[axis]})append(v);
    uint32_t bits{};std::memcpy(&bits,&input.scale[axis],sizeof bits);append(bits);}
  append(input.jittered?1u:0u);append(frame.depth_inverted?1u:0u);
  bool reset=input.reset||need_reset||!history||!history_submitted||guide_signature!=signature||last_recorded_frame+1<frame_index();input.reset=reset;
  if(!dlss5_frame::prepare(input,previous_jitter,history,guide)){state("invalid guide extent/subrect/scale/jitter");return;}
  if(!get_pipelines()){state("codec/guides shaders unavailable");return;}
  if(need_reset && !reset){reset=true;input.reset=true;dlss5_frame::prepare(input,previous_jitter,history,guide);}
  Slot* slot=reserve(cmd,cs->recording,frame.color.w,frame.color.h);if(!slot)return;auto& s=*slot;
  const auto old_depth=frame.depth.layout,old_motion=frame.motion.layout;
  if(old_depth==VK_IMAGE_LAYOUT_UNDEFINED||old_motion==VK_IMAGE_LAYOUT_UNDEFINED){state("unknown source image layout");return;}
  // All allocations and descriptor writes happen before modifying the RR output.
  CodecConstants codec{};codec.size[0]=codec.source_size[0]=codec.proxy_size[0]=codec.region_size[0]=s.w;
  codec.size[1]=codec.source_size[1]=codec.proxy_size[1]=codec.region_size[1]=s.h;
  codec.paper_white_scale=codec.transfer_strength=codec.color_strength=1;codec.hdr_mode=1;codec.foveation_width=codec.foveation_height=1;
  std::memcpy(s.constants[0].map,&codec,sizeof codec);std::memcpy(s.constants[1].map,&codec,sizeof codec);std::memcpy(s.constants[2].map,&guide,sizeof guide);
  for(unsigned i=0;i<3;++i)descriptors(s,i,frame);
  life_note(cmd,uint64_t(s.original.image)); // per-queue/submit-sequence completion marker, including failed creates
  life_note(cmd,uint64_t(frame.color.image));life_note(cmd,uint64_t(frame.color.view));
  for(const auto* b:{&frame.depth,&frame.motion}){life_note(cmd,uint64_t(b->image));life_note(cmd,uint64_t(b->view));}
  if(!create_feature(cmd,s,frame.depth_inverted)){restore_bindings(cmd,VK_PIPELINE_BIND_POINT_COMPUTE);cs->ours_since_event=true;return;}
  if(need_reset && !reset){reset=true;input.reset=true;dlss5_frame::prepare(input,previous_jitter,false,guide);std::memcpy(s.constants[2].map,&guide,sizeof guide);}
  for(auto* image:{&s.original,&s.proxy,&s.neural,&s.motion,&s.depth})barrier(cmd,image->image,s.layout.general?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
  s.layout.record_transition();
  barrier(cmd,frame.color.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);
  barrier(cmd,frame.depth.image,old_depth,VK_IMAGE_LAYOUT_GENERAL,aspect(frame.depth.format));barrier(cmd,frame.motion.image,old_motion,VK_IMAGE_LAYOUT_GENERAL);
  auto stamp=[&](unsigned i){if(s.query)device->vk.CmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,s.query,i);};
  if(s.query){device->vk.CmdResetQueryPool(cmd,s.query,0,6);s.query_valid=true;}stamp(0);dispatch(cmd,s,2);dispatch(cmd,s,0);
  for(auto* image:{&s.original,&s.proxy,&s.motion,&s.depth})barrier(cmd,image->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);stamp(1);
  barrier(cmd,frame.depth.image,VK_IMAGE_LAYOUT_GENERAL,old_depth,aspect(frame.depth.format));barrier(cmd,frame.motion.image,VK_IMAGE_LAYOUT_GENERAL,old_motion);
  auto* p=s.feature->parameters;p->Set("DLSSNR.Color",static_cast<void*>(&s.proxy.ngx));p->Set("DLSSNR.Output",static_cast<void*>(&s.neural.ngx));
  p->Set("DLSSNR.MVec",static_cast<void*>(&s.motion.ngx));p->Set("DLSSNR.Depth",static_cast<void*>(&s.depth.ngx));
  for(auto prefix:{"Color","Output","Depth","MVec"}){std::string base=std::string("DLSSNR.")+prefix+"Subrect";
    p->Set((base+"BaseX").c_str(),0u);p->Set((base+"BaseY").c_str(),0u);p->Set((base+"Width").c_str(),s.w);p->Set((base+"Height").c_str(),s.h);}
  p->Set("DLSSNR.MVecScaleX",float(s.w));p->Set("DLSSNR.MVecScaleY",float(s.h));p->Set("DLSSNR.DepthInverted",frame.depth_inverted?1u:0u);p->Set("DLSSNR.Reset",reset?1u:0u);
  stamp(2);auto result=ngx.evaluate(cmd,s.feature->handle,p,nullptr);stamp(3);stamp(4);
  if(result==NVSDK_NGX_Result_Success){barrier(cmd,s.neural.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);dispatch(cmd,s,1);
    barrier(cmd,frame.color.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);history=true;need_reset=false;last_recorded_frame=frame_index();guide_signature=signature;history_recording=s.recording;history_cmd=cmd;history_submitted=false;
    for(unsigned axis=0;axis<2;++axis)previous_jitter[axis]=input.jitter[axis]/float(input.render[axis]);
    if(why!="active"){why="active";log("[dlss5] NR evaluate result=0x00000001: RR -> feature 18 -> rrpost, colour %ux%u, render %ux%u, normalized MV scale %.9g %.9g, reset %u",s.w,s.h,input.render[0],input.render[1],guide.motion_scale[0],guide.motion_scale[1],unsigned(reset));}
  }else fail("EvaluateFeature(18)",unsigned(result));
  stamp(5);restore_bindings(cmd,VK_PIPELINE_BIND_POINT_COMPUTE);cs->ours_since_event=true;
}
void dlss5_note_submit(VkQueue queue,const std::vector<VkCommandBuffer>& cmds,bool success){
  if(!success)return;std::lock_guard<std::mutex> lock(mutex);if(!device)return;
  for(auto cmd:cmds)for(auto& s:slots)if(s.cmd==cmd&&s.recording&&s.recording==cmd_recording(cmd)){
    s.submitted=true;s.layout.submitted(true);if(history_recording==s.recording && history_cmd==cmd)history_submitted=true;s.submitted_frame=frame_index();s.queue=queue;
    if(device&&queue!=device->graphics_queue)fail("NR submitted on another queue (session stopped)");}
}
void dlss5_on_present(){
  if(!cfg_int("dlss5_nr",0))return;std::lock_guard<std::mutex> lock(mutex);config_state();if(!device)return;
  for(auto& s:slots)harvest(s);
  if(!last_bypass && !failed && frame_index()>last_recorded_frame+120)state("no usable pre-post RR evaluation in recent frames");
  const auto now=frame_index();if(now-stats_start<stats_period(120))return;stats_start=now;
  if(measurements){log_stats("[dlss5] gpu NR: %.3f ms/frame (codec+guides+evaluate), evaluate %.3f, encode+guides %.3f, decode %.3f ms/frame; frames %u, bypass %d",
    sums[3]/measurements,sums[0]/measurements,sums[1]/measurements,sums[2]/measurements,measurements,last_bypass);}
  std::fill(std::begin(sums),std::end(sums),0.0);measurements=0;
}
void dlss5_device_destroy(DeviceData* dd){
  std::lock_guard<std::mutex> lock(mutex);enabled_devices.erase(dd->device);if(device!=dd)return;retired_owner=true;
  const auto r=dd->vk.DeviceWaitIdle(dd->device);log("[dlss5] device drain %d; release feature/resources on existing owner (no NGX shutdown)",int(r));
  for(auto& s:slots)slot_destroy(s);feature.reset();pipelines.reset();device=nullptr;history=false;capabilities_checked=false;need_reset=true;
}

VkResult dlss5_create_device(InstanceData* id,PFN_vkCreateDevice create,VkPhysicalDevice physical,const VkDeviceCreateInfo* ci,const VkAllocationCallbacks* alloc,VkDevice* out){
  if(!cfg_int("dlss5_nr",0))return create(physical,ci,alloc,out);
  if(retired_owner.load()){log("[dlss5] NR disabled after device recreation; retained owner requires process restart");return create(physical,ci,alloc,out);}
  if(!id||!id->GetPhysicalDeviceFeatures2||!id->EnumerateDeviceExtensionProperties)return create(physical,ci,alloc,out);
  uint32_t count{};id->EnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr);std::vector<VkExtensionProperties> available(count);
  id->EnumerateDeviceExtensionProperties(physical,nullptr,&count,available.data());
  std::vector<const char*> extensions(ci->ppEnabledExtensionNames,ci->ppEnabledExtensionNames+ci->enabledExtensionCount);
  bool ext_bda=false;for(auto name:extensions)ext_bda|=!std::strcmp(name,"VK_EXT_buffer_device_address");
  const char* needed[]{"VK_NVX_binary_import","VK_NVX_image_view_handle","VK_KHR_push_descriptor","VK_KHR_buffer_device_address"};
  for(auto name:needed){if(ext_bda && !std::strcmp(name,"VK_KHR_buffer_device_address"))continue;bool found=false;for(const auto& e:available)found|=!std::strcmp(e.extensionName,name);
    if(!found){log("[dlss5] NR device extension unavailable: %s; native frame preserved",name);return create(physical,ci,alloc,out);}
    bool enabled=false;for(auto e:extensions)enabled|=!std::strcmp(e,name);if(!enabled)extensions.push_back(name);}
  VkPhysicalDeviceBufferDeviceAddressFeatures support_bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
  VkPhysicalDeviceBufferDeviceAddressFeaturesEXT support_ext{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT};
  support_ext.pNext=&support_bda;
  VkPhysicalDeviceFeatures2 support{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,ext_bda?static_cast<void*>(&support_ext):static_cast<void*>(&support_bda)};id->GetPhysicalDeviceFeatures2(physical,&support);
  if(!support_bda.bufferDeviceAddress || (ext_bda&&!support_ext.bufferDeviceAddress) || !support.features.shaderStorageImageExtendedFormats || !support.features.shaderStorageImageReadWithoutFormat || !support.features.shaderStorageImageWriteWithoutFormat){log("[dlss5] NR device features unavailable");return create(physical,ci,alloc,out);}
  // Match the existing RT feature patch convention: change only these known
  // booleans for the synchronous vkCreateDevice call, then restore caller data.
  struct Edits{std::vector<std::pair<VkBool32*,VkBool32>> fields;
    void set(VkBool32& f){fields.emplace_back(&f,f);f=VK_TRUE;}
    void restore(){for(auto& [p,v]:fields)*p=v;fields.clear();}
    ~Edits(){restore();}} edits;
  VkDeviceCreateInfo patched=*ci;patched.ppEnabledExtensionNames=extensions.data();patched.enabledExtensionCount=uint32_t(extensions.size());
  VkPhysicalDeviceFeatures core=ci->pEnabledFeatures?*ci->pEnabledFeatures:VkPhysicalDeviceFeatures{};bool features2=false,bda=false,ext_chained=false,vulkan12=false;
  for(auto p=static_cast<const VkBaseInStructure*>(ci->pNext);p;p=p->pNext){
    if(p->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2){auto* f=const_cast<VkPhysicalDeviceFeatures2*>(reinterpret_cast<const VkPhysicalDeviceFeatures2*>(p));edits.set(f->features.shaderStorageImageExtendedFormats);edits.set(f->features.shaderStorageImageReadWithoutFormat);edits.set(f->features.shaderStorageImageWriteWithoutFormat);features2=true;}
    if(p->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES){auto* f=const_cast<VkPhysicalDeviceVulkan12Features*>(reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(p));if(!ext_bda)edits.set(f->bufferDeviceAddress);vulkan12=true;}
    if(p->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES){auto* f=const_cast<VkPhysicalDeviceBufferDeviceAddressFeatures*>(reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(p));edits.set(f->bufferDeviceAddress);bda=true;}
    if(ext_bda && p->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT){auto* f=const_cast<VkPhysicalDeviceBufferDeviceAddressFeaturesEXT*>(reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeaturesEXT*>(p));edits.set(f->bufferDeviceAddress);ext_chained=true;}
  }
  if(!features2){core.shaderStorageImageExtendedFormats=core.shaderStorageImageReadWithoutFormat=core.shaderStorageImageWriteWithoutFormat=VK_TRUE;patched.pEnabledFeatures=&core;}
  const auto plan=dlss5_frame::bda_plan(ext_bda,ext_chained,bda,vulkan12);
  if(!plan.valid){log("[dlss5] NR optional feature setup refused: EXT buffer address plus Vulkan12 chain");edits.restore();return create(physical,ci,alloc,out);}
  VkPhysicalDeviceBufferDeviceAddressFeaturesEXT add_ext{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT};
  if(plan.add_ext){add_ext.bufferDeviceAddress=VK_TRUE;add_ext.pNext=const_cast<void*>(patched.pNext);patched.pNext=&add_ext;}
  VkPhysicalDeviceBufferDeviceAddressFeatures add{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
  // Same core-feature promotion as rtscene.cpp:714: the EXT extension can
  // coexist with this standalone core feature on API 1.2, unlike VkVulkan12Features.
  if(plan.add_core){add.bufferDeviceAddress=VK_TRUE;add.pNext=const_cast<void*>(patched.pNext);patched.pNext=&add;}
  log("[dlss5] experimental native Vulkan NR device requirements enabled (no interop/split)");
  auto result=create(physical,&patched,alloc,out);edits.restore();
  if(result!=VK_SUCCESS){log("[dlss5] optional NR device setup failed (%d), retry ordinary device",int(result));return create(physical,ci,alloc,out);}
  if(result==VK_SUCCESS){std::lock_guard<std::mutex> lock(mutex);enabled_devices.insert(*out);}
  return result;
}
} // namespace bg3
