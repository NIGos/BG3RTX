## Staged jobs pass the runner preflight (branch claude/rel-gate3, READY_FOR_LIVE, 2026-11-21)
- Session combined-e-20261121e refused three jobs at preflight (.scratch/coord/combined_e_runner.log).
- release_final energy-static: the generator sized windows with the frozen snapshot's models; the preflight lints
  with the live VALID corpus. The difference is the fps model, not the stall (live p95 stall 2.79 s, under the
  declared 5.18 s): Dialogue1 p10 fps 10.0 snapshot against 7.1 live, so energy-static was 6.40 s against a 6.46 s
  minimum. The staged generator now sizes with `flow.tighten_files` on the live corpus (the preflight's model).
- With the live S8 model (8.3 fps against 20) final-s8-ultra-high plans 188 s plus a 130 s readiness allowance,
  over the fixed 300 s cap. make_final.py `watchdog` keeps 300 s as the floor and raises it to the preflight's
  own estimate (plan + readiness + 120 s load): 438 s, max_minutes 8; Dialogue1 stays 300 s. The real S8 rate in
  run 014936 was about 30 fps, so this is a watchdog, not the expected duration.
- r3-d3-jitter: forbid `nonfinite` matched the normal "non-finite terms 0 (... kCsNonFinite)" water line. It now
  uses `live_flow.NONFINITE_EVENT` (a count above 0), the pattern repair_known_forbids already used, now one
  shared constant. r3-s8-sun-overlap and r4-inn had no such forbid.
- Lint without --frozen (the preflight): release_final 5 jobs, r3_core 2, r4_hitch 1, all 0 failing; --frozen too.
- Tests: release_qual_test, live_flow_test, batch_jobs_lint_test, build.ps1 -Test.
