# Session handoff - 2026-11-01 evening (PAUSED by the user)

Main d089b7b + this docs commit, ABI 25. Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) next, then
[GATE_AUDIT_20261101.md](GATE_AUDIT_20261101.md) and [META_AUDIT_20261101.md](META_AUDIT_20261101.md).

## Rules (read before any work)

- [../AGENTS.md](../AGENTS.md): never touch saves, `D:\BG3_Tools`, snapshot tags; never launch the game
  outside `scripts/test-queue.ps1`; never hold `captures/queue/results.md` open.
- Remove worktrees only with `scripts/remove-worktree.ps1 -Name <folder>` (junctions to `external/`,
  `data/assetdb`; shared file count must stay 994 = external 992 + assetdb 2).
- Live tests need explicit user steering for the batch. Prepare jobs outside every `pending` inbox.
- Gates hold in every ON window (`expect_every`); capacity, scene-wait and exposure-hold lints in every dryrun.
- Branches write `docs/progress/<branch>.md`, never PROGRESS; the coordinator folds them.
- Merge only with `scripts/merge-green.ps1` (shader step cached: ~1-5 min); features off until their gates pass.

## State

- **Live tests paused by the user.** The coordinator holds the pause (its flag; queue `pending` and `running`
  empty, no bg3.exe). Do not queue anything until the user resumes and names a batch.
- **Bugfix round done offline and merged** (details in PROGRESS): `rt_hit_material_recycle` (off, needs live),
  water cascade variants (water works on Ultra), capture buffers scaled by vertex size, capacity + scene-wait
  gates in every job, `smoke_ultra` job, `fg_letterbox` (off, needs live at a 3840x2160 desktop), rtStats
  renumber (rf_hit_gi 419..426, wind RGB 427..434), shader_identity cache.
- Merged and off: `rf_hit_gi`, `rt_hit_material_wind_rgb`, `rt_water_game_shadow`, `ls_box=2`, D-EM sliced check.

## Live batches ready, in priority order (when the user resumes)

1. `smoke_ultra` (su1, S8, ~4 min) with `rt_hit_material_recycle=1` and `fg_letterbox=1` added to the job
   (not in it yet). Gates: water traced > 0, capacity 0, scene-wait 0 timeouts in every window.
2. `scene_wait` (S8 + camp): recycle 0/1/0; expected 0 timeouts, mean 2-5 ms with 1. Then default 1.
3. `m5_promotion` (S8, Flymm, camp; 16.8 min, ~2.5 GB) -> merge worktree-agent-a14b8f58ffc130189 if it passes.
4. `m4_promotion` m4p3/m4p4 with `rf_hit_gi`, and `m4_promotion/s6_diag` (needs replay_pack_cap_mb=3300).
5. `replay_capture/ambient` (camp + inn packs, up to 7 GB during the run).
6. `m4_wind_rgb` (S8, 144 s).
7. Box lights visual check (`box_lights`, S6 + S7).

## Branches not merged (ignoring codex/*)

| Branch | What | Status |
|---|---|---|
| worktree-agent-a9e0aabd4d7f18008 | M4 bundle on in Ultra | merge only after m4_promotion passes |
| worktree-agent-a14b8f58ffc130189 | water chain on in Ultra | merge only after m5_promotion passes |
| lscheck-fix, sup/cascade-alphatest | old (10-27 / 10-30) | stale, superseded; keep |
| worktree-agent-a2212c228ec492214, -a814985dd663a9b89, -a8b7cb486c1434ebd, -a94065fdd50bc8494 | old 10-27 WIP | stale; keep |

## Next offline (no live needed)

1. Expose `rt_hit_material_recycle` and `fg_letterbox` in the GUI Experimental section (AGENTS: no ini edits by the user).
2. Wind RGB identity decision for RT_HIT_INSTANCED variants (336 requests differ with instanced on).
3. `rf_hit_shade` S6 excess is in the direct and box terms: next fix there (stays Experimental, user decision).
4. Release-hardening (Nexus beta question): defaults, FG letterbox, acts 2-3, long sessions, installer, README, licences.

## Working pattern

- One hypothesis, the smallest change behind an ini key, A/B within one run, keep or revert.
- New logic in `core/` (hot reload). ABI changes rare and merged early.
- Report the GPU ms of every new pass from the `gpu` log lines.
- Keep this file under ~80 lines and PROGRESS_CODEX under ~150; archive older sections.
