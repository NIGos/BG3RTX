## SCC1 offline re-gate (branch scc1-regate, FAIL / incomplete coverage, 2026-11-03)

- Updated job, plan and post-run predictions for always-on constants (165 variants); request delta 0.
- No game, DXC, shader tools or git writes. Original logs and executed plan preserved read-only/exact.
- Updated observed-window gate FAIL: cascade-off at 01:41:24.230 compiles water_compose at 01:41:31.120,
  1640 ms. Missed path: water_request still toggles RT_CASCADE_MASKS / RT_CASCADE_SUN_HIT defines.
- Startup rt_sun_resolve compile: 51 ms. Cascade-off pipelines 31.6-3472.1 ms (startup on 0.2-2.2 ms).
- Energy repeats inside OFF-OFF spread. GI deltas -0.028 / +0.0255 ms exceed 0.013 / 0.007 ms spread;
  total RT c0 +0.01775 ms exceeds 0.0105 ms. RF/LS and RT c1 within spread. No quality pass.
- Retired switch is ignored: historical off/on labels are identical-state repeats, not old/new evidence.
- Extended plan demands zero DXC and pipeline evidence for all requested converted-key toggles.
  Hit/native capabilities are startup-latched; writes alone do not qualify them. Pool2s needs a fixture.
- Full migration remains unqualified. Re-gate script and JSON report are in tools/live_batches/spec_cascade.
