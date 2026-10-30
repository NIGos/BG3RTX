#pragma once
// Passive F10 metadata, not a GPU execution/content/residency certificate.
#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>
#include <string>
namespace bg3::trace_view {
inline uint64_t allocate_id(std::atomic<uint64_t>& next){
 uint64_t id=next.load(std::memory_order_relaxed);
 while(id && id!=UINT64_MAX){if(next.compare_exchange_weak(id,id+1,std::memory_order_relaxed))return id;}
 return 0; // Saturated identity is explicitly unavailable; never wrap/reuse.
}
struct Creation {
 uint64_t id{};uint32_t image_id{};
 VkImageViewType type{VK_IMAGE_VIEW_TYPE_MAX_ENUM};VkComponentMapping components{};
 VkImageViewCreateFlags flags{};VkStructureType extension{VK_STRUCTURE_TYPE_MAX_ENUM};
 bool known{};
};
inline Creation capture(const VkImageViewCreateInfo& info,uint64_t id,uint32_t imageId){
 Creation c;c.id=id;c.image_id=imageId;c.type=info.viewType;c.components=info.components;c.flags=info.flags;c.known=true;
 if(info.pNext)c.extension=static_cast<const VkBaseInStructure*>(info.pNext)->sType;
 return c;
}
inline bool resolve_count(uint32_t base,uint32_t count,uint32_t total,uint32_t& resolved){
 resolved=0;if(base>=total)return false;
 resolved=count==UINT32_MAX?total-base:count;
 if(!resolved || resolved>total-base){resolved=0;return false;}return true;
}
inline std::string json(VkFormat format,VkImageSubresourceRange range,const Creation& c,
 uint32_t currentImageId,const VkImageCreateInfo* image,VkDescriptorType descriptor,VkImageLayout layout){
 uint32_t mips=0,layers=0;
 const bool rangeOk=image && resolve_count(range.baseMipLevel,range.levelCount,image->mipLevels,mips) &&
   resolve_count(range.baseArrayLayer,range.layerCount,image->arrayLayers,layers);
 const char* status=!c.known?"unavailable_create_metadata":!c.id?"identity_saturated":
   !image || !currentImageId || !c.image_id?"unavailable_image":c.image_id!=currentImageId?"stale_image_identity":
   c.extension!=VK_STRUCTURE_TYPE_MAX_ENUM?"unsupported_extensions":!rangeOk?"invalid_range":"known";
 auto n=[](auto value){return std::to_string(uint64_t(value));};
 std::string s="{\"status\":\""+std::string(status)+"\",\"view_id\":"+n(c.id)+",\"image_id\":"+n(c.image_id)+
  ",\"current_image_id\":"+n(currentImageId)+",\"format\":"+n(format)+",\"type\":"+n(c.type)+
  ",\"components\":["+n(c.components.r)+","+n(c.components.g)+","+n(c.components.b)+","+n(c.components.a)+
  "],\"flags\":"+n(c.flags)+",\"extension_first\":"+(c.extension==VK_STRUCTURE_TYPE_MAX_ENUM?"null":n(c.extension))+
  ",\"descriptor_type\":"+n(descriptor)+",\"descriptor_layout\":"+n(layout)+
  ",\"range\":{\"aspect\":"+n(range.aspectMask)+",\"base_mip\":"+n(range.baseMipLevel)+",\"mips\":"+n(range.levelCount)+
  ",\"base_layer\":"+n(range.baseArrayLayer)+",\"layers\":"+n(range.layerCount)+",\"resolved_mips\":"+n(mips)+",\"resolved_layers\":"+n(layers)+"}";
 if(image)s+=",\"image_format\":"+n(image->format)+",\"image_flags\":"+n(image->flags)+",\"image_mips\":"+n(image->mipLevels)+",\"image_layers\":"+n(image->arrayLayers);
 return s+"}";
}
}
