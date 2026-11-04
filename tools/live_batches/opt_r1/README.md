# Scene round 1

Prepared only. Do not copy these jobs into a watched inbox without explicit live authorization.
The retained compact-holds candidate has one Inn launch, hard capped at 300 s, `visible_unfocused_fg`, detailed logging,
and `dev=1` so its developer-only key can actually toggle. Both key states warm before measurement.
The compact-holds launch initializes the Ultra device layout, warms High, checks Ultra ON briefly,
then returns to High for the measured comparison. Rejected candidates have no unused jobs or keys.

OFF-a, OFF-b, ON and OFF-c each have separate GPU, float pre-post energy and rendered K8 QA windows.
GPU windows reset statistics at motion-ready. All touched producers use `stats_period()` and support
the 60-frame override (`common/stats_window.h`): core GI/trace/LS/RF, BVH and cascade.
Each measured window requires two complete 60-frame receipts. Capture/readback work is outside costs.
The analyzer checks every window, rejects missing receipts, derives RGB/luminance energy bands from
the three OFF repeats, and applies the existing QA temporal detector against every OFF burst.
It also rejects camera drift, unresolved flicker, savings below 0.05 ms or OFF-repeat spread, and
combined scene-build plus ray-pass regressions. It never infers presented FPS from an occluded window.

Generate with `python tools/live_batches/opt_r1/scene_jobs.py --results <main>/captures/queue/results.jsonl`.
Lint using actual
recent VALID evidence when available, because new transitions have no frozen readiness estimates:

```
python tools/live_batches/live_flow.py tools/live_batches/opt_r1 --results <main>/captures/queue/results.jsonl --newest 4 --cache NUL --report .scratch/opt-scene-live-flow-report.json
```

`NUL` prevents the corpus cache becoming a file larger than 20 MB. The small report is retained.
Actual evidence lint passes with 0 failures (157.72 s after load; about 4.63 minutes with 120 s load).
Frozen lint was attempted separately: the checked-in snapshot lacks the new transitions, a tooling
limitation rather than a failure of the job checked against current VALID logs. New-key readiness
history remains unproven. Jobs fail fast on their readiness timeouts and post-analysis gates.
Offline proof and the final build pass: 175/175 tests plus the newly registered analyzer test
(12 cases). The CPU dyn_hold fixture checks bit-preserving position/validity/UV packing and
native recipe/layout gates. No shaders change; request delta 0. The retained prediction is
`READY_FOR_LIVE`; generation conservatively restores `OFFLINE_PROOF_PENDING` until those checks
are reviewed again. No live result, measured saving or quality pass is claimed.
