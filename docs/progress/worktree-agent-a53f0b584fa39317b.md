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

## Main integration (offline, 2026-11-03)
- Resolved settings defaults with the generator; retained main VRAM guard, BLAS retirement/compaction and CPU perf work.
- Micromap per-draw BLAS keep attachment-aware size queries and now allocate through the VRAM guard in lifetime arena 1.
- Main specialization migration retained; RT_OMM_SCENE remains a define (coherent stamp + optional OMM bindings; S4 rejected).
- rt_omm_dyn is a host-side dispatch/BLAS switch, not a shader define; no new SpecId or shader on/off key is needed.
- Regenerated variants with shader_variant_test --write: 165 variants of 53 bases, delta 0 vs merge target. ABI unchanged.
- CMake build passed; CTests 120/120, 301.11 s (OMM SDK probes enabled).
- pp-diff main f0b0ccbb2733: 251 requests = 57 identical inputs + 149 pp-same + 45 changed; all changes use RT_OMM_SCENE=1.
- Spec sample 8: cascade 44/44, guards 26/26, hitfam 23/23, gifar 12/12, hitinst 23/23, native 61/61, rfgi 12/12, wateredge 9/9.
- All assignments passed (same order): 88, 52, 92, 24, 115, 312, 24, 36; no checker changes.
- Controls (same order): 1, 436, 17,633, 99,045, 52,940, 54,005, 129,768, 7,302 lines; all non-zero.
- Key-off identity (RT_OMM_SCENE absent): 197/197 byte-identical to MERGE_HEAD f0b0ccbb2733; bake/check 2/2 compiled and Vulkan-valid.
- Main advanced during verification to 6e69f90632e0 (now 145 variants); this resolves the existing merge of f0b0ccbb2733 only.
- Latest main shared key-off requests: 155/155 identical; pp-diff 209 = 57 identical inputs + 107 pp-same + 45 changed.
- Logs: omm-merge-*.log. No new shader constant, so no new-key sample-16 gate applies.
- No Git metadata writes or game launch. om4 remains READY_FOR_LIVE; GPU cost and live quality remain unmeasured.
