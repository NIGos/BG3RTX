#include "../common/native_hit_tileset_inputs.h"
#include "../common/native_hit_tileset_fields.h"
#include "../common/native_tileset_surface.h"
#include "../common/native_hit_resources.h"
#include "../layer/spirv_xfb.h"
#include <fstream>
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3::native_hit;
static unsigned checks;
static void check(bool p){++checks;if(!p){std::fprintf(stderr,"FAIL tileset check %u\n",checks);std::abort();}}
int main(int argc,char** argv){
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
 ResourceBudget unique;for(unsigned i=1;i<=nh_sampler_slots;++i)check(unique.reserve_sampler(i,indices));check(!unique.reserve_sampler(nh_sampler_slots+1,indices));check(unique.material_count==nh_sampler_slots&&unique.sampler_count==nh_sampler_slots&&unique.cache_count==0);check(unique.reserve_sampler(1,indices)&&indices.sampler==0&&!indices.new_sampler);
 for(auto state: {std::pair{false,true},std::pair{true,false}}){
  ResourceBudget gated;for(unsigned draw=0;draw<300;++draw)if(tileset_material_enabled(state.first,state.second))check(gated.reserve_sampler(draw+1,indices));
  const CacheIdentity image{1,2,3,4,5};
  for(unsigned draw=0;draw<material_slots;++draw)check(gated.reserve(8,{image,image,image},indices));
  check(gated.material_count==material_slots && gated.sampler_count==1 && gated.cache_count==1);
 }
 // Deferred output packet: validate actual SPIR-V decoration offsets, not a
 // reconstructed tangent frame. This minimal interface is parser-only.
 std::vector<uint32_t> code={0x07230203,0x10000,0,100,0};
 auto emit=[&](std::initializer_list<uint32_t> v){code.insert(code.end(),v);};
 emit({(2u<<16)|17,1});emit({(5u<<16)|15,0,50,0x6e69616d,0});emit({(4u<<16)|71,20,11,0});
 for(uint32_t i=0;i<5;++i)emit({(4u<<16)|71,30+i,30,i});
 emit({(3u<<16)|22,1,32});emit({(4u<<16)|23,2,1,2});emit({(4u<<16)|23,3,1,3});emit({(4u<<16)|23,4,1,4});
 emit({(4u<<16)|32,5,3,2});emit({(4u<<16)|32,6,3,3});emit({(4u<<16)|32,7,3,4});emit({(4u<<16)|59,7,20,3});
 for(uint32_t i=0;i<5;++i)emit({(4u<<16)|59,i==0?7u:i==1?5u:6u,30+i,3});
 auto packet=[&](const std::vector<uint32_t>& input,uint32_t stride,const std::string& prefix){
  std::vector<uint32_t> patched,off,explicit_off;int uv=-1;uint32_t flags=0,hair=0;
  check(bg3::patch_vs(input.data(),input.size(),patched,stride,&uv,&hair,false,nullptr,false,&flags,true));
  check(uv==1 && flags==3 && hair==0);
  std::vector<uint32_t> outputs;std::array<uint32_t,5> locations{};
  for(size_t i=5;i<input.size();i+=input[i]>>16)if((input[i]&65535)==59 && (input[i]>>16)>=4 && input[i+3]==3)outputs.push_back(input[i+2]);
  for(size_t i=5;i<input.size();i+=input[i]>>16)if((input[i]&65535)==71 && (input[i]>>16)==4 && input[i+2]==30 && input[i+3]<5 &&
      std::find(outputs.begin(),outputs.end(),input[i+1])!=outputs.end())locations[input[i+3]]=input[i+1];
  for(auto expected: {std::pair{0u,24u},std::pair{2u,40u},std::pair{3u,52u},std::pair{4u,64u}}){
   auto target=std::pair{locations[expected.first],expected.second};check(target.first!=0);
   bool offset=false,step=false;
   for(size_t i=5;i<patched.size();i+=patched[i]>>16)if((patched[i]&65535)==71 && (patched[i]>>16)==4 && patched[i+1]==target.first){
    offset|=patched[i+2]==35 && patched[i+3]==target.second;step|=patched[i+2]==37 && patched[i+3]==stride;
   }check(offset&&step);
  }
  check(bg3::patch_vs(input.data(),input.size(),off,stride));
  check(bg3::patch_vs(input.data(),input.size(),explicit_off,stride,nullptr,nullptr,false,nullptr,false,nullptr,false));check(off==explicit_off);
  if(!prefix.empty())for(auto data:{std::pair{&patched,".spv"},std::pair{&off,".off.spv"}}){
   std::ofstream f(prefix+std::to_string(stride)+data.second,std::ios::binary);f.write(reinterpret_cast<const char*>(data.first->data()),data.first->size()*4);check(bool(f));
  }
 };
 for(uint32_t stride:{80u,88u})packet(code,stride,"");
 std::vector<uint32_t> partial;uint32_t flags=0;
 check(bg3::patch_vs(code.data(),code.size(),partial,79,nullptr,nullptr,false,nullptr,false,&flags,true));check(flags==1);
 emit({(3u<<16)|71,32,14}); // Flat N must not be accepted as the smooth frame.
 check(bg3::patch_vs(code.data(),code.size(),partial,80,nullptr,nullptr,false,nullptr,false,&flags,true));check(flags==1);
 check(bg3::transport::NtsStatQualified==287 && bg3::transport::NtsStatPrimaryMatch==288 && bg3::transport::NtsStatsCount==289);
 if(argc==3){
  std::ifstream f(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
  check(bytes.size()>=20 && bytes.size()%4==0);std::vector<uint32_t> native(bytes.size()/4);std::memcpy(native.data(),bytes.data(),bytes.size());
  for(uint32_t stride:{80u,88u})packet(native,stride,argv[2]);
 }
 std::printf("%u tileset input/selector/capture checks passed\n",checks);
}
