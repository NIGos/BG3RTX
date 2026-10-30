#include "../common/omm_native_plain_recipes.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace bg3::omm;
static unsigned checks;
static void check(bool x){++checks;if(!x){std::fprintf(stderr,"check %u failed\n",checks);std::exit(1);}}
int main(){
  check(!native_plain_recipe(0,0));
  for(const auto& r:native_plain_recipes){
    check(native_plain_recipe(r.fs,r.vs)==&r);check(!native_plain_recipe(r.fs,0));check(!native_plain_recipe(r.fs^1,r.vs));
    check(r.uvLocation==0 && r.vertexUvInput==4 && r.samplerSet==0 && r.samplerBinding==7);
    auto matches=[&](uint32_t kind,uint32_t uv,uint32_t set,uint32_t bind,uint32_t ch,float cut,float first,float su,float sv){return native_plain_record_matches(r,kind,uv,set,bind,ch,cut,first,su,sv);};
    check(matches(2,0,r.textureSet,r.textureBinding,3,r.cutoff,r.cutoff,1,1));
    check(!matches(1,0,r.textureSet,r.textureBinding,3,r.cutoff,r.cutoff,1,1));
    check(!matches(3,0,r.textureSet,r.textureBinding,3,r.cutoff,r.cutoff,1,1));
    check(!matches(2,1,r.textureSet,r.textureBinding,3,r.cutoff,r.cutoff,1,1));
    check(!matches(2,0,0,r.textureBinding,3,r.cutoff,r.cutoff,1,1));
    check(!matches(2,0,r.textureSet,r.textureBinding+1,3,r.cutoff,r.cutoff,1,1));
    check(!matches(2,0,r.textureSet,r.textureBinding,0,r.cutoff,r.cutoff,1,1));
    check(!matches(2,0,r.textureSet,r.textureBinding,3,std::nextafter(r.cutoff,1.f),r.cutoff,1,1));
    check(!matches(2,0,r.textureSet,r.textureBinding,3,r.cutoff,0.f,1,1));
    check(!matches(2,0,r.textureSet,r.textureBinding,3,r.cutoff,r.cutoff,2,1));
    check(!matches(2,0,r.textureSet,r.textureBinding,3,r.cutoff,r.cutoff,1,NAN));
  }
  check(!native_plain_recipes[0].vertexDeformation && native_plain_recipes[1].vertexDeformation);
  check(!native_plain_recipe(0x03a2d9f645625c44ull,0)); // Scalp recipe is not raw alpha.
  check(!native_plain_recipe(0x9aaa6d61f7d2d9a9ull,0)); // Two texture/material factors.
  std::printf("Native plain recipe checks %u; shader agreement only, no runtime certificate\n",checks);
}
