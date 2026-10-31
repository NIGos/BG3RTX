#pragma once
#include "native_hit_inputs.h"
namespace bg3::native_hit {
inline constexpr uint64_t tileset_depth_vs=0x689836dece58e778ull,tileset_depth_fs=0x781cd8d1d600117dull;
inline constexpr uint64_t tileset_deferred_vs=0xa618ddae98c2d1cfull,tileset_deferred_fs=0xcad41eff979970e4ull;
inline bool tileset_depth(uint64_t vs,uint64_t fs){return vs==tileset_depth_vs && fs==tileset_depth_fs;}
inline bool tileset_deferred(uint64_t vs,uint64_t fs){return vs==tileset_deferred_vs && fs==tileset_deferred_fs;}
inline bool tileset_material_enabled(bool startup,bool active){return startup && active;}
struct TilesetInputs {std::array<InputAttribute,6> attributes{};bool valid{};};
// Exact active position, three instance rows, COLOR and UV. Quaternion location6
// belongs only to deferred shading; its actual output frame is captured by XFB.
inline TilesetInputs tileset_inputs(const VkGraphicsPipelineCreateInfo& ci){
 TilesetInputs out;const auto* v=ci.pVertexInputState;if(!v)return out;
 bool dynamic=false;if(ci.pDynamicState)for(uint32_t i=0;i<ci.pDynamicState->dynamicStateCount;++i){
  auto state=ci.pDynamicState->pDynamicStates[i];if(state==VK_DYNAMIC_STATE_VERTEX_INPUT_EXT)return out;
  dynamic|=state==VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;
 }
 const VkPipelineVertexInputDivisorStateCreateInfoEXT* divisors=nullptr;
 for(auto* p=static_cast<const VkBaseInStructure*>(v->pNext);p;p=p->pNext){
  if(p->sType!=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT || divisors)return out;
  divisors=reinterpret_cast<const VkPipelineVertexInputDivisorStateCreateInfoEXT*>(p);
  if(divisors->vertexBindingDivisorCount && !divisors->pVertexBindingDivisors)return out;
 }
 for(uint32_t location=0;location<6;++location){
  const VkVertexInputAttributeDescription* a=nullptr;const VkVertexInputBindingDescription* b=nullptr;
  for(uint32_t i=0;i<v->vertexAttributeDescriptionCount;++i)if(v->pVertexAttributeDescriptions[i].location==location){if(a)return out;a=v->pVertexAttributeDescriptions+i;}
  if(!a || a->format==VK_FORMAT_UNDEFINED)return out;
  for(uint32_t i=0;i<v->vertexBindingDescriptionCount;++i)if(v->pVertexBindingDescriptions[i].binding==a->binding){if(b)return out;b=v->pVertexBindingDescriptions+i;}
  const bool instance=location>=1 && location<=3;
  if(!b || b->inputRate!=(instance?VK_VERTEX_INPUT_RATE_INSTANCE:VK_VERTEX_INPUT_RATE_VERTEX))return out;
  if(divisors && instance){bool seen=false;for(uint32_t i=0;i<divisors->vertexBindingDivisorCount;++i)if(divisors->pVertexBindingDivisors[i].binding==b->binding){if(seen || divisors->pVertexBindingDivisors[i].divisor!=1)return out;seen=true;}}
  if(!dynamic && !b->stride)return out;
  out.attributes[location]={a->binding,a->offset,b->stride,a->format,dynamic};
 }
 out.valid=true;return out;
}
inline bool tileset_vertex_count(uint32_t count,uint32_t instances,uint32_t& vertices){
 const uint64_t n=uint64_t(count)*instances;if(!count || count%3 || !instances || n>UINT32_MAX)return false;vertices=uint32_t(n);return true;
}
inline bool tileset_key(const DrawKey& ordinary,const std::array<InputStream,6>& streams,DrawKey& out){
 if(ordinary.words[0]>1 || !ordinary.words[1] || ordinary.words[1]%3 || !ordinary.words[2])return false;
 out=ordinary;for(size_t i=9;i<out.words.size();++i)out.words[i]=0;
 if(out.words[0]==0)out.words[6]=out.words[7]=out.words[8]=0;
 for(uint32_t i=0;i<6;++i){const auto& s=streams[i];if(!s.buffer || !s.stride || !s.format)return false;
  out.words[9+4*i]=s.buffer;out.words[10+4*i]=s.offset;out.words[11+4*i]=s.stride;out.words[12+4*i]=s.format;}
 out.words[33]=0x4e485432u;return true;
}
}
