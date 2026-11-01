## Capacity gates in every lean job (branch capacity-gates, offline, 2026-11-01)
- 30 jobs in 12 batches gained the per-window capacity gates of `queue_gates.CAPACITY_GATES` over their traced cost
  windows: `[geo] RT capture` opaque draws dropped 0.00 (all), `[cascade] casters` buffer full 0.0 (windows with
  rt_cascade_casters=1: 17 jobs), plus the patterns that keep those lines. All set rt_timing_frames=120.
- `--lint-capacity` is now a check of every lean dryrun (tools/live_batches/lean_dryrun.ps1, after the scene-wait
  lint) and of CTest queue_gates_test (the whole tools/live_batches tree). Lints ok (capacity, scene wait, exposure
  hold); the 12 affected dryruns pass.
