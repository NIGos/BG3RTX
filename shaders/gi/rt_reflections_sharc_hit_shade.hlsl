// dxc: -enable-16bit-types
// rf_hit_shade=1 with the SHaRC reflection query (sharc=1, sharc_query bit 2): a tier (b) hit the
// material cannot shade still asks the radiance cache. Layout: as rt_reflections_hit_shade.hlsl + set 4.
#define SHARC_QUERY_PASS 1
#define RF_HIT_SHADE 1
// With rt_cascade_casters (RT_CASCADE_MASKS) the sun ray at a hit (only) sees the cascade's casters.
#define RT_CASCADE_SUN_HIT 1
#define RT_HIT_MATERIAL 1
#include "rt_reflections.hlsl"
