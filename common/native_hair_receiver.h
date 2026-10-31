#ifndef BG3_NATIVE_HAIR_RECEIVER_H
#define BG3_NATIVE_HAIR_RECEIVER_H
// Class 5 is shared by Hair/Pubes/Scalp. This rejects an unproven physical-origin
// substitution; it never changes alpha coverage, actual occluders or depth bounds.
#ifdef __cplusplus
#include <cmath>
#include <cstdint>
namespace bg3::hair_receiver {
using HprUint=uint32_t;
inline bool hpr_finite(float x){return std::isfinite(x);}
#else
#define HprUint uint
bool hpr_finite(float x){return isfinite(x);}
#endif
inline bool hpr_camera_scalp(HprUint lo,HprUint hi){
 return (lo==0x9f377ec5u && hi==0x9e77575au) || (lo==0xa83ea027u && hi==0x36eeb96bu);
}
inline bool hpr_window(float minimum,float maximum){
 return hpr_finite(minimum)&&hpr_finite(maximum)&&minimum>=0&&maximum>minimum;
}
inline bool hpr_reject(bool readable,bool camera_scalp,float local_t,float window_length,bool capped){
 if(capped || !readable || !hpr_finite(local_t) || !hpr_finite(window_length) ||
    local_t<0 || window_length<=0 || local_t>window_length)return true;
 // A captured camera Scalp triangle in the SAME match interval competes even
 // if the pixel-centre mask lookup rejects it: raster uses a jittered sample
 // and native derivatives. That lookup cannot prove absence of raster coverage.
 return camera_scalp;
}
#ifdef __cplusplus
}
#else
#undef HprUint
#endif
#endif
