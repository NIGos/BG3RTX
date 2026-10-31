#include "../common/native_hit_pipeline.h"
#include "../common/native_hit_inputs.h"
#include "../common/native_hit_resources.h"
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
 // Actual pipeline attribute bindings, not a fixed assumption that location0
 // lives in stream0 or that higher streams are unused.
 VkVertexInputBindingDescription bindings[2]={{4,32,VK_VERTEX_INPUT_RATE_VERTEX},{7,8,VK_VERTEX_INPUT_RATE_VERTEX}};
 VkVertexInputAttributeDescription attrs[2]={{0,4,VK_FORMAT_R32G32B32_SFLOAT,4},{1,7,VK_FORMAT_R32G32_SFLOAT,0}};
 VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vertex.vertexBindingDescriptionCount=2;vertex.pVertexBindingDescriptions=bindings;vertex.vertexAttributeDescriptionCount=2;vertex.pVertexAttributeDescriptions=attrs;ci.pVertexInputState=&vertex;
 auto inputs=static_inputs(ci);check(inputs.valid && inputs.position.binding==4 && inputs.uv.binding==7 && inputs.position.offset==4 && inputs.uv.stride==8);
 VkDynamicState stride=VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;dynamic.pDynamicStates=&stride;ci.pDynamicState=&dynamic;check(static_inputs(ci).position.dynamic_stride);
 VkDynamicState unknown_vertex=VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;dynamic.pDynamicStates=&unknown_vertex;check(!static_inputs(ci).valid);ci.pDynamicState=nullptr;
 bindings[1].inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;check(!static_inputs(ci).valid);bindings[1].inputRate=VK_VERTEX_INPUT_RATE_VERTEX;
 attrs[1].location=2;check(!static_inputs(ci).valid);attrs[1].location=1;
 DrawKey ordinary{};ordinary.words[0]=1;ordinary.words[1]=960;ordinary.words[6]=77;ordinary.words[9]=123;
 StaticKeyInputs a{};a.engine_buffer=1;a.engine_offset=112;a.position={2,16,32,uint32_t(VK_FORMAT_R32G32B32_SFLOAT)};a.uv={3,24,8,uint32_t(VK_FORMAT_R32G32_SFLOAT)};
 DrawKey source{},deferred{};check(canonical_static_key(ordinary,a,source));
 auto other_stale=ordinary;other_stale.words[9]=999;check(canonical_static_key(other_stale,a,deferred) && source==deferred);
 auto different=a;different.position.buffer=4;check(canonical_static_key(ordinary,different,deferred) && !(source==deferred));
 different=a;different.engine_offset+=112;check(canonical_static_key(ordinary,different,deferred) && !(source==deferred));
 different=a;different.uv.offset+=4;check(canonical_static_key(ordinary,different,deferred) && !(source==deferred));
 a.pull_hint_known=true;a.pull_index=5;a.pull_buffer=9;a.pull_offset=64;a.pull_range=256;check(canonical_static_key(ordinary,a,source));
 different=a;different.position={999,999,999,999};check(canonical_static_key(other_stale,different,deferred) && source==deferred); // ONLY inactive position stream omitted.
 different=a;different.engine_buffer=9999;different.engine_offset=987654;check(canonical_static_key(ordinary,different,deferred) && source==deferred); // Pass-local Engine copies are not geometry identity.
 auto other_index=ordinary;other_index.words[6]=78;check(canonical_static_key(other_index,a,deferred) && !(source==deferred));
 different=a;different.pull_offset+=20;check(canonical_static_key(ordinary,different,deferred) && !(source==deferred));
 different=a;different.uv.offset+=4;check(canonical_static_key(ordinary,different,deferred) && !(source==deferred));
 different=a;different.pull_index=6;check(canonical_static_key(ordinary,different,deferred) && !(source==deferred));
 different=a;different.pull_buffer=10;check(canonical_static_key(ordinary,different,deferred) && !(source==deferred));
 different=a;different.uv.buffer=11;check(canonical_static_key(ordinary,different,deferred) && !(source==deferred));
 different=a;different.pull_range=91;check(!canonical_static_key(ordinary,different,deferred));
 different=a;different.pull_offset=UINT64_MAX-1;check(!canonical_static_key(ordinary,different,deferred));
 a.pull_hint_known=false;different=a;different.position.buffer=4;check(canonical_static_key(ordinary,a,source));check(canonical_static_key(ordinary,different,deferred) && !(source==deferred));
 a.pull_hint_known=true;a.pull_index=0;check(canonical_static_key(ordinary,a,source));different=a;different.position.buffer=4;check(canonical_static_key(ordinary,different,deferred) && !(source==deferred));
 check(static_depth_family(0x8975ea2744c12a74ull,0xe2ecf65a7810b23bull));check(!static_depth_family(0x3812bc980438747full,0x8995da1e4060f53bull));
 // Actual packet resource limits apply to unique immutable identities, not draws.
 CacheIdentity normal{1,11,21,31,5},albedo{2,12,21,31,5},physical{3,13,21,31,5};
 std::array<CacheIdentity,3> views{normal,albedo,physical};ResourceBudget budget;ResourceIndices indices{};
 for(unsigned i=0;i<100;++i){check(budget.reserve(7,views,indices));check(indices.sampler==0 && indices.normal==0 && indices.albedo==1 && indices.physical==2 && indices.new_sampler==(i==0));}
 check(budget.material_count==100 && budget.sampler_count==1 && budget.cache_count==3);
 // Planning a new sampler then failing clone/allocation leaves published state unchanged.
 auto unpublished=budget;check(unpublished.reserve(8,views,indices));check(budget.sampler_count==1 && budget.material_count==100);
 for(unsigned i=100;i<material_slots;++i)check(budget.reserve(7,views,indices));
 auto unchanged=budget;check(!budget.reserve(7,views,indices));check(budget.material_count==unchanged.material_count && budget.cache_count==unchanged.cache_count);
 ResourceBudget samplers;for(unsigned i=1;i<=16;++i)check(samplers.reserve(i,views,indices));
 check(!samplers.reserve(17,views,indices));check(samplers.material_count==16 && samplers.sampler_count==16);check(samplers.reserve(1,views,indices) && !indices.new_sampler);
 ResourceBudget caches;for(unsigned i=1;i<=64;++i){auto v=normal;v.view=i;v.view_generation=i+100;check(caches.reserve(7,{v,v,v},indices));}
 auto newview=normal;newview.view=999;newview.view_generation=999;
 check(!caches.reserve(7,{normal,newview,newview},indices));check(caches.cache_count==64 && caches.material_count==64);
 ResourceBudget atomic;auto invalid=physical;invalid.image_generation=0;
 check(!atomic.reserve(7,{normal,albedo,invalid},indices));check(atomic.material_count==0 && atomic.cache_count==0 && atomic.sampler_count==0);
 check(atomic.reserve(7,views,indices));auto generation=normal;generation.view_generation++;check(atomic.reserve(7,{generation,albedo,physical},indices) && indices.normal==3);
 generation=normal;generation.image_generation++;check(atomic.reserve(7,{generation,albedo,physical},indices) && indices.normal==4);
 generation=normal;generation.layout++;check(atomic.reserve(7,{generation,albedo,physical},indices) && indices.normal==5);
 std::printf("PASS %u actual Vulkan material-state cases; no GPU\n",checks);
}
