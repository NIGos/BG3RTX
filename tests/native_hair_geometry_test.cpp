#include "../common/native_hair_math.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3::hair_math;
static unsigned checks=0;
static void check(bool v){++checks;if(!v){std::fprintf(stderr,"geometry check %u failed\n",checks);std::exit(1);}}
static long double dot_ref(float3 a,float3 b){return (long double)a.x*b.x+(long double)a.y*b.y+(long double)a.z*b.z;}
static long double clip(HairProjection p,float3 q,unsigned axis){
 if(axis==0)return dot_ref(p.rx,q)+p.translation.x;
 if(axis==1)return dot_ref(p.ry,q)+p.translation.y;
 if(axis==2)return dot_ref(p.rz,q)+p.translation.z;
 return dot_ref(p.rw,q)+p.translation.w;
}
static bool inside_xy(float3 a,float3 b,float3 c,float3 q){
 auto edge=[](float3 u,float3 v,float3 p){return ((long double)v.x-u.x)*((long double)p.y-u.y)-((long double)v.y-u.y)*((long double)p.x-u.x);};
 const auto x=edge(a,b,q),y=edge(b,c,q),z=edge(c,a,q);
 return (x>=0&&y>=0&&z>=0)||(x<=0&&y<=0&&z<=0);
}
int main(){
 constexpr long double e=std::numeric_limits<float>::epsilon(),gamma=(128*e)/(1-128*e);
 unsigned perspective=0,orthographic=0;
 for(int camera=0;camera<2;++camera)for(int shift=-2;shift<=2;++shift)for(int x=-6;x<=6;++x)
 for(int y=-4;y<=4;++y)for(int z=2;z<=20;z+=3)for(int hh=0;hh<=4;++hh){
  const float near=0.1f,far=1000,den=near-far,A=near/den,B=-near*far/den;
  float3 center{float(shift),float(-shift),0.5f*shift};
  HairProjection p{};p.rx={1.3f,0,0};p.ry={0,1.7f,0};p.near_far={near,far};p.ortho=camera!=0;
  p.rz={0,0,camera?1.0f/den:A};p.rw={0,0,camera?0.0f:1.0f};
  p.translation={-dot(p.rx,center),-dot(p.ry,center),(camera?-far/den:B)-dot(p.rz,center),camera?1.0f:-dot(p.rw,center)};
  float3 base=center+float3{float(x)*0.2f,float(y)*0.2f,float(z)};float h=hh*0.025f;
  float3 q=hm_virtual_point(p,base,h);check(std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z));
  long double w=clip(p,base,3),D=clip(p,base,2)/w;
  long double expected=camera?D+(long double)h/((long double)near-far):
    (D*w+(long double)h*near/((long double)near-far))/(w+h);
  long double got=clip(p,q,2)/clip(p,q,3);
  // Independent native gl_FragDepth equation, not a second world reconstruction.
  check(std::abs(got-expected)<=gamma*(1+std::abs(expected)));
  for(unsigned axis=0;axis<2;++axis){
   const long double before=clip(p,base,axis)/w,after=clip(p,q,axis)/clip(p,q,3);
   check(std::abs(before-after)<=gamma*(1+std::abs(before)));
  }
  if(camera){check(q.z>=base.z);check(hm_camera_ray(p,{0,0,0},{0,0,1}));++orthographic;}
  else{check(length(q-center)>=length(base-center)-float(gamma)*length(base-center));
   auto d=(base-center)/length(base-center);check(hm_camera_ray(p,center,d));++perspective;}
 }
 // A physical thin ribbon stays on its original plane for shadow/GI/reflection.
 // Frozen main-camera virtual geometry would introduce the wrong off-axis hit.
 float3 p0{-1,-1,5},p1{1,-1,5},p2{0,1,5},o{0,0,6},back{0,0,-1};
 check(hm_plane_t(p0,p1,p2,o,back)==1.0f);
 auto bary=hm_barycentric(p0,p1,p2,{0,0,5});check(bary.x==0.25f && bary.y==0.5f);
 // Native origin search may begin AFTER the physical card. Generated primary
 // depth remains inside the ray window even though base_t is negative.
 HairProjection p{};p.rx={1,0,0};p.ry={0,1,0};p.rw={0,0,1};p.near_far={0.1f,1000};
 p.rz={0,0,0.1f/(0.1f-1000)};p.translation={0,0,-0.1f*1000/(0.1f-1000),0};
 float3 start{0,0,5.05f},forward{0,0,1},base{0,0,5};
 check(hm_plane_t(p0,p1,p2,start,forward)<0);
 auto q=hm_virtual_point(p,base,0.1f);check(q.z>start.z);
 check(hm_camera_ray(p,start,forward));check(!hm_camera_ray(p,{1,0,0},forward));
 check(hm_virtual_point(p,base,0).z==5);
 // The native depth formula also works with noncanonical depth rows. This is
 // why bounds use the captured general projection rather than assuming A*rw.
 p.rz.x=0.001f;auto skew=hm_virtual_point(p,{0.3f,0.1f,5},0.05f);
 long double w=clip(p,{0.3f,0.1f,5},3),D=clip(p,{0.3f,0.1f,5},2)/w;
 long double expected=(D*w+0.05f*(long double)p.near_far.x/(p.near_far.x-p.near_far.y))/(w+0.05f);
 check(std::abs(clip(p,skew,2)/clip(p,skew,3)-expected)<=gamma);
 // Actual shared intersection implementation, independent long-double planar
 // oracle. Include translations, mirror winding, near edges and the diagonal
 // shared by two triangles. Unresolved numeric edges may be UNKNOWN, not holes.
 unsigned triangle_cases=0;
 for(float scale:{0.0001f,1.0f,10000.0f})for(float shift:{-1000.0f,0.0f,1000.0f})
 for(int mirror=0;mirror<2;++mirror)for(int ix=-12;ix<=12;++ix)for(int iy=-12;iy<=12;++iy){
  float3 center{shift,-shift,shift},a=center+float3{-scale,-scale,5*scale},
         b=center+float3{scale,-scale,5*scale},c=center+float3{scale,scale,5*scale},d=center+float3{-scale,scale,5*scale};
  if(mirror){auto old=b;b=d;d=old;}
  float3 target=center+float3{scale*(ix/10.0f),scale*(iy/10.0f),5*scale};
  float3 origin{target.x,target.y,center.z+3*scale},direction{0,0,1};
  auto h1=hm_ray_triangle(a,b,c,origin,direction),h2=hm_ray_triangle(a,c,d,origin,direction);
  const bool in1=inside_xy(a,b,c,target),in2=inside_xy(a,c,d,target);
  check(!in1 || h1.status!=0);check(!in2 || h2.status!=0);
  check(in1 || h1.status!=1);check(in2 || h2.status!=1);
  if(in1||in2)check(h1.status!=0||h2.status!=0);
  for(auto h:{h1,h2})if(h.status==1){
   const long double t=(long double)a.z-origin.z;
   check(std::abs(h.t-t)<=h.t_error);check(h.bary.x>=0&&h.bary.y>=0&&h.bary.x+h.bary.y<=1);
  }
  ++triangle_cases;
 }
 // Inactive is exclusively the converted capture marker. A native payload
 // containing NaN is NOT enough to call an otherwise finite primitive inactive.
 check(hm_proven_inactive(0));check(hm_proven_inactive(-0.0f));
 check(!hm_proven_inactive(1));check(!hm_proven_inactive(std::numeric_limits<float>::quiet_NaN()));
 check(!hm_proven_inactive(std::numeric_limits<float>::infinity()));
 std::printf("%u checks; %u perspective and %u orthographic native-depth cases\n",checks,perspective,orthographic);
 std::printf("%u shared-edge/scale/translation/mirror triangle cases\n",triangle_cases);
}
