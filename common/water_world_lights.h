#pragma once
// Shared CPU/HLSL bounded world query. 32 = one subgroup's worth of visibility
// rays per hit, versus the existing 1024-entry froxel bound (32x smaller).
#include "native_hit_direct.h"
#ifdef __cplusplus
namespace bg3::transport {
using WlVec3=HdVec3;using WlUint=HdUint;
#else
typedef float3 WlVec3;typedef uint WlUint;
#endif
static const WlUint WlK=32u;
struct WlSelection { WlUint ids[32]; float distances[32]; WlUint count, candidates; };
inline WlSelection wl_empty(){WlSelection s;s.count=0;s.candidates=0;return s;}
// Range is an authored bounding sphere. Strict boundary matches hd_native_incident.
inline bool wl_reaches(WlVec3 p,WlVec3 center,float inverse_radius,WlUint channels,WlUint channel){
 if(channel>31u || !tb_finite3(p) || !tb_finite3(center) ||
    !tb_finite(inverse_radius) || !(inverse_radius>0) || (channels&(1u<<channel))==0u)return false;
 WlVec3 d=tb_vec(p.x-center.x,p.y-center.y,p.z-center.z);
 float q=tb_dot(d,d), ir2=inverse_radius*inverse_radius;
 return tb_finite(q) && tb_finite(ir2) && ir2>0 && q<1/ir2;
}
// Stable nearest-centre order, slot id breaks ties. Caller visits each slot once.
inline void wl_insert(
#ifdef __cplusplus
 WlSelection& s,
#else
 inout WlSelection s,
#endif
 WlUint id,float d2){
 if(!tb_finite(d2) || d2<0)return;
 ++s.candidates;
 WlUint at=s.count;
 if(at==WlK){at=WlK-1u;if(d2>s.distances[at] || (d2==s.distances[at] && id>=s.ids[at]))return;}
 else ++s.count;
 while(at>0u){WlUint prev=at-1u;if(d2>s.distances[prev] || (d2==s.distances[prev] && id>=s.ids[prev]))break;
  s.ids[at]=s.ids[prev];s.distances[at]=s.distances[prev];at=prev;}
 s.ids[at]=id;s.distances[at]=d2;
}
#ifdef __cplusplus
}
#endif
