// gi_hit_sun=1 (docs/PLAN_M4_REFLECTIONS_GI.md M4.3): gi_hit_direct plus the sun at the same
// qualified hidden hits (gi_trace_rt.hlsl RT_HIT_GI_SUN). Layout: the GI hit push set, the
// material AlphaDSL set 1, the D-LS sun set 3 (core/gi.cpp hit_gi_dispatch).
#define RT_HIT_GI_DIRECT 1
#define RT_HIT_GI_SUN 1
#define RT_CASCADE_SUN_HIT 1
#define RT_HIT_MATERIAL 1
#include "gi_trace_rt.hlsl"
