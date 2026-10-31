#ifndef BG3_NATIVE_VT_FIELDS_H
#define BG3_NATIVE_VT_FIELDS_H
// Shared post-sampling arithmetic of the audited Deferred VT family. Sampling,
// view interpretation, coverage and native frame provenance are caller evidence.
#include "transport_bsdf.h"
#ifdef __cplusplus
namespace bg3::transport {
using NvVec3=TbVec3;using NvUint=TbUint;
#else
typedef float3 NvVec3;typedef uint NvUint;
#endif
struct NvFields {
 NvVec3 albedo,frame_normal;
 float roughness,reflectance,metalness,aux;
 NvUint status;
};
inline float nv_unit(float x){return tb_min(1,tb_max(0,x));}
// Reflectance input (RT2.y before the clamp) of the VT deferred variants, by the recipe's
// reflectance mode (native_vt_surface.h): 0 material UBO 1:1 x (8995, 14678345); 1 the constant
// 0.5, no 1:1 binding (3da860be, 3df678c0); 2 mix(0.5, normal-layer sample x, UBO 1:1 x)
// (81657854: the normal layer's x, which the frame normal (wzy) does not use). nv_fields clamps.
inline float nv_reflectance(NvUint mode,float ubo,float normal_x){return mode==1u?0.5f:(mode==2u?0.5f+(normal_x-0.5f)*ubo:ubo);}
inline NvFields nv_fields(NvVec3 albedo,NvVec3 normal_wzy,NvVec3 physical,float reflectance){
 NvFields r;r.albedo=tb_vec(0,0,0);r.frame_normal=tb_vec(0,0,0);r.roughness=0;r.reflectance=0;r.metalness=0;r.aux=0;r.status=1;
 if(!tb_finite3(albedo)||!tb_finite3(normal_wzy)||!tb_finite3(physical)||!tb_finite(reflectance))return r;
 NvVec3 n=tb_vec(normal_wzy.x*2-1,normal_wzy.y*2-1,1-normal_wzy.z*2);
 float length2=(n.x*n.x+n.y*n.y)+n.z*n.z;
 if(!tb_finite(length2)||length2<=0){r.status=2;return r;}
 float length=tb_sqrt(length2);r.frame_normal=tb_vec(n.x/length,n.y/length,n.z/length);
 r.albedo=tb_vec(nv_unit(albedo.x),nv_unit(albedo.y),nv_unit(albedo.z));
 r.roughness=nv_unit(physical.y);r.reflectance=nv_unit(reflectance);r.metalness=nv_unit(physical.x);r.aux=physical.z;r.status=0;return r;
}
#ifdef __cplusplus
}
#endif
#endif
