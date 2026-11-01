// rt_hit_emission (docs/PLAN_M4_REFLECTIONS_GI.md M4.4): rt_reflections_hit_shade.hlsl plus the D-EM emission at its hits
// (rt/native_hit_emission.hlsli; the emissive scene set, core/gi.cpp hit_em_set).
#define RT_HIT_EMISSION 1
#include "rt_reflections_hit_shade.hlsl"
