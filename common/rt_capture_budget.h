#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
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
// Admission precedes optional native-layout cache uploads. Pending GPU ranges
// remain charged in used; LRU eviction cannot make them immediately reusable.
inline bool mesh_cache_admit(uint64_t pool,uint64_t acceleration,uint64_t vertices,
                              uint64_t blas,uint64_t budget){
 if(pool>kMeshPoolHardCap || vertices>kMeshPoolHardCap-pool ||
    pool>budget || acceleration>budget-pool)return false;
 const uint64_t free=budget-pool-acceleration;
 return vertices<=free && blas<=free-vertices;
}
// rt_mesh_budget_own (claude/mesh-cache-rate, 2026-11-09, default 0, live): the BLAS bytes that the mesh
// cache's rt_mesh_budget_mb charges, in mesh_cache_admit and in the LRU pass (layer/bvhsplit.cpp).
// 0 (old): every byte of the shared BLAS chunks, so also the per-draw, cascade, sun and groom BLAS and pending
// ranges. In the zoomed-out S8 turns (rv2 2026-11-09 14:57-14:59) the whole pool plus all BLAS was 2.5-3.8 GB
// against 2048 MB: every new mesh was declined (about 250 draws per build) and every mesh not drawn in the
// last 8 frames was evicted each frame (35-60 meshes left). The declined draws then stay captured with their
// own uncompacted BLAS, which keeps the total over the budget: a loop that only a quiet view breaks.
// 1: only the cached meshes' own BLAS. The whole pool stays charged (shared and hard-capped, with mandatory
// users: hair bounds, holds, bases; pending ranges included), so the pool limits of the 2026-11-03 load fix
// are unchanged. Other BLAS chunks stay under the VRAM guard's chunk cap and its mesh LRU ask.
inline uint64_t mesh_cache_charged_as(uint64_t all_as,uint64_t cached_as,bool own){
 return own?(std::min)(cached_as,all_as):all_as;
}
// When the LRU pass runs (on present): above the budget, every 600 frames (meshes unused for lru_frames), and
// with rt_mesh_budget_own also after a build declined a mesh for the budget. Admission refuses AT the budget while
// the old pass ran only ABOVE it, so a cache filled to the budget kept its stale meshes and new ones stayed
// captured (rv2 15:00:39-51: pool + all BLAS 2044-2047 of 2048 MB, about 15 draws declined per build).
inline bool mesh_cache_lru_due(uint64_t charged,uint64_t budget,bool declined,bool own,uint64_t now){
 return charged>budget || now%600==0 || (own && declined);
}
// The LRU pass over the budget (layer/bvhsplit.cpp): `old` are the meshes not drawn in the last 8 frames,
// oldest first, each with its last use and its bytes (pool vertices + BLAS). Returns how many to evict from
// the front: while the charged bytes are above 90% of the budget, or the mesh is unused for lru_frames.
struct LruMesh{uint64_t last_used,bytes;};
template<class Seq>
inline size_t mesh_cache_lru_count(const Seq& old,uint64_t charged,uint64_t budget,uint64_t lru_frames,uint64_t now){
 size_t n=0;
 for(const LruMesh& m:old){
  if(charged<=budget*9/10 && m.last_used+lru_frames>now)break;
  charged-=(std::min)(charged,m.bytes);
  ++n;
 }
 return n;
}
// rt_mesh_budget_own headroom (claude/mesh-cache-rate-2, 2026-11-09). rv3 (19:15:28, S8 turns, key on): the cache
// grew to 433 meshes, the pool to 1977 / 2047 MB and the BLAS chunks to the VRAM guard's cap. Its meshes were all
// in use (the LRU never evicts meshes drawn in the last 8 frames), so the frame's other users found no room: the
// cascade casters' BLAS and grooms were refused and the hair bounds (pool) failed, which failed the whole build.
// Freed ranges only retire behind a newer scene, so from then on no scene was built (118 of 120 frames skipped).
// With the key the cache keeps out of the room the other users of each shared resource need: their peak demand
// (live bytes, pending ranges and refused requests) over the last 600 to 1200 frames, plus the 25% reserve of the
// elastic captures. Two-bucket rolling peak: each bucket spans `window` observations.
struct DemandPeak{
 uint64_t cur=0,prev=0,start=0;bool any=false;
 void observe(uint64_t v,uint64_t now,uint64_t window){
  if(!any){any=true;start=now;}
  else if(now>=start+window){prev=cur;cur=0;start=now;}
  cur=(std::max)(cur,v);
 }
 uint64_t peak()const{return (std::max)(cur,prev);}
};
// The cache's share of a resource of `capacity` bytes whose other users peaked at `other_peak`.
inline uint64_t mesh_cache_share(uint64_t capacity,uint64_t other_peak){
 const uint64_t need=other_peak>UINT64_MAX/2?UINT64_MAX:other_peak+other_peak/4;
 return capacity>need?capacity-need:0;
}
// Optional pool users (hold snapshots) may fill the pool only up to this limit: the mandatory users' reserve
// (their peak + 25%, of which `mandatory_now` is already allocated) stays free.
inline uint64_t mesh_cache_hold_limit(uint64_t capacity,uint64_t mandatory_peak,uint64_t mandatory_now){
 const uint64_t need=mandatory_peak>UINT64_MAX/2?UINT64_MAX:mandatory_peak+mandatory_peak/4;
 const uint64_t reserve=need>mandatory_now?need-mandatory_now:0;
 return capacity>reserve?capacity-reserve:0;
}
// A new mesh fits when both its pool vertices and its BLAS stay inside the cache's shares.
inline bool mesh_cache_fits(uint64_t cache_pool,uint64_t vertices,uint64_t pool_share,
                            uint64_t cache_as,uint64_t blas,uint64_t as_share){
 return cache_pool<=pool_share && vertices<=pool_share-cache_pool && cache_as<=as_share && blas<=as_share-cache_as;
}
// The share pass on present: meshes not drawn in the last 8 frames, oldest first, with their pool and BLAS bytes.
// Runs when the cache is above a share or a build declined a mesh for the shares (as mesh_cache_lru_due: a cache
// at its share would otherwise keep stale meshes); evicts from the front while the cache is above 90% of either.
struct ShareMesh{uint64_t last_used,pool,as;};
template<class Seq>
inline size_t mesh_cache_share_count(const Seq& old,uint64_t cache_pool,uint64_t pool_share,
                                     uint64_t cache_as,uint64_t as_share,bool declined){
 if(!declined && cache_pool<=pool_share && cache_as<=as_share)return 0;
 size_t n=0;
 for(const ShareMesh& m:old){
  if(cache_pool<=pool_share/10*9 && cache_as<=as_share/10*9)break;
  cache_pool-=(std::min)(cache_pool,m.pool);
  cache_as-=(std::min)(cache_as,m.as);
  ++n;
 }
 return n;
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
// A growth replaces the whole buffer, and the old one stays allocated next to it for four fence-covered
// frames. A target that creeps up in 16 MiB quanta therefore reallocated 1.4-1.8 GiB ten times in 13 s in
// the user's log of 2026-11-14 (22:15:10-23: 1408 -> 1456 -> 1568 -> 1680 -> 1744 -> 1776 -> 1792 -> 1808
// -> 1824 -> 1840 MiB; the VRAM guard went LOW at 22:15:14 with "RT capture 3520 MB", old + new). A growth
// is at least 1/8 of the current size (in quanta, under `cap`): that sequence takes 3 replacements, ending
// at 2016 MiB. The shrink rule needs peak + 25% to fit, so it never undoes a surplus of 12.5%. A `wanted`
// above the cap is returned as it is (the caller's own clamp and refusal decide).
inline uint64_t elastic_growth(uint64_t current,uint64_t wanted,uint64_t cap){
 if(wanted<=current)return wanted;
 const uint64_t quantum=16*mib;
 const uint64_t step=(current/8+quantum-1)/quantum*quantum;
 if(current>UINT64_MAX-step)return wanted;
 return (std::max)(wanted,(std::min)(current+step,cap));
}
// Producer-boundary replacement can be blocked by the previous detached soup
// for four fence-covered frames. Include the next producer interval as well:
// RT observes each frame (5 steps); a cascade slot each third frame (3 steps).
// Keep the largest observed positive step across quiet windows: a returning
// pan must not have to overflow again to relearn its reserve. First observation
// is not a step from zero (loading is not a measured camera movement).
//
// rt_capture_rise_reserve=1 (opt-vram, 2026-11-08): the movement reserve is the largest demand rise
// observed over `steps` consecutive samples (the replacement delay), not the largest single-sample step
// times `steps`. A ramp of s per sample still gives steps * s; a one-sample cut of S gives S, not 3 S
// (cascade) or 5 S (RT). Every rise of the learned history over the delay stays covered. The user's
// 2026-11-08 log: cascade 140 -> 310 MiB in one sample learned a 3 x 265 MiB reserve, 3 slots x 1232 MiB
// for a 370 MiB peak. `learn` is the demand the rise is learned from (default: `demand`): the RT capture
// passes its demand without the alpha region that scales with the buffer itself (40% floor), so its own
// growth is not learned as a camera step (1024 -> 1680 -> 2047 MiB in 0.2 s at load in that log).
struct CaptureHeadroom {
 uint64_t previous=0,step=0;
 bool initialized=false;
 static constexpr unsigned kRiseHistory=8;   // >= the largest delay in samples (5, primary capture)
 std::array<uint64_t,kRiseHistory> history{};
 unsigned history_count=0,history_next=0;
 uint64_t rise=0;
 // rt_capture_peak_reserve=1 (claude/vram-rightsize, 2026-11-14, default 0): the learned rise is a cut between two
 // demand levels (user log 22:14-22:26: the cascade demand alternates between 54-70 MiB and 490-580 MiB, rise 535
 // MiB), but it was added on top of the HIGH level too: 3 slots x 1088-1120 MiB for 510-580 MiB. With the key the
 // rise reserve is not taken past the highest demand of the last kPeakWindow to 2 x kPeakWindow samples + 25%
 // (the level a repeat of the cut lands on), except for a rise still in progress (the last `steps` samples: a
 // pan beyond that peak keeps its ramp reserve). At the low level the reserve stays (the cut up fits); when the
 // high level has not been seen for the window the reserve decays to the 25% and the preset quarter, and the next
 // cut overflows once, as the first one of a session does.
 static constexpr uint64_t kPeakWindow=7200;
 DemandPeak peak;uint64_t samples=0;
 uint64_t capacity(uint64_t demand,uint64_t storage_limit,uint64_t card_cap,unsigned interval,
                   uint64_t preset_capacity=0,uint64_t current=0,bool rise_reserve=false,
                   uint64_t learn=UINT64_MAX,bool peak_reserve=false){
  demand=(std::min)(demand,kMeshPoolHardCap);
  if(initialized && demand>previous)step=(std::max)(step,demand-previous);
  previous=demand;initialized=true;
  const uint64_t steps=(4+interval+interval-1)/interval;
  // Both estimators learn always, so a live toggle of the key uses the history already seen.
  const uint64_t observed=(std::min)(learn==UINT64_MAX?demand:learn,kMeshPoolHardCap);
  uint64_t recent=0;
  for(unsigned k=1;k<=history_count && k<=steps && k<=kRiseHistory;++k){
   const uint64_t before=history[(history_next+kRiseHistory-k)%kRiseHistory];
   if(observed>before){rise=(std::max)(rise,observed-before);recent=(std::max)(recent,observed-before);}
  }
  peak.observe(demand,samples++,kPeakWindow);
  history[history_next]=observed;history_next=(history_next+1)%kRiseHistory;
  if(history_count<kRiseHistory)++history_count;
  // Bootstrap from the preset's existing quarter-capacity reserve, rather
  // than learning the first pan only after a small static-scene shrink.
  uint64_t movement=rise_reserve?(std::min)(rise,kMeshPoolHardCap)
                                :(std::min)(step,kMeshPoolHardCap/steps)*steps;
  if(peak_reserve){
   const uint64_t level=peak.peak()+peak.peak()/4;
   movement=(std::min)(movement,(std::max)(recent,level>demand?level-demand:0));
  }
  const uint64_t reserve=(std::max)((std::max)(demand/4,preset_capacity/4),movement);
  const uint64_t quantum=16*mib;
  uint64_t cap=(std::min)(kMeshPoolHardCap,card_cap);
  if(storage_limit)cap=(std::min)(cap,storage_limit);
  const uint64_t required=(std::min)(kMeshPoolHardCap,
      (std::max)(quantum,(demand+reserve+quantum-1)/quantum*quantum));
  const uint64_t target=(std::min)(required,cap);
  // A reduced budget may refuse growth; it cannot remove reserve already held.
  return target<current && target<required?current:target;
 }
};
// 24 samples cover three complete eight-slot scene rings for RT, nine for
// a cascade slot sampled once per three builds, rather than a loading frame.
// Retain a rolling peak even across replacement. Production cascade and RT
// windows restore reserve proactively; shrink only when another 25% step fits.
template<unsigned Samples> struct CaptureWindow {
 static constexpr unsigned samples=Samples;
 unsigned count=0;
 uint64_t peak=0;
 std::array<uint64_t,samples> recent{};
 unsigned next=0;
 uint64_t observe(uint64_t current,uint64_t wanted,uint64_t demand=0,bool restore_reserve=false){
  // A falling card cap can restrict future growth, never discard live need.
  if(wanted<current && wanted<demand)wanted=current;
  recent[next]=wanted;next=(next+1)%samples;
  peak=*std::max_element(recent.begin(),recent.end());
  if(wanted>=current){
   count=0;
   return wanted>current && (restore_reserve || !demand || demand>current)?wanted:current;
  }
  if(count<samples)++count;
  return count<samples || peak+peak/4>current?current:peak;
 }
 void reset(){count=0;peak=0;recent={};next=0;}
};
using ElasticWindow=CaptureWindow<24>;
// Empty cascade captures (including casters toggled off) do not establish a
// smaller active working set. Inactivity has no upper bound, so no finite
// timeout can guarantee that the old casters fit on their first returning
// frame. Hold the allocated capacity until 120 consecutive nonempty samples
// establish a smaller need. Replacing the buffer after recording has begun
// cannot recover rejected draws or change commands/offsets already published.
struct CascadeElasticWindow : CaptureWindow<120> {
 uint64_t observe(uint64_t current,uint64_t wanted,uint64_t demand){
  if(!demand){reset();return current;}
  return CaptureWindow<120>::observe(current,wanted,demand,true);
 }
};
// The primary capture runs every frame: retain a whole smoke_ultra statistics
// window, rather than dropping a turn's peak after only 24 frames.
using RtElasticWindow=CaptureWindow<120>;

// The next draw's demand is unknowable at the producer boundary. The buffer
// cannot be replaced during recording: published offsets and Vulkan commands
// already refer to it. Keep the fixed path's capacity preallocated, so an
// elastic shrink cannot introduce an overflow before growth can take effect.
inline uint64_t elastic_rt_capacity(uint64_t demand,uint64_t storage_limit,
                                    uint64_t card_cap,uint64_t fixed_capacity,uint64_t current=0){
 const uint64_t target=(std::max)(fixed_capacity,elastic_capacity(demand,storage_limit,card_cap));
 // A tighter budget may forbid growth, but must not authorize shrinking
 // below demand plus margin already held by the existing allocation.
 const uint64_t reserve=elastic_capacity(demand,storage_limit,kMeshPoolHardCap);
 return target<current && target<reserve?current:target;
}
// rt_capture_demand_size (claude/vram-rightsize, 2026-11-14, default 0, live). Two things kept the RT capture
// far above its demand in the user's log of 22:15-22:26 (1408 to 2047 MiB held for 130 to 1100 MiB of soup):
//  1. the preset floor above (1408 MiB at the 88-byte stride), also re-applied at every replacement;
//  2. the demand counted the alpha region the buffer reserves for itself (40% of its own size at a wide stride,
//     818 MiB of 2047): demand >= 0.4 x size + opaque, so a large buffer justified itself (22:20:17: opaque 49.2 +
//     alpha 80.4 MiB gave a target of 1632 MiB).
// With the key the demand is the partition's need without the held region (elastic_rt_demand with reserved_alpha
// 0: opaque x 1.1 + max(alpha floor, alpha x 1.5), and opaque x 1.1 / 60% so the 40% floor still leaves the
// opaque room), and the floor is one quantum. The first buffer of a session is still the preset (the demand of
// a loading frame is unknown); the reserve (CaptureHeadroom: a quarter of the demand and of the preset, the
// learned rise) and the shrink window (120 low samples, peak + 25% must fit) are unchanged: no thrash.
// Cost bound when the demand jumps past the reserve: draws left out (alpha: made opaque) for the replacement
// delay, 5 frames, then one replacement of at least 1/8 (elastic_growth), old + new allocated for 4 frames.
inline uint64_t rt_fixed_floor(uint64_t preset,bool demand_size){return demand_size?16*mib:preset;}
inline constexpr int kBaseStride=24;   // the default capture layout (the MiB presets are sized for it)
// rt_capture_split_mb (claude/vram-rightsize, 2026-11-14, default 0, startup). The MiB presets follow the vertex
// stride (effective_mb), and with native Hair that is 88 bytes: 1408 MiB RT, 587 MiB per cascade slot, 235 MiB
// alpha floor. With rt_capture_wide_split active only strand-frame modules write 88 bytes (in the user's log the
// cascade pass had 0.0 wide draws); every other draw is captured at the base stride (64), where the same
// triangles need 1024 / 427 / 171 MiB. With the key the presets follow the base stride of an active split; the
// wide draws' bytes are part of the alpha demand and grow the buffer like any other demand.
inline uint32_t preset_stride(uint32_t full_stride,uint32_t base_stride,bool split_active,bool split_mb){
 return split_mb && split_active && base_stride>=uint32_t(kBaseStride) && base_stride<full_stride?base_stride:full_stride;
}
// A producer can be consumed by the next two cascade builds. Their command
// buffers may submit a frame later; use the same +2 fence coverage as scenes.
inline bool elastic_retired(uint64_t recorded,uint64_t gpu_done,bool cascade){
 const uint64_t lag=cascade?4:2;
 return gpu_done>=recorded && gpu_done-recorded>=lag;
}

// Detach at the next producer boundary; the old allocation is retained separately.
// Submission is required: a pending/unknown recording could execute much later.
inline bool elastic_can_detach(bool recording,int record_state,bool old_pending){
 return !recording && !old_pending && (record_state==1 || record_state==2);
}
// Start retirement at detachment, not the producer's constantly refreshed frame.
// Four fence-covered frames include the two future cascade consumers.
inline bool elastic_detached_retired(uint64_t detached,uint64_t gpu_done){
 return gpu_done>=detached && gpu_done-detached>=4;
}
struct ShrinkLogWindow {
 uint64_t next=0;
 bool due(uint64_t frame,uint64_t window){
  if(frame<next)return false;
  next=frame+window;return true;
 }
};

// Include the existing partition reserves, not just bytes physically captured.
// Preserve room for the wide-layout 40% alpha floor and the alpha percent cap.
inline uint64_t elastic_rt_demand(uint64_t opaque,uint64_t alpha,uint64_t floor,
                                  bool wide_floor,int alpha_percent,
                                  uint64_t reserved_alpha=0,uint64_t physical_opaque=0){
 opaque=(std::min)(opaque,kMeshPoolHardCap);alpha=(std::min)(alpha,kMeshPoolHardCap);
 const uint64_t o=opaque*11/10,a=(std::max)(floor,alpha*3/2);
 // Alpha hysteresis can retain a region much larger than alpha demand.
 // Include physical opaque placement (including alpha fallback/spills) and
 // the actual reserved region, not only the partition policy's next target.
 uint64_t need=(std::max)((std::max)(opaque+alpha,o+a),
                         physical_opaque+reserved_alpha);
 if(wide_floor)need=(std::max)(need,(o*100+59)/60);
 return (std::max)(need,(alpha*100+uint64_t((std::clamp)(alpha_percent,5,90))-1)/
                                      uint64_t((std::clamp)(alpha_percent,5,90)));
}

// The default MB caps (rt_xfb_mb 384, rt_cascade_mb 160) hold one frame's soup at 24-byte
// vertices. A wider capture layout, fixed at startup (rt_hit_material 64, native tileset 80,
// native Hair 88), holds the same triangles in stride/24 times the bytes: a cap left at its
// default follows the stride, rounded (384 -> 1024 and 160 -> 427 at 64 B). A cap the user
// changed is kept as set; 24 bytes or less keeps the default.
inline constexpr int kXfbDefaultMb=384, kCascadeDefaultMb=160;
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

// Existing per-frame alpha partition arithmetic. Physical MB limits and the
// 1.5x alpha / 1.1x opaque reserves are unchanged.
// cap_floor (rt_alpha_floor_yield=1, 2026-11-09): the lowest region the opaque cap and the demand split may
// leave (the stride-scaled rt_alpha_mb), instead of `floor` (40% of the buffer at a wide stride). The
// lowered S8 view (rv1 11:39) had alpha 435 MB of an 818 MB floor region while opaque peaked at 1568 MB of
// the 1229 MB left: 1.27 opaque draws/frame dropped; split by demand both fit (444 / 1603 MB).
inline uint64_t alpha_region(uint64_t size,uint64_t floor,uint64_t current,
                             uint64_t alpha_previous,uint64_t opaque_previous,
                             bool automatic,int max_percent,uint64_t cap_floor=UINT64_MAX){
 const uint64_t yield_floor=(std::min)(floor,cap_floor);
 uint64_t region=current?current:floor;
 if(automatic){
  uint64_t wanted=(std::max)(floor,(alpha_previous*3/2+mib-1)/mib*mib);
  if(wanted>region || wanted*2<region)region=wanted;
  const uint64_t pct_cap=size/100*uint64_t((std::clamp)(max_percent,5,90));
  uint64_t cap=pct_cap;
  uint64_t opaque=opaque_previous*11/10;
  if(opaque && opaque<size)cap=(std::min)(cap,(std::max)(size-opaque,yield_floor));
  // Both reserves do not fit (alpha x1.5 + opaque x1.1 > size): the buffer is split in proportion to the two
  // demands, so both keep the same relative headroom. Before, alpha kept its 50% while opaque had 10%: the inn
  // rotation (lean_regression lr2 2026-11-02 04:42) had alpha 418 MB in a 613 MB region while opaque grew from
  // about 373 to 440 MB of the remaining 411 MB (0.02 draws/frame dropped); split by demand: 541 / 483 MB.
  if(alpha_previous && opaque_previous && alpha_previous*3/2+opaque_previous*11/10>size){
   const double share=double(size)*double(alpha_previous)/double(alpha_previous+opaque_previous);
   const uint64_t split=(std::max)(uint64_t(share)/mib*mib,yield_floor);
   // With the yield the split decides alone: the opaque cap would take its 10% headroom from alpha's need.
   cap=cap_floor!=UINT64_MAX?(std::min)(pct_cap,split):(std::min)(cap,split);
  }
  region=(std::min)(region,cap);
 } else region=floor;
 return region;
}

// Extra elastic capacity must not reduce either stream's fixed-path room.
// In particular crossing the proportional-split threshold can otherwise
// increase alpha reservation faster than total capacity increases.
inline uint64_t elastic_alpha_region(uint64_t size,uint64_t proposed,
                                     uint64_t fixed_size,uint64_t fixed_region){
 if(size<fixed_size)return proposed;
 return (std::clamp)(proposed,fixed_region,size-fixed_size+fixed_region);
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
