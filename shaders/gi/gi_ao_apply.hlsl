// Full resolution, before the tiled lighting: folds the traced visibility into
// the game's AO, so its own diffuse and specular ambient get occluded by it.
//
// QA ambient floor (qa_zero bit 2, docs/QA.md): the AO is written 0 after the fold. What
// the lighting (f469db78; docs/FRAME_MAP.md, rt/bg3_ambient.hlsli) then loses:
//   - the diffuse ambient (its SH/probe irradiance x AO): gone;
//   - the specular ambient (probes x SO): gone too, Lagarde's SO =
//     saturate(pow(|N.V + AO|, k) - 1 + AO) is exactly 0 at AO 0 (pow(|N.V|, k) <= 1);
//   - direct light: the lighting gives sun and local lights mix(1, AO, m0.y) (diffuse) and
//     mix(1, SO, m0.y) (specular), m0.y = 0.5 in gameplay (TRACK_D_RT 9.1), so they keep
//     half. D-LS's ao_direct (1 in the ini) gives the traced local lights back their full
//     value (it reads this AO); the sun (core/gi.cpp clamps ao_direct to 0..2, so its sun
//     bits are never set from the ini) and projector lights keep half.
// What stays: our GI bounce (gi_composite.hlsl adds it to the diffuse after the lighting,
// it does not read the AO: gi_strength=0 removes it), and D-RF's traced reflections (the
// game's specular occlusion is not applied to them).
// The trace is a lower bound for an effect that only removes ambient light: the floor it
// gives is lower than "everything but the ambient" by the halved direct light above.
#include "gi_common.hlsli"
#include "gi_aux_ao.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 fp = tid.xy;
  if (any(fp >= pc.full)) return;
  float d = gDepth.Load(int3(fp, 0));
  if (d <= 0.0) {
    if ((pc.qa_zero & 4u) != 0u) gAO[fp] = float4(0.0, gAO[fp].yzw);
    return;
  }
  float vis = gather_history(fp, d).a;
  float4 ao = gAO[fp];
  // The game's own AO, for the D-LS pass's AO-on-direct factor (ao_direct=2).
  aux_ao_store(fp, ao.x);
  // Replace rather than multiply: the game's SSAO and our visibility measure
  // the same occlusion, and their product darkens contacts twice.
  ao.x = lerp(ao.x, vis, pc.ao);
  if (pc.debug == 3) ao.x = vis;
  if ((pc.qa_zero & 4u) != 0u) ao.x = 0.0;   // QA ambient floor (see the top)
  gAO[fp] = ao;
}
