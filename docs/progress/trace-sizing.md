## Trace window sizing (branch trace-sizing, kept offline, 2026-11-04)

- Trace serialization and capture duration sizing now use the shared capture-stall
  model: maximum of the declared floor, job evidence and current VALID p95.
- Regenerated affected source jobs and combined manifests/plans, including gi1,
  Tilesets and hv9. gi1's four shot-rays windows: 14.1 -> 14.2 s each, above
  the live exclusive minimum of 14.107 s. Cost windows: 2.4 -> 3.3 s;
  pan windows: 4.7 -> 4.9 s. No gates, trace counts or camera paths changed.
- Frozen snapshot changed only trace_capture_stall: p95 2.61315 -> 2.65112 s
  (359 samples). Other frozen derived values remain unchanged.
- Fresh live corpus: 75 VALID logs; active lint 11/32 failing before -> 0/32
  after. Additional gi1, Tileset OFF/ON and hv9 checks: 0/4 failing.
- batch_jobs_lint_test: 7 tests; live_flow_test: 69 tests; queue_gates_test:
  all checks and 8 unit tests pass. Added serialized measured-stall/reload coverage.
- Offline preparation only. No game launch, queue staging, Git metadata writes
  or generated corpus files added to Git. Rendering quality and GPU cost unchanged.

## hv9 and ts1 runner repairs (branch trace-sizing, kept offline, 2026-11-04)

- hv9 refusal: timed return-camera declared frames=0, rejected by the runner.
  Omit frame fields there; shared sizing retains five reset-aligned 60-frame
  windows (stats_reset=true, settle_frames=0, one period). Regenerated plan 66.6 s.
- Both ts1 startup states now use dev=1 to preserve the hidden Tilesets choice.
  Release Ultra values otherwise retained; existing cfg-key expectations require
  rt_native_tileset=0 OFF and 1 ON. Regenerated both jobs/plans and input hashes.
- Live-corpus lint: 75 VALID logs, active 32/32 and targeted hv9/ts1 3/3 pass.
  batch_jobs_lint_test: 7 tests pass; queue_gates_test: all checks and 8 tests pass.
- No game launch, queue staging, commit or Git metadata writes. Live confirmation
  remains pending; no rendering quality or GPU cost claim.
