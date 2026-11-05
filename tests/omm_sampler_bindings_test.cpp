#include "../layer/host.h"
#include "../layer/omm_sampler.h"
#include "../common/descriptor_epoch.h"
#include <cstdio>
#include <cstdlib>
namespace bg3 {
static Tracker tracked;static DeviceData device;static CmdState command;
Tracker& tracker(){return tracked;}
DeviceData* device_of(const void*){return &device;}
CmdState* cmd_state(VkCommandBuffer,bool){return &command;}
static uint64_t resource_epoch=1;
void material_resource_changed() noexcept {++resource_epoch;}
}
using namespace bg3;using namespace bg3::omm;
static unsigned checks;
static void check(bool x){++checks;if(!x){std::fprintf(stderr,"check %u failed\n",checks);std::exit(1);}}
template<class T>static T handle(uintptr_t x){return reinterpret_cast<T>(x);}
int main(){
  auto* dd=device_of(nullptr);auto cmd=handle<VkCommandBuffer>(1);auto sampler=handle<VkSampler>(2);
  auto layout=handle<VkDescriptorSetLayout>(3);auto set=handle<VkDescriptorSet>(4);auto pool=handle<VkDescriptorPool>(5);
  VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};ci.minFilter=ci.magFilter=VK_FILTER_LINEAR;ci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;ci.addressModeU=ci.addressModeV=ci.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;ci.maxLod=16;ci.maxAnisotropy=1;
  sampler_created(dd,sampler,ci);check(!sampler_tracking_active());check(!sampler_identity(sampler));
  sampler_device_created(dd);sampler_created(dd,sampler,ci);const auto originalId=sampler_identity(sampler);check(originalId!=0);
  VkDescriptorSetLayoutBinding binding{7,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,&sampler};
  VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};info.bindingCount=1;info.pBindings=&binding;
  sampler_layout_created(dd,layout,info);
  auto allocate=[&](VkDescriptorSet s,VkDescriptorSetLayout l){std::unique_lock lock(tracker().mu);descriptor_set_allocated(tracker(),s,l);sampler_set_allocated(dd,s,l,pool,true);};
  allocate(set,layout);cmd_state(cmd)->sets[0][0]=set;
  BoundSampler out;auto query=[&]{return bound_sampler(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,7,0,out);};
  check(query());check(out.immutable && out.sampler.id==originalId);check(repeat_linear_sampler(out.sampler));
  auto stable=out;check(immutable_sampler_current(stable));check(ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,7,0));
  const auto originalSetId=out.descriptorSetId,originalLayoutId=out.layoutId;
  check(originalSetId && originalLayoutId);
  sampler_layout_destroyed(dd,layout);check(query());check(out.layoutId==originalLayoutId); // Allocated set retains definition.
  VkSampler ignored=handle<VkSampler>(99);uint64_t ignoredId=999;
  check(immutable_sampler(set,7,0,ignored,ignoredId));check(ignored==sampler && ignoredId==originalId);
  cmd_state(cmd)->push_set[0]=0;check(!query());check(out.status==BoundSampler::Status::UnsupportedPush);cmd_state(cmd)->push_set[0]=UINT32_MAX;
  sampler_destroyed(dd,sampler);check(!query());check(out.status==BoundSampler::Status::DeadSampler);
  check(!immutable_sampler_current(stable));
  sampler_created(dd,sampler,ci);const auto newId=sampler_identity(sampler);check(newId!=originalId);check(!query());
  // A mutable descriptor's raw sampler handle cannot inherit a new generation.
  binding.pImmutableSamplers=nullptr;sampler_layout_created(dd,layout,info);allocate(set,layout);check(!query());
  check(out.descriptorSetId!=originalSetId);
  DescEntry entry{7,0,VK_DESCRIPTOR_TYPE_SAMPLER,VK_NULL_HANDLE,VK_NULL_HANDLE,0,0,VK_IMAGE_LAYOUT_UNDEFINED};entry.sampler=sampler;entry.sampler_id=newId;
  {std::unique_lock lock(tracker().mu);tracker().sets[set][desc_key(7,0)]=entry;}
  check(query());check(!out.immutable && out.sampler.id==newId);
  check(!immutable_sampler_current(out));
  check(ordinary_sampler_current(out));auto mutableStable=out;
  OrdinaryBindingFacts facts{};check(ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,7,0,&facts));
  check(facts.active && facts.command && !facts.push && facts.allocated && facts.supported && facts.layout_complete);
  check(facts.set_id==out.descriptorSetId && facts.layout_id==out.layoutId && facts.flags==0 && facts.count==1);
  auto wrong=out;++wrong.descriptorSetId;check(!ordinary_sampler_current(wrong));wrong=out;++wrong.layoutId;check(!ordinary_sampler_current(wrong));
  wrong=out;++wrong.sampler.id;check(!ordinary_sampler_current(wrong));wrong=out;wrong.bindingFlags=VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;check(!ordinary_sampler_current(wrong));
  cmd_state(cmd)->push_set[0]=0;check(!ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,7,0,&facts));check(facts.push);cmd_state(cmd)->push_set[0]=UINT32_MAX;

  VkDescriptorImageInfo image{sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.dstBinding=7;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;write.pImageInfo=&image;
  sampler_descriptor_ranges(dd,1,&write,0,nullptr);check(query());check(out.writeVersion==1);
  check(!ordinary_sampler_current(mutableStable));check(ordinary_sampler_current(out));auto versionOne=out;
  sampler_template_write(set,7,0,VK_DESCRIPTOR_TYPE_SAMPLER);check(!ordinary_sampler_current(versionOne));check(query());check(ordinary_sampler_current(out));
  sampler_destroyed(dd,sampler);sampler_created(dd,sampler,ci);check(!query());check(out.status==BoundSampler::Status::DeadSampler);
  // Rollovers cannot silently preserve a previous known descriptor.
  allocate(set,layout);entry.sampler_id=sampler_identity(sampler);{std::unique_lock lock(tracker().mu);tracker().sets[set][desc_key(7,0)]=entry;}
  write.descriptorCount=2;sampler_descriptor_ranges(dd,1,&write,0,nullptr);check(!query());check(out.status==BoundSampler::Status::UnsupportedRange);
  allocate(set,layout);{std::unique_lock lock(tracker().mu);tracker().sets[set][desc_key(7,0)]=entry;}
  sampler_template_write(set,7,1,VK_DESCRIPTOR_TYPE_SAMPLER);check(!query());check(out.status==BoundSampler::Status::UnsupportedRange);
  // An unknown source copy invalidates rather than retaining stale destination.
  allocate(set,layout);{std::unique_lock lock(tracker().mu);tracker().sets[set][desc_key(7,0)]=entry;}
  VkCopyDescriptorSet copy{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};copy.srcSet=handle<VkDescriptorSet>(99);copy.srcBinding=7;copy.dstSet=set;copy.dstBinding=7;copy.descriptorCount=1;
  sampler_descriptor_ranges(dd,0,nullptr,1,&copy);check(!query());check(out.status==BoundSampler::Status::UnsupportedRange);
  sampler_sets_freed(dd,1,&set);check(!query());allocate(set,layout);sampler_pool_reset(dd,pool);check(!query());
  // Actual registry/layout policy rejects update-after-bind and unknown layout
  // chains; the ordinary mutable-sampler extension does not relax either.
  VkDescriptorBindingFlags uab=VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
  VkDescriptorSetLayoutBindingFlagsCreateInfo flags{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};flags.bindingCount=1;flags.pBindingFlags=&uab;
  info.pNext=&flags;binding.pImmutableSamplers=nullptr;sampler_layout_created(dd,layout,info);allocate(set,layout);
  {std::unique_lock lock(tracker().mu);tracker().sets[set][desc_key(7,0)]=entry;}
  check(query());check(!ordinary_sampler_current(out));check(!ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,7,0,&facts));check(facts.flags==uab);
  VkBaseInStructure unknown{VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT,nullptr};info.pNext=&unknown;sampler_layout_created(dd,layout,info);allocate(set,layout);
  check(!query());check(out.status==BoundSampler::Status::UnsupportedLayout);check(!ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,0,7,0,&facts));check(!facts.layout_complete);
  info.pNext=nullptr;
  // Unsupported sampler chains stay observable but cannot certify a factory.
  VkSamplerCustomBorderColorCreateInfoEXT custom{VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};ci.pNext=&custom;sampler_created(dd,sampler,ci);binding.pImmutableSamplers=&sampler;sampler_layout_created(dd,layout,info);allocate(set,layout);
  check(!query());check(out.status==BoundSampler::Status::UnsupportedSampler);check(out.sampler.unsupportedStructure==custom.sType);
  // CPU descriptor receipts have their own write epoch. Non-sampler writes
  // must invalidate cached query metadata without changing sampler policy.
  ci.pNext=nullptr;sampler_created(dd,sampler,ci);entry.sampler_id=sampler_identity(sampler);
  VkDescriptorSetLayoutBinding receiptBindings[2]={{7,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
    {8,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
  info.pBindings=receiptBindings;info.bindingCount=2;sampler_layout_created(dd,layout,info);allocate(set,layout);
  {std::unique_lock lock(tracker().mu);tracker().sets[set][desc_key(7,0)]=entry;}
  check(query());
  uint64_t receiptId=0,receiptVersion=0;check(descriptor_receipt(set,8,0,receiptId,receiptVersion));
  uint64_t previousReceipt=receiptVersion;const auto allocatedReceiptId=receiptId;
  check(!descriptor_receipt(set,8,1,receiptId,receiptVersion));check(!descriptor_receipt(set,99,0,receiptId,receiptVersion));
  VkDescriptorBufferInfo bufferInfo{handle<VkBuffer>(123),0,64};
  VkWriteDescriptorSet bufferWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};bufferWrite.dstSet=set;bufferWrite.dstBinding=8;
  bufferWrite.descriptorCount=1;bufferWrite.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;bufferWrite.pBufferInfo=&bufferInfo;
  // A known ordinary source for the actual descriptor-copy callback.
  auto source=handle<VkDescriptorSet>(90);allocate(source,layout);
  {std::unique_lock lock(tracker().mu);tracker().sets[source][desc_key(8,0)]={8,0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_NULL_HANDLE,bufferInfo.buffer,0,64,VK_IMAGE_LAYOUT_UNDEFINED};}
  VkCopyDescriptorSet bufferCopy{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};bufferCopy.srcSet=source;bufferCopy.srcBinding=8;
  bufferCopy.dstSet=set;bufferCopy.dstBinding=8;bufferCopy.descriptorCount=1;
  for(unsigned frame=0;frame<144;++frame){
    const bool changed=frame%16==0;
    if(changed){
      if(frame%48==0)sampler_descriptor_ranges(dd,1,&bufferWrite,0,nullptr);
      else if(frame%48==16)sampler_template_write(set,8,0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
      else sampler_descriptor_ranges(dd,0,nullptr,1,&bufferCopy);
    }
    check(descriptor_receipt(set,8,0,receiptId,receiptVersion));check(receiptId==allocatedReceiptId);
    check(changed?receiptVersion>previousReceipt:receiptVersion==previousReceipt);previousReceipt=receiptVersion;
  }
  // Copies already advanced the original policy version before cpu_opt4, so
  // compare directly around an ordinary write/template to prove no new change.
  check(query());auto beforeNonSampler=out;sampler_descriptor_ranges(dd,1,&bufferWrite,0,nullptr);
  sampler_template_write(set,8,0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);check(query());check(out.writeVersion==beforeNonSampler.writeVersion);
  sampler_sets_freed(dd,1,&set);check(!descriptor_receipt(set,8,0,receiptId,receiptVersion));
  allocate(set,layout);check(descriptor_receipt(set,8,0,receiptId,receiptVersion));check(receiptId!=allocatedReceiptId);
  sampler_pool_reset(dd,pool);check(!descriptor_receipt(set,8,0,receiptId,receiptVersion));
  allocate(set,layout);check(descriptor_receipt(set,8,0,receiptId,receiptVersion));
  const auto beforeDestroy=resource_epoch;sampler_destroyed(dd,sampler);check(resource_epoch>beforeDestroy);
  sampler_device_destroyed(dd);check(!sampler_tracking_active());check(!query());check(!descriptor_receipt(set,8,0,receiptId,receiptVersion));
  std::printf("Bound sampler helper checks %u; fake handles, no Vulkan/device calls\n",checks);
}
