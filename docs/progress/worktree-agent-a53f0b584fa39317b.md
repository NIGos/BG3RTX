## OMM on masked per-draw BLAS: rt_omm_dyn (branch worktree-agent-a53f0b584fa39317b, offline, READY_FOR_LIVE, 2026-11-02)
- New key `rt_omm_dyn` (live, off; needs rt_omm=1 at startup, rt_vt_alpha_masks=1, rt_alpha_fine_lod=1): camp
  grass and the other masked VT cutouts (per-draw BLAS, wind, refit every frame) get four-state micro-maps,
  subdivision 4, baked on the GPU from the offline masks at mip 0 (`common/omm_mask_bake.h`,
  `shaders/rt/omm_bake_mask.hlsl`), one per draw identity + mask + cuts + scale + triangle count, reused across
  frames and draws. Design and margins: docs/P4_OMM.md 11.2i.
- Exactness: every frame a check writes each triangle's micro-map index (itself if its uv bits equal the bake's
  snapshot, else FULLY_UNKNOWN_OPAQUE) before the build/refit. The ordinary TLAS disables the micro-maps per
  instance and is the reference scene of the existing omm check (rtStats 443..447); a flag-patched instance copy
  is the accelerated TLAS (proof 'OMM3'). rt_omm_reference switches between them inside one launch.
- Offline gates: order + bound vs the NVIDIA OMM SDK CPU baker 0 contradictions (1,183 known states, 3 layouts x
  5 levels); dense-sampling test 0 violations in 3.2M samples; host_atomics_lint pass (counters device-local);
  100/100 CTests (with BG3RTX_OMM_CPU_PROBE). No ABI change.
- Live (not run, needs steering): `tools/live_batches/omm_qual` job om4 (camp, ~4 min with load): diag-check
  violations 0, [omm dyn] share, rt passes / gi GPU ms cost-on vs cost-ref mean, energy shot-on vs shot-ref.
  Predicted (predicted.json): >= 90% of masked per-draw triangles with a micro-map, ~40-49% micro-triangles known,
  candidates 30-50% fewer, rt passes 1.5-3 ms lower (upper bound 5.7-6.3 ms), energy equal.
- Memory: backend micro-map storage 64 B per baked triangle (+ build scratch), ours 24 B per triangle after the
  bake, index ranges 5 B per masked triangle in the mesh pool; all logged on the [omm dyn] line.
