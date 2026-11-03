#include "../common/water_world_lights.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>
using namespace bg3::transport;
void check(bool b){if(!b)std::abort();}
int main(){
 check(wl_reaches({0,0,0},{0,0,9},.1f,4,2));
 check(!wl_reaches({0,0,0},{0,0,10},.1f,4,2));
 check(!wl_reaches({0,0,0},{0,0,1},.1f,4,1));
 check(!wl_reaches({0,0,0},{0,0,1},0,4,2));
 check(!wl_reaches({0,0,0},{0,0,1},-1,4,2));
 float nan=std::numeric_limits<float>::quiet_NaN();
 check(!wl_reaches({nan,0,0},{0,0,1},.1f,4,2));
 check(!wl_reaches({0,0,0},{0,0,1},nan,4,2));
 check(!wl_reaches({0,0,0},{0,0,1},.1f,4,32));
 std::mt19937 rng(1016);
 for(unsigned n:{0u,1u,31u,32u,33u,4096u}){
  std::vector<std::pair<float,unsigned>> oracle;
  for(unsigned i=0;i<n;++i)oracle.push_back({float(rng()%100),i});
  auto input=oracle;std::shuffle(input.begin(),input.end(),rng);
  auto s=wl_empty();for(auto v:input)wl_insert(s,v.second,v.first);
  std::sort(oracle.begin(),oracle.end());
  check(s.count==std::min(n,WlK));check(s.candidates==n);
  for(unsigned i=0;i<s.count;++i){check(s.ids[i]==oracle[i].second);check(s.distances[i]==oracle[i].first);}
 }
 // Independent brute-force sphere oracle, including translated world points.
 for(unsigned trial=0;trial<100;++trial){
  HdVec3 p={float(int(rng()%100)-50),float(int(rng()%100)-50),float(int(rng()%100)-50)};
  auto selected=wl_empty();std::vector<std::pair<float,unsigned>> oracle;
  for(unsigned id=0;id<100;++id){
   HdVec3 c={p.x+float(int(rng()%40)-20),p.y,p.z};float radius=8;
   unsigned channels=rng()%8,channel=rng()%3;
   double dx=double(p.x)-c.x;
   // Use power-of-two radii to make strict boundary equality exact in both oracles.
   bool expected=(channels&(1u<<channel)) && dx*dx<64;
   bool reaches=wl_reaches(p,c,1/radius,channels,channel);check(reaches==expected);
   if(reaches){float d=(p.x-c.x)*(p.x-c.x);wl_insert(selected,id,d);oracle.push_back({d,id});}
  }
  std::sort(oracle.begin(),oracle.end());check(selected.candidates==oracle.size());
  for(unsigned j=0;j<selected.count;++j)check(selected.ids[j]==oracle[j].second);
 }
 auto s=wl_empty();wl_insert(s,0,nan);wl_insert(s,0,-1);check(s.count==0 && s.candidates==0);
 // Culling agrees with the incident reference for point/spot cones, boundaries,
 // channels and malformed records. Positive subnormal values must survive.
 HdLight l{};l.position={0,0,0};l.color={1,1,1};l.direction={0,0,1};
 l.inverse_radius=.125f;l.channels=1;l.cos_outer=.5f;l.cos_inner=.75f;
 l.global_selector=1;l.falloff_exponent=2;l.falloff_bias=0;
 for(bool spot:{false,true})for(unsigned channel:{0u,1u,32u})for(int x=-16;x<=16;++x){
  auto i=hd_native_incident(l,spot,{float(x),0,4},channel,1);
  auto fast=wl_incident(l,spot,{float(x),0,4},channel,1);
  check(fast.status==i.status);
  check(fast.radiance.x==i.radiance.x && fast.radiance.y==i.radiance.y && fast.radiance.z==i.radiance.z);
 }
 // Independent cone oracle: light axis +Z, hit at +Z is inside; -Z outside.
 check(wl_known_zero(wl_incident(l,true,{0,0,-4},0,1)));
 check(!wl_known_zero(wl_incident(l,true,{0,0,4},0,1)));
 for(unsigned trial=0;trial<10000;++trial){
  auto test=l;test.color={float(rng()%100),float(rng()%100),float(rng()%100)};
  test.falloff_exponent=float(rng()%8);test.falloff_bias=float(int(rng()%5)-2);
  test.cos_outer=float(int(rng()%200)-100)/100;test.cos_inner=test.cos_outer+(1-test.cos_outer)*.5f;
  test.global_selector=trial%2?-1.f:1.f;
  HdVec3 point={float(int(rng()%32)-16),float(int(rng()%32)-16),float(int(rng()%32)-16)};
  if(trial%13==0)test.direction={0,0,0};
  if(trial%17==0)test.cos_inner=nan;
  if(trial%19==0)test.color.x=-1;
  auto ref=hd_native_incident(test,trial%2,point,trial%3,2);
  auto fast=wl_incident(test,trial%2,point,trial%3,2);
  check(ref.status==fast.status);
  if(ref.status==HdOk)check(ref.radiance.x==fast.radiance.x && ref.radiance.y==fast.radiance.y && ref.radiance.z==fast.radiance.z);
 }
 HdIncident incident{};incident.status=HdInvalid;check(!wl_known_zero(incident));
 incident.status=HdOk;check(wl_known_zero(incident));
 incident.radiance.x=std::numeric_limits<float>::denorm_min();check(!wl_known_zero(incident));
 for(unsigned n:{0u,1u,31u,32u,33u,4096u}){
  auto a=wl_empty();for(unsigned j=0;j<n;++j)wl_append(a,j,float(j));
  check(a.count==std::min(n,WlK) && a.candidates==n);
  for(unsigned j=0;j<a.count;++j)check(a.ids[j]==j);
 }
 std::puts("world culling and nearest selection passed");
}
