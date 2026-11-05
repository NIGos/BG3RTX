# READY_FOR_LIVE ATTRIBUTION

Offline preparation only. Explicit live authorization is required before staging.
Two jobs, one launch each: `jobs/opt-attr-inn.json` and `jobs/opt-attr-s8.json`.
Regenerate with `python tools/live_batches/opt_r6/attr/make_job.py`.
Optional `--profile-exe` uses `observation_state(exe, 'ultra')`; otherwise the
compact-hit frozen Ultra observation profile is reused. Both use `dev=1`.

Prewarm near-only (`gi_far=0`, sun on), sun-off, then baseline before measuring.
Sequence in both scenes: base-a, near-only, base-b, sun-off, base-c.
Fresh readiness after cfg acknowledgement uses the GI monitor: a full period
with 0 frames waiting, 0 pending, 0 started, or its burst-completion line.
Prewarm timeout is 60 s; other readiness timeouts are 15 s. Five static windows
have 480 frames, reset alignment and three complete periods for eight timers.
Direct and AO stay on. OMM is skipped. Restore returns all keys to baseline.
No captures are requested.

Inn evidence (`opt-attr-inn.1.173641`): ready-base-a started 17:38:49.587;
watch at 17:38:52.435 and 17:38:56.584 reported 8 pipelines, 0 ms in driver.
The previous global QUIET at 17:38:48.262 coexisted with 3 pending GI builds.
GI completion at 17:38:57.358 reports 6 builds in 35534 ms. Thus global
pipeline creation is neither necessary nor sufficient for GI readiness here.
The runner now accepts the specific GI quiet witness, fresh after matching cfg.

## Live-key audit

| Key | Runtime implementation | Diagnostic meaning |
|---|---|---|
| `gi_hit_sun` | `core/gi.cpp:2671`, dispatch selection | Hit sun off also disables far bounce, which requires sun. |
| `gi_hit_direct` | `core/gi.cpp:3117`, per-frame dispatch gate | Off bypasses the entire hit-material GI dispatch, including hit sun, AO, emission and far bounce. `gi_hit_sun=0` is explicit but redundant. |
| `hit_ao_rays` | `core/gi.cpp:2226`, push constant | Held at Ultra baseline, 2. |
| `gi_far` | `core/gi.cpp:1194,1235,2702`, specialization / radius | Near-only disables far bounce with hit sun still on. |
| `rt_omm` | `common/settings.h:917`, restart list; `layer/omm_backend.cpp:91`, device setup | Skipped. `layer/omm_content.cpp:162` also invalidates content proof until restart when switched off at runtime. A return baseline would be confounded. |

The combined GUI hidden-surface choice includes restart-only capture keys;
these jobs change only the audited live raw keys with capture already enabled.
The separate sun trace timer measures screen-receiver shadows, not hit sun.
Origin and sun trace log average dispatch costs; GI trace logs a GPU median.
Analysis takes the median of each window's logged values (at least three) without mixing
those producer definitions or subtracting unrelated pass summary medians.

## Receipts and limits

The post command writes `{log}.attr.md`: all window receipts, and each variant
minus the mean of its two adjacent baselines for all eight timers. Signed drift
is following baseline minus preceding baseline. Only missing, insufficient or
nonfinite evidence makes analysis exit nonzero. Image and energy changes are
expected and allowed. Nothing ships, and ablation deltas must not be summed.

At 30 rendered fps, including 120 s load: inn 230.4 s, S8 227.4 s,
before conditional readiness. Both launches have a strict 299 s cap including
load. Cold compilation or slow frames can still make a run INVALID; completion
is unproven offline. Frozen history predicts 353.04 / 294.48 s with periodic
readiness floors, so the cap is a deadline, not a promise of completed evidence.
GPU ms remain the observable in a covered visible_unfocused_fg window.

Offline validation:

```
python tests/attr_jobs_test.py
python tools/live_batches/live_flow.py tools/live_batches/opt_r6/attr --frozen
python tools/live_batches/live_flow.py tools/live_batches/opt_r6/attr
```

Frozen readiness is extended additively; no existing FPS, regex or readiness
receipt is refreshed. No shader changes or variant requests are introduced.
