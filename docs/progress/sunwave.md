## Sun wave rejected; far GI attribution (branch sunwave, offline, 2026-11-05)

- Removed wave code/key/settings/spec wiring, scratch buffers, two requests and feature tests; SpecId 1034 retired, never reuse.
  Manifest requests 127 -> 125, no ABI change. Kept paired `--ab-spec` / `--ab-spv`, tests, CSV, equality checks and docs.
- Rejection ON-OFF median [95% median CI], 40 pairs, ms: S8 +0.735776 [0.540272, 0.849120];
  camp +3.114312 [2.451616, 3.897856]; inn +0.737392 [0.638336, 0.863264]. Outputs/counters were exact; all slower.
- Cleanup release proof vs main `5b076d9`: 215/215 ordinary requests byte-identical; negative control passes.
- Far attribution: original main shaders plus replay-only source snapshots, sun trust, radius 6 m, two rays, statistics off.
  Each value below is its own 40-pair median [95% median CI], ms; conditional execution/compiler effects are not additive.

| Pack | Traversal | Extra far hidden shading | Matched GI roof loop | Other far policy/context |
|---|---|---|---|---|
| S8 | 0.325 [0.324, 0.326] | 1.562 [1.227, 2.396] | 0.465 [0.351, 0.772] | 0.562 [0.561, 0.563] |
| Camp | 3.787 [3.641, 4.060] | 2.037 [1.500, 3.377] | 0.332 [-0.074, 1.362] | 3.217 [2.761, 3.822] |
| Inn | 2.065 [1.973, 2.329] | 0.926 [0.512, 1.154] | 0.374 [-0.007, 0.650] | 1.711 [1.372, 2.074] |

- Fresh original far totals: 2.762 / 9.132 / 4.702 ms; historical 2.430 / 5.460 / 2.448 are a different context.
  Exact frozen roof decisions preserve downstream work. Roof control retains both bodies/common cache load; only S8 excludes zero.
  N/Q outputs and near shading counters match; all roof controls match every output/counter bit, with zero cache/mismatch guards.
- Statistics atomics do no work at rt_stats=0; residual includes fade, screen/eligibility and static compiler context.
  Original near/far driver registers 168 -> 255. No spill/occupancy inference from anomalous memory figures.
- Memoized screen/hidden roof reuse has zero opportunities on all packs. Inn's extra hidden shading changes no stored output.
- Two candidates only, NOT implemented: early exact inside-roof rejection after emission, before far hidden material shading,
  planning 0.2-0.5 ms S8 / 0.4-0.7 ms inn (proportional qualified-hit model, less guard cost; camp could lose);
  exact identity-quaternion roof path, planning 0.1-0.2 ms S8 / 0-0.2 ms camp/inn. These are estimates, not measured gains.
- Cleanup `scripts/build.ps1 -Test` passed: full build and all 191 CTests (158.05 s).
  No game, queue or git writes; builds BelowNormal, GPU used only without bg3.exe.
- Design/proof caveats: TRACK_C_GI "Far GI cost split, 2026-11-05". Receipts: `.scratch/far-cost/summary.json`.
  Cleanup proof: `.scratch/sunwave/cleanup-equivalence.json`; historical rejection: `.scratch/sunwave/summary.json`.
- Analyzed image outputs removed safely; original packs, frozen input caches, shader hashes, counters, logs and CSV remain.
