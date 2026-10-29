// D-LS with the box (projector) lights (ls_box=1; docs/TRACK_D_RT.md 9.12), default settings.
// Reads the lighting's own set 1 as set 3 (core/gi.cpp ls_box layout). See rt_local_shadows.hlsl.
#define LS_BOX 1
#include "rt_local_shadows.hlsl"
