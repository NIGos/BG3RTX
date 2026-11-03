## RTX Hair automated validation (branch hair-validation, offline only, 2026-11-03)

- Added per-character coverage, silhouette, depth/duplicates, motion/trailing/flicker and energy/PT gates.
- Bounds use OFF-repeat Student prediction intervals plus pixel/depth precision; absent evidence is INVALID.
- Prepared combined Dialogue1/camp/S8 jobs: 297 s each, held Q/E, zoom, six toggles, K8/P16 and Ultra costs.
- Added selected-save F8 reload with sampled lifecycle evidence; live behaviour remains unverified.
- Dedicated 50 s GPU-AV job; launcher resolves local SDK manifest/DLL. No local validation layer found.
- 17 synthetic hair tests, 5 reload parser checks, existing QA detectors and 16 edge-flicker tests pass.
- Four-job dryrun: lints/budgets pass, four expected INVALID negatives and four valid structural positives.
- Estimated capture budget 1,480 MB; shader request delta 0, ABI/defaults unchanged.
- Historical 17:01/17:04 reports both FAIL: device-fault indicators, CPU exception and pool exhaustion.
- Missing runtime-owned mask/MV/fresh/FG/PT evidence and observed 1/5/10 cohorts block qualification.
- See docs/HAIR_VALIDATION.md; no game, hair runtime, saves or Git metadata changes. No commit.
