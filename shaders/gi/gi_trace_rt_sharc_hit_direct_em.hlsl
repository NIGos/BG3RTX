// dxc: -enable-16bit-types
// rt_hit_emission (docs/PLAN_M4_REFLECTIONS_GI.md M4.4): gi_trace_rt_sharc_hit_direct.hlsl plus the D-EM emission at its hits
// (rt/native_hit_emission.hlsli; the emissive scene set, core/gi.cpp hit_em_set).
#define RT_HIT_EMISSION 1
#include "gi_trace_rt_sharc_hit_direct.hlsl"
