# Session handoff - 2026-11-01 afternoon

Main ac57418, ABI 25 (layer and core rebuilt together). Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) next: open
user questions, the consolidation report, live numbers per stage and "How to test in game".
Ranked next steps come from [META_AUDIT_20261101.md](META_AUDIT_20261101.md) section 6.

## Rules (read before any work)

- [../AGENTS.md](../AGENTS.md): never touch saves, `D:\BG3_Tools`, snapshot tags; never launch the game
  outside `scripts/test-queue.ps1`; never hold `captures/queue/results.md` open.
- Remove worktrees only with `scripts/remove-worktree.ps1 -Name <folder>` (junctions to `external/`,
  `data/assetdb`; careless removal emptied the shared SDKs twice).
- Live tests need explicit user steering for the batch. Prepare jobs outside every `pending` inbox.
- No builds, shader compiles or GPU tests during a live batch's cost windows.
- Gates hold in every ON window (`expect_every`). Stats counters device-local (`host_atomics_lint_test`).
- Branches write `docs/progress/<branch>.md`, never PROGRESS; the coordinator folds them.
- Merge only with `scripts/merge-green.ps1`; features off by default until their gates pass.

## Running now

- Foreground live tests are running (latest queue logs: S8 water `m5w2` 13:12 and `m5w6` 13:16).
  Do not build, compile shaders or run GPU tests while they run.

## Branches not merged

- worktree-agent-a9e0aabd4d7f18008: M4 bundle on in Ultra. Do not merge: `m4_promotion` failed (S6 NaN).
- worktree-agent-abd9303a6e22b012e: M5.4 R = C - D speck fix, merged with main, 80 CTests. Needs one m5w4 run.
- worktree-agent-ab66efcab091c48ec: `ls_box=2` shadows the unshadowed box lights. Offline.
- low-prio: the live game keeps Normal priority (BelowNormal starved the level load).

## Next, in order

1. **M4 promotion.**
   - Fix the S6 Forge NaN with the bundle on (2.9k -> 8.7k px). Find which key adds them (toggle per step in one run).
   - Rerun `m4_promotion` (4 launches, ~18 min) with a two-sided per-step energy gate and the predicted delta
     written before the run, capacity at 1024/427 MB, and `hit_ao_rays` 0/2 inside the cost window to explain
     `rf_hit_shade`'s +1.4-1.8 ms. Bundle whole-GI cost was camp +4.8, S8 +3.4, S6 +9.1 ms: explain it.
   - `rt_hit_material_instanced` passed (0 clip rejects in every window): it may rejoin the bundle.
   - Separately: `gi_hit_sun` at S3; one M4.1 moving run with isolated GI trace ms.
   - `rf_hit_shade` promotion stays the user's call (bias S8 -7%, S6 +9%).
2. **M5 water.**
   - One m5w4 run of the R = C - D fix (clamped < 120 per ON window), then merge it.
   - One moving m5w3 window; a reflection crop against a reference to settle the 0.31 ratio.
   - A save with open sea for S1. Then `rt_water_hit_shade` (tier (b) for the 8.8% hidden rays).
   - M5.5 next steps only after step 1 (live: 69.6% composed, 0.24 ms) is reviewed in game.
3. **M6 step 1.** Re-capture the PT pack with `rt_cascade_casters=1` (or roofs in the PT sun rays), with the VT
   cache and the native ambient/sky, plus one interior pack (S3 or S6). The native / PT far+env ratio 0.70 is
   preliminary (no roofs). Only if far-field sun stays above ~30%: qualify `gi_far` live (batch `gi_far`).
4. **Housekeeping.** Refresh the coverage HANDLING table and recapture S1-S14; derive the `ls_check` threshold;
   keep PROGRESS at ~150 lines.

## Live batches prepared (not authorized)

Under `tools/live_batches/`: `m4_promotion` (gate table `python -m tools.m4_promotion_gate`), `m4_instanced`,
`m5_water` (m5w2-m5w6), `gi_far`, `replay_capture`, `em_forge`, `lean_regression`. Each README states its gates.
Ask the user for a concrete batch once its offline checks are done.

## Working pattern

- One hypothesis, the smallest change behind an ini key, A/B within one run, keep or revert.
- New logic in `core/` (hot reload). ABI changes rare and merged early.
- Report the GPU ms of every new pass from the `gpu` log lines.
- Record a disproved hypothesis in the batch that disproved it.
- Keep this file under ~150 lines and PROGRESS_CODEX under ~150; archive older sections.
