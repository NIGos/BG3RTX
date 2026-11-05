# Origin pass exact optimization A/B draft

Prepared offline only. No game launch or queue staging is authorized.

`python tools/live_batches/opt_r2/origin/make_job.py` regenerates this draft.
`python tools/live_batches/live_flow.py tools/live_batches/opt_r2/origin --frozen --report tools/live_batches/opt_r2/origin/lint.json` lints it without launching.

One inn launch, Ultra release profile, `dev=1`, `detailed_logging=1`, static camera.
Both specializations warm before the measured OFF / ON / OFF / ON windows.
Each 180-frame cost window requires two complete 60-frame timer periods; the
third period leaves a readback margin. Float pre-post
energy and rendered K8 QA captures run in separate phases, outside cost windows.
Restore disables `rt_origin_lazy_normal` and all capture/exposure controls.

`analyze.py` fails if either ON window does not improve origin GPU ms beyond
the two OFF medians' spread. Every ON RGB/luminance float mean must fit the
OFF-repeat Student prediction band. Two OFF replicates give one degree of
freedom; the report states this low-power limitation. Each ON K8 rendered
burst passes the existing QA temporal detector versus both OFF bursts, with
animated geometry excluded and camera drift rejected. Missing logs, exports,
QA transport or acknowledged windows invalidate the result.

The frozen lint snapshot has only additive readiness queries. New-key
transitions are recorded as unproven (zero historical samples), never measured
zero latency. The plan waits for settings acknowledgement, origin pipeline
completion/build reconciliation and fresh quiet driver state before sampling.
Expected phase duration is about 146 s after load; 300 s watchdog, no raw float
or PNG dump images, four rendered half-grid edge bursts. Target disk below 2 GB.
No GPU savings or live quality result is claimed until this named batch is authorized and run.
