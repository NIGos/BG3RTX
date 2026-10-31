#include "../common/omm_material_cache.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace bg3::omm;
static unsigned checks;
static void check(bool value){++checks;if(!value){std::fprintf(stderr,"material cache check %u failed\n",checks);std::exit(1);}}
int main(){
  const MaterialKey first{11,12,13,14,15};
  check(first==MaterialKey{11,12,13,14,15});
  // One mesh can be drawn with different material recipes, parent images,
  // subresource views or immutable samplers. None may share its micromap.
  check(!(first==MaterialKey{21,12,13,14,15}));check(!(first==MaterialKey{11,22,13,14,15}));
  check(!(first==MaterialKey{11,12,23,14,15}));check(!(first==MaterialKey{11,12,13,24,15}));
  check(!(first==MaterialKey{11,12,13,14,25}));
  for(uint64_t consumer:{100ull,101ull,100000ull}){
    check(!cached_bake_ready(100,consumer,-1));check(!cached_bake_ready(100,consumer,2));
    check(!cached_bake_ready(100,consumer,3));check(cached_bake_ready(100,consumer,1));
    check(cached_bake_ready(100,consumer,0)==(consumer==100));
  }
  for(int state:{-1,0,1,2}){
    check(!cached_bake_ready(0,100,state));check(!cached_bake_ready(100,0,state));
  }
  std::printf("OMM material/producer cache checks=%u\n",checks);
}
