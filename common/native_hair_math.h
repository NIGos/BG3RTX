#pragma once
// Pure geometry math compiled by BOTH the CPU regression and DXC. Texture
// sampling remains in the shader. The oracle checks projected native depth,
// independently of these world-space construction formulas.
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace bg3::hair_math {
struct float2 {float x,y;};
struct float3 {float x,y,z;float operator[](unsigned i)const{return i==0?x:i==1?y:z;}};
struct float4 {float x,y,z,w;};
inline float3 operator+(float3 a,float3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline float3 operator-(float3 a,float3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline float3 operator*(float3 a,float b){return {a.x*b,a.y*b,a.z*b};}
inline float3 operator/(float3 a,float b){return a*(1.0f/b);}
inline float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float3 cross(float3 a,float3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(float3 a){return std::sqrt(dot(a,a));}
inline float abs(float x){return std::abs(x);}
#define HM_INLINE inline
#define HM_PRECISE volatile
#define HM_UINT uint32_t
#define HM3(x,y,z) float3{x,y,z}
#define HM2(x,y) float2{x,y}
#else
#define HM_INLINE
#define HM_PRECISE precise
#define HM_UINT uint
#define HM3(x,y,z) float3(x,y,z)
#define HM2(x,y) float2(x,y)
#endif
struct HairProjection {float3 rx,ry,rz,rw;float4 translation;float2 near_far;bool ortho;};
HM_INLINE bool hm_proven_inactive(float converted_validity){return converted_validity==0.0f;}
HM_INLINE float3 hm_solve3(float3 x,float3 y,float3 z,float3 rhs){
 float3 a=cross(y,z),b=cross(z,x),c=cross(x,y);return (a*rhs.x+b*rhs.y+c*rhs.z)/dot(x,a);
}
HM_INLINE float3 hm_camera_center(HairProjection p){
 return hm_solve3(p.rx,p.ry,p.rw,HM3(-p.translation.x,-p.translation.y,-p.translation.w));
}
HM_INLINE float3 hm_virtual_point(HairProjection p,float3 base,float height){
 if(height==0)return base;
 if(p.ortho)return base+cross(p.rx,p.ry)*(height/((p.near_far.x-p.near_far.y)*dot(p.rz,cross(p.rx,p.ry))));
 float3 c=hm_camera_center(p);float w=dot(p.rw,base)+p.translation.w;
 float z=dot(p.rz,base)+p.translation.z;
 float B=dot(p.rz,c)+p.translation.z,A=p.near_far.x/(p.near_far.x-p.near_far.y),a=(z-B)/w;
 float scale=B*(w+height)/(w*(B+height*(A-a)));return c+(base-c)*scale;
}
HM_INLINE float2 hm_barycentric(float3 p0,float3 p1,float3 p2,float3 position){
 float3 n=cross(p1-p0,p2-p0),v=position-p0;float nn=dot(n,n);
 return HM2(dot(cross(v,p2-p0),n)/nn,dot(cross(p1-p0,v),n)/nn);
}
HM_INLINE float hm_plane_t(float3 p0,float3 p1,float3 p2,float3 o,float3 d){
 float3 n=cross(p1-p0,p2-p0);return dot(n,p0-o)/dot(n,d);
}
HM_INLINE bool hm_camera_ray(HairProjection p,float3 o,float3 d){
 const float gamma128=(128.0f*0.00000011920928955078125f)/(1.0f-128.0f*0.00000011920928955078125f);
 if(p.ortho){
  float3 ax=HM3(abs(p.rx.x),abs(p.rx.y),abs(p.rx.z)),ay=HM3(abs(p.ry.x),abs(p.ry.y),abs(p.ry.z));
  float3 ad=HM3(abs(d.x),abs(d.y),abs(d.z));
  return abs(dot(p.rx,d))+abs(dot(p.ry,d))<=gamma128*(dot(ax,ad)+dot(ay,ad));
 }
 float3 c=hm_camera_center(p);
 return length(cross(o-c,d))<=gamma128*(length(o)+length(c))*length(d);
}
struct HairTriangleHit {float t;float2 bary;float t_error;HM_UINT status;}; // 0 miss,1 hit,2 unknown.
HM_INLINE float2 hm_edge(float ax,float ay,float bx,float by,float2 ae,float2 be){
 HM_PRECISE float p=ax*by;HM_PRECISE float q=ay*bx;HM_PRECISE float edge=p-q;
 const float e=0.00000011920928955078125f,g4=4*e/(1-4*e),g16=16*e/(1-16*e);
 float error=(abs(ax)*be.y+abs(by)*ae.x+ae.x*be.y+abs(ay)*be.x+abs(bx)*ae.y+ae.y*be.x+g4*(abs(p)+abs(q)))*(1+g16);
 return HM2(edge,error);
}
// Dominant-axis shear keeps shared-edge arithmetic identical in adjacent cards.
// A derived FP32 error band returns UNKNOWN at unresolved edges, never MISS.
HM_INLINE HairTriangleHit hm_ray_triangle(float3 p0,float3 p1,float3 p2,float3 o,float3 d){
 HairTriangleHit h;h.t=-1;h.bary=HM2(0,0);h.t_error=0;h.status=2;
 HM_UINT kz=abs(d.x)>abs(d.y)?0:1;kz=abs(d[kz])>abs(d.z)?kz:2;
 if(d[kz]==0)return h;
 HM_UINT kx=(kz+1)%3,ky=(kx+1)%3;if(d[kz]<0){HM_UINT swap=kx;kx=ky;ky=swap;}
 float sx=d[kx]/d[kz],sy=d[ky]/d[kz],sz=1.0f/d[kz];
 float3 vertices[3];vertices[0]=p0;vertices[1]=p1;vertices[2]=p2;
 float3 v[3];float2 err[3];
 const float e=0.00000011920928955078125f,g4=4*e/(1-4*e),g16=16*e/(1-16*e);
 for(HM_UINT i=0;i<3;++i){
  HM_PRECISE float ax=vertices[i][kx]-o[kx],ay=vertices[i][ky]-o[ky],az=vertices[i][kz]-o[kz];
  HM_PRECISE float px=sx*az,py=sy*az;
  HM_PRECISE float xx=ax-px,yy=ay-py,zz=az*sz;
  v[i]=HM3(xx,yy,zz);err[i]=HM2(g4*(abs(ax)+abs(px)),g4*(abs(ay)+abs(py)));
 }
 float2 a=hm_edge(v[2].x,v[2].y,v[1].x,v[1].y,err[2],err[1]);
 float2 b=hm_edge(v[0].x,v[0].y,v[2].x,v[2].y,err[0],err[2]);
 float2 c=hm_edge(v[1].x,v[1].y,v[0].x,v[0].y,err[1],err[0]);
 HM_PRECISE float det=(a.x+b.x)+c.x;
 float de=(a.y+b.y+c.y+g4*(abs(a.x)+abs(b.x)+abs(c.x)))*(1+g16);
 if(abs(det)<=de)return h;
 float orientation=det>0?1.0f:-1.0f;
 if(a.x*orientation < -a.y || b.x*orientation < -b.y || c.x*orientation < -c.y){h.status=0;return h;}
 if(abs(a.x)<=a.y || abs(b.x)<=b.y || abs(c.x)<=c.y)return h;
 HM_PRECISE float na=a.x*v[0].z,nb=b.x*v[1].z,nc=c.x*v[2].z;
 HM_PRECISE float numerator=(na+nb)+nc;
 float ne=(a.y*abs(v[0].z)+b.y*abs(v[1].z)+c.y*abs(v[2].z)+
           g4*(abs(na)+abs(nb)+abs(nc)))*(1+g16);
 h.t=numerator/det;h.bary=HM2(b.x/det,c.x/det);
 h.t_error=((ne+abs(h.t)*de)/(abs(det)-de)+g4*abs(h.t))*(1+g16);
 h.status=1;return h;
}
#undef HM_INLINE
#undef HM_PRECISE
#undef HM_UINT
#undef HM3
#undef HM2
#ifdef __cplusplus
} // namespace bg3::hair_math
#endif
