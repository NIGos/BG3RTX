> Retired scheduling, 2026-11-04. Rejected: compose refused every pass because of the light binding 21-22 mismatch. Bits 1/2/4/7 have no qualification. Removed from active regeneration and scheduling; jobs/plans remain as test/lint fixtures. The instructions below are historical.

# Water reflection cost bits

Prepared offline, never queued. `python -m tools.live_batches.water_offscreen.make_jobs` reads retained source seeds and uses shared live_flow timing/readiness. `dryrun.ps1` validates each launch independently.

Three Water Queens House launches: `wo1-s8-hit-shade` tests bit 1 (sparse), `wo1-s8-bit2` tests cone/zero culling, `wo1-s8-bit4` tests candidate ordering. Each independently compares 0/value/0/value at fixed hit shading 2 and compose-shift 1. The historical oversized wo1 path now starts bit 1 only. The unused historic wo1_s8 plan is retained as provenance; it is not referenced by a job. The combined-bit test is excluded: it cannot substitute for independent bit verdicts.

Each job warms its own states, takes four static float-energy windows (two traces each), then four diagnostic-free cost windows. Shared sizing gives 188 s phases plus 8.428 s estimated readiness and 120 s loading: 5.3 minutes/launch, 490 MB each, 1.47 GB batch. Each has a runtime 15-minute cap; unproven key-specific readiness medians do not constitute timing qualification.

Every ON must preserve post-water float energy and composition/tier coverage within its own OFF-repeat noise, with 100% captured supported water draws/triangles, zero invalid hits and zero world truncation in every compared window. The existing m4 post gate checks every ON's `water-compose` energy divided by exposure. Retained log histograms provide the coverage comparison. Reject a bit if any ON fails those rules, or if either ON reflection GPU median/p90 does not improve beyond OFF-repeat spread. Report compose ms separately; never use frame time or captures for cost. Missing observations invalidate the run. Do not adjust radiance to pass.

Run the shading qualification first; bit jobs measure optimization equivalence, not the correctness of mode 2. Predictions were written before validation; the coordinator must commit them before live runs because this workspace's Git metadata is read-only. Final phases and queue restoration clear overrides. No live qualification is claimed by a synthetic receipt.

Finite cold-start wait is 180 s; subsequent key/producer waits are capped at 30 s (producer sanity 11.428 s). Including every timeout plus 120 s load, each launch is bounded by 799.428 s (13.33 min). Timeout is INVALID, never evidence to reject rendering.

Final outcome (2026-11-04): off-screen hit shading rejected for release; compose refused 60/60 passes for light binding 21-22 (input lifetime 57-60/60), despite 2.37 M diagnostic hidden hits per period. Bit 1 remains blocked; bits 2/4/7 rejected. Bit 4: 2.107 -> 2.100 ms, inconsistent compose saving; bits 7: 2.105 -> 0.529 ms without an energy pass. See [final water outcomes](../../../docs/TRACK_D_REFLECTIONS.md) and [backlog](../../../docs/BACKLOG_20261104.md).
