#undef NDEBUG
#include "../layer/native_light_validation.h"
#include <cassert>
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace bg3::native_light_validation;
static std::vector<uint32_t> fixture() {
  std::vector<uint32_t> w{spv::MagicNumber,0x10300,0,100,0};
  auto add=[&](spv::Op op,std::initializer_list<uint32_t> a) {
    w.push_back((uint32_t(a.size()+1)<<16)|op);w.insert(w.end(),a);
  };
  add(spv::OpEntryPoint,{5,90,0x6e69616d,0});
  for(auto [id,binding]:{std::pair{20u,0u},{21u,10u},{22u,11u},{23u,23u}}) {
    add(spv::OpDecorate,{id,spv::DecorationDescriptorSet,1});
    add(spv::OpDecorate,{id,spv::DecorationBinding,binding});
  }
  add(spv::OpMemberDecorate,{6,0,spv::DecorationOffset,0});
  add(spv::OpMemberDecorate,{6,1,spv::DecorationOffset,16});
  add(spv::OpTypeFloat,{1,32});add(spv::OpTypeInt,{2,32,0});
  add(spv::OpTypeVector,{3,1,4});add(spv::OpTypeVector,{4,2,2});
  add(spv::OpTypeStruct,{6,3,4,2,2});
  add(spv::OpTypePointer,{7,spv::StorageClassUniform,6});
  add(spv::OpTypePointer,{8,spv::StorageClassUniform,1});
  for(uint32_t i=0;i<4;++i)add(spv::OpConstant,{2,10+i,i});
  add(spv::OpVariable,{7,20,spv::StorageClassUniform});
  add(spv::OpFunction,{1,90,0,91});
  for(uint32_t i=0;i<4;++i) {
    add(spv::OpAccessChain,{8,30+i,20,10,10+i});add(spv::OpLoad,{1,40+i,30+i});
  }
  add(spv::OpFunctionEnd,{});return w;
}
int main(int argc,char** argv) {
  if(argc==3) {
    std::ifstream f(argv[1],std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
    if(bytes.empty() || bytes.size()%4)return 2;
    std::vector<uint32_t> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());
    auto r=patch(words.data(),words.size());
    std::cout<<"status "<<int(r.status)<<" loads "<<r.loads<<" components "<<r.components<<"\n";
    if(r.status!=Status::Applied)return 3;
    std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(r.words.data()),r.words.size()*4);
    return out?0:4;
  }
  auto w=fixture();auto r=patch(w.data(),w.size());
  assert(r.status==Status::Applied && r.loads==3 && r.components==11);
  // Independently interpret the four scalar results. Original memory is
  // x=.25,y=.75,z=.5,w=.125; only the diagnostic contract may change x/y/w.
  std::map<uint32_t,float> values;
  float expected[4]={1,0,.5f,1};uint32_t found=0;
  for(size_t at=5;at<r.words.size();) {
    auto* p=r.words.data()+at;auto op=spv::Op(p[0]&65535);uint32_t n=p[0]>>16;
    if(op==spv::OpConstant && p[1]==1)values[p[2]]=std::bit_cast<float>(p[3]);
    if(op==spv::OpCopyObject && p[1]==1 && p[2]>=40 && p[2]<=43) {
      assert(values.at(p[3])==expected[p[2]-40]);++found;
    }
    if(op==spv::OpLoad && p[2]==42) {assert(p[3]==32);++found;}
    at+=n;
  }
  assert(found==4 && r.words[3]==102);
  // A different descriptor set must remain entirely outside this transformation.
  auto other=w;
  for(size_t at=5;at<other.size();at+=other[at]>>16)
    if((other[at]&65535)==spv::OpDecorate && other[at+1]==20 && other[at+2]==spv::DecorationDescriptorSet)other[at+3]=0;
  assert(patch(other.data(),other.size()).status==Status::NotTarget);
  // Unsupported component accesses are never partially rewritten.
  auto wide=w;
  for(size_t at=5;at<wide.size();at+=wide[at]>>16)
    if((wide[at]&65535)==spv::OpAccessChain && wide[at+2]==30)wide[at+5]=13;
  // Invalid member0 component rejects the whole candidate, not just one read.
  for(size_t at=5;at<wide.size();at+=wide[at]>>16)
    if((wide[at]&65535)==spv::OpConstant && wide[at+2]==13)wide[at+3]=9;
  auto invalid=patch(wide.data(),wide.size());assert(invalid.status==Status::Unsupported && invalid.words.empty());
  w.push_back(0);assert(patch(w.data(),w.size()).words.empty());
  std::cout<<"native-light control isolation: semantic field preservation, wrong interface, unsupported and malformed inputs pass\n";
}
