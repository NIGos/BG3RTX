#ifndef BG3_TRANSPORT_FRAME_H
#define BG3_TRANSPORT_FRAME_H
// Shared isotropic scattering frame. Local +Z is the normalized world normal.
// This is a coordinate transform, not a shading-normal/geometric-normal correction.
// Directions/vectors retain their magnitude; callers validate them as appropriate.
#include "transport_bsdf.h"
#ifdef __cplusplus
namespace bg3::transport {
using TfVec3=TbVec3;
using TfUint=uint32_t;
#else
#define TfVec3 float3
#define TfUint uint
#endif
struct TfFrame {TfVec3 t,b,n;TfUint status;};
inline TfVec3 tf_cross(TfVec3 a,TfVec3 b){
 return tb_vec(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);
}
inline TfFrame tf_frame(TfVec3 normal){
 TfFrame f;f.t=f.b=f.n=tb_vec(0,0,0);f.status=TbInvalid;
 TbDirection d=tb_direction(normal);if(d.status!=TbOk)return f;
 TfVec3 n=d.v;
 // The smallest absolute normal component gives |axis x n| >= sqrt(2/3)
 // in exact arithmetic. This avoids cancellation at both poles without bias.
 float x=tb_abs(n.x),y=tb_abs(n.y),z=tb_abs(n.z);
 TfVec3 axis=(x<=y && x<=z)?tb_vec(1,0,0):(y<=z?tb_vec(0,1,0):tb_vec(0,0,1));
 TbDirection t=tb_direction(tf_cross(axis,n));
 if(t.status!=TbOk){f.status=TbNumericFailure;return f;}
 f.n=n;f.t=t.v;f.b=tf_cross(f.n,f.t);f.status=TbOk;return f;
}
inline TfVec3 tf_to_local(TfFrame f,TfVec3 world){
 if(f.status!=TbOk)return tb_vec(0,0,0);
 return tb_vec(tb_dot(f.t,world),tb_dot(f.b,world),tb_dot(f.n,world));
}
inline TfVec3 tf_to_world(TfFrame f,TfVec3 local){
 if(f.status!=TbOk)return tb_vec(0,0,0);
 return tb_vec(f.t.x*local.x+f.b.x*local.y+f.n.x*local.z,
               f.t.y*local.x+f.b.y*local.y+f.n.y*local.z,
               f.t.z*local.x+f.b.z*local.y+f.n.z*local.z);
}
#ifdef __cplusplus
} // namespace bg3::transport
#else
#undef TfVec3
#undef TfUint
#endif
#endif
