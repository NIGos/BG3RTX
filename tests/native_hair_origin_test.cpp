#include "../common/native_hair_origin.h"
#include "../common/native_hair_math.h"
#include "../common/settings.h"
#include <bit>
#include <cstdio>
#include <limits>
#include <cstdlib>
using namespace bg3::hair_origin;
using namespace bg3::hair_math;
static int checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
float offset(float p,float n){
 int v=int(256*n);int bits=std::bit_cast<int>(p);
 float ulp=std::bit_cast<float>(bits+(p<0?-v:v));
 return std::abs(p)<1.f/32?p+(1.f/65536)*n:ulp;
}
int main(){
 uint32_t off=0,on=0;
 CHECK(!capability(&off,false));CHECK(!capability(&off,true)); // hot enable/core reload cannot invent resources
 CHECK(capability(&on,true));CHECK(capability(&on,false));CHECK(capability(&on,true)); // resources survive live A/B
 CHECK(!binding_enabled(true,8,12));CHECK(binding_enabled(false,8,12));CHECK(binding_enabled(true,4,12));
 for(bool enabled:{false,true}){unsigned count=1+unsigned(enabled);for(unsigned i=0;i<31;i++)count+=binding_enabled(enabled,8,i);CHECK(count==32);}
 CHECK(hpo_unit_normal({0,0,1}));CHECK(!hpo_unit_normal({0,0,.8f}));
 CHECK(alternate_entry("gi/rt_origin.hlsl"));CHECK(alternate_entry("gi/rt_reflections_sharc.hlsl"));
 CHECK(!alternate_entry("gi/pan_primary.hlsl")); // primary/history semantics unchanged
 const auto* knob=bg3::settings::find_knob("exp_hair_physical_origin");
 CHECK(knob&&knob->experimental&&knob->apply==bg3::settings::Apply::Partial);
 for(int preset:knob->preset)CHECK(preset==0);
 // Actual shared ray-triangle geometry: virtual receiver re-hits its own card,
 // physical launch does not, for both viewing signs and different scene scales.
 for(float sign:{-1.f,1.f})for(float z:{.001f,1.f,100.f,10000.f}) {
  float physical=sign*z,virt=physical*1.1f,n=-sign,d=-sign;
  float3 a={-z,-z,physical},b={z,-z,physical},c={0,z,physical};
  auto old_hit=hm_ray_triangle(a,b,c,{0,0,offset(virt,n)},{0,0,d});
  CHECK(old_hit.status==1&&old_hit.t>0);
  auto exact=hm_ray_triangle(a,b,c,{0,0,0},{0,0,sign});
  CHECK(exact.status==1&&exact.t>0);
  auto point=hpo_reconstruct({0,0,virt},exact.t);
  CHECK(point.z==physical);
  auto fixed_hit=hm_ray_triangle(a,b,c,{0,0,offset(point.z,n)},{0,0,d});
  CHECK(fixed_hit.status==1&&fixed_hit.t<0);
 }
 // Rebased primitive coordinates preserve the same selected physical distance.
 float3 a={-1,-1,101},b={1,-1,101},c={0,1,101};
 auto rebased=hm_ray_triangle(a,b,c,{0,0,100},{0,0,1});CHECK(rebased.status==1&&rebased.t==1);
 auto rebase_point=hpo_reconstruct({0,0,1.1f},rebased.t);CHECK(rebase_point.z==1);
 // Point/finite-emitter rays must reach the actual sampled target after the
 // centimetre-scale receiver shift. Reusing the virtual light direction fails.
 OriginVec3 receiver={0,0,1},origin={0,0,.999f},center={2,0,3};
 const double endpoint_bound=(32*0x1p-24/(1-32*0x1p-24))*4; // gamma32, coordinate/segment magnitudes <4.
 auto point_light=hpo_light_segment(receiver,origin,center,{0,0,1},0,0);
 CHECK(point_light.valid);
 CHECK(std::abs(origin.x+point_light.direction.x*point_light.tmax-center.x)<=endpoint_bound);
 CHECK(std::abs(origin.z+point_light.direction.z*point_light.tmax-center.z)<=endpoint_bound);
 float norm=std::sqrt(8.f);OriginVec3 sample={2/norm,0,2/norm};
 auto sphere=hpo_light_segment(receiver,origin,center,sample,.5f,0);
 CHECK(sphere.valid);
 double tx=origin.x+sphere.direction.x*sphere.tmax,tz=origin.z+sphere.direction.z*sphere.tmax;
 double radial=std::sqrt((tx-center.x)*(tx-center.x)+(tz-center.z)*(tz-center.z));
 CHECK(std::abs(radial-.5)<=endpoint_bound);
 auto cleared=hpo_light_segment(receiver,origin,center,sample,.5f,1.f);CHECK(cleared.valid);
 CHECK(std::abs((sphere.tmax-cleared.tmax)-.5f)<=endpoint_bound);
 float angle=.1f;OriginVec3 oblique={(std::cos(angle)-std::sin(angle))/std::sqrt(2.f),0,(std::cos(angle)+std::sin(angle))/std::sqrt(2.f)};
 auto soft=hpo_light_segment(receiver,origin,center,oblique,.5f,0);CHECK(soft.valid);
 double sx=origin.x+soft.direction.x*soft.tmax,sz=origin.z+soft.direction.z*soft.tmax;
 CHECK(std::abs(std::sqrt((sx-center.x)*(sx-center.x)+(sz-center.z)*(sz-center.z))-.5)<=endpoint_bound);
 CHECK(std::abs((sx-receiver.x)*oblique.z-(sz-receiver.z)*oblique.x)<=endpoint_bound);
 auto inside=hpo_light_segment(center,center,center,sample,.5f,0);CHECK(inside.valid&&inside.tmax==0);
 auto zero=hpo_light_segment(center,center,center,sample,0,0);CHECK(zero.valid&&zero.tmax==0);
 auto beyond=hpo_light_segment(receiver,{3,0,4},center,sample,0,0);CHECK(beyond.valid&&beyond.tmax==0);
 auto miss=hpo_light_segment(receiver,origin,center,{0,0,-1},.5f,0);CHECK(!miss.valid);
 auto negative=hpo_light_segment(receiver,origin,center,sample,-1,0);CHECK(!negative.valid);
 // Direct distance avoids offset-ratio cancellation over a wide finite range.
 // gamma16 bounds normalized direction + scalar reconstruction arithmetic;
 // this is an encoding bound, not a native raster/GPU certificate.
 const double u=0x1p-24,gamma16=16*u/(1-16*u);double worst=0;
 for(float virtual_scale:{1e-20f,1.f,1e20f})for(float distance:{1e-20f,1.f,1e20f}) {
  OriginVec3 input={virtual_scale*.3f,virtual_scale*-.4f,virtual_scale*.5f};
  auto point=hpo_reconstruct(input,distance);
  double len=std::sqrt(double(input.x)*input.x+double(input.y)*input.y+double(input.z)*input.z);
  float values[]={point.x,point.y,point.z},coords[]={input.x,input.y,input.z};
  for(unsigned j=0;j<3;j++){
   double expected=double(coords[j])/len*distance,error=std::abs(values[j]-expected);
   CHECK(std::isfinite(values[j])&&error<=gamma16*distance);
   worst=std::max(worst,error/distance);
  }
 }
 std::printf("%d native Hair physical-origin checks passed; max normalized encoding error %.9g\n",checks,worst);
}
