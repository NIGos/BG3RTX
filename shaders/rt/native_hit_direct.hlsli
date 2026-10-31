#pragma once
// Bounded reusable point/spot DIRECT-ONLY reference. No sun, projector,
// environment/probe, emission, AO multiplier or multiple-bounce energy implied.
#include "bg3_lights.hlsli"
#include "../../common/native_hit_direct.h"
struct NativeHitDirect {float3 radiance;uint status;uint lights;};
static const uint kHitDirectSource=5u,kHitDirectProjector=6u,kHitDirectDomain=7u;
// Unlike bg3_cluster_raw's renderer fallback, missing/overflowed sources cannot
// certify a zero direct term. Native frustum/froxel membership is required.
bool native_hit_cluster(float3 surface_position,out uint4 cluster){
 cluster=0;float2 uv;float depth;
 if(!all(isfinite(surface_position)) || !project(frustum_inverse(),surface_position,uv,depth) ||
    !all(isfinite(uv)) || any(uv<0) || any(uv>=1))return false;
 uint tile_size=asuint(lConsts.c[31].x),last_z=asuint(lConsts.c[31].y);
 float columns=lConsts.c[30].z,rows=lConsts.c[30].w;
 float z=log2(depth*cam2.c[12].y)*lConsts.c[30].x+lConsts.c[30].y;
 if(tile_size==0 || !isfinite(z) || !isfinite(columns+rows) || columns<1 || rows<1 ||
    columns!=floor(columns) || rows!=floor(rows) || z<0 || z>float(last_z))return false;
 if(any(lFlags.res==0u))return false;
 uint2 tile=uint2(uv*float2(lFlags.res))/tile_size;
 if(any(float2(tile)>=float2(columns,rows)))return false;
 float index=(floor(z)*rows+float(tile.y))*columns+float(tile.x);
 uint bytes;lClusters.GetDimensions(bytes);
 if(!isfinite(index) || index<0 || index>=float(bytes/16u))return false;
 cluster=lClusters.Load4(uint(index)*16u);
 uint points=cluster.y&4095u,spots=(cluster.y>>12)&4095u,count=points+spots;
 uint indices;lIndex.GetDimensions(indices);
 if(count>kMaxFroxelLights || cluster.x>indices/4u || count>indices/4u-cluster.x)return false;
 return true;
}
NativeHitDirect native_hit_direct(NativeHitMaterial material,float3 surface_position,float3 outgoing,float3 geometric_normal,float footprint){
 NativeHitDirect result;result.radiance=0;result.status=kHitDirectSource;result.lights=0;
 if(!isfinite(footprint) || footprint<0){result.status=kHitDirectDomain;return result;}
 if(material.status!=0){result.status=HdUnknownMaterial;return result;}
 uint4 cluster;if(!native_hit_cluster(surface_position,cluster))return result;
 // The declared subset is the complete point+spot term only; do not silently
 // label a projector-bearing receiver as a complete local-light reference.
 if((cluster.z&1023u)!=0u){result.status=kHitDirectProjector;return result;}
 uint points=cluster.y&4095u,count=points+((cluster.y>>12)&4095u);
 uint scale_bytes;lScale.GetDimensions(scale_bytes);if(scale_bytes<4)return result;
 float global_scale=asfloat(lScale.Load(0));
 [loop]for(uint item=0;item<count;++item){
  uint index=lIndex.Load((cluster.x+item)*4u),bytes;lLights.GetDimensions(bytes);
  if(index>=bytes/80u)return result;
  Bg3Light light=load_light(index);
  HdLight source;source.position=light.pos;source.color=light.col;source.direction=light.dir;
  source.inverse_radius=light.inv_r;source.cos_outer=light.cos_outer;source.cos_inner=light.cos_inner;
  source.global_selector=light.m3;source.falloff_exponent=light.m4;source.falloff_bias=light.m5;source.channels=light.chan;
  HdIncident incident=hd_native_incident(source,item>=points,surface_position,material.channel,global_scale);
  if(incident.status!=HdOk){result.status=incident.status;return result;}
  // Validate material/normal domain before spending a visibility ray.
  HdDirect term=hd_scatter(material.scattering,material.normal,geometric_normal,outgoing,incident,true,1);
  if(term.status!=HdOk){result.status=term.status;return result;}
  if(!any(term.radiance>0))continue;
  // flags bit0 means native atlas presence, not physical transparency to the
  // light. The physical point/spot reference tests every positive term,
  // including lights that the native renderer originally left unshadowed.
  float3 normal=normalize(geometric_normal);
  if(!(dot(normal,incident.direction)>0)){result.status=HdNormalDomain;return result;}
  float3 origin=rt_offset_ray(surface_position,normal),delta=light.pos-origin;
  float distance=length(delta);if(!(distance>0) || !isfinite(distance)){result.status=kHitDirectDomain;return result;}
  RtCone cone={footprint,0};uint visibility=rt_visibility(origin,delta/distance,distance,cone);
  if(visibility==kRtUnknown){result.status=HdUnknownVisibility;return result;}
  term=hd_scatter(material.scattering,material.normal,geometric_normal,outgoing,incident,true,visibility==kRtLit?1.0:0.0);
  if(term.status!=HdOk){result.status=term.status;return result;}
  result.radiance+=term.radiance;++result.lights;
 }
 result.status=all(isfinite(result.radiance))?HdOk:HdInvalid;return result;
}
