#include "../common/native_sun_math.h"
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace bg3::sun_math;
static unsigned checks;
static void check(bool x){++checks;if(!x){std::fprintf(stderr,"native sun check %u failed\n",checks);std::exit(1);}}
static double sat(double x){return std::max(0.,std::min(1.,x));}
static double oracle(float3 q,double s,double d,double w){
 return sat((1-std::abs(2*sat(q.x)-1))/s)*sat((1-std::abs(2*sat(q.y)-1))/s)*sat(sat(q.z)*d/(w*s/2));
}
int main(){
 float3 q[4]={{.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,.5f,.5f}};
 float4 d{1,1,1,1},w{1,1,1,1};
 auto select=[&](unsigned mask=15,bool raster=true,bool space=true,bool aux=false){return ns_select(q,.2f,d,w,mask,raster,space,aux);};
 auto out=select();check(out.valid && out.selected==0 && out.next==4 && out.selectedWeight==1);
 q[0].x=.01f;out=select();check(out.valid && out.selected==0 && out.next==1);check(std::abs(out.selectedWeight-.1f)<1e-6 && out.nextWeight==1-out.selectedWeight);
 check(!select(1).valid);q[1].x=0;out=select();check(out.valid && out.next==4 && out.whiteWeight==0); // No automatic white fill.
 q[0].x=0;q[2].x=0;q[3].x=.01f;out=select();check(out.valid && out.selected==3 && out.whiteWeight==1-out.selectedWeight);
 q[3].x=0;out=select(0);check(out.valid && out.selected==4 && out.whiteWeight==1);
 q[0].x=.5f;check(!select(0).valid);check(!select(15,false).valid);check(!select(15,true,false).valid);check(!select(15,true,true,true).valid);
 q[0].z=2;check(select().valid && select().selected==0); // Native selection does not reject z>1.
 q[0].z=0;check(select().selected==4);q[0].z=.5f;
 check(!ns_select(q,0,d,w,15,true,true,false).valid);
 check(ns_aux(1,100,.5f,50).valid && !ns_aux(1,100,.5f,50).active);check(ns_aux(1,100,.51f,50).active);
 check(!ns_aux(0,100,.9f,50).active);check(!ns_aux(1,INFINITY,0,0).valid);
 auto c=ns_cone_bounds({.5f,.5f,.2f},{0,0,1},{.1f,.1f,.05f},{0,0,0},{0,0,0});
 auto front=ns_cone_front(c,true);check(front.valid && front.maxDistance>.8f);check(!ns_cone_front(c,false).valid);
 auto ray=ns_ray_front({.5f,.5f,.2f},{0,0,1},true);check(ray.valid && ray.lower<=.8 && ray.upper>=.8);
 check(!ns_ray_front({.5f,.5f,.2f},{2,0,1},true).valid);
 check(!ns_ray_front({.5f,.5f,1},{0,0,1},true).valid);check(!ns_ray_front({.5f,.5f,.5f},{0,0,0},true).valid);
 c.directionLo.z=0;check(!ns_cone_front(c,true).valid);
 c=ns_cone_bounds({.98f,.5f,.2f},{0,0,1},{.1f,.1f,0},{0,0,0},{0,0,0});check(!ns_cone_front(c,true).valid);
 c=ns_cone_bounds({.5f,.5f,.2f},{0,0,1},{-.1f,0,0},{0,0,0},{0,0,0});check(!ns_cone_front(c,true).valid);
 // Auxiliary spatial branch and actual caster-W to receiver-P mapping.
 NativeSunAux enabled{1,1},disabled{1,0};
 check(!ns_aux_domain(disabled,{.5f,.5f,.5f},{.5f,.5f,.5f},0).active);
 check(!ns_aux_domain(enabled,{.5f,.5f,.5f},{.5f,.5f,.5f},1).active);
 check(ns_aux_domain(enabled,{.5f,.5f,.5f},{.5f,.5f,.5f},.5f).active);
 check(ns_aux_domain(enabled,{.5f,.5f,.5f},{2,.5f,.5f},1).active);
 check(!ns_aux_domain(enabled,{2,.5f,.5f},{2,.5f,.5f},0).active);
 check(!ns_aux_domain(enabled,{NAN,0,0},{0,0,0},0).valid);
 float4 native[3]={{.125f,0,0,.25f},{0,.25f,0,.5f},{0,0,.0625f,.125f}};
 float4 caster[3]={{0,-.125f,0,1.5f},{.25f,0,0,-.25f},{0,0,.0625f,.25f}};
 auto affine=ns_affine_from_q(native,caster);check(affine.valid && ns_metric_affine(affine));
 for(int sample=-16;sample<=16;++sample)for(unsigned axis=0;axis<3;++axis){
  double v[3]={sample*.25,sample*-.125,sample*.5},mapped[3]={};
  for(unsigned row=0;row<3;++row){mapped[row]=affine.rows[row].w;for(unsigned col=0;col<3;++col)mapped[row]+=double(affine.rows[row][col])*v[col];}
  double left=native[axis].w,right=caster[axis].w;
  for(unsigned col=0;col<3;++col){left+=double(native[axis][col])*mapped[col];right+=double(caster[axis][col])*v[col];}
  check(left==right); // Powers of two make this independent double oracle exact.
 }
 caster[0].y=-.25f;affine=ns_affine_from_q(native,caster);check(affine.valid && !ns_metric_affine(affine));
 caster[0].y=-.125f;caster[0].x=.0625f;check(!ns_metric_affine(ns_affine_from_q(native,caster)));
 native[0]={0,0,0,0};check(!ns_affine_from_q(native,caster).valid);
 native[0]={NAN,0,0,0};check(!ns_affine_from_q(native,caster).valid);
 std::mt19937 rng(0x687fa32);std::uniform_real_distribution<float> unit(0,1);
 for(unsigned i=0;i<4000;++i){
  for(unsigned j=0;j<4;++j)q[j]={unit(rng)*1.4f-.2f,unit(rng)*1.4f-.2f,unit(rng)*1.5f};
  out=select();check(out.valid);unsigned expected=4;double weights[4];
  for(unsigned j=0;j<4;++j){weights[j]=oracle(q[j],.2f,1,1);if(expected==4 && weights[j]>0)expected=j;}
  check(out.selected==expected);
  if(expected<4){check(std::abs(out.selectedWeight-weights[expected])<2e-6);unsigned next=expected<3 && weights[expected]<1 && weights[expected+1]>0?expected+1:4;check(out.next==next);}
  float3 origin{.2f+unit(rng)*.6f,.2f+unit(rng)*.6f,.05f+unit(rng)*.8f};
  float3 center{(unit(rng)-.5f)*.1f,(unit(rng)-.5f)*.1f,.5f+unit(rng)};
  float3 radius{unit(rng)*.1f,unit(rng)*.1f,unit(rng)*.1f};
  c=ns_cone_bounds(origin,center,radius,{0,0,0},{0,0,0});front=ns_cone_front(c,true);
  if(!front.valid)continue;
  for(unsigned sample=0;sample<64;++sample){
   // Independent double precision box-extrema directions; a superset of disk.
   double dx=center.x+(2.*unit(rng)-1)*radius.x,dy=center.y+(2.*unit(rng)-1)*radius.y,dz=center.z+(2.*unit(rng)-1)*radius.z;
   double t=(1.-origin.z)/dz,x=origin.x+t*dx,y=origin.y+t*dy;
   check(t<=front.maxDistance && x>=0 && x<=1 && y>=0 && y<=1);
  }
 }
 std::printf("native sun shared math: %u checks, CPU only\n",checks);
}
