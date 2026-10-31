#ifndef BG3_NATIVE_HIT_MATERIAL_HLSLI
#define BG3_NATIVE_HIT_MATERIAL_HLSLI
#include "../../common/native_opaque_transport.h"
#include "../../common/native_vt_fields.h"
#include "../../common/native_hit_records.h"
#ifdef RT_NATIVE_TILESET
#include "../../common/native_hit_tileset_fields.h"
#endif
// Main AlphaDSL ONLY. Private current-Sun binding14 has a different contract.
[[vk::binding(12,1)]] ByteAddressBuffer gHitRecords;
[[vk::binding(13,1)]] ByteAddressBuffer gHitBasis;
[[vk::binding(14,1)]] Texture2DArray<float4> gHitCache[64];
[[vk::binding(15,1)]] SamplerState gHitSampler[16];
struct NativeHitMaterial {NtResult parameters;NtFresnelBlendResult scattering;float3 normal;uint status,channel;
#ifdef RT_NATIVE_TILESET
 uint family;
#endif
};
bool hit_material_publication_ready(){
 uint bytes;gHitRecords.GetDimensions(bytes);const uint control=16u+nh_material_rows*256u;
 return bytes>=control+4u && gHitRecords.Load(control)==1u;
}
NativeHitMaterial hit_material_unknown(uint reason){
 NativeHitMaterial r=(NativeHitMaterial)0;r.status=reason;r.parameters=nt_empty(NtUnknownSamples);
 r.scattering=nt_to_fresnel_blend(r.parameters);return r;
}
float hm_t(uint row,uint matrix,uint col,uint r){return asfloat(gHitRecords.Load(row+80u+4u*(matrix*16u+r*4u+col)));}
struct NativeLayer {float4 value;bool known;};
// Audited8995 sampling recipe, with declared isotropic ray-cone UV derivatives
// instead of unavailable raster quad derivatives. The actual native sampler,
// views, resident indirection channel and per-layer tile offsets are retained.
NativeLayer hit_material_layer(uint row,float2 uv,float g,uint table,uint cache,uint sampler,uint col){
 NativeLayer result=(NativeLayer)0;
 if(table==0 || table>=4096 || cache>=64 || sampler>=16 || col>2 || !(g>0) || !isfinite(g) || !all(isfinite(uv)))return result;
 if(hm_t(row,0,2,3)!=2202.0)return result;
 float4 p0=asfloat(gHitRecords.Load4(row+48)),p1=asfloat(gHitRecords.Load4(row+64));
 float2 dx=p0.zw*float2(g,0),dy=p0.zw*float2(0,g);
 float2 scale=float2(hm_t(row,0,2,0),hm_t(row,0,3,0));
 float d1=dot(dx*scale,dx*scale),d2=dot(dy*scale,dy*scale);
 if(!(d1>0 && d2>0) || !isfinite(d1+d2))return result;
 float l=log2(max(d1,d2));float aniso=min(.5*l-.5*log2(min(d1,d2)),hm_t(row,0,1,0));
 float mip=floor(min(max(.5*l-aniso-.5,0.0),p1.x)+.5);
 uint w,h,levels;gAlphaTex[NonUniformResourceIndex(table)].GetDimensions(0,w,h,levels);
 if(!isfinite(mip) || mip<0 || mip>=levels)return result;
 float2 u=mad(frac(uv),p0.zw,p0.xy);
 float2 dims=float2(hm_t(row,0,0,3),hm_t(row,0,0,3)*hm_t(row,0,1,3));
 float2 ip=floor(u*dims*exp2(-mip));
 if(!all(isfinite(ip)) || any(ip<0) || ip.x>=max(w>>uint(mip),1u) || ip.y>=max(h>>uint(mip),1u))return result;
 uint4 page=asuint(gAlphaTex[NonUniformResourceIndex(table)].Load(int3(int2(ip),int(mip))));uint e=page[col];
 float sc=exp2(float(e&15u));float2 s163=float2(sc,sc*hm_t(row,0,1,3));
 float2 s168=float2(hm_t(row,0,0,2),hm_t(row,0,1,2));
 float2 s175=float2(hm_t(row,1,0,col),hm_t(row,1,1,col));
 float2 c=mad(mad(frac(u*s163),s168,float2(float((e>>14)&1023u),float((e>>4)&1023u))),s175,float2(hm_t(row,1,2,col),hm_t(row,1,3,col)));
 // All three native cache derivatives use tile-set matrix0[0].y, not the
 // component of the sampled layer (FS8995 _207/_322/_415/_509/_616).
 float2 gradient=(s163*s168)*s175*hm_t(row,0,0,1);
 uint cw,ch,layers,cacheLevels;gHitCache[NonUniformResourceIndex(cache)].GetDimensions(0,cw,ch,layers,cacheLevels);
 uint layer=(e>>24)&127u;
 if(layer>=layers || !all(isfinite(c)) || !all(isfinite(gradient)))return result;
 result.value=gHitCache[NonUniformResourceIndex(cache)].SampleGrad(gHitSampler[NonUniformResourceIndex(sampler)],float3(c,float(layer)),dx*gradient,dy*gradient);
 result.known=all(isfinite(result.value));return result;
}
#ifdef RT_NATIVE_TILESET
NativeLayer hit_material_tileset_layer(uint row,uint layer,float2 uv,float g,float tiling,uint sampler){
 NativeLayer result=(NativeLayer)0;uint index=gHitRecords.Load(row+80u+4u*layer);
 if(index==0 || index>=4096 || sampler>=16 || !(g>0) || !isfinite(g) || !isfinite(tiling))return result;
 float2 u=uv*tiling,dx=float2(g*tiling,0),dy=float2(0,g*tiling);
 if(!all(isfinite(u)) || !all(isfinite(dx)) || !all(isfinite(dy)))return result;
 result.value=gAlphaTex[NonUniformResourceIndex(index)].SampleGrad(gHitSampler[NonUniformResourceIndex(sampler)],u,dx,dy);
 result.known=all(isfinite(result.value));return result;
}
#endif
NativeHitMaterial rt_hit_material(RtHit hit,float footprint){
 if(!hit_material_publication_ready())return hit_material_unknown(1);
 NativeHitMaterial result=hit_material_unknown(1);
 if(hit.t<0 || hit.aborted || !isfinite(footprint) || footprint<=0 || !rt_scene_ok())return result;
 InstInfo ii=inst_info(hit.inst);
 bool opaque=(ii.meta&nh_opaque_meta)!=0;
 if((ii.meta&(kInfoLegacy|kInfoPool))!=0 || hit.prim>=ii.tris)return result;
 if(opaque){if((ii.meta&kInfoAlpha)!=0 || ii.rec<nh_alpha_rows || ii.rec>=nh_material_rows)return result;}
 else if((ii.meta&kInfoAlpha)==0 || !rec_in_range(ii.rec) || (ii.rec-16u)%208u!=0)return result;
#ifdef RT_NATIVE_HAIR
 if((ii.meta&kInfoHairProcedural)!=0)return result;
#endif
 uint record=opaque?ii.rec:(ii.rec-16u)/208u,bytes;gHitRecords.GetDimensions(bytes);
 if(record>=nh_material_rows || bytes<16u+256u || record>=(bytes-16u)/256u)return result;
 uint4 header=gHitRecords.Load4(0);
 if(header.w!=1 || header.x!=pc.frame || header.y==0 || header.y!=gSceneStamp.Load(0) || header.y!=gSceneStamp.Load(4))return hit_material_unknown(2);
 uint row=16u+record*256u;uint4 h0=gHitRecords.Load4(row),h1=gHitRecords.Load4(row+16),h2=gHitRecords.Load4(row+32);
 if(h0.x!=record+1u || hit.prim>=h0.y || h1.w!=pc.frame || h1.x<64 || h1.x!=(ii.meta&255u))return hit_material_unknown(3);
 #ifdef RT_NATIVE_TILESET
 bool tileset=h2.w==2u;if(tileset && h1.x<NtsMinimumStride)return result;
#endif
 uint bbytes;gHitBasis.GetDimensions(bbytes);uint size=h0.y*3u*h1.x;
 if(h0.y>0xffffffffu/(3u*h1.x) || h0.z>bbytes || size>bbytes-h0.z || h0.w>bbytes || size>bbytes-h0.w)return result;
 // Actual native positions and UVs, not a mesh/hash-only proof. Exact comparison
 // intentionally declines even harmless arithmetic differences in this slice.
 [unroll]for(uint v=0;v<3;++v){
  uint pre=h0.z+(hit.prim*3u+v)*h1.x,def=h0.w+(hit.prim*3u+v)*h1.x;
  if(any(gHitBasis.Load4(pre)!=gHitBasis.Load4(def)) || any(gHitBasis.Load2(pre+16)!=gHitBasis.Load2(def+16)))return hit_material_unknown(4);
#ifdef RT_NATIVE_TILESET
  if(tileset && any(gHitBasis.Load4(pre+NtsColorOffset)!=gHitBasis.Load4(def+NtsColorOffset)))return hit_material_unknown(4);
#endif
 }
 float2 uv[3];float3 position[3];float3 basis[3];
 [unroll]for(uint v=0;v<3;++v){
  uint at=h0.w+(hit.prim*3u+v)*h1.x;
  uv[v]=asfloat(gHitBasis.Load2(at+16));position[v]=inst_pos(ii,hit.prim,v);
 }
 float area=length(cross(position[1]-position[0],position[2]-position[0]));
 float2 d1=uv[1]-uv[0],d2=uv[2]-uv[0];float uvarea=abs(d1.x*d2.y-d1.y*d2.x);
 if(!(area>0 && uvarea>0) || !isfinite(area+uvarea))return hit_material_unknown(5);
 float g=footprint*sqrt(uvarea/area);float2 u=uv[0]+d1*hit.bary.x+d2*hit.bary.y;
 NativeLayer a,n,p;
#ifdef RT_NATIVE_TILESET
 if(tileset){
  uint at=h0.w+hit.prim*3u*h1.x+NtsColorOffset;
  float4 c0=asfloat(gHitBasis.Load4(at)),c1=asfloat(gHitBasis.Load4(at+h1.x)),c2=asfloat(gHitBasis.Load4(at+2u*h1.x));
  float4 color=c0+(c1-c0)*hit.bary.x+(c2-c0)*hit.bary.y;
  float4 params=asfloat(gHitRecords.Load4(row+48));NtsPlan plan=nts_plan(color.x,color.y,params.xyz);
  if(plan.status!=0)return hit_material_unknown(6);
  n=hit_material_tileset_layer(row,plan.branch,u,g,plan.tiling,h1.z);
  a=hit_material_tileset_layer(row,3u+plan.branch,u,g,plan.tiling,h1.z);
  p=hit_material_tileset_layer(row,6u+plan.branch,u,g,plan.tiling,h1.z);
 }else
#endif
 {
  a=hit_material_layer(row,u,g,h1.y,h2.y,h1.z,0);
  n=hit_material_layer(row,u,g,h1.y,h2.x,h1.z,1);
  p=hit_material_layer(row,u,g,h1.y,h2.z,h1.z,2);
 }
 if(!a.known || !n.known || !p.known)return hit_material_unknown(6);
 NvFields fields=nv_fields(a.value.rgb,n.value.wzy,p.value.xyz,asfloat(gHitRecords.Load(row+208u)));
 if(fields.status!=0)return hit_material_unknown(7);
 [unroll]for(uint component=0;component<3;++component){
  uint start=h0.w+hit.prim*3u*h1.x+24u+12u*component;
#ifdef RT_NATIVE_TILESET
  if(tileset)start=h0.w+hit.prim*3u*h1.x+NtsBasisOffset+12u*component;
#endif
  float3 b0=asfloat(gHitBasis.Load3(start)),b1=asfloat(gHitBasis.Load3(start+h1.x)),b2=asfloat(gHitBasis.Load3(start+2u*h1.x));
  basis[component]=b0+(b1-b0)*hit.bary.x+(b2-b0)*hit.bary.y;
 }
 // Native FS mat3(T,N,B) with original interpolated VS outputs (loc3/1/2).
 float3 normal=basis[2]*fields.frame_normal.x+basis[0]*fields.frame_normal.y+basis[1]*fields.frame_normal.z;
 if(!all(isfinite(normal)) || !(dot(normal,normal)>0))return hit_material_unknown(8);
 NtInput input;input.linear_albedo=fields.albedo;input.roughness=fields.roughness;input.reflectance=fields.reflectance;input.metalness=fields.metalness;
 input.material_class=0;input.evidence=NtSamplesKnown|NtLinearColorKnown|NtOpaqueCoverageKnown|NtLobesKnown;
 result.parameters=nt_class0_parameters(input);result.scattering=nt_to_fresnel_blend(result.parameters);result.normal=normalize(normal);
 result.channel=gHitRecords.Load(row+212u);
#ifdef RT_NATIVE_TILESET
 result.family=tileset?NtsRecipe:0u;
#endif
 result.status=(result.parameters.status==NtOk && result.scattering.status==NtOk)?0:9;return result;
}
// Reason histogram of one lookup (core/gi.cpp "hit material" line): counter 253+status
// (0 qualified; 1 base, 2 epoch, 3 row, 4 clip, 5 geometry, 6 sampling, 7 fields, 8 frame,
// 9 parameters) and 127: status 1 because the hit instance carries no material record at
// all (neither nh_opaque_meta nor kInfoAlpha in its BVH-split info: a draw family, instancing
// or pool the provider does not cover). Callers gate it on their stats key.
void hit_material_count(RtHit hit,uint status,uint bin){
 [unroll]for(uint s=0;s<10;++s)stat_add(253u+s,status==s,bin);
 bool uncovered=false;
 if(status==1u && hit.t>=0 && !hit.aborted){InstInfo ii=inst_info(hit.inst);uncovered=(ii.meta&(nh_opaque_meta|kInfoAlpha))==0u || (ii.meta&(kInfoLegacy|kInfoPool))!=0u;}
 stat_add(127u,uncovered,bin);
}
#endif
