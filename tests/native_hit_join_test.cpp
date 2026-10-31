#include "../common/native_hit_join.h"
#include "../common/native_hit_writes.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
using namespace bg3::native_hit;
int main(int argc,char** argv){
 DrawJoin j;DrawKey a,b;uint32_t rec=0;a.words[0]=10;b=a;b.words[59]=1;
 assert(!j.note(1,1,a,7));j.begin(1,2);
 assert(j.note(1,2,a,7));assert(j.note(1,2,b,8));
 assert(!j.claim(1,1,a,rec));assert(!j.claim(2,2,a,rec));
 assert(j.claim(1,2,a,rec)&&rec==7);assert(j.unique(a,7));assert(!j.unique(a,8));
 // A late duplicate must revoke a previously captured deferred row.
 assert(!j.claim(1,2,a,rec));assert(!j.unique(a,7));
 assert(j.claim(1,2,b,rec)&&rec==8);assert(j.unique(b,8));
 j.begin(1,3);assert(!j.claim(1,3,b,rec));assert(!j.unique(b,8));
 assert(j.note(1,3,a,9));assert(!j.note(1,3,a,10));
 assert(!j.claim(1,3,a,rec));assert(!j.unique(a,9));
 j.begin(0,0);assert(!j.note(0,0,a,1));assert(!j.claim(0,0,a,rec));
 assert(byte_range(0,0,0));assert(byte_range(16,32,48));
 assert(!byte_range(17,32,48));assert(!byte_range(UINT64_MAX,1,UINT64_MAX));
 assert(byte_range(UINT64_MAX,0,UINT64_MAX));
 // Producer, deferred and consumer are independent one-time recordings. A
 // delayed/failed submit is not completion even if unrelated presents advance.
 RecordingOwner producer{1,10},deferred{2,20},consumer{3,30};
 assert(!producer.retired(10,UINT64_MAX));assert(!deferred.retired(20,UINT64_MAX));assert(!consumer.retired(30,UINT64_MAX));
 assert(producer.reserve());producer.finish(true,100);assert(!producer.retired(11,101));assert(producer.retired(11,102));
 assert(deferred.reserve());assert(!deferred.retired(21,UINT64_MAX));deferred.finish(false,100);
 assert(!deferred.retired(20,UINT64_MAX));assert(deferred.reserve());deferred.finish(true,200);
 assert(!deferred.retired(20,102));assert(!deferred.retired(21,201));assert(deferred.retired(21,202));
 assert(consumer.reserve());assert(!consumer.retired(31,UINT64_MAX));consumer.finish(false,250);assert(consumer.retired(31,0));
 assert(producer.reserve());producer.finish(true,300);assert(producer.poisoned);assert(!producer.retired(11,UINT64_MAX));
 // A successful explicit reset/free exposes recording zero, without requiring
 // another Begin. Failed reset retains the old recording and cannot retire it.
 RecordingOwner resetOnly{5,50};
 assert(!resetOnly.retired(50,UINT64_MAX));
 assert(resetOnly.retired(0,0));
 assert(resetOnly.reserve());assert(!resetOnly.retired(0,UINT64_MAX));
 resetOnly.finish(true,400);assert(!resetOnly.retired(0,401));assert(resetOnly.retired(0,402));
 RecordingOwner saturated{4,40};saturated.reservations=UINT32_MAX;assert(!saturated.reserve());assert(!saturated.retired(41,UINT64_MAX));
 j.begin(5,6);assert(j.note(5,6,a,UINT32_MAX));assert(j.assign(5,6,a,7));
 assert(j.claim(5,6,a,rec)&&rec==7);assert(!j.assign(5,7,a,8));
 assert(!j.note(5,6,a,UINT32_MAX));assert(!j.assign(5,6,a,8));assert(!j.unique(a,7));
 // Destination pointer storage class, not shader naming, proves the no-write
 // domain. Includes hidden pointers through a copied pointer and atomics.
 auto module=[](uint32_t storage,uint32_t operation){
  std::vector<uint32_t> s={0x07230203,0x10000,0,100,0,(4u<<16)|32,10,storage,11,
   (4u<<16)|59,10,20,storage,(4u<<16)|83,10,21,20};
  if(operation==62)s.insert(s.end(),{(3u<<16)|62,21,30});
  if(operation==229)s.insert(s.end(),{(7u<<16)|229,11,31,21,1,0,30});
  if(operation==99)s.insert(s.end(),{(4u<<16)|99,20,30,31});
  return s;
 };
 for(uint32_t storage:{3u,6u,7u}){auto s=module(storage,62);assert(no_graphics_storage_writes(s.data(),s.size()));}
 for(uint32_t storage:{0u,2u,4u,12u,5349u})for(uint32_t op:{62u,229u}){auto s=module(storage,op);assert(!no_graphics_storage_writes(s.data(),s.size()));}
 {auto s=module(7,99);assert(!no_graphics_storage_writes(s.data(),s.size()));s.pop_back();assert(!no_graphics_storage_writes(s.data(),s.size()));}
 {auto s=module(7,62);s.back()=30;s[s.size()-2]=99;assert(!no_graphics_storage_writes(s.data(),s.size()));}
 for(unsigned field=0;field<a.words.size();++field){
  j.begin(4,5);a={};b=a;b.words[field]=1;
  assert(j.note(4,5,a,1));assert(j.note(4,5,b,2));
  assert(j.claim(4,5,a,rec)&&rec==1);assert(j.claim(4,5,b,rec)&&rec==2);
  assert(j.unique(a,1)&&j.unique(b,2));
 }
 puts("PASS exact current-recording material draw join, duplicates/reuse/range checks");
 for(int i=1;i<argc;++i){std::ifstream f(argv[i],std::ios::binary|std::ios::ate);assert(f);auto bytes=f.tellg();assert(bytes>0 && size_t(bytes)%4==0);
  std::vector<uint32_t> words(size_t(bytes)/4);f.seekg(0);f.read(reinterpret_cast<char*>(words.data()),bytes);assert(f);
  bool safe=no_graphics_storage_writes(words.data(),words.size());std::printf("native graphics storage write bound %s: %s\n",argv[i],safe?"READ_ONLY":"UNSUPPORTED");assert(safe);
 }
}
