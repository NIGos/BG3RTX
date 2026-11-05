## Exact local-shadow optimisation (branch ls-opt2, OFFLINE QUALIFIED, 2026-11-05)

- Offline only; no game, queue or Git writes. Inn pack frame 1984, production
  box exact-k request; shader request delta 0, ABI unchanged.
- Fixed replay: appending ray-mask-excluded cascade geometry enlarged soup from
  2,146,435,072 to 2,381,090,368 bytes. NVIDIA GetDimensions returned 0, causing
  92,389 alpha bounds guards and 45,674 diffuse / 41,431 specular differing bytes.
- Preserve the captured soup range when both effective LS specializations exclude
  cascade geometry, respecting common and paired overrides. Regression cases added.
- Fixed replay: 0 differing bytes in diffuse, specular and auxiliary outputs;
  every raw counter matches live, guards 0, two repeats deterministic.
  Receipt: `.scratch/ls-opt2/fixed-validation.log`.
- Baseline: 168 registers, 7,168 shared-memory bytes; replay 0.821296 ms median
  (two dispatches). Replay versus live about 2.0 ms is a context difference,
  not an optimisation result. Driver local-memory statistic is anomalous.
- Added replay-only `--ab-warmup` with complete GPU input restoration; default 0.
  Synthetic warmed/unwarmed output and counter identity passes. Final same-module
  control, 128 warmups/40 pairs: -0.001752 ms, 95% CI [-0.004256, +0.001008].
  Earlier 64-warmup control predates the final barrier; not the matched cost gate.
- Seven nonadditive ablations: rays -0.289352 ms, point/spot loop -0.536176 ms,
  boxes/projectors -0.232368 ms (each resolved at 95%). Ownership, atlas, weight
  BRDF and duplicate early tests remain unresolved. The captured join is not timed.
  Full intervals/registers: `.scratch/ls-opt2/profile-summary.md` and `.json`.
- Kept `ls_fast=0`, developer-only, SpecId 1040, forced off in release/presets.
  K1/NR1 omits independent unused reservoir 1; other modes retain the old path.
- Forty paired samples: saving 0.011552 ms, 95% CI [0.004832, 0.015232], about
  1.5% of the roughly 0.77 ms warmed replay; no live 2.0 ms extrapolation.
  Every output/counter bit equals OFF: 160 measured dispatches at rt_stats=0,
  eight at rt_stats=1. Final production SPIR-V equals the timed candidate bytes.
- Registers 168 -> 168; shared memory 7,168 -> 6,912 bytes. Duplicate-load and
  box lifetime trials gave no register reduction and were not retained.
- Key-off release equivalence: 203 identical + 12 equivalent, 0 different;
  260 assignments, universal certificates and negative control PASS.
  Receipt: `.scratch/ls-opt2/release-equivalence.json`.
- Recheck inn frame 1984 offline with `--ab-spec 1040=0,1 --repeat 40
  --ab-warmup 128 --ab-require-equal`; no live test authorized or run.
- BelowNormal `build.ps1 -Test` PASS 195/195 (173.56 s CTest).
  Log: `.scratch/ls-opt2/build-test.log`; filename classification/package guard pass.
- Coordinator decision: ls_fast dropped as negligible (0.012 ms, below the 0.05 ms bar used this round); code removed,
  SpecId 1040 retired. Kept: the LS replay fix (live outputs and counters bit-equal).
