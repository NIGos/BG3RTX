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
 std::puts("world culling and nearest selection passed");
}
