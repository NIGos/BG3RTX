#pragma once
#include "../../common/native_hit_bounce.h"
// Extend the SAME direct reference by one BSDF continuation. The second
// vertex's direct NEE closes depth2; no third vertex/environment is evaluated.
struct NativeHitBounce {float3 radiance;uint status;bool positive_indirect;};
NativeHitBounce native_hit_bounce(uint2 fp,NativeHitMaterial primary,float3 position,float3 outgoing,
 float3 geometric_normal,RtCone cone,float3 primary_direct){
 NativeHitBounce result;result.radiance=primary_direct;result.status=HbPathSample;result.positive_indirect=false;
 uint seed=pixel_seed(fp,11u);
 float lobe=rand01(seed),u=rand01(seed),v=rand01(seed);
 HbContinuation continuation=hb_continuation(primary.scattering,primary.normal,geometric_normal,outgoing,lobe,u,v);
 result.status=continuation.status;if(result.status!=HbPathComplete)return result;
 if(!(pc.rt_tmax>0) || !isfinite(pc.rt_tmax) || !all(isfinite(position)) ||
    !isfinite(cone.f0) || !isfinite(cone.spread) || !(cone.f0>0) || cone.spread<0){result.status=HbPathNumeric;return result;}
 float3 ng=tb_direction(geometric_normal).v;
 float3 origin=rt_offset_ray(position,ng);
 if(!all(isfinite(origin))){result.status=HbPathNumeric;return result;}
 RtHit hit=rt_closest_hit(origin,continuation.direction,pc.rt_tmax,cone);
 if(hit.aborted){result.status=HbPathTraversal;return result;}
 if(!(hit.t>=0) || !isfinite(hit.t)){result.status=HbPathNoSecondary;return result;}
 float footprint=cone.f0+cone.spread*hit.t;
 NativeHitMaterial secondary=rt_hit_material(hit,footprint);
 if(secondary.status!=0){result.status=HbPathSecondaryMaterial;return result;}
 float3 secondary_position=origin+continuation.direction*hit.t;
 NativeHitDirect direct=native_hit_direct(secondary,secondary_position,-continuation.direction,rt_hit_normal(hit),footprint);
 if(direct.status!=HdOk){result.status=HbPathSecondaryDirect;return result;}
 HbSum sum=hb_compose(primary_direct,continuation.throughput,direct.radiance);
 result.status=sum.status;if(result.status!=HbPathComplete)return result;
 result.radiance=sum.radiance;result.positive_indirect=any(continuation.throughput*direct.radiance>0);return result;
}
