#include "../common/cpu_descriptor_receipt.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace bg3::cpu_opt;
#define check(ok) do {if(!(ok)){std::fprintf(stderr,"check failed line %d: %s\n",__LINE__,#ok);std::abort();}}while(0)
struct Binding {uint64_t view{},buffer{},image{},offset{},range{},width{},height{};bool operator==(const Binding&)const=default;};
int main(){
 DescriptorReceipts<Binding,32> cache;
 DescriptorReceipts<Binding,32>::Key key{1,1,3,0,1,1,55,0};
 std::vector<uint32_t> dynamic{0,128};Binding live{11,0,22,0,64,256,256};
 size_t loads=0,hits=0;bool valid=true;
 for(unsigned frame=0;frame<320;++frame){
  // Same raw handle is never an allocation identity. Both copy and template
  // writes use the per-set receipt generation, independent of sampler policy.
  if(frame==30){++key[1];live.view=12;}
  if(frame==60){++key[1];live.width=512;} // descriptor copy
  if(frame==90){++key[1];live.offset=32;} // template write
  if(frame==120){++key[0];key[1]=1;live.view=11;} // free/pool reset/reallocate
  if(frame==150){++key[5];live.image=33;} // recycled view/image/sampler incarnation
  if(frame==180){++key[4];live.buffer=44;live.view=0;live.image=0;} // buffer rebind/map epoch
  if(frame==210){dynamic[1]=256;live.offset=256;}
  if(frame==240){dynamic.push_back(0);live.range=32;} // same prefix, different offset count
  if(frame==270){++key[4];valid=false;}
  if(frame==272){++key[4];valid=true;live.range=128;}
  for(unsigned draw=0;draw<16;++draw){
   // Starting on the same reference checks the ordinary last-note dedup rule.
   std::vector<uint64_t> off{live.view?live.view:live.buffer},on=off;
   auto note=[](std::vector<uint64_t>& refs,uint64_t h){if(h && (refs.empty() || refs.back()!=h))refs.push_back(h);};
   auto use=[&](std::vector<uint64_t>& refs,const Binding& b){descriptor_uses(b,[&](uint64_t h){note(refs,h);});};
   use(off,live);Binding out{};const auto previous=loads;
   bool ok=cache.query(key,dynamic,out,[&](Binding& b){++loads;b=live;use(on,b);return valid;},[&](const Binding& b){++hits;use(on,b);});
   check(ok==valid);check(off==on);check(out==live);
   if(!valid)check(loads==previous+1); // partial failure side effects run every time
  }
 }
 check(hits>4900);check(loads<100);
 // Force every lookup to the same slot: full dynamic offset and key equality
 // must reject both a colliding identity and a same-prefix dynamic vector.
 DescriptorReceipts<Binding,1> collision;
 for(unsigned i=0;i<64;++i){auto k=key;k[0]=i%2;std::vector<uint32_t> d(i%3,7);Binding out{};unsigned n=0;
  check(collision.query(k,d,out,[&](Binding& b){++n;b=live;return true;},[](auto&){}));check(n==1);
  check(collision.query(k,d,out,[&](Binding& b){++n;b=live;return true;},[](auto&){}));check(n==1);
 }
 std::printf("cpu_opt4 descriptor receipts: 320-frame query payloads and lifetime order identical; %zu loads, %zu hits; write/copy/template, set reuse, resources, offsets, failure and collisions verified\n",loads,hits);
}
