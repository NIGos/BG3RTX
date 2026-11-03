#include "../common/rt_capture_budget.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
using namespace bg3::capture_budget;
static unsigned checks;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"capture budget check %u failed\n",checks);std::exit(1);}}
static uint64_t upper(double mb){return uint64_t(std::ceil(mb*double(mib)));}
int main(){
 // hq4 load: native 88-byte cache admission must account for BLAS and
 // pending pool ownership before uploads; optional refusal retains capture.
 check(mesh_cache_admit(819*mib,465*mib,500*mib,200*mib,2048*mib));
 check(!mesh_cache_admit(819*mib,465*mib,1000*mib,200*mib,2048*mib));
 check(!mesh_cache_admit(kMeshPoolHardCap,0,1,0,UINT64_MAX));
 check(mesh_cache_admit(100,200,300,400,1000));
 check(!mesh_cache_admit(100,200,300,401,1000));
 check(!mesh_cache_admit(UINT64_MAX,1,1,1,1000));
 check(!mesh_cache_admit(1,UINT64_MAX,1,1,1000));
 check(!mesh_cache_admit(1,1,UINT64_MAX,1,1000));
 check(!mesh_cache_admit(1,1,1,UINT64_MAX,1000));
 // Golden existing-policy outputs, including hysteresis and fixed-MB semantics.
 check(alpha_region(384*mib,64*mib,0,0,0,true,60)==64*mib);
 check(alpha_region(384*mib,64*mib,64*mib,100*mib,100*mib,true,60)==150*mib);
 check(alpha_region(384*mib,64*mib,200*mib,100*mib,100*mib,true,60)==200*mib);
 check(alpha_region(384*mib,64*mib,300*mib,50*mib,0,true,60)==75*mib);
 check(alpha_region(384*mib,64*mib,300*mib,300*mib,0,false,60)==64*mib);
 // Both reserves do not fit: split by demand (inn lr2 2026-11-02: alpha 418, opaque 373 MB of 1024).
 {const uint64_t r=alpha_region(1024*mib,409*mib,613*mib,418*mib,373*mib,true,60);
  check(r==541*mib && 1024*mib-r>=440*mib);   // opaque keeps room for the 440 MB peak that overflowed
  check(alpha_region(1024*mib,409*mib,613*mib,418*mib,700*mib,true,60)==409*mib);}   // never below the floor
 check(alpha_region(1024*mib,409*mib,0,200*mib,300*mib,true,60)==409*mib);   // reserves fit: unchanged policy
 check(opaque_partition_demand(700*mib,400*mib,false)==700*mib); // 24-byte path unchanged.
 check(opaque_partition_demand(700*mib,400*mib,true)==300*mib);
 check(opaque_partition_demand(700*mib,0,true)==700*mib);
 check(opaque_partition_demand(700*mib,701*mib,true)==0); // Saturate; never wrap the reservation.
 // Default caps follow the startup stride; set caps and narrow layouts are kept.
 check(effective_mb(kXfbDefaultMb,kXfbDefaultMb,64)==1024);
 check(effective_mb(kCascadeDefaultMb,kCascadeDefaultMb,64)==427);
 check(effective_mb(kXfbDefaultMb,kXfbDefaultMb,88)==1408); // the hair jobs' hand-set sizes
 check(effective_mb(kCascadeDefaultMb,kCascadeDefaultMb,88)==587);
 check(effective_mb(kXfbDefaultMb,kXfbDefaultMb,24)==384);
 check(effective_mb(kAlphaDefaultMb,kAlphaDefaultMb,64)==171 && effective_mb(kAlphaDefaultMb,kAlphaDefaultMb,24)==64);
 check(effective_mb(96,kAlphaDefaultMb,64)==96); // set by hand: kept
 check(effective_mb(kXfbDefaultMb,kXfbDefaultMb,16)==384);
 check(effective_mb(1408,kXfbDefaultMb,64)==1408);
 check(effective_mb(200,kCascadeDefaultMb,64)==200);
 check(!corrected_partition_feedback(16));
 check(!corrected_partition_feedback(24));
 check(corrected_partition_feedback(40));
 check(corrected_partition_feedback(64));
 check(corrected_partition_feedback(80));
 check(corrected_partition_feedback(88));
 check(!corrected_partition_feedback(0));
 check(!corrected_partition_feedback(32));
 check(!corrected_partition_feedback(128));
 check((1024*mib)/64==(384*mib)/24); // Explicit material-only budget; not an automatic resize.

 // Root supplied maxima over 47 actual log windows: A660.8/O634.5 MiB.
 // The logs print one decimal; use upper half-code bounds, not the last window.
 // O includes alpha fallback, so treating it as true O below is a conservative
 // synthetic policy hazard, NOT a claim that this simultaneous live state occurred.
 const uint64_t a=upper(660.85),o=upper(634.55),size=1408*mib,floor=64*mib;
 check(size/88==(384*mib)/24); // Exact original geometric vertex capacity.
 check(alpha_region(384*mib,floor,floor,a,o,true,60)<a);
 check(alpha_region(1024*mib,floor,floor,a,o,true,60)<a);
 auto partition=alpha_region(size,floor,floor,a,o,true,60);
 const auto base=(size-partition)&~uint64_t(255);
 check(size-base>=a && base>=o);
 check(base+255>=o*11/10); // Existing 256-byte base alignment can consume at most255 guard bytes.
 check(partition<(a*3/2+mib-1)/mib*mib); // 1408 is not full 1.5x alpha headroom.
 const auto reserved=alpha_region(1691*mib,992*mib,992*mib,a,o,true,60);
 check(reserved>=992*mib && 1691*mib-reserved>=o*11/10);
 const auto boot=alpha_region(size,661*mib,0,0,0,true,60);
 check(boot>=a && size-boot>=o); // Explicit data-derived startup floor.

 // Before the fix, alpha overflow contaminates the next opaque reserve and
 // creates a self-sustaining 64 MiB partition despite sufficient total space.
 uint64_t old_region=floor,new_region=floor;
 for(unsigned frame=0;frame<4;++frame){
  auto old_fallback=a>old_region?a-old_region:0;
  auto old_total_opaque=o+old_fallback;
  old_region=alpha_region(size,floor,old_region,a,opaque_partition_demand(old_total_opaque,old_fallback,false),true,60);
  check(old_region==floor);
  auto fallback=a>new_region?a-new_region:0;
  auto total_opaque=o+fallback;
  auto intrinsic=opaque_partition_demand(total_opaque,fallback,true);
  check(intrinsic==o);
  new_region=alpha_region(size,floor,new_region,a,intrinsic,true,60);
  check(new_region>=a && size-new_region>=o);
 }

 // The material-only64 layout must recover too. Scale the synthetic demands,
 // not configured MB limits; no live64 capture or performance claim is implied.
 uint64_t material_region=floor;
 const auto material_a=(a*64+87)/88,material_o=(o*64+87)/88;
 for(unsigned frame=0;frame<4;++frame){
  auto fallback=material_a>material_region?material_a-material_region:0;
  auto intrinsic=opaque_partition_demand(material_o+fallback,fallback,corrected_partition_feedback(64));
  check(intrinsic==material_o);
  material_region=alpha_region(1024*mib,floor,material_region,material_a,intrinsic,true,60);
  check(material_region>=material_a && 1024*mib-material_region>=material_o);
 }

 // New native tileset layouts must not resurrect the same double-counting.
 // Synthetic demands preserve the tested geometric load by scaling byte counts;
 // explicit configured MB limits are inputs, never rewritten by the helper.
 for(uint32_t stride:{40u,80u}){
  const auto demand_a=(a*stride+87)/88,demand_o=(o*stride+87)/88;
  const uint64_t capacity=uint64_t(384)*mib*stride/24;
  uint64_t legacy=floor,corrected=floor;
  for(unsigned frame=0;frame<4;++frame){
   auto lost=demand_a>legacy?demand_a-legacy:0;
   legacy=alpha_region(capacity,floor,legacy,demand_a,opaque_partition_demand(demand_o+lost,lost,false),true,60);
   check(legacy==floor);
   auto fallback=demand_a>corrected?demand_a-corrected:0;
   const auto intrinsic=opaque_partition_demand(demand_o+fallback,fallback,corrected_partition_feedback(stride));
   check(intrinsic==demand_o);
   corrected=alpha_region(capacity,floor,corrected,demand_a,intrinsic,true,60);
   check(corrected>=demand_a && capacity-corrected>=demand_o);
   check(alpha_region(capacity,floor,corrected,demand_a,intrinsic,false,60)==floor);
  }
 }

 // Partition overflow with free opaque storage must retain a direct alpha
 // record. The reservation is transactional: failures leave the output/cursors.
 uint64_t offset=999;
 check(alpha_spill_offset(true,true,true,true,100,200,100,offset) && offset==100);
 for(unsigned missing=0;missing<4;++missing){
  bool conditions[4]={true,true,true,true};conditions[missing]=false;offset=999;
  check(!alpha_spill_offset(conditions[0],conditions[1],conditions[2],conditions[3],100,200,100,offset) && offset==999);
 }
 for(auto request:{std::pair{201ull,1ull},std::pair{100ull,101ull},std::pair{100ull,0ull},
                   std::pair{UINT64_MAX,UINT64_MAX}}){
  offset=999;check(!alpha_spill_offset(true,true,true,true,request.first,200,request.second,offset) && offset==999);
 }
 // Overflow-safe last-byte reservation and independent storage cursors.
 check(alpha_spill_offset(true,true,true,true,UINT64_MAX-1,UINT64_MAX,1,offset) && offset==UINT64_MAX-1);
 uint64_t opaque_cursor=100,alpha_cursor=900,opaque_end=200;
 check(alpha_spill_offset(true,true,true,true,opaque_cursor,opaque_end,60,offset));
 opaque_cursor+=60;check(opaque_cursor==160 && alpha_cursor==900);
 check(!alpha_spill_offset(true,true,true,true,opaque_cursor,opaque_end,60,offset));
 check(alpha_spill_offset(true,true,true,true,opaque_cursor,opaque_end,40,offset));
 opaque_cursor+=40;check(opaque_cursor==opaque_end && alpha_cursor==900);
 check(opaque_partition_demand(opaque_cursor,100,true)==100); // Spill is alpha demand only.

 // Existing shader/host addressing: InstInfo base and HLSL byte offsets are
 // uint32, metadata stores stride in eight bits, conversion dispatch is256 wide.
 // These are representability checks, NOT runtime device-memory availability.
 check(size<std::numeric_limits<uint32_t>::max());
 check(88u<256u && size/264u<std::numeric_limits<uint32_t>::max());
 const uint64_t triangles=size/(3*88),groups=(triangles+255)/256;
 check(groups<=65535 && groups*256<=std::numeric_limits<uint32_t>::max());
 check(triangles*3*88<=size && triangles*3*88<=std::numeric_limits<uint32_t>::max());
 check((size-partition)-base<256);
 check(storage_range_supported(size,size));
 check(storage_range_supported(size,uint64_t(1)<<31));
 check(!storage_range_supported(size,size-1));
 check(!storage_range_supported(size,0));
 check(!storage_range_supported(0,size));
 // Mesh pool: doubles 256 -> 512 -> 1024, then stops below 2^31 (2047 MB), never past the device range.
 {
  const uint64_t M=mib,G=1024*M;
  check(mesh_pool_capacity(0,10*M,256*M,3072*M,0xFFFFFFFFull)==256*M);
  check(mesh_pool_capacity(256*M,10*M,256*M,3072*M,0xFFFFFFFFull)==512*M);
  check(mesh_pool_capacity(G,10*M,256*M,3072*M,0xFFFFFFFFull)==kMeshPoolHardCap);   // was 2048 MB
  check(mesh_pool_capacity(G,10*M,256*M,3072*M,0xFFFFFFFFull)<(uint64_t(1)<<31));
  check(mesh_pool_capacity(kMeshPoolHardCap-M,2*M,256*M,3072*M,0xFFFFFFFFull)==0);   // full: uncached
  check(mesh_pool_capacity(G,10*M,256*M,1536*M,0xFFFFFFFFull)==1536*M);              // the ini max
  check(mesh_pool_capacity(G,10*M,256*M,3072*M,1200*M)==1200*M);                     // the device range
  check(mesh_pool_capacity(512*M,600*M,256*M,3072*M,0xFFFFFFFFull)==2*G-M);         // 1112 MB -> clamped
  // Cascade: 427 MB full with 30 MB left out -> 457 * 1.25 = 571.25 -> 576 MB; capped; never shrinks.
  check(cascade_grow(427*M,457*M,0xFFFFFFFFull)==576*M);
  check(cascade_grow(427*M,3000*M,0xFFFFFFFFull)==kMeshPoolHardCap);
  check(cascade_grow(427*M,457*M,512*M)==512*M);
  check(cascade_grow(800*M,457*M,0xFFFFFFFFull)==800*M);
  // Alpha floor at 64 B: 171 MB -> 409 MB of a 1024 MB buffer (S8 load frame wanted 223.8 MB); 24 B and a set
  // rt_alpha_mb unchanged; the region still yields to the opaque demand above the floor.
  check(alpha_floor(171*M,G,true,64)==409*M);
  check(alpha_floor(64*M,G,true,24)==64*M);
  check(alpha_floor(100*M,G,false,64)==100*M);
  check(alpha_region(G,409*M,0,0,0,true,60)==409*M);
  check(alpha_region(G,409*M,409*M,2*M,100*M,true,60)==409*M);   // loading: no shrink below the floor
 }
 std::printf("RT capture budget: %u CPU checks; explicit MB limits unchanged; no GPU claim\n",checks);
}
