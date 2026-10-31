#include "../common/native_hit_gi_bindings.h"
#include "../common/native_hit_records.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <functional>
using namespace bg3::native_hit_gi;
static unsigned checks;
static void check(bool ok,const char* reason){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",reason);std::exit(1);}}
struct Instruction{uint32_t op;std::vector<uint32_t> a;};
static void module(const char* file,bool reference,bool tileset,bool sun){
 std::ifstream f(file,std::ios::binary|std::ios::ate);check(bool(f),file);auto bytes=f.tellg();check(bytes>=20 && bytes%4==0,"SPIR-V byte range");f.seekg(0);
 std::vector<uint32_t>w(size_t(bytes)/4);f.read(reinterpret_cast<char*>(w.data()),bytes);check(bool(f)&&w[0]==0x07230203u,"SPIR-V header");
 std::unordered_map<uint32_t,Instruction> types;std::unordered_map<uint32_t,uint32_t> sets,bindings,constants,pointers,strides;
 std::unordered_map<uint64_t,uint32_t> offsets;std::vector<std::array<uint32_t,3>> variables;
 for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;check(n>0 && n<=w.size()-i,"SPIR-V instruction range");
  if(op==71 && n>=4){if(w[i+2]==34)sets[w[i+1]]=w[i+3];if(w[i+2]==33)bindings[w[i+1]]=w[i+3];if(w[i+2]==6)strides[w[i+1]]=w[i+3];}
  if(op==72 && n>=5 && w[i+3]==35)offsets[(uint64_t(w[i+1])<<32)|w[i+2]]=w[i+4];
  if(op==43 && n>=4)constants[w[i+2]]=w[i+3];
  if(op==59 && n>=4)variables.push_back({w[i+1],w[i+2],w[i+3]});
  if((op>=19 && op<=33)||op==5341)types[w[i+1]]={op,std::vector<uint32_t>(w.begin()+i+2,w.begin()+i+n)};
  i+=n;
 }
 std::function<uint32_t(uint32_t)> size=[&](uint32_t id)->uint32_t{
  auto t=types.at(id);auto&a=t.a;
  if(t.op==21||t.op==22)return a[0]/8;
  if(t.op==23||t.op==24)return size(a[0])*a[1];
  if(t.op==28)return strides.at(id)*constants.at(a[1]);
  if(t.op==30){uint32_t end=0;for(uint32_t i=0;i<a.size();++i)end=(std::max)(end,offsets.at((uint64_t(id)<<32)|i)+size(a[i]));return end;}
  return 0;
 };
 std::unordered_map<uint32_t,uint32_t> seen;unsigned descriptors=0,push=0,sun_seen=0;
 for(const auto& v:variables){
  const auto ptr=types.at(v[0]);check(ptr.op==32 && ptr.a.size()==2,"variable pointer");uint32_t id=ptr.a[1];
  if(v[2]==9){push=size(id);check(push>0 && push<=push_bytes,"actual shader push range fits host");continue;}
  if(!sets.contains(v[1])||!bindings.contains(v[1]))continue;
  const uint32_t set=sets[v[1]];check(set==0 || set==1 || (!reference && set==4) || (sun && !reference && set==kSetSun),"only declared consumer sets statically used");
  uint32_t count=1;while(types.at(id).op==28){auto&a=types.at(id).a;count*=constants.at(a[1]);id=a[0];}
  auto t=types.at(id);VkDescriptorType type=VK_DESCRIPTOR_TYPE_MAX_ENUM;
  if(v[2]==12)type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  else if(v[2]==2)type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  else if(t.op==5341)type=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  else if(t.op==25 && t.a.size()>=7)type=t.a[5]==1?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  else if(t.op==26)type=VK_DESCRIPTOR_TYPE_SAMPLER;
  const uint32_t slot=bindings[v[1]];
  if(set==kSetSun && sun){
   const auto* b=find_sun(slot);check(b!=nullptr,"gi_hit_sun: sun set binding in the contract");
   check(type==b->type && count==b->count,"gi_hit_sun: sun set descriptor type/array count");
   sun_seen|=1u<<slot;continue;
  }
  if(set!=0){
   VkDescriptorType expected=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;uint32_t expected_count=1;
   if(set==1){check(slot<=15 || (tileset && slot==19),"feature-specific main material AlphaDSL slot range");
    if(slot==2||slot==3||slot==14){expected=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;expected_count=slot==2?4096u:slot==3?16u:bg3::native_hit::nh_cache_slots;}
    if(slot==4||slot==5||slot==15){expected=VK_DESCRIPTOR_TYPE_SAMPLER;expected_count=slot==15?bg3::native_hit::nh_sampler_slots:1u;}
    if(slot==10)expected=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    if(tileset && slot==19){expected=VK_DESCRIPTOR_TYPE_SAMPLER;expected_count=16u;}
   }else{check(slot<=5,"SHaRC slot range");if(slot==3)expected=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;}
   check(type==expected && count==expected_count,"existing material/SHaRC descriptor type and array count");continue;
  }
  const auto* host=find(slot,true,reference);
  check(host!=nullptr,"actual shader binding exists in BOTH shared host layout/write contract");
  check(type==host->type && count==host->count,"actual shader descriptor type/array count match");
  if(!seen.contains(slot)){seen[slot]=count;descriptors+=count;}else check(seen[slot]==count,"duplicate binding aliases compatible");
 }
 check(descriptors>0&&descriptors<=32,"actual descriptor array counts within device minimum");check(push>0,"actual push block found");
 if(!reference){for(uint32_t slot:{8u,11u,13u,17u})check(seen.contains(slot),"actual GI outputs/history/MV retained");
  check(sun?(sun_seen&1u)!=0u:sun_seen==0u,"gi_hit_sun variants (only) read the sun set (atmosphere b0)");}
 else {for(uint32_t slot:{6u,7u})check(seen.contains(slot),"actual reference radiance outputs retained");}
 std::printf("PASS %s: %u unique set0 descriptors, push %u bytes\n",file,descriptors,push);
}
int main(int argc,char**argv){
 check(descriptor_count(false)==20 && descriptor_count(true)==21,"bounded plain/Hair union");
 check(!find(47,true),"roof volumes come from the sun set b8, not the push set");
 check(kSetSun==3 && sun.size()==8 && find_sun(0)->type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && find_sun(6)->type==VK_DESCRIPTOR_TYPE_SAMPLER && !find_sun(7)
       && find_sun(8)->type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,"gi_hit_sun sun set 0-6 and the roof volumes 8");
 check(find(46,false)->type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE && !find(46,false,true),"hit light policy per-light data (GI only)");
 for(bool hair:{false,true}){uint32_t n=0;for(const auto& b:bindings)if(enabled(b,hair)){check(find(b.binding,hair)==&b,"layout and write iteration identical");n+=b.count;}check(n<=32,"host push descriptor count");}
 for(uint32_t i:{8u,11u,13u,17u}){check(find(i,false)!=nullptr,"regression: required GI binding omitted by old LS contraction");check(!(i<=7 || i==12 || i==16 || i==18 || i>=20),"old LS filter really omitted GI input/output");}
 check(find(48,false)->type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,"rebase and SHaRC camera input");check(!find(50,false)&&find(50,true),"physical Hair conditional descriptor");
 check(!find(27,true)&&!find(28,true)&&!find(41,true),"unused LS shadow/class extras excluded");
 check(descriptor_count(false,true)==18 && descriptor_count(true,true)==18,"bounded dedicated reference union");
 check(find(48,false,true)->type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,"reference rebase camera retained");
 check(!find(50,true,true),"no hypothetical physical-origin reference descriptor");
 bool reference=false,tileset=false,sun=false;for(int i=1;i<argc;++i){if(std::strcmp(argv[i],"--reference")==0){reference=true;sun=false;continue;}if(std::strcmp(argv[i],"--tileset")==0){tileset=true;continue;}if(std::strcmp(argv[i],"--sun")==0){sun=true;continue;}module(argv[i],reference,tileset,sun);}std::printf("PASS %u GI host/interface checks\n",checks);
}
