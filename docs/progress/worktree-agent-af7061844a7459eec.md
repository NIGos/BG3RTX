## rt_omm qualification prepared: observables + omm_qual camp job (branch worktree-agent-af7061844a7459eec, READY_FOR_LIVE, 2026-11-02)
- Re-check on main 566db3d (ABI 25): both DLLs build, CTests pass, OMM variants compile, key-off shaders
  byte-identical. Nothing was broken; no ABI change.
- New observables (all behind rt_omm=1, off by default): layer `[omm] last N builds` (alpha-tested triangles
  per build, with a capture, with attached micromaps and their %, variants, bytes, cumulative baked
  microtriangle states from a device-local bake counter); core `omm check` (rt_stats=1, rtStats 435..442:
  same-ray visibility / closest-hit compare against the reference scene, violations, candidates per trace);
  live key `rt_omm_reference` (OMM variants trace the reference scene, guard kept) for an in-launch A/B.
- Job `tools/live_batches/omm_qual`: om1 camp rt_omm=1 (diag-check, cost REF / ON / REF, 3 PNG), om2 camp
  rt_omm=0 baseline (cost OFF x2, 1 PNG). About 7.2 min, 0.26 GB. Dry run and lints pass. Not authorized,
  not run. predicted.json: energy null, violations 0, coverage about 0, cost null (sign only).
- Expectation: reject. Only one plain-texture recipe can bake (no VT foliage), and a known state needs
  alpha 1 at every mip down to 1x1, so cutout triangles stay UNKNOWN. When ranges are attached, the guard
  makes five per-lane host-visible atomics per ray query.
- Docs: docs/P4_OMM.md section 1 and 1a.
