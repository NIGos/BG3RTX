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
 // Golden existing-policy outputs, including hysteresis and fixed-MB semantics.
 check(alpha_region(384*mib,64*mib,0,0,0,true,60)==64*mib);
 check(alpha_region(384*mib,64*mib,64*mib,100*mib,100*mib,true,60)==150*mib);
 check(alpha_region(384*mib,64*mib,200*mib,100*mib,100*mib,true,60)==200*mib);
 check(alpha_region(384*mib,64*mib,300*mib,50*mib,0,true,60)==75*mib);
 check(alpha_region(384*mib,64*mib,300*mib,300*mib,0,false,60)==64*mib);
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
 std::printf("RT capture budget: %u CPU checks; explicit MB limits unchanged; no GPU claim\n",checks);
}
