#include "../common/native_hair_receiver.h"
#include "../common/native_hair_math.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3::hair_receiver;
using namespace bg3::hair_math;
int main(){
 unsigned checks=0;auto check=[&](bool v){++checks;if(!v)std::abort();};
 check(hpr_camera_scalp(0x9f377ec5,0x9e77575a));check(hpr_camera_scalp(0xa83ea027,0x36eeb96b));
 check(!hpr_camera_scalp(0x45625c44,0x03a2d9f6));check(!hpr_camera_scalp(0,0));
 check(hpr_window(2.9f,3.1f));check(!hpr_window(3,3));check(!hpr_window(-1,3));
 check(!hpr_window(0,std::numeric_limits<float>::infinity()));
 for(unsigned kind=1;kind<=5;++kind)check(hpr_readable_geometry(true,24,true,kind));
 check(!hpr_readable_geometry(false,88,true,3));
 check(!hpr_readable_geometry(true,23,true,3));
 check(!hpr_readable_geometry(true,88,false,3));
 check(!hpr_readable_geometry(true,88,true,0));
 check(!hpr_readable_geometry(true,88,true,6));
 check(!hpr_reject(true,false,.1f,.2f,false));check(hpr_reject(true,true,.1f,.2f,false));
 check(hpr_reject(false,false,.1f,.2f,false));check(hpr_reject(true,false,.1f,.2f,true));
 check(hpr_reject(true,false,std::numeric_limits<float>::quiet_NaN(),.2f,false));
 check(hpr_reject(true,false,-1,.2f,false));check(hpr_reject(true,false,.3f,.2f,false));
 // Independent finite camera geometry. Bounds are supplied by the existing
 // depth_tol caller; this helper cannot enlarge or tune them.
 const float start=2.9f,end=3.1f;
 for(float z:{2.0f,3.0f,4.0f}){
  auto hit=hm_ray_triangle({-1,-1,z},{1,-1,z},{0,1,z},{0,0,start},{0,0,1});
  const bool traversed=hit.status==1u && hit.t>=0 && hit.t<=end-start;
  check(traversed==(z==3.0f));
  if(traversed)check(hpr_reject(true,true,hit.t,end-start,false));
 }
 check(hpr_reject(true,true,0,.2f,false));check(hpr_reject(true,true,.2f,.2f,false));
 std::printf("Hair receiver ambiguity: %u CPU checks; no occluder/alpha/tolerance changes\n",checks);
}
