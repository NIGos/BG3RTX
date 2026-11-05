#pragma once
#include "cpu_exact_cache.h"
#include <cstring>

namespace bg3::cpu_opt {
// Cache only the address, never mutable mapped contents. The resource epoch
// changes on map/unmap, bind, destroy/create and allocation retirement.
template<class Resolve>
bool mapped_read(uint64_t buffer,uint64_t offset,void* dst,size_t bytes,
                 uint64_t epoch,bool cached,Resolve&& resolve) {
  const uint8_t* source;
  if(cached) {
    using Cache=ExactCache<3,const uint8_t*,4096>;thread_local Cache cache;
    source=cache.get({buffer,offset,epoch},[&]{return resolve(buffer,offset);});
  } else source=resolve(buffer,offset);
  if(!source)return false;
  std::memcpy(dst,source,bytes);return true;
}
struct BufferFacts {uint64_t size=0,usage=0;bool known=false,size_known=false;};
template<class Load>
BufferFacts buffer_facts(uint64_t buffer,uint64_t epoch,bool cached,Load&& load) {
  if(!cached)return load();
  using Cache=ExactCache<2,BufferFacts,4096>;thread_local Cache cache;
  return cache.get({buffer,epoch},load);
}
}
