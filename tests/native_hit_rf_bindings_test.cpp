// rf_hit_shade descriptor contract (common/native_hit_rf_bindings.h) against compiled SPIR-V.
//   native_hit_rf_bindings_test                       table checks (ctest)
//   native_hit_rf_bindings_test [--sharc] [--tileset] [--hair] a.spv ...   + every module given
// (tools/compile_shaders.py passes every compiled gi/rt_reflections*_hit_shade.hlsl request).
#include "../common/native_hit_rf_bindings.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <set>
#include <unordered_map>
#include <vector>
using namespace bg3::native_hit_rf;
static unsigned checks;
static void check(bool ok,const char* reason){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",reason);std::exit(1);}}
struct Instruction{uint32_t op;std::vector<uint32_t> a;};

static void module(const char* file,bool sharc,bool tileset,bool hair){
 std::ifstream f(file,std::ios::binary|std::ios::ate);check(bool(f),file);auto bytes=f.tellg();
 check(bytes>=20 && bytes%4==0,"SPIR-V byte range");f.seekg(0);
 std::vector<uint32_t> w(size_t(bytes)/4);f.read(reinterpret_cast<char*>(w.data()),bytes);check(bool(f) && w[0]==0x07230203u,"SPIR-V header");
 std::unordered_map<uint32_t,Instruction> types;std::unordered_map<uint32_t,uint32_t> sets,bindings,constants;
 std::vector<std::array<uint32_t,3>> variables;
 for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;check(n>0 && n<=w.size()-i,"SPIR-V instruction range");
  if(op==71 && n>=4){if(w[i+2]==34)sets[w[i+1]]=w[i+3];if(w[i+2]==33)bindings[w[i+1]]=w[i+3];}
  if(op==43 && n>=4)constants[w[i+2]]=w[i+3];
  if(op==59 && n>=4)variables.push_back({w[i+1],w[i+2],w[i+3]});
  if((op>=19 && op<=33)||op==5341)types[w[i+1]]={op,std::vector<uint32_t>(w.begin()+i+2,w.begin()+i+n)};
  i+=n;
 }
 std::set<std::pair<uint32_t,uint32_t>> seen;unsigned push_descriptors=0;
 for(const auto& v:variables){
  const auto ptr=types.at(v[0]);check(ptr.op==32 && ptr.a.size()==2,"variable pointer");uint32_t id=ptr.a[1];
  if(!sets.contains(v[1]) || !bindings.contains(v[1]))continue;
  const uint32_t set=sets[v[1]],slot=bindings[v[1]];
  uint32_t count=1;
  while(types.at(id).op==28 || types.at(id).op==29){auto& a=types.at(id).a;if(types.at(id).op==28)count*=constants.at(a[1]);else count=0;id=a[0];}
  const auto t=types.at(id);VkDescriptorType type=VK_DESCRIPTOR_TYPE_MAX_ENUM;
  if(v[2]==12)type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  else if(v[2]==2)type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  else if(t.op==5341)type=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  else if(t.op==25 && t.a.size()>=7)type=t.a[5]==1?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  else if(t.op==26)type=VK_DESCRIPTOR_TYPE_SAMPLER;
  check(set<set_count(sharc),"only the declared sets are used");
  const Binding* want=nullptr;
  if(set==kSetPush){want=find_in(push,slot);check(want && push_enabled(*want,hair),"set 0 binding is in D-RF's push set");}
  else if(set==kSetMaterial){
   // The host's main material AlphaDSL (as tests/native_hit_gi_bindings_test.cpp).
   check(slot<=15 || (tileset && slot==19),"material AlphaDSL slot range");
   VkDescriptorType e=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;uint32_t c=1;
   if(slot==2||slot==3||slot==14){e=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;c=slot==2?4096u:slot==3?16u:64u;}
   if(slot==4||slot==5||slot==15){e=VK_DESCRIPTOR_TYPE_SAMPLER;c=slot==15?16u:1u;}
   if(slot==10)e=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
   if(tileset && slot==19){e=VK_DESCRIPTOR_TYPE_SAMPLER;c=16u;}
   check(type==e && count==c,"material AlphaDSL type and count");seen.insert({set,slot});continue;
  }
  else if(set==kSetGame){want=find_in(game,slot);check(want!=nullptr,"set 2 binding is a declared lighting set 1 input");}
  else if(set==kSetSun){want=find_in(sun,slot);check(want!=nullptr,"set 3 binding is in the sun set");}
  else {check(sharc && slot<=5,"SHaRC slot range");seen.insert({set,slot});continue;}
  check(type==want->type,"descriptor type matches the contract");
  check(count==want->count,"descriptor array count matches the contract");
  if(set==kSetPush && seen.insert({set,slot}).second)push_descriptors+=count;
  seen.insert({set,slot});
 }
 check(push_descriptors<=32,"push descriptors within the device's 32");
 // The feature is really there: material records, sky irradiance, the sun's atmosphere.
 check(seen.contains({kSetMaterial,12u}) && seen.contains({kSetGame,34u}) && seen.contains({kSetSun,0u}),"material, irradiance and sun inputs used");
 check(seen.contains({kSetPush,7u}) && seen.contains({kSetPush,30u}),"D-RF outputs retained");
 if(sharc)check(seen.contains({kSetSharc,0u}) || seen.contains({kSetSharc,1u}),"SHaRC set used by the SHaRC variant");
 std::printf("PASS %s: %u push descriptors, %zu bindings\n",file,push_descriptors,seen.size());
}

int main(int argc,char** argv){
 check(set_count(false)==4 && set_count(true)==5 && kSetSharc==4,"set numbering (the SHaRC set stays 4)");
 check(push_count(false)==32 && push_count(true)==32,"push set exactly the D-RF 32");
 check(find_in(push,30)->type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE && find_in(push,18)->type==VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,"D-RF hit distance and TLAS");
 check(!find_in(push,41) && !find_in(push,42) && !find_in(push,48),"no D-LS/SHaRC extras pushed");
 check(find_in(game,34)->type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && find_in(game,36)->type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,"sky irradiance cubes");
 check(find_in(game,45)->count==0,"bindless probe cubes");
 check(find_in(sun,0)->type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && find_in(sun,6)->type==VK_DESCRIPTOR_TYPE_SAMPLER,"sun set");
 bool sharc=false,tileset=false,hair=false;
 for(int i=1;i<argc;++i){
  if(std::strcmp(argv[i],"--sharc")==0){sharc=true;continue;}
  if(std::strcmp(argv[i],"--plain")==0){sharc=false;continue;}
  if(std::strcmp(argv[i],"--tileset")==0){tileset=true;continue;}
  if(std::strcmp(argv[i],"--hair")==0){hair=true;continue;}
  if(std::strcmp(argv[i],"--nohair")==0){hair=false;continue;}
  if(std::strcmp(argv[i],"--notileset")==0){tileset=false;continue;}
  module(argv[i],sharc,tileset,hair);
 }
 std::printf("PASS %u rf hit shade interface checks\n",checks);
}
