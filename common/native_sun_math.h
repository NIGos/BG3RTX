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
#define NS4(x,y,z,w) float4{x,y,z,w}
inline float ns_up(float a){return std::nextafter(a,INFINITY);}
inline float ns_down(float a){return std::nextafter(a,-INFINITY);}
#else
#define NS_INLINE
#define NS_UINT uint
#define NS3(x,y,z) float3(x,y,z)
#define NS4(x,y,z,w) float4(x,y,z,w)
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
// The threshold only enters the auxiliary branch. A full CSM3 suppresses q5
// auxiliary sampling; q4 is tried only when q5 has no spatial support.
NS_INLINE NativeSunAux ns_aux_domain(NativeSunAux threshold,float3 q4,float3 q5,float csm3Weight) {
 NativeSunAux result=threshold;if(!result.valid || !result.active)return result;
 if(!isfinite(q4.x)||!isfinite(q4.y)||!isfinite(q4.z)||!isfinite(q5.x)||!isfinite(q5.y)||!isfinite(q5.z)||!isfinite(csm3Weight)) {result.valid=0;return result;}
 bool inside5=q5.x>0 && q5.x<1 && q5.y>0 && q5.y<1 && q5.z>=0;
 result.active=inside5?(csm3Weight<1?1u:0u):(q4.x>0 && q4.x<1 && q4.y>0 && q4.y<1 && q4.z>=0?1u:0u);
 return result;
}
NS_INLINE float ns_dot3(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
NS_INLINE float3 ns_cross3(float3 a,float3 b){return NS3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
struct NativeSunAffine {NS_UINT valid;float error;float4 rows[3];};
// q=An*P+bn=Ac*W+bc. Normalize equations by their largest native coefficient
// before inversion, avoiding false singularity from anisotropic cascade scale.
NS_INLINE NativeSunAffine ns_affine_from_q(float4 nativeRows[3],float4 casterRows[3]) {
 NativeSunAffine result;result.valid=0;result.error=0;
 float4 n[3],c[3];
 for(NS_UINT i=0;i<3;++i){
  float scale=max(abs(nativeRows[i].x),max(abs(nativeRows[i].y),abs(nativeRows[i].z)));
  if(!isfinite(scale)||scale<=0)return result;
  for(NS_UINT k=0;k<4;++k)if(!isfinite(nativeRows[i][k])||!isfinite(casterRows[i][k]))return result;
  n[i]=NS4(nativeRows[i].x/scale,nativeRows[i].y/scale,nativeRows[i].z/scale,nativeRows[i].w/scale);
  c[i]=NS4(casterRows[i].x/scale,casterRows[i].y/scale,casterRows[i].z/scale,casterRows[i].w/scale);
 }
 float3 nv[3]={NS3(n[0].x,n[0].y,n[0].z),NS3(n[1].x,n[1].y,n[1].z),NS3(n[2].x,n[2].y,n[2].z)};
 // A directional orthographic native projection must have orthogonal metric
 // axes. gamma128 bounds normalized-input encoding and dot-product rounding.
 for(NS_UINT i=0;i<3;++i)for(NS_UINT j=i+1;j<3;++j){
  float magnitude=abs(nv[i].x*nv[j].x)+abs(nv[i].y*nv[j].y)+abs(nv[i].z*nv[j].z);
  if(abs(ns_dot3(nv[i],nv[j]))>7.62945274e-6f*magnitude)return result;
 }
 float3 co[3]={ns_cross3(nv[1],nv[2]),ns_cross3(nv[2],nv[0]),ns_cross3(nv[0],nv[1])};
 float determinant=ns_dot3(nv[0],co[0]);
 float magnitude=abs(nv[0].x)*(abs(nv[1].y*nv[2].z)+abs(nv[1].z*nv[2].y))+
  abs(nv[0].y)*(abs(nv[1].z*nv[2].x)+abs(nv[1].x*nv[2].z))+
  abs(nv[0].z)*(abs(nv[1].x*nv[2].y)+abs(nv[1].y*nv[2].x));
 if(!isfinite(determinant)||abs(determinant)<=7.62945274e-6f*magnitude)return result;
 float maximum=1,condition=0;
 for(NS_UINT row=0;row<3;++row){
  float x=(co[0][row]*c[0].x+co[1][row]*c[1].x+co[2][row]*c[2].x)/determinant;
  float y=(co[0][row]*c[0].y+co[1][row]*c[1].y+co[2][row]*c[2].y)/determinant;
  float z=(co[0][row]*c[0].z+co[1][row]*c[1].z+co[2][row]*c[2].z)/determinant;
  float w=(co[0][row]*(c[0].w-n[0].w)+co[1][row]*(c[1].w-n[1].w)+co[2][row]*(c[2].w-n[2].w))/determinant;
  if(!isfinite(x)||!isfinite(y)||!isfinite(z)||!isfinite(w))return result;
  result.rows[row]=NS4(x,y,z,w);maximum=max(maximum,max(abs(x),max(abs(y),abs(z))));
  condition=max(condition,3*(abs(co[0][row])+abs(co[1][row])+abs(co[2][row]))/abs(determinant));
 }
 // gamma512 covers <128 scalar operations, weighted conservatively for
 // normalization/division (up to8 rounding units each), plus metric products.
 result.error=3.05185094e-5f*max(condition,1.0f)*maximum;
 result.valid=isfinite(result.error)&&result.error<1?1u:0u;return result;
}
NS_INLINE bool ns_metric_affine(NativeSunAffine a){
 if(!a.valid)return false;
 for(NS_UINT i=0;i<3;++i)for(NS_UINT j=i;j<3;++j){
  float dot=a.rows[i].x*a.rows[j].x+a.rows[i].y*a.rows[j].y+a.rows[i].z*a.rows[j].z;
  float mag=abs(a.rows[i].x*a.rows[j].x)+abs(a.rows[i].y*a.rows[j].y)+abs(a.rows[i].z*a.rows[j].z);
  float sums=abs(a.rows[i].x)+abs(a.rows[i].y)+abs(a.rows[i].z)+abs(a.rows[j].x)+abs(a.rows[j].y)+abs(a.rows[j].z);
  float bound=7.62945274e-6f*mag+a.error*(sums+3*a.error);
  if(abs(dot-(i==j?1.0f:0.0f))>bound)return false;
 }
 return true;
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
#undef NS4
