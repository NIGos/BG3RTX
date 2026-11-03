// Reconstruct only the reflection delta; preserve each destination's native probe/direct light.
#include "gi_common.hlsli"
[[vk::binding(30,0)]] [[vk::image_format("r16f")]] RWTexture2D<float> rfHitDist;
// Same reused RGBA16F binding 8 as the half-grid reflection trace.
#define rfRaw giRaw
uint2 sample_pixel(uint2 q) { return min(q*2u+uint2(pc.frame&1u,(pc.frame>>1)&1u),pc.full-1u); }
[numthreads(8,8,1)]
void main(uint3 tid:SV_DispatchThreadID) {
  uint2 fp=tid.xy;
  if(any(fp>=pc.full)) return;
  rfHitDist[fp]=65504.0;
  float d=gDepth.Load(int3(fp,0));
  float4 mat=gMaterial.Load(int3(fp,0));
  uint model=uint(round(mat.w*255.0))&7u;
  if(d<=0.0 || ((pc.rf_models>>model)&1u)==0u || mat.x>pc.rf_max_rough) return;
  float2 px=float2(fp)+0.5, inv=1.0/float2(pc.full);
  float3 P=ray_at(px*inv)*d;
  float r=length(P), pa=pixel_angle(px*inv);
  float ndv=r>0.0?dot(depth_geo_normal(int2(fp),P),P)/r:0.0;
  float3 normal=decode_normal(gNormal.Load(int3(fp,0)).xy);
  float4 sum=0; float weights=0, strongest=0, hit=65504.0;
  int2 size=int2((pc.full+1u)/2u);
  for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
    int2 q=clamp(int2(fp/2u)+int2(x,y),int2(0,0),size-1);
    uint2 sp=sample_pixel(q);
    float hd=gDepth.Load(int3(sp,0));
    float4 sm=gMaterial.Load(int3(sp,0));
    if(hd<=0.0 || uint(round(sm.w*255.0))!=uint(round(mat.w*255.0))) continue;
    float2 qs=float2(sp)+0.5;
    float rq=hd*length(ray_at(qs*inv));
    float sigma=depth_tol(r,pa,ndv,length(qs-px),2.0);
    float w=exp(-abs(rq-r)/sigma)*max(dot(normal,decode_normal(gNormal.Load(int3(sp,0)).xy)),0.0);
    w*=x==0&&y==0?2.0:1.0;
    float4 v=rfRaw[q];
    if(all(isfinite(v))) { sum+=w*v; weights+=w; if(w>strongest) { strongest=w; hit=v.a; } }
  }
  if(weights>1e-4) {
    float4 v=sum/weights;
    float3 result=max(gSpecular[fp]+v.rgb,0.0);
    if(all(isfinite(result))) gSpecular[fp]=result;
    rfHitDist[fp]=hit;
  }
}
