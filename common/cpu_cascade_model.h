#pragma once
#include "cpu_exact_cache.h"
#include "cascade_casters.h"
#include <algorithm>

namespace bg3::cpu_opt {
inline bool model_position(cascade::DrawId& draw,const uint32_t model[16],const float origin[3],uint64_t epoch,bool cached){
  if(!cached)return cascade::model_position(draw,model,origin);
  using Cache=ExactCache<20,cascade::DrawId>;thread_local Cache cache;Cache::Key key{};key[0]=epoch;
  for(unsigned i=0;i<16;++i)key[i+1]=model[i];
  uint32_t bits[3];std::memcpy(bits,origin,sizeof bits);
  for(unsigned i=0;i<3;++i)key[i+17]=bits[i];
  const auto& value=cache.get(key,[&]{cascade::DrawId v;cascade::model_position(v,model,origin);return v;});
  std::copy_n(value.position,3,draw.position);std::copy_n(value.position_error,3,draw.position_error);
  return draw.position_known=value.position_known;
}
}
