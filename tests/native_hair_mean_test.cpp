// CPU reference of the precompute control flow, with virtual huge inputs.
#include "../common/native_hair_probability.h"
#include "../common/native_hair_math.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
using namespace bg3::native_hair_probability;
static unsigned checks=0;
static void check(bool v){++checks;if(!v)std::abort();}
struct Result {unsigned reads=0,writes=0,capped=0,samples=0;float probability=-1;};
static Result precompute(uint32_t base,uint32_t allocation,uint32_t tris,uint32_t owned,
                         uint32_t pool,uint32_t source,uint32_t stride,uint32_t source_bytes,
                         uint32_t first_live,float coverage=100.0f){
 Result r;
 if(!nh_mean_range(base,allocation,tris,owned,pool))return r;
 // Model each Store, proving both tail words stay in this draw (neighbours canary).
 auto store=[&](uint32_t at){check(at>=base && uint64_t(at)+4<=uint64_t(base)+allocation);++r.writes;};
 const uint32_t tail=base+tris*24u;store(tail);store(tail+4u);
 if(stride<88u || (stride&3u) || (source&3u) || source>source_bytes ||
    tris>(source_bytes-source)/(3u*stride))return r;
 bool found=false;
 for(uint32_t i=0;i<nh_scan_count(tris);++i){
  const uint64_t at=uint64_t(source)+uint64_t(i)*3u*stride;
  check(at+60u<=source_bytes);++r.reads;
  if(i==first_live){found=true;break;}
 }
 if(!found){r.capped=tris>nh_scan_count(tris);store(tail+4u);return r;}
 // Analytic orthonormal strand fixture; execute the shader's two transforms.
 using namespace bg3::hair_math;
 const float3 n={0,0,1},t={0,1,0},b={1,0,0};
 auto norm=[](float3 v){return v/std::sqrt(dot(v,v));};
 float angular[8];
 for(unsigned d=0;d<8;++d){
  float z=(float(d)+0.5f)/8.0f,phi=float(d)*2.399963229728653f;
  float radius=std::sqrt((std::max)(0.0f,1.0f-z*z));
  float3 v={radius*std::cos(phi),radius*std::sin(phi),z};
  float3 local=norm({dot(v,b),dot(v,n),dot(v,t)});
  float3 world=norm(b*local.x+n*local.y+t*local.z);
  angular[d]=(std::min)(std::abs(dot(world,norm(n)))*5.0f,1.0f);
  if(!std::isfinite(angular[d]))return r;
 }
 unsigned hits=0;
 for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
  r.samples+=3; // two mask samples and noise; constant material fixture
  for(unsigned d=0;d<8;++d){if(!std::isfinite(coverage))return r;hits+=coverage*angular[d]>=0.3333f;}
 }
 r.probability=nh_probability(hits,512u);store(tail);return r;
}
int main(){
 constexpr uint32_t U=UINT32_MAX;
 // Valid probability endpoints and hard threshold, shared exact 512-decision estimator.
 for(float c: {0.0f,0.3332f,1.0f,100.0f}){
  auto r=precompute(128,24*65+8,65,65,4096,0,88,65*264,0,c);
  unsigned expected=0;
  for(unsigned d=0;d<8;++d)expected+=double(c)*(std::min)(5.0*(d+0.5)/8.0,1.0)>=double(0.3333f);
  check(r.probability==float(expected)/8.0f);check(r.samples==192 && r.reads==1 && r.writes==3);
 }
 auto run=[](uint32_t n,uint32_t live){return precompute(0,n*24+8,n,n,U,0,88,U,live);};
 check(run(65,63).probability==1); // last admitted candidate
 check(run(65,64).capped==1 && run(65,64).probability==-1); // fallback, never transparent
 check(run(64,U).capped==0 && run(64,U).reads==64);
 check(run(10000000,U).capped==1 && run(10000000,U).reads==64); // no huge host allocation
 check(run(10000000,0).reads==1);
 check(run(0,0).writes==0);
 check(precompute(0,U,U,U,U,0,88,U,0).writes==0); // multiplication overflow
 check(precompute(U-3,64,1,1,U,0,88,U,0).writes==0);
 check(precompute(1,64,1,1,4096,0,88,264,0).writes==0);
 check(precompute(128,24,1,1,4096,0,88,264,0).writes==0); // neighbour is in pool, tail is not owned
 check(precompute(128,28,1,1,4096,0,88,264,0).writes==0); // counter also must fit
 check(precompute(128,64,2,1,4096,0,88,528,0).writes==0); // wrong draw/count
 check(precompute(128,64,1,1,4096,U-3,88,U,0).probability==-1);
 check(precompute(128,64,1,1,4096,0,87,264,0).reads==0);
 check(precompute(128,64,1,1,4096,0,88,263,0).reads==0);
 check(precompute(128,64,1,1,4096,0,88,264,0,std::numeric_limits<float>::quiet_NaN()).probability==-1);
 // Independent 64-bit arithmetic oracle across adversarial offsets/capacities/counts.
 for(uint32_t base: {0u,1u,128u,U-3,U})for(uint32_t size: {0u,4u,8u,24u,28u,32u,4096u,U})
 for(uint32_t n: {0u,1u,64u,65u,10000000u,U})for(uint32_t pool: {0u,4096u,U}){
  bool oracle=n && !(base&3u) && uint64_t(base)+size<=pool && uint64_t(n)*24+8<=size;
  check(nh_mean_range(base,size,n,n,pool)==oracle);
 }
 std::printf("hair mean: %u checks passed\n",checks);
}
