#pragma once
#include <vulkan/vulkan.h>
#include "native_hit_join.h"
namespace bg3::native_hit {
struct InputAttribute {uint32_t binding{},offset{},stride{};VkFormat format{VK_FORMAT_UNDEFINED};bool dynamic_stride{};};
struct StaticInputs {InputAttribute position,uv;bool valid{};};
inline StaticInputs static_inputs(const VkGraphicsPipelineCreateInfo& ci){
 StaticInputs result{};const auto* vi=ci.pVertexInputState;if(!vi || vi->pNext)return result;
 bool dynamic=false;if(ci.pDynamicState)for(uint32_t i=0;i<ci.pDynamicState->dynamicStateCount;++i){
  if(ci.pDynamicState->pDynamicStates[i]==VK_DYNAMIC_STATE_VERTEX_INPUT_EXT)return result;
  dynamic|=ci.pDynamicState->pDynamicStates[i]==VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;
 }
 for(uint32_t location=0;location<2;++location){
  const VkVertexInputAttributeDescription* attribute=nullptr;const VkVertexInputBindingDescription* binding=nullptr;
  for(uint32_t i=0;i<vi->vertexAttributeDescriptionCount;++i)if(vi->pVertexAttributeDescriptions[i].location==location){if(attribute)return result;attribute=vi->pVertexAttributeDescriptions+i;}
  if(!attribute)return result;
  for(uint32_t i=0;i<vi->vertexBindingDescriptionCount;++i)if(vi->pVertexBindingDescriptions[i].binding==attribute->binding){if(binding)return result;binding=vi->pVertexBindingDescriptions+i;}
  if(!binding || binding->inputRate!=VK_VERTEX_INPUT_RATE_VERTEX)return result;
  auto& out=location?result.uv:result.position;out={binding->binding,attribute->offset,binding->stride,attribute->format,dynamic};
 }
 result.valid=true;return result;
}
struct InputStream {uint64_t buffer{},offset{},stride{};uint32_t format{};};
struct StaticKeyInputs {
 uint64_t engine_buffer{},engine_offset{},pull_buffer{},pull_offset{},pull_range{};
 uint32_t pull_index{};bool pull_hint_known{};InputStream position,uv;
};
inline bool canonical_static_key(const DrawKey& ordinary,const StaticKeyInputs& in,DrawKey& out){
 if(!in.engine_buffer || !in.uv.buffer || !in.uv.stride || !in.uv.format)return false;
 const bool pull=in.pull_hint_known && in.pull_index>0;
 InputStream position=in.position;
 if(pull){
  const uint64_t bytes=uint64_t(in.pull_index-1)*20u;if(!in.pull_buffer || in.pull_offset>UINT64_MAX-bytes || bytes>in.pull_range || 12u>in.pull_range-bytes)return false;
  position={in.pull_buffer,in.pull_offset,20u,uint32_t(VK_FORMAT_R32G32B32_SFLOAT)};
 }else if(!position.buffer || !position.stride || !position.format)return false;
 out=ordinary;for(size_t i=9;i<out.words.size();++i)out.words[i]=0;
 if(out.words[0]==0)out.words[6]=out.words[7]=out.words[8]=0; // Non-indexed input does not use the index binding.
 out.words[9]=in.engine_buffer;out.words[10]=in.engine_offset;out.words[11]=pull?1u:0u;
 out.words[12]=position.buffer;out.words[13]=position.offset;out.words[14]=position.stride;out.words[15]=position.format;
 out.words[16]=in.uv.buffer;out.words[17]=in.uv.offset;out.words[18]=in.uv.stride;out.words[19]=in.uv.format;
 out.words[20]=0x4e484b31u;out.words[21]=pull?in.pull_index:0u;return true;
}
inline bool static_depth_family(uint64_t vs,uint64_t fs){return vs==0x8975ea2744c12a74ull && fs==0xe2ecf65a7810b23bull;}
}
