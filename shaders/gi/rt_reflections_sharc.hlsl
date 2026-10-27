// dxc: -enable-16bit-types
// D-RF with the SHaRC radiance cache for glossy hidden/off-screen hits (sharc=1, sharc_query
// bit 2; core/gi.cpp; layout: the D-RF push set, the host's set 1, the game's set 2, set 4).
#define SHARC_QUERY_PASS 1
#include "rt_reflections.hlsl"
