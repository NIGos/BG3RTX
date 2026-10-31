#include "../common/native_hit_tileset_inputs.h"
#include "../common/native_hit_tileset_fields.h"
#include "../common/native_tileset_surface.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3::native_hit;
static unsigned checks;
static void check(bool p){++checks;if(!p)std::abort();}
int main(){
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
 for(unsigned i=0;i<6;++i)for(unsigned f=0;f<4;++f){auto changed=streams;auto* words=&changed[i].buffer;if(f<3)++words[f];else ++changed[i].format;DrawKey other;check(tileset_key(raw,changed,other)&&!(expected==other));}
 raw.words[3]++;DrawKey other;check(tileset_key(raw,streams,other)&&!(expected==other));
 using namespace bg3::transport;
 for(float red:{-1.f,0.f,std::numeric_limits<float>::denorm_min(),1.f})for(float green:{-1.f,0.f,std::numeric_limits<float>::denorm_min(),1.f}){
  bg3::native_tileset::Plan oracle;check(bg3::native_tileset::plan(tileset_deferred_fs,tileset_deferred_vs,true,{red,green},{.25f,.75f},{2,3,4},oracle)==bg3::native_tileset::Status::Supported);
  auto actual=nts_plan(red,green,tb_vec(2,3,4));check(actual.status==0 && actual.branch==unsigned(oracle.channel));check(actual.tiling*.25f==oracle.uv[0]&&actual.tiling*.75f==oracle.uv[1]);
 }
 check(nts_plan(0,std::numeric_limits<float>::quiet_NaN(),tb_vec(1,1,1)).status!=0);
 std::printf("%u tileset input/selector checks passed\n",checks);
}
