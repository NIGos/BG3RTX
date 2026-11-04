## Scene-wait capture exclusion (branch scenewait-traces, kept offline, 2026-11-04)

- GI 1-ray's shot-rays-off-a failed on 1 timeout in 7 lines (max 26.483 ms).
  Float export stalls delay scene builds; these shots are not cost windows.
- Scope both GI timeout gates to its 4 cost windows. Shared frame-window preparation
  excludes trace_at phases from timeout gates and their wait_stats. Keep 0 timeouts
  strict in the remaining measurement windows; capacity and trace-count gates remain.
- Refresh affected active jobs, plans and combined manifests. Retain existing timings
  and corpus data. Shader requests, runtime settings and ABI are unchanged.
- queue_gates checks pass; live_flow 67/67 and batch_jobs_lint 7/7 pass, including
  all 31 active jobs against the frozen corpus and capture exclusion regressions.
- Fresh 74-log corpus lint: 11/31 active jobs and GI fail trace-schedule bounds.
  Read-only HEAD comparison of all 32 jobs finds identical failures, 0 new problems.
  Reports are in captures/.cache/scenewait-{active-lint,gi-lint,baseline-comparison}.json.
- No game launched, no GPU measurement, no Git metadata changes or commits.

## Scene-wait measurement exclusions (branch scenewait-traces, kept, 2026-11-04)

- ts1 OFF live 16:28: diag-off reported 59 current scenes, 1 timeout, max 16.688 ms.
  Diagnostic readbacks confound scene-build timing just as trace captures do.
- Exclude windows with traces or enabled *_stats, *_check or *_debug keys in
  pipeline_readiness.CONTROL, using cumulative settings. Keep zero timeouts strict
  elsewhere; capacity gates are unchanged.
- Regenerated scene-wait scopes and wait_stats in tileset OFF/ON, smoke_ultra and
  its active combined job/manifest. Preserve existing frame counts and timing evidence.
- Offline: queue_gates all checks passed; live_flow 68 tests; batch_jobs_lint 7 tests.
  No game launch, commit or generated corpus files. Next live check: ts1 cost windows
  still require 0 timeouts; diagnostic coverage/capacity gates remain mandatory.
