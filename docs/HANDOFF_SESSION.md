# Session handoff - 2026-11-15

Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) first (top: VRAM right-sizing, the consolidation round, findings not fixed).
Design and history: [RTX_HAIR_PRIMARY](RTX_HAIR_PRIMARY.md) 6g-6k, [SCENE_HANDOUT_DESIGN](SCENE_HANDOUT_DESIGN.md) section 8. ABI 31.
Main is 1c09e039 plus the docs commit that follows it.

## State

- Lives: the user allowed autonomous lives on the evening and night of 2026-11-14 while the PC was free. That go does not
  carry over: a new session must ask again. After a go, tell the user and wait for their ok right before launching the runner.
  Pending live evidence is READY_FOR_LIVE, not a pass.
- Installed test app: 0.9.6-hairtest18 (commit 1c09e039) in `%LOCALAPPDATA%\Programs\BG3RTX`. It equals main for
  everything shipped. Setups of hairtest16-18 are in `.scratch/setup-hairtest*`.
- Releases 0.9.4 and 0.9.5 are packaged. The release profile below is unchanged.
- Nothing is queued. No merge is in progress.
- Still pending from the user: in-game confirmation of performance, hair stability and the launcher.

## VRAM right-sizing (night of 2026-11-14)

- Why: the user's sessions reached 26.5 GB used, with a Streamline fence timeout and 26 hitches over 100 ms in 10 s.
  Ours was 10.2-12.6 GB of that.
- On by default now, after the lives vr-s8 and vr-inn: `rt_capture_peak_reserve`, `rt_capture_demand_size`,
  `rt_capture_split_mb`, `rt_mesh_pool_trim`. All live keys, no shader or ABI change, 0 GPU ms.
- Result: VRAM used -0.8 GB at S8 and -1.0 GB at the inn. Zero faults (no dropped draws, no buffer full, guard normal).
  Energy inside noise (-0.25% and -0.18%, floor 0.5%). Shrink hitches 36-56 ms, rare.
- Launch with the defaults was confirmed by hx1 001645: RT capture first buffer 1024 MB, cascade 3 x 427 MB,
  0 frame-watchdog trips, 0 dropped draws, guard quiet.
- `rt_as_chunk_fit` stays 0. Its prediction was disproved (no saving, no harm). `rt_as_cap_occupied` stays 0 (never run).

Open:
- BLAS chunks hold 2.3-2.7 GB for 0.65-0.8 GB live after a dolly at the inn. This is the largest reserve in motion.
  It is untouched by these keys.
- The mesh pool cannot shrink at the inn while a groom sits at its end (2047 MB for 690 used). It needs groom ranges that move.
- The pixel-LOD camera BLAS is not compacted (about 16 MB per groom).
- The user's 26.5 GB sessions predate these fixes. Re-check from their next play log. `[vram]` lines are always on;
  `[mem] pools reserved/used MB` needs `detailed_logging=1`.

## Consolidation round (three streams, offline, merged)

Layer:
- Elastic captures grow by at least 1/8 of their size (was 16 MB quanta: ten 1.4-1.8 GB reallocations became three).
- `rt_hair_groom_async` (default 0, live): groom decode off the build thread. READY_FOR_LIVE, unverified live.
- The shader source scan runs every 5 s in release (was 250 ms). Unchanged with dev=1.
- Streamline: stale layout of a destroyed FG input is erased.

Core:
- The GI images are no longer reallocated twice at every level load (`core/rr_mode_latch.h`). READY_FOR_LIVE: after
  loading a save from the menu the log has no new `gi: resources` line. The startup pair stays.
- Two dead shaders removed (`probe.hlsl`, `rr_hair.hlsl`). `shaders/variants.txt` delta 0.

Tools and launcher:
- The launcher refresh sends only changes. Slider drag fixed. Rendered frame limit starts at 58 fps, not 30.
- The hair converter runs at idle priority while the game runs and stops on Play.
- 640 px layout fixed (all pages reachable). Detail texts are player-facing.
- 22 tests that never ran are now in CTest (16 PowerShell, 6 Python). Two had rotted and are fixed.
- Live lint counts the 17 active jobs only (0 failing); 107 historical jobs are counted apart.
- Scripts name Smart App Control when it blocks fresh exes. Job `post` commands written with `python` run with `py -3`.

## Findings reported, not fixed

- Descriptor-set maps in `layer/tracker.cpp` (515-523) are never erased. Growth is unmeasured.
- Hold snapshots copy 91.5 MB per static-camera build at the 64-byte stride.
- 282 draws are re-captured and refit every frame with a static camera at S8.
- `refresh()` in `core/gi.cpp` runs about 25 times per frame (about 0.1 ms, estimated). It needs a host settings generation (ABI).
- Hidden-hit emission beyond the roof is kept in one branch and dropped in the other
  (`shaders/gi/gi_trace_rt.hlsl:531` against `:460`). It needs a PT A/B.
- NaN-to-65504 select at `gi_trace_rt.hlsl:746` (not reachable today).
- Dead `#if` branches and unused spec ids remain.
- The 750-line hit-GI loop was not read line by line.

## RTX Hair strands

- Instability under camera motion is solved for converted styles (2026-11-14). The RTX Hair On bundle has 26 keys,
  with `rt_hair_header_cam_gpu=1`, `rt_hair_header_scene_pass=1` and `rt_hair_coverage_keep=1`. Old files get them on load.
- Final live hx1 202905 (S8): 0 strands->cards in every bundle window. An entering head shows cards for about 3 frames.
- Open: styles without a groom file stay cards (S8 index counts 3399 and 7086). Hitch frames are fixed offline only.
- `rt_hair_card_fill` is an Advanced option outside the bundle (no visible gain). `rt_hair_dense_sample` is a diagnostic.
- RTX Hair is Off in every preset. In release it is an opt-in Beta control (Lighting > RTX Hair, Ctrl+Shift+F7). The user
  decides whether it goes On in a preset.
- Launcher controls: "RTX Hair detail at distance" (Off / 1/4 / 1/8, live) and "RTX Hair motion stability"
  (`rt_hair_mv_card=2`, On in High and Ultra, restart).
- Staged jobs and their state: `tools/live_batches_staged/README.md` (hx1 in `hair_stable/` is current).

## Scene handout ("Low-latency scene")

- On in High and Ultra by user decision, always with `rt_scene_handout_hit_wait=1`. Off in Low and Medium.
- Scene wait 3.5 -> 0.3 ms at S8 and 8.5 -> 2.9 ms at the inn. Frame time unchanged on Ultra (GPU-bound), about
  -0.5 ms on High at S8. 0 fallbacks. Energy equal. The card says "Saves 0 to 0.5 ms".
- Open: GI trace read 0.2-0.4 ms lower with the handout in one rerun (EXPLAIN). The benefit in CPU-bound scenes is unmeasured.

## Launcher

- "Direction A" with instant apply is in the installed app. The native fallback UI keeps Save and says so.
- No agent has opened the launcher in the real WebView2 window. All rendering checks are headless Edge
  (`node tools/launcher/ui_shots.js <out dir>`, shots in `docs/ui_rework/shots_review/`).
- The minimised-window WebView suspend is not yet seen in the real window. Design docs: `docs/ui_rework/`.

## Gates

- Sensible gates: `AGENTS.md` and `tools/live_batches/sane_gates.py`. FAIL only beyond the OFF noise plus a floor.
- An unpredicted drop is EXPLAIN, not a pass. Short windows are "not judged" instead of making the run INVALID.

## Housekeeping

- Disk cleanup done on the night of 2026-11-14 with the user's approval. About 311 GB of files deleted: old captures,
  `.scratch/hair-c`, old setups and packages, `hair_grooms_v2_20261110`, merged worktrees.
  164 merged remote branches and 531 local ones deleted. Tags untouched.
- ANOMALY: free space on E: still read 482.3 GB afterwards by every unelevated method. Unexplained. The user was asked
  to check in Explorer and with `vssadmin list shadowstorage`.
- Kept: all 8 replay packs (several are referenced), the hairtest16-17 setups, `.scratch/saved_from_worktrees/`.
- Still to review with the user:
  - Worktrees with unmerged commits (under `.claude/worktrees/`): agent-a2c38a4884e982692 (anim-materials),
    agent-a7c43f1e79329fb0f (hair-footprint), fix-rrpost-src (water-glint), hair-edge-still, hair-pixel-lod-study.
  - `codex-hair-lod1` (branch codex/hair-groom-lod1): no unmerged commit, 7 dirty files.
  - Unregistered folders `agent-a0dba2e726bdabdad` and `ui-rework`. Check for junctions before touching them.
  - 24 unmerged remote branches.
- `test-main` is used by merge-green; keep. Remove worktrees only with `scripts/remove-worktree.ps1 -Name <folder>`.
- Untracked in the main checkout, left alone: `tools/live_batches_staged/dlss5/`.

## Release profile (do not change without a user decision)

- Stock visible hair. Ultra: temporal post, exact-k, two GI rays. High: OMM and offline masks.
- Always on: wf9 water camera correction, far-GI roof-leak fix. Low-latency scene On in High and Ultra.
- The four VRAM right-sizing policies are On by default (2026-11-14 night).
- Experimental page (behind "Show experimental options", all off): Water surface reflections, two FG pacing tests, Strand
  hair edge resolve. RTX Hair: Beta control on Lighting, off. Tilesets hidden and off.
- Ctrl+Shift+F9 master RT toggle. GPL-3.0-or-later plus NVIDIA exception. "Tested only on RTX 5090".
- Asset database ships in the archive (user decision). Nothing is uploaded by agents.

## Next work

1. READY_FOR_LIVE, one job each when the user gives a go: `rt_hair_groom_async` (decode ms leave the build, card runs
   stay in the hx1 gate); the `rr_mode_latch` log check after a level load.
2. BLAS chunk reserve in motion (2.3-2.7 GB for 0.65-0.8 GB live at the inn): the largest VRAM item left.
3. The findings above, cheapest first: tracker map erase with a counter line, the hold-snapshot stride, the static-camera re-capture.
4. RTX Hair: unconverted styles, then the preset decision.
5. Deferred: L11, L12, L17, C4/C7/C8/C12, S-F05, T2; SHaRC removal; native Tilesets; water off-screen shading.

## Rules that bit recently

- A cost that falls without a predicted reason is not a gain. Check energy and counts.
- Frame time measured while the user uses the PC is not evidence.
- A job `post` must not rely on `python` resolving (the Store alias made vr-s8 and vr-inn INVALID; the runner now uses `py -3`).
- Give a job's any-line `expect` only lines that its first state prints; per-state lines go in the per-window gates.
- Check blobs over 20 MB before every commit and merge. Generated files stay in `.scratch/`.
- Never run builds or merge-green while the user plays (`captures/queue/USER_PLAYING` or bg3.exe running).
- Follow [AGENTS.md](../AGENTS.md): never saves, backups in D:\BG3_Tools, force-push or snapshot tags;
  remove worktrees only with scripts/remove-worktree.ps1; launch the game only via scripts/test-queue.ps1.

## Open for the user

1. Confirm in play with hairtest18: performance and VRAM, RTX Hair while moving, the new launcher window.
2. The E: free-space anomaly: Explorer and `vssadmin list shadowstorage`.
3. Decide whether RTX Hair goes On in a preset. Unconverted styles: convert them or accept cards.
4. The worktrees, folders and 24 remote branches above: keep, merge or remove.
