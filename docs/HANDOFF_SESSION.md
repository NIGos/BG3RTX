# Session handoff (2026-10-27, about 22:30)

Read this first in a new session, then `docs/STATUS.md` (top section).
Everything needed to resume is in the repo; this file is the entry point.

## Repository
- Private remote: `origin` = https://github.com/NIGos/BG3RTX (all branches pushed; `main` tracks `origin/main`).
- **History was rewritten on 2026-10-27** (author/committer email set to the GitHub noreply address
  `9080863+NIGos@users.noreply.github.com`; local `user.email` is set to it). **Every commit SHA
  changed.** SHAs quoted in older docs, branch `HANDOFF.md`/`PAUSED.md` files and commit messages
  are the OLD ones: translate them with `D:\BG3_Tools\git_backup\commit-map-old-to-new.txt`
  (old full SHA, new full SHA). A bundle of the old history is in the same folder.
  Examples: 4990df7 → 200e803, bbea7ec → fc4d818.

## State right now
- **Paused by the user.** The test queue runner, the window hider and the test game are stopped
  so the user can play. **Do not restart the runner until the user says they have finished.**
  Pending jobs stay in `captures/queue/pending/`.
- Main (fc4d818) is stable: device-lost fix, 8/8 clean stability runs (`stable2-main-*`, 20:00-20:09).

## Priority 1: image unstable during camera pans (user report, not investigated)
While panning, the shadows seem to "detach" from their objects and the image is not stable, as
if the camera motion were not compensated. Facts from the user:
- seen in the latest save played (2026-10-27 evening);
- Ctrl+Shift+F9 (all RT effects off) makes it normal, so it is ours;
- frame generation was on, but the frame rate felt low (FG may not have engaged);
- not sure whether it is the sun shadows, the local-light shadows or GI.

Plan:
1. Read the user's own `bg3rtx.log` from that session before anything overwrites it:
   `[ab]`, `[pace]`, `[ngx]`/frame-generation lines (did FG engage, what frame rate).
2. Scripted pan (`-Move`, through the queue) with one effect on at a time.
3. Suspects: the world position rebuilt from depth with a camera matrix one frame off
   (the previous frame's view-projection, or a jitter mismatch; the error grows with pan speed
   and shows on static objects too); RR motion vectors or the "RR before post" guides using a
   stale camera.

## How work runs
- **In-game tests:** only `scripts/test-queue.ps1` launches the game, serially, with the window
  hidden and click-through (`scripts/game-minimizer.ps1`). Jobs are JSON in
  `captures/queue/pending/`, or in a worktree's own `captures/queue/pending/`. The runner reloads
  itself when its script changes. Jobs without `tree` run on the test tree
  (`.claude/worktrees/test-main`, `scripts/test-tree.ps1`).
- **Results:** `py -3 scripts/queue_results.py <job prefix> [--lines]`. Per-run logs are in
  `captures/queue/logs/`. A run whose `expect` lines (`patterns`) are missing is INVALID.
  Never hold `captures/queue/results.md` open (`tail -f` included): a locked file stopped the
  runner once (writes now retry, fc4d818).
- **Background frame rate:** the hidden game can run at about 0.5 fps. Statistic lines that need
  120 frames (`ls check`, `ls stats`, ...) then never print within a session run: give such jobs
  `"hold": 90` or more.
- **Merging:** `scripts/merge-green.ps1 <branch>`. It merges only if the build, all tests, all
  shaders and the QA tests pass, then syncs the test tree. Merge features early, off by default.
- **Agents:** fresh, short briefs pointing at the branch's `HANDOFF.md`, and closed when done.
  Check the key numbers in the logs yourself before merging.
- **Rules:**
  - every gate derived, or review-only (QUALITY R0 addendum, QA.md);
  - quality before frame rate on Ultra;
  - physically correct, not matching the game (R0);
  - never touch the user's saves.

## Open branches (not in main), with what decides them

| Branch (worktree under .claude/worktrees) | Head | State | Decided by |
|---|---|---|---|
| `worktree-agent-a94065fdd50bc8494` (EN2 hit materials) | 0916cff | G-buffer join fix (cross-thread build), off by default | job `170-en2fix-S8-cov` (pending): `[hitmat] last 300 frames (N joined)` with N > 0, the coverage tool's self-check, the CPU wait in that line |
| `worktree-agent-a2212c228ec492214` (D-EM emissive capture) | 9f4d17d | all scenes pass `em_gate` except AF `304be1c7` (bright ratio about 0.36) | em12 jobs: `em12-af-check` ran (20:19, not yet evaluated); `af-view19/20` pending. Compare ours vs the game's per pixel, pattern or level |
| `lscheck-fix` (frozen D-LS reference realigned) | 48cb758 | reference had an old shadow-ray class | jobs `94-s6-lscheck-ref-cap0/1-h` (pending, with hold 90): "differ in any bit on 0 px". The 20:10 runs were INVALID (no `ls check` line: too few frames) |
| `sharc-v2` | a3181ad | SHaRC is off; S7 passes the reference, S8 indecisive; fine-cell reference added | job `sharc8-qa-S7-S8`; the v2 traced update is blocked on EN2 |
| `worktree-agent-a814985dd663a9b89` (box lights, `ls_box`) | a28a4c7 | WIP. Shader side done (`shaders/rt/bg3_boxes.hlsli`, `LS_BOX` variants, `ls_debug=4` with projector pixels, stats 136-150); `LS_BOX=0` gives the same SPIR-V as before. C++ side in `core/gi.cpp` not done (pipelines, set 1 binding, stats line, ini key). No in-game jobs yet | its `HANDOFF.md`; then jobs on S6, S7, S5 |
| `worktree-agent-a8b7cb486c1434ebd` (OMM, `rt_omm`) | 8b3b87a | design only, no code (GPU baker, conservative opaque/transparent states, unknown falls back to the alpha test, `rt_omm_check`) | its `HANDOFF.md`. Note: that file replaced the inherited root `HANDOFF.md` on the branch; don't let it overwrite main's when merging |

## Next, in order (after priority 1)
1. Merge EN2, D-EM and lscheck-fix as their jobs pass (merge-green).
2. Box lights: finish the C++ side, then S6/S7/S5 jobs (TRACK_D 9.10, COVERAGE.md gap 2).
3. Water: no RT effect at all; 89% of the screen on the docks (COVERAGE.md gap 1).
4. Opacity micromaps for foliage (OMM SDK in external/omm-sdk-1.10.0; DESIGN_SOTA_NEXT item 3).
5. SHaRC v2 once EN2 lands. Then light tree (1c).
6. Hit materials in GI and SHaRC (EN2 consumers).

## Decided by the user
- Modded saves (Act 2, Rivington): leave them; don't reinstall the mods.
- Downloads already approved and done: Aftermath SDK (D:\BG3_Tools), SHaRC 1.8.3, OMM SDK 1.10.0
  (external/, backed up in D:\BG3_Tools\external_backup).
- Replies to the user in Italian, terse.
