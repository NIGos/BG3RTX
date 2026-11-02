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
