#include "../common/native_fur.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <tuple>
#include <vector>
using namespace bg3::native_fur;
static unsigned checks;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"Fur check %u failed\n",checks);std::exit(1);}}
using D3=std::array<long double,3>;
static long double dotd(D3 a,D3 b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static D3 norm(D3 a){auto s=std::sqrt(dotd(a,a));return {a[0]/s,a[1]/s,a[2]/s};}
static D3 widen(Vec3 a){return {a[0],a[1],a[2]};}
static long double oracle(Vec3 vv,Vec3 nn,Vec3 tt,Vec3 bb,float power){
 auto v=norm(widen(vv)),n=widen(nn),t=widen(tt),b=widen(bb);
 auto l=norm(D3{dotd(v,b),dotd(v,n),dotd(v,t)});
 auto w=norm(D3{b[0]*l[0]+n[0]*l[1]+t[0]*l[2],b[1]*l[0]+n[1]*l[1]+t[1]*l[2],b[2]*l[0]+n[2]*l[1]+t[2]*l[2]});
 auto base=std::max(std::abs(1-std::abs(dotd(w,norm(n)))),(long double)9.9999997473787516355514526367188e-05f);
 return std::clamp((1-std::pow(base,(long double)power))*5,0.L,1.L);
}
int main(){
 constexpr uint64_t camera_fs=0xc22b20253e25e473ull,camera_vs=0x0d6885be08b2faa4ull,
                   shadow_fs=0x1e144b6e40e48558ull,shadow_vs=0xff2a169c3d58bac8ull;
 check(lookup(camera_fs,camera_vs)->camera);check(!lookup(shadow_fs,shadow_vs)->camera);
 check(!lookup(camera_fs,shadow_vs));check(!lookup(shadow_fs,camera_vs));check(!lookup(0,camera_vs));
 std::array<float,8> material{0,.2f,.8f,3,1,0,0,0};
 std::array<float,2> frame{1,1};std::array<float,16> vp{};vp[0]=vp[5]=vp[10]=vp[15]=1;
 std::vector<std::tuple<unsigned,unsigned,unsigned,unsigned>> reads;
 int fail_read=-1;
 auto read=[&](unsigned set,unsigned binding,unsigned offset,void* target,unsigned bytes){
  const unsigned i=unsigned(reads.size());reads.emplace_back(set,binding,offset,bytes);if(int(i)==fail_read)return false;
  if(set==1 && binding==1 && offset==0 && (bytes==16 || bytes==32)){std::memcpy(target,material.data(),bytes);return true;}
  if(set==0 && binding==0 && offset==8 && bytes==8){std::memcpy(target,frame.data(),bytes);return true;}
  if(set==0 && binding==2 && offset==128 && bytes==64){std::memcpy(target,vp.data(),bytes);return true;}
  return false;
 };
 Captured c;check(capture(camera_fs,camera_vs,read,c));check(c.valid && c.camera && reads.size()==3);
 check(reads[0]==std::make_tuple(1u,1u,0u,16u));check(reads[1]==std::make_tuple(0u,0u,8u,8u));check(reads[2]==std::make_tuple(0u,2u,128u,64u));
 auto captured_camera=c;
 reads.clear();check(capture(shadow_fs,shadow_vs,read,c));check(!c.camera && reads.size()==1 && std::get<3>(reads[0])==32);
 auto captured_shadow=c;
 for(int i=0;i<3;++i){reads.clear();fail_read=i;check(!capture(camera_fs,camera_vs,read,c));check(!c.valid);}
 fail_read=-1;reads.clear();check(!capture(camera_fs,shadow_vs,read,c));check(reads.empty());
 for(float phase:{-1.f,.5f,16777216.f,INFINITY}){frame[0]=phase;reads.clear();check(!capture(camera_fs,camera_vs,read,c));check(!c.valid);}
 frame[0]=1;material[3]=NAN;reads.clear();check(!capture(shadow_fs,shadow_vs,read,c));material[3]=3;
 Vec3 n{0,1,0},t{1,0,0},b{0,0,1},view{0,1,0};
 c=captured_shadow;auto cov=coverage(c,1,1,view,n,t,b);check(cov.known() && cov.value==1);
 check(shadow(c,cov).known() && shadow(c,cov).survives);
 c.material[4]=0;check(!shadow(c,cov).survives);c.material[4]=1;
 cov=coverage(c,1,0,view,n,t,b);check(cov.known() && cov.value==0 && !shadow(c,cov).survives);
 // Real catalog defaults have descending edges. They are explicitly UNKNOWN,
 // never sorted or silently treated as a raw-alpha/opaque recipe.
 c.material[1]=1;c.material[2]=.6f;check(coverage(c,1,.8f,view,n,t,b).unknown==Unknown::SmoothStepOrder);
 c.material[2]=1;check(coverage(c,1,.8f,view,n,t,b).unknown==Unknown::SmoothStepOrder);
 c.material[1]=-std::numeric_limits<float>::max();c.material[2]=std::numeric_limits<float>::max();check(coverage(c,1,.8f,view,n,t,b).unknown==Unknown::Nonfinite);
 c=captured_shadow;check(coverage(c,NAN,1,view,n,t,b).unknown==Unknown::Nonfinite);
 check(!angular({0,0,0},n,t,b,3).known());check(!angular(view,{0,0,0},t,b,3).known());
 c=captured_camera;c.frame={0,1};check(camera(c,{0},0,0,0).survives); // step(0,0) is one.
 c.frame={1,1};check(!camera(c,{std::nextafter(.5f,0.f)},0,0,0).survives);check(camera(c,{.5f},0,0,0).survives);
 c.frame={8,1};check(camera(c,{0},0,0,0).survives);c.frame={1,0};check(camera(c,{0},0,0,0).survives);
 check(!camera(c,{1},0,-1,0).known());check(!camera(c,{1},0,2147483648.f,0).known());
 check(!shadow(c,{1}).known());check(!camera(captured_shadow,{1},0,0,0).known());
 std::mt19937 rng(0xc22b2025);std::uniform_real_distribution<float> unit(0,1);
 // Well-conditioned, non-orthogonal interpolated frames. The independent long
 // double oracle retains both TBN transforms. gamma512 is a conservative numeric
 // regression bound, not a shader/driver proof or a runtime acceptance threshold.
 constexpr long double u=std::numeric_limits<float>::epsilon()/2;
 constexpr long double gamma512=512*u/(1-512*u);
 for(unsigned i=0;i<10000;++i){
  Vec3 nn{.15f*unit(rng),1,.15f*unit(rng)},tt{1,.15f*unit(rng),.15f*unit(rng)},bb{.15f*unit(rng),.15f*unit(rng),1};
  Vec3 vv{.2f+unit(rng),unit(rng)*.4f,.2f+unit(rng)};float power=.25f+unit(rng)*4;
  auto a=angular(vv,nn,tt,bb,power);check(a.known());
  auto expected=oracle(vv,nn,tt,bb,power);check(std::abs((long double)a.value-expected)<=gamma512);
  Vec3 flipped{-nn[0],-nn[1],-nn[2]};auto flipped_value=angular(vv,flipped,tt,bb,power);
  check(flipped_value.known() && flipped_value.value==a.value);
  c=captured_shadow;c.material[3]=power;float alpha=unit(rng),blue=unit(rng);
  cov=coverage(c,alpha,blue,vv,nn,tt,bb);check(cov.known());
  auto x=std::clamp(((long double)blue-c.material[1])/((long double)c.material[2]-c.material[1]),0.L,1.L);
  auto expected_cov=alpha*x*x*(3-2*x)*expected;
  check(std::abs((long double)cov.value-expected_cov)<=gamma512);
  if(std::abs(expected_cov-(long double)0.33329999446868896484375f)>gamma512)
   check(shadow(c,cov).survives==(expected_cov>=0.33329999446868896484375f));
 }
 std::printf("native Fur: %u CPU capture/coverage checks; no runtime or GPU claim\n",checks);
}
