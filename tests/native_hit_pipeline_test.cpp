#include "../common/native_hit_pipeline.h"
#include <cstdio>
#include <initializer_list>
#include <cstdlib>
using namespace bg3::native_hit;
static unsigned checks;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"material state check %u failed\n",checks);std::exit(1);}}
int main(){
 VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
 VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
 VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
 ci.pRasterizationState=&raster;ci.pMultisampleState=&ms;ci.pColorBlendState=&blend;
 check(material_state_reasons(ci)==0);
 VkPipelineRasterizationDepthClipStateCreateInfoEXT clip{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT};clip.depthClipEnable=VK_TRUE;raster.pNext=&clip;
 check(material_state_reasons(ci)==0); // Exact known state rejected by the old blanket-pNext test.
 clip.depthClipEnable=VK_FALSE;check((material_state_reasons(ci)&MaterialClipDomain)!=0);clip.depthClipEnable=VK_TRUE;
 clip.flags=1;check((material_state_reasons(ci)&MaterialRasterExtension)!=0);clip.flags=0;
 VkPipelineRasterizationDepthClipStateCreateInfoEXT second=clip;clip.pNext=&second;check((material_state_reasons(ci)&MaterialRasterExtension)!=0);clip.pNext=nullptr;
 VkBaseInStructure unknown{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT,nullptr};
 clip.pNext=&unknown;check((material_state_reasons(ci)&MaterialRasterExtension)!=0);clip.pNext=nullptr;
 raster.pNext=&unknown;check((material_state_reasons(ci)&MaterialRasterExtension)!=0);raster.pNext=&clip;
 raster.rasterizerDiscardEnable=VK_TRUE;check((material_state_reasons(ci)&MaterialRasterDiscard)!=0);raster.rasterizerDiscardEnable=VK_FALSE;
 raster.depthClampEnable=VK_TRUE;check((material_state_reasons(ci)&MaterialClipDomain)!=0);raster.depthClampEnable=VK_FALSE;
 VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};ci.pDynamicState=&dynamic;dynamic.dynamicStateCount=1;
 for(auto state:{VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT,VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT,VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT}){
  dynamic.pDynamicStates=&state;check((material_state_reasons(ci)&MaterialClipDomain)!=0);
 }
 VkDynamicState viewport=VK_DYNAMIC_STATE_VIEWPORT;dynamic.pDynamicStates=&viewport;check(material_state_reasons(ci)==0);ci.pDynamicState=nullptr;
 ms.rasterizationSamples=VK_SAMPLE_COUNT_2_BIT;check((material_state_reasons(ci)&MaterialMultisample)!=0);ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 ms.alphaToCoverageEnable=VK_TRUE;check((material_state_reasons(ci)&MaterialMultisample)!=0);ms.alphaToCoverageEnable=VK_FALSE;
 ms.pNext=&unknown;check((material_state_reasons(ci)&MaterialMultisample)!=0);ms.pNext=nullptr;
 blend.pNext=&unknown;check((material_state_reasons(ci)&MaterialBlendState)!=0);blend.pNext=nullptr;
 ci.pRasterizationState=nullptr;check((material_state_reasons(ci)&MaterialRasterMissing)!=0);ci.pRasterizationState=&raster;
 ci.pMultisampleState=nullptr;check((material_state_reasons(ci)&MaterialMultisample)!=0);ci.pMultisampleState=&ms;
 ci.pColorBlendState=nullptr;check((material_state_reasons(ci)&MaterialBlendState)!=0);ci.pColorBlendState=&blend;
 raster.pNext=nullptr;check(material_state_reasons(ci)==0);
 std::printf("PASS %u actual Vulkan material-state cases; no GPU\n",checks);
}
