#include "../common/omm_content_registry.h"
#include <cstdio>
#include <cstdlib>
using namespace bg3::omm;
static unsigned checks;
static void check(bool x){++checks;if(!x){std::fprintf(stderr,"check %u failed\n",checks);std::exit(1);}}
int main(){
  ContentRegistry r(3);check(!r.create(0,true));check(!r.create(1,false));
  check(r.create(1,true));auto a=*r.lookup(1);check(a.imageId==1 && a.slot==0 && !a.boundSafe);
  r.bind(1,true);check(r.lookup(1)->boundSafe);r.bind(1,false);check(!r.lookup(1)->boundSafe);
  r.destroy(1);check(!r.lookup(1));check(r.create(1,true));auto b=*r.lookup(1);
  check(b.imageId!=a.imageId && b.slot!=a.slot);check(!b.boundSafe);
  check(r.create(2,true));check(!r.create(3,true));check(!r.lookup(3));
  r.destroy(2);check(!r.create(3,true)); // Freed slots are NEVER recycled.
  ContentRegistry ids;ids.nextId=UINT64_MAX;check(!ids.create(1,true));
  ids.nextId=UINT64_MAX-1;check(ids.create(1,true));check(!ids.create(2,true));
  std::printf("OMM content registry checks %u; no device calls\n",checks);
}
