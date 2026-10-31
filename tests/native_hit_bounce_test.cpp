// CPU-only integration of the shared Nt BSDF/frame, throughput and path sum.
#include "../common/native_hit_bounce.h"
#include "../common/settings.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3::transport;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static bool near(float value,double expected){const double u=0x1p-24,gamma64=64*u/(1-64*u);return std::fabs(value-expected)<=gamma64*std::fmax(1,std::fabs(expected));}
int main(){
 NtFresnelBlendResult material{};material.status=NtOk;material.value.Rd={0,0,0};material.value.Rs={1,1,1};material.value.alpha=1;
 // At wo=wi=N, alpha1/Rs1: f=1/(4pi), mixture PDF=.5*(1/pi+1/(4pi)), so f*cos/pdf=2/5.
 auto normal=hb_continuation(material,{0,0,1},{0,0,1},{0,0,1},.9f,0,.25f);
 CHECK(normal.status==HbPathComplete);CHECK(near(normal.throughput.x,.4));CHECK(near(normal.throughput.y,.4));CHECK(near(normal.throughput.z,.4));CHECK(near(normal.direction.z,1));
 auto sum=hb_compose({2,3,4},normal.throughput,{5,7,11});
 CHECK(sum.status==HbPathComplete);CHECK(near(sum.radiance.x,4));CHECK(near(sum.radiance.y,5.8));CHECK(near(sum.radiance.z,8.4));
 // Omission or duplication of cosine/PDF/lobe-mixture factors changes the known analytic result above.
 unsigned nulls=0,hits=0;
 for(unsigned i=0;i<64;++i){float u=(i+.5f)/64;
  auto ndf=hb_continuation(material,{0,0,1},{0,0,1},{0,0,1},.1f,u,.37f);
  CHECK(ndf.status==(i<32?HbPathComplete:HbPathNull));
  if(ndf.status==HbPathNull){++nulls;CHECK(ndf.throughput.x==0&&ndf.direction.z==0);}else ++hits;
  auto cosine=hb_continuation(material,{0,0,1},{0,0,1},{0,0,1},.9f,u,.37f);
  CHECK(cosine.status==HbPathComplete);CHECK(near(tb_dot(cosine.direction,cosine.direction),1));CHECK(cosine.throughput.x>=0&&tb_finite3(cosine.throughput));
 }
 CHECK(nulls==32&&hits==32); // No retry or conditional-PDF renormalization of rejected NDF samples.
 for(TbVec3 n: {TbVec3{1,0,0},TbVec3{0,-1,0},TbVec3{0,0,-1},TbVec3{1,2,3},TbVec3{-3,.01f,2}}){
  auto d=tb_direction(n);auto r=hb_continuation(material,n,n,n,.9f,.3f,.31f);
  CHECK(r.status==HbPathComplete);CHECK(near(tb_dot(r.direction,d.v),std::sqrt(.7)));CHECK(near(tb_dot(r.direction,r.direction),1));
  // Same polar sample under a coordinate rotation has identical isotropic weight.
  auto z=hb_continuation(material,{0,0,1},{0,0,1},{0,0,1},.9f,.3f,.31f);CHECK(near(r.throughput.x,z.throughput.x));
 }
 CHECK(hb_continuation(material,{0,0,1},{0,0,-1},{0,0,1},.9f,.3f,.2f).status==HbPathNormalDomain);
 CHECK(hb_continuation(material,{0,0,1},{1,0,.01f},{0,0,1},.9f,.9f,.75f).status==HbPathNormalDomain);
 CHECK(hb_continuation(material,{0,0,0},{0,0,1},{0,0,1},.9f,.3f,.2f).status==HbPathSample);
 CHECK(hb_continuation(material,{0,0,1},{0,0,1},{0,0,1},1,.3f,.2f).status==HbPathSample);
 material.status=NtUnknownSamples;CHECK(hb_continuation(material,{0,0,1},{0,0,1},{0,0,1},.9f,.3f,.2f).status==HbPathSample);
 material.status=NtOk;material.value.alpha=0;CHECK(hb_continuation(material,{0,0,1},{0,0,1},{0,0,1},.9f,.3f,.2f).status==HbPathSample);
 auto overflow=hb_compose({2,3,4},{1e30f,1,1},{1e30f,1,1});CHECK(overflow.status==HbPathNumeric&&overflow.radiance.x==2);
 CHECK(hb_compose({2,3,4},{1,1,1},{-1,0,0}).status==HbPathNumeric);
 CHECK(hb_compose({2,3,4},{std::numeric_limits<float>::quiet_NaN(),1,1},{1,1,1}).status==HbPathNumeric);
 auto zero=hb_compose({2,3,4},{0,0,0},{1e30f,1e30f,1e30f});CHECK(zero.status==HbPathComplete&&zero.radiance.x==2);
 const auto* knob=bg3::settings::find_knob("exp_hit_reference");CHECK(knob&&knob->experimental&&knob->apply==bg3::settings::Apply::Live);
 for(int value:knob->preset)CHECK(value==0);
 CHECK(std::string(bg3::settings::code_default("rt_hit_reference"))=="0");
 std::printf("%u native two-surface reference CPU checks passed; NDF null 32/64 retained\n",checks);
}
