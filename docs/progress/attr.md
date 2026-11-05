## Ultra GI attribution batch (branch attr, READY_FOR_LIVE, 2026-11-05)

- Prepared `tools/live_batches/opt_r6/attr`: inn and S8, one launch each, dev Ultra,
  ten static 360-frame windows, three reset-aligned periods for eight GPU timers.
- Live: gi_hit_sun, gi_hit_direct, hit_ao_rays, gi_far. OMM skipped: restart-only,
  and runtime off invalidates content proof until restart. Direct-off also disables
  hit sun, AO, emission and far GI; sun-off also disables far GI.
- Diagnostic only: expected image changes, no shipping/energy/saving gate. Markdown
  reports variant minus adjacent baseline mean and signed baseline drift.
- Nominal including 120 s load: inn 277.5 s, S8 274.5 s; 300 s cap. Conservative
  frozen history predicts 464.42 / 294.74 s: slow completion remains unproven.
- Four offline unit tests pass; frozen and non-frozen live-flow lint pass, 2 jobs,
  0 failing. Readiness estimates remain unproven. Shader request delta 0.
- No launch, queue writes or git writes. Coordinator owns commit and any live staging.
