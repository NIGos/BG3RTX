#ifndef BG3_NATIVE_HIT_BOUNCE_H
#define BG3_NATIVE_HIT_BOUNCE_H
// Finite two-surface point/spot reference arithmetic, not a full radiance model.
#include "native_hit_direct.h"
#ifdef __cplusplus
namespace bg3::transport {
using HbVec3=TbVec3;using HbUint=uint32_t;
#else
#define HbVec3 float3
#define HbUint uint
#endif
static const HbUint HbPathComplete=0,HbPathNull=1,HbPathPrimaryMaterial=2,HbPathPrimaryDirect=3,
 HbPathSample=4,HbPathNormalDomain=5,HbPathTraversal=6,HbPathNoSecondary=7,
 HbPathSecondaryMaterial=8,HbPathSecondaryDirect=9,HbPathNumeric=10,HbPathPrimaryMismatch=11;
struct HbContinuation {HbVec3 direction,throughput;HbUint status;};
inline HbContinuation hb_continuation(NtFresnelBlendResult material,HbVec3 normal,HbVec3 geometric_normal,
 HbVec3 outgoing,float lobe,float u,float v){
 HbContinuation r;r.direction=r.throughput=tb_vec(0,0,0);r.status=HbPathSample;
 if(material.status!=NtOk)return r;
 TfFrame frame=tf_frame(normal);TbDirection ng=tb_direction(geometric_normal),wo=tb_direction(outgoing);
 if(frame.status!=TbOk || ng.status!=TbOk || wo.status!=TbOk)return r;
 HbVec3 local=tf_to_local(frame,wo.v);
 r.status=HbPathNormalDomain;if(!(local.z>0) || !(tb_dot(ng.v,wo.v)>0))return r;
 TbSample bsdf=tb_fresnel_sample(material.value,local,lobe,u,v);
 // NDF null probability is part of the sampler. Do not retry, renormalize or
 // confuse it with a scene miss (whose environment/geometry remains unknown).
 if(bsdf.status==TbNull){r.status=HbPathNull;return r;}
 r.status=HbPathSample;if(bsdf.status!=TbOk)return r;
 TbDirection world=tb_direction(tf_to_world(frame,bsdf.wi));
 if(world.status!=TbOk)return r;
 r.status=HbPathNormalDomain;if(!(tb_dot(ng.v,world.v)>0))return r;
 r.status=HbPathNumeric;if(!(bsdf.pdf>0) || !tb_finite(bsdf.pdf) || !(bsdf.wi.z>0))return r;
 float weight=bsdf.wi.z/bsdf.pdf;
 r.throughput=tb_vec(bsdf.f.x*weight,bsdf.f.y*weight,bsdf.f.z*weight);
 if(!tb_finite3(r.throughput) || r.throughput.x<0 || r.throughput.y<0 || r.throughput.z<0)return r;
 r.direction=world.v;r.status=HbPathComplete;return r;
}
struct HbSum {HbVec3 radiance;HbUint status;};
inline HbSum hb_compose(HbVec3 direct,HbVec3 throughput,HbVec3 secondary_direct){
 HbSum r;r.radiance=direct;r.status=HbPathNumeric;
 if(!tb_finite3(direct) || !tb_finite3(throughput) || !tb_finite3(secondary_direct) ||
    direct.x<0 || direct.y<0 || direct.z<0 || throughput.x<0 || throughput.y<0 || throughput.z<0 ||
    secondary_direct.x<0 || secondary_direct.y<0 || secondary_direct.z<0)return r;
 HbVec3 sum=tb_vec(direct.x+throughput.x*secondary_direct.x,direct.y+throughput.y*secondary_direct.y,direct.z+throughput.z*secondary_direct.z);
 if(!tb_finite3(sum))return r;
 r.radiance=sum;r.status=HbPathComplete;return r;
}
#ifdef __cplusplus
}
#else
#undef HbVec3
#undef HbUint
#endif
#endif
