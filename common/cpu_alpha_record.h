#pragma once
#include "cpu_exact_cache.h"
#include <cmath>
#include <cstring>

namespace bg3::cpu_opt {
struct VtMaterialPacket {std::array<float,8> uv{};std::array<float,32> tiles{};};
template<class Record> struct MaterialTemplate {Record record{};bool valid=false;};
// Fresh mapped bytes are the certificate: descriptor generations cannot attest
// host-written material memory. Never cache frame-local slots or triangle bases.
template<class Record,class Recipe>
MaterialTemplate<Record> vt_material_record(const Recipe& a,const VtMaterialPacket& packet,uint64_t epoch,bool cached) {
  auto prepare=[&]{
    MaterialTemplate<Record> out;auto& r=out.record;
    r.kind=a.kind;r.channel=a.channel;r.cut=a.cut;r.cut_first=a.cut_first;
    r.fs_lo=uint32_t(a.hash);r.fs_hi=uint32_t(a.hash>>32);r.scale[0]=r.scale[1]=a.k;
    std::memcpy(r.p0,packet.uv.data(),32);std::memcpy(r.t,packet.tiles.data(),128);
    auto finite=[](float f){return f>-1e30f && f<1e30f;};
    for(float f:packet.uv)if(!finite(f))return out;
    for(float f:packet.tiles)if(!finite(f))return out;
    out.valid=finite(a.k);return out;
  };
  if(!cached)return prepare();
  using Cache=ExactCache<26,MaterialTemplate<Record>>;
  thread_local Cache cache;typename Cache::Key key{};
  key[0]=epoch;key[1]=a.hash;key[2]=uint64_t(a.kind)|(uint64_t(a.channel)<<32);
  uint32_t cuts[2];std::memcpy(cuts,&a.cut,4);std::memcpy(cuts+1,&a.cut_first,4);
  std::memcpy(key.data()+3,cuts,8);std::memcpy(key.data()+4,&a.k,4);
  std::memcpy(key.data()+5,packet.uv.data(),32);std::memcpy(key.data()+9,packet.tiles.data(),128);
  return cache.get(key,prepare);
}
}
