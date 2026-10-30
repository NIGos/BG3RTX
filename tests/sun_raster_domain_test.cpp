#include "../common/sun_raster_domain.h"
#include <cstdio>
#include <initializer_list>
#include <cstdlib>
#define CHECK(x) do { ++checks; if (!(x)) {std::fprintf(stderr,"failed line %d\n",__LINE__);std::exit(1);} } while(0)
int main() {
  using bg3::sun_coverage::finite_native_clip;
  unsigned checks=0;
  VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  CHECK(!finite_native_clip(ci));
  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  ci.pRasterizationState=&raster; CHECK(finite_native_clip(ci));
  raster.depthClampEnable=VK_TRUE; CHECK(!finite_native_clip(ci)); raster.depthClampEnable=VK_FALSE;
  VkPipelineRasterizationDepthClipStateCreateInfoEXT clip{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT};
  raster.pNext=&clip; CHECK(!finite_native_clip(ci)); clip.depthClipEnable=VK_TRUE; CHECK(finite_native_clip(ci));
  VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  VkPipelineViewportDepthClipControlCreateInfoEXT control{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT};
  viewport.pNext=&control;ci.pViewportState=&viewport;CHECK(finite_native_clip(ci));
  control.negativeOneToOne=VK_TRUE;CHECK(!finite_native_clip(ci));control.negativeOneToOne=VK_FALSE;
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  ci.pDynamicState=&dynamic; dynamic.dynamicStateCount=1;
  for(auto state:{VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT,VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT,VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT}) {
    dynamic.pDynamicStates=&state;CHECK(!finite_native_clip(ci));
  }
  VkDynamicState unrelated=VK_DYNAMIC_STATE_VIEWPORT;dynamic.pDynamicStates=&unrelated;CHECK(finite_native_clip(ci));
  clip.depthClipEnable=VK_FALSE;CHECK(!finite_native_clip(ci));
  using bg3::sun_coverage::full_native_view;
  VkViewport view{0,0,2048,2048,0,1};VkRect2D scissor{{0,0},{2048,2048}};
  CHECK(full_native_view(view,scissor,2048,2048));
  view.y=2048;view.height=-2048;CHECK(full_native_view(view,scissor,2048,2048));
  view.y=0;CHECK(!full_native_view(view,scissor,2048,2048));
  view.height=2048;view.minDepth=1;view.maxDepth=0;CHECK(!full_native_view(view,scissor,2048,2048));
  view.minDepth=0;view.maxDepth=1;scissor.extent.width=1024;CHECK(!full_native_view(view,scissor,2048,2048));
  scissor.extent.width=2048;scissor.offset.x=1;CHECK(!full_native_view(view,scissor,2048,2048));
  scissor.offset.x=0;view.width=1024;CHECK(!full_native_view(view,scissor,2048,2048));
  std::printf("Sun raster domain: %u checks; no Vulkan calls\n",checks);
}
