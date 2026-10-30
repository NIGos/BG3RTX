#include "host.h"
#include "omm_content.h"
#include "omm_submit.h"
#include "../common/omm_content_registry.h"
#include <algorithm>
#include <cstring>
#include <atomic>
namespace bg3::omm {
namespace {
std::mutex contentMutex;
DeviceData* owner{};std::atomic<bool> active{};bool recorded{},cpuUnsupported{};
ContentRegistry registry;
std::unordered_map<VkDeviceMemory,bool> memorySafe;
std::unordered_map<VkImage,VkDeviceMemory> imageMemory;
std::unordered_map<uint32_t,VkDescriptorSet> sets;
std::vector<VkQueueFamilyProperties> families;
VkBuffer counters{};VkDeviceMemory counterMemory{};void* mapped{};
VkDeviceSize stride{},bytes{};
VkDescriptorSetLayout setLayout{};VkDescriptorPool pool{};VkPipelineLayout layout{};VkPipeline pipeline{},beginPipeline{};
uint64_t key(VkImage image){return uint64_t(reinterpret_cast<uintptr_t>(image));}
uint64_t view_key(VkImageView view){return uint64_t(reinterpret_cast<uintptr_t>(view));}
void cleanup(){
  if(pipeline)owner->vk.DestroyPipeline(owner->device,pipeline,nullptr);
  if(beginPipeline)owner->vk.DestroyPipeline(owner->device,beginPipeline,nullptr);
  if(layout)owner->vk.DestroyPipelineLayout(owner->device,layout,nullptr);
  if(pool)owner->vk.DestroyDescriptorPool(owner->device,pool,nullptr);
  if(setLayout)owner->vk.DestroyDescriptorSetLayout(owner->device,setLayout,nullptr);
  if(mapped)owner->vk.UnmapMemory(owner->device,counterMemory);
  if(counters)owner->vk.DestroyBuffer(owner->device,counters,nullptr);
  if(counterMemory)owner->vk.FreeMemory(owner->device,counterMemory,nullptr);
  pipeline=beginPipeline=VK_NULL_HANDLE;layout=VK_NULL_HANDLE;pool=VK_NULL_HANDLE;setLayout=VK_NULL_HANDLE;
  counters=VK_NULL_HANDLE;counterMemory=VK_NULL_HANDLE;mapped=nullptr;sets.clear();memorySafe.clear();imageMemory.clear();registry.images.clear();registry.views.clear();
}
uint32_t certificate(VkFormat format){
  switch(format){
    case VK_FORMAT_R8_UNORM:case VK_FORMAT_R8G8_UNORM:case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_B8G8R8A8_UNORM:return 1;
    case VK_FORMAT_R8_SRGB:case VK_FORMAT_R8G8_SRGB:case VK_FORMAT_R8G8B8A8_SRGB:case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC2_UNORM_BLOCK:case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC4_UNORM_BLOCK:case VK_FORMAT_BC5_UNORM_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:case VK_FORMAT_BC7_SRGB_BLOCK:return 2;
    default:return 0;
  }
}
bool policy(const VkImageCreateInfo& c){
  constexpr VkImageUsageFlags allowed=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  return !c.pNext && !c.flags && certificate(c.format)!=0 && c.imageType==VK_IMAGE_TYPE_2D &&
    c.tiling==VK_IMAGE_TILING_OPTIMAL && c.samples==VK_SAMPLE_COUNT_1_BIT &&
    c.extent.width && c.extent.height && c.extent.depth==1 && c.arrayLayers==1 && c.mipLevels &&
    (c.usage&VK_IMAGE_USAGE_SAMPLED_BIT) && !(c.usage&~allowed) && c.initialLayout==VK_IMAGE_LAYOUT_UNDEFINED;
}
void memory_barrier(VkCommandBuffer cmd,VkPipelineStageFlags source,VkAccessFlags write,VkPipelineStageFlags target,VkAccessFlags access){
  VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=write;b.dstAccessMask=access;
  owner->vk.CmdPipelineBarrier(cmd,source,target,0,1,&b,0,nullptr,0,nullptr);
}
VkDescriptorSet descriptor(uint32_t slot){
  if(auto i=sets.find(slot);i!=sets.end())return i->second;
  VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};a.descriptorPool=pool;a.descriptorSetCount=1;a.pSetLayouts=&setLayout;
  VkDescriptorSet set{};if(owner->vk.AllocateDescriptorSets(owner->device,&a,&set)!=VK_SUCCESS)return VK_NULL_HANDLE;
  VkDescriptorBufferInfo info[]={{counters,0,16},{counters,(VkDeviceSize(slot)+1)*stride,4}};
  VkWriteDescriptorSet writes[2]{};for(uint32_t i=0;i<2;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&info[i];}
  owner->vk.UpdateDescriptorSets(owner->device,2,writes,0,nullptr);sets.emplace(slot,set);return set;
}
}
bool content_device_created(DeviceData* dd){
  std::lock_guard lock(contentMutex);if(owner)return active.load();owner=dd;recorded=false;cpuUnsupported=false;
  for(const auto& extension:dd->extensions)if(extension==VK_KHR_DEVICE_ADDRESS_COMMANDS_EXTENSION_NAME || extension==VK_NV_DEDICATED_ALLOCATION_IMAGE_ALIASING_EXTENSION_NAME){cleanup();return false;}
  VkPhysicalDeviceProperties props{};dd->inst->GetPhysicalDeviceProperties(dd->physical,&props);
  stride=std::max<VkDeviceSize>(16,props.limits.minStorageBufferOffsetAlignment);
  bytes=(VkDeviceSize(registry.capacity)+1)*stride;
  if(bytes>64ull*1024*1024 || bytes>props.limits.maxStorageBufferRange){cleanup();return false;}
  uint32_t n{};dd->inst->GetPhysicalDeviceQueueFamilyProperties(dd->physical,&n,nullptr);families.resize(n);dd->inst->GetPhysicalDeviceQueueFamilyProperties(dd->physical,&n,families.data());
  std::vector<uint32_t> indices;for(uint32_t i=0;i<n;++i)indices.push_back(i);
  VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=bytes;b.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  if(n>1){b.sharingMode=VK_SHARING_MODE_CONCURRENT;b.queueFamilyIndexCount=n;b.pQueueFamilyIndices=indices.data();}
  if(dd->vk.CreateBuffer(dd->device,&b,nullptr,&counters)!=VK_SUCCESS){cleanup();return false;}
  VkMemoryRequirements req{};dd->vk.GetBufferMemoryRequirements(dd->device,counters,&req);
  VkPhysicalDeviceMemoryProperties mem{};dd->inst->GetPhysicalDeviceMemoryProperties(dd->physical,&mem);
  uint32_t type=UINT32_MAX;for(uint32_t i=0;i<mem.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i)) && (mem.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}
  VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;a.memoryTypeIndex=type;
  if(type==UINT32_MAX || dd->vk.AllocateMemory(dd->device,&a,nullptr,&counterMemory)!=VK_SUCCESS ||
     dd->vk.BindBufferMemory(dd->device,counters,counterMemory,0)!=VK_SUCCESS || dd->vk.MapMemory(dd->device,counterMemory,0,bytes,0,&mapped)!=VK_SUCCESS){cleanup();return false;}
  // The only CPU writes: coherent initialization BEFORE any submitted GPU use.
  std::memset(mapped,0,size_t(bytes));
  VkDescriptorSetLayoutBinding bindings[]={{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
  VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sci.bindingCount=2;sci.pBindings=bindings;
  if(dd->vk.CreateDescriptorSetLayout(dd->device,&sci,nullptr,&setLayout)!=VK_SUCCESS){cleanup();return false;}
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,registry.capacity*2};VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pci.maxSets=registry.capacity;pci.poolSizeCount=1;pci.pPoolSizes=&size;
  if(dd->vk.CreateDescriptorPool(dd->device,&pci,nullptr,&pool)!=VK_SUCCESS){cleanup();return false;}
  VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};l.setLayoutCount=1;l.pSetLayouts=&setLayout;
  if(dd->vk.CreatePipelineLayout(dd->device,&l,nullptr,&layout)!=VK_SUCCESS){cleanup();return false;}
  Bg3ShaderBlob blob{};if(!shader_get("rt/omm_content_epoch.hlsl","main","cs_6_6",&blob)){cleanup();return false;}
  VkShaderModuleCreateInfo s{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};s.codeSize=blob.size_bytes;s.pCode=blob.code;VkShaderModule shader{};
  if(dd->vk.CreateShaderModule(dd->device,&s,nullptr,&shader)!=VK_SUCCESS){cleanup();return false;}
  VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};p.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader,"main",nullptr};p.layout=layout;
  const auto result=dd->vk.CreateComputePipelines(dd->device,VK_NULL_HANDLE,1,&p,nullptr,&pipeline);
  const uint32_t begin=1;VkSpecializationMapEntry entry{0,0,4};VkSpecializationInfo spec{1,&entry,4,&begin};p.stage.pSpecializationInfo=&spec;
  const auto beginResult=result==VK_SUCCESS?dd->vk.CreateComputePipelines(dd->device,VK_NULL_HANDLE,1,&p,nullptr,&beginPipeline):result;
  dd->vk.DestroyShaderModule(dd->device,shader,nullptr);
  if(result!=VK_SUCCESS || beginResult!=VK_SUCCESS){cleanup();return false;}
  active.store(true,std::memory_order_release);log("[omm content] GPU epochs active slots=%u stride=%llu bytes=%llu",registry.capacity,stride,bytes);return true;
}
void content_device_destroyed(DeviceData* dd){
  std::lock_guard lock(contentMutex);if(owner!=dd)return;active.store(false,std::memory_order_release);
  if(recorded)dd->vk.DeviceWaitIdle(dd->device);
  cleanup();owner=nullptr;families.clear();
}
void content_image_created(DeviceData* dd,VkImage image,const VkImageCreateInfo& info){if(!active.load() || dd!=owner)return;std::lock_guard lock(contentMutex);registry.create(key(image),policy(info),info.mipLevels,uint32_t(info.format),certificate(info.format));}
void content_image_destroyed(DeviceData* dd,VkImage image){if(!active.load() || dd!=owner)return;std::lock_guard lock(contentMutex);registry.destroy(key(image));imageMemory.erase(image);}
void content_view_created(DeviceData* dd,VkImageView view,const VkImageViewCreateInfo& info){
  if(!active.load() || dd!=owner)return;std::lock_guard lock(contentMutex);
  const bool simple=!info.pNext && !info.flags && info.viewType==VK_IMAGE_VIEW_TYPE_2D &&
    info.subresourceRange.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT && info.subresourceRange.baseArrayLayer==0 &&
    (info.subresourceRange.layerCount==1 || info.subresourceRange.layerCount==VK_REMAINING_ARRAY_LAYERS);
  registry.create_view(view_key(view),key(info.image),simple,info.subresourceRange.baseMipLevel,
    info.subresourceRange.levelCount,uint32_t(info.format),{uint32_t(info.components.r),uint32_t(info.components.g),uint32_t(info.components.b),uint32_t(info.components.a)});
}
void content_view_destroyed(DeviceData* dd,VkImageView view){if(!active.load() || dd!=owner)return;std::lock_guard lock(contentMutex);registry.destroy_view(view_key(view));}
void content_memory_allocated(DeviceData* dd,VkDeviceMemory memory,const VkMemoryAllocateInfo& info){
  if(!active.load() || dd!=owner)return;std::lock_guard lock(contentMutex);
  VkPhysicalDeviceMemoryProperties m{};dd->inst->GetPhysicalDeviceMemoryProperties(dd->physical,&m);
  bool safe=info.memoryTypeIndex<m.memoryTypeCount && !(m.memoryTypes[info.memoryTypeIndex].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  for(auto* p=static_cast<const VkBaseInStructure*>(info.pNext);p;p=p->pNext)
    if(p->sType!=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO && p->sType!=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO)safe=false;
  memorySafe[memory]=safe;
}
void content_memory_freed(DeviceData* dd,VkDeviceMemory memory){if(!active.load() || dd!=owner)return;std::lock_guard lock(contentMutex);memorySafe.erase(memory);for(const auto& [image,bound]:imageMemory)if(bound==memory)registry.bind(key(image),false);}
void content_image_bound(DeviceData* dd,VkImage image,VkDeviceMemory memory,bool simple){
  if(!active.load() || dd!=owner)return;std::lock_guard lock(contentMutex);auto m=memorySafe.find(memory);registry.bind(key(image),simple && m!=memorySafe.end() && m->second);imageMemory[image]=memory;
}
bool content_source(VkImage image,ContentSource& out){
  out={};if(!active.load())return false;std::lock_guard lock(contentMutex);const auto* e=registry.lookup(key(image));
  if(cpuUnsupported || !e || !e->boundSafe)return false;
  out={counters,bytes,(VkDeviceSize(e->slot)+1)*stride,e->imageId,e->slot};out.requiredSamplingCertificate=e->requiredSamplingCertificate;return true;
}
bool content_source_view(VkImageView view,ContentSource& out){
  out={};if(!active.load())return false;std::lock_guard lock(contentMutex);const auto* v=registry.lookup_view(view_key(view));
  if(cpuUnsupported || !v)return false;const auto* image=registry.lookup(v->image);if(!image || !image->boundSafe)return false;
  out={counters,bytes,(VkDeviceSize(image->slot)+1)*stride,image->imageId,image->slot,v->viewId,v->baseMip,v->levelCount,VkFormat(v->format),
    {VkComponentSwizzle(v->components[0]),VkComponentSwizzle(v->components[1]),VkComponentSwizzle(v->components[2]),VkComponentSwizzle(v->components[3])}};
  out.requiredSamplingCertificate=image->requiredSamplingCertificate;
  return true;
}
bool content_before_image_write(VkCommandBuffer cmd,VkImage image,bool unsupported){
  if(!active.load() || device_of(cmd)!=owner)return false;std::lock_guard lock(contentMutex);const auto* e=registry.lookup(key(image));if(!e || !e->boundSafe)return false;
  uint32_t family{};CmdState* state=cmd_state(cmd);
  const bool tracked=mark_content_dispatch(cmd);
  if(!tracked)cpuUnsupported=true;
  const bool compute=!unsupported && tracked && command_queue_family(cmd,family) && family<families.size() &&
    (families[family].queueFlags&VK_QUEUE_COMPUTE_BIT) && state && state->push_set[1]==UINT32_MAX;
  const VkDescriptorSet set=compute?descriptor(e->slot):VK_NULL_HANDLE;
  recorded=true;
  if(!set){
    memory_barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    owner->vk.CmdFillBuffer(cmd,counters,4,4,1); // Sticky; CPU never clears this word.
    memory_barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
    return false;
  }
  memory_barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  owner->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,beginPipeline);
  owner->vk.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
  owner->vk.CmdDispatch(cmd,1,1,1);
  memory_barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
  restore_bindings(cmd,VK_PIPELINE_BIND_POINT_COMPUTE);
  return true;
}
void content_after_image_write(VkCommandBuffer cmd,VkImage image,bool began){
  if(!began || !active.load() || device_of(cmd)!=owner)return;
  std::lock_guard lock(contentMutex);const auto* e=registry.lookup(key(image));
  if(!e || !e->boundSafe)return; // Missing completion keeps active!=0, never certifies.
  auto i=sets.find(e->slot);if(i==sets.end())return;
  memory_barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  owner->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
  owner->vk.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&i->second,0,nullptr);
  owner->vk.CmdDispatch(cmd,1,1,1);
  memory_barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
  restore_bindings(cmd,VK_PIPELINE_BIND_POINT_COMPUTE);
}
}
