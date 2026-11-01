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
[[vk::binding(14,1)]] Texture2DArray<float4> gHitCache[nh_cache_slots];
[[vk::binding(15,1)]] SamplerState gHitSampler[nh_sampler_slots];
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
 if(table==0 || table>=4096 || cache>=nh_cache_slots || sampler>=nh_sampler_slots || col>2 || !(g>0) || !isfinite(g) || !all(isfinite(uv)))return result;
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
 if(index==0 || index>=4096 || sampler>=nh_sampler_slots || !(g>0) || !isfinite(g) || !isfinite(tiling))return result;
 float2 u=uv*tiling,dx=float2(g*tiling,0),dy=float2(0,g*tiling);
 if(!all(isfinite(u)) || !all(isfinite(dx)) || !all(isfinite(dy)))return result;
 result.value=gAlphaTex[NonUniformResourceIndex(index)].SampleGrad(gHitSampler[NonUniformResourceIndex(sampler)],u,dx,dy);
 result.known=all(isfinite(result.value));return result;
}
#endif
// One 2D texture-table view (ENV_DetailMap_VT detail normal/mask) with a declared isotropic
// ray-cone footprint g in its own UV units, as the VT layers.
NativeLayer hit_material_2d(uint index,uint sampler,float2 uv,float g){
 NativeLayer result=(NativeLayer)0;
 if(index==0 || index>=4096 || sampler>=nh_sampler_slots || !(g>0) || !isfinite(g) || !all(isfinite(uv)))return result;
 result.value=gAlphaTex[NonUniformResourceIndex(index)].SampleGrad(gHitSampler[NonUniformResourceIndex(sampler)],uv,float2(g,0),float2(0,g));
 result.known=all(isfinite(result.value));return result;
}
// Pool instance with a material row (native_hit_records.h nh_pool_meta, native_hit_pool.h): the
// qualified record (VS 2d2ac3c3 with FS 14678345, 3df678c0 or 81657854: the 8995 sampling recipe
// and FS frame, they differ only in the reflectance term, row word 220; VS 54df894a with FS
// 613ead9c adds the detail layers, row words 224..255; VS c32ce535 with FS 9064a6bd samples five
// plain 2D views instead of the VT layers, word 224 = 2) in the
// query's pool region, its UV and object-space N/B/T in the mesh pool. The basis was extracted
// from the G-buffer XFB of instance 0 after the positions matched M_0 * P within the mesh cache
// tolerance (rt/mesh_extract_basis.hlsl); the row binds it to this mesh by pool base, triangle
// count and mesh generation. Instance i: per vertex normalize(A_i * A_0^-1 * w_0), the native
// normalize(A_i * b), then interpolated as the rasterizer does. Same status codes as below.
NativeHitMaterial rt_hit_material_pool(RtHit hit,float footprint,InstInfo ii){
 NativeHitMaterial result=hit_material_unknown(1);
 const uint slot=ii.rec&~nh_pool_rec_flag,stride=ii.meta&255u;
 if((ii.meta&kInfoAlpha)!=0 || (ii.rec&nh_pool_rec_flag)==0 || slot>=nh_pool_rows || hit.prim>=ii.tris || stride<16u)return result;
 uint bytes,pbytes;gHitRecords.GetDimensions(bytes);gMeshPool.GetDimensions(pbytes);
 const uint row=nh_pool_row_base+slot*256u;
 if(bytes<row+256u || !tri_in_range(ii.base,hit.prim,stride,pbytes))return result;
 uint4 header=gHitRecords.Load4(0);
 if(header.w!=1 || header.x!=pc.frame || header.y==0 || header.y!=gSceneStamp.Load(0) || header.y!=gSceneStamp.Load(4))return hit_material_unknown(2);
 uint4 h0=gHitRecords.Load4(row),h1=gHitRecords.Load4(row+16),h2=gHitRecords.Load4(row+32);
 if(h0.x!=slot+1u || h0.y!=ii.tris || h0.z!=ii.base || h1.w!=pc.frame || h1.x!=nh_pool_basis_vertex || h2.w!=3u)return hit_material_unknown(3);
 if(h0.y>(0xffffffffu-nh_pool_basis_header)/(3u*nh_pool_basis_vertex) || h0.w>pbytes || nh_pool_basis_header+h0.y*3u*nh_pool_basis_vertex>pbytes-h0.w)return result;
 uint4 bh=gMeshPool.Load4(h0.w);
 if(bh.x!=nh_pool_basis_magic || bh.y!=gHitRecords.Load(row+216u) || bh.z!=h0.y || bh.w!=1u)return hit_material_unknown(4);
 const uint first=h0.w+nh_pool_basis_header+hit.prim*3u*nh_pool_basis_vertex;
 const float3x3 a=(float3x3)hit.o2w;
 float2 uv[3];float3 position[3];
 [unroll]for(uint v=0;v<3;++v){
  uv[v]=asfloat(gMeshPool.Load2(first+v*nh_pool_basis_vertex));position[v]=mul(hit.o2w,float4(inst_pos(ii,hit.prim,v),1.0));
 }
 float area=length(cross(position[1]-position[0],position[2]-position[0]));
 float2 d1=uv[1]-uv[0],d2=uv[2]-uv[0];float uvarea=abs(d1.x*d2.y-d1.y*d2.x);
 if(!(area>0 && uvarea>0) || !isfinite(area+uvarea))return hit_material_unknown(5);
 float g=footprint*sqrt(uvarea/area);float2 u=uv[0]+d1*hit.bary.x+d2*hit.bary.y;
 float3 basis[3];
 [unroll]for(uint component=0;component<3;++component){
  float3 w[3];
  [unroll]for(uint v=0;v<3;++v){float3 b=mul(a,asfloat(gMeshPool.Load3(first+v*nh_pool_basis_vertex+8u+12u*component)));w[v]=b*rsqrt(dot(b,b));}
  basis[component]=w[0]+(w[1]-w[0])*hit.bary.x+(w[2]-w[0])*hit.bary.y;
 }
 const uint4 detail=gHitRecords.Load4(row+nh_pool_row_detail);
 NvFields fields;
 if(detail.x==2u){
  // ENV_DetailMap_Iridescent (word 224 = 2; native_vt_fields.h nv_iridescent_fields): five 2D
  // table views (words 228..244 = bindings 1:2..1:6) with the row sampler (set 0:7), the 1:1
  // floats at 48 (strength, tiling, iridescence, reflectance lo) and 64 (hi). The FS view
  // vector is normalize(-W), W the camera-relative world position (VS location 4): here the
  // hit's interpolated position, in the frame mat3(T, N, B) of the interpolated VS outputs.
  const uint4 views=gHitRecords.Load4(row+nh_pool_row_detail+4u);const uint colour=gHitRecords.Load(row+nh_pool_row_detail+20u);
  const float4 q=asfloat(gHitRecords.Load4(row+48u));const float hi=asfloat(gHitRecords.Load(row+64u));
  const float2 ut=u*q.y;const float gt=g*abs(q.y);
  NativeLayer na=hit_material_2d(views.x,h1.z,u*2.0,g*2.0),ph=hit_material_2d(views.y,h1.z,ut,gt),nb=hit_material_2d(views.z,h1.z,ut,gt),
              co=hit_material_2d(views.w,h1.z,ut,gt),ir=hit_material_2d(colour,h1.z,u*20.0,g*20.0);
  if(!na.known || !ph.known || !nb.known || !co.known || !ir.known)return hit_material_unknown(6);
  const float3 w=position[0]+(position[1]-position[0])*hit.bary.x+(position[2]-position[0])*hit.bary.y;
  if(!(dot(w,w)>0))return hit_material_unknown(5);
  const float3 v=-w*rsqrt(dot(w,w));
  fields=nv_iridescent_fields(na.value.wzy,nb.value.wzy,ph.value.x,ph.value.y,ph.value.z,ph.value.w,co.value.rgb,ir.value.rgb,
                              float3(dot(v,basis[2]),dot(v,basis[0]),dot(v,basis[1])),q.x,q.z,q.w,hi);
  if(fields.status!=0)return hit_material_unknown(7);
 }else{
 NativeLayer al=hit_material_layer(row,u,g,h1.y,h2.y,h1.z,0),nl=hit_material_layer(row,u,g,h1.y,h2.x,h1.z,1),pl=hit_material_layer(row,u,g,h1.y,h2.z,h1.z,2);
 if(!al.known || !nl.known || !pl.known)return hit_material_unknown(6);
 // Row word 220: the record pair's reflectance mode (native_vt_fields.h nv_reflectance; 81657854
 // mixes 0.5 with the normal layer's x by the UBO value; 3df678c0's 0.5 is already in word 208).
 const float reflectance=nv_reflectance(gHitRecords.Load(row+220u),asfloat(gHitRecords.Load(row+208u)),nl.value.x);
 fields=nv_fields(al.value.rgb,nl.value.wzy,pl.value.xyz,reflectance);
 if(fields.status!=0)return hit_material_unknown(7);
 // ENV_DetailMap_VT (word 224 = 1): detail UV = (UV * |A P| / |P|) * tiling at the interpolated
 // object position (the FS's interpolated W - T and P), footprint scaled alike.
 if(detail.x==1u){
  const float4 dp=asfloat(gHitRecords.Load4(row+nh_pool_row_detail+16u));
  const float3 p0=inst_pos(ii,hit.prim,0),p1=inst_pos(ii,hit.prim,1),p2=inst_pos(ii,hit.prim,2);
  const float3 p=p0+(p1-p0)*hit.bary.x+(p2-p0)*hit.bary.y;
  const float scale=nv_detail_scale(length(mul(a,p)),length(p));
  const float2 du=(u*scale)*dp.y;const float dg=(g*scale)*abs(dp.y);
  NativeLayer dn=hit_material_2d(detail.y,detail.w,du,dg),dm=hit_material_2d(detail.z,detail.w,du,dg);
  if(!dn.known || !dm.known)return hit_material_unknown(6);
  fields=nv_detail_fields(fields,dn.value.wzy,dm.value.x,dm.value.y,pl.value.y,pl.value.z,dp.x,dp.z,dp.w);
  if(fields.status!=0)return hit_material_unknown(7);
 }
 }
 // Native FS mat3(T,N,B) with the VS outputs at locations 3/1/2 (FS 14678345 _237, as 8995).
 float3 normal=basis[2]*fields.frame_normal.x+basis[0]*fields.frame_normal.y+basis[1]*fields.frame_normal.z;
 if(!all(isfinite(normal)) || !(dot(normal,normal)>0))return hit_material_unknown(8);
 NtInput input;input.linear_albedo=fields.albedo;input.roughness=fields.roughness;input.reflectance=fields.reflectance;input.metalness=fields.metalness;
 input.material_class=0;input.evidence=NtSamplesKnown|NtLinearColorKnown|NtOpaqueCoverageKnown|NtLobesKnown;
 result.parameters=nt_class0_parameters(input);result.scattering=nt_to_fresnel_blend(result.parameters);result.normal=normalize(normal);
 result.channel=gHitRecords.Load(row+212u);
#ifdef RT_NATIVE_TILESET
 result.family=0u;
#endif
 result.status=(result.parameters.status==NtOk && result.scattering.status==NtOk)?0:9;return result;
}
NativeHitMaterial rt_hit_material(RtHit hit,float footprint){
 if(!hit_material_publication_ready())return hit_material_unknown(1);
 NativeHitMaterial result=hit_material_unknown(1);
 if(hit.t<0 || hit.aborted || !isfinite(footprint) || footprint<=0 || !rt_scene_ok())return result;
 InstInfo ii=inst_info(hit.inst);
 if((ii.meta&(nh_pool_meta|kInfoPool|kInfoLegacy))==(nh_pool_meta|kInfoPool))return rt_hit_material_pool(hit,footprint,ii);
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
 // Row word 220 bit 0 (rt_hit_material_instanced, RT_HIT_INSTANCED variants, common/native_hit_instanced.h): two-sided FS
 // (Base_AlphaTest_2s_VT 923131c7) scales the N column by (gl_FrontFacing ? 1 : -1); at a hit the
 // facing is the geometry's: nv_two_sided_sign of the captured triangle, its interpolated N and the ray.
#ifdef RT_HIT_INSTANCED
 if((gHitRecords.Load(row+220u)&1u)!=0u)basis[0]*=nv_two_sided_sign(cross(position[1]-position[0],position[2]-position[0]),basis[0],hit.dir);
#endif
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
// all (neither nh_opaque_meta nor kInfoAlpha in its BVH-split info, nor a pool row: a draw
// family, instancing or pool the provider does not cover). Callers gate it on their stats key.
void hit_material_count(RtHit hit,uint status,uint bin){
 [unroll]for(uint s=0;s<10;++s)stat_add(253u+s,status==s,bin);
 bool uncovered=false;
 if(status==1u && hit.t>=0 && !hit.aborted){InstInfo ii=inst_info(hit.inst);
  const bool pool_row=(ii.meta&(nh_pool_meta|kInfoPool|kInfoLegacy))==(nh_pool_meta|kInfoPool);
  uncovered=!pool_row && ((ii.meta&(nh_opaque_meta|kInfoAlpha))==0u || (ii.meta&(kInfoLegacy|kInfoPool))!=0u);}
 stat_add(127u,uncovered,bin);
}
#endif
