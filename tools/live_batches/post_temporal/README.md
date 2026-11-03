# Temporal post candidate

Prepared outside watched inboxes. Never launch the game or queue these jobs without
fresh user steering. Keep rr_post_temporal off in presets.

Dialogue3 now uses 12 adjacent, non-overlapping pairs, alternating pair order:
OFF ON / ON OFF, repeated six times. This balances smooth time-order drift and
puts the two states within seconds of each other. It cannot guarantee identical
animation phases or cancel arbitrary periodic/serially correlated motion. Registration
and cuts are still checked with the existing edge_flicker functions.

Every window settles, captures rendered K8, separately captures presented K8,
and records one float energy trace. All 12 registered OFF windows freeze masks
before any ON measurement. No candidate-derived masks or threshold retuning.
K8 retains 36 submitted samples: depth/final/AC/EB each frame, material/albedo
at both ends. AC is C.rgb/A.a; EB is bloom.rgb/fresh exposure.a. Decode formats
97/109 as before. Subtitle exclusion and registration rejection stay in force.

Durations come from live_flow.duration_budget, at 30 app FPS with 1 s settings
latency and 1 s write/trace stall. Exclusive bounds round up to the next 0.1 s:

- OFF settle: 32 Halton frames, 1 + 32/30 -> 2.1 s.
- ON settle: ceil(log(1/1024)/log(15/16)) = 108 EMA frames, 1 + 108/30 -> 4.7 s.
- Rendered K8: 1 + 0.1 capture delay + (9+3)/30 + 1 -> 2.6 s.
- Presented K8: FG status gate dominates transport: 1 latency + 1 settle + 30/30 -> 3.1 s.
- Energy trace: max(1, latency) + 2/30 + 1 -> 2.1 s.

OFF/ON windows are 9.9/12.5 s; quality captures total 268.8 s. Warmups, separate
OFF/ON cost phases (11.1 s each plus settles), and restoration bring the plan to
338.9 s. Including 360 s maximum readiness waits and 120 s loading gives 818.9 s
(13.65 min), below the 900 s timeout/cap. No timing readbacks overlap captures.

For hair-over-DoF and bright contours, compute ON minus OFF of the same residual
per adjacent pair. Apply the exact one-sided sign test to total/even/odd residuals
in rendered/presented domains: 12 predeclared observables, Bonferroni alpha 0.05/12.
Ties count against improvement. At least ceil(log2(12/0.05)) = 8 pairs are necessary;
12 pairs allow one non-improving pair (p = 13/4096). There is no hand-selected
residual amplitude threshold. OFF-repeat spread remains a reported noise diagnostic;
the paired repeat signs determine uncertainty. K8 frames/pixels are not replicates.
Every observable must pass. Empty control support, failed registration, darker
float energy, or missing S8 evidence still block qualification.

The retained pt-dialogue3.1.210703 summary has only two pairs. Its new result is
INSUFFICIENT (insufficient alternations), independent of the sign of its effect.
The historical RESULT_20261103_D3_valid files remain unchanged.

```powershell
python -m tools.live_batches.post_temporal.make_job --scene dialogue3
python tools/live_batches/live_flow.py tools/live_batches/post_temporal/jobs/pt-dialogue3.json --preflight
python tests/post_temporal_pair_test.py
python tools/live_batches/post_temporal/analyze_d3.py --summary tools/live_batches/post_temporal/RESULT_20261103_D3_valid.json --out tools/live_batches/post_temporal/RESULT_20261103_D3_paired.json
```

For new raw analysis, supply --log, --runner, --plan (the exact plan used in that
run) and --out. It never defaults to an old run with a changed plan. The existing
raws were already deleted after analysis; summary re-gating reads no raw files.
Float energy uses joined rrpost slots 1/2/6 and dumps.json means, never PNG energy.
Fixed background support requires trace registration. Face non-regression and
energy tolerances still come from all OFF repeats.

The revised one-off Dialogue3 pack exceeds 2 GiB because inference needs 24 K8
windows. The conservative bound is in predicted.json, including presented PNGs,
float previews and logs. Analyze it before another scene and retain summaries;
this task neither creates captures nor deletes data. S8 retains its four-window
job and still needs independent quality evidence; its prior 0.12 ms is cost only.
The dryrun.ps1 entry point now runs the focused offline checks; the legacy fake
queue harness assumes 5 s minimum phases and is unsuitable for these derived phases.
