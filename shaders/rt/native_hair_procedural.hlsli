// Native camera-depth surface over the original88-byte Hair card. Included
// only in capable variants, after alpha_eval, InstInfo and RtCone.
#include "../../common/native_hair_math.h"
#include "../../common/native_hair_probability.h"
static const uint kInfoHairProcedural=1u<<12;
struct HairPrimitive {
  InstInfo info;uint a,prim;float3 p0,p1,p2,n,c,rx,ry,rz,rw;float4 translation;
  float2 u0,u1,u2;float hmax;bool camera,ortho,valid,inactive;
};
struct HairProceduralHit {float t;bool unknown;};
HairProjection hair_projection(HairPrimitive p){
 HairProjection projection;projection.rx=p.rx;projection.ry=p.ry;projection.rz=p.rz;projection.rw=p.rw;
 projection.translation=p.translation;projection.near_far=asfloat(gAlphaRec.Load2(p.a+176));projection.ortho=p.ortho;return projection;
}
bool hair_proven_inactive(uint inst,uint prim){
 InstInfo ii=inst_info(inst);uint bytes;
 if((ii.meta&kInfoHairProcedural)==0u || !rec_in_range(ii.rec) || prim>=ii.tris || (ii.meta&255u)<88u)return false;
 if(gAlphaRec.Load(ii.rec+8u)!=4u)return false;
 if((ii.meta&kInfoPool)!=0u)gMeshPool.GetDimensions(bytes);else gOpaqueSoup.GetDimensions(bytes);
 if(!tri_in_range(ii.base,prim,ii.meta&255u,bytes))return false;
 uint at=ii.base+prim*3u*(ii.meta&255u)+12u;
 return hm_proven_inactive(asfloat((ii.meta&kInfoPool)!=0u?gMeshPool.Load(at):gOpaqueSoup.Load(at)));
}
HairPrimitive hair_primitive(uint inst,uint prim){
 HairPrimitive p=(HairPrimitive)0;p.info=inst_info(inst);p.a=p.info.rec;p.prim=prim;
 uint bytes;if((p.info.meta&kInfoPool)!=0u)gMeshPool.GetDimensions(bytes);else gOpaqueSoup.GetDimensions(bytes);
 if((p.info.meta&kInfoHairProcedural)==0u || !rec_in_range(p.a) || prim>=p.info.tris ||
    (p.info.meta&255u)<88u || !tri_in_range(p.info.base,prim,p.info.meta&255u,bytes))return p;
 if(gAlphaRec.Load(p.a+8u)!=4u)return p;
 uint converted=p.info.base+prim*3u*(p.info.meta&255u)+12u;
 float validity=asfloat((p.info.meta&kInfoPool)!=0u?gMeshPool.Load(converted):gOpaqueSoup.Load(converted));
 if(hm_proven_inactive(validity)){p.inactive=true;p.valid=true;return p;}
 p.p0=hair_vertex(p.info.base,p.info.meta,prim,0,60,3,false).xyz;
 p.p1=hair_vertex(p.info.base,p.info.meta,prim,1,60,3,false).xyz;
 p.p2=hair_vertex(p.info.base,p.info.meta,prim,2,60,3,false).xyz;
 p.u0=inst_uv(p.info,prim,0);p.u1=inst_uv(p.info,prim,1);p.u2=inst_uv(p.info,prim,2);
 p.n=cross(p.p1-p.p0,p.p2-p.p0);p.camera=gAlphaRec.Load(p.a+20u)!=0u;
 float4 r0=asfloat(gAlphaRec.Load4(p.a+96)),r1=asfloat(gAlphaRec.Load4(p.a+112)),r2=asfloat(gAlphaRec.Load4(p.a+128));
 p.translation=asfloat(gAlphaRec.Load4(p.a+144));
 p.rx=float3(r0.x,r1.x,r2.x);p.ry=float3(r0.y,r1.y,r2.y);p.rz=float3(r0.z,r1.z,r2.z);p.rw=float3(r0.w,r1.w,r2.w);
 p.ortho=asfloat(gAlphaRec.Load(p.a+184))==1.0;
 p.hmax=0.01*max(max(hair_param(p.a,4),hair_param(p.a,5)),0.0);
 if(p.camera && !p.ortho)p.c=hm_camera_center(hair_projection(p));
 p.valid=all(isfinite(p.p0))&&all(isfinite(p.p1))&&all(isfinite(p.p2))&&all(isfinite(p.c))&&
         all(isfinite(p.u0))&&all(isfinite(p.u1))&&all(isfinite(p.u2))&&dot(p.n,p.n)>0;
 return p;
}
float2 hair_uv(HairPrimitive p,float2 bary){return p.u0+(p.u1-p.u0)*bary.x+(p.u2-p.u0)*bary.y;}
float hair_gradient(HairPrimitive p){float2 e=p.u1-p.u0,f=p.u2-p.u0;return sqrt(abs(e.x*f.y-e.y*f.x)/max(length(p.n),1e-12));}
bool hair_virtual(HairPrimitive p,float3 base,float height,out float3 surface_point){
 surface_point=hm_virtual_point(hair_projection(p),base,height);
 return all(isfinite(surface_point));
}
bool hair_alpha(HairPrimitive p,float2 bary,float footprint,float3 ray,bool physical,out bool known){
 alpha_stat(0,true,p.prim);
 #if defined(RT_GI_HAIR_CULL)
 if(physical && RT_HAIR_GI_MEAN_ON && RT_HAIR_GI_MEAN_QUERY && (gAlphaRec.Load(12u)&0x80000000u)!=0u) {
  if((alpha_flags()&8u)!=0u){known=true;return true;}
  uint bytes;gMeshPool.GetDimensions(bytes);
  uint offset=gAlphaRec.Load(p.a+192u);
  if(nh_mean_range(offset,gAlphaRec.Load(p.a+200u),p.info.tris,gAlphaRec.Load(p.a+204u),bytes)) {
   float probability=asfloat(gMeshPool.Load(offset+p.info.tris*24u));
   if(nh_consume(probability,0u)!=2u) {
    // One stored probability bypasses coverage material/strand interpolation.
    // Include direction and instance so separate cards and rays are not correlated.
    uint3 h=pcg3d(uint3(asuint(bary.x)^asuint(ray.x),asuint(bary.y)^asuint(ray.y),
                      p.prim^(p.a*0x85ebca6bu)^(pc.frame*0x9e3779b9u)^asuint(ray.z)));
    known=true;bool solid=nh_consume(probability,h.x>>8)==1u;
    alpha_stat(8,solid,p.prim);alpha_stat(9,!solid,p.prim);return solid;
   }
  }
 }
 #endif
 hair_inputs(p.info.base,p.info.meta,p.prim,bary,ray,false,p.camera);
 gNativeHair.physical=physical;
 if((alpha_flags()&8u)!=0u){known=true;return true;}
 if(!gNativeHair.valid){known=false;return false;}
 AlphaRead alpha=native_hair_alpha(p.a,hair_uv(p,bary),footprint*hair_gradient(p),
                                  min(gAlphaRec.Load(p.a+12),4095u),min(gAlphaRec.Load(p.a+16),4095u));
 known=alpha.ok;bool solid=known && alpha.a>=0.3333;
 alpha_stat(8,known&&solid,p.prim);alpha_stat(9,known&&!solid,p.prim);return solid;
}
bool hair_ray_box(HairPrimitive p,float3 o,float3 d,float tmax,out float2 range){
 uint offset=gAlphaRec.Load(p.a+192u),bytes;gMeshPool.GetDimensions(bytes);
 range=float2(0,tmax);if(!tri_in_range(offset,p.prim,8u,bytes))return false;
 uint at=offset+p.prim*24u;float3 lo=asfloat(gMeshPool.Load3(at)),hi=asfloat(gMeshPool.Load3(at+12));
 if(!all(isfinite(lo))||!all(isfinite(hi)))return false;
 [unroll]for(uint j=0;j<3;++j){
  if(d[j]==0.0){if(o[j]<lo[j]||o[j]>hi[j]){range=float2(1,0);return true;}}
  else{float2 t=float2((lo[j]-o[j])/d[j],(hi[j]-o[j])/d[j]);range=float2(max(range.x,min(t.x,t.y)),min(range.y,max(t.x,t.y)));}
 }return true;
}
HairProceduralHit hair_intersect(uint inst,uint prim,float3 o,float3 d,float tmax,RtCone cone){
 HairProceduralHit hit;hit.t=-1;hit.unknown=false;
 HairPrimitive p=hair_primitive(inst,prim);if(!p.valid){hit.unknown=true;return hit;}
 if(p.inactive)return hit;
 float2 range;if(!hair_ray_box(p,o,d,tmax,range)){hit.unknown=true;return hit;}
 if(range.x>range.y)return hit;
 float denominator=dot(p.n,d),base_t=denominator!=0.0?dot(p.n,p.p0-o)/denominator:range.x;
 float footprint=max(cone.f0+cone.spread*base_t,0.0);
 const float gamma128=(128.0*0.00000011920928955078125)/(1.0-128.0*0.00000011920928955078125);
 bool primary=hm_camera_ray(hair_projection(p),o,d);
 bool physical=!gHairCameraQuery || !p.camera;
 if(!physical && !primary){hit.unknown=true;return hit;}
 {
  if(denominator==0.0)return hit;
  HairTriangleHit base_hit=hm_ray_triangle(p.p0,p.p1,p.p2,o,d);
  if(base_hit.status==2u){hit.unknown=true;return hit;}if(base_hit.status==0u)return hit;
  base_t=base_hit.t;footprint=max(cone.f0+cone.spread*base_t,0.0);
  float2 bary=base_hit.bary;float3 base=p.p0+(p.p1-p.p0)*bary.x+(p.p2-p.p0)*bary.y;
  float t=base_t;
  if(!physical && p.camera){
   HairMaterialSample material=hair_material(p.a,hair_uv(p,bary),footprint*hair_gradient(p),
                                            min(gAlphaRec.Load(p.a+12),4095u),min(gAlphaRec.Load(p.a+16),4095u),true);
   float3 surface_point;
   if(!material.ok || !hair_virtual(p,base,material.height,surface_point)){hit.unknown=true;return hit;}
   t=dot(surface_point-o,d)/dot(d,d);
   if(length(surface_point-(o+d*t))>gamma128*(length(surface_point)+length(o))){hit.unknown=true;return hit;}
  }
  if(t<0.0||t>tmax)return hit;
  bool known;bool solid=hair_alpha(p,bary,footprint,d,physical,known);
  if(!known)hit.unknown=true;else if(solid)hit.t=t;
  return hit;
 }
}
