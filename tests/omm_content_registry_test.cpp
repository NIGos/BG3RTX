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
  ContentRegistry views;check(views.create(10,true,5,37));views.bind(10,true);
  check(!views.create_view(0,10,true,0,1,37));check(!views.create_view(1,99,true,0,1,37));
  check(!views.create_view(1,10,false,0,1,37));check(!views.create_view(1,10,true,0,1,38));
  check(!views.create_view(1,10,true,0,0,37));check(!views.create_view(1,10,true,5,1,37));
  check(!views.create_view(1,10,true,UINT32_MAX,1,37));check(!views.create_view(1,10,true,4,2,37));
  for(uint32_t base=0;base<5;++base){
    check(views.create_view(1,10,true,base,UINT32_MAX,37));const auto* view=views.lookup_view(1);
    check(view && view->baseMip==base && view->levelCount==5-base);
    for(uint32_t n=1;n<=5-base;++n){check(views.create_view(1,10,true,base,n,37));check(views.lookup_view(1)->levelCount==n);}
  }
  check(views.create_view(1,10,true,0,5,37,{1,2,3,4}));const auto old=*views.lookup_view(1);
  check(old.components==std::array<uint32_t,4>{1,2,3,4});views.destroy_view(1);check(!views.lookup_view(1));
  check(views.create_view(1,10,true,0,5,37));check(views.lookup_view(1)->viewId!=old.viewId);
  views.destroy(10);check(!views.lookup_view(1));check(views.create(10,true,5,37));views.bind(10,true);check(!views.lookup_view(1));
  check(views.create_view(1,10,true,0,5,37));check(views.lookup_view(1)->imageId!=old.imageId);
  views.nextViewId=UINT64_MAX;check(!views.create_view(2,10,true,0,1,37));
  ContentRegistry policies;check(!policies.create(1,true,1,37,0));check(!policies.create(1,true,1,37,3));
  check(policies.create(1,true,3,137,2));check(policies.lookup(1)->requiredSamplingCertificate==2);
  check(policies.create_view(1,1,true,0,3,137));check(policies.lookup_view(1)->format==137);
  std::printf("OMM content registry checks %u; no device calls\n",checks);
}
