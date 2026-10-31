#pragma once
#include "../common/omm_sampler_metadata.h"
namespace bg3 {struct DeviceData;}
namespace bg3::omm {
void sampler_device_created(DeviceData*);
void sampler_device_destroyed(DeviceData*);
bool sampler_tracking_active();
uint64_t sampler_identity(VkSampler);
void sampler_created(DeviceData*,VkSampler,const VkSamplerCreateInfo&);
void sampler_destroyed(DeviceData*,VkSampler);
void sampler_layout_created(DeviceData*,VkDescriptorSetLayout,const VkDescriptorSetLayoutCreateInfo&);
void sampler_layout_destroyed(DeviceData*,VkDescriptorSetLayout);
// Called under tracker().mu after descriptor_set_allocated; seeds immutable
// sampler entries into the existing descriptor tracker (no parallel decoder).
void sampler_set_allocated(DeviceData*,VkDescriptorSet,VkDescriptorSetLayout,VkDescriptorPool,bool simpleAllocation);
void sampler_sets_freed(DeviceData*,uint32_t,const VkDescriptorSet*);
void sampler_pool_reset(DeviceData*,VkDescriptorPool);
void sampler_descriptor_ranges(DeviceData*,uint32_t,const VkWriteDescriptorSet*,uint32_t,const VkCopyDescriptorSet*);
void sampler_template_write(VkDescriptorSet,uint32_t binding,uint32_t element,VkDescriptorType);
bool immutable_sampler(VkDescriptorSet,uint32_t binding,uint32_t element,VkSampler&,uint64_t& id);
struct BoundSampler {
  SamplerMetadata sampler;
  uint64_t descriptorSetId{},layoutId{},writeVersion{};
  VkDescriptorSet descriptorSet{};uint32_t binding{},element{};
  VkDescriptorBindingFlags bindingFlags{};
  bool immutable{};
  enum class Status {UnknownBinding,CompleteMetadata,UnsupportedSampler,UnsupportedLayout,UnsupportedPush,UnsupportedRange,DeadSampler} status{Status::UnknownBinding};
};
// Current CPU binding metadata, not a frozen GPU descriptor certificate. Mutable
// descriptors require caller pinning/revalidation; unknown/push bindings decline.
bool bound_sampler(VkCommandBuffer,VkPipelineBindPoint,uint32_t set,uint32_t binding,uint32_t element,BoundSampler&);
bool immutable_sampler_current(const BoundSampler&);
// Native hit capture also accepts ordinary mutable sampler descriptors: their
// exact set/layout/version and live sampler generation must remain unchanged.
bool ordinary_sampler_current(const BoundSampler&);
struct OrdinaryBindingFacts {
 bool active{},command{},push{},allocated{},supported{},layout_complete{};
 uint64_t set_id{},layout_id{},version{};VkDescriptorSet set{};
 VkDescriptorType type{VK_DESCRIPTOR_TYPE_MAX_ENUM};VkDescriptorBindingFlags flags{};uint32_t count{};
};
// Standard descriptor lifetime rules freeze this used binding through execution;
// update-after-bind, push, unknown layouts and variable allocation decline.
bool ordinary_frozen_binding(VkCommandBuffer,VkPipelineBindPoint,uint32_t set,uint32_t binding,uint32_t element,OrdinaryBindingFacts* facts=nullptr);
}
