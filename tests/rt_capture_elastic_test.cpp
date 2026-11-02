#include "../common/rt_capture_budget.h"
#include "../common/vram_guard.h"
#include <cstdio>
#include <cstdlib>
using namespace bg3::capture_budget;
static unsigned checks;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"elastic check %u failed\n",checks);std::exit(1);}}
int main(){
 const uint64_t cap=kMeshPoolHardCap;
 // vg1 cascade: 3 x 560 MiB reserved for 169 MiB peak -> 3 x 224 MiB.
 check(elastic_capacity(169*mib,cap,cap)==224*mib);
 check(elastic_capacity(0,cap,cap)==16*mib);
 check(elastic_capacity(UINT64_MAX,cap,cap)==cap);
 check(elastic_capacity(1182*mib,cap,cap)>1024*mib);
 check(elastic_capacity(1182*mib,1200*mib,cap)==1200*mib);
 const uint64_t small=bg3::vram_guard::cap(bg3::vram_guard::kCascade,12000*mib);
 check(elastic_capacity(1182*mib,cap,small)==small);
 // Shrink must cover a full window, retaining its maximum (not its mean).
 ElasticWindow w;
 for(unsigned i=1;i<ElasticWindow::samples;++i)check(w.observe(560*mib,(i==2?240:224)*mib)==560*mib);
 check(w.observe(560*mib,224*mib)==240*mib);
 check(w.observe(560*mib,224*mib)==240*mib); // Keep pending shrink until a retired slot exists.
 w.reset(); // Successful replacement begins a new observation window.
 for(unsigned i=1;i<ElasticWindow::samples;++i)check(w.observe(560*mib,224*mib)==560*mib);
 check(w.observe(560*mib,800*mib)==800*mib); // Overflow grows immediately and resets shrink.
 for(unsigned i=1;i<ElasticWindow::samples;++i)check(w.observe(560*mib,224*mib)==560*mib);
 check(w.observe(560*mib,224*mib)==224*mib);
 w.observe(560*mib,224*mib);w.reset();check(w.count==0 && w.peak==0);
 for(unsigned i=0;i<ElasticWindow::samples+1;++i)
  check(w.observe(560*mib,427*mib,500*mib)==560*mib); // Card-cap drop must not shrink below demand.
 // Retired producers only; late submission and F+2 cascade consumers covered.
 check(!elastic_retired(10,11,false));check(elastic_retired(10,12,false));
 check(!elastic_retired(10,13,true));check(elastic_retired(10,14,true));
 check(!elastic_retired(UINT64_MAX,0,true));check(!elastic_retired(10,9,false));
 // Continuous producers never meet the old retirement predicate. Detaching
 // starts an independent clock, so new captures cannot postpone reclamation.
 for(uint64_t frame=100;frame<200;++frame){
  check(!elastic_retired(frame-1,frame-2,false));
  check(!elastic_retired(frame-3,frame-2,true));
  check(elastic_can_detach(false,1,false));
 }
 check(!elastic_can_detach(true,1,false));
 check(!elastic_can_detach(false,0,false));
 check(!elastic_can_detach(false,-1,false));
 check(!elastic_can_detach(false,1,true)); // One retained allocation per producer.
 check(elastic_can_detach(false,2,false));
 check(!elastic_detached_retired(100,99));
 check(!elastic_detached_retired(100,103));
 check(elastic_detached_retired(100,104));
 check(!elastic_detached_retired(UINT64_MAX,0));
 ShrinkLogWindow log;
 check(log.due(100,24));check(!log.due(101,24));
 check(!log.due(123,24));check(log.due(124,24));
 // 1182 MiB opaque plus alpha: exceed the old 1024 MiB cap without losing
 // opaque room to the wide-layout 40% alpha floor. Still obey the hard cap.
 const uint64_t demand=elastic_rt_demand(1182*mib,169*mib,171*mib,true,60);
 const uint64_t size=elastic_capacity(demand,cap,cap);
 check(size==cap);
 const uint64_t floor=alpha_floor(171*mib,size,true,64);
 const uint64_t region=alpha_region(size,floor,0,169*mib,1182*mib,true,60);
 check(size-region>=1182*mib && region>=169*mib);
 const uint64_t lower=elastic_capacity(elastic_rt_demand(200*mib,100*mib,171*mib,true,60),cap,cap);
 check(lower<1024*mib);
 const uint64_t low_floor=alpha_floor(171*mib,lower,true,64);
 const uint64_t low_region=alpha_region(lower,low_floor,0,100*mib,200*mib,true,60);
 check(lower-low_region>=200*mib && low_region>=100*mib);
 // Low-memory guard refuses new allocation, including replacement overlap.
 bg3::vram_guard::Guard guard;guard.sample(31000*mib,31419*mib);
 check(guard.low && !guard.allow(size));
 guard.sample(10000*mib,31419*mib);check(!guard.low && guard.allow(size));
 std::printf("RT elastic capture: %u CPU checks; no GPU claim\n",checks);
}
