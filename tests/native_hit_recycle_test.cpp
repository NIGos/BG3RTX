// common/native_hit_recycle.h: size rounding, exact-key reuse, cap and idle eviction. No Vulkan.
#include "../common/native_hit_recycle.h"
#include <cstdio>
#include <cstdlib>
using namespace bg3::native_hit::recycle;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 // Rounding: never below the request, at most 12.5% above it past 1 MiB, few distinct sizes.
 CHECK(round_bytes(0)==0);
 CHECK(round_bytes(1)==(64u<<10) && round_bytes(64u<<10)==(64u<<10));
 CHECK(round_bytes((64u<<10)+1)==(128u<<10));
 CHECK(round_bytes(1u<<20)==(1u<<20));
 CHECK(round_bytes((1u<<20)+1)==(1u<<20)+(128u<<10)); // 2 MiB / 16 steps
 CHECK(round_bytes(32ull<<20)==(32ull<<20));             // the deferred chunk: exact
 CHECK(round_bytes(256ull<<20)==(256ull<<20));           // query_cap: exact
 for(uint64_t b=1;b<(600ull<<20);b=b*3/2+7){const uint64_t r=round_bytes(b);
  CHECK(r>=b);if(b>(1u<<20))CHECK(double(r)<=double(b)*1.125+1.0);CHECK(round_bytes(r)==r);}
 {uint64_t prev=0;unsigned distinct=0;for(uint64_t b=(1u<<20)+1;b<=(256ull<<20);b+=(64u<<10)){const uint64_t r=round_bytes(b);if(r!=prev){++distinct;prev=r;}}
  CHECK(distinct<=8*8+1);} // eight sizes per power of two, 1 MiB .. 256 MiB

 // Exact key: another size, usage or memory kind is never handed out.
 Pool<int> p(1000,10);std::vector<int> ev;
 const Key a{100,1,false},b{100,2,false},c{100,1,true},d{200,1,false};
 p.give(a,1,5,ev);CHECK(ev.empty() && p.retained()==100 && p.size()==1);
 int h=0;CHECK(!p.take(b,h) && !p.take(c,h) && !p.take(d,h));
 CHECK(p.take(a,h) && h==1 && p.retained()==0 && p.size()==0 && p.reused()==1);
 CHECK(!p.take(a,h));
 // Most recent first.
 p.give(a,2,6,ev);p.give(a,3,7,ev);CHECK(p.take(a,h) && h==3);CHECK(p.take(a,h) && h==2);

 // Cap: the oldest are evicted; one larger than the cap is evicted at once.
 p.give(Key{400,1,false},10,1,ev);p.give(Key{400,1,false},11,2,ev);CHECK(ev.empty() && p.retained()==800);
 p.give(Key{400,1,false},12,3,ev);CHECK(ev.size()==1 && ev[0]==10 && p.retained()==800);
 ev.clear();p.give(Key{2000,1,false},13,3,ev);CHECK(ev.size()==1 && ev[0]==13 && p.retained()==800);

 // Idle: released more than 10 frames ago.
 ev.clear();p.trim(12,ev);CHECK(ev.empty());p.trim(13,ev);CHECK(ev.size()==1 && ev[0]==11 && p.size()==1 && p.retained()==400);
 ev.clear();p.trim(13,ev);CHECK(ev.empty());p.trim(14,ev);CHECK(ev.size()==1 && ev[0]==12 && p.size()==0 && p.retained()==0);

 // Clear hands back everything.
 ev.clear();p.give(a,20,1,ev);p.give(d,21,1,ev);p.clear(ev);CHECK(ev.size()==2 && p.size()==0 && p.retained()==0);

 // Default cap holds one frame's largest transient set (4 x 32 MB chunks + a 256 MB query).
 Pool<int> q;ev.clear();
 for(int i=0;i<4;++i)q.give(Key{round_bytes(32ull<<20),1,false},i,1,ev);
 q.give(Key{round_bytes(256ull<<20),2,false},9,1,ev);CHECK(ev.empty());
 std::printf("native_hit_recycle_test: %u checks passed\n",checks);
 return 0;
}
