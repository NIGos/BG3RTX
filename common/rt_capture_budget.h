#pragma once
#include <algorithm>
#include <cstdint>

namespace bg3::capture_budget {
inline constexpr uint64_t mib=uint64_t(1)<<20;
inline bool storage_range_supported(uint64_t size,uint64_t limit){return size && limit && size<=limit;}

// The mesh pool (layer/bvhsplit.cpp pool_alloc, set 1 binding 8) stays below 2^31 bytes and within
// maxStorageBufferRange: every 2026-11-01 run whose pool doubled to 2048 MB (exactly 2^31) read it out of
// range from then on (`[core] guard: set 1 N out-of-range`, 0.7-4.2M per frame: m4p2, su1, sw1, sw3 and the
// user's CTY run), none that stayed at 1024 MB did. The capacity for `needed` bytes after `current`:
// doubling from `first` (or `current`), clamped to the cap; 0 when `needed` cannot fit (the mesh stays uncached).
inline constexpr uint64_t kMeshPoolHardCap=(uint64_t(1)<<31)-mib;   // 2047 MB
inline uint64_t mesh_pool_capacity(uint64_t current,uint64_t needed,uint64_t first,uint64_t cfg_max,uint64_t storage_limit){
 uint64_t cap_max=(std::min)(cfg_max,kMeshPoolHardCap);
 if(storage_limit)cap_max=(std::min)(cap_max,storage_limit/mib*mib);
 if(current+needed>cap_max || current>=cap_max)return 0;
 uint64_t cap=current?current*2:first;
 while(cap<current+needed)cap*=2;
 return (std::min)(cap,cap_max);
}
// The cascade capture after a frame that overflowed: its demand (captured + left-out bytes) + 25%, in
// 16 MB steps, under the same 2047 MB / device-range cap; never below `current`.
inline uint64_t cascade_grow(uint64_t current,uint64_t demand,uint64_t storage_limit){
 const uint64_t step=16*mib;
 uint64_t cap=kMeshPoolHardCap;
 if(storage_limit)cap=(std::min)(cap,storage_limit/mib*mib);
 const uint64_t want=(std::min)((demand+demand/4+step-1)/step*step,cap);
 return (std::max)(want,current);
}

// Elastic capture uses the cascade's existing 25% reserve and 16 MiB allocation
// quantum. Saturate before arithmetic: demand includes draws that did not fit.
inline uint64_t elastic_capacity(uint64_t demand,uint64_t storage_limit,uint64_t card_cap){
 const uint64_t step=16*mib;
 uint64_t cap=(std::min)(kMeshPoolHardCap,card_cap);
 if(storage_limit)cap=(std::min)(cap,storage_limit);
 demand=(std::min)(demand,kMeshPoolHardCap);
 return (std::min)((std::max)(step,(demand+demand/4+step-1)/step*step),cap);
}
// 24 samples cover three complete eight-slot scene rings for RT, nine for
// a cascade slot sampled once per three builds, rather than a loading frame.
// A new high demand invalidates the whole low-demand window immediately.
struct ElasticWindow {
 static constexpr unsigned samples=24;
 unsigned count=0;
 uint64_t peak=0;
 uint64_t observe(uint64_t current,uint64_t wanted,uint64_t demand=0){
  // A falling card cap can restrict future growth, never discard live need.
  if(wanted<current && wanted<demand)wanted=current;
  if(wanted>=current){count=0;peak=0;return wanted;}
  peak=(std::max)(peak,wanted);
  if(count<samples)++count;
  return count<samples?current:peak;
 }
 void reset(){count=0;peak=0;}
};
// A producer can be consumed by the next two cascade builds. Their command
// buffers may submit a frame later; use the same +2 fence coverage as scenes.
inline bool elastic_retired(uint64_t recorded,uint64_t gpu_done,bool cascade){
 const uint64_t lag=cascade?4:2;
 return gpu_done>=recorded && gpu_done-recorded>=lag;
}

// Include the existing partition reserves, not just bytes physically captured.
// Preserve room for the wide-layout 40% alpha floor and the alpha percent cap.
inline uint64_t elastic_rt_demand(uint64_t opaque,uint64_t alpha,uint64_t floor,
                                  bool wide_floor,int alpha_percent){
 opaque=(std::min)(opaque,kMeshPoolHardCap);alpha=(std::min)(alpha,kMeshPoolHardCap);
 const uint64_t o=opaque*11/10,a=(std::max)(floor,alpha*3/2);
 uint64_t need=(std::max)(opaque+alpha,o+a);
 if(wide_floor)need=(std::max)(need,(o*100+59)/60);
 return (std::max)(need,(alpha*100+uint64_t((std::clamp)(alpha_percent,5,90))-1)/
                                      uint64_t((std::clamp)(alpha_percent,5,90)));
}

// The default MB caps (rt_xfb_mb 384, rt_cascade_mb 160) hold one frame's soup at 24-byte
// vertices. A wider capture layout, fixed at startup (rt_hit_material 64, native tileset 80,
// native Hair 88), holds the same triangles in stride/24 times the bytes: a cap left at its
// default follows the stride, rounded (384 -> 1024 and 160 -> 427 at 64 B). A cap the user
// changed is kept as set; 24 bytes or less keeps the default.
inline constexpr int kXfbDefaultMb=384, kCascadeDefaultMb=160, kBaseStride=24;
// The alpha region floor (rt_alpha_mb 64) follows the stride the same way: 64 -> 171 MB at 64 B, so the
// first frames after a scene change (region at its floor, demand from the last frame unknown) hold the
// same triangles as at 24 B (su1 S8 2026-11-01: 0.58 alpha draws/frame made opaque in the first window).
inline constexpr int kAlphaDefaultMb=64;
// The floor is also what the first frame of a new scene gets (the region shrinks to it during loading,
// when the demand is near 0): at 171 MB the S8 load frame (223.8 MB wanted) made 36 draws opaque (smoke_ultra
// 2026-11-01, "region full ... 0.30" per frame in that window). With the default rt_alpha_mb at a wide
// stride the floor is 40% of the buffer: the largest alpha demand of the day was 404.6 MB of 1024 MB (39.5%),
// the largest opaque peak 463.8 MB (45%), so both fit together (the cap still yields to opaque demand).
inline constexpr unsigned kAlphaFloorPctWide=40;
inline uint64_t alpha_floor(uint64_t effective_floor,uint64_t size,bool default_mb,uint32_t stride){
 if(!default_mb || stride<=uint32_t(kBaseStride))return effective_floor;
 return (std::max)(effective_floor,size/100*kAlphaFloorPctWide/mib*mib);
}
inline int effective_mb(int value,int default_mb,uint32_t stride){
 if(value!=default_mb || stride<=uint32_t(kBaseStride) || value<=0)return value;
 return int((uint64_t(value)*stride+kBaseStride/2)/kBaseStride);
}
// Explicit opt-in layouts: native tileset COLOR (40), material basis (64),
// native tileset material frame (80), native Hair (88).
// Ordinary POSITION and POSITION/UV layouts retain their established policy.
inline bool corrected_partition_feedback(uint32_t stride){return stride==40u || stride==64u || stride==80u || stride==88u;}

// Split instances carry a direct AlphaRec and absolute soup offset. When the
// alpha partition is full, an exact dynamic list can use free opaque storage
// WITHOUT changing its alpha semantics. No cursor is consumed until record
// publication succeeds. Legacy geometry uses first_tri and must never spill.
inline bool alpha_spill_offset(bool enabled,bool split,bool direct_record,bool exact_list,
                               uint64_t cursor,uint64_t end,uint64_t bytes,uint64_t& offset){
 if(!enabled || !split || !direct_record || !exact_list || !bytes || cursor>end || bytes>end-cursor)return false;
 offset=cursor;return true;
}

// Existing per-frame alpha partition arithmetic. Physical MB limits and the
// 1.5x alpha / 1.1x opaque reserves are unchanged.
inline uint64_t alpha_region(uint64_t size,uint64_t floor,uint64_t current,
                             uint64_t alpha_previous,uint64_t opaque_previous,
                             bool automatic,int max_percent){
 uint64_t region=current?current:floor;
 if(automatic){
  uint64_t wanted=(std::max)(floor,(alpha_previous*3/2+mib-1)/mib*mib);
  if(wanted>region || wanted*2<region)region=wanted;
  uint64_t cap=size/100*uint64_t((std::clamp)(max_percent,5,90));
  uint64_t opaque=opaque_previous*11/10;
  if(opaque && opaque<size)cap=(std::min)(cap,(std::max)(size-opaque,floor));
  // Both reserves do not fit (alpha x1.5 + opaque x1.1 > size): the buffer is split in proportion to the two
  // demands, so both keep the same relative headroom. Before, alpha kept its 50% while opaque had 10%: the inn
  // rotation (lean_regression lr2 2026-11-02 04:42) had alpha 418 MB in a 613 MB region while opaque grew from
  // about 373 to 440 MB of the remaining 411 MB (0.02 draws/frame dropped); split by demand: 541 / 483 MB.
  if(alpha_previous && opaque_previous && alpha_previous*3/2+opaque_previous*11/10>size){
   const double share=double(size)*double(alpha_previous)/double(alpha_previous+opaque_previous);
   cap=(std::min)(cap,(std::max)(uint64_t(share)/mib*mib,floor));
  }
  region=(std::min)(region,cap);
 } else region=floor;
 return region;
}

// A capacity-overflowed alpha draw was counted in BOTH alpha demand and the
// opaque fallback stream. It must not become intrinsic opaque demand next
// frame, preventing alpha from recovering even when the configured budget fits.
// Default 24-byte capture keeps its previous policy. Never reinterpret MB caps.
inline uint64_t opaque_partition_demand(uint64_t total_opaque,uint64_t alpha_overflow,
                                        bool native_wide_capture){
 if(!native_wide_capture)return total_opaque;
 return total_opaque-(std::min)(total_opaque,alpha_overflow);
}
} // namespace bg3::capture_budget
