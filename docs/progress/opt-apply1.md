## Round 1 optimization outcomes (branch opt-apply1, applied, 2026-11-05)

- Compact holds qualified: High inn saving 0.87 ms vs OFF spread 0.075 ms,
  float energy within OFF spread, identical BVH geometry fixtures. Default and
  every preset 1; release forces 1; developer A/B key retained, no player option.
- Roof reject removed: GI total OFF 21.75/21.58 vs ON 21.87/21.95 ms.
- Lazy origin normal removed: OFF 1.514/1.495 vs ON 1.634/1.520 ms.
  Both show no saving. IDs 1027/1028 reserved; tests/prewarm/jobs removed.
- GI attribution job removed: readiness cannot see cached pipelines.
- Historical scene analyzer flicker FAIL retained in TRACK; no detector pass claimed.
- Check High inn after layer restart; developer key permits same-run A/B.
- Shader request delta 0; ABI unchanged. `scripts/build.ps1 -Test` passes
  176/176 CTests in 150.24 s after schema regeneration and stale test cleanup.
- Release equivalence against main f3d3c41a passes: 203 identical, 11 equivalent,
  0 different; 1,041 assignments, 11 universal certificates; negative control PASS.
  Receipt: `.scratch/release-equivalence.json` (278.4 s).
- Defaults/schema and diff checks pass; no changed Git-visible file exceeds 20 MB.
- No commit, game launch or queue staging.
