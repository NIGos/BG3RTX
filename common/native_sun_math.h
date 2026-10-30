#pragma once
// Shared CPU/HLSL receiver math from native FS0687fa327aed7f8e.
// Inputs are in the audited native affine q space, not inferred world space.
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <bit>
namespace bg3::sun_math {
struct float3 {float x,y,z;float operator[](unsigned i)const{return i==0?x:i==1?y:z;}};
struct float4 {float x,y,z,w;float operator[](unsigned i)const{return i==0?x:i==1?y:i==2?z:w;}};
inline float min(float a,float b){return std::min(a,b);}
inline float max(float a,float b){return std::max(a,b);}
inline float abs(float a){return std::abs(a);}
inline bool isfinite(float a){return std::isfinite(a);}
inline float saturate(float a){return std::clamp(a,0.f,1.f);}
#define NS_INLINE inline
#define NS_UINT uint32_t
#define NS3(x,y,z) float3{x,y,z}
inline float ns_up(float a){return std::nextafter(a,INFINITY);}
inline float ns_down(float a){return std::nextafter(a,-INFINITY);}
#else
#define NS_INLINE
#define NS_UINT uint
#define NS3(x,y,z) float3(x,y,z)
float ns_up(float a){if(a==0)return asfloat(1u);uint b=asuint(a);return asfloat(a>0?b+1u:b-1u);}
float ns_down(float a){if(a==0)return -asfloat(1u);uint b=asuint(a);return asfloat(a>0?b-1u:b+1u);}
#endif
struct NativeSunSelection {
 NS_UINT valid, selected, next;
 float selectedWeight,nextWeight,whiteWeight;
};
NS_INLINE float ns_weight(float3 q,float feather,float depth,float width){
 float hx=saturate((1-abs(2*saturate(q.x)-1))/feather);
 float hy=saturate((1-abs(2*saturate(q.y)-1))/feather);
 return (hx*hy)*saturate((saturate(q.z)*depth)/(width*(.5f*feather)));
}
NS_INLINE NativeSunSelection ns_select(float3 q[4],float feather,float4 depth,float4 width,
 NS_UINT certifiedDomains,bool nativeRasterValid,bool receiverSpaceValid,bool auxEnabled){
 NativeSunSelection result;result.valid=0;result.selected=4;result.next=4;
 result.selectedWeight=0;result.nextWeight=0;result.whiteWeight=0;
 if(!nativeRasterValid || !receiverSpaceValid || auxEnabled || !isfinite(feather) || feather<=0)return result;
 float weights[4];
 for(NS_UINT i=0;i<4;++i){
  if(!isfinite(depth[i]) || !isfinite(width[i]) || depth[i]<=0 || width[i]<=0 ||
   !isfinite(q[i].x) || !isfinite(q[i].y) || !isfinite(q[i].z))return result;
  weights[i]=ns_weight(q[i],feather,depth[i],width[i]);
  if(!isfinite(weights[i]))return result;
 }
 for(NS_UINT j=0;j<4;++j)if(weights[j]>0){result.selected=j;break;}
 if(result.selected==4){result.valid=1;result.whiteWeight=1;return result;}
 NS_UINT s=result.selected;
 if((certifiedDomains&(1u<<s))==0)return result;
 result.selectedWeight=weights[s];
 if(s==3)result.whiteWeight=1-weights[s];
 else if(weights[s]<1 && weights[s+1]>0){
  if((certifiedDomains&(1u<<(s+1)))==0)return result;
  result.next=s+1;result.nextWeight=1-weights[s];
 }
 result.valid=1;return result;
}
// Interval encloses ALL projected cone directions (including transform error).
// Caller derives it from actual finite light-disc basis and validated matrices.
// Per-layer native caster membership and raster/depth clipping MUST be certified.
struct NativeSunCone {float3 originLo,originHi,directionLo,directionHi;};
NS_INLINE NativeSunCone ns_cone_bounds(float3 origin,float3 center,float3 diskHalfwidth,
 float3 originError,float3 directionError){
 NativeSunCone c;
 c.originLo=NS3(ns_down(origin.x-originError.x),ns_down(origin.y-originError.y),ns_down(origin.z-originError.z));
 c.originHi=NS3(ns_up(origin.x+originError.x),ns_up(origin.y+originError.y),ns_up(origin.z+originError.z));
 float3 radius=NS3(ns_up(diskHalfwidth.x+directionError.x),ns_up(diskHalfwidth.y+directionError.y),ns_up(diskHalfwidth.z+directionError.z));
 c.directionLo=NS3(ns_down(center.x-radius.x),ns_down(center.y-radius.y),ns_down(center.z-radius.z));
 c.directionHi=NS3(ns_up(center.x+radius.x),ns_up(center.y+radius.y),ns_up(center.z+radius.z));
 // Invalid negative error/halfwidth cannot produce a certificate.
 if(diskHalfwidth.x<0 || diskHalfwidth.y<0 || diskHalfwidth.z<0 ||
  originError.x<0 || originError.y<0 || originError.z<0 ||
  directionError.x<0 || directionError.y<0 || directionError.z<0)c.originLo.x=2;
 return c;
}
struct NativeSunAux {NS_UINT valid,active;};
NS_INLINE NativeSunAux ns_aux(NS_UINT enabled,float cameraFar,float linearDepth,float threshold){
 NativeSunAux result;result.valid=0;result.active=0;
 if(!isfinite(cameraFar) || !isfinite(linearDepth) || !isfinite(threshold))return result;
 float depth=cameraFar*linearDepth;if(!isfinite(depth))return result;
 result.valid=1;result.active=enabled!=0 && depth>threshold?1u:0u;return result;
}
struct NativeSunRayFront {NS_UINT valid;float lower,upper;};
NS_INLINE NativeSunRayFront ns_ray_front(float3 origin,float3 direction,bool domainCertified){
 NativeSunRayFront result;result.valid=0;result.lower=0;result.upper=0;
 if(!domainCertified || !isfinite(origin.x) || !isfinite(origin.y) || !isfinite(origin.z) ||
  !isfinite(direction.x) || !isfinite(direction.y) || !isfinite(direction.z) || direction.z<=0 ||
  origin.x<=0 || origin.x>=1 || origin.y<=0 || origin.y>=1 || origin.z<0 || origin.z>=1)return result;
 result.lower=ns_down(ns_down(1-origin.z)/direction.z);
 result.upper=ns_up(ns_up(1-origin.z)/direction.z);
 if(!isfinite(result.lower) || !isfinite(result.upper) || result.lower<=0)return result;
 for(NS_UINT axis=0;axis<2;++axis){
  float lo=min(origin[axis],ns_down(origin[axis]+ns_down(result.upper*direction[axis])));
  float hi=max(origin[axis],ns_up(origin[axis]+ns_up(result.upper*direction[axis])));
  if(lo<=0 || hi>=1)return result;
 }
 // No exact endpoint is promised by a rounded quotient. Caller enforces native
 // hit-domain clipping or declines an ambiguous boundary hit.
 result.valid=1;return result;
}
struct NativeSunFront {NS_UINT valid;float maxDistance;};
NS_INLINE NativeSunFront ns_cone_front(NativeSunCone cone,bool domainCertified){
 NativeSunFront result;result.valid=0;result.maxDistance=0;
 if(!domainCertified)return result;
 for(NS_UINT axis=0;axis<3;++axis){
  if(!isfinite(cone.originLo[axis]) || !isfinite(cone.originHi[axis]) ||
   !isfinite(cone.directionLo[axis]) || !isfinite(cone.directionHi[axis]) ||
   cone.originLo[axis]>cone.originHi[axis] || cone.directionLo[axis]>cone.directionHi[axis] ||
   cone.originLo[axis]<0 || cone.originHi[axis]>1)return result;
 }
 if(cone.directionLo.z<=0 || cone.originHi.z>=1)return result;
 // Outward rounded finite front, covering the slowest positive z direction.
 float t=ns_up(ns_up(1-cone.originLo.z)/cone.directionLo.z);
 if(!isfinite(t) || t<=0)return result;
 for(NS_UINT a=0;a<2;++a){
  float low=min(cone.originLo[a],ns_down(cone.originLo[a]+ns_down(t*cone.directionLo[a])));
  float high=max(cone.originHi[a],ns_up(cone.originHi[a]+ns_up(t*cone.directionHi[a])));
  if(low<=0 || high>=1)return result;
 }
 result.valid=1;result.maxDistance=t;return result;
}
#ifdef __cplusplus
}
#endif
#undef NS_INLINE
#undef NS_UINT
#undef NS3
