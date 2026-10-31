#include "../common/native_hit_tileset_inputs.h"
#include "../common/native_hit_tileset_fields.h"
#include "../common/native_tileset_surface.h"
#include "../common/native_hit_resources.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3::native_hit;
static unsigned checks;
static void check(bool p){++checks;if(!p)std::abort();}
int main(){
 check(!tileset_material_enabled(false,false));check(!tileset_material_enabled(false,true));check(!tileset_material_enabled(true,false));check(tileset_material_enabled(true,true));
 VkVertexInputBindingDescription b[6]{};VkVertexInputAttributeDescription a[6]{};
 for(unsigned i=0;i<6;++i){b[i]={i,32,i>=1&&i<=3?VK_VERTEX_INPUT_RATE_INSTANCE:VK_VERTEX_INPUT_RATE_VERTEX};a[i]={i,i, VK_FORMAT_R32G32B32A32_SFLOAT,0};}
 VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vi.vertexBindingDescriptionCount=6;vi.pVertexBindingDescriptions=b;vi.vertexAttributeDescriptionCount=6;vi.pVertexAttributeDescriptions=a;
 VkGraphicsPipelineCreateInfo p{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};p.pVertexInputState=&vi;
 check(tileset_inputs(p).valid);b[1].inputRate=VK_VERTEX_INPUT_RATE_VERTEX;check(!tileset_inputs(p).valid);b[1].inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;
 VkVertexInputBindingDivisorDescriptionEXT div{1,2};VkPipelineVertexInputDivisorStateCreateInfoEXT ds{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT};ds.vertexBindingDivisorCount=1;ds.pVertexBindingDivisors=&div;vi.pNext=&ds;
 check(!tileset_inputs(p).valid);div.divisor=1;check(tileset_inputs(p).valid);
 VkBaseInStructure unknown{VK_STRUCTURE_TYPE_APPLICATION_INFO};ds.pNext=&unknown;check(!tileset_inputs(p).valid);ds.pNext=nullptr;
 a[5].location=4;check(!tileset_inputs(p).valid);a[5].location=5;
 VkDynamicState state=VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dyn.dynamicStateCount=1;dyn.pDynamicStates=&state;p.pDynamicState=&dyn;check(!tileset_inputs(p).valid);state=VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;check(tileset_inputs(p).valid);
 uint32_t n=99;check(tileset_vertex_count(6,3,n)&&n==18);check(!tileset_vertex_count(5,3,n));check(!tileset_vertex_count(3,UINT32_MAX,n));check(!tileset_vertex_count(3,0,n));
 DrawKey raw;raw.words[1]=6;raw.words[2]=3;std::array<InputStream,6> streams;for(unsigned i=0;i<6;++i)streams[i]={i+1,16*i,32,uint32_t(VK_FORMAT_R32G32B32A32_SFLOAT)};
 DrawKey expected;check(tileset_key(raw,streams,expected));
 for(unsigned i=0;i<6;++i)for(unsigned f=0;f<4;++f){auto changed=streams;if(f==0)++changed[i].buffer;else if(f==1)++changed[i].offset;else if(f==2)++changed[i].stride;else ++changed[i].format;DrawKey other;check(tileset_key(raw,changed,other)&&!(expected==other));}
 raw.words[3]++;DrawKey other;check(tileset_key(raw,streams,other)&&!(expected==other));
 using namespace bg3::transport;
 for(float red:{-1.f,0.f,std::numeric_limits<float>::denorm_min(),1.f})for(float green:{-1.f,0.f,std::numeric_limits<float>::denorm_min(),1.f}){
  bg3::native_tileset::Plan oracle;check(bg3::native_tileset::plan(tileset_deferred_fs,tileset_deferred_vs,true,{red,green},{.25f,.75f},{2,3,4},oracle)==bg3::native_tileset::Status::Supported);
  auto actual=nts_plan(red,green,tb_vec(2,3,4));check(actual.status==0 && actual.branch==unsigned(oracle.channel));check(actual.tiling*.25f==oracle.uv[0]&&actual.tiling*.75f==oracle.uv[1]);
 }
 check(nts_plan(0,std::numeric_limits<float>::quiet_NaN(),tb_vec(1,1,1)).status!=0);
 // A 2D material consumes one material row and a deduplicated sampler, never
 // three fake array-cache slots. Failure is transactional, including outputs.
 ResourceBudget budget;ResourceIndices indices;
 for(unsigned i=0;i<material_slots;++i){check(budget.reserve_sampler(7,indices));check(indices.sampler==0 && indices.new_sampler==(i==0));check(budget.cache_count==0);}
 auto full=budget;indices.sampler=77;check(!budget.reserve_sampler(8,indices));check(budget.material_count==full.material_count&&budget.sampler_count==full.sampler_count&&indices.sampler==77);
 ResourceBudget unique;for(unsigned i=1;i<=16;++i)check(unique.reserve_sampler(i,indices));check(!unique.reserve_sampler(17,indices));check(unique.material_count==16&&unique.sampler_count==16&&unique.cache_count==0);check(unique.reserve_sampler(1,indices)&&indices.sampler==0&&!indices.new_sampler);
 for(auto state: {std::pair{false,true},std::pair{true,false}}){
  ResourceBudget gated;for(unsigned draw=0;draw<300;++draw)if(tileset_material_enabled(state.first,state.second))check(gated.reserve_sampler(draw+1,indices));
  const CacheIdentity image{1,2,3,4,5};
  for(unsigned draw=0;draw<material_slots;++draw)check(gated.reserve(8,{image,image,image},indices));
  check(gated.material_count==material_slots && gated.sampler_count==1 && gated.cache_count==1);
 }
 std::printf("%u tileset input/selector checks passed\n",checks);
}
