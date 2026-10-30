// Procedural native Hair bounds. Every operation on a range rounds outward by
// one representable float. Undefined/unbounded ranges invalidate the scene;
// a finite base-card hull is written for AS safety, never reported as complete.
[[vk::binding(0,0)]] ByteAddressBuffer Soup;
[[vk::binding(1,0)]] RWByteAddressBuffer Bounds;
[[vk::binding(2,0)]] RWByteAddressBuffer Status;
struct Push {uint tris,step,soup_base,bounds_base;float4 vp[4];float4 depth;uint stride,camera,bounded,pad;};
[[vk::push_constant]] Push pc;
#include "../../common/native_hair_math.h"
#include "hair_interval.hlsli"

[numthreads(128,1,1)]
void main(uint3 id:SV_DispatchThreadID){
 for(uint tri=id.x;tri<pc.tris;tri+=pc.step){
  float3 p[3],base[3];bool valid=pc.bounded!=0,inactive=false;
  [unroll]for(uint k=0;k<3;++k){uint a=pc.soup_base+(3u*tri+k)*pc.stride;
   float4 converted=asfloat(Soup.Load4(a));inactive=inactive||hm_proven_inactive(converted.w);
   p[k]=asfloat(Soup.Load3(a+60));base[k]=converted.xyz;
   valid=valid&&all(isfinite(p[k]));
   if(!all(isfinite(base[k])))base[k]=0;
  }
  // Existing xfb_to_world cull contract, NOT an unsupported material fallback.
  // Keep a finite degenerate AABB for refit identity; queries skip this marker
  // before inspecting the possibly NaN original native outputs.
  if(inactive){uint at=pc.bounds_base+tri*24u;Bounds.Store3(at,asuint(base[0]));Bounds.Store3(at+12,asuint(base[0]));continue;}
  float3 fallback_lo=min(base[0],min(base[1],base[2])),fallback_hi=max(base[0],max(base[1],base[2]));
  float3 round_scale=max(abs(p[0]),max(abs(p[1]),abs(p[2])));
  float2 box[3];[unroll]for(uint j=0;j<3;++j)box[j]=float2(min(p[0][j],min(p[1][j],p[2][j])),max(p[0][j],max(p[1][j],p[2][j])));
  // 128 scalar operations cover filtering-to-height and world reconstruction.
  const float gamma128=(128.0*0.00000011920928955078125)/(1.0-128.0*0.00000011920928955078125);
  float2 height=float2(0,up(up(pc.depth.x)*(1.0+gamma128)));
  if(pc.camera!=0 && height.y>0){
   float3 rx=float3(pc.vp[0].x,pc.vp[1].x,pc.vp[2].x),ry=float3(pc.vp[0].y,pc.vp[1].y,pc.vp[2].y),
          rz=float3(pc.vp[0].z,pc.vp[1].z,pc.vp[2].z),rw=float3(pc.vp[0].w,pc.vp[1].w,pc.vp[2].w);
   float2 near_far=isu(ip(pc.depth.y),ip(pc.depth.z));
   float2 xy[3];crossi(rx,ry,xy);
   if(pc.depth.w==1.0){
    float2 det=idoti(rz,xy),den=im(det,near_far);
    [unroll]for(uint j=0;j<3;++j)box[j]=ia(box[j],im(height,idi(xy[j],den)));
   } else {
    float2 yw[3],wx[3];crossi(ry,rw,yw);crossi(rw,rx,wx);
    float2 det=idoti(rw,xy),c[3];
    [unroll]for(uint j=0;j<3;++j)c[j]=idi(ia(ia(im(yw[j],ip(-pc.vp[3].x)),im(wx[j],ip(-pc.vp[3].y))),im(xy[j],ip(-pc.vp[3].w))),det);
    [unroll]for(uint j=0;j<3;++j)round_scale[j]=max(round_scale[j],max(abs(c[j].x),abs(c[j].y)));
    float2 B=ia(idoti(rz,c),ip(pc.vp[3].z)),A=idi(ip(pc.depth.y),near_far);
    float2 ratios[3];float2 aa=0,kk=0;
    [unroll]for(uint k=0;k<3;++k){
     float2 w=ia(idot(rw,p[k]),ip(pc.vp[3].w));valid=valid&&w.x>0;
     float2 relative[3];[unroll]for(uint j=0;j<3;++j){relative[j]=isu(ip(p[k][j]),c[j]);float2 r=idi(relative[j],w);ratios[j]=k==0?r:ih(ratios[j],r);}
     float2 a=idi(idoti(rz,relative),w),clipz=ia(idot(rz,p[k]),ip(pc.vp[3].z)),K=isu(clipz,im(A,w));
     aa=k==0?a:ih(aa,a);kk=k==0?K:ih(kk,K);
    }
    float2 denominator=ia(B,im(height,isu(A,aa)));
    // General captured projection: delta=(P-C)/w * h*K/[B+h*(A-a)].
    // Standard native perspective reduces exactly to (P-C)/w*h.
    float2 factor=idi(im(height,kk),denominator);
    [unroll]for(uint j=0;j<3;++j)box[j]=ia(box[j],im(ratios[j],factor));
   }
  }
  float3 lo,hi;[unroll]for(uint j=0;j<3;++j){valid=valid&&good(box[j]);lo[j]=box[j].x;hi[j]=box[j].y;}
  if(!valid){uint old;Status.InterlockedAdd(12,1u,old);lo=fallback_lo;hi=fallback_hi;}
  // Include the native Shadow base card regardless of camera depth output.
  lo=min(lo,fallback_lo);hi=max(hi,fallback_hi);
  [unroll]for(uint j=0;j<3;++j){float padding=gamma128*(round_scale[j]+max(abs(lo[j]),abs(hi[j])));
   if(!isfinite(padding))padding=0;
   lo[j]=down(lo[j]-padding);hi[j]=up(hi[j]+padding);}
  if(!all(isfinite(lo))||!all(isfinite(hi))){uint old;Status.InterlockedAdd(12,1u,old);lo=fallback_lo;hi=fallback_hi;}
  uint out_at=pc.bounds_base+tri*24u;Bounds.Store3(out_at,asuint(lo));Bounds.Store3(out_at+12,asuint(hi));
 }
}
