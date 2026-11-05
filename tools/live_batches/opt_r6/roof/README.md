# Early roof rejection confirmation

READY_FOR_LIVE. Offline preparation only; explicit authorization is required before staging or launching.
Copied the compact_hit batch: inn (`The Blushing Mermaid - 96h 39m`) and S8 (`Water Queens House`),
Ultra release observation profile, dev=1, gi_far=1, rt_stats=0 and qualified compact hit state 1.
Only `gi_far_roof_early` changes between OFF/ON/OFF/ON static windows, each 600 rendered frames.
All six GPU timers use reset-aligned 120-frame periods, at least three complete periods per window.
No images, dumps, or extra diagnostics in cost windows.

SpecId 1038 is absent from experimental_specs. Warmup exercises ON explicitly; every acknowledged
toggle waits for fresh driver QUIET. Missing QUIET/timer evidence fails fast. The readiness TABLE
maps the key to GI_TRACE; the frozen snapshot is extended additively for these two jobs only.
When rt_stats != 0 the host forces 1038=0, selecting the original executable with no diagnostic toggle cost.

At 30 rendered fps plus 120 s load: inn 225.9 s / S8 222.9 s, plus conditional readiness (55 s max).
The unchanged compact_hit hard cap is 300 s. Historical frozen p10 predicts inn 351.18 s / S8 237.06 s;
the inn cap is unproven at that throughput. A capped incomplete run is INVALID, never a saving.
Do not build or compile during cost windows. Judge GPU ms, not FG display fps.

analyze.py reports all six passes, OFF repeat spread and paired ON-minus-preceding-OFF deltas;
GI trace pair deltas also print to stdout. SAVING requires both ON trace medians to beat both OFF
medians by more than the OFF repeat spread. NO_SAVING exits successfully; missing evidence is INVALID.
Offline exactness, production timing and register evidence: docs/progress/roofrej.md.
Latest ID 1038 replay gains at S8/inn but loses at camp (+0.495 [0.103, 1.696] ms, rt_stats=0).
The production no-loss bar is not freshly satisfied. READY_FOR_LIVE describes the prepared jobs,
not an offline performance qualification; coordinator review precedes any live authorization.

Generate: python tools/live_batches/opt_r6/roof/make_job.py
The default freezes compact_hit's exact profile, keeping qualified rt_compact_hit_state=1.
Optional --profile-exe refreshes from the launcher profile exporter.
Frozen lint: python tools/live_batches/live_flow.py tools/live_batches/opt_r6/roof --frozen --report tools/live_batches/opt_r6/roof/lint.json
Non-frozen lint: python tools/live_batches/live_flow.py tools/live_batches/opt_r6/roof --cache .scratch/roofrej1038/live-flow-cache.json --report .scratch/roofrej1038/lint-live.json
The job test is build-free, like compact_hit_jobs_test; no merge_scope build-only exclusion is needed.
