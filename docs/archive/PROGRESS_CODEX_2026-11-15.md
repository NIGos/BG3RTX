# BG3RTX progress archive - 2026-11-15 (main 1c09e039)

Full text of the entries that PROGRESS_CODEX.md keeps only in short form after the fold of 2026-11-15:
the five fragments of the night of 2026-11-14 (VRAM right-sizing, its live result, the three consolidation streams),
then the 2026-11-14 sections as they stood in PROGRESS before the fold (their own full text is in
[PROGRESS_CODEX_2026-11-14](PROGRESS_CODEX_2026-11-14.md)).

## VRAM right-sizing: five policies behind keys, replayed on the user's logs, one live job staged (2026-11-14)

Branches `claude/vram-rightsize` and `claude/vram-rightsize-2`. Offline only. No shader or ABI change. All keys default 0, live.
Evidence: the user's play logs of 22:12-22:27 (ours 10.2-12.6 GB of 23-27.5 GB used) and hq-s8-ultra / hq-inn-fix.

| Pool | Cause | Key | Replayed saving |
| --- | --- | --- | --- |
| BLAS chunks | A BLAS over 64 MB gets a chunk of its exact size. The cascade casters' BLAS is rebuilt at a new size when its draw list changes; a larger one fits no retained empty chunk: 18 new chunks, 448 -> 1770 MB for 356 MB live (S8 21:54:48). | `rt_as_chunk_fit`: that chunk is sized + 1/8 in 16 MB steps; the smallest retained chunk that fits is taken. | 1316 -> 160 MB for that event; the user's log had 99-1214 MB of empty chunks in 15 of 19 samples. |
| RT capture | The preset floor (1408 MB at 88 bytes), and the demand counted the 40% alpha region the buffer holds for itself: 1632 MB kept for 130 MB of soup. | `rt_capture_demand_size`: need without the held region, floor one quantum; the first buffer stays the preset. | Mean 1787 -> 1544 MB, low level 1408 -> 832 MB; with the two keys below: peak 2047 -> 1808, mean 1485 MB. |
| Capture presets | Presets follow the 88-byte stride although the wide split keeps every non-strand draw at 64 bytes (0.0 wide casters per frame). | `rt_capture_split_mb`: presets by the base stride (RT 1024, cascade slot 427, alpha floor 171 MB). | First buffers -384 MB (RT) and 3 x -160 MB (cascade); reserve -40 MB per cascade slot after that. |
| Cascade capture | The rise reserve stacked on the high level (first pass). | `rt_capture_peak_reserve` | 1044 -> 769 MB per slot, 3 slots -826 MB. |
| Mesh pool | The repack moves meshes and holds only, so with RTX Hair it never runs (36 "native hair ranges" lines): 1947 MB for 400-1420 MB used. | `rt_mesh_pool_trim`: a shorter pool with the same offsets when its end is free and a quarter is saved, after the same quiet window. | 1947 -> 1353 MB (-594) at 22:24 if no mesh lies above the last groom (1162 MB); no trim at 22:19 (a groom at 1585 MB). |

Expected together: about 1.5 to 2.5 GB less reserved in the user's session (cascade 0.8, RT capture 0.3-0.6, BLAS
0.3-1.1 in motion, pool 0-0.6), from replays, not measured live.

Answers without a change:
- Three cascade buffers: slot F % 3 records frame F while the builds read F-1 and F-2, and each holds one frame's whole
  caster soup. Each is sized from its own samples, which see the same demand. Two slots would need the build to wait
  for the recording: not a sizing change.
- The 88-byte stride is already limited to strand-frame shaders (`rt_capture_wide_split=1`); only the presets were left.
- `rt_as_cap_occupied` (rv5, never run): it lets oversize chunks pass the cap up to + 25%. It lowers refusals, not the
  reserve. With `rt_as_chunk_fit` the oversize requests reuse chunks; keep it at 0 until a live shows refusals remain.

Cost bounds (stated, not measured): a capture demand jumping past its reserve leaves draws out for the replacement
delay (5 frames RT, 3 cascade), then one replacement of at least 1/8, old + new allocated 4 frames. A trim is one
copy of the kept bytes, at most once per 254 frames. A fitted chunk wastes at most size / 8 + 16 MB.
Hysteresis: shrink after 120 low samples with 25% to spare, trim only for a quarter saved (replay: 9 against 8 growths).

`[mem] pools reserved/used MB` (detailed_logging, one line per window): cascade capture, RT capture, BLAS chunks (live,
pending, empty), mesh pool, hit-material recycled, OMM, hair in the pool and BLAS, VRAM used and budget, the keys.

CTests: `as_chunk_alloc_test`, `rt_capture_elastic_test`, `rt_capture_budget_test`, `vram_compact_test`.
READY_FOR_LIVE: `tools/live_batches_staged/vram_rightsize/` (vr-s8 293 s, vr-inn 514 s after load; predicted.json first;
lint live and frozen 0 failing). Not queued, not authorized.

Risks: the trim switches the pool inside the build like a growth (a dropped command buffer would leave the new pool
without its copy, as for a growth today); the capture policies can drop draws for a few frames at a cut.
Not done: pixel-LOD camera BLAS compaction (about 16 MB per groom before compaction; needs a second query and copy per
groom in the groom build path, no offline test); moving groom ranges in a repack; the ms of one replacement.

## VRAM right-sizing live result: 4 of 5 policies qualify, -0.8 to -1.0 GB, no fault (branch claude/vram-result, READY to default, 2026-11-14)

Runs `vr-s8.1.232616` and `vr-inn.1.233221` (Ultra, RTX Hair On, FG 3x, visible unfocused, 22 of 22 phases, 0 occluded
checks). INVALID only for the post gate: the job's `post` starts with `python`, which cmd resolved to the Store alias
("Python was not found"), so `analyze.py` never ran. Fix: the runner sends a leading `python` through `py -3` (34 job
files use it; dryrun job `l2-post-python`). Run by hand, the analyzer reports 0 missing observations on both logs.
Reserved MB (used MB in the first column); OFF = the off-a and off-b states:

| Pool | S8 OFF | S8 ON | Inn OFF | Inn ON | Saving S8 / inn |
| --- | ---: | ---: | ---: | ---: | --- |
| Cascade capture, 3 slots (535-816) | 1136-1408 | 1088 | 1424-1584 | 1152 | -50 to -320 / -270 to -430 |
| RT capture (257-565) | 1408 | 736 | 1776-2047 | 1184, dolly peak 1776, then 1408 | -672 / -590 to -860, 0 to -270 at the peak |
| Mesh pool (520-1270) | 1024, 1455 | 727, 1017 | 2047 | 2047 | 2 trims, -297 and -438 / 0 (deferred) |
| BLAS chunks, dolly mean (450-810) | 1042-1347 | 1356 | 2354-2774 | 2694 | none (empty chunks 0 / -270) |
| Hit-material, OMM, hair | 168, 5, 820 | same | 0-300, 5, 800 | same | 0 |
| VRAM used, all windows (budget 31419) | 16422-16456 | 15648 | 18587-18895 | 17697 | -790 / -1040 (tracked -710 / -800) |

- Against `predicted.json`: static 0.3-0.9 GB predicted, 0.8 and 1.0 measured; moving 0.7-2.0 predicted, 0.74 (S8) and
  0.47 (inn, inside the OFF pair's 0.58) measured. Disproved: `rt_as_chunk_fit` (empty chunks 0-200 MB predicted, up
  to 911 MB seen; 22 chunks created in the inn dolly-out with the key, 22 and 23 without).
- Faults in the plan, both runs, every state: draws dropped (buffer full) 0.00, cascade buffer full 0.0, stride split
  left out 0.00, guard normal, 0 refusals, 0 scene wait timeouts, 0 handout fallbacks, 0 strands to cards, no `VK_ERROR`.
- Demand past the reserve: only the inn dolly-out with the keys on (opaque 126 -> 558 MB): two growths (1184 -> 1424
  -> 1776 MB), one deferred on the fence, 0 draws left out. S8: no growth in any ON window.
- Hitches (per-frame `[stats]` intervals, 33 ms cap): trims 38 and 48 ms, RT shrinks 45-51 ms, cascade 36-56 ms. Inn
  dolly-out max 234 / 248 / 209 ms, 851 / 1079 / 1130 ms over 50 ms (off-a / on / off-b): the dolly's own BLAS
  builds, also without a replacement. Pans and statics: max 38-48 ms in every state.
- GPU ms equal: S8 static RT 10.98 on, 11.38 off-b; scene build 2.93, 2.91. Inn RT 12.93, 12.93; build 2.77, 2.71.
- Energy (4 float traces per state, exposure held): S8 on 833.14 in OFF 830.36-838.72 (-0.25%); inn on 151.10 in OFF
  148.77-155.28 (-0.18%); under the 0.5% floor. Confound: the dolly-in ends closer than the start (S8 12 -> 13 grooms,
  inn 11 -> 8), so for statics and pans ON is compared with off-b.
- Verdict: `rt_capture_demand_size` on (-0.6 to -0.9 GB). `rt_capture_peak_reserve` on (-0.05 to -0.43 GB).
  `rt_capture_split_mb` on with them (toggled together, no fault; its smaller first buffers at launch were not run:
  confirm in the next authorized live with the keys on from the start). `rt_mesh_pool_trim` on (no hitch); no effect
  when a groom sits at the pool's end (inn: 2047 MB for 690 used; needs groom ranges that move). `rt_as_chunk_fit`
  stays 0: no saving, no harm. The branch's last commit turns the four on (drop it to keep them off). Cost: 0 GPU ms.
- Left: after a dolly the inn's BLAS chunks hold 2.3-2.7 GB for 0.65-0.8 GB live in every state (partly filled
  chunks, back to 0.9-1.0 GB after about 15 s still): the largest reserve in motion, not touched by these keys.
- User check: `detailed_logging=1`, play, read `[mem] pools reserved/used MB` (`policies ...=1`; RT capture 700-1200 MB at rest,
  not 1408-2047); Task Manager, dedicated GPU memory of bg3.exe 0.8-1 GB lower in the same place.

## Consolidation, stream C: launcher review, tooling health, docs (branch claude/consol-tools, offline, 2026-11-14)
- Launcher process health: the 2 s refresh sent the whole state (30 KB) every tick and the page rebuilt itself each
  time, also beside the running game. It now sends a state only when it changed; a minimised window hides and
  suspends its WebView (not yet seen in the real window). The hair converter runs at idle priority while the game runs;
  Play stops the shader precompile and the converter at once instead of at the next tick. No handle leak found
  (snapshots, job objects and process handles are closed; GDI objects are per window).
- Launcher bugs fixed, with tests: a slider drag was cut by the timer refresh; Rendered frame limit On started at 30 fps
  (now the preset's 58 fps or the last value); a bridge message stayed in the status strip until the next successful
  write; clicking the current preset wrote the file and offered an empty Undo; a stopped conversion showed its error twice.
- Launcher layout, headless Edge with the sample data, 20 pages and states at 1280x800, 1000x700 and 640x700
  (`node tools/launcher/ui_shots.js <out dir>` audits clipped and overlapping boxes: 0 problems now; 24 reviewed
  screenshots in `docs/ui_rework/shots_review/`). Fixed: at 640 px the tab row hid four pages behind Play (icons with
  the current page's name now); a long restart badge ran under its control.
- Detail and cost texts a player reads are plain English (RTX Hair and its two sub-settings, Low-latency scene, tracing
  speed, DLSS 5, the experimental options); the technical notes stay in the comments of `common/settings.h`.
  The native fallback window keeps Save and says so in its title and status line.
- Settings table cross-check: all 355 keys of `bg3rtx.ini` are read by layer or core (3 are launcher keys), every knob key
  is read, no alias or opt-in entry names a missing knob, `enforce_release_policy` has no stale entry,
  `docs/USER_SETTINGS.md` matches the 27 release controls. New test `test_old_user_files`: the hairtest8 file (RTX Hair
  On without the three stability keys, plus two removed keys) and a preset-only file load as choices, complete the
  bundle and save to the same choices.
- Live lint: "124 jobs, 71 failing" were 71 historical jobs older than the rules of 2026-11-05. The whole-tree run
  now gates the 17 active jobs (0 failing) and counts 107 historical ones apart (71 with findings).
- Tests that nothing ran are in CTest: 16 offline PowerShell tests (`ps_suite_test`, 21 s) and 6 Python tests. Two had
  rotted unseen: `queue_child_permission_test.ps1` (failing since 2026-10-31, the test's own regex) and
  `merge_green_test.py` (failing since 2026-11-06, fixtures lacked the tooling stand-ins). Both fixed.
  Still by hand: `queue_cancellation_test.ps1`, `queue_child_startprocess_test.ps1` (they start the real runner on a
  stand-in tree), `readiness_cached_replay.ps1`, and six `*_shader_test.py` identity gates.
- CTest slowest: release_lifecycle 55 s, hair_groom 45 s, merge_green 25 s, frame_replay 24 s, release_install 21 s.
- Scripts: `remove-worktree.ps1` refuses outside the main checkout, refuses test-main, and leaves an unregistered
  folder in place; `build.ps1 -Test` and the Setup check name Smart App Control when it blocks fresh executables;
  `package.ps1` also checks the built exe's version, before creating the output folder.
- Staged batches: `tools/live_batches_staged/README.md` lists each batch's state; hair_cam, hair_late and hair_final
  removed (dead keys, superseded by hair_stable).
- Docs: current-state summary at the top of `RTX_HAIR_PRIMARY.md` (path, controls, the 26-key bundle, defaults, open
  items) and of `SCENE_HANDOUT_DESIGN.md`; 8.8 records the rerun hq-s8-ultra 215258 (+4.4 ms p99 was external load).
- Not done: the launcher was not opened in the real WebView2 window (no launch of the app in this pass); the converter's
  disk I/O priority is unchanged (only CPU priority drops); no hygiene deletion was made (list in the final report).
- Check: drag Mod lighting strength while Preparation converts (the thumb stays in hand); Rendered frame limit On
  gives 58 fps; at the minimum window width all pages are reachable; minimise during play, restore: the page is current.

## Consolidation, stream A (host layer): stutter and memory from the user's play logs (2026-11-14)

Evidence: the installed app's `bg3rtx.log` / `bg3rtx.prev.log` of 22:12-22:27 (Ultra, FG 2x, RTX Hair On, 3840x1600)
and the live logs hq-s8-ultra 215258 and hq-inn-fix 211816. Offline only. No shader or ABI change.

Fixed (branch `claude/consol-layer`):
- Elastic capture growth. The RT and cascade captures grew in 16 MB quanta, each growth a whole new buffer with the
  old one kept for four frames: 22:15:10-23 has ten RT reallocations of 1.4 to 1.8 GB for +16 to +112 MB, and the
  VRAM guard went LOW twice with old + new allocated ("RT capture 3520 MB", "3615 MB"). A growth is now at least
  1/8 of the size when the budget allows it (`capture_budget::elastic_growth`, `vram_fits`): that sequence takes
  3 reallocations and ends at 2016 instead of 1840 MB; no shrink back. CTest `rt_capture_elastic_test` replays it.
  Cost in ms of one reallocation: unmeasured (needs a live window).
- `rt_hair_groom_async` (new key, default 0, live). Groom LOD read + LZMS decode ran inside the scene build:
  228 decodes in the two sessions, median 47 ms, max 203 ms, 15.6 s in all (179 were repeats after the Ctrl+Shift+F7
  toggles and evictions). With the key they run on a below-normal worker (at most 4 outstanding); the draw keeps
  its cards a few frames longer. CTest `hair_groom_decode_test`. READY_FOR_LIVE: decode ms leave the build
  (`[rt] scene wait` max, `[pace]` max in a town walk); card runs of entering heads must stay within the hx1 gate.
  Stream C: add `rt_hair_groom_async=0` to `bg3rtx.ini` and regenerate the defaults.
- Shader source scan (`shader_poll`, present thread, 160 files): every 5 s with dev=0, 250 ms with dev=1 as before.
  About 0.3 to 0.8 ms per scan (Python proxy 0.76 ms), 4 times a second before.
- Streamline: `g_layout` / `g_input_layout` kept the layout of a destroyed FG input rotated out of the watch ring
  (stale layout for a reused handle, one entry per image). Erased with the other tag metadata.

Found, not fixed (ranked):
1. VRAM. Ours is 10.2 to 12.6 GB of 23 to 27.5 GB used (budget 30 GB); 26 hitches over 100 ms in 10 s at 22:19:16
   start with a Streamline `waitCPUFence` timeout at 26.5 GB. Largest parts: cascade capture 3 x 1088 MB for a
   510-580 MB demand (the rise reserve never decays: one camera cut of about 500 MB is kept for the session); RT
   capture 2047 MB for about 900 MB; BLAS chunks 2.6-3.9 GB reserved for 0.8-1.3 GB live (`rt_as_cap_occupied`
   is the staged answer); mesh pool 1947 MB with 640-920 used (shrink deferred by "native hair ranges"). The 88-byte
   capture stride of RTX Hair applies to every draw (1408 MB start instead of 1024). All need an A/B: policy, not bugs.
2. Hold snapshots: 247 copies, 91.5 MB per build, every static-camera build (hq-s8-ultra: 10.98 GB per 120 builds)
   at the 64-byte stride. The 24-byte pack exists only for the 88-byte layout (`dyn_hold_stride`). GPU ms unmeasured.
3. `Tracker::sets` / `dynamic_counts` / `set_layouts` are never erased on FreeDescriptorSets / ResetDescriptorPool /
   DestroyDescriptorPool (layer/tracker.cpp:515-523); `layout_dynamic` never on DestroyDescriptorSetLayout. Growth
   depends on the driver reusing handle values: unmeasured, needs a counter line in a long session.
4. Captured draws: 282 per build, all refit and re-captured every frame with a static camera (convert + extract
   0.66 ms, cascade pass 0.69 ms, BLAS 0.36 ms GPU; capture hooks 6.1 ms CPU over all threads, 4.2 in pre-draw).
5. Small: `log_stats` gate = 5 locked string lookups per pass; `cfg_int` per cascade and water draw; `log()` flushes
   per line under one mutex; `hair_primary_note_pipelines` builds up to 33 pipelines inside the game's create call.

## Consolidation, core and shaders: GI images no longer reallocated twice per level load; two dead shaders removed (branch claude/consol-core, offline verified, READY_FOR_LIVE, 2026-11-14)

- Evidence first: the play logs of the installed hairtest17 (`bg3rtx.log`, `bg3rtx.prev.log`) and the 12 newest queue logs.
- Fixed: every level load allocated the GI images twice. `rr_active` is true while RR evaluated in the last 3 presents; a
  loading screen is longer, so the first lit frame saw "inactive", the 13 GI images (about 180 MB at 2560x1067) were
  rebuilt at half resolution and again at full resolution on the next frame, and the history was cleared twice.
  12 of 12 queue sessions and both play sessions show the pair (`gi: resources ... (GI at 1280x534)` then `(GI at 2560x1067)`,
  60 to 150 ms apart) at startup and again at each load.
- Fix: `core/rr_mode_latch.h`. The GI keeps its RR mode while RR is inactive for less than 30 presents, counted from the
  first frame the GI sees it inactive. RR does denoise that first frame, so RR-mode input is the right one for it.
  A real fallback (SR, FSR, TAA) switches after the hold. `gi_rr` 0/1 still overrides at once. CTest `rr_mode_latch_test`.
- Image: steady state unchanged. The first frame after a load now gets the same GI as the frames after it (full
  resolution, `gi_rr_rays`), not half-resolution accumulation over an empty history. No shader change, no new key.
- Cost: CPU only. Saves 13 image allocations, 13 deferred frees and one history clear per level load. Not measurable offline.
- Live check (one observable, any save): after loading a save from the menu the log has no new `gi: resources` line
  (before: two). The startup pair stays (RR does not exist yet on the first frame).
- Removed: `shaders/probe.hlsl` (its pipeline was removed in 4dda0c72) and `shaders/rr_hair.hlsl` (rr_hair_resolve
  retired in 2d072e42). Nothing requests or includes them. `shaders/variants.txt` delta 0; merge-green compiles 2
  standalone files less. No other request changes, so no identity run was needed.

Looked at and found clean (no change):
- Pipelines: no rebuild without a cause. Per session: about 30 builds at startup (first frames, 16 to 190 ms bursts), one
  `_em` to plain hit-GI switch at a level load (104 ms present interval inside the load), 22 builds in 66 ms when the user
  pressed the RTX Hair hotkey (old pipelines kept meanwhile). Layout changes destroy their variants.
- Memory: every core image and buffer is allocated once or on a size change and freed through `defer_destroy`. No growth.
  All GI images are allocated whatever is on (about 180 MB on Ultra at 2560x1067); skipping some needs dummy bindings.
- Shaders: stores to history, previous radiance, hit distance and RR guides are guarded. Visibility rays end at the
  first hit; the groom trace is behind constant 1020 and the stamp's instance count; the origin pass skips sky.
- Small passes cost 0.02 to 0.04 ms each (temporal, composite, AO fold): nothing to gain there.

Not fixed (reasons):
- `gi_trace_rt.hlsl:746` maps a NaN to 65504 (`select(x <= max, x, max)`). Not reachable today (every term is guarded).
  A finite-only guard changes every hit-GI module: do it with the next change of that file.
- Hidden-hit emission beyond the roof bound is kept (`gi_trace_rt.hlsl:531`) while the screen branch drops it (:460).
  Both are commented as intended; a PT-reference question, not a bug fix.
- `refresh()` runs about 25 times per frame with about 15 `cfg_int` and 6 `persist` lookups each (mutex, hash, string
  copy). Estimated 0.1 ms CPU per frame, unmeasured. Needs a settings generation from the host (ABI) to cache safely.
- Dead branches kept because offline tools still define them: `RT_GI_FAR`, `RT_CASCADE_MASKS`, `RT_HIT_INSTANCED`.
  Defined nowhere: `RT_SCENE_REBASE`, `RT_REBASE_LIGHT_CAMERA`, `GI_DEBUG_SWIZZLE`, `GI_FAR_M` (removal is
  byte-identical by construction; left for a pass that already recompiles `rt_common.hlsli`).

## 2026-11-14, afternoon and evening

### RTX Hair instability under camera motion: solved for converted styles (branches claude/hair-stable, hair-residual, hair-adopt, hair-final-report, hair-cleanup)
- What the user saw (play log of hairtest13, about 2 minutes, Ultra): 960 of 2927 hair frames with every hair draw on cards,
  139 switches to cards, 161 draws under a tenth of their card pixels. Still camera: 0 switches.
- Cause 1: the header camera was read on the CPU and lagged one camera step (0.08-0.57 m); `rt_hair_header_cam_gpu` was 0.
  Fix: RTX Hair On writes `rt_hair_header_cam_gpu=1` (old files get it on load).
- Cause 2 (6h): the header was applied at the first pass with 4 or more colour targets, and the decal pass matches too. When the
  present fell between the scene G-buffer pass and the decal pass, the next frame's hair traced the previous publish's TLAS:
  52-76% of all frames in hx1 190905 (165 of 217 and 131 of 183 in the dollies). Fix: `rt_hair_header_scene_pass=1`, in the
  bundle (only the 5-target scene pass applies the header). The "same build" reading of 6e was wrong (stamp copied at that apply).
- `rt_hair_coverage_keep=1` in the bundle (26 keys): coverage evidence does not expire by age. "Coverage stale" refusals
  16-35 -> 0. A head entering the view shows cards for 10 frames without it, typically 3 with it (5 before the handshake change, 6k).
- Final live hx1 202905 (S8, 16 of 16 phases; INVALID only for a wrong control gate, replayed ok): all six bundle windows
  0 strands->cards, 0 frames on cards, 0 watchdog trips. Card runs min / median / max with keep: 3 / 3 / 3 in four windows,
  3 / 3 / 9 in in-on, 3 / 6 / 6 in orbit-r (the 9 and the 6 are unexplained). Keep off: median 9-10, max 72.
- hx1 194658 (25 of 25 phases, VALID): strand draws 97.4-99.1% of the hair draws; dense samples moving 79-84% accepted, inside
  the still spread (control without the key: 59% and 70%). Cost: strand G-buffer 0.13-0.20 ms zoomed out, 0.27-0.35 ms near.
  The two new keys are CPU only: GPU cost 0, no shader or ABI change, variants.txt delta 0.
- Removed with code, knobs, ini lines and tests: `rt_hair_stale_publish`, `rt_hair_row_stable`, `rt_hair_watchdog_draw`,
  `rt_hair_watchdog_one_key`, `rt_hair_swap_latch`, `rt_hair_coverage_min_pixels` (all read 0 in the qualifying run; old user
  files still load). The switches log line now has seven causes.
- `rt_hair_card_fill` (card under missing strands, restart, spec 1053): Advanced option, not in the bundle. It draws 4.5-5.6%
  of the card pixels; final image equal within 1 of 255, no visible gain (pairs in `docs/hair_fill/`).
  `rt_hair_dense_sample` stays as a diagnostic. Disproved: LSS refit as the cause (61347 of 61347 hits kept).
- Open: styles without a groom file stay cards (S8 index counts 3399, 7086). Hitch frames (two scene passes in one present
  interval) are fixed offline only (CTest `test_header_pass`), never seen live; the first one of a session writes one
  `[hair primary] hitch frame` line. The 2 build-latency frames of an entering head stay. The key-off header path stays as the A/B.
- RTX Hair is still Off in every preset (user decision pending). Check: S8, RTX Hair On alone, zoom fast with the wheel and
  orbit: heads in view keep strands; a head that enters shows cards for about 3 frames.

### Scene handout, "Low-latency scene": On by default in High and Ultra (branches claude/handout-on, handout-energy, handout-modes, handout-fix)
- User decision "Si, accendilo", then "Tienilo acceso" after the corrected numbers. Preset row Off / Off / On / On, a normal
  Performance control, always with `rt_scene_handout_hit_wait=1` (code default 1, release policy keeps it, old files load with it).
- First qualification (handout_qual, focused, FG 3x): inn Ultra 30.0 -> 26.5 ms, S8 Ultra 20.9 -> 19.9, S8 High pan 12.1 -> 11.6.
  The Ultra gains were mostly dropped work: the commit-time hit-material query was never published on handout frames
  (`[hit material submit] ordered 0`), so hit GI traced almost no light rays at hits (0.14 M against 3.84 M). Energy -0.20% at
  S8 (hq-s8-energy 191419, 4.7 standard errors), GI trace 4.83 -> 3.49 ms (S8), 8.2 -> 4.7 ms (inn). High was not affected.
- Fix `rt_scene_handout_hit_wait=1`: hit-material passes of a handout frame take the committed pair (one wait).
  hq-s8-modes 205520: 3.83 M rays, diffuse +0.064%, wait 3.5 -> 0.28 ms, frame 21.58 against OFF 21.29 / 21.49, GI trace equal.
  hq-inn-fix 211816: wait 8.5 -> 2.9 ms, frame 30.0 -> 30.1 (noise 0.35), energy +0.06%, 0 fallbacks, 0 ON timeouts.
- Honest result: frame time unchanged on Ultra (both scenes GPU-bound), about -0.5 ms on High at S8, 0 fallbacks, energy equal.
  Card text now "Saves 0 to 0.5 ms"; the "1 to 3.5 ms" claim is withdrawn.
- Rerun hq-s8-ultra 215258 (PC idle, 26 of 26 phases): EXPLAIN, no failure. Wait 3.4-3.7 -> 0.16-0.24 ms. Hair-pan frame time
  p99 +1.2 ms inside a noise of 6.0, so the afternoon's +4.4 ms (run 160909) was external load: the user was using the PC.
- Open: GI trace reads 0.2-0.4 ms lower with the handout in that rerun (static -0.36, pan -0.22, energy windows -0.38;
  EXPLAIN; not reproduced in 205520, where it was equal). Benefit in CPU-bound scenes unmeasured.
- Check: restart on High or Ultra; `[rt] scene wait` near 0.3 ms and `hit material passes on the committed scene N` with N
  above 0; Performance, Low-latency scene Off (live) to compare.

### Sensible gates (branches claude/handout-energy, handout-modes, gates-refine; rule in AGENTS.md, `tools/live_batches/sane_gates.py`)
- FAIL only beyond the OFF noise plus a floor (cost max(0.1 ms, 1%), energy 0.5%, match 1 point); INFO between noise and noise
  plus floor; an unpredicted drop is EXPLAIN; a p99 takes the full range of the OFF periods as noise; events in OFF windows are
  the baseline; equal counts allow the OFF spread plus 1%; short windows are "not judged" instead of an INVALID run.
- hq-inn-fix 211816 had 6 false FAILs, now 0. Still failing by design: mode 2 without the fix, +1 ms at 21 ms, a fallback.
- `live_flow.PRODUCER_SETTINGS`: GI far needs `rt_stats=1`, the core handout line needs `rt_scene_handout=2` (the stop of
  hq-s8-modes 204217 was this job fault). tests/sane_gates_test.py, 10 tests. No code or shader change.
- Disk: trace folders `.claude/worktrees/test-main/captures/20261114-191625` to `-191832` (24, 1.17 GB) are analysed, deletable.

## 2026-11-14, morning

### Performance investigation (branches claude/perf-ab, claude/cpu-hooks, claude/vram-guard-latch)
- Release profile today vs 2026-11-05 (static windows, GPU ms, Ultra): GI much cheaper (S8 12.10 -> 8.20, inn 21.18 -> 15.99,
  Dialogue1 14.12 -> 10.94). Scene build +0.1 to +0.7 ms (S8 1.18 -> 1.84, inn 2.49 -> 2.59, inn High 2.14 -> 2.66). Local shadows,
  reflections, RR within about 0.3 ms (noise). Wide split 0 vs 1 differs by only 0.05-0.1 ms. Camp save "Campsite - 27h" no longer exists.
- RTX Hair On costs about +1.4 ms frame time at the inn (28.9 -> 30.3 ms, focused, FG 3x). Base CPU hooks 12.13 vs 12.39 ms: no regression.
- Likely cause of the user's "very low fps": the VRAM guard latched LOW after the OS halved the budget (31419 -> 15477 MB, log of 2026-11-13).
  Fixed (`common/vram_guard.h`): drop memory of two stats windows, slack capped by what the yielders can free, one trim ask per episode,
  log lines `OS VRAM BUDGET TOO LOW` and `budget covers this process again`. Unit-test replay of the log. Unconfirmed in play.
  User check: alt-tab away and back; one `headroom LOW` line, then `headroom back`.
- Hair-on per-draw global mutex removed (`hair_primary::DrawFilter`): about 6900 non-hair draws per frame took `g_mu`.
  Offline model only: 0.20 -> 0.04 ms/frame (1 thread), 1.15 -> 0.07 (4 threads).
- The S8 perf-ab job timed out on pipeline readiness; DLSS 5 cost not measured. Staged: `tools/live_batches_staged/perf_ab/`.
- Queue runner: `test-queue.ps1` pruned logs older than 3 days, then the preflight refused every job. Fixed: `keep_logs.py` keeps the
  newest 3 VALID logs per save; an empty corpus lints with the frozen snapshot. The hair live logs of 2026-11-10/11 are gone.
- Windows Smart App Control turned itself on at 14:06 and blocked fresh test exes and a setup; the user turned it off at 14:18.

### Launcher UI rework (branch claude/ui-rework, merged) and review (claude/launcher-review, 2026-11-11)
- "Direction A": sidebar with Play, setting rows in groups with indented children, detail panel, cost chip, amber restart badge.
- Instant apply: a click writes at once (no Save, no review). The notice names what else changed, with Undo. A failed write changes
  nothing and offers Retry. The native fallback UI is unchanged (still has Save). In the installed app.
- Rendered only in headless Edge (`docs/ui_rework/shots_impl/`); not reviewed by the user in the real WebView2 window. Detail text
  of some settings is still long. Check: `tools/launcher/ui/index.html?sample&chrome=1`.
- Review fixes: RTX Hair Off sticks; version from the package `VERSION.txt`; reports no longer "Partial" for old ini files; converter fixes.
