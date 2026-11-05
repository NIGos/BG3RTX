#include "../common/cpu_mapped_source.h"
#include "../common/cpu_submit_matrices.h"
#include <cstdio>
#include <chrono>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
using namespace bg3;
#define check(v) do{if(!(v)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#v);std::abort();}}while(0)
int main(){
 std::array<uint8_t,512> first{},second{};uint8_t* mapping=first.data();uint64_t epoch=1;size_t resolves=0;
 auto resolve=[&](uint64_t,uint64_t off)->const uint8_t*{++resolves;return mapping?mapping+off:nullptr;};
 for(unsigned frame=0;frame<240;++frame){
  if(frame==30){mapping=nullptr;++epoch;}
  if(frame==31){mapping=second.data();++epoch;}
  if(frame==60){mapping=first.data();++epoch;}
  if(frame==90)++epoch;
  if(frame==120){mapping=second.data();++epoch;}
  if(mapping)mapping[4]=uint8_t(frame); // writes do NOT change the epoch
  uint32_t off=0x12345678,on=off;
  check(cpu_opt::mapped_read(55,4,&off,4,epoch,false,resolve)==cpu_opt::mapped_read(55,4,&on,4,epoch,true,resolve));check(off==on);
 }
 check(resolves<300);
 cpu_opt::BufferFacts facts{512,7,true,true};
 auto load=[&]{return facts;};
 for(unsigned frame=0;frame<240;++frame){
  if(frame==30){facts={};++epoch;} // destroy, same numeric handle recreated
  if(frame==31){facts={1024,9,true,true};++epoch;}
  if(frame==60){facts.usage=2;++epoch;}
  if(frame==90){facts.size=12;++epoch;}
  auto off=cpu_opt::buffer_facts(55,epoch,false,load),on=cpu_opt::buffer_facts(55,epoch,true,load);
  check(off.size==on.size && off.usage==on.usage && off.known==on.known && off.size_known==on.size_known);
 }
 auto cache_owner=std::make_unique<cpu_opt::SubmitMatrices>();auto& cache=*cache_owner;std::array<float,12> matrix{1,0,0,3,0,1,0,4,0,0,1,5};
 size_t reads=0;
 auto gather=[&](float* dst,const uint8_t* src){++reads;std::memcpy(dst,src,16);};
 for(uint64_t frame=1;frame<=240;++frame){
  cache.begin();if(frame==60)++epoch;
  matrix[3]=float(frame); // same pointers, changed between submits
  if(frame==90)matrix[0]=std::numeric_limits<float>::infinity();
  if(frame==91)matrix[0]=1;
  perf::MatrixSource src;
  for(unsigned k=0;k<3;++k)src.rows[k]=reinterpret_cast<const uint8_t*>(matrix.data()+k*4);
  for(unsigned draw=0;draw<16;++draw){
   auto& value=cache.get(src,epoch,frame,gather);check(!std::memcmp(value.rows.data(),matrix.data(),48));
   check(value.valid==perf::valid_matrix(matrix.data()));
   for(bool instance:{false,true}){std::array<uint8_t,64> off{},on{};
    perf::scatter_matrix(off.data(),matrix.data(),perf::valid_matrix(matrix.data()),instance);
    perf::scatter_matrix(on.data(),value.rows.data(),value.valid,instance);check(off==on);
   }
  }
  check(cache.size()==1);
 }
 check(reads==240*3);
 // Exercise overflow with more distinct sources than resident slots. Repeated
 // reads after host mutation must return each source's FIRST sample this submit.
 std::vector<std::array<float,12>> crowd(10000,matrix);cache.begin();
 for(unsigned pass=0;pass<2;++pass)for(auto& rows:crowd){
  perf::MatrixSource src;for(unsigned k=0;k<3;++k)src.rows[k]=reinterpret_cast<const uint8_t*>(rows.data()+4*k);
  const auto& value=cache.get(src,epoch,1000,gather);
  check(value.rows[3]==matrix[3]);rows[3]+=1;
 }
 check(cache.size()==crowd.size());
 cache.begin();
 for(auto& rows:crowd){
  perf::MatrixSource src;for(unsigned k=0;k<3;++k)src.rows[k]=reinterpret_cast<const uint8_t*>(rows.data()+4*k);
  const auto& value=cache.get(src,epoch+1,1001,gather);check(value.rows[3]==matrix[3]+2);
 }
 check(cache.size()==crowd.size());
 auto bench=[&](bool opt){
  auto persistent_owner=std::make_unique<cpu_opt::SubmitMatrices>();auto& persistent=*persistent_owner;double checksum=0;const auto start=std::chrono::steady_clock::now();
  for(uint64_t frame=0;frame<120;++frame){
   perf::SubmitMatrices previous;persistent.begin();
   for(unsigned draw=0;draw<5000;++draw){
    auto& rows=crowd[draw%1500];perf::MatrixSource src;
    for(unsigned k=0;k<3;++k)src.rows[k]=reinterpret_cast<const uint8_t*>(rows.data()+4*k);
    const auto& value=opt?persistent.get(src,epoch,frame+1,gather):previous.get(src,gather);
    checksum+=value.rows[3];
   }
  }
  return std::array<double,2>{std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/120,checksum};
 };
 const auto off=bench(false),on=bench(true);check(off[1]==on[1]);
 std::printf("cpu_opt4 submit 5000 draws/1500 sources: prior map %.4f, persistent %.4f ms/frame\n",off[0],on[0]);
 std::puts("cpu_opt4 capture: 240-frame mapped/facts/submit fixtures identical, host writes, unmap/remap, rebind, recycled handles, invalid matrices and per-submit refresh; matrix gathers 11520 -> 720");
}
