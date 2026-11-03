# smoke_ultra: qualified Ultra (READY_FOR_LIVE, not authorized)

One job, `su1-ultra-s8-fg2`, S8 Water Queens House, one launch, 11 phases, 211 s
(about 5.5 min with load). The 45 s initial warmup allows new-build shader prefetch. No game is launched by the offline dryrun.

- `preset=ultra` supplies qualified box shadows (`ls_box=2`, default
  `ls_box_honour_shadow=1`; ba1 S6 user approval 2026-11-03, +0.13 ms) and OMM (`rt_omm=1`, `rt_omm_dyn=1`, `rt_vt_alpha_masks=1`,
  `rt_alpha_fine_lod=1`, reference off), water and the capture budgets. Box shadows
  use the preset and retain the default art-intent correction.
- Static, pan Q/E and rotation windows retain zero opaque/cascade capture drops,
  scene-wait timeouts 0, water traced and composed every frame, no non-finite
  compose terms and VRAM within the real driver budget. RT and GI cost lines are required.
- After the motion windows: `diag-warm-omm` 25 s warms the statistics pipelines,
  then `diag-omm` 15 s gates compared > 0, not compared 0, OMM violations 0 and
  scene-wait timeouts 0 (60-frame log cadence, 100 ms diagnostic wait budget).
  Cost windows keep diagnostics off and the normal 12 ms wait budget.
- `ab-off` / `ab-on` finish the run and gate toggle latency. Missing shader variants,
  pool overflows, roof warnings, invalid reads, unavailable OMM and bake failures fail the run.
- VRAM guard, compaction and elastic capture use their automatic defaults, without
  a synthetic budget override. Opaque capture drop tolerance remains 0.00.
- Offline: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/smoke_ultra/dryrun.ps1`.
