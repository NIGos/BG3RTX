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
// Two-sided VT variants (Base_AlphaTest_2s_VT FS 923131c7): the native FS scales the N column of
// mat3(T, N, B) by (gl_FrontFacing ? 1 : -1). At a ray hit the facing is taken from the geometry,
// independent of the winding and of the pipeline's frontFace: front when the ray origin and the
// interpolated vertex normal n lie on the same side of the triangle plane (any normal ng of it),
// i.e. the authored N faces the viewer. This equals the raster rule for triangles whose winding
// agrees with their authored normals. d: ray direction. Returns the N column's sign.
inline float nv_two_sided_sign(NvVec3 ng,NvVec3 n,NvVec3 d){
 const float view=-((ng.x*d.x+ng.y*d.y)+ng.z*d.z),side=(ng.x*n.x+ng.y*n.y)+ng.z*n.z;
 return view*side<0?-1.f:1.f;
}
#ifdef __cplusplus
inline float nv_pow(float x,float y){return std::pow(x,y);}
#else
float nv_pow(float x,float y){return pow(x,y);}
#endif
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
// ENV_DetailMap_VT (FS 613ead9c, VS 54df894a; native_vt_surface.h LayoutDetail). The detail UV is
// (UV * (|W - T| / |P|)) * tiling with W the world position, T the instance translation and P the
// object position (VS locations 0/1/2): the ratio is |A P| / |P| (A the instance 3x3).
inline float nv_detail_scale(float world_length,float object_length){return world_length/object_length;}
// On top of nv_fields (the VT layers; reflectance = 1:1 byte 8), with the 2D detail normal dn (1:3)
// and mask (1:8) sampled at the detail UV and the 1:1 strength (byte 0), mask power (12) and
// roughness add (16), as FS 613ead9c:
//   frame = normalize(fma(normalize(dn.wzy * 2 - 1, z negated) * strength, (1, 0, 1), base frame))
//   RT2.x roughness = clamp(fma(1 - mask.y, roughness add, physical.y))
//   RT1.w aux = clamp(pow(max(|mask.x|, 1e-4), mask power)) * physical.z
// Albedo, metalness (physical.x) and reflectance are the base ones. Status 2: degenerate normal.
inline NvFields nv_detail_fields(NvFields base,NvVec3 detail_wzy,float mask_x,float mask_y,float physical_y,float physical_z,
                                 float strength,float mask_power,float roughness_add){
 NvFields r=base;if(base.status!=0)return r;r.status=1;
 if(!tb_finite3(detail_wzy)||!tb_finite(mask_x)||!tb_finite(mask_y)||!tb_finite(physical_y)||!tb_finite(physical_z)||
    !tb_finite(strength)||!tb_finite(mask_power)||!tb_finite(roughness_add))return r;
 NvVec3 d=tb_vec(detail_wzy.x*2-1,detail_wzy.y*2-1,1-detail_wzy.z*2);
 float l2=(d.x*d.x+d.y*d.y)+d.z*d.z;if(!tb_finite(l2)||l2<=0){r.status=2;return r;}
 float l=tb_sqrt(l2);
 NvVec3 f=tb_vec(d.x/l*strength+base.frame_normal.x,base.frame_normal.y,d.z/l*strength+base.frame_normal.z);
 float f2=(f.x*f.x+f.y*f.y)+f.z*f.z;if(!tb_finite(f2)||f2<=0){r.status=2;return r;}
 float fl=tb_sqrt(f2);r.frame_normal=tb_vec(f.x/fl,f.y/fl,f.z/fl);
 r.roughness=nv_unit((1-mask_y)*roughness_add+physical_y);
 const float aux=nv_unit(nv_pow(tb_max(tb_abs(mask_x),1e-4f),mask_power))*physical_z;
 if(!tb_finite(aux)||!tb_finite(r.roughness))return r;
 r.aux=aux;r.status=0;return r;
}
// ENV_DetailMap_Iridescent (FS 9064a6bd, VS c32ce535; native_vt_surface.h LayoutIridescent). All
// samples are plain 2D (sampler 0:7): a = normal A 1:2 at UV * 2; p = physical 1:3, b = normal B
// 1:4, c = albedo 1:5 at UV * tiling (1:1 _m1); i = iridescent colour 1:6 at UV * 20. view_t =
// normalize(-W) * mat3(T, N, B) (the FS's camera direction in the frame; normalized here). As the FS:
//   n = normalize(fma((1, 0, -1) * strength, normalize(a) * p.w, normalize(b)))   (a, b: wzy*2-1, z negated)
//   t = pow(max(|1 - clamp(dot(n, normalize(view_t)))|, 1e-4), 1) * p.w * iridescence
//   RT1 albedo = clamp(mix(c, i, t)), aux = p.z; RT2 = (clamp(p.y), clamp(mix(lo, hi, p.w)), clamp(p.x))
// (strength _m0, iridescence _m2, lo/hi _m3/_m4). Status 1 non-finite, 2 degenerate normal/view.
inline NvFields nv_iridescent_fields(NvVec3 a_wzy,NvVec3 b_wzy,float px,float py,float pz,float pw,NvVec3 albedo,NvVec3 iridescent,
                                     NvVec3 view_t,float strength,float iridescence,float lo,float hi){
 NvFields r;r.albedo=tb_vec(0,0,0);r.frame_normal=tb_vec(0,0,0);r.roughness=0;r.reflectance=0;r.metalness=0;r.aux=0;r.status=1;
 if(!tb_finite3(a_wzy)||!tb_finite3(b_wzy)||!tb_finite(px)||!tb_finite(py)||!tb_finite(pz)||!tb_finite(pw)||!tb_finite3(albedo)||
    !tb_finite3(iridescent)||!tb_finite3(view_t)||!tb_finite(strength)||!tb_finite(iridescence)||!tb_finite(lo)||!tb_finite(hi))return r;
 NvVec3 a=tb_vec(a_wzy.x*2-1,a_wzy.y*2-1,1-a_wzy.z*2),b=tb_vec(b_wzy.x*2-1,b_wzy.y*2-1,1-b_wzy.z*2);
 float la=(a.x*a.x+a.y*a.y)+a.z*a.z,lb=(b.x*b.x+b.y*b.y)+b.z*b.z,lv=(view_t.x*view_t.x+view_t.y*view_t.y)+view_t.z*view_t.z;
 if(!(la>0)||!(lb>0)||!(lv>0)||!tb_finite(la+lb+lv)){r.status=2;return r;}
 la=tb_sqrt(la);lb=tb_sqrt(lb);lv=tb_sqrt(lv);
 NvVec3 f=tb_vec((a.x/la*pw)*strength+b.x/lb,b.y/lb,-((a.z/la*pw)*strength)+b.z/lb);
 float lf=(f.x*f.x+f.y*f.y)+f.z*f.z;if(!(lf>0)||!tb_finite(lf)){r.status=2;return r;}
 lf=tb_sqrt(lf);r.frame_normal=tb_vec(f.x/lf,f.y/lf,f.z/lf);
 const float c=nv_unit((r.frame_normal.x*view_t.x+r.frame_normal.y*view_t.y+r.frame_normal.z*view_t.z)/lv);
 const float t=(tb_max(tb_abs(1-c),1e-4f)*pw)*iridescence;
 r.albedo=tb_vec(nv_unit(albedo.x*(1-t)+iridescent.x*t),nv_unit(albedo.y*(1-t)+iridescent.y*t),nv_unit(albedo.z*(1-t)+iridescent.z*t));
 r.roughness=nv_unit(py);r.reflectance=nv_unit(lo*(1-pw)+hi*pw);r.metalness=nv_unit(px);r.aux=pz;
 if(!tb_finite3(r.albedo)||!tb_finite(r.reflectance))return r;
 r.status=0;return r;
}
#ifdef __cplusplus
}
#endif
#endif
