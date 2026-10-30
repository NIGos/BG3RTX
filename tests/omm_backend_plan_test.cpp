#include "../common/omm_backend_plan.h"
#include <cstdio>
#include <cstdlib>
using namespace bg3::omm;
static unsigned checks;
static void check(bool x) { ++checks; if (!x) { std::fprintf(stderr,"check %u failed\n",checks); std::exit(1); } }
int main() {
  static_assert(sizeof(Triangle)==8); static_assert(sizeof(Usage)==12);
  const Triangle ts[]={{0,0,2},{1,0,2},{2,1,2}}; Plan p;
  check(make_plan(ts,3,12,p)); check(p.triangles==3 && p.usage.size()==2);
  check(p.usage[0].count==2 && p.usage[1].count==1);
  check(input_ranges(p,256,3,512,24));
  check(!input_ranges(p,257,3,512,24)); check(!input_ranges(p,256,3,513,24));
  check(!input_ranges(p,256,2,512,24)); check(!input_ranges(p,256,3,512,23));
  check(!input_ranges(p,UINT64_MAX-255,256,512,24));
  check(!input_ranges(p,256,3,UINT64_MAX-255,256));
  check(!make_plan({},3,12,p)); check(!make_plan(ts,0,12,p));
  for (unsigned level=0;level<=12;++level) {
    Triangle t{0,uint16_t(level),2}; const uint64_t bytes=((uint64_t(2)<<(2*level))+7)/8;
    check(make_plan({&t,1},bytes,12,p)); check(!make_plan({&t,1},bytes-1,12,p));
    t.dataOffset=UINT32_MAX; check(!make_plan({&t,1},bytes,12,p));
  }
  Triangle t{0,13,2}; check(!make_plan({&t,1},UINT64_MAX,20,p));
  t={0,1,2}; check(!make_plan({&t,1},1,0,p));
  t={0,0,1}; check(!make_plan({&t,1},1,12,p));
  Lifetime l; check(!l.retire()); check(!l.releasable(UINT64_MAX)); check(!l.use(0));
  check(l.use(10)); check(!l.use(9)); check(l.use(10)); check(l.use(12));
  check(!l.releasable(12)); check(l.retire()); check(!l.retire()); check(!l.use(13));
  check(!l.releasable(11)); check(l.releasable(12)); check(l.releasable(13));
  check(l.reserve());check(!l.releasable(13));check(l.unreserve());check(l.releasable(13));check(!l.unreserve());
  Lifetime pending;check(pending.discardable());check(pending.reserve());check(!pending.discardable());
  check(pending.unreserve());check(pending.discardable());
  check(pending.reserve());check(pending.use(20));check(!pending.discardable());
  check(pending.retire());check(!pending.releasable(20));check(pending.unreserve());check(pending.releasable(20));
  pending.reservations=UINT32_MAX;check(!pending.reserve());check(!pending.releasable(UINT64_MAX));
  std::printf("OMM EXT plan/range/lifetime checks %u; no device calls\n",checks);
}
