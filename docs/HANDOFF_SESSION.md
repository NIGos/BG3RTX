# Session handoff - 2026-11-01

Main 5202bc2, ABI 24 (rebuild both DLLs together). Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) next:
it has the live numbers and the "How to test in game" section. The previous handoff (10-31) is in git
history; its evidence details are kept in [archive/PROGRESS_CODEX_2026-11-01.md](archive/PROGRESS_CODEX_2026-11-01.md).

## Rules (read before any work)

- [../AGENTS.md](../AGENTS.md): never touch saves, `D:\BG3_Tools`, snapshot tags; never launch the game
  outside `scripts/test-queue.ps1`; never hold `captures/queue/results.md` open.
- Worktrees with junctions (`external/`, `data/assetdb`): unlink the junctions first
  with `scripts/remove-worktree.ps1 -Name <folder>`, which also checks the targets. A careless removal emptied the
  shared SDKs once on 11-01 (restored).
- Live tests need explicit user steering for the batch. Prepare jobs outside every `pending` inbox.
  Old approvals and markers do not grant it.
- Lean live jobs: no `rt_stats` / `rt_primary_check` inside cost windows (~80 ms per frame).
- Merge only with `scripts/merge-green.ps1`; features off by default until their gates pass.
- Docs index: [README.md](README.md). Plan: [RENDERING_EXECUTION_PLAN.md](RENDERING_EXECUTION_PLAN.md).

## Current state

- Stage 0 (consolidation, regression, lean jobs) is closed. `rt_cascade_casters` is ON in Ultra.
- Stage 1 = M4 ([PLAN_M4_REFLECTIONS_GI.md](PLAN_M4_REFLECTIONS_GI.md)), all keys off by default:
  - `rt_hit_material` (restart): qualified hits S8 ~17% (was 0.4%), S6 ~6.7%.
  - `rf_hit_shade`: tier (a) L_b / L_a S8 0.95 (AO at hit 0.805 vs screen 0.802), S6 1.48. The box
    AO-on-direct fix (a9b7e75) landed after the S6 run and is unmeasured.
  - `gi_hit_direct`: nonzero live. Motion qualification and isolated cost open.
  - `gi_hit_sun`: merged with the shared roof rule; live gate at S3 pending.
- D-EM: 5 of 6 families pass AF; 304be1c7 excluded (`rt_em_fx_pulse=0`), 159a6211 legitimately 0.
  `rt_emissive` stays 0; no consumer adds emission at hits yet (M4.4).
- Inn rotation: pool not lagging; about half the mismatch is camera-faded walls, which keep casting
  shadows by user decision. Scene ready in time in normal play.
- Watch items (no cause): camp rugs/planks, Astarion hairline, dialogue shadow flicker.

## Next, in order

1. **M4 remaining**
   - Hit-material families by combined share ([MATERIAL_FAMILIES.md](MATERIAL_FAMILIES.md)):
     54df894a/613ead9c (12.4%, full recipe audit), then the cheap COPY/VARIANT rows 50fbfceb/8e49cde8,
     1e9145e4/923131c7, 9cc7094f/4464f442; c32ce535/9064a6bd for S6. Regenerate the ranking after each.
   - S6 direct bias recheck: one S6 run with `rf_stats=1` after a9b7e75. Split tier (a) direct into
     point/spot and box, histogram hit vs screen light channel ([LIGHT_CONVENTIONS.md](LIGHT_CONVENTIONS.md) 4).
     Expect the excess to fall by box diffuse share x 0.24; if not, look at visibility and material inputs.
   - M4.1 `gi_hit_direct` in motion with isolated GPU ms; M4.3 `gi_hit_sun` at S3. Gates that can fail
     (participation share, energy delta, per-term bias budget: [META_AUDIT_20261031.md](META_AUDIT_20261031.md)).
   - M4.4 emission at hits after the above.
2. **M5 water**: M5.1 `rt_water_geometry` is in progress offline (capture, own TLAS, coverage pass,
   ABI 25 merged early and alone). Plan and gates: [PLAN_M5_WATER.md](PLAN_M5_WATER.md).
3. **Tooling**: offline frame replay (re-run our passes on a captured frame without the game; needs a
   replay-pack capture mode first). Ranking: [OFFLINE_OPPORTUNITIES.md](OFFLINE_OPPORTUNITIES.md) A.
4. M2 persistence only when a visible offscreen need requires it; M6 reference/PT research last.

## Live batches prepared (not authorized)

Under `tools/live_batches/`: `m4_round1`, `m4_m5_first`, `em_forge`, `pool_lag`, `lean_regression`.
Each README states its gates. Ask the user for a concrete batch once its offline checks are done.

## Working pattern

- One hypothesis, the smallest change behind an ini key, A/B within one run, keep or revert.
- New logic in `core/` (hot reload). ABI changes rare and merged early.
- Report the GPU ms of every new pass from the `gpu` log lines.
- Keep this file under ~120 lines and PROGRESS_CODEX under ~150; archive older sections.
