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

## Missed water cascade migration (branch scc1-regate, offline candidate, 2026-11-03)

- Compose, reflect and glint prepass share fixed WATER_SPEC_CASCADE requests; existing SpecId 1000
  selects both cascade states. Sun-hit state is conditional on that constant. Edge 1014 coexists.
- Pipeline keys include constant values; glint/compose keys stay aligned. No ABI or preset change.
- Regenerated variants: 165 -> 145 (-20). pp-diff vs main: 209/209 unchanged requests agree
  (206 identical inputs + 3 preprocessed-identical), zero compiles needed.
- watercasc sample 16: 19/19 requests, 66/66 comparisons, control 5,862 lines.
- cascade sample 8: 44/44 requests, 88/88 comparisons, control 1 line.
- wateredge sample 8: all 7/7 requests, 28/28 comparisons, control 7,316 lines.
- Build passed; all 117 CTests passed across the full run and targeted reruns. Fixed the progress
  file's existing BOM; normalized an unchanged recording header's CRLF checkout to repository LF.
- Audit only: rt_water_rr_hitdist -> WATER_RR_HIT; rt_water_glint_aa -> WATER_GLINT_AA remain.
  Water shadow/LS/refraction/game-shadow defines follow captured record layouts. GI retains
  rt_omm -> RT_OMM_SCENE, generated pool rows -> RT_HIT_POOL_2S (capabilities), and
  rt_sun_cascade_current -> RT_SUN_CURRENT plus cascade defines (separate layout).
- No game launch or git metadata writes. Live zero-DXC, energy and GPU ms remain READY_FOR_LIVE;
  check by toggling rt_cascade_casters in the prepared SCC1 scene, after explicit batch steering.
