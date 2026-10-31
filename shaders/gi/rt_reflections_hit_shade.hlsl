// D-RF tier (b), rf_hit_shade=1 (docs/PLAN_M4_REFLECTIONS_GI.md M4.2): hidden / off-screen hits
// shaded with the hit's native material and lighting (rt/native_hit_shade.hlsli). Layout: the
// D-RF push set, the material AlphaDSL set 1, the game's lighting set 2, the sun set 3 (core/gi.cpp).
#define RF_HIT_SHADE 1
// With rt_cascade_casters (RT_CASCADE_MASKS) the sun ray at a hit (only) sees the cascade's casters.
#define RT_CASCADE_SUN_HIT 1
#define RT_HIT_MATERIAL 1
#include "rt_reflections.hlsl"
