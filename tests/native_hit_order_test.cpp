// Independent bounded CPU cases; no Vulkan API or game calls.
#include "../common/native_hit_order.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace bg3::native_hit;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 constexpr uint64_t graphics=7;
 Evidence source{10,false,0},deferred{20,false,0};
 std::array<uint64_t,4> ids{10,20,30,40};
 do{
  for(bool s:{false,true})for(bool d:{false,true}){
   auto a=source,b=deferred;a.submitted=s;b.submitted=d;a.queue=s?graphics:0;b.queue=d?graphics:0;
   auto pos=[&](uint64_t id){return std::find(ids.begin(),ids.end(),id)-ids.begin();};
   bool expected=(s || pos(10)<pos(30)) && (d || pos(20)<pos(30));
   CHECK(ordered_before_query(a,b,30,graphics,graphics,ids)==expected);
  }
 }while(std::next_permutation(ids.begin(),ids.end()));
 std::array<uint64_t,3> ordered{10,20,30},reverse{30,20,10},duplicate{10,30,30};
 CHECK(ordered_before_query(source,deferred,30,graphics,graphics,ordered));
 CHECK(!ordered_before_query(source,deferred,30,graphics,graphics,reverse));
 CHECK(!ordered_before_query(source,deferred,30,graphics,8,ordered));
 CHECK(!ordered_before_query(Evidence{10,true,8},deferred,30,graphics,graphics,ordered));
 CHECK(!ordered_before_query(source,deferred,30,graphics,graphics,duplicate));
 CHECK(!ordered_before_query(Evidence{30,false,0},deferred,30,graphics,graphics,ordered));
 CHECK(!ordered_before_query(source,Evidence{30,true,graphics},30,graphics,graphics,ordered));
 CHECK(!ordered_before_query(source,deferred,99,graphics,graphics,ordered));
 CHECK(!ordered_before_query({},deferred,30,graphics,graphics,ordered));
 CHECK(!ordered_before_query(source,deferred,30,0,0,ordered));
 std::array<uint64_t,1> queryOnly{30};
 CHECK(ordered_before_query(Evidence{10,true,graphics},Evidence{20,true,graphics},30,graphics,graphics,queryOnly));
 CHECK(!ordered_before_query(source,Evidence{20,true,graphics},30,graphics,graphics,queryOnly));
 std::array<uint64_t,4> withUntracked{10,0,20,30};
 CHECK(ordered_before_query(source,deferred,30,graphics,graphics,withUntracked));
 CHECK(ordered_before_query(source,source,30,graphics,graphics,ordered)); // Both packets may originate in one earlier CB.
 CHECK(ordered_before_query(Evidence{30,false,0,true},deferred,30,graphics,graphics,ordered));
 CHECK(ordered_before_query(source,Evidence{30,false,0,true},30,graphics,graphics,ordered));
 CHECK(ordered_before_query(Evidence{30,false,0,true},Evidence{30,false,0,true},30,graphics,graphics,queryOnly));
 CHECK(!ordered_before_query(Evidence{30,false,0,true},Evidence{30,false,0,false},30,graphics,graphics,queryOnly));
 CHECK(!ordered_before_query(Evidence{30,true,8,true},deferred,30,graphics,graphics,ordered));
 CHECK(!ordered_before_query(Evidence{10,false,0,true},Evidence{20,false,0,true},30,graphics,graphics,reverse));
 PendingPublication retry;
 CHECK(retry.prepare(true)==1 && retry.pending && !retry.sealed);
 retry.finish(false);CHECK(!retry.pending && !retry.published && !retry.sealed);
 CHECK(retry.prepare(false)==0);retry.finish(true);CHECK(retry.sealed && !retry.published && !retry.pending);
 CHECK(retry.prepare(true)==0 && !retry.published && !retry.pending); // Sealed unknown cannot be upgraded.
 PendingPublication good;CHECK(good.prepare(true)==1);good.finish(true);
 CHECK(good.sealed && good.published && !good.pending);
 CHECK(good.prepare(false)==1 && good.published && !good.pending); // Never rewrite successful GPU proof.
 good.finish(false);CHECK(good.sealed && good.published);
 PendingPublication badThenGood;CHECK(badThenGood.prepare(false)==0);badThenGood.finish(false);
 CHECK(badThenGood.prepare(true)==1);badThenGood.finish(true);CHECK(badThenGood.sealed && badThenGood.published);
 PendingPublication twice;CHECK(twice.prepare(true)==1);CHECK(twice.prepare(true)==0 && !twice.published && twice.pending);
 twice.finish(true);CHECK(twice.sealed && !twice.published);
 // Scene handout (docs/SCENE_HANDOUT_DESIGN.md 8.6): the capture command buffer 20 is submitted before the G-buffer
 // material one 21 (the deferred packet) and the lighting 30. A query recorded at the scene commit, in 20, has the
 // packet after it: never published (hq-s8-ultra 2026-11-14, "ordered 0"). The lighting query in 30 is.
 const std::array<uint64_t,3> handoutFrame{20,21,30};
 CHECK(!ordered_before_query(Evidence{20,false,0,true},Evidence{21,false,0},20,graphics,graphics,handoutFrame));
 CHECK(ordered_before_query(Evidence{20,false,0},Evidence{21,false,0},30,graphics,graphics,handoutFrame));
 std::printf("%u native hit submit-order/publication checks passed\n",checks);
}
