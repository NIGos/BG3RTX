# Session handoff - 2026-11-02 evening

Main e9f2098, ABI 26. Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) next (report and questions at the top),
then [GOAL_NEXT.md](GOAL_NEXT.md) for the stage order. History: [archive/](archive/).

## Rules (read before any work)

- [../AGENTS.md](../AGENTS.md): never touch saves, `D:\BG3_Tools`, snapshot tags; never launch the game
  outside `scripts/test-queue.ps1`; never hold `captures/queue/results.md` open.
- Remove worktrees only with `scripts/remove-worktree.ps1 -Name <folder>` (junctions to `external/`,
  `data/assetdb`; check the shared file count before and after).
- Live tests need explicit user steering for the batch. Prepare jobs outside every `pending` inbox.
- Gates hold in every ON window (`expect_every`); snapshot lines are `"event": true` with a 1 s `settle_s`.
- Shader compile rules (AGENTS.md "Measuring correctly"): new on/off keys are specialization constants; heavy
  DXC runs one at a time; don't spend compile time a planned improvement makes unnecessary.
- The coordinator's scratchpad `PAUSE` flag was set at 19:50 to free the PC: remove it before the next batch.

## State

- Stages 1-3 done (morning). Ultra: RT water + refraction, + Bounce, Emission at hits, gi_far 6 m.
- Today merged (all new keys off): stream caps, source trim (-0.4 GB), NaN GI fix, OMM option B and VT masks
  (catalogue of 1,677 materials, generated on the user's PC), recipe rows, water RR/FG motion keys (null),
  preprocessed shader cache, settings generator without the key count.
- Closed: port water flicker is the game's own water (wf3-wf7). IP grass and generated rows stay off (cost/gain).
- Dialogue shadows: RT AO darkens characters most; opaque hair cards at the port. Native hair is the fix.

## Next (in this order)

1. Spec-constant migration on `variant-migration` (worktree agent-a0dba2e726bdabdad), on top of
   `spec-cascade-s1-default`. Verified: G, S2a. Unverified code: S2b (gi_far), S2c (instanced), S3a host.
   Commands per step: `docs/progress/variant-migration.md`. Then S3 rest, S4, S5; one merge-green of the final
   set (~100-180 modules); live `scc1` (extend it with windows for G, S2).
2. Rebase on it, cheap identity, then their lives:
   - occ diagnostic `worktree-agent-a0c9be1ab336eae1a` + `dialogue_shadows` batch (numeric occluders, box cascade fix);
   - native hair `worktree-agent-a78f48005e3b350bd` (async compile, water hair) + `hair_qual`;
   - OMM bake `worktree-agent-a53f0b584fa39317b` + `omm_qual` om4;
   - `hm-recipe-rows` + `m4_generated` rerun (bush pairing, two-sided).
3. Consolidation: smoke_ultra with everything qualified, gate audit, remove null keys (water flicker keys),
   fold `docs/progress/`, clean merged worktrees and dead captures (test-main captures were 39 GB).
4. Stage 4 rest (lava flowmap, mesh rows S8), stage 5 SHaRC, stage 6 release hardening.

## Codex

The user may let the coordinator run Codex CLI agents (`codex exec`, workspace-write sandbox, never `resume`)
for verifiable offline tasks. Pending: the user's login choice, then a pilot task. Plan in the transcript of
2026-11-02: one worktree per task, structured report via `--output-schema`, independent re-run and review.

## Open user questions

ls_box=2 visual check (S6); GUI_AUDIT_20261102 five yes/no; project licence; NVIDIA notification;
water sun-shadow edge look at the S8 pool.

## Working pattern

- One hypothesis, the smallest change behind a key, A/B within one run, keep or revert.
- New logic in `core/` (hot reload). ABI changes rare and merged early.
- Report the GPU ms of every new pass. Keep this file under ~80 lines, PROGRESS under ~150.
