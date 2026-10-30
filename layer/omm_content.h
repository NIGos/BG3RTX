#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <span>
namespace bg3 {struct DeviceData;}
namespace bg3::omm {
struct ContentSource {
  VkBuffer buffer{};VkDeviceSize bytes{},imageOffset{};
  uint64_t imageId{};uint32_t slot{};
  uint64_t viewId{};uint32_t baseMipLevel{},levelCount{};
  VkFormat format{VK_FORMAT_UNDEFINED};VkComponentMapping components{};
  // Header uint globalEpoch@0, stickyDisabled@4. Image epoch@imageOffset.
  // Reads MUST be atomic (InterlockedOr(...,0,out)) if upload queues can run.
  // A zero image epoch is uninitialized, not an immutable-content certificate.
};
bool content_device_created(DeviceData*);
void content_device_destroyed(DeviceData*);
void content_image_created(DeviceData*,VkImage,const VkImageCreateInfo&);
void content_image_destroyed(DeviceData*,VkImage);
void content_view_created(DeviceData*,VkImageView,const VkImageViewCreateInfo&);
void content_view_destroyed(DeviceData*,VkImageView);
void content_memory_allocated(DeviceData*,VkDeviceMemory,const VkMemoryAllocateInfo&);
void content_memory_freed(DeviceData*,VkDeviceMemory);
void content_image_bound(DeviceData*,VkImage,VkDeviceMemory,bool simpleBinding);
bool content_source(VkImage,ContentSource&);
// Sampled view proof: image identity alone does NOT certify view mip semantics.
bool content_source_view(VkImageView,ContentSource&);
void content_after_image_write(VkCommandBuffer,VkImage,bool unsupported=false);
// Includes sticky-disable writes, OMM ticket readers and secondary generations. Submit2
// signal stages for these submissions must cover ALL_COMMANDS, not just transfer.
bool content_dispatch_in_submit(std::span<const VkCommandBuffer>);
}
