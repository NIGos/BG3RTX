// rt_cascade_casters (layer/rtscene.cpp rt_cascade_convert): the XFB soup of a sun shadow
// cascade pass -> camera-relative world, in place, with the cascade's own orthographic
// view-projection (its set 0 binding 2 UBO, the camera layout; docs/TRACK_D_RT.md 6.7).
#define XFB_ORTHO 1
#include "xfb_to_world.hlsl"
