// D-LS exact-k + RIS remainder (ls_k > 0; docs/TRACK_D_LOCAL_LIGHTS.md 9.11), default settings (LS_K=1).
// core/gi.cpp uses it instead of rt_local_shadows.hlsl only with ls_k > 0. See rt_local_shadows.hlsl.
#define LS_K 1
#include "rt_local_shadows.hlsl"
