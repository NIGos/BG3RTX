# Compact hit state GPU receipt

READY_FOR_LIVE; offline preparation only. Two launches, camp and inn, using the
Ultra release observation profile. Static OFF/ON/OFF/ON windows each measure 600
rendered frames, with reset-aligned 120-frame GPU timers. No images or dumps.

Specialization 1029 is absent from `experimental_specs` bindings. Warmup explicitly
exercises ON; each subsequent toggle waits only for fresh driver QUIET after the
runner acknowledges settings. Missing quiet or timer evidence fails fast.

At 30 rendered fps including 120 s load: camp 234.9 s, inn 225.9 s, plus conditional
readiness (55 s maximum). Both launches have a hard 300 s cap. Historical frozen
p10 throughput predicts about 356 s / 351 s: completion under five minutes is
unproven at those rates; a capped incomplete run is INVALID, never a saving.
Do not build or compile during cost windows. Judge GPU ms, not displayed FG fps.

`analyze.py` reports all six passes and OFF repeat spreads. SAVING requires both
ON trace medians to beat both OFF medians by more than the OFF spread. NO_SAVING
exits successfully; only missing evidence exits nonzero. Offline image bit equality
is documented in `docs/progress/gi-livestate.md`; this batch measures GPU cost only.

Generate: `python tools/live_batches/opt_r5/compact_hit/make_job.py --profile-exe .scratch/compact-hit-profile.exe`
(compile `tools/live_batches/release_qual/profile.cpp` first, as in opt_profile).
Lint: `python tools/live_batches/live_flow.py tools/live_batches/opt_r5/compact_hit --frozen --report tools/live_batches/opt_r5/compact_hit/lint.json`.
The snapshot was extended additively for these two jobs only; no corpus refresh.
Explicit authorization is required before staging or launching.
