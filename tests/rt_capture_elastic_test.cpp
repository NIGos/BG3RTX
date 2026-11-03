#include "../common/rt_capture_budget.h"
#include "../common/vram_guard.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace bg3::capture_budget;
static unsigned checks;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"elastic check %u failed\n",checks);std::exit(1);}}
static uint64_t mb(double value){return uint64_t(std::ceil(value*double(mib)));}
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
 // Reserve-only demand above current must not immediately undo a shrink.
 w.reset();
 for(unsigned i=0;i<ElasticWindow::samples;++i)w.observe(544*mib,352*mib,280*mib);
 check(w.observe(352*mib,368*mib,290*mib)==352*mib);
 check(w.observe(352*mib,448*mib,353*mib)==448*mib); // Real overflow still grows.
 // A high sample must age out, not disappear on replacement or one low sample.
 for(unsigned i=0;i<ElasticWindow::samples-1;++i)
  check(w.observe(544*mib,272*mib,210*mib)==544*mib);
 check(w.observe(544*mib,272*mib,210*mib)==272*mib);
 w.reset();
 for(unsigned i=0;i<ElasticWindow::samples+1;++i)
  check(w.observe(352*mib,320*mib,250*mib)==352*mib); // Too little saving to shrink.
 w.observe(560*mib,224*mib);w.reset();check(w.count==0 && w.peak==0);
 for(unsigned i=0;i<ElasticWindow::samples+1;++i)
  check(w.observe(560*mib,427*mib,500*mib)==560*mib); // Card-cap drop must not shrink below demand.
 // ds3: every ring slot held 320 MiB, then 24 empty captures shrank it
 // to 16 MiB. Returning casters overflowed before the next producer boundary.
 // Inactive duration is unbounded: even a long pause must preserve capacity.
 CascadeElasticWindow cascades[3];
 uint64_t cascade_sizes[3]={320*mib,320*mib,320*mib};
 for(unsigned frame=0;frame<3*CascadeElasticWindow::samples*10;++frame){
  const unsigned slot=frame%3;
  cascade_sizes[slot]=cascades[slot].observe(cascade_sizes[slot],
      elastic_capacity(0,cap,cap),0);
  check(cascade_sizes[slot]==320*mib);
 }
 for(unsigned slot=0;slot<3;++slot){
  const uint64_t returning=250*mib;
  check(returning<=cascade_sizes[slot]); // No first-return draw rejection.
  check(cascades[slot].observe(cascade_sizes[slot],
      elastic_capacity(returning,cap,cap),returning)==320*mib);
  // Real smaller active demand still releases memory after a full window.
  for(unsigned i=1;i<CascadeElasticWindow::samples;++i)
   check(cascades[slot].observe(320*mib,128*mib,100*mib)==320*mib);
  check(cascades[slot].observe(320*mib,128*mib,100*mib)==128*mib);
 }
 CascadeElasticWindow cascade;
 // Empty frames also cancel an incomplete low-demand shrink observation.
 for(unsigned i=1;i<CascadeElasticWindow::samples;++i)
  check(cascade.observe(320*mib,128*mib,100*mib)==320*mib);
 check(cascade.observe(320*mib,16*mib,0)==320*mib && cascade.count==0);
 check(cascade.observe(320*mib,128*mib,100*mib)==320*mib);
 // A tighter card budget neither shrinks below active demand nor authorizes
 // growth beyond the guard cap. Overflow is included in the observed demand.
 cascade.reset();
 for(unsigned i=0;i<2*CascadeElasticWindow::samples;++i)
  check(cascade.observe(320*mib,elastic_capacity(250*mib,cap,128*mib),250*mib)==320*mib);
 check(cascade.observe(128*mib,elastic_capacity(250*mib,cap,128*mib),250*mib)==128*mib);
 check(cascade.observe(128*mib,elastic_capacity(250*mib,cap,cap),250*mib)==320*mib);
 // A turn's 271 MiB demand must remain covered throughout the 120-sample
 // window, rather than shrinking to 256 MiB after only 24 quieter samples.
 static_assert(CascadeElasticWindow::samples==RtElasticWindow::samples);
 cascade.reset();
 uint64_t turn_capacity=cascade.observe(256*mib,elastic_capacity(271*mib,cap,cap),271*mib);
 check(turn_capacity>=271*mib);
 for(unsigned i=1;i<CascadeElasticWindow::samples;++i){
  turn_capacity=cascade.observe(turn_capacity,256*mib,200*mib);
  check(turn_capacity>=271*mib);
 }
 check(cascade.observe(turn_capacity,elastic_capacity(271*mib,cap,cap),271*mib)>=271*mib);
 // su1 summary-envelope trace, not a per-frame reconstruction: reserve from
 // the 427 MiB preset must cover the first 206.18 -> 301.71 MiB turn BEFORE
 // feedback. Thereafter retain the measured positive step over quiet windows.
 CaptureHeadroom ch;
 cascade.reset();
 uint64_t size_c=427*mib;
 for(unsigned i=0;i<120;++i)
  size_c=cascade.observe(size_c,ch.capacity(mb(206.18),cap,cap,3,427*mib),mb(206.18));
 check(size_c==320*mib && size_c>=mb(301.71));
 size_c=cascade.observe(size_c,ch.capacity(mb(301.71),cap,cap,3,427*mib),mb(301.71));
 check(size_c==592*mib); // 301.71 + 3 * (301.71 - 206.18), rounded to 16.
 const uint64_t learned=ch.step;
 for(unsigned i=0;i<240;++i){
  const uint64_t d=mb(i%2?301.71:206.18);
  check(d<=size_c); // Test admission BEFORE computing next producer's target.
  size_c=cascade.observe(size_c,ch.capacity(d,cap,cap,3,427*mib),d);
 }
 const uint64_t busy=size_c;
 for(unsigned i=1;i<120;++i){
  size_c=cascade.observe(size_c,ch.capacity(20*mib,cap,cap,3,427*mib),20*mib);
  check(size_c==busy);
 }
 size_c=cascade.observe(size_c,ch.capacity(20*mib,cap,cap,3,427*mib),20*mib);
 check(size_c==320*mib && size_c<427*mib && ch.step==learned);
 check(ch.capacity(UINT64_MAX,cap,cap,3,427*mib)==cap);
 check(ch.capacity(100*mib,128*mib,cap,3,427*mib)==128*mib);
 check(ch.capacity(100*mib,cap,128*mib,3,427*mib,592*mib)==592*mib);
 // Primary producer: five frame increments cover retirement + next boundary.
 // Repeated steps fit while a replacement remains fence-blocked for 4 frames.
 CaptureHeadroom rh;
 uint64_t rt_size=1024*mib;
 rh.capacity(600*mib,cap,cap,1,1024*mib);
 uint64_t pending=rh.capacity(680*mib,cap,cap,1,1024*mib);
 for(unsigned i=1;i<=4;++i){
  const uint64_t d=(680+80*i)*mib;
  check(d<=rt_size);
  pending=rh.capacity(d,cap,cap,1,1024*mib);
 }
 rt_size=pending;
 check(rt_size==1408*mib); // 1000 + 5 * 80, beyond either quarter reserve.
 check(rh.capacity(1080*mib,cap,cap,1,1024*mib)>rt_size);
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
 // su1: the 120-frame summary reports opaque peak 394.2 MiB, alpha
 // demand 118.5 MiB, region 364 MiB and FINAL capacity 912 MiB. The same
 // window includes 544 -> 720 -> 912 MiB replacements. These are not
 // simultaneous maxima/capacity; the final 548 MiB opaque room hides drops.
 const uint64_t logged_opaque=mb(394.2),logged_alpha=mb(118.5);
 check(912*mib-364*mib>logged_opaque);
 const uint64_t logged_need=elastic_rt_demand(logged_opaque,logged_alpha,
     171*mib,true,60,364*mib,logged_opaque);
 check(logged_need>=logged_opaque+364*mib);
 // A synthetic instantaneous replay using the logged low capacity and
 // retained alpha reservation exposes the hazard; the log does not give
 // the failing frame's exact partition. The fixed 1024 MiB split fits.
 check(544*mib-364*mib<logged_opaque);
 const uint64_t fixed_region=alpha_region(1024*mib,409*mib,364*mib,
     logged_alpha,logged_opaque,true,60);
 check(1024*mib-fixed_region>=logged_opaque && fixed_region>=logged_alpha);
 RtElasticWindow rt;
 // Even sustained near-zero demand cannot shrink below the fixed baseline.
 for(unsigned i=0;i<240;++i){
  const uint64_t want=elastic_rt_capacity(0,cap,cap,1024*mib);
  check(rt.observe(1024*mib,want)==1024*mib);
 }
 // Keep the last 120-frame maximum plus its 25% margin after growth;
 // the former 24-frame window discarded it before the summary was printed.
 rt.reset();
 const uint64_t spike=elastic_rt_demand(600*mib,logged_alpha,171*mib,true,60,
     364*mib,600*mib);
 const uint64_t high=elastic_rt_capacity(spike,cap,cap,1024*mib);
 check(high>=spike+spike/4 && high>1024*mib);
 rt.observe(cap,high);
 for(unsigned i=1;i<119;++i)check(rt.observe(cap,1024*mib)==cap);
 check(rt.observe(cap,1024*mib)==high);
 check(rt.observe(cap,1024*mib)==1024*mib);
 // Primary reserve growth precedes overflow, rather than waiting for demand
 // to exceed current capacity. Physical placement includes opaque fallbacks.
 rt.reset();
 const uint64_t placed=elastic_rt_demand(400*mib,50*mib,171*mib,true,60,
     600*mib,500*mib);
 check(placed>=1100*mib);
 const uint64_t grow=elastic_rt_capacity(placed,cap,cap,1024*mib);
 check(rt.observe(1024*mib,grow,placed,true)==grow && grow>1024*mib);
 check(elastic_rt_capacity(1000*mib,cap,1100*mib,1024*mib,1408*mib)==1408*mib);
 check(elastic_rt_capacity(0,cap,1100*mib,1024*mib,1408*mib)==1024*mib);
 check(elastic_rt_capacity(0,cap,cap,384*mib)==384*mib);
 check(elastic_rt_capacity(0,cap,cap,1408*mib)==1408*mib);
 // Inn split: increasing capacity across the reserve threshold used to
 // REDUCE opaque room (1024-541=483 -> 1040-627=413 MiB).
 const uint64_t baseline=alpha_region(1024*mib,409*mib,613*mib,418*mib,373*mib,true,60);
 const uint64_t larger=alpha_region(1040*mib,416*mib,613*mib,418*mib,373*mib,true,60);
 check(1040*mib-larger<1024*mib-baseline);
 const uint64_t safe=elastic_alpha_region(1040*mib,larger,1024*mib,baseline);
 check(1040*mib-safe>=1024*mib-baseline && safe>=baseline);
 check(elastic_alpha_region(1024*mib,364*mib,1024*mib,fixed_region)==fixed_region);
 // Low-memory guard refuses new allocation, including replacement overlap.
 bg3::vram_guard::Guard guard;guard.sample(31000*mib,31419*mib);
 check(guard.low && !guard.allow(size));
 guard.sample(10000*mib,31419*mib);check(!guard.low && guard.allow(size));
 std::printf("RT elastic capture: %u CPU checks; no GPU claim\n",checks);
}
