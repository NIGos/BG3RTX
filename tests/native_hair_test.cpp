#include "../common/native_hair.h"
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
 for(const auto& r:recipes){
  Captured c;unsigned calls=0;
  check(capture(r.hash,[&](unsigned set,unsigned binding,unsigned off,void* dst,size_t bytes){
   ++calls;std::memset(dst,0,bytes);
   if(set==1){check(binding==r.material && off==0 && bytes==(r.camera?48u:32u));float vals[12]={0,0,3,0.1f,4,5,0.4f,0.5f,0.7f,0.8f,0,0};std::memcpy(dst,vals,bytes);}
   else {check(set==0);if(binding==2 && off==192){const float nf[2]={0.1f,1000};std::memcpy(dst,nf,8);}}
   return true;
  },c));
  check(c.valid && c.material[2]==3 && c.material[3]==0.1f);
  check(c.material[8]==(r.camera?0.7f:4.0f));check(c.material[9]==(r.camera?0.8f:5.0f));
  check(calls==(r.camera?5u:1u));
  check(!capture(r.hash,[](unsigned,unsigned,unsigned,void*,size_t){return false;},c));check(!c.valid);
 }
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
