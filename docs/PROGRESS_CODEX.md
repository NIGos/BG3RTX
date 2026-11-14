# BG3RTX progress - 2026-11-15 (main 1c09e039)

Live tests: the user allowed autonomous lives on the evening and night of 2026-11-14 while the PC was free. That go is
spent: a new session must ask again, and tell the user right before launching the runner. Pending live evidence is READY_FOR_LIVE.
State and next steps: [HANDOFF_SESSION](HANDOFF_SESSION.md). RTX Hair: [RTX_HAIR_PRIMARY](RTX_HAIR_PRIMARY.md) 6g-6k.
Scene handout: [SCENE_HANDOUT_DESIGN](SCENE_HANDOUT_DESIGN.md) 8.5-8.8.
Full text of the 2026-11-14 entries below: [archive 2026-11-15](archive/PROGRESS_CODEX_2026-11-15.md) (night),
[archive 2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md) (day); older: [2026-11-11](archive/PROGRESS_CODEX_2026-11-11.md).
Installed test app: 0.9.6-hairtest18 (commit 1c09e039) = main for everything shipped.

## 2026-11-14, night: VRAM right-sizing and the consolidation round

### VRAM right-sizing: four policies On by default (branches claude/vram-rightsize, vram-rightsize-2, vram-result)
- Evidence: the user's play logs of 22:12-22:27 (Ultra, FG 2x, RTX Hair On): ours 10.2-12.6 GB of 23-27.5 GB used, and
  26 hitches over 100 ms in 10 s starting with a Streamline `waitCPUFence` timeout at 26.5 GB.
- Five policies behind live keys, no shader or ABI change, 0 GPU ms:
  - `rt_capture_demand_size`: RT capture sized by the need without the held 40% alpha region. On (-0.6 to -0.9 GB).
  - `rt_capture_peak_reserve`: the rise reserve no longer stacks on the high level. On (-0.05 to -0.43 GB).
  - `rt_capture_split_mb`: presets by the 64-byte base stride (RT 1024 MB, cascade slot 427 MB). On.
  - `rt_mesh_pool_trim`: a shorter pool with the same offsets when its end is free and a quarter is saved. On.
  - `rt_as_chunk_fit`: stays 0. Predicted 0-200 MB of empty chunks, up to 911 MB seen; 22 chunks created in the inn
    dolly-out with the key, 22 and 23 without. Prediction disproved: no saving, no harm.
- Live `vr-s8.1.232616` and `vr-inn.1.233221` (Ultra, RTX Hair On, FG 3x, 22 of 22 phases; INVALID only because the
  job's `post` used `python`, which hit the Store alias; the analyzer run by hand reports 0 missing observations).
  VRAM used 16422-16456 -> 15648 MB at S8 (-0.8 GB) and 18587-18895 -> 17697 MB at the inn (-1.0 GB).
  RT capture 1408 -> 736 MB (S8), 1776-2047 -> 1184 MB (inn; dolly peak 1776). Cascade, 3 slots: -50 to -430 MB.
- Faults in every state of both runs: 0 dropped draws, 0 buffer full, guard normal, 0 refusals, 0 scene wait timeouts,
  0 handout fallbacks, 0 strands to cards, no `VK_ERROR`. GPU ms equal (S8 RT 10.98 on, 11.38 off; inn 12.93, 12.93).
- Energy inside noise: S8 -0.25%, inn -0.18% (floor 0.5%). Shrink hitches 36-56 ms (trims 38 and 48 ms), rare.
  The inn dolly-out has 209-248 ms hitches in every state: the dolly's own BLAS builds.
- Launch with the defaults confirmed afterwards (hx1 001645, coordinator's reading, no fragment): RT capture first
  buffer 1024 MB, cascade 3 x 427 MB, 0 frame-watchdog trips, 0 dropped draws, guard quiet.
- Open: after a dolly the inn's BLAS chunks hold 2.3-2.7 GB for 0.65-0.8 GB live (back to 0.9-1.0 GB after about
  15 s still): the largest reserve in motion, untouched. The mesh pool cannot shrink at the inn while a groom sits at its
  end (2047 MB for 690 used). Pixel-LOD camera BLAS not compacted. `rt_as_cap_occupied` stays 0 (never run).
  The user's 26.5 GB sessions predate these fixes: re-check from their next play log.
- Check: `[vram]` lines are always on; `detailed_logging=1` adds `[mem] pools reserved/used MB` (RT capture 700-1200 MB
  at rest, not 1408-2047). Task Manager: dedicated GPU memory of bg3.exe 0.8-1 GB lower in the same place.

### Consolidation, host layer (branch claude/consol-layer, offline; no shader or ABI change)
- Elastic capture growth: captures grew in 16 MB quanta, each a whole new buffer (ten RT reallocations of 1.4-1.8 GB
  for +16 to +112 MB at 22:15, guard LOW twice). A growth is now at least 1/8 of the size when the budget allows:
  the same sequence takes 3 reallocations and ends at 2016 instead of 1840 MB. CTest `rt_capture_elastic_test`.
- `rt_hair_groom_async` (new key, default 0, live): groom read and LZMS decode on a below-normal worker instead of inside
  the scene build (228 decodes in two sessions, median 47 ms, max 203 ms). The draw keeps cards a few frames longer.
  READY_FOR_LIVE, unverified live: decode ms must leave `[rt] scene wait` max; card runs must stay in the hx1 gate.
- Shader source scan on the present thread: every 5 s with dev=0 (was 250 ms; 0.3-0.8 ms per scan). Unchanged with dev=1.
- Streamline: the layout of a destroyed FG input that left the watch ring is erased (stale layout on a reused handle).

### Consolidation, core and shaders (branch claude/consol-core, offline, READY_FOR_LIVE)
- Every level load allocated the 13 GI images (about 180 MB at 2560x1067) twice, at half then full resolution, and cleared
  the history twice: `rr_active` goes false during a loading screen. Seen in 12 of 12 queue sessions and both play sessions.
- Fix `core/rr_mode_latch.h`: the GI keeps its RR mode while RR is inactive for less than 30 presents. A real fallback
  (SR, FSR, TAA) switches after the hold; `gi_rr` 0/1 overrides at once. CTest `rr_mode_latch_test`. No shader change, no key.
- Check: after loading a save from the menu the log has no new `gi: resources` line (before: two). The startup pair stays.
- Removed `shaders/probe.hlsl` and `shaders/rr_hair.hlsl` (nothing requests them). `shaders/variants.txt` delta 0.
- Found clean: no pipeline rebuild without a cause; no core memory growth; history and guide stores are guarded.

### Consolidation, launcher and tools (branch claude/consol-tools, offline)
- Launcher: the 2 s refresh sends a state only when it changed (was 30 KB and a page rebuild every tick). A slider drag
  is no longer cut by the refresh. Rendered frame limit On starts at the preset's 58 fps (was 30). The hair converter
  runs at idle priority while the game runs; Play stops the precompile and the converter at once.
- Layout audited in headless Edge at 1280x800, 1000x700 and 640x700 (`node tools/launcher/ui_shots.js <out dir>`,
  0 problems; shots in `docs/ui_rework/shots_review/`). At 640 px all pages are reachable. Detail texts are player-facing.
- Tests: 16 PowerShell tests (`ps_suite_test`) and 6 Python tests that nothing ran are in CTest; two had rotted and are fixed.
  New `test_old_user_files`. All 355 keys of `bg3rtx.ini` are read by layer or core (3 are launcher keys).
- Live lint gates the 17 active jobs (0 failing) and counts 107 historical ones apart (the old "71 failing" were historical).
- Scripts: `build.ps1 -Test` and the Setup check name Smart App Control when it blocks fresh exes; `remove-worktree.ps1`
  refuses outside the main checkout and refuses test-main; a job `post` written with `python` runs through `py -3`.
- Not done: the launcher was never opened in the real WebView2 window; the minimised-window suspend is unseen there.

### Findings reported, not fixed
- `layer/tracker.cpp:515-523`: `Tracker::sets`, `dynamic_counts`, `set_layouts` are never erased on free, reset or pool
  destroy; `layout_dynamic` never on layout destroy. Growth unmeasured (needs a counter line in a long session).
- Hold snapshots copy 91.5 MB per static-camera build at the 64-byte stride (247 copies; the 24-byte pack exists only at 88).
- 282 draws are re-captured and refit every frame with a static camera at S8 (capture hooks 6.1 ms CPU over all threads).
- `refresh()` in `core/gi.cpp` runs about 25 times per frame (about 0.1 ms CPU, estimated); needs a host settings generation (ABI).
- Hidden-hit emission beyond the roof bound is kept at `shaders/gi/gi_trace_rt.hlsl:531` and dropped at `:460`: needs a PT A/B.
- `gi_trace_rt.hlsl:746` maps a NaN to 65504 (not reachable today). Dead `#if` branches (`RT_SCENE_REBASE`,
  `RT_REBASE_LIGHT_CAMERA`, `GI_DEBUG_SWIZZLE`, `GI_FAR_M`) are left for a pass that already recompiles `rt_common.hlsli`.

### Disk cleanup (night, with the user's approval; coordinator's report, no fragment)
- About 311 GB of files deleted (old captures, `.scratch/hair-c`, old setups and packages, `hair_grooms_v2_20261110`,
  merged worktrees); 164 merged remote branches and 531 local ones deleted; tags untouched.
- ANOMALY: free space on E: still read 482.3 GB afterwards by every unelevated method. Unexplained; the user was asked to
  check in Explorer and with `vssadmin list shadowstorage`.
- Kept: all 8 replay packs (several are referenced), the hairtest16-17 setups, `.scratch/saved_from_worktrees/`.

## 2026-11-14, day (one line each; full text in [archive 2026-11-14](archive/PROGRESS_CODEX_2026-11-14.md))
- RTX Hair instability under camera motion solved for converted styles: `rt_hair_header_cam_gpu=1`, `rt_hair_header_scene_pass=1`, `rt_hair_coverage_keep=1` in the 26-key bundle; hx1 202905: 0 strands->cards in all six windows; entering heads show cards for about 3 frames. Six obsolete keys removed. Still Off in every preset.
- Open for hair: styles without a groom file stay cards (S8 index counts 3399, 7086); hitch frames fixed offline only; `rt_hair_card_fill` is an Advanced option with no visible gain.
- Scene handout ("Low-latency scene") On in High and Ultra with `rt_scene_handout_hit_wait=1`: scene wait 3.5 -> 0.3 ms (S8), 8.5 -> 2.9 ms (inn); frame time unchanged on Ultra, about -0.5 ms on High; energy equal; 0 fallbacks. The first "1 to 3.5 ms" gain was dropped work and is withdrawn.
- Sensible gates (`tools/live_batches/sane_gates.py`): FAIL only beyond the OFF noise plus a floor; an unpredicted drop is EXPLAIN.
- Performance investigation: the release profile is not slower than on 2026-11-05 (GI much cheaper); the VRAM guard latched LOW after the OS halved the budget, fixed in `common/vram_guard.h`, unconfirmed in play; per-draw hair mutex removed; queue log pruning fixed.
- Launcher "Direction A" with instant apply merged and installed; rendered only in headless Edge.

## RTX Hair, 2026-11-10/11
- Motion stability ON in High and Ultra (`rt_hair_mv_card=2`, user decision; needs a restart). hm1 camera-only share 11% (off 98%), ratio 0.77.
- Detail at distance (pixel LOD, Off / 1/4 / 1/8, applies live): heads 35-60 px keep 1/4; strand G-buffer -27% (1/4), -34% (1/8). Camera BLAS about 16 MB per groom, not compacted.
- Holes under a fast dolly: cause 1 found then (`rt_hair_header_cam_gpu`), cause 2 on 2026-11-14 (above). Other branches of those days: see the archive.

## Current user decisions
- Stock visible hair ships; strand hair hidden until its post-release update. Experimental page in release: only Water surface reflections (off).
- The game-derived asset database ships in the archive. GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification. "Tested only on RTX 5090".
- Nexus: setup in a ZIP plus portable ZIP; manual download. Lean: only the tests that are really needed. Optimisations that qualify are always on.
- 2026-11-11: RTX Hair motion stability On in High and Ultra. 2026-11-14: launcher Direction A, instant apply, dark only.
- 2026-11-14: Low-latency scene On in High and Ultra, always with the hit wait fix ("Si, accendilo", "Tienilo acceso").
- 2026-11-14: sensible gates (fail beyond noise plus floor). Lives allowed that evening and night while the PC was free; ask again in a new session.
- 2026-11-14 night: the four VRAM right-sizing policies On by default after the live result; disk cleanup approved.
- Pending: RTX Hair On in a preset; `rt_hair_card_fill` stays an Advanced option unless the user wants it.

## Older entries (2026-11-04 to 2026-11-09; full text in the archives [2026-11-11](archive/PROGRESS_CODEX_2026-11-11.md), [2026-11-05](archive/PROGRESS_CODEX_2026-11-05.md), [2026-11-04](archive/PROGRESS_CODEX_2026-11-04.md))
- Releases: 0.9.3-beta, 0.9.4-beta (RC PASS), 0.9.5-beta packaged 2026-11-08; build.ps1 prefers VS 2022; packaging on a mounted-folder disk; launcher picks the RTX GPU.
- Scene handout design and increments 1-5 (2026-11-08/09), mesh cache budget charged with its own BLAS, BLAS chunk caps, VRAM reserve from the observed rise.
- RTX Hair 2026-11-04 to 11-09: groom storage v3 and converter `bg3rtx_hairconv`, swap and join fixes, strand fibre shading, edge quality, warm-up,
  switchable while playing (Ctrl+Shift+F7), header camera from the GPU, strand-resolve pairing, capture stride split, M1-M5 quality items (mostly READY_FOR_LIVE).
- Lighting: RT pipeline GI trace (on), compact GI hit state (always on), early roof rejection (on), exact local-shadow optimisation, RF probe work (mixed),
  SER feasibility, wide split on by default, rise reserve on, Home dashboard and Hotkeys page.
- Rejected: far GI live state, conservative GI-only TLAS, GI origin audit, BLAS flags replay, sun wave, default Ultra RF follow-ups.
- Audits (fixed offline): DLSS 5, FG, core completion contract, L11/L12/L17 submission, suspect review, S-F05 R11G11B10, ReShade isolation, queue cancellation.
