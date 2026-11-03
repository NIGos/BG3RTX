#include "../common/native_hair.h"
#include "../common/native_hair_probability.h"
#include "../common/native_hair_visibility.h"
#include "../layer/spirv_xfb.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
using namespace bg3::native_hair;
unsigned count=0;
void check(bool v){++count;if(!v){std::fprintf(stderr,"check%u failed\n",count);std::exit(1);}}
int main(){
 // Every estimate of the 512-sample offline quadrature is binary-exact.
 // A stratified uniform sweep must preserve its expected hard-hit count,
 // including both endpoints; no scene-energy equivalence is inferred.
 using namespace bg3::native_hair_probability;
 check(nh_probability(0,0)==-1.0f);
 check(nh_probability(1,0)==-1.0f);
 check(nh_probability(513,512)==-1.0f);
 for(unsigned hits=0;hits<=512;++hits) {
  const float p=nh_probability(hits,512);
  check(double(p)==double(hits)/512.0);
  unsigned accepted=0;
  for(unsigned bin=0;bin<512;++bin)
   accepted+=unsigned(nh_bernoulli(p,bin*32768u+16384u));
  check(accepted==hits);
 }
 check(!nh_bernoulli(0.0f,0u));
 check(nh_bernoulli(1.0f,0xffffffu));
 check(nh_bernoulli(0.5f,0x7fffffu));
 check(!nh_bernoulli(0.5f,0x800000u));
 check(!nh_bernoulli(-1.0f,0u));
 check(!nh_bernoulli(2.0f,0u));
 check(!nh_bernoulli(std::numeric_limits<float>::quiet_NaN(),0u));
 check(!nh_bernoulli(1.0f,0x1000000u));

 {
  namespace hv=bg3::hair_visibility;
  // Perspective x/z,y/z camera at zero; independent projected-box fixtures.
  const float vp[16]={1,0,0,0, 0,1,0,0, 0,0,1,1, 0,0,0,0};
  const float model[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  check(hv::model_valid(model));
  float tiny_model[16];std::memcpy(tiny_model,model,sizeof model);
  tiny_model[0]=tiny_model[5]=tiny_model[10]=1e-30f;
  check(!hv::model_valid(tiny_model));
  float ill_model[16];std::memcpy(ill_model,model,sizeof model);ill_model[10]=1e-30f;
  check(!hv::model_valid(ill_model));
  hv::Bounds invalid_model_box;const float origin_vertex[3]={0,0,0};
  check(!hv::add(invalid_model_box,origin_vertex,tiny_model));
  hv::Bounds tiny;const float a[3]={-0.001f,-0.001f,10},b[3]={0.001f,0.001f,10};
  check(hv::add(tiny,a,model));check(hv::add(tiny,b,model));
  check(hv::select(tiny,vp,1000,1000,1)==hv::Decision::Cards);
  check(hv::select(tiny,vp,1000,1000,12)==hv::Decision::Native);
  hv::Bounds large=tiny;large.lo[0]=-1;large.hi[0]=1;
  check(hv::select(large,vp,1000,1000,1)==hv::Decision::Native);
  float bad[16]{};check(hv::select(tiny,bad,1000,1000,1)==hv::Decision::Unknown);
  check(hv::select(hv::Bounds{},vp,1000,1000,1)==hv::Decision::Unknown);
  check(hv::select(tiny,vp,0,1000,1)==hv::Decision::Unknown);
  auto windy=tiny;check(hv::expand(windy,hv::wind(10,1,1,1)));
  check(hv::select(windy,vp,1000,1000,1)==hv::Decision::Native);
  const auto calm=hv::wind(0,1,1,1);check(calm[0]==0 && calm[1]==0 && calm[2]==0);
  auto behind=tiny;behind.lo[2]=-10;behind.hi[2]=-9;
  check(hv::select(behind,vp,1000,1000,1)==hv::Decision::Unknown);
  auto nan=tiny;nan.lo[0]=std::numeric_limits<double>::quiet_NaN();
  check(hv::select(nan,vp,1000,1000,1)==hv::Decision::Unknown);
 }
 for(const auto& r:recipes){
  Captured c;unsigned calls=0,seen=0;
  check(capture(r.hash,[&](unsigned set,unsigned binding,unsigned off,void* dst,size_t bytes){
   ++calls;std::memset(dst,0,bytes);
   if(set==1){
    check(binding==r.material && off==0 && bytes==(r.layout==Layout::Pubes?(r.camera?32u:16u):(r.camera?48u:32u)));
    float vals[12]={0,0,3,0.1f,4,5,0.4f,0.5f,0.7f,0.8f,0,0};
    if(r.layout==Layout::Pubes){const float pubes[8]={3,0.1f,4,5,0.4f,0.5f,0.7f,0};std::memcpy(vals,pubes,32);if(!r.camera)vals[2]=0.7f;}
    std::memcpy(dst,vals,bytes);
   }
   else {
    check(set==0);
    if(binding==0 && off==8){check(bytes==8);const float frame[2]={37,1};std::memcpy(dst,frame,8);seen|=1;}
    else if(binding==2 && off==128){check(bytes==64);float matrix[16]={};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;std::memcpy(dst,matrix,64);seen|=2;}
    else if(binding==2 && off==192){check(bytes==8);const float nf[2]={0.1f,1000};std::memcpy(dst,nf,8);seen|=4;}
    else if(binding==2 && off==124){check(bytes==4);const float ortho=1;std::memcpy(dst,&ortho,4);seen|=8;}
    else check(false);
   }
   return true;
  },c));
  check(c.valid && c.material[2]==3 && c.material[3]==0.1f);
  check(c.material[8]==(r.layout==Layout::Pubes?0.7f:(r.camera?0.7f:4.0f)));
  check(c.material[9]==(r.layout==Layout::Pubes?1.0f:(r.camera?0.8f:5.0f)));
  check(c.subtype==uint32_t(r.layout));
  if(r.camera)check(c.material[4]==4 && c.material[5]==5 && c.material[6]==0.4f && c.material[7]==0.5f);
  else check(c.material[4]==0 && c.material[5]==0);
  check(calls==(r.camera?5u:1u));
  check(seen==(r.camera?15u:0u));
  if(r.camera){check(c.frame[0]==37 && c.frame[1]==1 && c.orthographic==1);check(c.view_projection[5]==1 && c.near_far[1]==1000);}
  check(!capture(r.hash,[](unsigned,unsigned,unsigned,void*,size_t){return false;},c));check(!c.valid && c.subtype==0);
 }
 for(uint64_t fs:{0x2f2c46668da906a2ull,0xbf44216269789070ull}) {
  const auto* r=lookup(fs);check(r && r->material==1 && r->noise==4 && r->mask==5 && r->layout==Layout::Pubes);
 }
 // Only the two verified static recipes are enabled, not unreviewed instancing.
 check(!lookup(0x102b58c38ce6d991ull));check(!lookup(0xb33cb854a349f3e3ull));
 Captured bad;check(!capture(0,[](unsigned,unsigned,unsigned,void*,size_t){return true;},bad));
 // Native combined mask differs from a lone A read and from componentwise max
 // in the centered red channel when opposing signed offsets have unequal size.
 auto m=combine_masks({0.9f,0.2f,0.3f,0.4f},{0.0f,0.8f,0.7f,0.1f});
 check(m[0]==1.0f && m[1]==0.8f && m[2]==0.7f && m[3]==0.4f);
 check(coverage({0,0,1,1},1,1,1)==0);
 check(coverage({0,0,0,1},1,0.25f,1)==0.25f);
 check(coverage({0,0,0,1},0,1,0.25f)==0.25f);
 // Do not simplify the interpolated nonorthogonal strand-frame projection.
 // Both front-face signs must agree, including skewed/scaled bases.
 unsigned angular_cases=0;
 for(int x=-8;x<=8;++x)for(int y=-8;y<=8;++y)for(int z=-8;z<=8;++z){
  if(!x&&!y&&!z)continue;
  Vec3 n{0.1f,0.8f,0.2f},t{1,0.2f,0.1f},b{0.1f,0.3f,0.9f};float a=0,rev=0;
  check(angular({float(x),float(y),float(z)},n,t,b,a));check(angular({float(x),float(y),float(z)},scale(n,-1),t,b,rev));
  check(a==rev);check(a>=0 && a<=1);++angular_cases;
 }
 float a=0;check(!angular({0,0,0},{0,1,0},{1,0,0},{0,0,1},a));
 // Synthetic SPIR-V interface (patcher's input parser needs no executable body).
 std::vector<uint32_t> code={0x07230203,0x10000,0,100,0};
 auto emit=[&](std::initializer_list<uint32_t> v){code.insert(code.end(),v);};
 emit({(2u<<16)|17,1});emit({(5u<<16)|15,0,50,0x6e69616d,0});
 emit({(4u<<16)|71,20,11,0});
 for(uint32_t i=0;i<6;++i)emit({(4u<<16)|71,30+i,30,i});
 emit({(3u<<16)|22,1,32});emit({(4u<<16)|23,2,1,2});emit({(4u<<16)|23,3,1,3});emit({(4u<<16)|23,4,1,4});
 emit({(4u<<16)|32,5,3,2});emit({(4u<<16)|32,6,3,3});emit({(4u<<16)|32,7,3,4});emit({(4u<<16)|59,7,20,3});
 for(uint32_t i=0;i<6;++i)emit({(4u<<16)|59,i==0?5u:i==5?7u:6u,30+i,3});
 std::vector<uint32_t> patched,ordinary;int uv=-1;uint32_t attrs=0;
 check(bg3::patch_vs(code.data(),code.size(),patched,88,&uv,&attrs));check(uv==0 && attrs==3);
 check(bg3::patch_vs(code.data(),code.size(),ordinary,24,&uv,&attrs));check(attrs==0);
 // Flat N is not the smooth native strand interface; refuse attribute proof.
 emit({(3u<<16)|71,31,14});check(bg3::patch_vs(code.data(),code.size(),patched,88,&uv,&attrs));check(attrs==0);
 std::printf("%u checks, %u strand sign-invariance cases\n",count,angular_cases);
}
