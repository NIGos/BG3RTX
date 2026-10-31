// dxc: -enable-16bit-types
// gi_hit_sun=1 with SHaRC (gi_trace_rt_hit_sun.hlsl); the SHaRC set stays set 4.
#define RT_HIT_GI_DIRECT 1
#define RT_HIT_GI_SUN 1
#define RT_CASCADE_SUN_HIT 1
#define RT_HIT_MATERIAL 1
#include "gi_trace_rt_sharc.hlsl"
