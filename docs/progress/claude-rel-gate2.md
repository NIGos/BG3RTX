## Release smoke final-s8-ultra-high: two test defects fixed, staged jobs regenerated (branch claude/rel-gate2, READY_FOR_LIVE, 2026-11-21)
- Run final-s8-ultra-high.1.014936 failed twice for test defects, not render defects.
- High half hang: rel2-smoke-high-s8 is a deepcopy of the Ultra smoke and kept the water reflections, compose
  stability, non-finite-by-water and cascade casters gates. Today's High profile exports rt_water_reflections=0,
  rt_water_compose=0, rt_cascade_casters=0, so those lines never come; live_flow turns gates into wait_stats and
  b2-...--static-a waited until the 300 s job cap. Fix: release_qual/make_jobs.py `PRODUCERS` +
  `drop_unproduced` keep a gate only in windows whose exported state has its producer keys on (core/water.cpp
  water_rf_wanted/compose_wanted, the cascade casters pass); make_final.py applies it to every job before
  `qualify`. High drops exactly those 4 gates; Ultra and Dialogue keep all of theirs.
- Runner: a frame window whose stats lines never come had no timeout after the frames completed (wait_log_timeout
  covers readiness only). It now ends at max(10 s, wait_log_timeout) after the frame target with
  `[stats-wait] timed out phase=.. label=.. missing: ..` and result `stats wait timed out: <label>`
  (scripts/camera-plan.ps1 Test-CameraPlanStatsTimedOut, Get-CameraPlanStatsMissing; auto-run.ps1).
- Ultra fail-fast: the first `[water] compose stability (60 frames)` line of b1-...--static-a counted 114 water
  frames with 1 uncomposed. water_stats_reset (the rt_stats_reset token) reset the compose window but not the
  ComposeContinuity counts, so the line straddled the previous energy-static phase (rt_stats=0, float trace
  stall). Fix in core/water.cpp: the token also resets those counts (continuity kept, so a flip right after the
  reset still counts). Stats only, no image change, no key. The gate is unchanged: a real uncomposed frame inside
  a window still fails.
- Regenerated tools/live_batches_staged/release_final from today's profile (new off-by-default keys in the set);
  `--extend-snapshot` appended 3 readiness queries to lint_corpus_snapshot.json.
- Tests: release_qual_test (+2: drop rule, staged job has no gate or wait_stats without a producer; the second
  fails on the old staged job), live_flow_test, batch_jobs_lint_test, camera_plan_test.ps1 (+9),
  camera_plan_fields_test, hair_reload_test, build.ps1 -Test. Lint `--frozen`: 5 jobs, 0 failing.
- Left: the non-frozen lint reports the energy-static trace schedule on main as well (unchanged by this
  branch); readiness_cached_replay.ps1 needs old logs under Documents (environment). Needs a live go to rerun.
