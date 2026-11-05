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

## Cached GI readiness fix (branch attr, kept offline, 2026-11-05)

- GI trace/query TABLE entries use QUIET only. Queue delegates readiness to
  auto-run's camera-plan helper; that helper was inventing generation requirements
  from background starts even for QUIET-only plans. It now requires fresh QUIET
  after matching cfg acknowledgement. Offline readiness estimates use the same rule.
- The failed inn warmup-sun-off log now passes offline replay without generation
  records. Missing/stale QUIET and wrong settings still fail regression checks.
- Regenerated both attr jobs: no content delta. Shader request delta 0.
- Tests: 102 Python tests and 692 PowerShell checks pass. Attr frozen/current
  lint: 2/2 pass; active frozen: 17/17 pass. Active current: 5/17 fail with 34
  existing trace-timing findings, identical to original HEAD; 0 new failures.
- Reports: `.scratch/attr-readiness/`. READY_FOR_LIVE only; no game, queue or git writes.

## Unconfounded far / sun batch (offline, 2026-11-05)

- S8 prior run VALID: GI trace 8.0 -> 2.1 ms sun-off, 1.5 ms direct-off;
  sun-off includes far removal. New sequence: base-a, near-only, base-b, sun-off,
  base-c in both scenes; near-only and sun-off prewarmed first.
- Inn failed at ready-base-a: watch 8 pipelines at 17:38:52.435 / 56.584;
  earlier global QUIET still had 3 GI builds pending. Specific GI burst completed
  at 17:38:57.358 (6 builds, 35534 ms). Use fresh GI idle period or burst done,
  with cfg acknowledgement; keep unrelated keys' global QUIET contracts.
- Nominal including 120 s load 230.4 / 227.4 s; strict launch cap 299 s.
  Completion remains unproven; conservative frozen floors exceed the inn cap.
- Validation: attr 5 tests, live-flow 74 tests, camera-plan 692 checks pass;
  frozen/current attr lint 2/2 pass. No game, queue writes or git writes;
  shader request delta 0.
