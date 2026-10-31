#include "../common/transport_frame.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3::transport;
static unsigned checks;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"frame check %u failed\n",checks);std::exit(1);}}
static double dot(TbVec3 a,TbVec3 b){return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;}
static double error(TbVec3 a,TbVec3 b){return std::fmax(std::fabs(double(a.x)-b.x),std::fmax(std::fabs(double(a.y)-b.y),std::fabs(double(a.z)-b.z)));}
static void valid(TbVec3 normal){
 // Conservative CPU rounding allowance for normalization, two crosses and
 // three-term transforms; not a GPU precision or rendering acceptance gate.
 const double eps=std::numeric_limits<float>::epsilon(),bound=64*eps;
 auto f=tf_frame(normal);check(f.status==TbOk);
 // Independent double-precision normalization and matrix invariants.
 double length=std::sqrt(dot(normal,normal));
 check(std::fabs(f.n.x-normal.x/length)<=16*eps && std::fabs(f.n.y-normal.y/length)<=16*eps && std::fabs(f.n.z-normal.z/length)<=16*eps);
 check(std::fabs(dot(f.t,f.t)-1)<=bound && std::fabs(dot(f.b,f.b)-1)<=bound && std::fabs(dot(f.n,f.n)-1)<=bound);
 check(std::fabs(dot(f.t,f.b))<=bound && std::fabs(dot(f.t,f.n))<=bound && std::fabs(dot(f.b,f.n))<=bound);
 // Independent determinant: +1, so both hemispheres and handedness survive.
 double determinant=f.t.x*(double(f.b.y)*f.n.z-double(f.b.z)*f.n.y)-f.b.x*(double(f.t.y)*f.n.z-double(f.t.z)*f.n.y)+f.n.x*(double(f.t.y)*f.b.z-double(f.t.z)*f.b.y);
 check(std::fabs(determinant-1)<=bound);
 for(auto v:std::array<TbVec3,4>{{{1,0,0},{0,-1,0},{0,0,1},{3.25f,-4.5f,7.125f}}}){
  auto local=tf_to_local(f,v),world=tf_to_world(f,v);
  double scale=std::fabs(v.x)+std::fabs(v.y)+std::fabs(v.z);
  check(error(tf_to_world(f,local),v)<=bound*scale);
  check(error(tf_to_local(f,world),v)<=bound*scale);
  check(std::fabs(dot(local,local)-dot(v,v))<=bound*scale*scale);
  check(std::fabs(local.z-dot(f.n,v))<=bound*scale);
 }
}
int main(){
 for(float s:{-1.f,1.f})for(auto n:std::array<TbVec3,6>{{{s,0,0},{0,s,0},{0,0,s},{1.e-7f,-1.e-7f,s},{s,s,s},{s,s,-s}}})valid(n);
 for(int i=1;i<=192;++i){
  float a=float((i*37)%101-50),b=float((i*53)%103-51),c=float((i*71)%107-53);
  float scale=std::ldexp(1.f,(i%81)-40);valid({a*scale,b*scale,c*scale});
 }
 valid({std::numeric_limits<float>::max(),1,0});
 valid({std::numeric_limits<float>::denorm_min(),0,0});
 for(auto n:std::array<TbVec3,4>{{{0,0,0},{NAN,0,1},{0,INFINITY,0},{-INFINITY,0,0}}}){
  auto f=tf_frame(n);check(f.status==TbInvalid);
  check(error(tf_to_world(f,{1,2,3}),{0,0,0})==0);
  check(error(tf_to_local(f,{1,2,3}),{0,0,0})==0);
 }
 std::printf("Transport frame: %u CPU checks, independent normalization/determinant/isometry; no GPU claim\n",checks);
}
