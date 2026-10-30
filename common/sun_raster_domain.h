#pragma once
#include <vulkan/vulkan.h>

namespace bg3::sun_coverage {
// An affine shader matrix does not establish finite clipping: Vulkan raster
// state can clamp or disable it. Unknown dynamic values withhold the witness.
inline bool finite_native_clip(const VkGraphicsPipelineCreateInfo& ci) {
  if (!ci.pRasterizationState || ci.pRasterizationState->depthClampEnable) return false;
  for (auto* n=static_cast<const VkBaseInStructure*>(ci.pRasterizationState->pNext); n; n=n->pNext)
    if (n->sType==VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT &&
        !reinterpret_cast<const VkPipelineRasterizationDepthClipStateCreateInfoEXT*>(n)->depthClipEnable) return false;
  if (ci.pViewportState)
    for (auto* n=static_cast<const VkBaseInStructure*>(ci.pViewportState->pNext); n; n=n->pNext)
      if (n->sType==VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT &&
          reinterpret_cast<const VkPipelineViewportDepthClipControlCreateInfoEXT*>(n)->negativeOneToOne) return false;
  if (ci.pDynamicState)
    for (uint32_t i=0;i<ci.pDynamicState->dynamicStateCount;++i) {
      const auto s=ci.pDynamicState->pDynamicStates[i];
      if (s==VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT || s==VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT ||
          s==VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT) return false;
    }
  return true;
}
}
