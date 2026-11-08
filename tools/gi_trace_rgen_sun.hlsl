// SER prototype (offline, frame_replay --ab-rgen): the production Ultra GI trace without emission,
// gi/gi_trace_rt_hit_sun.hlsl with RT_SPEC_GI_FAR (spec 1007), compiled as a ray generation shader (lib_6_6) over
// the same thread grid. Spec 1050 (kGiSerHint): 0 no reorder, 1 hit kind + instance, 2 hit kind only.
// Never a game request: shaders/variants.txt does not list it.
#define GI_TRACE_RAYGEN 1
#include "gi/gi_trace_rt_hit_sun.hlsl"
