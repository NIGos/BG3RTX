# S8 hidden water shading qualification

Prepared offline; no game launch or queue submission. Run `python -m tools.live_batches.reflections_offscreen.make_jobs` and `dryrun.ps1` to reproduce.

One Water Queens House launch, Ultra, visible_unfocused_fg, shading mode 0/2/0 on a static view and matching held-Q/E turns. Mode 2 is the baseline needed by the independent water-cost-bit jobs. Shared timing/readiness retimes every window; cost phases have diagnostics and captures off. Twelve PNG/float-mean traces, no float bursts. Estimated 4.8 minutes including 120 s load and measured readiness; 730 MB. A runtime 15-minute cap rejects readiness stalls, whose exact medians are unproven.

Deciding observations: every ON cost window has attempted and hidden qualified shading; full captured draw/triangle coverage, no world truncation/roof overflow/capacity loss; tier histogram, reflections and compose GPU ms. Compare static post-water float energy (`water-compose`, divided by exposure) with same-run OFF-repeat noise using the existing gated `tools/m4_promotion_gate.py --pool run`. Inspect paired reflected-object images and matching turns; reject unsupported/no visible improvement or physically wrong shading. Energy is a shading-model diagnostic: an energy change needs a matching offline PT reference explanation before promotion; it must never be normalized away. This job alone cannot grant full off-screen reference correctness. Missing observations are INVALID, never a pass.

Predictions are uncommitted because Git metadata is read-only here. The coordinator must commit them before live use; the existing energy gate rejects post-hoc predictions. `offline_coverage.json` records why the retained GI-only replay cannot validate water shading. Run wos1 before any water-cost launch; keep failed candidate keys off.

Finite waits: 180 s cold start, at most 30 s per later key wait, 11.667 s producer sanity. Full phases + every wait timeout + 120 s load = 652.967 s (10.89 min). Timeout is INVALID.
