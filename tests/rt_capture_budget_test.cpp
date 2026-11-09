#include "../common/rt_capture_budget.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>
using namespace bg3::capture_budget;
static unsigned checks;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"capture budget check %u failed\n",checks);std::exit(1);}}
static uint64_t upper(double mb){return uint64_t(std::ceil(mb*double(mib)));}

// rt_mesh_budget_own: a turning view over a ring of static instanceable meshes, plus per-draw BLAS of the
// non-instanceable (pattern) draws and the pool's other users. Sizes from rv2 (2026-11-09 14:58:21, S8 zoomed
// out): 241 new meshes held 0.86M triangles (about 3600 each); pool vertices 64 B (192 B per triangle);
// 4.0M captured triangles held 1.1-2.9 GB of non-mesh BLAS (about 400 B per triangle with pending ranges),
// 2.0M of them pattern (800 MB); 844 MB pool with 55 meshes (about 700 MB holds, bounds and bases).
// Compacted mesh BLAS about 80 B per triangle. The layer's order per build: admission against the charged
// bytes, then the LRU pass when due (meshes drawn in the last 8 builds are never evicted).
struct TurnResult{double visible_cached;uint64_t admitted_late,max_charged_after_lru;};
static TurnResult turn_model(bool own,uint32_t ring){
 const uint64_t budget=2048*mib,pool_other=700*mib,pattern_as=800*mib,tris=3600;
 const uint64_t mesh_pool=tris*3*64,mesh_as=tris*80,dyn_as=tris*400;
 const uint32_t view=600,step=10,builds=ring/step*4;   // four laps; the last two are measured
 std::vector<int64_t> last(ring,-1);   // -1: not cached
 uint64_t dyn_prev=0,admitted_late=0,max_charged=0,cached_seen=0,seen=0;
 for(uint32_t b=0;b<builds;++b){
  const uint64_t now=b+1000;
  uint64_t pool=pool_other,cached_as=0;
  for(uint32_t i=0;i<ring;++i)if(last[i]>=0){pool+=mesh_pool;cached_as+=mesh_as;}
  uint64_t all_as=pattern_as+cached_as+dyn_prev,dyn=0;
  const bool late=b>=builds/2;
  bool declined=false;
  for(uint32_t v=0;v<view;++v){
   const uint32_t i=(b*step+v)%ring;
   if(last[i]>=0)last[i]=int64_t(now);
   else if(mesh_cache_admit(pool,mesh_cache_charged_as(all_as,cached_as,own),mesh_pool,mesh_as,budget)){
    last[i]=int64_t(now);pool+=mesh_pool;cached_as+=mesh_as;all_as+=mesh_as;admitted_late+=late;
   }else{dyn+=dyn_as;declined=true;}
   if(late){++seen;cached_seen+=last[i]>=0;}   // cached (or admitted) in this build, else captured
  }
  dyn_prev=dyn;
  all_as=pattern_as+cached_as+dyn;
  const uint64_t used=mesh_cache_charged_as(all_as,cached_as,own)+pool;
  if(mesh_cache_lru_due(used,budget,declined,own,now)){
   std::vector<std::pair<int64_t,uint32_t>> old;
   for(uint32_t i=0;i<ring;++i)if(last[i]>=0 && uint64_t(last[i])+8<now)old.push_back({last[i],i});
   std::sort(old.begin(),old.end());
   std::vector<LruMesh> plan;
   for(auto& o:old)plan.push_back({uint64_t(o.first),mesh_pool+mesh_as});
   const size_t n=mesh_cache_lru_count(plan,used,budget,36000,now);
   for(size_t k=0;k<n;++k){last[old[k].second]=-1;pool-=mesh_pool;cached_as-=mesh_as;all_as-=mesh_as;}
  }
  if(late)max_charged=(std::max)(max_charged,mesh_cache_charged_as(all_as,cached_as,own)+pool);
 }
 return {double(cached_seen)/double(seen),admitted_late,max_charged};
}

// rv3 starvation (19:15:28-34, S8 turns then a stop, rt_mesh_budget_own=1): the shared pool (2047 MB) and BLAS chunks
// (VRAM guard cap 4013 MB) with the frame's other users. Per build: new native hair draws need fresh bounds in the
// pool (old ones freed); visible uncached meshes are captured with a per-draw BLAS (refused: left out) and, while
// the camera stands, a 64-byte hold snapshot (refused: skipped). Freed ranges retire only behind a newer built
// scene (as_chunks::retired, oldest_live). Old own rule (charged 2048 MB budget only): the cache fills the pool,
// the first stop's holds take the rest, the hair bounds fail, the build fails, nothing retires again: deadlock.
// headroom=true adds the shares (others' peak + 25%) and leaves a draw out instead of failing the build.
struct StarveResult{uint32_t failed_last,built_last,left_out;double visible_cached;uint64_t max_pool,max_as;};
static StarveResult starve_model(bool headroom){
 // rv3 19:15:28: 433 meshes held 3.6M triangles (about 8000 each); pattern draws about 2.0M triangles (800 MB of
 // per-draw BLAS; 600 MB of standing holds at 64 bytes with the small uncached draws, so that the other users held
 // about 1.1-1.3 GB of the full pool at 19:15:33); the turn swept more static meshes than the pool holds. Other pool
 // users while turning (grooms, OMM indices, bases, kept holds): rv3 19:15:22, pool 733 MB with 312 meshes of 0.6M
 // triangles (about 115 MB of vertices), so about 600 MB: 450 MB fixed here plus bounds and pending ranges.
 const uint64_t pool_cap=kMeshPoolHardCap,as_cap=4013*mib,budget=2048*mib,tris=8000;
 const uint64_t mesh_pool=tris*3*64,mesh_as=tris*80,dyn_as=tris*400,hold=tris*3*64;
 const uint64_t pattern_as=800*mib,pattern_hold=600*mib,bounds=8*mib,fixed_pool=450*mib;
 const uint32_t ring=1000,view=300,builds=1200;
 std::vector<int64_t> last(ring,-1);
 // Pending ranges: [0] the other users', [1] evicted cache meshes' (counted on the cache's side, layer evict()).
 struct Pend{uint64_t frame,pool,as;bool cache;};
 std::vector<Pend> pend;
 uint64_t cache_pool=0,cache_as=0,dyn=0,holds=0,bounds_live=0,pend_pool[2]={},pend_as[2]={},max_pool=0,max_as=0;
 uint64_t pool_refused=0,as_refused=0,seen=0,cached_seen=0,last_built=0;
 uint32_t failed_last=0,built_last=0,left_out=0,pos=0;
 DemandPeak pool_peak,as_peak;
 uint64_t pool_share=UINT64_MAX,as_share=UINT64_MAX,hold_limit=UINT64_MAX,want_extra=0;
 auto pool_used=[&]{return fixed_pool+cache_pool+holds+bounds_live+pend_pool[0]+pend_pool[1];};
 auto as_used=[&]{return cache_as+dyn+pend_as[0]+pend_as[1];};
 auto free_later=[&](uint64_t now,uint64_t pool,uint64_t as,bool cache){
  if(!pool && !as)return;
  pend.push_back({now,pool,as,cache});pend_pool[cache]+=pool;pend_as[cache]+=as;
 };
 auto evict_mesh=[&](uint32_t i,uint64_t now){
  last[i]=-1;cache_pool-=mesh_pool;cache_as-=mesh_as;free_later(now,mesh_pool,mesh_as,true);
 };
 for(uint32_t b=0;b<builds;++b){
  const uint64_t now=b+1000;
  const bool still=(b/150)%2==1;   // 150 builds turning, 150 standing
  if(!still)pos+=10;
  bool ok=true,declined=false,share_declined=false;
  // 1. mandatory hair bounds, allocated first; the previous ones are freed. The build after the first standing
  // one brings a group of characters into view (64 MB of new bounds and groom points): a mandatory request at a
  // full pool, larger than what retires from before the last built scene.
  free_later(now,bounds_live,0,false);bounds_live=0;
  // Bounds not granted stay wanted: the hair draws ask again every build.
  if(b%300==151)want_extra=64*mib;
  const uint64_t want=bounds+want_extra;
  if(pool_used()+want<=pool_cap){bounds_live=want;want_extra=0;}
  else{pool_refused+=want;if(headroom)++left_out;else ok=false;}   // old: the whole build fails
  // 2. holds: snapshotted draw by draw at the first standing build (optional, they fill the pool to the brim),
  // kept while standing, released on moving. Visible meshes: cached, admitted, or captured with a per-draw BLAS.
  const bool snap=ok && still && holds==0;
  uint64_t new_dyn=pattern_as,new_holds=0;
  if(!still){free_later(now,holds,0,false);holds=0;}
  // Holds are optional: never counted as demand; with headroom they also keep out of the mandatory reserve.
  const uint64_t hold_cap=headroom?(std::min)(pool_cap,hold_limit):pool_cap;
  if(snap)for(uint64_t h=0;h<pattern_hold;h+=mib)if(pool_used()+new_holds+mib<=hold_cap)new_holds+=mib;
  for(uint32_t v=0;v<view && ok;++v){
   const uint32_t i=(pos+v)%ring;
   if(last[i]>=0)last[i]=int64_t(now);
   else if(!mesh_cache_admit(pool_used()+new_holds,cache_as,mesh_pool,mesh_as,budget))declined=true;
   else if(headroom && !mesh_cache_fits(cache_pool,mesh_pool,pool_share,cache_as,mesh_as,as_share))
    share_declined=true;
   else if(pool_used()+new_holds+mesh_pool<=pool_cap && as_used()+new_dyn+mesh_as<=as_cap){
    last[i]=int64_t(now);cache_pool+=mesh_pool;cache_as+=mesh_as;
   }
   if(last[i]!=int64_t(now)){
    if(as_used()+new_dyn+dyn_as<=as_cap)new_dyn+=dyn_as;else{as_refused+=dyn_as;++left_out;}
    if(snap && pool_used()+new_holds+hold<=hold_cap)new_holds+=hold;
   }
   if(b>=builds-300){++seen;cached_seen+=last[i]==int64_t(now);}
  }
  if(ok){dyn=new_dyn;if(snap)holds=new_holds;}
  // 3. a built scene lets ranges at least 6 builds old retire (as_chunks::retired); a failed one retires nothing.
  if(ok)last_built=now;
  for(size_t k=0;k<pend.size();){
   if(now-pend[k].frame>=6 && pend[k].frame<last_built){
    pend_pool[pend[k].cache]-=pend[k].pool;pend_as[pend[k].cache]-=pend[k].as;pend.erase(pend.begin()+k);
   }else ++k;
  }
  if(b>=builds-300){failed_last+=!ok;built_last+=ok;}
  max_pool=(std::max)(max_pool,pool_used());max_as=(std::max)(max_as,as_used());
  // 4. present: the budget LRU (own rule), then the share pass; meshes drawn in the last 8 builds stay.
  auto old_meshes=[&]{
   std::vector<std::pair<int64_t,uint32_t>> old;
   for(uint32_t i=0;i<ring;++i)if(last[i]>=0 && uint64_t(last[i])+8<now)old.push_back({last[i],i});
   std::sort(old.begin(),old.end());
   return old;
  };
  if(mesh_cache_lru_due(pool_used()+cache_as,budget,declined,true,now)){
   const auto old=old_meshes();
   std::vector<LruMesh> plan;for(auto& o:old)plan.push_back({uint64_t(o.first),mesh_pool+mesh_as});
   const size_t n=mesh_cache_lru_count(plan,pool_used()+cache_as,budget,36000,now);
   for(size_t k=0;k<n;++k)evict_mesh(old[k].second,now);
  }
  // The others' demand: live bytes of the mandatory users plus refused requests; pending ranges (transient, they
  // retire behind the next built scene) and optional holds are not demand.
  const uint64_t mandatory=fixed_pool+bounds_live+pool_refused;
  pool_peak.observe(mandatory,now,600);
  as_peak.observe(dyn+as_refused,now,600);
  pool_refused=as_refused=0;
  if(headroom){
   pool_share=mesh_cache_share(pool_cap,pool_peak.peak());
   as_share=mesh_cache_share(as_cap,as_peak.peak());
   hold_limit=mesh_cache_hold_limit(pool_cap,pool_peak.peak(),mandatory);
   const auto old=old_meshes();
   std::vector<ShareMesh> plan;for(auto& o:old)plan.push_back({uint64_t(o.first),mesh_pool,mesh_as});
   const size_t n=mesh_cache_share_count(plan,cache_pool,pool_share,cache_as,as_share,share_declined);
   for(size_t k=0;k<n;++k)evict_mesh(old[k].second,now);
  }
 }
 return {failed_last,built_last,left_out,seen?double(cached_seen)/double(seen):0.0,max_pool,max_as};
}

int main(){
 // rt_mesh_budget_own off: every BLAS byte, as before; on: the cached meshes' own BLAS (never above the total).
 check(mesh_cache_charged_as(1677*mib,9*mib,false)==1677*mib);
 check(mesh_cache_charged_as(1677*mib,9*mib,true)==9*mib);
 check(mesh_cache_charged_as(5*mib,9*mib,true)==5*mib);
 // rv2 14:58:21: pool 844 MB, all BLAS 1677 MB, 55 meshes: a 3600-triangle mesh (0.66 MB pool, 0.27 MB BLAS) was
 // declined; charged by its own BLAS (about 55 x 0.3 MB) it fits. The pool hard cap still refuses at 2047 MB.
 check(!mesh_cache_admit(upper(844.2),mesh_cache_charged_as(upper(1677.3),17*mib,false),3600*192,3600*80,2048*mib));
 check(mesh_cache_admit(upper(844.2),mesh_cache_charged_as(upper(1677.3),17*mib,true),3600*192,3600*80,2048*mib));
 check(!mesh_cache_admit(kMeshPoolHardCap,mesh_cache_charged_as(upper(1677.3),0,true),1,0,UINT64_MAX));
 check(!mesh_cache_admit(2000*mib,mesh_cache_charged_as(upper(1677.3),40*mib,true),9*mib,1,2048*mib));
 // The pass runs above the budget and every 600 frames as before; on, also after a decline at the budget.
 check(mesh_cache_lru_due(2049*mib,2048*mib,false,false,1001) && mesh_cache_lru_due(0,2048*mib,false,false,1200));
 check(!mesh_cache_lru_due(2047*mib,2048*mib,true,false,1001) && mesh_cache_lru_due(2047*mib,2048*mib,true,true,1001));
 check(!mesh_cache_lru_due(2047*mib,2048*mib,false,true,1001));
 // The LRU plan equals the old in-place loop for every charged level (key-off identity).
 {
  std::vector<LruMesh> old;
  for(uint64_t i=0;i<40;++i)old.push_back({100+i*37,(1+i%7)*mib});
  for(uint64_t used=0;used<=3000*mib;used+=7*mib)
   for(uint64_t now:{uint64_t(1000),uint64_t(1700),uint64_t(40000)}){
    size_t n=0;uint64_t cur=used;
    for(const LruMesh& m:old){if(cur<=2048*mib*9/10 && m.last_used+600>now)break;cur-=(std::min)(cur,m.bytes);++n;}
    check(mesh_cache_lru_count(old,used,2048*mib,600,now)==n);
   }
  // rv2 14:58:21 off: 2521 MB charged, 55 meshes of about 1 MB cannot bring it to 1843 MB: all old ones go.
  std::vector<LruMesh> s8(55,LruMesh{900,mib});
  check(mesh_cache_lru_count(s8,upper(844.2)+upper(1677.3),2048*mib,36000,1000)==55);
  check(mesh_cache_lru_count(s8,upper(844.2)+55*mib,2048*mib,36000,1000)==0);
 }
 // Turning view: off, the per-draw BLAS of the declined draws keep the cache over budget (most visible static
 // draws stay captured, re-admitted and evicted again); on, every visible mesh stays cached, a ring that fits
 // is admitted once (0 admissions in laps 3-4), a larger ring evicts only meshes out of view, and the charged
 // bytes after each LRU pass stay within the budget.
 {
  const TurnResult off=turn_model(false,1200),on=turn_model(true,1200),big=turn_model(true,2400);
  std::printf("turn model: visible cached off %.4f / on %.4f / on, 2x ring %.4f; admissions in laps 3-4 off %llu / on %llu"
              " / on, 2x ring %llu; max charged on %.0f MB, 2x ring %.0f MB\n",off.visible_cached,on.visible_cached,
              big.visible_cached,(unsigned long long)off.admitted_late,(unsigned long long)on.admitted_late,
              (unsigned long long)big.admitted_late,double(on.max_charged_after_lru)/double(mib),
              double(big.max_charged_after_lru)/double(mib));
  check(off.visible_cached<0.5);
  check(on.visible_cached==1.0 && on.admitted_late==0 && on.max_charged_after_lru<=2048*mib);
  // A decline at the budget leaves at most that build's new edge (10 of 600 meshes) captured; the pass it triggers
  // frees 10% of the budget (about 220 meshes), so declines are rarer than one build in 20.
  check(big.visible_cached>=1.0-10.0/600.0/20.0 && big.max_charged_after_lru<=2048*mib);
 }
 // rv3 starvation: the old own rule deadlocks (no scene built in the last 300 builds); with the shares and the
 // left-out fallback every build commits a scene, and the cache still holds most of the visible meshes.
 {
  const StarveResult old=starve_model(false),fix=starve_model(true);
  std::printf("starvation model: last 300 builds failed/built old %u/%u, headroom %u/%u; visible cached old %.3f, "
              "headroom %.3f; left out with headroom %u; peak pool %.0f / AS %.0f MB\n",old.failed_last,old.built_last,
              fix.failed_last,fix.built_last,old.visible_cached,fix.visible_cached,fix.left_out,
              double(fix.max_pool)/double(mib),double(fix.max_as)/double(mib));
  check(old.built_last==0 && old.failed_last==300);
  check(fix.failed_last==0 && fix.built_last==300 && fix.max_pool<=kMeshPoolHardCap && fix.max_as<=4013*mib);
  check(fix.visible_cached>0.5);
 }
 // Shares: capacity minus the others' peak + 25%; a mesh fits only inside both.
 check(mesh_cache_share(2047*mib,1000*mib)==797*mib && mesh_cache_share(2047*mib,1700*mib)==0);
 check(mesh_cache_fits(100,50,150,10,5,15) && !mesh_cache_fits(100,51,150,10,5,15) && !mesh_cache_fits(100,50,150,10,6,15));
 {
  DemandPeak p;p.observe(500,0,600);p.observe(100,599,600);check(p.peak()==500);
  p.observe(100,600,600);check(p.peak()==500);   // the previous bucket still holds it
  p.observe(50,1200,600);check(p.peak()==100);    // forgotten after a full bucket
  std::vector<ShareMesh> s(10,ShareMesh{0,10,1});
  check(mesh_cache_share_count(s,100,100,10,10,false)==0);   // within both shares: nothing
  check(mesh_cache_share_count(s,120,100,10,10,false)==3);   // 120 -> 90 (90% of 100)
  check(mesh_cache_share_count(s,50,100,13,10,false)==4);    // BLAS 13 -> 9
  check(mesh_cache_share_count(s,100,100,10,10,true)==1);    // at the share after a decline: 100 -> 90
 }
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
 // rt_alpha_floor_yield: the lowered S8 view (rv1 2026-11-09 11:39): alpha 435, opaque 1568 MB of 2047.
 {const uint64_t size=2047*mib,wide=818*mib,base=171*mib;
  check(alpha_region(size,wide,wide,435*mib,1568*mib,true,60)==wide);                 // key off: 40% kept
  const uint64_t r=alpha_region(size,wide,wide,435*mib,1568*mib,true,60,base);
  check(r>=435*mib && size-r>=1568*mib);                                            // both fit
  check(alpha_region(size,wide,wide,100*mib,1900*mib,true,60,base)==base);          // never below base
  check(alpha_region(1024*mib,409*mib,0,200*mib,300*mib,true,60,base)==409*mib);    // fits: unchanged
  check(alpha_region(size,wide,0,0,0,true,60,base)==wide);}                         // load frame: wide floor
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
