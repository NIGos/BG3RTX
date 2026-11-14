# Session handoff - 2026-11-14 evening

Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) first (top: RTX Hair stability, scene handout, sensible gates). Design and history:
[RTX_HAIR_PRIMARY](RTX_HAIR_PRIMARY.md) 6g-6k, [SCENE_HANDOUT_DESIGN](SCENE_HANDOUT_DESIGN.md) section 8. ABI 31.
Main is d2cceda0 plus the docs commit that follows it.

## State

- Lives: on the evening of 2026-11-14 the user allowed autonomous lives while the PC was free. That go does not carry over:
  a new session must ask again. After a go, tell the user and wait for their ok right before launching the runner.
  Pending live evidence is READY_FOR_LIVE, not a pass.
- Installed test app: 0.9.6-hairtest17 (commit 6199932b) in `%LOCALAPPDATA%\Programs\BG3RTX`. Main d2cceda0 adds only gate
  refinements and docs, so the installed app is current. Setups of hairtest1-17 are in `.scratch/setup-hairtest*`.
- Releases 0.9.4 and 0.9.5 are packaged. The release profile below is unchanged, except Low-latency scene (see below).
- Nothing is queued. No merge is in progress.

## RTX Hair strands: instability solved for converted styles

The user saw heads popping to cards and holes while the camera moved (hairtest13 play log: 33% of hair frames all on cards).
Two causes, both fixed and in the RTX Hair On bundle (26 keys; old files get them on load):
1. The header camera was read on the CPU and lagged one camera step. Fix: `rt_hair_header_cam_gpu=1`.
2. The hair header was applied at the decal pass when the present fell before it. Hair then traced the previous publish's
   TLAS in more than half of all frames (52-76% in hx1 190905). Fix: `rt_hair_header_scene_pass=1`.
Also in the bundle: `rt_hair_coverage_keep=1`. A head entering the view shows cards for typically 3 frames (10 without it).

- Final live hx1 202905 (S8): every bundle window has 0 strands->cards, 0 frames on cards, 0 watchdog trips.
  Card runs of entering heads: median 3 in five of six windows; two runs of 6 and 9 frames are unexplained.
- Six obsolete keys are removed with their code: `rt_hair_stale_publish`, `rt_hair_row_stable`, `rt_hair_watchdog_draw`,
  `rt_hair_watchdog_one_key`, `rt_hair_swap_latch`, `rt_hair_coverage_min_pixels`. Old user files still load.
- Cost: the two new keys are CPU only (GPU 0). Strand G-buffer 0.13-0.35 ms. No shader or ABI change.

Open:
- Styles without a groom file stay cards (S8: index counts 3399 and 7086). Not an instability; they need conversion.
- Hitch frames (two scene passes in one present interval) are fixed offline only (CTest `test_header_pass`). Never seen
  live. If one happens, the log has one `[hair primary] hitch frame` line per session.
- `rt_hair_card_fill` stays an Advanced option outside the bundle: no visible gain (pairs in `docs/hair_fill/`).
- `rt_hair_dense_sample` is kept as a diagnostic. The key-off path of `rt_hair_header_scene_pass` is kept as the A/B.
- RTX Hair itself is still Off in every preset and hidden in release. The user decides whether it goes On in a preset.
- Older open items (unchanged): camera BLAS for pixel LOD not compacted (about 16 MB per groom); above 60 px the k rule
  over-covers about 15%; `lint_snapshot` needs a merge mode; possible full build at the `layer/bvhsplit.cpp` refit fallback.

Launcher controls (Lighting page): "RTX Hair detail at distance" (Off / 1/4 / 1/8, presets Off, live) and "RTX Hair motion
stability" (`rt_hair_mv_card=2`, On in High and Ultra, restart).
Staged hair jobs: `tools/live_batches_staged/hair_stable/` (hx1, current). hc1, hk1, hl1, hf2 are historical; pl1 is not current.

## Scene handout ("Low-latency scene")

- On by default in High and Ultra by user decision ("Si, accendilo", then "Tienilo acceso" after the corrected numbers).
  Always with `rt_scene_handout_hit_wait=1` (code default, release policy keeps it). Off in Low and Medium.
- Keep the history honest. The first qualification showed Ultra gains of -1.0 ms at S8 and -3.5 ms at the inn. Most of that
  was dropped work: native hit materials were not published on handout frames (energy -0.20%).
- With the fix: scene wait 3.5 -> 0.3 ms at S8 and 8.5 -> 2.9 ms at the inn. Frame time unchanged on Ultra (GPU-bound).
  About -0.5 ms on High at S8. 0 fallbacks. Energy equal. The launcher card says "Saves 0 to 0.5 ms".
- Rerun hq-s8-ultra 215258 (PC idle): no failure. Frame time p99 with RTX Hair in motion +1.2 ms inside a noise of 6.0.
  So the afternoon's +4.4 ms was external load (the user was using the PC).
- Open: GI trace reads 0.2-0.4 ms lower with the handout in that rerun (EXPLAIN). It was equal in hq-s8-modes 205520.
- Open: the benefit in CPU-bound scenes is unmeasured. Both qualified scenes are GPU-bound on Ultra.
- SCENE_HANDOUT_DESIGN 8.8 still lists the +4.4 ms FAIL as never re-examined: update it with the rerun.
- Other unshipped optimisations worth a live: `rt_mesh_budget_own` (rv3 rerun), `rt_as_cap_occupied` (rv5), camera BLAS compaction.

## Performance investigation (morning)

- The user reported very low fps (Ultra, FG 3x, RTX Hair On). The release profile is not slower than on 2026-11-05
  (GI much cheaper, scene build +0.1 to +0.7 ms).
- Most likely cause: the VRAM guard latched LOW after the OS halved the budget. Fixed (`common/vram_guard.h`), in the
  installed app, unconfirmed in play.
- Per-draw hair mutex removed (`hair_primary::DrawFilter`). Offline model only, no live number.
- Queue log pruning fixed: the newest 3 VALID logs per save are kept. The hair live logs of 2026-11-10/11 are gone.
- Windows Smart App Control turned itself on at 14:06 and blocked fresh exes; the user turned it off at 14:18.
  If fresh exes are blocked again, check it first.
- Not done: DLSS 5 cost; the camp save "Campsite - 27h" no longer exists. Staged: `tools/live_batches_staged/perf_ab/`.

## Gates

- Sensible-gates rule: `AGENTS.md` and `tools/live_batches/sane_gates.py` (used by handout_qual and perf_ab).
- FAIL only beyond the OFF noise plus a floor. An unpredicted drop is EXPLAIN, not a pass: find its cause.
- Events in OFF windows are the baseline. Short windows are "not judged" instead of making the run INVALID.
- `live_flow.PRODUCER_SETTINGS` knows which settings a log line needs (GI far: `rt_stats=1`; core handout line: mode 2).

## Launcher

- "Direction A" with instant apply (no Save; dependency notice with Undo; restart badge) is in the installed app.
- Not yet reviewed by the user in the real window. The native fallback UI is unchanged (still has Save).
- Detail-panel copy is still long for some settings. Design docs: `docs/ui_rework/`.

## Housekeeping

- Trace folders `.claude/worktrees/test-main/captures/20261114-191625` to `-191832` (24 folders, 1.17 GB) are analysed.
  They can be deleted after checking they hold no reparse points. Never a recursive delete through a junction.
- Registered worktrees with uncommitted work or commits not on main (under `.claude/worktrees/`, not reviewed):
  agent-a2c38a4884e982692 (anim-materials), agent-a507a735412f7f20f (hair-m5-m2), agent-a7c43f1e79329fb0f (hair-footprint),
  agent-a84a839fb14568d30 (hair-m1-rerun), agent-ae065cb9955e96e59 (hair-m4-locals), codex-hair-lod1, fix-rrpost-src (water-glint),
  hair-edge-still, hair-pixel-lod-study, hair-reload-fix (release-licences). `test-main` is used by merge-green; keep.
  Remove only with `scripts/remove-worktree.ps1 -Name <folder>`.
- Leftover folders that are not registered worktrees: `launcher-review` and `confident-diffie-fb694f` (empty), `ui-rework`,
  `gi-livestate` and `agent-a0dba2e726bdabdad` (with files). Check for junctions before touching them.
- Untracked in the main checkout, left alone: `bg3rtx_ui.ini`, `tools/live_batches_staged/dlss5/`, `tools/live_batches_staged/reshade_compat/`.

## Release profile (do not change without a user decision)

- Stock visible hair. Ultra: temporal post, exact-k, two GI rays. High: OMM and offline masks.
- Always on: wf9 water camera correction, far-GI roof-leak fix. Low-latency scene On in High and Ultra (2026-11-14).
- Experimental: only Water surface reflections (off). Strand hair hidden. Tilesets hidden and off.
- Ctrl+Shift+F9 master RT toggle. GPL-3.0-or-later plus NVIDIA exception. "Tested only on RTX 5090".
- Asset database ships in the archive (user decision). Nothing is uploaded by agents.

## Post-release queue

1. RTX Hair strands: unconverted styles, hitch frames if they show up, then the preset decision.
2. Deferred findings: L11, L12, L17 (submission-dependency tracking), C4/C7/C8/C12 (core completion contract),
   S-F05 (R11G11B10 emulation), T2 (queue cancellation of descendants).
3. SHaRC removal after separating its device capability (layer/rtscene.cpp, water.cpp, hair_primary.cpp).
4. Native Tilesets, opaque hidden-hit reflections, water off-screen shading: see the backlog and TRACK docs.

## Rules that bit recently

- A cost that falls without a predicted reason is not a gain: the handout's first numbers were dropped work. Check energy and counts.
- Frame time measured while the user uses the PC is not evidence (+4.4 ms p99 vanished on an idle PC).
- Give a job's any-line `expect` only lines that its first state prints; per-state lines go in the per-window gates.
- Check blobs > 20 MB before every commit and merge. Generated files stay in `.scratch/`.
- Never run builds or merge-green while the user plays (`captures/queue/USER_PLAYING` or bg3.exe running).
- Follow [AGENTS.md](../AGENTS.md): never saves, backups in D:\BG3_Tools, force-push or snapshot tags;
  remove worktrees only with scripts/remove-worktree.ps1; launch the game only via scripts/test-queue.ps1.

## Open for the user

1. Confirm in play with hairtest17: performance (VRAM guard fix, Low-latency scene), RTX Hair while moving, the new launcher window.
2. Decide whether RTX Hair goes On in a preset.
3. Unconverted hair styles: convert them or accept cards for those.
4. The old worktrees and leftover folders above: keep, merge or remove.
