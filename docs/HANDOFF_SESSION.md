# Session handoff (2026-10-27, about 20:30)

Read this first in a new session, then `docs/STATUS.md` (top section) and the memory index.
The previous session's context grew too large; everything needed is in the repo.

## How work runs
- **In-game tests:** only `scripts/test-queue.ps1` launches the game, serially, with the window
  hidden and click-through (`scripts/game-minimizer.ps1`). Jobs are JSON in
  `captures/queue/pending/`, or in a worktree's own `captures/queue/pending/`. The runner reloads
  itself when its script changes. Jobs without `tree` run on the test tree
  (`.claude/worktrees/test-main`, `scripts/test-tree.ps1`).
- **Results:** `py -3 scripts/queue_results.py <job prefix> [--lines]`. Per-run logs are in
  `captures/queue/logs/`. A run whose `expect` lines are missing is INVALID.
- **Merging:** `scripts/merge-green.ps1 <branch>`. It merges only if the build, all tests, all
  shaders and the QA tests pass, then syncs the test tree. Merge features early, off by default.
- **Agents:** fresh, short briefs pointing at the branch's HANDOFF.md, and closed when done.
  Don't resume old agents. Check the key numbers in the logs yourself before merging.
- **Rules:**
  - every gate derived, or review-only (QUALITY R0 addendum, QA.md);
  - quality before frame rate on Ultra;
  - physically correct, not matching the game (R0);
  - never touch the user's saves.

## Open branches (not in main), with what decides them

| Branch (worktree under .claude/worktrees) | Head | State | Decided by |
|---|---|---|---|
| `worktree-agent-a94065fdd50bc8494` (EN2 hit materials) | 153a4f5 | G-buffer join fix (cross-thread build), off by default | job `170-en2fix-S8-cov`: `[hitmat] last 300 frames (N joined)` with N > 0, the coverage tool's self-check, the CPU wait in that line |
| `worktree-agent-a2212c228ec492214` (D-EM emissive capture) | af7c8fa or later | all scenes pass `em_gate` except AF `304be1c7` (bright ratio about 0.36) | em12 jobs (af-check, gf-check, af-view19/20 per-family views): compare ours vs the game's per pixel, pattern or level |
| `lscheck-fix` (frozen D-LS reference realigned) | 403b2f9 | reference had an old shadow-ray class | jobs `s6-lscheck-ref-cap0/1`: "differ in any bit on 0 px" |
| `sharc-v2` | merged up to 91e8eaa | SHaRC is off; S7 passes the reference, S8 indecisive; fine-cell reference added | job `sharc8-qa-S7-S8`; the v2 traced update is blocked on EN2 |

## In main and stable (0b80c5c)
- Crash fix (destroyed SSS buffer in D-LS), BVH split on, Ctrl+Shift+F9 A/B hotkey (not yet
  tried by hand), GI diffuse bounce, local shadows for all lights, runtime-heuristic fixes
  (sun trust from roofs and frustum, candidate cap falls back to the game, shared reflection
  origin), derived QA thresholds, exposure hold.
- Stability check `stable2-main-*` was running at handoff: check it with queue_results.

## Next, in order
1. Merge EN2, D-EM and lscheck-fix as their jobs pass (merge-green).
2. Box lights: 15% of the light at Adamantine, 8% at Grymforge and the Blushing Mermaid
   (TRACK_D 9.10 plan, COVERAGE.md gap 2).
3. Water: no RT effect at all; 89% of the screen on the docks (COVERAGE.md gap 1).
4. Opacity micromaps for foliage (OMM SDK in external/omm-sdk-1.10.0; DESIGN_SOTA_NEXT item 3).
5. SHaRC v2 once EN2 lands. Then light tree (1c).
6. Hit materials in GI and SHaRC (EN2 consumers).

## Decided by the user
- Modded saves (Act 2, Rivington): leave them; don't reinstall the mods.
- Downloads already approved and done: Aftermath SDK (D:\BG3_Tools), SHaRC 1.8.3, OMM SDK 1.10.0
  (external/, backed up in D:\BG3_Tools\external_backup).
