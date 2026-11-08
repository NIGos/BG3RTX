// SER prototype (offline, frame_replay --ab-rgen): gi/gi_trace_rt_hit_direct.hlsl compiled as a ray generation
// shader (lib_6_6), the same body over the same thread grid, with one OpReorderThreadWithHintEXT per GI ray when
// specialization constant 1050 (kGiSerHint) is 1. Never a game request: shaders/variants.txt does not list it.
#define GI_TRACE_RAYGEN 1
#include "gi/gi_trace_rt_hit_direct.hlsl"
