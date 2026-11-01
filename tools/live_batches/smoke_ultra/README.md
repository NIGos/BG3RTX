# smoke_ultra: Ultra with every qualified experimental key (READY_FOR_LIVE, not authorized)

One job, `su1-ultra-s8-fg2`, S8 Water Queens House, one launch, 7 phases, 120 s (about 4 min with the load).

- Launch keys: preset ultra, fg=2, rt_hit_material, rt_hit_material_instanced, gi_hit_direct, gi_hit_sun,
  rt_emissive + rt_hit_emission=1, ls_box=2, rt_xfb_mb=1024, rt_cascade_mb=427 (the water keys come from
  preset=ultra since branch ultra-water-preset: the test tree must hold it),
  rt_cascade_casters=1 (as Ultra sets it).
- Phases: warmup (10 s), static-a (trace), pan Q, pan E, rotate right, rotate left, static-b (trace).
  qa_exposure_hold=1 from static-a on; expect `[rrpost] exposure held`.
- Every-window gates (`expect_every`, scripts/queue_gates.py): water reflections traced > 0 in every window
  but the warmup, and in the same windows queue_gates.CAPACITY_GATES (RT capture drops 0, cascade buffer full 0)
  and SCENE_WAIT_GATE (0 timeouts, rt_scene_wait_log_frames=120).
- Forbidden anywhere in the run (silent degradations, docs/GATE_AUDIT_20261101.md section 8): `skipped: no
  variant for` (a pass without its shader variant), the roof-volume WARNING / `over kSunMaxVolumes on N>0`,
  and a nonzero `set 1 ... out-of-range reads skipped` in the `[core] guard:` line.
- Expected today: FAIL on the water gate (water reflections are skipped while rt_cascade_casters=1). That is
  the point: the job is the regression check for that fix and for the scene-wait fix.
- Offline: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\smoke_ultra\dryrun.ps1`.
