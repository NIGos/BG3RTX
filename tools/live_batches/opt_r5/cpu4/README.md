# CPU pre-draw optimization 4

Prepared offline only. No queue staging or game launch. `cpu_opt4=0` is layer only;
`cpu_opt1=1`, `cpu_opt2=1` and qualified `cpu_opt3=1` stay fixed in all windows.
ABI unchanged, shader request delta 0. The copied real baseline retains
`perf_hook_state=0` and `rt_alpha_stream=1`: persistent matrix lookup slots apply
only with `perf_hook_state=1`, and the alpha address bypass only to non-stream
reads. Neither is part of the saving prediction for this job. Descriptor receipts
still cover the material, alpha and cascade queries in this job.

One inn Ultra run uses OFF/ON/OFF/ON with 600 rendered frames per window.
Ready phases wait only for a fresh zero-pipeline watchdog. Normal runner settings
acknowledgements remain enabled. Every measured window needs submit, capture,
capture detail, hit material and BVH CPU receipts; the post analyzer rejects
missing timer periods. Compare both ON windows against both OFF windows and the
OFF repeat spread before claiming savings. The analyzer returns failure unless
each ON pre-draw and hook-body median beats both OFF medians by more than their
measured repeat spread (KEPT_CPU_COST); noise or regression is NO_SAVING. These are CPU gates; full rendered
quality and energy equivalence remain unmeasured.

Generate: `python tools/live_batches/opt_r5/cpu4/make_job.py`.
Lint: `python tools/live_batches/live_flow.py tools/live_batches/opt_r5/cpu4 --frozen --report tools/live_batches/opt_r5/cpu4/lint.json`.

Baseline CPU3 qualification: inn Ultra capture total -0.24 ms/frame vs OFF,
OFF repeat spread 0.054 ms/frame (`captures/queue/logs/opt-cpu3-inn.1.055551.log.cpu.md`).
CPU4 has no live result. The job is READY_FOR_LIVE only after dependent offline checks pass.
