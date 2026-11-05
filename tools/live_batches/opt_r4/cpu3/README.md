# CPU pre-draw optimization 3

Prepared offline only. No queue staging or game launch. `cpu_opt3=0` is layer only;
`cpu_opt1=1` and the qualified `cpu_opt2=1` stay fixed in all windows.
ABI unchanged, shader request delta 0.

One inn Ultra run uses OFF/ON/OFF/ON with 600 rendered frames per window.
Ready phases wait only for a fresh zero-pipeline watchdog. Normal runner settings
acknowledgements remain enabled. Every measured window needs submit, capture,
capture detail, hit material and BVH CPU receipts; the post analyzer rejects
missing timer periods. Compare both ON windows against both OFF windows and the
OFF repeat spread before claiming savings. The analyzer returns failure unless
each ON pre-draw and hook-body median beats both OFF medians by more than their
measured repeat spread (KEPT_CPU_COST); noise or regression is NO_SAVING. These are CPU gates; full rendered
quality and energy equivalence remain unmeasured.

Generate: `python tools/live_batches/opt_r4/cpu3/make_job.py`.
Lint: `python tools/live_batches/live_flow.py tools/live_batches/opt_r4/cpu3 --frozen --report tools/live_batches/opt_r4/cpu3/lint.json`.

Baseline CPU2 qualification: inn Ultra hook bodies -0.30 ms/frame, OFF repeat
spread 0.088 ms/frame (`captures/queue/logs/opt-cpu2-inn.1.052927.log.cpu.md`).
CPU3 has no live result. The job is READY_FOR_LIVE only after dependent offline checks pass.
