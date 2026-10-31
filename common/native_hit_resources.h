#pragma once
#include <array>
#include <cstdint>
namespace bg3::native_hit {
inline constexpr uint32_t material_slots=128;
struct CacheIdentity {
 uint64_t view{},view_generation{},image{},image_generation{},layout{};
 bool operator==(const CacheIdentity&) const = default;
 bool valid() const {return view && view_generation && image && image_generation;}
};
struct ResourceIndices {uint32_t sampler{},normal{},albedo{},physical{};bool new_sampler{};};
// Bounded packet-local resource accounting. Plan on a copy, then publish that
// copy only after the owned sampler clone and draw allocation succeed.
struct ResourceBudget {
 std::array<uint64_t,16> samplers{};std::array<CacheIdentity,64> caches{};
 uint32_t sampler_count{},cache_count{},material_count{};
 bool reserve_sampler(uint64_t sampler,ResourceIndices& result){
  if(!sampler || material_count>=material_slots)return false;
  auto next=*this;ResourceIndices indices{};indices.sampler=next.sampler_count;
  for(uint32_t i=0;i<next.sampler_count;++i)if(next.samplers[i]==sampler){indices.sampler=i;break;}
  indices.new_sampler=indices.sampler==next.sampler_count;
  if(indices.new_sampler){if(next.sampler_count==next.samplers.size())return false;next.samplers[next.sampler_count++]=sampler;}
  ++next.material_count;*this=next;result=indices;return true;
 }
 bool reserve(uint64_t sampler,const std::array<CacheIdentity,3>& views,ResourceIndices& result){
  if(!sampler || material_count>=material_slots)return false;
  auto next=*this;ResourceIndices indices{};indices.sampler=next.sampler_count;
  for(uint32_t i=0;i<next.sampler_count;++i)if(next.samplers[i]==sampler){indices.sampler=i;break;}
  indices.new_sampler=indices.sampler==next.sampler_count;
  if(indices.new_sampler){if(next.sampler_count==next.samplers.size())return false;next.samplers[next.sampler_count++]=sampler;}
  uint32_t mapped[3]{};
  for(uint32_t v=0;v<3;++v){
   if(!views[v].valid())return false;uint32_t i=0;
   for(;i<next.cache_count;++i)if(next.caches[i]==views[v])break;
   if(i==next.cache_count){if(next.cache_count==next.caches.size())return false;next.caches[next.cache_count++]=views[v];}
   mapped[v]=i;
  }
  indices.normal=mapped[0];indices.albedo=mapped[1];indices.physical=mapped[2];
  ++next.material_count;*this=next;result=indices;return true;
 }
};
}
