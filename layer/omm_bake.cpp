#include "host.h"
#include "omm_bake.h"
#include "omm_submit.h"
#include <cstring>
#include <atomic>
#include <memory>
#include <mutex>

namespace bg3::omm {
namespace {
std::atomic<uint64_t> input_bytes{};
constexpr uint64_t input_budget=128ull*1024*1024;
struct Buffer {
  DeviceData* dd{}; VkBuffer handle{}; VkDeviceMemory memory{};
  VkDeviceSize bytes{}, charged{}; VkDeviceAddress address{}; void* mapped{};
  ~Buffer() {
    if (!dd) return;
    if (mapped) dd->vk.UnmapMemory(dd->device,memory);
    if (handle) dd->vk.DestroyBuffer(dd->device,handle,nullptr);
    if (memory) dd->vk.FreeMemory(dd->device,memory,nullptr);
    if (charged) input_bytes.fetch_sub(charged,std::memory_order_relaxed);
  }
};
struct Pipeline {
  DeviceData* dd{}; VkDescriptorSetLayout descriptors{};
  VkPipelineLayout layout{}; VkPipeline handle{}; uint64_t generation{};
  ~Pipeline() {
    if (!dd) return;
    if (handle) dd->vk.DestroyPipeline(dd->device,handle,nullptr);
    if (layout) dd->vk.DestroyPipelineLayout(dd->device,layout,nullptr);
    if (descriptors) dd->vk.DestroyDescriptorSetLayout(dd->device,descriptors,nullptr);
  }
};
struct Inputs {
  Buffer data, triangles, audit;
  std::shared_ptr<Pipeline> pipeline;
  std::shared_ptr<void> sampled;
};
std::mutex bake_mutex;
std::shared_ptr<Pipeline> pipeline;
std::vector<std::shared_ptr<Inputs>> failed_recordings;
bool producer_failed{};

bool allocate(DeviceData* dd, Buffer& out, VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
  out.dd=dd; out.bytes=size;
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  ci.size=size; ci.usage=usage|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  if (dd->vk.CreateBuffer(dd->device,&ci,nullptr,&out.handle)!=VK_SUCCESS) return false;
  VkMemoryRequirements req{}; dd->vk.GetBufferMemoryRequirements(dd->device,out.handle,&req);
  uint64_t used=input_bytes.load(std::memory_order_relaxed);
  for (;;) {
    if (req.size>input_budget || used>input_budget-req.size) return false;
    if (input_bytes.compare_exchange_weak(used,used+req.size,std::memory_order_relaxed)) break;
  }
  out.charged=req.size;
  VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
  flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.pNext=&flags;
  ai.allocationSize=req.size;
  const VkMemoryPropertyFlags properties=host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                            : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  ai.memoryTypeIndex=find_memory_type(dd,req.memoryTypeBits,properties);
  if (ai.memoryTypeIndex==UINT32_MAX || dd->vk.AllocateMemory(dd->device,&ai,nullptr,&out.memory)!=VK_SUCCESS ||
      dd->vk.BindBufferMemory(dd->device,out.handle,out.memory,0)!=VK_SUCCESS) return false;
  out.address=rt_buffer_address(out.handle);
  if (!out.address || out.address%256) return false;
  return !host || dd->vk.MapMemory(dd->device,out.memory,0,size,0,&out.mapped)==VK_SUCCESS;
}
bool ensure_pipeline(DeviceData* dd) {
  Bg3ShaderBlob blob{};
  // Compilation failure must not bake with a different evaluator generation.
  if (!shader_get("rt/omm_bake_plain.hlsl","main","cs_6_6",&blob)) return false;
  if (pipeline && pipeline->dd==dd && pipeline->generation==blob.generation) return true;
  auto fresh=std::make_shared<Pipeline>(); fresh->dd=dd;
  VkDescriptorSetLayoutBinding b[4]{};
  for (uint32_t i=0;i<4;++i) b[i]={i,i==1?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                   1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
  VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR; dci.bindingCount=4; dci.pBindings=b;
  if (dd->vk.CreateDescriptorSetLayout(dd->device,&dci,nullptr,&fresh->descriptors)!=VK_SUCCESS) return false;
  VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(PlainBakePush)};
  VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  lci.setLayoutCount=1;lci.pSetLayouts=&fresh->descriptors;lci.pushConstantRangeCount=1;lci.pPushConstantRanges=&range;
  if (dd->vk.CreatePipelineLayout(dd->device,&lci,nullptr,&fresh->layout)!=VK_SUCCESS) return false;
  VkShaderModuleCreateInfo sci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  sci.codeSize=blob.size_bytes;sci.pCode=blob.code;
  VkShaderModule shader{};
  if (dd->vk.CreateShaderModule(dd->device,&sci,nullptr,&shader)!=VK_SUCCESS) return false;
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader,"main",nullptr};
  pci.layout=fresh->layout;
  const VkResult result=dd->vk.CreateComputePipelines(dd->device,VK_NULL_HANDLE,1,&pci,nullptr,&fresh->handle);
  dd->vk.DestroyShaderModule(dd->device,shader,nullptr);
  if (result!=VK_SUCCESS) return false;
  fresh->generation=blob.generation;
  // Submitted owners retain the exact pipeline/layout until actual completion.
  pipeline=std::move(fresh);
  return true;
}
}

Ticket record_plain(VkCommandBuffer cmd,const PlainInput& in) {
  if (!available() || !cfg_int("rt_omm",0) || !tracked_recording(cmd) || !in.vertices || !in.mask ||
      !in.sampled_owner || !valid_plain_push(in.push) || in.vertex_bytes<in.push.source_bytes ||
      (in.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && in.layout!=VK_IMAGE_LAYOUT_GENERAL)) return 0;
  std::lock_guard lock(bake_mutex);
  DeviceData* dd=device_of(cmd);
  if (producer_failed || !dd || !ensure_pipeline(dd)) return 0;
  VkPhysicalDeviceProperties properties{};
  dd->inst->GetPhysicalDeviceProperties(dd->physical,&properties);
  if (in.push.source_bytes>properties.limits.maxStorageBufferRange ||
      VkDeviceSize(in.push.triangles)*16>properties.limits.maxStorageBufferRange) return 0;
  std::vector<Triangle> triangles;
  if (!level_zero_descriptors(in.push.triangles,triangles)) return 0;
  auto inputs=std::make_shared<Inputs>(); inputs->pipeline=pipeline; inputs->sampled=in.sampled_owner;
  constexpr VkBufferUsageFlags buildInput=VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT;
  if (!allocate(dd,inputs->data,VkDeviceSize(in.push.triangles)*4,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|buildInput,false) ||
      !allocate(dd,inputs->triangles,triangles.size()*sizeof(Triangle),buildInput,true) ||
      !allocate(dd,inputs->audit,VkDeviceSize(in.push.triangles)*16,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,false)) return 0;
  std::memcpy(inputs->triangles.mapped,triangles.data(),triangles.size()*sizeof(Triangle));
  VkMemoryBarrier producer{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  producer.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;producer.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
  dd->vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0,1,&producer,0,nullptr,0,nullptr);
  VkDescriptorBufferInfo buffers[3]{{in.vertices,0,in.push.source_bytes},{inputs->data.handle,0,inputs->data.bytes},
                                  {inputs->audit.handle,0,inputs->audit.bytes}};
  VkDescriptorImageInfo image{};image.imageView=in.mask;image.imageLayout=in.layout;
  VkWriteDescriptorSet writes[4]{};
  for (uint32_t i=0;i<4;++i) {
    writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstBinding=i;writes[i].descriptorCount=1;
    writes[i].descriptorType=i==1?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    if (i==1) writes[i].pImageInfo=&image; else writes[i].pBufferInfo=&buffers[i==0?0:i-1];
  }
  dd->vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline->handle);
  dd->vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline->layout,0,4,writes);
  dd->vk.CmdPushConstants(cmd,pipeline->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(in.push),&in.push);
  dd->vk.CmdDispatch(cmd,(in.push.triangles+63)/64,1,1);
  Input build{};build.descriptors=triangles;build.data=inputs->data.address;build.triangles=inputs->triangles.address;
  build.dataBytes=inputs->data.bytes;build.triangleBytes=inputs->triangles.bytes;
  build.dataUsage=buildInput|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  build.triangleUsage=buildInput|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;build.input_owner=inputs;
  const Ticket ticket=record(cmd,build);
  if (!ticket) {
    // Compute is already recorded. Never free its buffers even if EXT allocation
    // or recording fails. Bound the failure path to ONE orphan, then disable
    // further production; explicit device-idle teardown releases it safely.
    failed_recordings.push_back(std::move(inputs));producer_failed=true;
    log("[omm] producer disabled after EXT record failure; pending inputs retained until device idle");
  }
  return ticket;
}
void bake_device_destroyed(DeviceData* dd) {
  std::lock_guard lock(bake_mutex);
  if ((pipeline && pipeline->dd==dd) || !failed_recordings.empty()) {
    dd->vk.DeviceWaitIdle(dd->device);
    failed_recordings.clear();pipeline.reset();producer_failed=false;
  }
}
}
