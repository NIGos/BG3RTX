#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "native_hit_records.h"
#include "rt_capture_budget.h"
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
 // A second sampler of the same material (ENV_DetailMap_VT: VT sampler 1:2 plus detail sampler
 // 0:7), planned against the budget as `first` would leave it. Commit first, then commit_sampler.
 bool plan_second_sampler(const ResourcePlan& first,uint64_t sampler,ResourcePlan& out) const {
  out=ResourcePlan{};
  if(!sampler){out.failure=ResourceFailure::Invalid;return false;}
  uint32_t index=sampler==first.sampler?first.indices.sampler:find_sampler(samplers,sampler);
  out.indices.new_sampler=index==UINT32_MAX;
  if(out.indices.new_sampler){index=sampler_count+(first.indices.new_sampler?1u:0u);if(index>=nh_sampler_slots){out.failure=ResourceFailure::Samplers;return false;}}
  out.indices.sampler=index;out.sampler=sampler;return true;
 }
 void commit_sampler(const ResourcePlan& p){if(p.indices.new_sampler){samplers.push_back(p.sampler);++sampler_count;}}
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
// The caps of the three streams, from the RT capture they copy (layer/hit_material.cpp; buffers are still sized to
// the need, so a cap costs nothing until used). The source stream copies the paired draws' prepass triangles out of
// one frame's capture: at most the capture's wide-stride alpha floor share (capture_budget kAlphaFloorPctWide, 40%),
// in whole granules. The deferred XFB holds the same triangles at the same stride: one chunk per source granule plus
// one (a draw never straddles two chunks). The query basis holds both copies: 2 x source. Never below the
// 2026-10-31 fixed caps (128 MB, 4 chunks, 256 MB). Camp 2026-11-02 (hig1 09:54, IP grass on, 1024 MB capture): the
// fixed caps overflowed 119-134 deferred rows (126.6 MB in 4/4 chunks), source 117/128 MB, basis 234/256 MB.
inline constexpr uint64_t kSourceFloor=128ull<<20,kQueryFloor=256ull<<20;inline constexpr uint32_t kDeferredChunksFloor=4;
inline constexpr unsigned kStreamCapturePct=capture_budget::kAlphaFloorPctWide;
// A scene slot's source copy is read only when creating queries in the slot's own frame (F-1 borrowing reuses
// existing query storage): once the frame moved on and every owner (the scene recording and
// its queries) retired on the GPU, the copy goes back to the recycle pool. Before, all 8 slots kept theirs (hig1
// 2026-11-02 10:27: source 2144 MB in 8 slots with IP grass on, 512 MB off, while at most 2 queries were alive).
inline bool source_releasable(bool has_buffer,uint64_t slot_frame,uint64_t frame,bool owners_retired){
 return has_buffer && slot_frame<frame && owners_retired;
}
struct StreamCaps {uint64_t source{},query{};uint32_t deferred_chunks{};};
inline StreamCaps stream_caps(uint64_t capture_bytes,uint64_t granule){
 StreamCaps c{kSourceFloor,kQueryFloor,kDeferredChunksFloor};
 if(!granule)return c;
 const uint64_t limit=(uint64_t(UINT32_MAX)/2)/granule*granule;   // uint32 row offsets into the query basis (2 x source)
 uint64_t source=capture_bytes/100*kStreamCapturePct/granule*granule;
 if(source<=kSourceFloor)return c;   // small (24 B) or unknown capture: the fixed caps exactly
 source=source>limit?limit:source;
 c.source=source;c.query=2*source<kQueryFloor?kQueryFloor:2*source;
 const uint64_t chunks=source/granule+1;c.deferred_chunks=chunks<kDeferredChunksFloor?kDeferredChunksFloor:uint32_t(chunks);
 return c;
}
}
