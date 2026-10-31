#include "../common/native_hit_direct.h"
#include <cstdio>
#include <initializer_list>
#include <cstdlib>
#include <limits>
using namespace bg3::transport;
static unsigned checks;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"direct check %u failed\n",checks);std::exit(1);}}
static bool near(float a,double b){return std::fabs(double(a)-b)<=256*std::numeric_limits<float>::epsilon()*std::fmax(1,std::fabs(b));}
int main(){
 HdLight l{};l.position={0,0,2};l.color={2,3,4};l.direction={0,0,-1};l.inverse_radius=.1f;l.cos_outer=.5f;l.cos_inner=.9f;l.falloff_exponent=2;l.channels=4;
 unsigned nonzero=0;
 for(bool spot:{false,true})for(float bias:{-.25f,0.f,.25f})for(float exponent:{0.f,1.f,2.f,5.f})for(float distance:{.2f,1.f,3.f,7.f,9.99f,10.f,12.f}){
  l.position.z=distance;l.falloff_bias=bias;l.falloff_exponent=exponent;l.global_selector=-1;
  auto r=hd_native_incident(l,spot,{0,0,0},2,1.75f);check(r.status==HdOk);
  double ir=double(l.inverse_radius)*l.inverse_radius,d2=double(distance)*distance;
  double expected=0;
  if(d2<1/ir){
   double window=std::fmax(0,1-(d2*ir)*(d2*ir));
   double a=std::pow(std::fmax(window,double(9.9999997e-05f)),double(exponent));
   a*=bias<0 ? 4*ir+std::fabs(bias) : 1/std::fmax(d2,double(9.9999997e-05f))+std::fabs(bias);
   expected=a*(spot?double(0.31830987f):double(0.07957747f))*1.75;
  }
  check(near(r.radiance.x,expected*2));check(near(r.radiance.y,expected*3));check(near(r.radiance.z,expected*4));
  check(near(r.distance,distance));if(expected>0)++nonzero;
 }
 check(nonzero>0);
 l.position={0,0,2};l.falloff_exponent=2;l.falloff_bias=0;l.global_selector=0;
 for(float cosine:{.2f,.5f,.6f,.8f,.9f,1.f}){
  // Construct actual direction at an independently known cone angle.
  l.direction={std::sqrt(1-cosine*cosine),0,-cosine};
  auto r=hd_native_incident(l,true,{0,0,0},2,1);
  auto p=hd_native_incident(l,false,{0,0,0},2,1);
  double t=std::fmin(1.,std::fmax(0.,(double(cosine)-l.cos_outer)/(l.cos_inner-l.cos_outer)));
  check(r.status==HdOk);check(near(r.radiance.x,double(p.radiance.x)*(double(0.31830987f)/0.07957747f)*t*t));
 }
 // Native bg3_lights explicitly normalizes the authored spot axis.
 l.direction={0,0,-1};auto unit_axis=hd_native_incident(l,true,{0,0,0},2,1);
 l.direction={0,0,-3};auto scaled_axis=hd_native_incident(l,true,{0,0,0},2,1);
 check(scaled_axis.status==HdOk);check(near(scaled_axis.radiance.x,unit_axis.radiance.x));
 check(hd_native_incident(l,false,{0,0,0},0,1).radiance.x==0);
 check(hd_native_incident(l,false,{0,0,0},32,1).status==HdInvalid);
 l.position={0,0,0};check(hd_native_incident(l,false,{0,0,0},2,1).status==HdInvalid);l.position.z=2;
 l.falloff_exponent=std::numeric_limits<float>::quiet_NaN();check(hd_native_incident(l,false,{0,0,0},2,1).status==HdInvalid);l.falloff_exponent=2;
 auto incident=hd_native_incident(l,false,{0,0,0},2,1);
 NtInput in{{.3f,.6f,.9f},.4f,.5f,0,0,NtSamplesKnown|NtLinearColorKnown|NtOpaqueCoverageKnown|NtLobesKnown};
 auto material=nt_to_fresnel_blend(nt_class0_parameters(in));
 for(float angle:{0.f,.2f,.6f}){
  TbVec3 n{std::sin(angle),0,std::cos(angle)},o{0,0,1};
  auto result=hd_scatter(material,n,n,o,incident,true,.7f);check(result.status==HdOk);
  // Independent axis-angle inverse rotation, no shared frame helper.
  TbVec3 local{float(-std::sin(angle)),0,float(std::cos(angle))};
  auto e=tb_fresnel_evaluate(material.value,local,local);
  check(near(result.radiance.x,double(e.f.x)*incident.radiance.x*std::cos(angle)*.7));
  check(near(result.radiance.y,double(e.f.y)*incident.radiance.y*std::cos(angle)*.7));
  check(near(result.radiance.z,double(e.f.z)*incident.radiance.z*std::cos(angle)*.7));
 }
 check(hd_scatter(material,{0,0,1},{0,0,1},{0,0,1},incident,false,1).status==HdUnknownVisibility);
 check(hd_scatter(material,{0,0,1},{0,0,1},{0,0,1},incident,true,1.1f).status==HdInvalid);
 check(hd_scatter(material,{0,0,-1},{0,0,1},{0,0,1},incident,true,1).status==HdNormalDomain);
 check(hd_scatter(material,{0,0,1},{0,0,-1},{0,0,1},incident,true,1).status==HdNormalDomain);
 auto blocked=hd_scatter(material,{0,0,1},{0,0,1},{0,0,1},incident,true,0);check(blocked.status==HdOk && blocked.radiance.x==0);
 auto missing=material;missing.status=NtUnknownSamples;check(hd_scatter(missing,{0,0,1},{0,0,1},{0,0,1},incident,true,1).status==HdUnknownMaterial);
 std::printf("PASS %u native authored incident/FresnelBlend direct checks; no GPU\n",checks);
}
