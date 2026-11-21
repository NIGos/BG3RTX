## Release smoke: readiness waits derived per phase (branch claude/rel-gate4, READY_FOR_LIVE, 2026-11-21)
- final-s8-ultra-high.1.033318 failed at b1-...--ab-on: "pipeline readiness timed out ... gi_trace_rt_hit_sun".
  ab_off 1 -> 0 rebuilds the GI sun raygen pipeline in 11.9-12.6 s in the driver (runs 014227, 01:43, 033318);
  make_final.py gave every non-first readiness wait a fixed 10 s. The Ultra windows before ab-off passed (no
  fail-fast; compose stability uncomposed 0, water traced, 0 scene-wait timeouts).
- make_final.py `readiness_timeouts`: startup 60 s; a phase that changes a key of pipeline_readiness.TABLE (not a
  CONTROL key; ab_off=1 records no RT pass) gets readiness_budget.timeouts' allowance: the learned VALID median plus
  margin, else the unproven pipeline allowance (pipeline_readiness.PIPELINE_TIMEOUT_S, 180 s, the value apply()
  already used); 10 s only where no pipeline key changes. S8: ab-on and High settings-reset 180 s, the rest 10 s;
  watchdog 694 s (12 min, under the 15-minute launch rule); Dialogue1 unchanged (60 s startup, 300 s).
- Lint without and with --frozen: release_final 5, r3_core 2, r4_hitch 1 jobs, 0 failing. Tests: release_qual_test
  (+1: rebuild phases > 12.6 s, ab-off and startup unchanged), live_flow_test, batch_jobs_lint_test. No C++ change.
