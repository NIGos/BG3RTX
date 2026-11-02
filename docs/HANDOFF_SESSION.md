# Session handoff - 2026-11-02 morning

Main fe8f75d + this docs commit, ABI 25. Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) next (report and
questions at the top), then [GOAL_NEXT.md](GOAL_NEXT.md) for the stage order. History: [archive/](archive/).

## Rules (read before any work)

- [../AGENTS.md](../AGENTS.md): never touch saves, `D:\BG3_Tools`, snapshot tags; never launch the game
  outside `scripts/test-queue.ps1`; never hold `captures/queue/results.md` open.
- Remove worktrees only with `scripts/remove-worktree.ps1 -Name <folder>` (junctions to `external/`,
  `data/assetdb`; shared file count must stay 994 = external 992 + assetdb 2).
- Live tests need explicit user steering for the batch. Prepare jobs outside every `pending` inbox.
- Gates hold in every ON window (`expect_every`); capacity, scene-wait and exposure-hold lints in every dryrun.
- Branches write `docs/progress/<branch>.md`, never PROGRESS; the coordinator folds them.
- Agents run `git merge main` before gates or long tools (main has the cached, fastest tools).
- Merge only with `scripts/merge-green.ps1` (shader step cached: ~1-5 min); features off until their gates pass.

## State

- Stages 1-3 done: bugfix round live-checked (smoke_ultra valid, scene wait 0, water 120/120 composed, shader
  cache, fg_letterbox automatic); GUI levels (Experimental 5 entries); RT water "+ Refraction" in Ultra.
- Ultra also has Hidden-surface shading "+ Bounce", Emission at ray hits "Hidden hits", Far-field sun bounce 6 m.
  smoke_ultra with all of it: valid, median frame 16.65 ms (was 13.75).
- Trace fix 385efd1 (layer/geocapture.cpp): traced frames keep the hit-material capture. Energies measured
  before it are invalid for hit features.
- M6 step 1 done: ambient ratio not consistent across scenes (S8 1.44, camp 0.84, inn 0.78): no global correction.
- `rt_hit_material_pool_alpha` default 1 (S6 qualified 14.8% -> 52.1%, +0.46 ms GI trace).
- Off, waiting: `ls_box=2` (user visual check, question in PROGRESS), `rt_hit_material_mask` (live `m4_hit_mask` ready), `rf_hit_shade` / `rf_hit_gi` (Experimental, bias).

## Next

Stage 4 rest (M4):
1. Coverage: S6 lava discs FLUID_Lava_Plane_Flowmap (animated flowmap recipe: time, vertex colour, world position);
   camp grass IP_Wind_GrassFlowers (per-draw rows like wind RGB). Rank: `tools/hit_material_blockers.py`.
2. `rf_hit_gi`: measure tier (a) L_b / L_a at S8 / S6 after the trace fix; then the S6 visibility suspect
   (lit 71% of rf light rays vs 56% in the GI). rf_hit_shade stays Experimental until in the spread.
3. `ls_box=2` into Ultra if the user approves; then smoke_ultra once.
4. Decide the `rt_hit_material_mask` default from its live numbers.

Stage 5 (M6): SHaRC (radiance cache) behind a key, off; first an offline replay comparison against the PT reference.
Why S8's native ambient is brighter than the reference (1.44) while camp and inn are darker.

Stage 6: release hardening for a Nexus beta: release.ini vs the new Ultra keys (rt_emissive forced 0 there),
long sessions, acts 2-3, installer, README, licences; later the diagnostic package (save picked by the user, read-only).

Consolidation after each stage: non-regression batch + smoke_ultra, gate audit, remove superseded keys,
HANDOFF <= 80 lines, PROGRESS ~150 (fold fragments), clean worktrees and dead captures.

## Branches not merged (ignoring codex/*)

| Branch | What | Status |
|---|---|---|
| worktree-agent-a9e0aabd4d7f18008 | old M4 bundle preset | superseded by m4-preset (merged); keep |
| worktree-agent-a14b8f58ffc130189 | old water preset | superseded by ultra-water-preset (merged); keep |
| lscheck-fix, sup/cascade-alphatest | old (10-27 / 10-30) | stale; keep |
| worktree-agent-a2212c228ec492214, -a814985dd663a9b89, -a8b7cb486c1434ebd, -a94065fdd50bc8494 | old 10-27 WIP | stale; keep |

## Working pattern

- One hypothesis, the smallest change behind an ini key, A/B within one run, keep or revert.
- New logic in `core/` (hot reload). ABI changes rare and merged early.
- Report the GPU ms of every new pass from the `gpu` log lines.
- Keep this file under ~80 lines and PROGRESS_CODEX under ~150; archive older sections.
