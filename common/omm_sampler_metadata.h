#pragma once
#include <vulkan/vulkan_core.h>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <unordered_map>
namespace bg3::omm {
struct SamplerMetadata {
  uint64_t id{};
  VkSampler sampler{};
  VkSamplerCreateInfo info{};
  VkSamplerReductionMode reduction{VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE};
  VkStructureType unsupportedStructure{VK_STRUCTURE_TYPE_MAX_ENUM};
  bool complete{};
};
inline SamplerMetadata sampler_metadata(uint64_t id,VkSampler sampler,const VkSamplerCreateInfo& info){
  SamplerMetadata out{};out.id=id;out.sampler=sampler;out.info=info;out.info.pNext=nullptr;
  out.complete=id && sampler && info.sType==VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  for(float x:{info.mipLodBias,info.minLod,info.maxLod,info.maxAnisotropy})out.complete&=std::isfinite(x);
  out.complete&=info.minLod<=info.maxLod;
  bool reductionSeen=false;
  for(auto* p=static_cast<const VkBaseInStructure*>(info.pNext);p;p=p->pNext){
    if(p->sType==VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO && !reductionSeen){
      reductionSeen=true;out.reduction=reinterpret_cast<const VkSamplerReductionModeCreateInfo*>(p)->reductionMode;
    }else{out.complete=false;out.unsupportedStructure=p->sType;break;}
  }
  return out;
}
// Only the address/filter topology used by ordinary RT alpha. LOD range/bias,
// view support, descriptor immutability and numeric proof remain factory inputs.
inline bool repeat_linear_sampler(const SamplerMetadata& s){
  const auto& c=s.info;
  return s.complete && c.flags==0 && !c.compareEnable && !c.anisotropyEnable && !c.unnormalizedCoordinates &&
    s.reduction==VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE &&
    c.magFilter==VK_FILTER_LINEAR && c.minFilter==VK_FILTER_LINEAR && c.mipmapMode==VK_SAMPLER_MIPMAP_MODE_LINEAR &&
    c.addressModeU==VK_SAMPLER_ADDRESS_MODE_REPEAT && c.addressModeV==VK_SAMPLER_ADDRESS_MODE_REPEAT && c.addressModeW==VK_SAMPLER_ADDRESS_MODE_REPEAT;
}
struct SamplerRegistry {
  uint64_t nextId{1};std::unordered_map<uint64_t,SamplerMetadata> entries;
  bool create(uint64_t key,VkSampler sampler,const VkSamplerCreateInfo& info){
    entries.erase(key);if(!key || nextId==UINT64_MAX)return false;
    entries.emplace(key,sampler_metadata(nextId++,sampler,info));return true;
  }
  void destroy(uint64_t key){entries.erase(key);}
  const SamplerMetadata* lookup(uint64_t key)const{auto i=entries.find(key);return i==entries.end()?nullptr:&i->second;}
};
}
