#pragma once
#include <algorithm>
#include <cstdint>

namespace bg3::capture_budget {
inline constexpr uint64_t mib=uint64_t(1)<<20;
inline bool storage_range_supported(uint64_t size,uint64_t limit){return size && limit && size<=limit;}
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
