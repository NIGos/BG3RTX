#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "native_hit_records.h"
namespace bg3::native_hit {
// Deferred material draws captured per packet (one row each in the packet rows buffer).
inline constexpr uint32_t material_slots=nh_deferred_rows;
struct CacheIdentity {
 uint64_t view{},view_generation{},image{},image_generation{},layout{};
 bool operator==(const CacheIdentity&) const = default;
 bool valid() const {return view && view_generation && image && image_generation;}
};
struct ResourceIndices {uint32_t sampler{},normal{},albedo{},physical{};bool new_sampler{};};
// Why a plan failed: the per-packet material rows, the descriptor arrays (bindings 14/15),
// or an invalid input. Capacity failures are counted by the layer's capacity log.
enum class ResourceFailure : uint32_t {None,Slots,Samplers,Caches,Invalid};
// A planned reservation: the indices and the identities it would add. Planning never
// changes the budget; commit publishes it. The layer plans, clones the sampler and
// allocates the draw, and commits only when all of that succeeded.
struct ResourcePlan {
 ResourceIndices indices{};uint64_t sampler{};std::array<CacheIdentity,3> added{};uint32_t added_count{};
 ResourceFailure failure{ResourceFailure::None};
};
// Packet-local resource accounting, bounded by the descriptor array sizes shared with
// the shader (nh_sampler_slots, nh_cache_slots) and the row count (material_slots).
struct ResourceBudget {
 std::vector<uint64_t> samplers;std::vector<CacheIdentity> caches;
 uint32_t sampler_count{},cache_count{},material_count{};
 static uint32_t find_sampler(const std::vector<uint64_t>& list,uint64_t sampler){
  for(uint32_t i=0;i<list.size();++i)if(list[i]==sampler)return i;return UINT32_MAX;
 }
 bool plan_sampler(uint64_t sampler,ResourcePlan& out) const {
  out=ResourcePlan{};
  if(!sampler){out.failure=ResourceFailure::Invalid;return false;}
  if(material_count>=material_slots){out.failure=ResourceFailure::Slots;return false;}
  uint32_t index=find_sampler(samplers,sampler);out.indices.new_sampler=index==UINT32_MAX;
  if(out.indices.new_sampler){if(sampler_count>=nh_sampler_slots){out.failure=ResourceFailure::Samplers;return false;}index=sampler_count;}
  out.indices.sampler=index;out.sampler=sampler;return true;
 }
 bool plan(uint64_t sampler,const std::array<CacheIdentity,3>& views,ResourcePlan& out) const {
  if(!plan_sampler(sampler,out))return false;
  uint32_t mapped[3]{};
  for(uint32_t v=0;v<3;++v){
   if(!views[v].valid()){out=ResourcePlan{};out.failure=ResourceFailure::Invalid;return false;}
   uint32_t i=0;for(;i<cache_count;++i)if(caches[i]==views[v])break;
   if(i==cache_count){
    uint32_t a=0;for(;a<out.added_count;++a)if(out.added[a]==views[v])break;
    if(a==out.added_count){
     if(cache_count+out.added_count>=nh_cache_slots){out=ResourcePlan{};out.failure=ResourceFailure::Caches;return false;}
     out.added[out.added_count++]=views[v];
    }
    i=cache_count+a;
   }
   mapped[v]=i;
  }
  out.indices.normal=mapped[0];out.indices.albedo=mapped[1];out.indices.physical=mapped[2];return true;
 }
 void commit(const ResourcePlan& p){
  if(p.indices.new_sampler){samplers.push_back(p.sampler);++sampler_count;}
  for(uint32_t a=0;a<p.added_count;++a){caches.push_back(p.added[a]);++cache_count;}
  ++material_count;
 }
 bool reserve_sampler(uint64_t sampler,ResourceIndices& result){
  ResourcePlan p;if(!plan_sampler(sampler,p))return false;commit(p);result=p.indices;return true;
 }
 bool reserve(uint64_t sampler,const std::array<CacheIdentity,3>& views,ResourceIndices& result){
  ResourcePlan p;if(!plan(sampler,views,p))return false;commit(p);result=p.indices;return true;
 }
};
// Deferred-packet XFB storage: fixed-size chunks allocated on demand, so the capture is
// not bounded by one preallocated stream. A draw never straddles two chunks.
struct ChunkPlacement {uint32_t chunk{};uint64_t offset{};bool new_chunk{};};
inline bool place_in_chunks(uint32_t chunks,uint64_t cursor,uint64_t bytes,uint64_t chunk_bytes,uint32_t max_chunks,ChunkPlacement& out){
 if(!bytes || bytes>chunk_bytes || !max_chunks)return false;
 if(chunks && cursor<=chunk_bytes && bytes<=chunk_bytes-cursor){out={chunks-1u,cursor,false};return true;}
 if(chunks>=max_chunks)return false;
 out={chunks,0,true};return true;
}
// Source-stream size for one scene slot: the exact need rounded up to the granularity,
// at least one granule, at most the cap (0 when the need itself exceeds the cap).
inline uint64_t source_stream_bytes(uint64_t need,uint64_t granule,uint64_t cap){
 if(!granule || need>cap)return 0;
 uint64_t rounded=need<=granule?granule:((need+granule-1u)/granule)*granule;
 return rounded>cap?cap:rounded;
}
}
