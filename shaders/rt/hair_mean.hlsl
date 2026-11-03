// Per-draw empirical physical hard coverage. Runs after this scene's alpha set
// is committed. GI queries read the one stored probability; no texture fetches.
// This is an isotropic/material-domain approximation, NOT a guarantee of matching
// a particular camera's GI energy. It deliberately retains nonzero hair occlusion.
[[vk::binding(0,1)]] ByteAddressBuffer gAlphaRec;
[[vk::binding(2,1)]] Texture2D<float4> gAlphaTex[4096];
[[vk::binding(4,1)]] SamplerState gAlphaWrap;
[[vk::binding(6,1)]] ByteAddressBuffer Soup;
[[vk::binding(7,1)]] ByteAddressBuffer Info;
[[vk::binding(8,1)]] RWByteAddressBuffer Pool;
struct Push {uint instances,pad0,pad1,pad2;};
[[vk::push_constant]] Push pc;
#include "../../common/native_hair_math.h"
#include "../../common/native_hair_probability.h"
float hair_param(uint a,uint i){return asfloat(gAlphaRec.Load(a+48u+4u*i));}
float4 hair_sample(uint tex,float2 uv,float g){return gAlphaTex[NonUniformResourceIndex(tex)].SampleLevel(gAlphaWrap,uv,0);}
#include "hair_material.hlsli"

[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID){
 if(id.x>=pc.instances)return;
 uint4 ii=Info.Load4(id.x*16u);
 if((ii.y&(1u<<12))==0u || ii.w==0u)return;
 uint a=ii.z,rec_bytes,pool_bytes,soup_bytes;
 gAlphaRec.GetDimensions(rec_bytes);Pool.GetDimensions(pool_bytes);Soup.GetDimensions(soup_bytes);
 if(a>rec_bytes || rec_bytes-a<208u || gAlphaRec.Load(a+8u)!=4u)return;
 uint bounds=gAlphaRec.Load(a+192u);
 if(bounds>pool_bytes || ii.w>(pool_bytes-bounds)/24u || pool_bytes-bounds-ii.w*24u<4u)return;
 uint out_at=bounds+ii.w*24u;
 Pool.Store(out_at,asuint(-1.0));
 uint stride=ii.y&255u;if(stride<88u)return;
 bool held=(ii.y&(1u<<8))!=0u;
 uint bytes=held?pool_bytes:soup_bytes;
 if(ii.x>bytes || ii.w>(bytes-ii.x)/(3u*stride))return;
 // Pick a live strand frame, rather than interpreting native inactive NaNs.
 float3 n=0,t=0,b=0;bool found=false;
 for(uint prim=0;prim<ii.w;++prim){
  uint v=ii.x+prim*3u*stride;
  float marker=asfloat(held?Pool.Load(v+12u):Soup.Load(v+12u));
  if(hm_proven_inactive(marker))continue;
  n=asfloat(held?Pool.Load3(v+24u):Soup.Load3(v+24u));
  t=asfloat(held?Pool.Load3(v+36u):Soup.Load3(v+36u));
  b=asfloat(held?Pool.Load3(v+48u):Soup.Load3(v+48u));
  found=all(isfinite(n))&&all(isfinite(t))&&all(isfinite(b))&&dot(n,n)>0.0;
  if(found)break;
 }
 if(!found)return;
 float angular[8];
 [unroll]for(uint d=0;d<8u;++d){
  // Equal-solid-angle Fibonacci directions; even physical coverage admits
  // antipodes, so use the hemisphere (8 directions represent 16 full-sphere).
  float z=(float(d)+0.5)/8.0,phi=float(d)*2.399963229728653;
  float radius=sqrt(max(0.0,1.0-z*z));
  float3 v=float3(radius*cos(phi),radius*sin(phi),z);
  float3 local=normalize(float3(dot(v,b),dot(v,n),dot(v,t)));
  float3 world=normalize(b*local.x+n*local.y+t*local.z);
  angular[d]=saturate(abs(dot(world,normalize(n)))*5.0);
  if(!isfinite(angular[d]))return;
 }
 uint hits=0u;
 [loop]for(uint y=0;y<8u;++y)[loop]for(uint x=0;x<8u;++x){
  float2 uv=(float2(x,y)+0.5)/8.0;
  HairMaterialSample material=hair_material(a,uv,0.0,min(gAlphaRec.Load(a+12u),4095u),min(gAlphaRec.Load(a+16u),4095u),false);
  if(!material.ok)return;
  float base=material.mask.w*mad(-material.mask.z,hair_param(a,8u),1.0);
  [unroll]for(uint d=0;d<8u;++d){
   float coverage=(base*angular[d])*hair_param(a,9u);
   if(!isfinite(coverage))return;
   hits+=coverage>=0.3333?1u:0u;
  }
 }
 Pool.Store(out_at,asuint(nh_probability(hits,512u)));
}
