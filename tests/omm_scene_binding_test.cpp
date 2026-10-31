#include "../common/omm_scene_binding.h"
#include "../common/omm_scene_capability.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
using namespace bg3::omm;
static unsigned checks;static void check(bool b){++checks;if(!b)std::abort();}
int main(){
  uint64_t scene=21;unsigned calls=0;std::set<unsigned> registered;
  auto validate=[&](unsigned command,bool allowed,bool found,uint64_t id=7){return [&,command,allowed,found,id](uint64_t selected,uint64_t& reference,uint64_t& identity){++calls;check(selected==21);reference=found?11:0;identity=id;if(allowed)registered.insert(command);return allowed;};};
  {SceneBinding scope(scene,false,validate(0,true,true));check(scope.valid());check(scope.state()==BindingState::Ordinary);check(scene==21);}
  check(calls==0);
  // Four independent recording commands: pre, local, reflection and cache.
  for(unsigned command=1;command<=4;++command){SceneBinding scope(scene,true,validate(command,true,true));check(scope.valid());check(scene==21);check(scope.state()==BindingState::Accelerated);}
  check(registered.size()==4 && calls==4);
  {SceneBinding scope(scene,true,validate(5,false,true));check(scope.valid());check(scene==11);check(scope.state()==BindingState::Reference);}
  check(scene==21 && !registered.contains(5));
  // New command generation must revalidate, rather than reuse command1's success.
  registered.erase(1);
  {SceneBinding scope(scene,true,validate(1,false,true));check(scene==11);check(scope.valid());}
  check(scene==21 && !registered.contains(1));
  for(bool fast:{false,true}){SceneBinding scope(scene,true,validate(6,fast,false));check(!scope.valid());check(scene==21);}
  {SceneBinding scope(scene,true,validate(7,true,true,0));check(!scope.valid());}
  try{SceneBinding scope(scene,true,validate(8,false,true));check(scene==11);throw std::runtime_error("early exit");}catch(const std::runtime_error&){}
  check(scene==21);
  // Reused AS handles with a different build generation cannot consume old data.
  {SceneBinding scope(scene,true,validate(9,true,true,8),7);check(!scope.valid());check(scene==21);}
  {SceneBinding scope(scene,true,validate(10,false,true,8),7);check(!scope.valid());check(scene==21);}
  {SceneBinding scope(scene,true,validate(11,false,true,7),7);check(scope.valid());check(scene==11);}
  check(scene==21);
  uint32_t cap=0;check(!scene_capability(&cap));check(!scene_capability(nullptr));publish_scene_capability(&cap);check(scene_capability(&cap));publish_scene_capability(&cap);check(cap==1);publish_scene_consumer(&cap);check(scene_consumer(&cap));release_scene_consumer(&cap);check(!scene_consumer(&cap));check(scene_capability(&cap));
  std::printf("OMM per-command binding: %u checks, no GPU claim\n",checks);
}
