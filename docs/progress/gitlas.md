## Conservative GI-only TLAS (branch gitlas, rejected, 2026-11-05)

- STEP 1 only, offline RTX 5090, production sun-trust far GI, 6 m, two rays, compact hit state 1.
- Full instance slots/order, indices, transforms, flags, SBT offsets and BLAS addresses preserved; exclusions change only masks to 0.
- Only GI closest-hit uses the second TLAS; nested lighting visibility retains the full TLAS.
- All positive-depth surfaces, captured geometric offsets, bias/footprint fallbacks and outward rounding define conservative reach bounds. Unknown bounds retain instances.
- S8/camp/inn axis expansion E: 6.486/7.110/6.787 m; safe spherical R_max: 6.842/7.922/7.363 m. Culling uses the tighter per-axis envelope, which contains every GI-origin radius-6 m sphere.
- Culled GI-eligible instances: S8 10/874 (1.14%); camp 9/1039 (0.87%); inn 53/1506 (3.52%). Original slots: 875/1040/1507.

Each row: 40 ABBA/BAAB pairs, four excluded warmups; ms GPU. Delta is B minus A; intervals are exact 95% median intervals. A/B are separate medians of pair means; their difference need not equal the paired median delta.

| Pack / rt_stats | GI full / reduced | GI delta [95%] | Extra BUILD + dependency | Net delta [95%] |
|---|---|---|---|---|
| S8 / 0 | 5.068 / 4.703 | +0.216 [-0.496, +0.442] | 0.326 | +0.535 [-0.175, +0.815] |
| Camp / 0 | 12.374 / 12.621 | +0.449 [-0.532, +1.137] | 0.267 | +0.712 [-0.269, +1.461] |
| Inn / 0 | 4.589 / 4.848 | +0.248 [-0.470, +0.990] | 0.362 | +0.590 [-0.129, +1.354] |
| S8 / 1 | 27.748 / 27.826 | +0.511 [-0.517, +1.357] | 0.253 | +0.740 [-0.261, +1.616] |
| Camp / 1 | 100.295 / 100.161 | +0.233 [-1.097, +1.417] | 0.167 | +0.400 [-0.930, +1.582] |
| Inn / 1 | 28.569 / 28.372 | +0.033 [-0.933, +0.544] | 0.281 | +0.314 [-0.664, +0.795] |

- All stored outputs and every raw counter/bin were 100% bit-equal in all six comparisons, with validation enabled and exit 0. Each B dispatch builds the extra TLAS afresh.
- No interval establishes a net gain. STOP: no STEP 2 product design or product implementation. This rejects the tested enclosing-AABB candidate; no general production exactness claim.
- Net excludes offline CPU bounds/filter/upload costs: depth bounds 156-157 ms; filter context 6.7-12.9 s includes replay BLAS reconstruction, not a product-frame estimate. GPU net is already an optimistic lower bound.
- Initial S8 run INVALID: missing production sampler layout set 1/b19. Stopped, fixed replay layout and pipeline fail-fast, rebuilt; all six new valid runs retained separately.
- BelowNormal minimal build; focused CPU CTests 3/3 PASS, cached production SPIR-V validation PASS. The new bounds test is a native CTest; no build-dependent Python test needs merge_scope exclusion.
- Receipts: `.scratch/gitlas/summary.json`, six `*-valid` directories/logs, raw outputs/counters, `ab.csv`, build/net pairs and full intervals. Historical receipt field `R_max` means E; summary normalizes it explicitly. Replay limitations retain masked unsupported geometry and no OMM.
- Shader request delta 0; ABI unchanged. No game, queue or git writes; GPU ran only after checking no bg3.exe. Reproduction: [frame_replay](../../tools/frame_replay.md).
