#pragma once
#include "cpu_exact_cache.h"
#include <cstdint>

namespace bg3::cpu_opt {
struct InstanceRowSource {uint64_t buffer=0,offset=0,stride=0;uint32_t layout_stride=0,layout_offset=0;};
struct InstanceRows {std::array<const uint8_t*,3> rows{};std::array<uint32_t,3> strides{};bool valid=false;};
template<class Size,class Mapped>
InstanceRows instance_rows(const std::array<InstanceRowSource,3>& sources,uint32_t first_instance,uint32_t instances,
                           uint64_t epoch,bool cached,Size&& size_of,Mapped&& mapped) {
  auto prepare=[&]{
    InstanceRows out;if(!instances)return out;
    for(unsigned k=0;k<3;++k){const auto& s=sources[k];
      const uint32_t stride=s.layout_stride?s.layout_stride:uint32_t(s.stride);
      const uint64_t size=s.buffer?size_of(s.buffer):0;
      const uint64_t first=s.offset+uint64_t(first_instance)*stride+s.layout_offset;
      const uint64_t last=first+uint64_t(instances-1)*stride+16;
      const auto* p=s.buffer && stride && size && last<=size?mapped(s.buffer,first):nullptr;
      if(!p)return out;out.rows[k]=p;out.strides[k]=stride;
    }
    out.valid=true;return out;
  };
  if(!cached)return prepare();
  using Cache=ExactCache<18,InstanceRows,4096>;thread_local Cache cache;typename Cache::Key key{};
  unsigned n=0;key[n++]=epoch;key[n++]=first_instance;key[n++]=instances;
  for(const auto& s:sources){key[n++]=s.buffer;key[n++]=s.offset;key[n++]=s.stride;key[n++]=s.layout_stride;key[n++]=s.layout_offset;}
  return cache.get(key,prepare);
}
}
