// dxc: -enable-16bit-types
// The hybrid RT GI trace reading the SHaRC radiance cache at hidden and off-screen hits
// (sharc=1, core/gi.cpp; layout: the ray-traced push set, the host's set 1, the cache's set 4).
#define SHARC_QUERY_PASS 1
#include "gi_trace_rt.hlsl"
