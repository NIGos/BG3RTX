## Readiness: a selected hit-GI raygen pipeline resolves a superseded raygen build (branch claude/rel-gate5, READY_FOR_LIVE, 2026-11-21)
- final-s8-ultra-high.1.040347 timed out at ab-on again with 180 s. Cause (log lines 6320-6330): at ab_off 1 -> 0 the
  first frame has no emission list, so core starts the sun raygen build (GI_TRACE_RAYGEN=1, "the pass waits") and runs
  the trace as compute; 34 ms later it selects the resident emission raygen pipeline ("runs as a ray tracing pipeline,
  emission variant"). The sun build is superseded and never logs "generation", so the runner waited for it forever.
- Fix (runner scripts/camera-plan.ps1 Get-CameraPlanMissing and its offline mirror readiness_budget.estimate): the line
  "gi: gi_rt_pipeline: the hit-GI trace runs as a ray tracing pipeline" (either variant) resolves the pending
  gi_trace_rt_hit_sun[_em].hlsl ...GI_TRACE_RAYGEN=1 builds started before it. Other builds, and raygen builds started
  after the selection, still need their completion.
- The ab-on and High settings-reset allowance stays what the preflight computes (180 s, no VALID median yet).
- Tests: camera_plan_test.ps1 (+4, the exact log lines), live_flow_test (+1), release_qual_test, batch_jobs_lint_test,
  camera_plan_fields_test, hair_reload_test. Lint without and with --frozen: release_final, r3_core, r4_hitch 0 failing.
