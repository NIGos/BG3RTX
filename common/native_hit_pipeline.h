#pragma once
#include "sun_raster_domain.h"
namespace bg3::native_hit {
enum MaterialStateReason : uint32_t {
 MaterialRasterMissing=1,MaterialRasterDiscard=2,MaterialRasterExtension=4,
 MaterialClipDomain=8,MaterialMultisample=16,MaterialBlendState=32,MaterialDynamicState=64
};
// This is only the fixed-state half of the audited material pipeline contract.
// Unknown/duplicate extension structures cannot establish a supported state.
// Reuse finite_native_clip's depthClamp/depthClip/dynamic semantics; that helper
// intentionally ignores unknown pNexts, so it is not itself a full whitelist.
inline uint32_t material_state_reasons(const VkGraphicsPipelineCreateInfo& ci){
 uint32_t reasons=0;const auto* raster=ci.pRasterizationState;
 if(!raster)reasons|=MaterialRasterMissing;
 else{
  if(raster->rasterizerDiscardEnable)reasons|=MaterialRasterDiscard;
  bool seen=false;
  for(auto* next=static_cast<const VkBaseInStructure*>(raster->pNext);next;next=next->pNext){
   if(next->sType!=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT || seen){reasons|=MaterialRasterExtension;break;}
   seen=true;const auto& clip=*reinterpret_cast<const VkPipelineRasterizationDepthClipStateCreateInfoEXT*>(next);
   if(clip.flags)reasons|=MaterialRasterExtension;
  }
 }
 if(!sun_coverage::finite_native_clip(ci))reasons|=MaterialClipDomain;
 const auto* ms=ci.pMultisampleState;
 if(!ms || ms->pNext || ms->rasterizationSamples!=VK_SAMPLE_COUNT_1_BIT || ms->alphaToCoverageEnable)reasons|=MaterialMultisample;
 if(!ci.pColorBlendState || ci.pColorBlendState->pNext)reasons|=MaterialBlendState;
 return reasons;
}
}
