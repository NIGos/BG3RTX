# Temporal post candidate

Prepared outside watched inboxes. Never launch the game or queue these jobs without
fresh user steering. Keep rr_post_temporal off in presets.

Dialogue3 now uses 10 adjacent, non-overlapping pairs, alternating pair order:
OFF ON / ON OFF, repeated five times. This balances smooth time-order drift and
puts the two states within seconds of each other. It cannot guarantee identical
animation phases or cancel arbitrary periodic/serially correlated motion. Registration
and cuts are still checked with the existing edge_flicker functions.

Every window settles, captures rendered K8, separately captures presented K8,
and records one float energy trace. All 10 registered OFF windows freeze masks
before any ON measurement. No candidate-derived masks or threshold retuning.
K8 retains 36 submitted samples: depth/final/AC/EB each frame, material/albedo
at both ends. AC is C.rgb/A.a; EB is bloom.rgb/fresh exposure.a. Decode formats
97/109 as before. Subtitle exclusion and registration rejection stay in force.

Durations use live_flow's same-save corpus FPS (currently 25.685 app FPS).
The failed 004032 run issued five traces with request-to-written latencies
0.081-0.097 s, then issued no request in energy-on-02. That nominal 2.1 s
window lasted 3.803 s. The synchronous guard runs before phase scheduling;
round this observed missed dispatch interval up to 4 s. The written marker
precedes dumps_finish, so it cannot measure the entire export stall.

The current VALID corpus has 439 matched request-to-written samples: p95
0.317 s, max 0.437 s. live_flow persists that nearest-rank p95 and carries
previous trace completion across phases. Each energy window holds
max(trace_at, settings latency, previous export remaining) + 4 s dispatch +
max(2/FPS + 1 s write stall, corpus p95), rounded exclusively to 0.1 s: 6.1 s.
All energy traces and every-window gates remain mandatory. The old 2.1 s
window fails offline lint. Timing captures remain separate.

OFF/ON quality windows take 14.2/17.2 s (31.4 s per pair). Fixed phases take
70.9 s. The plan is 384.9 s + 360 s readiness + 120 s loading = 864.9 s.
The largest balanced even pair count below 900 s is 10; 12 would take 927.7 s.
The statistical minimum remains 8; no gate or family alpha is relaxed.

For hair-over-DoF and bright contours, compute ON minus OFF of the same residual
per adjacent pair. Apply the exact one-sided sign test to total/even/odd residuals
in rendered/presented domains: 12 predeclared observables, Bonferroni alpha 0.05/12.
Ties count against improvement. At least ceil(log2(12/0.05)) = 8 pairs are necessary;
10 pairs require all ten improvements (p = 1/1024); one non-improving
pair fails (p = 11/1024). There is no hand-selected
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

The revised one-off Dialogue3 pack exceeds 2 GiB because inference needs 20 K8
windows. The conservative bound is in predicted.json, including presented PNGs,
float previews and logs. Analyze it before another scene and retain summaries;
this task neither creates captures nor deletes data. S8 retains its four-window
job and still needs independent quality evidence; its prior 0.12 ms is cost only.
The dryrun.ps1 entry point now runs the focused offline checks; the legacy fake
queue harness assumes 5 s minimum phases and is unsuitable for these derived phases.
