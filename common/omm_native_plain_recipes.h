#pragma once
#include <cstdint>
namespace bg3::omm {
// Audited shader predicates, NOT complete bake/readiness certificates. Source:
// docs/evidence/omm-native-plain-recipes-20261030.json. Unknown pairs decline.
// Factory must separately prove same sampled view/sampler, captured post-VS UV,
// geometry/deformation identity, full mip support and actual content epochs.
struct NativePlainRecipe {
  uint64_t fs,vs;
  uint32_t uvLocation,vertexUvInput,textureSet,textureBinding,samplerSet,samplerBinding,channel;
  float cutoff;
  bool vertexDeformation;
};
inline constexpr NativePlainRecipe native_plain_recipes[]={
  {0x16ac1391689391d0ull,0xf4b35620d54abc6cull,0,4,1,1,0,7,3,.5f,false},
  {0x42d7e2cf1c9a7e24ull,0x910cffb017c2fb37ull,0,4,1,3,0,7,3,.2f,true},
};
inline const NativePlainRecipe* native_plain_recipe(uint64_t fs,uint64_t vs){
  for(const auto& recipe:native_plain_recipes)if(recipe.fs==fs && recipe.vs==vs)return &recipe;
  return nullptr;
}
// Exact current alpha-record recipe agreement. No cutoff epsilon, absent factor
// assumptions or generic tex-kind promotion. This returns only recipe agreement.
inline bool native_plain_record_matches(const NativePlainRecipe& r,uint32_t kind,uint32_t uv,
    uint32_t set,uint32_t binding,uint32_t channel,float cut,float firstCut,float scaleU,float scaleV){
  return kind==2 && uv==r.uvLocation && set==r.textureSet && binding==r.textureBinding &&
    channel==r.channel && cut==r.cutoff && firstCut==r.cutoff && scaleU==1.f && scaleV==1.f;
}
}
