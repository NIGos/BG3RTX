#include "../common/cpu_instance_rows.h"
#include "../common/cpu_alpha_record.h"
#include "../common/cpu_cascade_model.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

using namespace bg3::cpu_opt;
#define check(ok) do {if(!(ok)){std::fprintf(stderr,"check failed line %d: %s\n",__LINE__,#ok);std::abort();}}while(0)
struct Record {
 uint32_t first_tri{},tris{},kind{},tex{},cache{},channel{};float cut{},cut_first{},scale[2]{};
 uint32_t fs_lo{},fs_hi{};float p0[4]{},p1[4]{},t[32]{};
};
static_assert(sizeof(Record)==208);
struct Recipe {uint32_t kind=1,channel=3;float cut=.5f,cut_first=.1f;uint64_t hash=123;float k=1;};
int main(){
 std::vector<uint8_t> first(1024*1024),recreated(first.size());
 uint8_t* memory=first.data();uint64_t bytes=first.size(),epoch=1;size_t reads=0;
 std::mutex mutex;
 auto size_of=[&](uint64_t){std::lock_guard lock(mutex);++reads;return bytes;};
 auto mapped=[&](uint64_t,uint64_t off){std::lock_guard lock(mutex);++reads;return memory?memory+off:nullptr;};
 std::array<InstanceRowSource,3> sources{{{55,0,48,0,0},{55,0,48,0,16},{55,0,48,0,32}}};
 for(unsigned frame=0;frame<240;++frame){
   // Same numeric handle after destroy/recreate; rebind, epoch-only bump,
   // unmap/remap, out-of-range and range/stride changes all fail closed.
   if(frame==30){memory=nullptr;++epoch;}
   if(frame==31){memory=recreated.data();++epoch;}
   if(frame==60){memory=first.data();++epoch;}
   if(frame==90)++epoch;
   if(frame==120){bytes=16;++epoch;}
   if(frame==121){bytes=first.size();++epoch;}
   if(frame==150)for(auto& s:sources)s.offset+=48;
   if(frame==180)for(auto& s:sources)s.stride=64;
   const auto off=instance_rows(sources,frame%4,2,epoch,false,size_of,mapped);
   const auto on=instance_rows(sources,frame%4,2,epoch,true,size_of,mapped);
   check(off.valid==on.valid && off.rows==on.rows && off.strides==on.strides);
   if(on.valid){memory[sources[0].offset]=uint8_t(frame);check(*on.rows[0]==*off.rows[0]);}
 }
 Recipe recipe;VtMaterialPacket packet;packet.tiles[15]=3;
 uint32_t model[16]{};float origin[3]{};
 for(unsigned frame=0;frame<240;++frame){
   if(frame==30){float translation=3;std::memcpy(model+12,&translation,4);}
   if(frame==60)origin[0]=23; // fresh camera/material bytes without an epoch
   if(frame==90)++epoch;
   if(frame==120)model[8]=0x7f800000; // nonfinite model fails closed
   if(frame==121)model[8]=0;
   cascade::DrawId off,on;
   const auto a=bg3::cpu_opt::model_position(off,model,origin,epoch,false);
   const auto b=bg3::cpu_opt::model_position(on,model,origin,epoch,true);
   check(a==b && off.position_known==on.position_known &&
     !std::memcmp(off.position,on.position,sizeof off.position) &&
     !std::memcmp(off.position_error,on.position_error,sizeof off.position_error));
 }
 for(float value:{-1e30f,1e30f,std::nextafter(-1e30f,0.f),std::nextafter(1e30f,0.f),
      std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
   const bool expected=value>-1e30f && value<1e30f;
   for(unsigned field=0;field<41;++field){VtMaterialPacket probe=packet;Recipe rec=recipe;
     if(field<8)probe.uv[field]=value;else if(field<40)probe.tiles[field-8]=value;else rec.k=value;
     for(bool cached:{false,true})check(vt_material_record<Record>(rec,probe,epoch,cached).valid==expected);
   }
 }
 for(unsigned frame=0;frame<240;++frame){
   if(frame==30)packet.uv[2]=.25f; // host write without epoch: MUST invalidate
   if(frame==60)recipe.cut=.33f;
   if(frame==90)++epoch;
   if(frame==120)packet.tiles[9]=std::numeric_limits<float>::quiet_NaN();
   if(frame==121)packet.tiles[9]=0;
   if(frame==150)recipe.channel=7;
   auto off=vt_material_record<Record>(recipe,packet,epoch,false);
   auto on=vt_material_record<Record>(recipe,packet,epoch,true);
   check(off.valid==on.valid && !std::memcmp(&off.record,&on.record,sizeof(Record)));
   // Per-frame slot/triangle remapping must not mutate the owned template.
   on.record.first_tri=frame*100;on.record.tris=frame+1;on.record.tex=frame%7;
   check(vt_material_record<Record>(recipe,packet,epoch,true).record.first_tri==0);
 }
 auto bench=[&](bool opt){
   memory=first.data();bytes=first.size();reads=0;uint64_t receipt=0;
   const auto start=std::chrono::steady_clock::now();
   for(unsigned frame=0;frame<120;++frame)for(unsigned draw=0;draw<5000;++draw){
     const uint64_t base=uint64_t(draw%1500)*128;
     for(unsigned k=0;k<3;++k)sources[k]={55,base,48,0,k*16};
     const auto rows=instance_rows(sources,0,2,10000,opt,size_of,mapped);
     check(rows.valid);receipt+=uint64_t(rows.rows[0]-memory)+rows.strides[0];
   }
   return std::array<double,3>{std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/120,double(reads),double(receipt)};
 };
 const auto off=bench(false),on=bench(true);check(off[2]==on[2] && on[1]<off[1]);
 std::printf("cpu_opt3 capture: 240-frame rows/material fixtures identical including handle reuse, rebind, epochs, host writes; 5000 draws/1500 streams row preparation OFF %.4f ON %.4f ms/frame; registry reads %.0f -> %.0f\n",off[0],on[0],off[1],on[1]);
}
