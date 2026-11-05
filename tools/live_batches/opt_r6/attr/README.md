# READY_FOR_LIVE ATTRIBUTION

Offline preparation only. Explicit live authorization is required before staging.
Two jobs, one launch each: `jobs/opt-attr-inn.json` and `jobs/opt-attr-s8.json`.
Regenerate with `python tools/live_batches/opt_r6/attr/make_job.py`.
Optional `--profile-exe` uses `observation_state(exe, 'ultra')`; otherwise the
compact-hit frozen Ultra observation profile is reused. Both use `dev=1`.

Each variant is warmed before measurement, with fresh QUIET after settings
acknowledgement. Measured static windows are 360 frames, reset-aligned, with
three complete 120-frame periods required for each of eight timers.
Sequence: base-a, sun-off, base-b, direct-off, base-c, ao-off, base-d,
near-only, base-e, base-f. OMM is skipped; base-f remains a final drift receipt.
Restore returns all four keys to Ultra baseline. No captures are requested.

## Live-key audit

| Key | Runtime implementation | Diagnostic meaning |
|---|---|---|
| `gi_hit_sun` | `core/gi.cpp:2671`, dispatch selection | Hit sun off also disables far bounce, which requires sun. |
| `gi_hit_direct` | `core/gi.cpp:3117`, per-frame dispatch gate | Off bypasses the entire hit-material GI dispatch, including hit sun, AO, emission and far bounce. `gi_hit_sun=0` is explicit but redundant. |
| `hit_ao_rays` | `core/gi.cpp:2226`, push constant | Zero AO rays at native hits; no pipeline change. |
| `gi_far` | `core/gi.cpp:1194,1235,2702`, specialization / radius | Near-only retains hit sun and direct; fresh QUIET covers pipeline selection. |
| `rt_omm` | `common/settings.h:917`, restart list; `layer/omm_backend.cpp:91`, device setup | Skipped. `layer/omm_content.cpp:162` also invalidates content proof until restart when switched off at runtime. A return baseline would be confounded. |

The combined GUI hidden-surface choice includes restart-only capture keys;
these jobs change only the audited live raw keys with capture already enabled.
The separate sun trace timer measures screen-receiver shadows, not hit sun.
Origin and sun trace log average dispatch costs; GI trace logs a GPU median.
Analysis takes the median of each window's three logged values without mixing
those producer definitions or subtracting unrelated pass summary medians.

## Receipts and limits

The post command writes `{log}.attr.md`: all window receipts, and each variant
minus the mean of its two adjacent baselines for all eight timers. Signed drift
is following baseline minus preceding baseline. Only missing, insufficient or
nonfinite evidence makes analysis exit nonzero. Image and energy changes are
expected and allowed. Nothing ships, and ablation deltas must not be summed.

At 30 rendered fps, including 120 s load: inn 277.5 s (4.63 min), S8 274.5 s
(4.58 min). Both declare a 300 s timeout and a five-minute launch cap.
Readiness is conditional and unproven. The conservative frozen FPS history
predicts 464.42 s at inn and 294.74 s at S8, so completion within the cap is
not guaranteed; a slow/occluded run must end INVALID, never extend the launch.
GPU ms remain the observable in a covered `visible_unfocused_fg` window.

Offline validation:

```
python tests/attr_jobs_test.py
python tools/live_batches/live_flow.py tools/live_batches/opt_r6/attr --frozen
python tools/live_batches/live_flow.py tools/live_batches/opt_r6/attr
```

Frozen readiness is extended additively; no existing FPS, regex or readiness
receipt is refreshed. No shader changes or variant requests are introduced.
