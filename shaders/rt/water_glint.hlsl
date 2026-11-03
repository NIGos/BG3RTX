// rt_water_glint_aa: the pre-pass of the WATER_GLINT_AA compose (shaders/rt/water_compose.hlsl glint_of /
// glint_mean, core/water.cpp compose_at_pass_end): each owned sun record's sun term D into gGlint, before the
// compose reads its neighbours'. Requested with the compose request's own defines; without WATER_GLINT_AA and
// WATER_SHADOWS (the plain file) the entry is empty.
#include "water_compose.hlsl"

[numthreads(8, 8, 1)]
void water_glint(uint3 tid : SV_DispatchThreadID) {
#if defined(WATER_GLINT_AA) && defined(WATER_SHADOWS)
  const uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  gGlint[fp] = glint_of(fp);
#endif
}
