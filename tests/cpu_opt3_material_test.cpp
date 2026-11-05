#include "../common/cpu_material_prepare.h"
#include "../common/native_hit_instanced.h"
#include "../common/native_hit_pool.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
using namespace bg3;
using namespace bg3::native_hit;
static void check(bool value){if(!value)std::abort();}
int main(){
 std::mutex mutex;
 std::unique_lock lock(mutex);
 // Use the real provider lock type and a second thread, so ownership flags
 // alone cannot hide an incorrectly retained mutex during capture/staging.
 auto available=[&](){return std::async(std::launch::async,[&](){
  if(!mutex.try_lock())return false;mutex.unlock();return true;
 }).get();};
 for(bool capture_unlocked:{false,true})for(bool optimize:{false,true}){
  check(cpu_opt::prepare_material(lock,capture_unlocked,optimize,
   [&](){check(available()==(capture_unlocked || optimize));return true;},
   [&](){check(available()==optimize);return true;})==cpu_opt::MaterialPrepare::Ready);
  check(lock.owns_lock() && !available());
  unsigned staged=0;
  check(cpu_opt::prepare_material(lock,capture_unlocked,optimize,[]{return false;},[&](){++staged;return true;})==cpu_opt::MaterialPrepare::CaptureFailed);
  check(staged==0 && lock.owns_lock());
  check(cpu_opt::prepare_material(lock,capture_unlocked,optimize,[]{return true;},[]{return false;})==cpu_opt::MaterialPrepare::StageFailed);
  check(lock.owns_lock());
  for(bool fail_capture:{false,true}){
   try{cpu_opt::prepare_material(lock,capture_unlocked,optimize,
    [&](){if(fail_capture)throw 1;return true;},[&]()->bool{throw 2;});check(false);}
   catch(int value){check(value==(fail_capture?1:2) && lock.owns_lock());}
  }
 }
 // Persistently mapped input bytes change without a resource/descriptor epoch
 // bump. Both placements must read the current draw, including short-read
 // rejection. Compare the actual gather staging bytes over many frames.
 for(uint64_t fs:{0x8e49cde81b680054ull,0x923131c7039f33daull}){
  const auto* recipe=native_vt::recipe(fs);check(recipe);
  std::array<unsigned char,instanced::kStageBytes> previous{};
  for(uint32_t frame=0;frame<96;++frame){
   uint32_t engine[16],ubo[8];for(uint32_t i=0;i<16;++i)engine[i]=0x1000+frame*17+i;
   for(uint32_t i=0;i<8;++i)ubo[i]=0x2000+frame*31+i;
   const auto eb=pool::engine_bytes(*recipe)/4,mb=native_vt::material_bytes(*recipe)/4;
   const bool short_read=frame%7==0;
   unsigned char outputs[2][instanced::kStageBytes]{};
   for(bool optimize:{false,true}){
    const auto status=cpu_opt::prepare_material(lock,true,optimize,[]{return true;},[&](){
     check(lock.owns_lock()!=optimize);
     return instanced::stage(*recipe,engine,eb-(short_read?1:0),ubo,mb,outputs[optimize]);
    });
    check(status==(short_read?cpu_opt::MaterialPrepare::StageFailed:cpu_opt::MaterialPrepare::Ready));
    check(lock.owns_lock());
   }
   check(std::memcmp(outputs[0],outputs[1],sizeof outputs[0])==0);
   if(!short_read){check(std::memcmp(outputs[1],previous.data(),previous.size())!=0);std::memcpy(previous.data(),outputs[1],previous.size());}
  }
 }
 std::puts("cpu_opt3 material: identical live staging across 192 frames; lock release, failure and exception ownership verified");
}
