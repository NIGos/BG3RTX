# BG3RTX progress - 2026-11-05

## Report: optimisation night 2026-11-05 (main, release candidate rc-20261105-2aa5dcf8 PASS 12/12)

**Question for the user (only one): pubblichiamo la 0.9.1?** Files: `.scratch/rc-test/rc-20261105-2aa5dcf8-30de6495/`
(Setup.zip, Setup.exe, portable ZIP). Nothing uploaded.

**Qualified, always on** (in-run A/B at the inn, 600-frame windows, OFF repeats as noise):
- `rt_scene_compact_holds`: High scene build -0.87 ms GPU (OFF spread 0.075); energy within OFF spread; BVH identical by fixtures.
- `cpu_opt1`: layer hook bodies -0.13 ms/frame CPU (spread 0.016); BVH build -0.2 ms.
- `cpu_opt2`: -0.30 ms/frame CPU (spread 0.088). `cpu_opt3`: -0.24 ms/frame CPU (spread 0.054).
- Total CPU about -0.67 ms/frame at the inn on Ultra (layer hooks 12.4 -> about 11.7 ms/frame).

**Before/after on the release build (static, ms GPU, profile runs 00:09-00:21 vs 06:51-07:50, all VALID):**
| Scene | Preset | Scene build | GI block | Local shadows | Reflections | RR | Sum of these |
|---|---|---|---|---|---|---|---|
| Water Queens House | Ultra | 1.19 -> 1.18 | 12.20 -> 12.10 | 0.69 -> 0.59 | 1.09 -> 1.10 | 2.60 -> 2.52 | 17.77 -> 17.49 (-0.27) |
| Water Queens House | High | 1.92 -> 1.27 | 2.06 -> 2.24 | 0.13 -> 0.12 | 0.41 -> 0.44 | 2.48 -> 2.48 | 7.01 -> 6.55 (-0.45) |
| Inn | Ultra | 2.45 -> 2.49 | 21.27 -> 21.18 | 2.03 -> 2.00 | 2.12 -> 2.11 | 2.53 -> 2.54 | 30.39 -> 30.32 (-0.07) |
| Inn | High | 3.42 -> 2.14 | 3.09 -> 3.07 | 1.07 -> 0.87 | 0.53 -> 0.58 | 2.61 -> 2.57 | 10.71 -> 9.23 (-1.48) |
| Camp | Ultra | 2.07 -> 2.06 | 17.82 -> 16.53 | 1.69 -> 1.64 | 1.63 -> 1.56 | 2.49 -> 2.43 | 25.69 -> 24.22 (-1.48) |
| Camp | High | 2.10 -> 2.25 | 3.87 -> 4.34 | 1.03 -> 0.97 | 0.46 -> 0.56 | 2.49 -> 2.52 | 9.96 -> 10.63 (+0.67) |
| Dialogue1 | Ultra | 1.76 -> 1.76 | 14.03 -> 14.12 | 1.35 -> 1.25 | 1.16 -> 1.18 | 2.61 -> 2.54 | 20.91 -> 20.85 (-0.07) |
| Dialogue1 | High | 2.14 -> 1.68 | 2.74 -> 2.80 | 0.80 -> 0.83 | 0.35 -> 0.36 | 2.50 -> 2.43 | 8.54 -> 8.10 (-0.44) |

Totals per preset (sum of the four scenes): Ultra 94.76 -> 92.88 ms (-1.88, mostly camp GI); High 36.22 -> 34.51 ms (-1.71,
mostly scene build at the inn and S8 from `rt_scene_compact_holds`). Single static windows from separate runs: differences under
about 0.3 ms per pass are run-to-run noise (camp High GI +0.47 is in a pass no qualified key touches: noise, not a regression).
CPU: about -0.67 ms/frame at the inn on Ultra (in-run A/B above).

**Release smokes (release profile, visible_unfocused_fg, all VALID):** Ultra S8 116 s; High S8 116 s (08:31, after removing gates
for water and cascade casters, which High does not run); stock-hair final dialogue 124 s.

**Release candidate:** rc-20261105-2aa5dcf8 PASS 12/12 (verify_rc: ZIP and setup). The merges after it (report, smoke-job fixes)
touch only `docs/`, `tools/` and `tests/`: no shipped file changed, so the candidate stays valid without a rebuild.

**Rejected with numbers** (code removed): gi_roof_reject (inn GI 21.75/21.58 OFF vs 21.87/21.95 ON), rt_origin_lazy_normal
(no saving), cpu_opt4 (+0.47/+0.54 ms CPU, slower). Dropped offline: GI temporal current-sample, reflection/local-shadow
early-outs (no exact proof or under 0.05 ms); ray flags already first-hit. Rounds stopped when a round found no win.

**Not reached:** Ultra GI trace (8-13.5 ms) stays the largest GPU cost; per-feature attribution needs a live tool that sees
cached pipelines (post-release). Next per user: bug fixing.

Details: [OPTIMIZATION_20261105](OPTIMIZATION_20261105.md), [RC_CHECK](release/RC_CHECK.md).

## Current user decisions

- Stock visible hair ships; strand hair hidden until its post-release update.
- Experimental page: only Water surface reflections (Fresnel), off.
- The game-derived asset database ships in the archive (user's own risk on data rights).
- GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification. "Tested only on RTX 5090".
- Nexus: setup in a ZIP (main file) plus portable ZIP; manual download, no Vortex.
- Lean: only the tests that are really needed; one final live on the user's go.

Full folded branch notes: [2026-11-05](archive/PROGRESS_CODEX_2026-11-05.md), [2026-11-04](archive/PROGRESS_CODEX_2026-11-04.md).
- Optimisations that qualify are always on; no player option.

Full notes of the night: [archive](archive/PROGRESS_CODEX_2026-11-05-night.md); release 0.9 report: [archive](archive/PROGRESS_CODEX_2026-11-05.md).

## RTX Hair motion stability ON in High and Ultra (branch claude/hair-mv-preset, 2026-11-11; user decision)

- Key: `hair_motion_stability` On = `rt_hair_mv_card=2` (docs/RTX_HAIR_PRIMARY.md 5y). Preset row {0, 0, 1, 1}:
  High and Ultra On, Low and Medium Off. bg3rtx.ini default 0 -> 2 (High = the developer defaults, settings_test).
- data/release.ini: `rt_hair_mv_card=0` removed (a release.ini value is the code default, which must equal High).
  Release players: the preset the launcher saves writes On for High and Ultra, Off for Low and Medium, as the presets;
  a file without a preset follows bg3rtx.ini (High): On. A player's own Off/On choice survives the release policy.
- Gates: hm1 210705 (static, mode 2) camera-only share 11% (off 98%), hair/ring MV ratio 0.77 (cards 0.55); hf2 000619
  (Ultra, all new keys, orbit burst) camera-only share 3.2%, hair/neighbour ratio 1.00.
- Cost: strand G-buffer 0.22-0.31 ms, no regression. Effect only with RTX Hair on (Off in every preset).
- Turning it on from Off needs a game restart (specialization 1049 at pipeline creation); a preset change mid-session
  to High/Ultra shows "Restart to apply". Off is live.
- User check: Ultra, S8 at isometric distance, characters walking or the camera turning: hair no longer smears or
  trails behind moving heads.
- Tests: settings_test `test_hair_motion_stability` (preset rows, defaults read as High, release per preset); schema.

## Holes when a groom joins while others swap: nearest-strand trace (branch claude/hair-v3-settle, offline, 2026-11-10)

- hs1 125217: not v3 and not the warm-up test. The trips at 2171-2176 start with the session's first joins of three
  more hair draws (45252, 100101, 7092) after the warm-up ended; existing keys lose their strands. 103633 had the same
  arrivals inside its warm-up window (0 trips). Same mechanism as the "groom joins mid-play" item.
- GPU fixture (hair_primary_gpu_test nearest_check, twin groom, card-kept copy 3 cm in front): the strand G-buffer's
  restart loop (kHpSkips 3, restart 2 cm past the other strand) leaves 2567 of 25478 strand pixels empty and returns a
  farther strand on 11154 more.
- Fix `rt_hair_groom_nearest` (off; specialization 1047, header flag 64 live off-switch): one non-opaque query that
  passes over other rows' candidates and commits accepted ones (shaders/rt/hair_primary_nearest.hlsli). Fixture: 25478
  of 25478 exact, 0 holes, 0 card-kept hits. Key off: 223 identical + G-buffer equivalent with 1047 frozen; variants 0.
- hs1 staged job: key on for the whole run (pipeline-creation key). Predicted: 0 frame watchdog trips in the arrival
  period; GPU cost from the strand G-buffer line (0.133 ms/frame before). Details: docs/RTX_HAIR_PRIMARY.md 5u.

## RTX Hair strand/card alternation: switch counter in the lean log, retry latch behind a key (branch claude/hair-swap-transitions, offline, not live-verified, 2026-11-10)

- User evidence: hair toggles between strands and cards with a still camera, also with fg=0 (FG ruled out), worse
  far away; zoomed in it flickers, is stable about 0.5 s, repeats. 28 frames (`kWatchdogFrames`) at 58 fps are 0.48 s.
- From the code, two retry loops exist without any scene change: the frame watchdog (28 frames of cards, 2 to 3 to
  resume, verdict 5 to 6 frames later: about 35 to 37 frames per cycle while the fault lasts) and the per-key
  coverage gate (a held key at the minimum ratio fails about 16% of measurements by count noise, then retries on the
  next passing probe: card runs of about 3 frames). The user log has one 31-frame trip pair (2746 -> 2777) and
  near-empty strand frames; the per-trip line is capped at 8, so the period is not proven from it.
- Added, always on, lean too, CPU only: `[hair primary] switches (last 300 frames)` with switches by cause, frames
  with every draw on cards, strand and card run lengths (min / median / max), frame watchdog trip detail and the 3
  worst keys (ratio as strands, as cards, card pixels). `hair_primary::SwapTransitions`.
- Key `rt_hair_swap_latch` (off; launcher "Strand hair: no strand retries", Advanced): a key whose strands measured
  outside its bound keeps its cards until its card pixel count differs from the reference by more than
  sqrt(C) + sqrt(C0). Limit: a noise-only failure stays on cards in a still scene.
- Gates: build.ps1 -Test all pass; `test_swap_transitions` and `test_swap_latch` in hair_groom_asset_test; key off
  runs the old gate answers (same test sequence off and on). No shader or ABI change, variants.txt delta 0.
- To confirm (user session, key off then on in one scene): card runs near 28 to 31 frames with cause "frame
  watchdog", or short card runs with "coverage low"; with the key on, switches near 0 and latched keys > 0.
- Details: docs/RTX_HAIR_PRIMARY.md 5t.

## Strand holes under a fast camera: stale publish check, cards for that frame (branch claude/hair-stale-publish, READY_FOR_LIVE, 2026-11-10)

- hl1 210357 (VALID): all 5 frame watchdog trips applied the previous build frame's publish (k = 1) with its header
  camera one step (0.34-0.70 m) from the frame's, VP eye 0; static windows 0 trips.
- Cause: under the wheel dolly the game submits some G-buffer passes before their frame's scene build; the newest
  publish ordered before them is the previous frame's (the builds rewrite points and BLASes in place: the only
  consistent one). The pick counter called that "this frame's publish" (any upgrade counts), hence 5z's "58 of 61".
- The brief's option (b) already exists (hp_rebase: rays start at gCam.position - header camera). The trips are its
  first non-zero use and it lost the strands; why is not decidable offline. Option (a), waiting for the frame's build,
  would block the render thread on its own later submit.
- Fix (CPU only, no shader, no ABI, variants delta 0), key `rt_hair_stale_publish` (off): at the G-buffer's submit the
  applied publish's build camera is compared with the G-buffer's own camera (its gCam UBO +208, trusted when its VP eye
  is at the origin; else its frame's build camera). Over 1 cm (hp_on_card's fixed band term), or unknown for an older
  publish, every recorded slot draws its card that frame (existing card mode; no coverage evidence, no watchdog
  judgement). One card frame instead of 28. Key off: counted and logged only.
- New lines: `stale publish check (last N frames)` per period and the first 8 `stale publish: G-buffer frame F ...`
  (offset, camera source, UBO VP eye, UBO camera against the build camera of the same frame).
- CTest hair_groom_asset_test test_stale_publish. hl1 regenerated with the key on (predicted.json fix_6a_stale_publish):
  expect_every watchdog trips 0 in lower and watch, the stale check line acting in lower; lint 0 failing (live, frozen;
  one readiness query appended to lint_corpus_snapshot.json).
- Live hl1 should show: 0 frame watchdog trips; "camera moved since the publish" >= 1 and "drawn as cards" >= 1 in lower;
  0 moved in static windows. The UBO-vs-build camera distance then says whether the rebase (Y about 0) or +208 is wrong.
- User check (once qualified): S8, zoom the wheel fast; the hair keeps its strands except at most a single card frame.

## rt_hair_stale_publish carded every frame: unknown camera now traces, gCam looked up at any hair draw (branch claude/hair-stale-camera, READY_FOR_LIVE, 2026-11-10)

- hf2 225504 (key on): "camera not known 60, drawn as cards 60" in every period, every G-buffer applied build frame N - 1
  by label, still camera included: strands were cards in every frame (its 0 trips mean nothing).
- k = 1 is the normal label pairing (G-buffer label frame_index + 1 at recording, build label frame_index at the build:
  the build labelled N is never noted at G-buffer N's submit). 6a's claim that k = 1 caused the 210357 trips is withdrawn.
- Not known because the gCam UBO was looked up only after a swapped draw, matched by command buffer: the first frame
  drew no strand, then carded frames gave no coverage evidence and nothing swapped again (lock-in).
- Fix: lookup at any main-view G-buffer hair draw, matched by G-buffer frame, at most 4 tries; the label fallback no
  longer decides; only a measured move over 1 cm cards a frame, an unknown camera traces as before. Period line names
  why a camera was not measured; the first 4 checks are logged whatever the verdict.
- rt_hair_mv_card, rt_hair_pixel_lod, rt_hair_row_stable: no effect on submit order (velocity specialization / build
  content only).
- CTest test_stale_publish updated (unknown policy, label indexing, reasons). Key stays off until a live shows the
  G-buffer camera measured and equal to the publish camera on a still camera.

## Stable character slots for same-style hair, FG check staged (branch claude/hair-slot-identity, READY_FOR_LIVE, 2026-11-10)
- hs1 015416 confirmed 5q: key 8c68 (two rows) lost both rows in 6 frames, 5 of them on a slot jump frame; jumps only
  on that key, in pairs. Not the owner test (one key, one draw slot): the trace wrote no strand entry for either row.
- New key `rt_hair_slot_stable` (off; launcher "Strand hair: stable character slots", Advanced): the occurrence index
  of a groom-joined hair draw follows the character's placement (model matrix read on the CPU at classify) instead of
  the draw order; a character alone keeps its slot. Layer only, no shader variant (delta 0), no ABI change.
- Counter line "[hair groom] slot exchanges resolved": draws kept against the draw order, captures swapped, captures
  with a draw missing or new, groups left in draw order. Cost: CPU only.
- Unverified until a live: that the two characters' hair draws carry different model matrices. Gate: key on, resolved
  > 0 and 8c68 dropouts and jump pairs 0; key off in the same run reproduces them.
- Test: hair_groom_asset_test (swap, both moving, one leaving, return, third character). Build and CTests: 224 of 224 (settings_test after regenerating data/settings_schema.json).
- Frame generation (user: heads switch on and off quickly, numbers clean): FG gets per-frame binary strand depth and
  motion vectors (they follow the jitter), under a stable colour; `fg` is live-toggleable when started with fg >= 2.
- Staged, not queued: tools/live_batches_staged/hair_fg `hf1-s8-hair-fg`, 98 s: windows fg=3, fg=0, fg=3, fg=3 with
  rr_hair_strand_aa=0, fg=3 with cards, 8 s each plus 12 presented frames. The user says where the toggling stops.
- Details: docs/RTX_HAIR_PRIMARY.md 5s.

## RTX Hair: shared style key and one-key watchdog (branch claude/hair-shared-key, READY_FOR_LIVE, 2026-11-10)
- hc1 2026-11-09 (S8 turns): review item 11 "CPU reads the build camera late" disproved (0 of 120 publishes differ at
  submit; `rt_hair_header_cam_gpu=1` did not remove the trips; `rt_hair_watchdog_draw=1` worse).
- Cause: the dropouts are the keys with two groom rows (7a27 86, 5285 41, 93ea 13 of ~190 entries): two characters
  with one hairstyle share one swap key (index range) while the scene keys grooms per occurrence. A published key
  swapped the draw whose own groom was out of this publish (LOD upload, held soup, first update): 0 strand pixels,
  then a frame watchdog trip put every hair on cards for 28 frames.
- New `rt_hair_shared_key` (off, Advanced `exp_hair_shared_key`): a key publishes only when its keyed rows cover all
  the capture's groom-matched draws with it; else all its draws keep cards this frame. CPU only, no shader, no ABI.
- New `rt_hair_watchdog_one_key` (off, Advanced `exp_hair_watchdog_one_key`): one bad key next to a passing draw
  cards that key only for 28 frames instead of tripping the whole frame.
- Tests: hair_groom_asset_test test_watchdog extended (shared key, one-key watchdog, key-off unchanged).
- hs1 2026-11-09 INVALID: burst 99300 completed with "transport INVALID" (frame gap) because the pan pipelines
  compiled inside it (677 ms pipeline, 5.5 s DXC); 99301/99302 ok. Job fault: warm-inputs now 8 s with
  pan_prepare=1, settles keep pan_prepare=1 (make_job.py regenerated, live_flow 0 failing).
- Live check (not run): S8 turns, keys off / shared / shared+one_key / off; expect the dropouts line without
  7a27/5285/93ea, "watchdog trips" near 0, "shared-key rows withheld" > 0 while turning.

## Stable groom rows: rt_hair_row_stable (branch claude/hair-row-stable, READY_FOR_LIVE, 2026-11-10)

- Live hl1 164930 decided the 5u candidates: A (another build's TLAS traced) disproved (75 submits checked, 0), C (full
  BLAS builds) disproved (0), B confirmed: 11 of 11 keyed rows held another key in the first frame of the wheel zoom,
  then 7, 4, 4, 0, 7 ...; frame watchdog at G-buffer frames 2125, 2126, 2128, 2129, 2130, 39 dropouts.
- Cause of the order change: a groom's row is its position in the dynamic draw list, which layer/bvhsplit.cpp sorts by
  key only inside `if (hold_frames && !moving)`; with a moving camera it is the game's capture draw order, which
  alternates. Nothing kept a key's row.
- Not established: which consumer pairs a row with another publish. Every layer consumer read resolves instance, key
  map and row from the one header applied to its frame; the TLAS check is 0. Stable rows remove the exposure anyway.
- Fix: `hair_primary::GroomRows` (common/hair_primary.h), used for the groom TLAS instances, header rows and InstInfo
  rows. A groom keeps its row while live; a freed row is reused 8 build frames later (kSceneSlots); gaps are inactive
  instances with key 0. Host key `rt_hair_row_stable`, live, off by default (the consumer is not proven, so key off is
  not provably a bug); launcher: Advanced, "Strand hair: stable strand rows".
- The first-seen diagnostic counts only rows that held a key before and hold another now.
- Test: hair_groom_asset_test replays the arrival (row dropped, newcomers, alternating draw order): 0 moved rows, 0
  rows naming different grooms in consecutive publishes, freed row reused only after the lag.
- Cost: CPU under 1 us per build, no GPU pass, no shader change (variants delta 0, no ABI change).
- Staged, not authorized: hl1-s8-late-arrival sets the key; expect_every `holds another key than before 0` in the
  "lower" window. Predicted (predicted.json): 0 moved rows; 0 watchdog trips and 0 "rejected after the hit" dropouts
  after the zoom starts if the order was the whole cause; otherwise candidate D and a per-groom points check next.
- User check: S8, several characters in view, key On, zoom with the wheel: no holes at the start of the zoom.

## RTX Hair On was inert in release: policy reset fixed (branch claude/hair-release-policy, offline, READY_FOR_LIVE, 2026-11-10)
- Bug (0.9.5-beta, dev=0): RTX Hair On drew no strands. `enforce_release_policy` reset the developer-only hair knobs
  and with them 10 of the 18 keys the release control's On choice writes: rt_hair_groom (exp_hair_groom),
  rt_hair_groom_shadow_t, rt_hair_groom_ao_t, rt_hair_groom_cascade_drop, rt_hair_fibre, _gi, _spec, _mapped (their
  exp_* knobs), rr_hair_guides (exp_rr_hair_guides), rt_hair_physical_origin (exp_hair_physical_origin).
- Fix (common/settings.h, 9 lines): the selected choice of a release control keeps the file's values of the keys it
  writes through the developer reset. Generic rule; the only choice it affects is exp_hair_groom_primary On (pinned by
  the test). Off, a stale raw rt_hair_groom=1 alone, and dev=1 behave as before. The developer knobs stay hidden.
- No new key, no shader change (variants.txt delta 0), no ABI bump. The launcher already showed On and saved the whole
  set; `rt_hair` (Ctrl+Shift+F7) still gates the On list.
- Tests: settings_test `test_release_rtx_hair` (10 FAIL lines without the fix, passes with it); `build.ps1 -Test`:
  224 of 224 CTests passed (13 min 19 s).
- Left: enabling the toggle exposes the open strand flicker (RTX_HAIR_PRIMARY 5n, 5p) to release players; decide
  whether the next package ships it On-capable or waits for hs1. A file with only rt_hair_groom_primary=1 (the mark)
  shows On in the launcher but runs the partial keys it has until saved (as before).
- User check: release package, RTX Hair On, Dialogue1: `[cfg] keys` shows rt_hair_groom=1 and strands are drawn.

## Strand holes at the zoom start: pool reuse (D) disproved, camera-step diagnostic (branch claude/hair-pool-reuse, READY_FOR_LIVE diagnostic, 2026-11-10)

- Disproved before this branch by lives: A (another build's TLAS traced: TLAS check 0 in every line, 164930/180301),
  C (full BLAS builds: 0 of 11 at the trip frames), B (row order: with rt_hair_row_stable=1 every first-seen line in
  180301 and 180949 shows 0 moved rows, holes remain: trips at G-buffer 2089-2095 and 2152-2158, down to 0 strand px).
- D (newcomer upload / free / eviction / pool growth overwrites or races an existing groom) disproved offline:
  - the first trip precedes every upload by 4 frames (2089 vs upload in build 2093; 2152 vs 2156; 2125 vs 2128);
  - same run 180301, static warm-up: 10 groom uploads (68 MB) while 420 of 460 G-buffer hair draws were strands,
    0 dropouts, 0 trips;
  - code: no eviction under the 512 MB groom budget; frees retire after 6 frames, GPU done + 2 and every live scene
    slot (bvhsplit.cpp:355, bvh_on_present); uploads copy into retired ranges between barriers (bvhsplit.cpp:3736,
    3767) and a LOD is used only after its recording completed; no pool growth or repack in any hole window.
- Common trigger in all four hole runs (125217, 164930, 180301, 180949): the first frame of the runner's wheel zoom;
  the arrivals are characters entering the lowered view. At that frame the build switches to its moving-camera mode
  (bvhsplit.cpp:2626/2813/3459). The rejecting stage under motion is not decided offline. No fix, no key.
- Added (CPU only, no shader, no ABI, variants delta 0): the frame watchdog line ends `build camera step X m at that
  frame, Y m the frame before` (hair_primary::CameraSteps). CTest hair_groom_asset_test test_camera_steps.
- hl1 job: wheel expect fixed (`phase=2`, derived from the plan; phase=1 made 180301 INVALID), expect_every on the
  camera step of every trip line in lower/watch; predicted.json first; lint 0 failing (live and frozen corpus).
- Next live predicted: trip lines in "lower" with a step >= 0.05 m, none in the static windows. Then the stage.
- Details: docs/RTX_HAIR_PRIMARY.md 5x.

## Pixel-footprint strand LOD for camera rays: (a), (b), (d), (e) built, (c) open (branch claude/hair-pixel-lod, offline, 2026-11-10)

- Key rt_hair_pixel_lod (dev, off, pinned in release.ini), rt_hair_pixel_lod_k 1.0. Design and details:
  docs/RTX_HAIR_PRIMARY.md 5v.
- Step 1: decode_v3_lod(..., extra_keep) rebuilds keeps 1/8, 1/16, 1/32 from LOD 0 (nested hash, radii x 1/keep, 9 mm
  cap); no file change. hair_groom_v3_test: all three non-empty, nested, radii exact to 1e-6.
- (a) 2a43c06b: pixel_lod_level (common/hair_groom_budget.h): finest lod_keep / 2^j reaching k px, 1/32 floor, level 0
  under 60 px heads, quarter-octave hysteresis; unit tests.
- (b) edb6103e: camera arrays per (LOD, level) (hair_groom::camera_subset: kept strands' segment starts, radii x 2^j),
  a camera BLAS per groom slot on its points (refit with the groom BLAS), a camera groom TLAS per scene slot at the
  groom TLAS rows; the header names it, row.z = the camera index buffer. Shadow, GI and fibre rays keep the groom TLAS.
  Every pixel-record segment reader resolves through the header row (checked). Key off: no allocation, unchanged.
  Shader identity 227/227 vs e5f62b50, variants delta 0. hair_groom_v3_test: 14 LOD x level pairs equal to the
  decode-time keeps.
- (d) 3b501d8e: period line `[hair groom] pixel LOD` (levels, diameter and head px, px per radian, camera builds, arrays
  and BLAS MB). Pool MB, BLAS MB, refit ms: not measured offline (no layer BLAS fixture); the live part reads them.
- (e) 8cff5e87: no separate job; hl1-s8-late-arrival (hair_late) gains part 2 after its unchanged arrival part:
  pl-off / pl-on / pl-off2 (60-frame windows), PNG bursts 99310/99311, period line and strand G-buffer GPU ms per
  window; predictions in predicted.json part2_rt_hair_pixel_lod. live_flow --frozen 0 failing. Staged, not queued.
- Left: (c) cross-fade between adjacent levels (spec constant 1048), design on the GPU hair fixture first; without it a
  level change pops (about 2x a LOD step). No launcher toggle yet. The projection read (UBO float 21) is confirmed only
  by the live period line.

## pl1 S8 small-heads live: results and job fix (branch claude/hair-pixel-lod-small2, 2026-11-10; no code change)

Run `pl1-s8-small-heads.1.211046` (S8 zoomed out, static camera, off / small keep 1/4 / 1/8 / off).
- INVALID cause: the job's 1/4 regex had one `/\d+` too many (7 level counts, the line has 6), so
  `488/0/305/0/0/0` could not match; the line itself was right (after the switch, "small keep 0.250"). Fixed in
  `tools/live_batches_staged/hair_pixel_lod/make_job.py`, checked against the run's four on-window lines (all match
  their window's pattern), live_flow --frozen 0 failing (1 readiness query added). The job now also sets
  dump_preview_f32=1 (float scene colour at the preview grid, ~10 MB per trace) so head energy is float, not PNG.
- Pick, as predicted: 1/4 window 488/0/305/0/0/0 groom frames (5 of 13 grooms, LOD 0 grooms, at level 2 = keep 1/4),
  head 6-53 px; 1/8 window the same 5 at level 3. Nothing at levels 1, 4, 5.
- Strand G-buffer GPU ms per window (60-frame lines): off-a 0.304 / 0.281 / 0.287, on 1/4 0.219 / 0.212, on 1/8
  0.193 / 0.193, off-b 0.293 / 0.307 / 0.307. 1/4 saves about 0.08 ms (-27%), 1/8 about 0.10 ms (-34%), beyond the
  off-a / off-b spread (0.02 ms); larger than the predicted +-0.03 ms.
- Memory: camera arrays 17.4 MB (1/4) and 33.1 MB (1/8, the 1/4 arrays stay resident: arrays are cached per LOD and
  level and freed only with the LOD or the key, 17.4 + 15.7); camera BLAS 82.1 MB (1/4) and 41.1 MB (1/8).
  - BLAS size scales with the kept segments: 1/8 keeps half of 1/4's strands, so half the BLAS (e.g. c10b0714 LOD 0:
    399406 of 1599844 segments at level 2). About 16 MB per groom at 1/4, ~41 B per segment.
  - Avoidable in part: the camera BLAS is built with ALLOW_UPDATE and never compacted (bvhsplit.cpp camera BLAS,
    "never compacted: it is rebuilt at every level change"), while the groom BLAS uses the slot compaction path. A
    compaction after its first build (update stays allowed) is the cheapest saving; the ratio for refittable LSS is
    not measured. Not done here (layer change, needs a live).
- PNG traces (scene colour 5c3fdabc preview, 1280 x 533; G-buffer RT2 class 5 as the hair mask; the six largest
  hair regions, 185-1449 px): float frame mean off-a 0.2909, 1/4 0.2916, 1/8 0.2902, off-b 0.2921 (within the
  off-off spread 0.4%). On the heads the noise floor is too wide for the predicted 0.5-3% effects: off-a vs off-b
  hair pixels 2313 vs 2249 pooled but up to 2x per head (head 0: 597 vs 276), preview luminance 0.422 vs 0.418
  (per head up to 18%), Laplacian HF 0.270 vs 0.253. 1/4: 1632 hair px, 0.412, HF 0.259; 1/8: 2467, 0.423, 0.262:
  all inside or at the off-off spread. The crops show why: the NPCs animate (heads turn) between shots taken 8 s
  apart, so one shot per state does not isolate the strand LOD. Verdict: no visible defect (no holes, no card
  fallback, no outline growth seen), image gain not resolved; brightness unchanged at frame level.
- Crops (off / 1/4 / 1/8, x4 nearest): `.scratch/pl1/head0_off_q_e.png` .. `head3_off_q_e.png`.
- Next if the image gain must be shown: several alternating shots per state in one run (or the pan burst at a
  frozen frame) with dump_preview_f32; otherwise the GPU saving alone favours keeping 1/4.

## Pixel LOD for heads of 35-60 px (branch claude/hair-pixel-lod-small, offline, 2026-11-10; key rt_hair_pixel_lod off)

- Why: hl1 200821 (S8 zoomed out) had every head at 6-53 px, so the 60 px floor left all 13 grooms at camera level 0.
- Tool: `tools/hair_groom/pixel_lod.py --ladder` (the study tool, brought over with its sweep mode): study beard 16fa158c,
  heads 20-60 px, 32 jittered frames, every camera level the layer can trace (today's LOD keep / 2^j down to 1/32), the
  continuous widths k 0.5-0.8, and a coverage-conserving radius per level (radii x c, c fitted so mean coverage equals
  the truth). About 2 minutes, writes ladder.json.
- Gate (derived, per size): coverage bias, silhouette growth and bulk energy may exceed today's by at most
  max(today's LOD step, split-half noise of today and of the case); a gain is coverage noise or HF beyond that noise.
  Per-pixel coverage RMS is reported, not gated: it rises with every HF gain by construction.
- Finding: the coverage and silhouette error depend on the keep, not the head size. Bias / silhouette at every size
  20-60 px: keep 1/4 +0.5..+0.8% / +0.1..+2.1% (today's LOD 1 -> 2 step), 1/8 +1.8..+2.9% / +3..+7.5%, 1/16
  +5.3..+5.7%, 1/32 +15% (+15..+17%); the study's 80-150 px numbers fit the same curve. "Fails under 60 px" was the k
  rule asking for 1/32 there.
- Per size (today LOD 1 = keep 1/2 -> keep 1/4; HF, coverage noise, bias, silhouette):
  - 20 px: today is already LOD 2 (1/4); 1/8 fails (+2.9% / +3.3%). Unchanged.
  - 30 px: 0.045 -> 0.051, 0.184 -> 0.182, +0.5%, +0.2%: no gain beyond noise. Unchanged.
  - 35 px: 0.050 -> 0.058, 0.183 -> 0.175, -0.3 -> +0.6%, +1.0 -> +2.1%: pass (per-frame HF gain).
  - 40 px: 0.051 -> 0.059, 0.165 -> 0.164, 0.0 -> +0.7%, -0.2 -> +0.1%: pass.
  - 50 px: 0.054 -> 0.068, 0.161 -> 0.154, -0.2 -> +0.7%, +0.7 -> +2.0%: pass.
  - 60 px: 0.054 -> 0.071, 0.153 -> 0.146, -0.2 -> +0.5%, +0.6 -> +1.7%: pass.
  - 1/8 at 35-60 px: HF 0.068-0.087, noise 0.125-0.164, but +2.2..+2.4% / +3.1..+4.2%: fails everywhere.
  - Coverage-conserving radius (c 0.53-0.97 by keep): passes at some sizes only (30, 35, 50 px) and is a per-groom
    calibration: rejected. Cards for scale (study, 40 px): HF 0.147, coverage -47%.
- Rule (common/hair_groom_budget.h pixel_lod_level): heads of 35 (kPixelLodSmallHeadPx, the smallest passing size)
  to 60 px run the same k pick with the ladder stopped at keep 1/4 of the full groom (LOD 1 -> level 1, LOD 0 ->
  level 2, LOD 2 -> 0); under 35 px level 0; 60 px and over unchanged. Dev knob `rt_hair_pixel_lod_small_keep`
  (0.25; 0.125 for the look A/B; 0 = level 0 under 60 px as before), shown in the period line ("small keep").
- Key off: unchanged (the pick runs only with rt_hair_pixel_lod on; no allocation). No shader change (no file under
  shaders/ or any include touched), variants delta 0. CTest: hair_groom_asset_test checks the step-1 sizes.
- Live staged, not queued: `tools/live_batches_staged/hair_pixel_lod` (pl1-s8-small-heads): S8, hl1's wheel step,
  static camera, off / on (1/4) / on (1/8) / off, period line, strand G-buffer GPU ms, one PNG trace per window;
  predicted.json first; live_flow --frozen 0 failing (2 readiness queries added to the lint snapshot).
- What the live should show: 1-6 grooms at level 1-2 with small keep 0.250, no outline growth, GPU ms within
  +-0.03 ms; strand structure slightly finer than today (subtle at 1280 px). 1/8 is the visible step and the user's call.
- Open: one groom (a beard); the 60 px boundary jumps from keep 1/4 to the k rule's 1/32 (+15% coverage, as before
  above 60 px: by the same finding the >= 60 px rule overcovers too, a decision for the coordinator).

## rt_hair_pixel_lod first live: inconclusive, a level-pick bug left 12 of 13 grooms at level 0 (branch claude/hair-pixel-lod-live1, analysis only, 2026-11-10)

- Data: hl1-s8-late-arrival runs 180301 and 180949 (repeat), S8 static zoomed-out camera, bursts off 99310 / on 99311 per
  run (16 frames, `tools/hair_groom/temporal_burst.py` reader and HF metric). The camera is identical in all 4 bursts
  (P11 1.921); characters idle and walk between bursts, so only the static front character is a clean control.
- px per radian is right: 1067 / 2 x P11 1.921 = 1025 (vertical fov 0.961 rad). The `[sl] fov 0.838` line is the
  load-screen camera (P11 2.246), logged only for the first 3 frames.
- "Head px" is not the study's head size: `layer/bvhsplit.cpp:3393` takes the groom bounds diagonal / distance (:1420),
  the study (`pixel_lod.py` HEAD_M) a 0.25 m head height. The front head is about 64 px tall (0.25 m at 3.97 m) but its
  groom logs 177-180 px: the diagonal reads 1.5-3x high, so the 60 px floor (kPixelLodMinHeadPx) acts about 2x too low.
- Head sizes by the code's measure (LOD 0 = diagonal over 92 px, LOD 1 = 31-92, LOD 2 = under 31): 5 grooms over 92 px,
  3 in 31-92, 5 under 31 (both runs). By real head height about 4-6 heads are 45-65 px: the isometric medium case sits
  right at the 60 px floor, not mostly below it.
- Bug, why only one groom changed: `groom_r_median` (bvhsplit.cpp:1333-1339) returns 0 without host radii, so d0 = 0 and
  `pixel_lod_level` returns 0 (:3391-3395). Host radii are kept only for LODs uploaded with the key on (:1260); the
  re-decode in `groom_cam` (:1288-1298) runs only at level > 0, which needs d0 > 0 first. Only 36576f20 LOD 1 was
  uploaded during the key-on part 1 of both runs and kept its cached r_median. The "0.00" d0 values are this bug.
- The changed groom (LOD 1, level 4, 9593 of 151794 segments) is not identified for certain. Best match in run 1: the
  dark-haired head at render x 786-867, y 430-493. Off/on there: albedo HF 0.458/0.598, accumulated albedo HF
  0.130/0.206, final HFacc 0.0115/0.0186, temporal std/mean 0.047/0.028, mean 0.332/0.336 (SEM 0.0005), coverage -0.7%,
  albedo change per frame 0.64/0.61 (study: 0.96-0.99 to 0.51-0.79). In run 2 that character is elsewhere.
- Control (front head, LOD 0, unchanged), off/on: std 0.0285/0.0197 (r1), 0.0251/0.0120 (r2); HFacc 0.0135/0.0150,
  0.0153/0.0206; mean +0.66%, +0.59%. Off vs off between runs: std 0.0285/0.0251, HFacc 0.0135/0.0153, mean 0.4%. All
  hair: mean +1.7% (r1), +1.2% (r2) against off/off 2.8%. The candidate's std drop (-41%) is within the control's
  (-31% to -52%); its HF rise (+62%) exceeds the control's (+11% to +35%), from one run only.
- HF scale: the study's 0.09-0.15 (cards) is offline shading HF before RR, not comparable with live final HF (0.02-0.03,
  after RR). Closest: accumulated albedo HF, 0.08-0.10 on unchanged strand hair, 0.21 on the candidate.
- One LOD 1 draw fell to cards in each on burst (758b r1, e851 r2); in r2 before the key went on, so not the key.
- Strand G-buffer GPU is noise: r1 0.30-0.34 off, 0.517/0.458 on; r2 0.28-0.40 off, 0.281/0.282 on.
- Conclusions: (1) the live run does not test the design: the r_median bug and the diagonal head size decide which
  grooms change. Fix both before another live (r_median at upload whatever the key; 0.25 m x ppr / distance or a scaled
  floor). (2) Where it acted, the predicted structure gain (accumulated HF up), no energy or coverage shift beyond noise.
  (3) Paused, key off. Crops: `.scratch/pixel_lod_live1/b4_*.png` (main checkout, not committed).
- The trail / blur in motion found during this analysis (strand motion vectors camera-only on most strand pixels) is
  followed up in docs/progress/claude-hair-mv.md and docs/RTX_HAIR_PRIMARY.md 5y.

## rt_hair_pixel_lod: level pick inputs fixed (branch claude/hair-pixel-lod-fix, offline, key still off, 2026-11-10)

- Cause 1 (hl1 180301/180949: 12 of 13 grooms never left level 0): `groom_r_median` (layer/bvhsplit.cpp) read the
  LOD's host radii, kept only for LODs uploaded while the key was on; for the others d0 = 0, so `pixel_lod_level`
  returned 0 and the re-decode in `groom_cam` (which needs level > 0) never ran.
  Fix: `groom_lod` sets the LOD's median radius at every upload whatever the key (`hair_groom::median_radius`, one
  float per LOD; nth_element over the decoded radii already in memory). The period line's LOD 0 diameter is then never
  0 for an uploaded LOD; host radii remain only a fallback.
- Cause 2: "head px" was the groom bounds diagonal / distance (177-180 px for a 64 px head), so the study's 60 px floor
  applied about 2x too low. Fix: head px = the study's head height 0.25 m (`kPixelLodHeadM`) / distance x px per
  radian (`hair_groom::pixel_lod_head_px`); the floor keeps its 60 px, now on the quantity it was derived for.
  px per radian (1025 in S8) was already right.
- Test (hair_groom_asset_test): median radius from the decoded radii only (no host arrays); a 64 px head at 4.0 m with
  0.186 mm strands gives d0 0.048 px and level 5 (> 0); a 59 px head keeps level 0 although its diagonal is over 60 px.
- No shader, no ABI, no settings change; variants delta 0. CPU cost: one nth_element per LOD upload (under 1 ms for
  0.9 M points, inside the 30-80 ms decode).
- Expected in the next pixel LOD live (pl-on): d0 range without 0.00, head px 0.25 m-based (front S8 head about 64),
  most medium heads at levels 3-5, heads under 60 px at level 0. Status: key off, READY_FOR_LIVE with the next hl1.

## RTX Hair On bundle: add rt_hair_groom_nearest (live hs1 133243) (branch claude/hair-nearest-default, 2026-11-10)

- hs1 133243 (S8 still, v3 set, key on, VALID): 30 of 30 periods with 0 frame watchdog, 0 key watchdog, 0 dropouts and
  no "coverage low" switch; 2 "coverage stale" switches (tiny-hair item). Strand G-buffer GPU median 0.171 ms/frame
  (0.118..0.320) against 0.227 (0.097..0.307) in 125217: no measurable cost.
- Not reproduced: the three arrivals (joins 13:34:25.14-25.62) fell 0.6-1.1 s after the switch-on, inside the warm-up
  window (as 103633). The run shows no regression; the proof of the fix is the GPU case (2,567 holes to 0).
- rt_hair_groom_nearest=1 added to the "On" choice of exp_hair_groom_primary (code default 0, schema, settings_test
  pins, bg3rtx.ini comment). Compiled at pipeline creation: a game started with RTX Hair off keeps the old trace until a
  restart. settings_test 6271 checks pass; build.ps1 -Test. No shader or ABI change, variants delta 0.

## Strand motion vectors: the velocity variant does not write most strand pixels; rt_hair_mv_card and pan_mvec built (branch claude/hair-mv, offline, key off, 2026-11-10)

- User: strand hair at isometric distance trails / blurs when characters or the camera move.
- Measured on the hs1-s8-static-hair bursts on disk (static camera, idle party) with the new
  `tools/hair_groom/mv_ratio.py`:
  - camera-only motion vectors (within 0.02 px of the static background): 73-86% of strand hair pixels, 5-7% with cards;
  - hair / 6 px ring median |mv|: strands 0.05-0.23, cards 0.52-1.08;
  - per head: strands 0-12% of the pixels moved (next to heads moving 0.1-0.7 px), cards 86-100%.
  RR then takes the history from where the hair was: a trail.
- Cause, narrowed offline: the strand path itself never writes these pixels. 95% of them read 1e-4-3e-4 px, the game's
  camera pass value. The strand formula, even without previous points, gives the axis point's offset from the pixel
  centre instead (up to a strand radius), so the previous-points fallback (row.w 0) is not it.
- Left open: one of the velocity discards (seal/tag, accepted marker slot, segment bounds) or the depth test (an
  unswapped draw, or the depth changed after the G-buffer). It reads the G-buffer's record on the same card triangles,
  so neither re-trace nor coverage can explain whole heads. The full list is in docs/RTX_HAIR_PRIMARY.md 5y.
- Built, off by default: `rt_hair_mv_card` (dev key, pinned 0 in data/release.ini), specialization 1049 in the velocity
  variant, with live header flags:
  - 1 = the card's skinned motion where the row lacks previous points;
  - 2 = the card's motion for every fragment of a swapped draw, before any strand check, with the sealed strand depth.
  Mode 2 bypasses every discard, so the live separates "discard" (mode 2 fixes it) from "depth test or swap" (it does not).
- Counter line: `[hair groom] strand motion` (rows with and without previous points and why; publishes without the
  previous camera). `hair_primary::prev_row_state` is unit-tested.
- `pan_mvec=1`: K8 edge bursts keep RR's motion vectors (half-size, every frame; EdgePanSamples and pan_epoch_test).
  A burst is now about 0.1 GB instead of 4.9 GB.
- Key off: 225/227 shader requests byte-identical to main b29a9d3b. The velocity shader is equivalent with 1049 frozen to
  0, and spirv-val passes. Variants delta 0.
- Staged, not queued: `tools/live_batches_staged/hair_mv` (hm1-s8-hair-mv: counter line at 0/1/2, four K8 edge bursts:
  strands 0/1/2 and cards, about 0.4 GB; predicted.json first; live_flow --frozen 0 failing).
- Status: READY_FOR_LIVE (needs the user's go). Cost: no extra trace; mode 2 skips the strand loads.
- Also on this branch: `docs/progress/claude-hair-pixel-lod-live1.md` (the pixel LOD live analysis).

## hm1 edge bursts were refused: FG inputs hit the edge extent budget before the name filter (branch claude/hair-mv-burst, offline, 2026-11-10)

- hm1 201105 INVALID: every K8 edge burst logged "[pan] edge capture REFUSED: incompatible mode or extent budget"
  (3 per frame, 4668 lines) and no "burst 9940x complete". Cause: with pan_inputs=1 the layer also offers the
  display-size FG inputs (fg_hudless, fg_ui, fg_final at 3840x1600); layer/dump.cpp checked the 2560 px budget before
  the edge-mode name filter, so these images invalidated the burst although it never keeps them.
- Fix (no key, diagnostics path only): `EdgePanSamples::kept` (common/pan_epoch.h) lists what an edge burst keeps
  (depth, final, material, albedo; rr_post_ac/eb with pan_post; rr_mvec with pan_mvec); pan_capture skips everything
  else before the budget check. pan_epoch_test: FG inputs and rr_color not kept, rr_mvec only with pan_mvec, every
  wanted image kept. Shaders untouched.
- The hm1 counter line answered the other question: 780 of 780 keyed rows with previous points per 60 frames and every
  publish with the previous camera, so missing previous points are disproved; rt_hair_mv_card=1 is useless, =2 is the
  test (docs/RTX_HAIR_PRIMARY.md 5y).
- hm1 unchanged (4 edge bursts, about 0.4 GB; live_flow --frozen 0 failing). Rerun it with a tree that has this fix.

## Late groom arrival on demand: rt_hair_debug_late_arrival and the hl1 job (branch claude/hair-late-arrival, offline, staged, 2026-11-10)

- `rt_hair_debug_late_arrival` (developer only, index count, 0 off, pinned 0 in release.ini): after every live strands
  switch-on the groom of the hair draws with that index count is held for 301 frames (kLoadQuietFrames + 1, one past
  the longest warm-up), not counted by the warm-up, then released. Logs the hold and the release. CPU only.
- Staged job tools/live_batches_staged/hair_late/jobs/hl1-s8-late-arrival.json (S8, one launch, ~61 s of phases,
  45252 held): arrival-a with rt_hair_groom_nearest live off must show holes (frame watchdog trips or dropouts),
  arrival-b with it on none (0 trips, dropouts <= 2 in every period). predicted.json precedes it; live_flow 0 failing.
- Gates: hair_groom_asset_test test_late_arrival; build.ps1 -Test. Details: docs/RTX_HAIR_PRIMARY.md 5u.

## RTX Hair On now includes the union count, the confirmed coverage failure, stable slots and the live warm-up (branch claude/hair-keys-default, offline gates passed, 2026-11-10)

- User decision 2026-11-10: keys that are useful and fix the problem go in. RTX Hair On (common/settings.h
  `exp_hair_groom_primary` choice "on") also writes `rt_hair_coverage_union=1`, `rt_hair_coverage_confirm=1` and
  `rt_hair_slot_stable=1`. The live switch `rt_hair=0` gates them with the rest (layer/util.cpp `hair_gated` reads the
  same list); a file with only `rt_hair_groom_primary=1` still reads as On.
- Evidence (docs/RTX_HAIR_PRIMARY.md 5u): VALID hs1 095322 with the three keys on: coverage low / other switches 0
  in 30 periods (25 and 22 with the keys off in 085153), dropouts 0 in 28 of 29 periods, judged ratio 1.00 to 1.04.
- `rt_hair_live_warmup=1` is in the bundle too: VALID hs1 103633 (five keys on), 30 periods, frame watchdog trips 0
  (095322: 5 after the toggle), dropouts 0 in 27 of 27 footprint lines, 11 keys back on strands after the live toggle
  (card runs 80 to 105 frames) without a trip.
- Disproved prediction: `rt_hair_coverage_min_pixels` was to give 0 "coverage stale" switches on tiny hair; 103633
  shows 5 (key 2aa3 at 57 to 70 card pixels, across the 64 pixel threshold, which has no hysteresis; key 9b09 four
  times). Not qualified, kept out, not fixed here.
- Not in the bundle (settings test checks it): `rt_hair_coverage_min_pixels`, `rt_hair_swap_latch`.
- Code defaults stay 0: with RTX Hair off nothing changes. No shader edit; the union count request
  `rt/hair_primary_count.hlsl?HP_COUNT_UNION=1` was already in shaders/variants.txt (delta 0).
- Staged hs1 job: its hard-coded hair set got the four keys (job `set` and make_job.py).

## Strand holes under the wheel zoom (branch claude/hair-holes-root, READY_FOR_LIVE, 2026-11-10/11)

- 6c, cause 1 (fixed by the existing key `rt_hair_header_cam_gpu`, in hl1): the header camera was a CPU read of the
  camera UBO at the build's recording, the groom points are converted on the GPU; under motion the read can be one step
  behind and every hit is one step off its card. hc1 234025: GPU copy -> origin 0 in 18 of 18 samples; CPU -> 1-12 cm.
- Live hl1 000345 (key on): origin 0, zoom coverage 50-70% (was 2-7%), still 7 trips; the loss follows the camera step
  (1.31 m: 5%), all or nothing per draw. The 0-trip prediction was wrong.
- 6d: build stamp (layer only). Live hl1 003637: all 5 trips (2448-2452) "of the applied publish's build", 0 newer in
  every period: the "header one publish older than the BLASes" candidate is DISPROVED.
- 6e: BLAS refit ruled out from runs on disk (the loss is gone when still, refits only). No rebuild key: grooms are
  compacted, a full build needs the uncompacted size. Lost first: the far LOD 2 grooms (58da 64/0, 6a5d 74/0).
- 6f, offline read of the three candidates left (no fix found):
  - view-projection: the G-buffer side uses one block (gCam.vp) for card point, ray, depth and band; the points are the
    exact inverse of the prepass block's VP, so a jitter or projection difference moves no point. Open only: the block
    rewritten between the prepass draws and the conversion, or a VP that is not view x projection (both in the sampler).
  - entry reuse by a second G-buffer of one label: ruled out for 003637 (labels 2448-2452 each found their own build
    stamp, builds logged once per label). 000345 has a label gap at its long frame: open for that frame only.
  - game LOD switch: in all 8 hl1 logs a new hair draw arrives at the first trip frame or the one before; unmatched
    (not swapped), rows and BLASes unchanged, the loss lasts 5-7 frames and follows the step: zoom start coincidence.
- 6f, dense sampler built: key `rt_hair_dense_sample` (developer, off, pinned 0 in release.ini), specialization
  constant 1052 (`kSpecIdHairDenseSample`) of the strand G-buffer variants, added only when on. A quarter of the
  card-marked fragments store a 96-byte sample, 4 per draw slot (64 a frame), behind the pixel record (device-local);
  the next build copies 6 KB behind the counts readback. Per sample: reason (accepted, miss, other draw's strand,
  segment, card band, UVs, tips), reused or private, ray origin and direction, card and hit distance, band,
  strand point off its ray, sine against the block's view x projection, camera, rows, the groom's anchor point.
  Layer: camera step from the GPU cameras, anchor drift in the world per key, LOD and key age; lines
  `[hair primary] dense sample:` (moving and watchdog frames) and `[hair primary] dense samples (last N frames ...)`
  (still | moving). CTest hair_groom_asset_test test_dense_sample.
- Signatures (predicted.json dense_sampler_6f): displaced points -> anchor moves cm or more per frame while moving;
  inconsistent camera block -> sine over 0.1 mrad; entry reuse -> reused entries with the strand point cm off its ray;
  game LOD -> rejects only on new keys or cards metres from their anchor; other publish's key map -> "strand of another
  draw"; trace or BLAS/TLAS -> misses with the anchor still and the ray right.
- Identity vs origin/main: only rt/hair_primary_gbuffer.hlsl changed (the constant is in the module; 0 runs none of
  it); variants.txt delta 0. Cost on: a 4x4 inverse and a 96-byte store per sampled fragment (read the strand G-buffer
  GPU line against 0.206 ms). hl1 regenerated: rt_hair_dense_sample=1, pipeline line with 1052 on, the period line with
  sampled frames in lower and watch, sample lines in lower; trips reported, not gated. Lint 0 failing (live, frozen).
- Next live shows which signature the moving samples carry; the fix follows from it. User check once fixed: S8, wheel
  zoom in fast: the hair keeps its strands, no pop to cards.
- Left: lint_snapshot on this PC shrinks the checked-in snapshot (needs a merge mode); the CPU camera also feeds the
  cascade caster rebase (rt_cascade_origin=0), the groom LOD bounds and rt_camera_moving.

## Per-groom coverage table from the native converter (branch claude/hair-groom-coverage, offline, 2026-11-10)

- bg3rtx_hairconv --all over the whole catalogue (user's LSLib granny2.dll, not copied): 20 styles in 6 s, 1,217 rows
  in 406 s with --export-hcap; 898 pass, 282 needs-review, 37 reject.
- The converter's 3-view validation now writes `coverage_ratio` (strand / card composited opacity per view, the
  bound's own quantity) next to `coverage_delta`. hairconv tests pass.
- Every written groom (898) is inside the gate's bounds: U/C 0.907..1.075 against [0.8913, 1.0945]. The 72 rows
  outside are all unwritten (37 rejects, 44 needs-review: Watcher headwear 0.882-0.888, Minthara LOD0 0.881).
- tools/hair_groom/coverage_audit.py (texture-space test card on each real card and mask, cached, progress, 35 min for
  898): the old S/C count falls under the bound for 15% / 81% / 99% / 100% of grooms at 2 / 4 / 8 / 16 texels per
  pixel (cause A for every style). Its U/C overestimates the 3-D ratio (1.18 vs 0.99): relative S/C only.
- 3399 / 7086 (S8 cards every frame) are in no catalogue row; 1623 / 4077 are needs-review LOD1 beards; the user's
  other unmatched counts all have passing grooms (conversion on the PC pending). 2 mm radius cap: at most 0.17% of a
  groom's points, no visible effect. Details: docs/RTX_HAIR_PRIMARY.md 5u.

## RTX Hair one-frame holes: cause not established, deciding diagnostic added (branch claude/hair-frame-miss, READY_FOR_LIVE diagnostic, 2026-11-10)

- hk1 is INVALID and disproves the shared-key prediction: the gate of 0 watchdog trips failed in turn-shared-r (8
  trips; 3 to 10 per 120 frames without the key), later windows did not run, so `rt_hair_watchdog_one_key` is
  unmeasured in turns. "shared-key rows withheld" 0: both rows of the dropping key were in every publish.
- hs1 (static): pre-RR flicker strands / cards 1.02, final 1.50 (reconstruction side of predicted.json). Not clean:
  12 dropouts of key 6047 in frames 2244 to 3322 with watchdog trips 0; in the bursts one head loses its hair for one
  frame (class-5 pixels 232 against about 2100 at frame 2582, 332 against about 1900 at 2904; final luminance
  0.44 -> 0.32). Frame 2582 is not in the dropout list: the counter sums the two draws of a key.
- 6047 (hs1) and 3b08 (hk1) are the keys on two groom rows (two characters, one hairstyle). Their rows exchange
  content between frames (row 10 / row 11 strand pixels 2206 / 3233 -> 3185 / 2177): a dynamic slot is the n-th draw
  of its mesh in the capture (layer/bvhsplit.cpp `g_dyn_seen`), so a draw-order swap puts the other character's points
  into each slot. Proven effect: previous points of the other character (wrong motion vectors that frame). Not proven:
  that the hole happens in that frame, nor why. "rejected after the hit" for one key does not exclude missed rays
  (dropout_reason uses frame-wide totals), so the static holes and the frame-wide miss in turns may be one class;
  undecided offline.
- Added, CPU only, no key, no shader, no ABI change, variants.txt delta 0: `[hair groom] slot content jumps` line
  (hair_primary::groom_bounds_jump on the per-slot bounds readback, threshold = the bounds diagonal) and
  `[2 rows: a+b]` per-row strand pixels on listed dropouts of a two-row key.
- Deciding observable in the next S8 static live: every 6047 dropout frame F has a jump at build frame F or F - 1,
  jumps only on two-row keys. Otherwise the exchange is ruled out and the per-row pixels name one row or both.
- Installed build: strand primary is off by default (here and at cb2d03f2); the live jobs turn it on.
- Details: docs/RTX_HAIR_PRIMARY.md 5q. No fix shipped (no guessed fix).

## hf1 job fix after the INVALID run 073932 (branch claude/hair-fg-job, staged, not queued, 2026-11-10)
- Cause of "window 'snap-fg3-a' has 0 event line(s)": a second periodic gate turned the snap windows into
  stats_reset frame windows, which start at motion-ready (07:41:30.365); `present capture done` was logged at
  07:41:29.932 (fg_snap is applied at the settings read, before motion-ready).
- Fix (tools/live_batches_staged/hair_fg/make_job.py): snap windows are plain 4 s timed windows gated on the last PNG
  line (`[sl] presented frame 11 -> ... (ok)`); each fg=3 watch window waits for a fresh `[sl] DLSS-G frame` line
  with displayed/rendered >= 2.5 (30 s, then `pipeline readiness timed out: survive-fg3-...`); zoom 20 wheel notches
  (was 10; head size not verified offline).
- FG after load: SDK cadence 3.00 two seconds after FG on (07:40:37); the layer's present counter 1.00 until
  07:40:50, 3.00 at 07:40:53.9; the log gives no cause. Live re-enable: 3.00 after 2.7 s, so window A had generated
  frames throughout.
- 97 s of phases plus waits (about 2 to 3 s per fg=3 window); lint 0 failing (4 duration notes: snap windows kept at
  4 s for the PNG writer). No launch. Details: docs/RTX_HAIR_PRIMARY.md 5s.

## RTX Hair detail at distance: launcher control (branch claude/hair-detail-knob, 2026-11-10; offline, Off in presets)

- What: one release control `hair_distant_detail` ("RTX Hair detail at distance"), launcher page Lighting, card right
  after RTX Hair. Choices: Off / 1/4 (Standard) / 1/8 (Sharper). Off writes `rt_hair_pixel_lod=0`; 1/4 writes
  `rt_hair_pixel_lod=1` + `rt_hair_pixel_lod_small_keep=0.25`; 1/8 writes `=1` + `0.125` (docs/RTX_HAIR_PRIMARY.md 5v,
  common/hair_groom_budget.h pixel_lod_level). `rt_hair_pixel_lod_k` stays 1.0 (not exposed).
- Live: every choice is Apply::Live ("Applies after saving"). The launcher's Save writes bg3rtx_user.ini; the layer's
  cfg_poll (layer/util.cpp cfg_read_files) re-reads the ini files on mtime change every frame, and layer/bvhsplit.cpp
  reads both keys with cfg_int/cfg_float per capture (the pl1 live toggled them in one session). Neither key is in
  key_change_needs_restart. Turning it off releases the camera arrays from the mesh pool.
- Release: `data/release.ini` keeps `rt_hair_pixel_lod=0` as a default, but enforce_release_policy no longer pins it to 0
  (it pinned every release.ini 0 key, which would have made the control inert in release, as RTX Hair once was).
- No Need on RTX Hair: without RTX Hair the keys do nothing (the pick runs only with strands on screen), and a preset
  can select 1/4 without forcing RTX Hair on. The tooltip says it only changes RTX Hair.
- Cost on the card: strand G-buffer -0.08 ms (1/4) / -0.10 ms (1/8), camera arrays + BLAS about 100 MB (1/4) and
  74 MB (1/8), from pl1 211046 (RTX 5090, S8); "not yet qualified".
- Tests: settings_test `test_hair_distant_detail` (choice keys and labels, live, Off in presets and release, each choice
  survives the release policy, a file with only `rt_hair_pixel_lod=1` reads 1/4); release visible-knob list,
  launcher_web_bridge_json_test (21 controls, labels), launcher_ui_test (Lighting card order); schema regenerated.
- Shaders: none touched; variants delta 0.

### Making 1/4 the default (after the final qualification live)
- common/settings.h, `hair_distant_detail`: preset row `{0, 0, 0, 0}` -> `{0, 0, 1, 1}` (High and Ultra; or all four).
- High equals the developer defaults (settings_test), so also bg3rtx.ini `rt_hair_pixel_lod=1` (and data/release.ini
  `rt_hair_pixel_lod=1`, or drop that line), then `python scripts/gen_settings_defaults.py`,
  `bin/settings_test.exe --write-schema`, and update `test_hair_distant_detail`'s preset/default checks.

### Final qualification live, staged (not queued): `tools/live_batches_staged/hair_final` (hf2-s8-hair-final)
- S8, ultra + fg=3, RTX Hair bundle + rt_hair_stale_publish=1, rt_hair_mv_card=2 (from load), rt_hair_row_stable=1,
  distant detail 1/4. Warm-up, hl1 zoom (0 watchdog trips per 60-frame line), slow orbit right/left with one K8 RR-motion
  burst (mv_ratio.py ring ratio), off / 1/4 / 1/8 / off PNG traces 1.5 s after each switch, cost lines in every window.
  About 93 s after load, under 0.3 GB. live_flow live and frozen: 0 failing (4 readiness queries added to the snapshot).
### RTX Hair motion stability (`hair_motion_stability`, Lighting, after the distance detail; 2026-11-10)
- Off / On = `rt_hair_mv_card` 0 / 2 (5y; mode 1 disproved, not exposed). Off in every preset and in data/release.ini
  (a default there, not a pin: the release policy keeps a selected On). Off -> On shows "Restart to apply"
  (choice Apply::Restart, key_change_needs_restart 0 -> non-zero): specialization 1049 is compiled into the velocity
  pipelines only when the key is non-zero at their creation (layer/hair_primary.cpp). On -> Off is live (header flag).
  Not made live-compilable: always compiling 1049 would change the default velocity pipeline and touch
  layer/hair_primary.cpp, where the stale-publish fix is in progress; a later step if wanted.
- Tests: settings_test `test_hair_motion_stability`, release controls 22, bridge and UI order, schema regenerated.

## Why strands fail the coverage bound: the gate counted strand pixels inside the card markers only (branch claude/hair-coverage-cause, offline, not live-verified, 2026-11-10)

- Cause A, real: the count's S is the strand pixels whose card marker (the dithered, mip-filtered native alpha test)
  is set too. Far away a pixel's card alpha is the texel mean and the dither keeps it independently of the strands, so
  S/C falls with the footprint; the bound 0.891 was derived for strand over card opacity (every strand pixel, U/C).
  Fixture (primary.footprint_report, 0.5 to 16 texels per pixel): S/C 0.934 -> 0.585, U/C 0.965 -> 0.967 (flat).
  Distant keys therefore fail by measurement (5t loop 2), and frames with several of them trip the frame watchdog
  for every key (5t loop 1, the ~0.5 s period, also on the close character).
- Fix `rt_hair_coverage_union` (off, live; launcher Advanced "Strand hair: coverage check on every strand pixel"): the
  count runs rt/hair_primary_count.hlsl?HP_COUNT_UNION=1 and the lower bound, the latch and both watchdogs judge U;
  the upper bound keeps S. Relaxation only: U >= S per slot, so a key that passes today passes with it; missing
  strands still fail. Measurement change of the gate, no shading or reconstruction change.
- Cause B (draws/frames with ~0 strands): real as known mechanisms (5s two-row exchange, 5i shared key, 5h LOD drop);
  the user log's near-empty frames sit in groom LOD uploads and a refused pool growth. No speculative fix.
- Cause C (count noise): not independent; with U the per-frame spread is 3 to 4 times under the bound's sigma, fails
  0 down to 534 card pixels in the fixture.
- Live hs1 085153 (S8 still, flicker seen): per-key one-frame card flashes, not the frame watchdog (2 of 30 periods).
  "other" = the prepass handshake echo of the same coverage-low refusal (prepass "strike" = allow_prepass). be12's
  zeros are real one-frame holes at its slot exchanges (dropouts [2 rows: 0+0] on the jump frames; slot_stable was 0).
- Fix `rt_hair_coverage_confirm` (off, live, CPU only): a held key goes to cards only after 2 bad measurements in a
  row (kWatchdogDrawBad's reason). Relaxation only; a real loss still refused on the second frame.
- Lean line, always on, CPU only: `[hair primary] coverage by footprint` (S/C, judged, H/C from the existing shared
  trace counts, U/C, own-bound fails per card-pixel bin; dropouts frame-wide / nothing traced / rejected / two-row).
- Gates: build.ps1 -Test all pass; hair_groom_asset_test test_coverage_union, hair_groom_test
  test_coverage_footprint_metric; default count request byte-identical (shader_identity vs 2e1f28d0, 226/226), the
  new request compiles and passes spirv-val; variants.txt 131 -> 132 (+1). No ABI change.
- User session (all keys off, 1 min, still camera, characters far and near): A is confirmed if the small bins' S/C is
  >= 0.1 under the 4096+ bins while H/C stays within 0.05. Then rt_hair_coverage_union=1: judged flat, switches near 0.
- Live result hs1 095322 (S8 still, union + confirm + slot_stable on, VALID) against 085153 (keys off): coverage low
  25 -> 0 and other 22 -> 0 switches over 30 periods; judged U/C 1.00-1.04 flat over 64..16383 card pixels; own-bound
  fails 0 in 84 bins; dropouts 0 in 28 of 29 periods; 104 slot exchanges resolved per period. Kept (off by default).
- Details: docs/RTX_HAIR_PRIMARY.md 5u.

## Strand holes under a fast camera step: camera mismatch candidates narrowed, deciding diagnostic (branch claude/hair-cam-step, READY_FOR_LIVE diagnostic, 2026-11-10)

- hl1 200821 (VALID) confirmed the zoom trigger: trips at G-buffer frames 2064, 2065, 2067-2070 with build camera
  steps 0.34-1.81 m (1.81 m: a 130 ms frame during a groom decode; dolly about 15 m/s), none in static windows.
- Read (docs/RTX_HAIR_PRIMARY.md 5z): groom points, groom TLAS (identity instances), header camera and the soup's VP
  come from one capture UBO; the G-buffer applies this frame's publish in 58-60 of 61 frames (org 0); card band,
  ray and hit depth all use the G-buffer's own gCam.vp. TLAS check, header/BLAS mismatch, build-after-G-buffer: 0.
  The moving-camera mode (bvhsplit.cpp 2626/2813/3459) changes holds, snapshots and row order only (not frame-wide).
- Not established offline: whether the rays' start (the VP origin, hp_rebase) is the eye while the camera moves, or
  whether the trip frames applied an older publish. No fix, no key (no guessed fix).
- Added (CPU only, no shader, no ABI, variants delta 0): `hair_primary::vp_eye` and the applied publish per G-buffer
  frame; the frame watchdog line now names the applied publish (k frames before), the header camera offset and the VP
  eye offset at that publish and at the frame. CTest hair_groom_asset_test test_vp_eye.
- hl1 regenerated (expect_every: every trip line carries the fields; predicted.json camera_mismatch_5z); lint 0
  failing (live and frozen).
- 200821 was VALID: the `phase=1` INVALIDs were 180301/180949 (job from before the 5x fix); main's job has phase=2.
- Next live decides: eye about one step at trips -> shader ray from the near-plane point; k > 0 -> stale header pick;
  neither -> per-reason reject counters in the strand G-buffer.

## An upgraded install gets a grown bundle without a launcher save (branch claude/hair-bundle-upgrade, offline gates passed, 2026-11-10)

- Problem: a `bg3rtx_user.ini` written before 2026-11-10 has RTX Hair On without `rt_hair_coverage_union`,
  `rt_hair_coverage_confirm`, `rt_hair_slot_stable` and `rt_hair_live_warmup`. The mark `rt_hair_groom_primary=1`
  still selected On, but the four keys loaded as their code default 0 until the launcher saved again.
- Fix (common/settings.h `complete_marked_choices`, called by `apply_layer` before `enforce_release_policy`): when a
  file selects a release choice through its migration mark and no choice matches exactly, the choice's keys that the
  file does not set take the choice's values. A key the file sets keeps the file's value (`rt_hair_coverage_union=0`
  in the file stays 0, as before). Not with `dev=1`, never for developer-only knobs or choices, and only when the
  mark is in the same file. It lives in `apply_layer` because only there is "missing from the file" known.
- Tests (tests/settings_test.cpp `test_release_rtx_hair`): old On file gives the four keys at 1; the mark alone gives
  the whole bundle; an explicit 0 survives; RTX Hair off gives 0; `dev=1` completes nothing. Existing pinned checks
  are unchanged.
- Other knobs: the rule is general, so the four other release knobs with marks follow it: `sun_shadows`,
  `exp_water`, `exp_hidden_surface`, `exp_native_coverage`. Only a hand-edited partial file changes (a mark key
  without the rest of its choice, under a preset that selects a lower choice): for example `rt_cascade_casters=1`
  alone now also loads `rt_shadows=1`, `rt_water_shadows=1` alone loads the whole water Shadows choice. This is what
  a launcher save already wrote for such a file. Files the launcher wrote hold every key of their choices and load
  as before.
- No table, schema, shader or ABI change. Gates: settings and launcher tests, full `build.ps1 -Test`.

## RTX Hair audit after the keys-on run: live-on warm-up and the evidence floor (branch claude/hair-audit, offline, not live-verified, 2026-11-10)

- Before/after (hs1 S8 still): 085153 keys off -> 095322 union + confirm + slot_stable on: coverage low 25 -> 0,
  other 22 -> 0 switches over 30 periods, per-key switch periods ~15 -> 0, judged U/C 1.00-1.04 flat over 64..16383
  card pixels, own-bound fails 0 in 84 bins, dropouts 0 in 28 of 29 periods, 104 slot exchanges resolved per period.
- Left 1: strands-on transient (5 frame watchdog trips, 26 dropouts) inside the rebuild of every groom after the live
  switch (full builds 11, LOD waits 15, UV joins 12). Fix `rt_hair_live_warmup` (off): the switch opens the level-load
  warm-up window (rt_hair_warmup's settled signal, at most 300 frames). Mid-play rebuilds (new character, LOD upload)
  not covered: stage not decided (row keys changed in that period).
- Left 2: tiny hair (5364, 1..55 card pixels, ratio up to 6.0) cycles every ~30 frames by "coverage stale". Fix
  `rt_hair_coverage_min_pixels` (off): measurements under 64 card pixels (kDropoutMinCard) are no evidence.
- Upper bound never refuses (S <= C); real keys reach U/C 1.07-1.08 at 256+ pixels. Steady cards: 2 S8 draws without
  a groom (3399, 7086), tiny/hidden keys. Install has 29 grooms vs 871 in the repo; 7 of the user's unmatched counts
  exist in the repo catalogue (conversion pending), 4 nowhere.
- Gates: hair_groom_asset_test test_coverage_audit (tiny draw sequence, 5000 random cases identical at >= 64 pixels,
  LoadGuard::restart window); build.ps1 -Test. No shader, no ABI change, variants delta 0.
- Live check: toggle RTX Hair off/on in S8 with rt_hair_live_warmup=1: 0 frame watchdog trips and 0 dropouts in the
  period after the toggle, "warm-up: load quiet ended" 30-80 frames after it. Details: docs/RTX_HAIR_PRIMARY.md 5u.

## hl1 161106: the late-arrival test did not reproduce the holes; the natural arrival did, with the nearest trace on (branch claude/hair-arrival-cause, offline, 2026-11-10)

- Disproved: predicted.json of hl1 ("arrival-a with the restart loop shows holes"). Holding 45252 for 301 frames and
  releasing it while the other keys swapped gave 0 frame watchdog trips and 0 dropouts (mask-hole pixels 936). The
  hold-one-groom reproduction is wrong; "the newcomer's strands block the others' rays" (5u) is not the mechanism.
- The holes came at the natural arrival in phase "lower" with rt_hair_groom_nearest=1: frame watchdog at G-buffer
  frames 1992, 1994, 1996, 1997 (0/5383), 1998; 31 dropouts (rays missed 10, rejected after the hit 20); the period's
  coverage line: nothing traced 20, traced then rejected 10, frame-wide 3 of 60. At frame 1992 the first newcomer
  (45252) is only "UV join pending": no groom row, no instance, nothing in the groom TLAS. The existing keys (0d51,
  0754, 51e8, 2e35) lost their strands before any newcomer strand existed. So the cause is not ray skipping and the
  nearest trace cannot fix it (nor did it).
- Q1 (was the nearest trace on?): the strand pipelines were created with the job's startup set (rt_hair_groom_nearest=1)
  and the header flag is set from the key every publish ("lower" sets 1), but no line proved it. Added: the strand
  G-buffer pipeline line prints "nearest-strand trace compiled in (specialization 1047) N", and a line whenever the
  header flag changes says which trace runs.
- Q2 (what changes for the existing keys at 1992): not decidable from the log and the code. The capture dropped
  nothing ([geo] RT capture: 0 draws dropped), slot jumps 0, header/BLAS mismatch 0, build camera unchanged, slot
  exchanges resolved as in every period. Missing single observable: for the dropped frame, whether the groom TLAS the
  strand G-buffer traced holds the instances the applied header's rows and key map describe (instance count and
  per-instance key of the TLAS built for that publish against the header's rows), logged per frame on mismatch.
- Q3 (why right after a settings read): the layer's settings read (util.cpp cfg_read_files) touches no hair or capture
  state, and the capture declined no draw before the arrival, so the game itself started issuing the three LOD 0 hair
  draws. In all five runs this came 0.06-0.6 s after the runner started a plan phase (hl1 20.2 s after scene ready,
  hs1 23.7 s), not at a fixed scene time. What the runner sends to the game at a phase start besides the ini (window
  messages, input) decides it; not decidable offline. A player's settings change writes only the ini.
- No fix landed (the cause is not established). Left: the observable above, then the fix at the cause; the hl1 job
  should gate the natural arrival in its first phase change instead of holding one groom.
- Added (2026-11-10, same branch): the observable and the job. With detailed logging: a groom TLAS check at every strand
  G-buffer submit (hair_primary::TlasCheck: the newest build of the header's TLAS address against the header's own
  build; a line when another, already submitted build is traced, with instance counts and the first differing key, and
  a period line), and for the 6 frames after a hair draw is first seen waiting for its UV join a line with the groom
  rows (vs the previous publish), keyed rows whose row index now holds another key, and full BLAS builds. CTest
  hair_groom_asset_test test_tlas_check. hl1 rewritten to gate the natural arrival (warm-up 20 s, "lower" as in 161106,
  watch 360 frames; no hold, no fail-fast; predicted.json names what candidates A-D print). No shader or ABI change,
  variants delta 0.

## RTX Hair strand/card flicker while turning: header camera from the GPU (branch claude/hair-header-cam, READY_FOR_LIVE, 2026-11-09)
- Cause (item 11 of the 5i review, reopened): the header camera was the CPU's record-time read of the game's camera UBO;
  the groom points come from the GPU's read at execute. rv3-s8 220145 turns: sampled origin 0 -> 94-98.5% strand
  share (8/8 frames), origin 2.6-15.5 cm -> 18-80% (7/7); 0-23 frame-watchdog trips per 300 frames, each holding
  every hair draw on cards for 28 frames (up to 2045 watchdog refusals per line).
- Proof counter (always on, CPU only): header safety line "build camera re-read at submit (item 11): N publishes
  checked, M differ (max X m), K header cameras copied on the GPU"; sampled-ray line adds the header camera and the
  evidence strand share.
- `rt_hair_header_cam_gpu` (live, off, Advanced knob "Strand hair: GPU build camera"): the build copies the UBO camera
  into the header on the GPU (geo_copy_camera + two 12-byte copies; previous camera from a device slot). No shader
  change, variants delta 0. Cost: one tiny dispatch per publish (not measured live).
- `rt_hair_watchdog_draw` (live, off, Advanced knob "Strand hair: per-draw watchdog"): per draw key, 2 bad judged
  frames in a row (one header per G-buffer frame), hold kWatchdogFrames (28) for that key only. Unit test in
  hair_groom_asset_test.
- "with a strand" counter: correct but misnamed (shared trace of swapped slots only; probes trace privately). Relabelled.
- Gates: build -Test, settings test, no shader request changed. Live job staged (not queued):
  tools/live_batches_staged/hair_cam/jobs/hc1-s8-header-cam.json, predicted.json; live_flow lint passes.
- User check: S8 or Test Dialogue1, turn the camera with both keys on: hair stays strands, no flash to cards.

## rt_capture_wide_split on by default (branch claude/wide-split-on, 2026-11-09)

- rv2 (Water Queens House zoomed out, release Ultra, FG 3, RTX Hair, warm cache, idle GPU, 14:55) against rv1
  (same plan, key off, 14:47): RT capture opaque drops 0.3-2.0 -> 0.00 per frame in every phase; opaque peak
  1600 -> about 1190 MB; 88-byte strand-frame draws about 12/frame, 83 MB, 0 left out; RT passes 8.5-9.5 ->
  7.4-8.9 ms, GI 11.1-12.5 -> 9.5-11.7 ms, scene wait unchanged. Kept, on.
- The first rv2 (14:30) and the 14:37 rerun were confounded (a video render on the GPU; then the first launch
  after the build compiled 54 GI pipelines in 260 s). Structural counts matched rv1 in every run.

## Strand resolve finds its frame's snapshot under a moving present (branch claude/strand-resolve-fallback, offline, READY_FOR_LIVE, 2026-11-09)

- Symptom: user session 2026-11-09 07:59 (0.9.5, Ultra, fg=3, fps_cap=58, rr_hair_strand_aa=2): 145-395 of 600 strand
  resolves per period fell back to the game's dither resolve while 599 of 600 snapshots ran, so analytic coverage and the
  dither alternated frame to frame. Queue lives (fixed camera, no cap) showed 0-3.
- Cause: the snapshot ring was keyed on frame_index() (counts presents). The present thread often advanced it between a
  frame's G-buffer end and its post-chain resolve, so the resolve looked for the next frame's snapshot.
- Fix: common/strand_snapshot.h Ring keys on recorded frames (a frame begins at its first snapshot after the last
  resolve, ends at its resolve); layer/rrpost.cpp record_strand_snapshot / strand_resolve use begin/wrote/take. A frame
  with no snapshot falls back once and never reads an older snapshot. Layer only (game restart), no shader or ABI change.
- Test: hair_groom_asset_test test_strand_snapshot_ring (600 of 600 frames, site merge and decal dedupe, refused
  snapshot, broken chain, one-in-three snapshots gives 400 consistent fallbacks). settings_test passes.
- Live check: any normal play with RTX Hair and exp_hair_strand_aa=coverage: the 600-frame line reads 'N frames, 0 fell
  back' with N equal to the 'strand snapshots' count of the same period (predicted 0 fell back).

## Strand resolve paired with its own frame's snapshot by submission order (branch claude/strand-pair, offline, READY_FOR_LIVE, 2026-11-09)

- Symptom: rv1-s8 (Water Queens House, fg=3, Reflex, rr_hair_strand_aa=2): every 600-frame period 600 snapshots, 0
  refused, yet 113-140 resolves fell back; Dialogue1 (fg=2) 0.
- Cause (code read): the G-buffer end (snapshot) and the post chain (resolve) are recorded on different game threads in
  different command buffers. With snap N + 1 recorded before resolve N, the recording-order Ring gave resolve N the image
  of frame N + 1 (on the GPU it still held N - 1: a wrong frame, silently) and resolve N + 1 fell back. No per-frame
  identity is visible at both sites (frame_index, the ngx G-buffer camera and the hair tag are present- or latest-based).
- Fix: common/strand_snapshot.h Pairing replaces Ring. One snapshot image (at the resolve's GPU time it holds the
  snapshot submitted last). The k-th snapshot and the k-th resolve share an ordinal; layer/rrpost.cpp
  rrpost_submit_begin (layer.cpp, before vkQueueSubmit and vkQueueSubmit2) writes the resolve's host-visible flag with
  its token only when the snapshot submitted last carries its ordinal and was written. shaders/rr_strand_aa.hlsl
  strand_resolve otherwise reads no snapshot (exactly the game's resolve). A mismatch resyncs the offset. Refused
  snapshots (no camera) and resolves after a broken chain still count, so both sides follow the same frames.
- Layer + one shader request (rr_strand_aa.hlsl strand_resolve; no other request changes); game restart; no ABI change.
  Snapshot memory 2 -> 1 RGBA16F image (-22 MB at 2560x1067). CPU: a few compares per submit.
- Test: hair_groom_asset_test test_strand_snapshot_pairing (in order, overlapped snap N/N+1 then resolve N/N+1,
  inverted, 600 mixed frames: all own frame, 0 refused; refused or missing snapshot, missing resolve, out-of-order
  batches, resubmission: refused, never a wrong frame). build -Test 220/220; rr_strand_aa.hlsl compiles and passes
  spirv-val (shader_identity --validate). host_atomics_lint classifies the flags (PairFlags, read only).
- Live check (same scene as rv1-s8, fg=3): the period line '[rrpost] strand resolve ... N frames, M fell back ... (no
  snapshot of the frame: a at recording, b at submit); pairing: r resyncs, snapshots recorded up to k ahead' should read
  M near 0 with N + M about 600, r near 0 in steady play, and k >= 1 (confirms the overlap). If b stays high with r
  high, the batches are not in frame order (the premise): then it fails safe (the game's resolve).

## Scene handout design (branch claude/scene-handout-design, design only, 2026-11-09)
- docs/SCENE_HANDOUT_DESIGN.md: how to remove the `[rt] scene wait` (3.3 ms static, 5-7 ms motion at S8).
- Design: set 1 update-after-bind, TLAS read by address from the slot's scene stamp (as the groom TLAS already is),
  fallback to the previous slot by one descriptor-set copy at vkQueueSubmit. All four review blockers answered.
- Smaller steps (notify before AS recording, scratch reuse) remove at most ~1 ms; the rest is the game's recording.
- Next: run the prepared sh1 job (rt_scene_wait 1/0/1, uncapped) to get the frame-time ceiling; build only at >= 1 ms.

## Scene handout increment 5: traversal flag (ABI 31) and water on the handout (branch claude/scene-handout-5, 2026-11-09)
- sh2 live (18:43, S8 real Ultra): ON wait mean 0.47-0.69 ms vs OFF 4.4-5.1, 0 fallbacks, 0 pairing mismatches, frame
  median 24.2-24.6 vs 24.6-25.2 ms (GPU-bound), rt passes 6.2-6.6 vs 7.2-7.5 ms. Two findings fixed here:
- `[rt] scene handout: N stamp traversal downgrade(s)` at 59-60 per 60 frames in every window: water's reference
  lookups (rt_omm_scene_reference, which water never uses for traversal) downgraded the slot's OMM traversal address,
  so with rt_scene_handout >= 1 the OMM variants traced the reference TLAS every frame. Host ABI 31:
  `rt_omm_scene_reference(..., bool traversal)`, water passes false; only a real traversal refusal downgrades
  (scene_handout::downgrade_on_refusal). Rebuild both DLLs. `[core] [core]` double prefix dropped.
- Water took the committed pair once per frame (`water 60` settles, a few about 3 ms waits when run_post came before the
  commit). Now `water_rf` takes the handout when its pipelines carry constant 1051 (`water_handout_ready`) and its
  command buffer joins the handout; compose joins it for this frame's lighting or, once resolved, uses the committed
  scene of the stash's frame (rt_scene, no wait), never the dummy alone (scene_handout::water_handout_pick). Its build
  camera is the ring entry of the handout's frame (written by that commit before the compose runs).
- Logs: `[core] scene handout: ...; water on the handout W`, `[water] compose ...; scene handout N`.
- Tests: scene_handout_test 20 (downgrade rule) and 21 (water pick, water joins only its frame's open handout).
  No shader change. Key off identical. Built and tested only after the coordinator's "build ok".

## Scene handout increment 4b: native hit materials under the handout (branch claude/scene-handout-4b, READY_FOR_LIVE, 2026-11-09)
- The hit-material query of a reserved slot is built at its commit (`native_hit::commit_query`, layer/hit_material.cpp):
  `register_use(..., commit=true)` on the G-buffer command buffer (row uploads, join/remap/gather recorded there, before
  the lighting in the same submit) into a per-slot handout query set (`handout_set`, update-after-bind pool, allocated
  at the reservation). Frame/generation checks unchanged (slot frame and generation at the commit, row header on the GPU).
- The lighting's hit-material passes (rt_hit_material_use with the dummy TLAS and set R) get that query set at once and
  record the camera they bound; the submit settle checks it against the query's camera. Mismatch (kCamera) or no query at
  the commit (kQuery: packet missing, no exact mapping, capacity, recording domain) resolve to P: set P and P's live query
  set of its own generation (else P's plain set, whose rows header rejects every row) copied into R's; R poisoned.
- Lifetime: in order, every user recording owns the commit-time query (recycle collect retires it only when the G-buffer
  and all lighting recordings are GPU-done); on a fallback the users own P's query.
- Tests: scene_handout_test cases 16 (query failed at the commit), 17 (camera mismatch), 18 (query built, state reset per
  reservation, other slot ignored), 19 (recycle ownership model); 223/223 CTests. No shader, ABI or variants change.
  Key off identical (no reservation: register_use runs as before; commit=false keeps every check).
- Handout line gains `hit material queries at the commit Q, failed F (fallbacks: query X, camera Y)`.
- Live job sh2-s8-scene-handout now runs the real Ultra preset (hit materials + OMM on); predicted.json first; live and
  frozen lint 0 failing. Not run.
- Risk to watch first in the live: the commit-time query requires the G-buffer command buffer to be a one-time recording
  (`one_time` in register_use); if not, every query fails and every handout frame falls back (fallbacks query = frames).

## Scene handout increment 4: OMM on reserved slots; hit materials analysed (branch claude/scene-handout-4, offline-verified, 2026-11-09)
- OMM: a reserved slot keeps the OMM accelerated TLAS in stamp byte 24 when `bvh_omm_tickets` (layer/bvhsplit.cpp) gives
  its micro-map tickets; the commit registers them on every recording that bound the handed-out set (`Machine::accelerate`
  hands back users and their command buffers), later joiners register in `rt_scene_handout`. A recording that already
  ended cannot hold tickets: byte 24 goes back to the reference (monotonic). Handout line: `OMM accelerated commits A,
  downgraded D` appended (existing gates unchanged).
- Native hit materials: still refuse the handout. Exact cause and the increment 4b plan (query built at the commit in the
  G-buffer command buffer, per-slot query set, camera check at the submit settle) in docs/SCENE_HANDOUT_DESIGN.md.
- Legacy single-BLAS path: left on the waiting path (shared TLAS, never fills a reservation).
- Tests: scene_handout_test cases 14 (accelerate: users + handles, only the committing slot, joiners, reset on close and
  on a new reservation) and 15 (ticket lifetime: an ended user forces the downgrade); 223/223 CTests. No shader, ABI or
  variants change. Key off identical (no reservation: nothing new runs).
- Live job sh2-s8-scene-handout unchanged in plan (Ultra without hit materials, OMM on): predicted.json gains the OMM
  observables (accelerated commits > 0, downgraded 0, RT GPU ms equal to OFF). Not run.

## Scene handout increment 3: handout without the scene wait (branch claude/scene-handout-3, READY_FOR_LIVE, 2026-11-09)
- `rt_scene_handout=2` (live; needs `rt_scene_handout_layout=1` at startup; not with `rt_hit_material`): the capture
  begin reserves the slot R the build will write (P = the current scene); the lighting's first RT query binds set R and
  the dummy TLAS without waiting and without the scene lock; spec-1051 shaders trace the TLAS address the build writes
  into R's stamp. At every vkQueueSubmit the layer resolves an open handout before the driver call: in order (R's build
  earlier in the batch or submitted) or the fallback (set P copied into set R, P's traversal address downgraded).
- common/scene_handout.h: the state machine (reserve, hand_out, commit, resolve, pins). tests/scene_handout_test.cpp:
  13 scripted cases (in order, lighting first, build failed, order, committed first, no capture, dropped, toggles,
  second capture begin, extra users, retirement, older frame, stale reservation) and every order of capture begin /
  lighting query / capture end with three submit splits, three frames each; the safety model (traced scene's build
  executed, slots never reused while bound) catches 13 violations when the fallback copy is removed (mutation control).
- Lock split (layer/rtscene.cpp): `g_handout_mu` is a leaf for the lighting thread (never `g_mu`); `g_wait_mu` for the
  wait line. Order g_mu -> g_wait_mu -> g_handout_mu. `slot_retired` keeps R and P while the handout is open.
- Core: `prepare_omm_traversal` lets a pass run on the handout only with a constant-1051 pipeline and a registered command
  buffer; otherwise it settles once per frame to the committed pair (rt_scene_set wait) and a pipeline without the
  constant keeps the next two frames on the waiting path. Water settles before `water_rf` (it needs the real TLAS).
- Host ABI 29 -> 30 (`Bg3HostApi::rt_scene_handout`): rebuild both DLLs. No shader change (variants.txt delta 0).
- Logs: the wait line gains `, handed out N (0 ms)`; `[rt] scene handout: ... fallbacks F (not committed, build failed,
  order, dropped), pairing mismatches M ...`; `[rt] scene handout fallback: ...`; `[core] scene handout: ...`.
- Limits (increment 4): OMM acceleration off for reserved slots (tickets per user recording), native hit materials
  refused (their query reads commit-time rows on the CPU), legacy single-BLAS builds never fill a reservation.
- Key off: no reservation, no submit work, wait line unchanged. Tests: 223/223 CTests (new scene_handout_test).
- Prepared, not run: tools/live_batches/rel095_verify jobs/sh2-s8-scene-handout.json (S8, uncapped, focused, 0 / 2 / 0
  in turns and static windows, check windows with rt_primary_check), predictions in predicted.json.
- Live job lint: live corpus 0 failing, frozen snapshot 0 failing after extend_readiness (5 queries) and 8 new regexes
  added with 0 matches (a full lint_snapshot refresh would shrink the corpus 79 -> 27 VALID logs: not done).

## Scene handout increment 2: scene TLAS by stamp address (branch claude/scene-handout-2, offline-verified, 2026-11-09)
- Layer: every scene commit writes the TLAS addresses into the slot's stamp (bytes 16 reference, 24 traversal = the OMM
  accelerated TLAS when the commit chose it); `rt_omm_scene_reference` downgrades 24 to the reference when a recording
  refuses the accelerated TLAS (never back up; `[rt] scene handout: N stamp traversal downgrade(s)` with the wait line).
- Shaders: constant 1051 (`kSpecIdSceneHandout`), `RT_TRACE_SCENE` in rt_common.hlsli at the 8 GI/visibility sites,
  ls_ref_body and hair_receiver: OMM variants trace word 24, others word 16, address 0 falls back to gScene.
- Core: live key `rt_scene_handout` (default 0) adds 1051 = 1 to gi/ and water pipelines; binding 18 stays the real TLAS.
- Key off: shader_identity vs main 84/226 byte-identical; the 142 that include rt_common differ in raw bytes only:
  tools/scene_handout_spec_check.py proves all 154 entries the same program with 1051 = 0 (others symbolic), and 1 differs
  in 142 (control). Identity run 242 s (148 compiles), equivalence 781 s. variants.txt delta 0. No ABI change.
- frame_replay `--handout 1|2`; self-test 4c: spec 0 vs 1 bit-equal on the synthetic pack, empty-TLAS control differs.
  No real replay pack on disk (lost at the 11-07 format). 222/222 CTests pass (retirement gate: 1051 fixed 0 like 1026).
- Live (not run): toggle rt_scene_handout 0/1 in one session; GI/shadow/RF energy and rt_primary_check equal, GPU ms equal.

## Scene handout increment 1: update-after-bind set 1 layout (branch claude/scene-handout-1, offline-verified, 2026-11-09)
- New restart key `rt_scene_handout_layout` (default 0; docs/SCENE_HANDOUT_DESIGN.md 3.2). On: the layer asks for
  storage-buffer and sampled-image update-after-bind in the game's own descriptor-indexing (or Vulkan 1.2) features
  struct, acceleration-structure update-after-bind in its own struct; set 1 (layer/alphatest.cpp) gets
  UPDATE_AFTER_BIND on every written binding (immutable samplers excluded), its pool and the hit-material clone pools
  the UAB pool flag; an empty dummy TLAS is recorded once into the first scene build's command buffer
  (`rt_scene_dummy_tlas()`, valid once that recording was submitted). If the device creation fails with the request,
  the layer retries without it. Nothing consumes the layout or the dummy yet (increments 2-3).
- Off: no device, layout, pool or command change; no shader or variant change (shaders/ and
  common/shader_variant.h untouched, so every request is byte-identical; variants.txt delta 0). No ABI change.
- Offline: build + 222/222 CTests pass. New CTest scene_handout_gpu_fixture (RTX 5090, Khronos validation, private
  loader, no game): set 1 look-alike with the layer's flags; update after bind (stamp, one of 4096 views, TLAS),
  second update, fallback copy P -> R after bind all read back right; empty TLAS traces no hit; 0 validation errors.
  UAB sampled-image limit per stage 1048576 (set 1 needs 4176).
- Not tested in game: the feature patch on BG3's device. Check at the next live: log lines
  `[rt] + scene handout layout`, `[rt] scene handout layout on`, `[alpha] set 1 layout update-after-bind`,
  `[rt] scene handout: dummy TLAS ... recorded`, and the GPU ms of the RT passes unchanged.

## RTX Hair switchable while playing, hotkey Ctrl+Shift+F7 (branch claude/rtx-hair-live, READY_FOR_LIVE, 2026-11-09)
- RTX Hair (exp_hair_groom_primary) is Apply::Live. Startup parts now prepared on every capable device (LSS, shaderInt64,
  fragment stores): strand header 3.5 KB and pipeline variants (about 0.1 s at the first level load, from hcap3 logs;
  new `strand variants` line measures it), occlusionQueryPrecise, world-space tangent compiled in (spec 1045 = 1),
  BC3 TRANSFER_SRC for the capture (memory requirements identical on the RTX 5090). Only rt_hair_physical_origin
  stays startup (43.7 MB at 2560 x 1067 plus GI variants): a game started with RTX Hair off shows the restart banner
  for it while the strands come on live.
- Live key rt_hair (default 1): 0 makes the whole RTX Hair bundle read 0 (cards only, same frame or next; in-flight
  prepass drops restored by the existing depth latch; stale publishes never applied after a switch-on).
- hotkey_rtx_hair (default Ctrl+Shift+F7, no conflict with F8/F9/F10), launcher Lighting page; flips rt_hair in memory,
  one `[hair] hotkey` line. Native fallback launcher keeps its two shortcut fields (validation covers all three).
- Tests: hair_live_test (on, off, on with frames in flight), hotkeys_test, hotkey_poll_test (hair edge, two queues),
  settings_test, web_bridge_test, launcher_ui_test. No shader source change (specialization value only).
- Live job tools/live_batches/rtx_hair_live (rhl1-dialogue, ~111 s of phases, lint 0 failing), not queued.
- User check: Test Dialogue1 with RTX Hair On, press Ctrl+Shift+F7: hair turns to cards and back, no flash of
  missing hair. Cost of the always-on startup part: the `[hair primary] strand variants` log line.

## Release verification lives 2026-11-09: rise reserve on, capture limits found (branch claude/rise-reserve-on)

- `rt_capture_rise_reserve=1` by default. rv1 (Water Queens House, camera lowered 10 notches, release Ultra,
  FG 3, RTX Hair, 12:11): cascade capture 4480-4640 MB off, 3120 MB with the key, 4096 MB off again
  (-1.0 to -1.5 GB); `[cascade] casters ... buffer full 0.0` in every window. BLAS chunks flat at about
  4.09 GB, no stall report. Kept.
- `rt_alpha_floor_yield` stays off: opaque drops 2-8 -> 0.03-1.4 per frame in that view, but 0.02-0.15
  alpha draws/frame made opaque. The view needs 2065 MB (opaque 1620 + alpha 445) of the 2047 MB cap
  (2^31, shader addressing): RTX Hair's 88-byte capture vertices (64 without) push it over. Next step for
  wide views: a per-draw stride or a second capture buffer, not the partition.
- Strand resolve (rr_hair_strand_aa=2): 0 fallbacks in he1 play windows (Dialogue1, FG 2; the morning fix
  works), but at S8 with FG 3 113-140 of 600 frames fall back with 600 snapshots and 600 resolves:
  overlapped frame recording, fix on branch claude/strand-pair.
- he1 VALID 11:30; rv1 attempts INVALID by design (gates written before the S8 overflow was known), mined.

## Mesh cache budget charged with its own BLAS (branch claude/mesh-cache-rate, READY_FOR_LIVE, 2026-11-09)

- Cause (rv2 14:57-14:59, S8 zoomed out and turning, main 0b45b0c8): `rt_mesh_budget_mb` (2048) charged the whole
  pool plus EVERY BLAS chunk byte (`as_bytes()`: per-draw, cascade, sun, groom BLAS and pending ranges). In the turns
  pool + all BLAS was 2.5-3.8 GB (e.g. 14:58:21: pool 844 + AS 1677 = 2521 MB). Then every new mesh was declined
  (`mesh_cache_admit`, counted as "deferred by the per-build budget": 30k-49k per line = every non-pattern captured
  draw, about 250 per build) and the LRU evicted every mesh not drawn in the last 8 frames, each frame, because its
  90% target is unreachable while non-cache bytes alone exceed it: 28-60 meshes left, 300-700 LRU per line.
  The declined draws stay captured with uncompacted per-draw BLAS, which keeps the total over: a loop.
- Not the cause: the per-build budget (`rt_mesh_new_tris` 4M, 1024 meshes), verification throughput (241-646 verified
  per line, 0 mismatches), the pool cap (pool 2047 MB, 528-1463 used). The "pattern" rule (VS not class A: skinned,
  wind, UBO-offset variants) is by design: 359 of 610 captured draws per build. In steady windows captured draws equal
  pattern draws (381 vs 385), so about half of the 4.0M captured triangles (2.0M; 4.0-6.2M in the turns) is static,
  instanceable and was only kept out by the budget accounting.
- Second defect: admission refuses AT the budget, the LRU ran only ABOVE it: a cache filled to the budget kept stale
  meshes (rv2 15:00:39-51: 2044-2047 of 2048 MB, about 15 draws declined per build).
- Fix, `rt_mesh_budget_own` (live, default 0, developer key): 1 charges the whole pool (shared, hard-capped, pending
  included: the 2026-11-03 load-admission rule unchanged) plus only the cached meshes' own BLAS; a decline at the
  budget triggers the LRU pass (still only meshes unused for 8+ frames, oldest first, down to 90%). Other BLAS stay
  under the VRAM guard's chunk cap and its mesh LRU ask. Key off: identical code paths and values (the LRU plan helper
  replays the old loop exactly). Host only: no shader, ABI or variant change; NO_PIPELINE.
- `[bvh] mesh cache` line appends `budget (rt_mesh_budget_own=K): charged X / 2048 MB (pool, cached-mesh BLAS, other
  BLAS), N draws declined`.
- Tests: rt_capture_budget_test (charged bytes, rv2 admission and LRU cases, LRU plan = old loop over 1300 cases, a
  turning-view model: visible meshes cached off 0.00 / on 1.00, 0 re-admissions on a ring that fits, charged <= budget).
- Expected (predicted.json): turns with the key on keep 300-600 meshes, captured triangles 4.0-6.2M -> 1.7-3.6M
  (the pattern draws), BLAS GPU median in motion 2.4-4.3 -> 1.0-2.0 ms (p99 7-13 -> 3-6), scene wait 5.6-7.5 -> 3-5 ms,
  no BLAS chunk refusals by the VRAM guard (rv2: 42-146 per window, cascade casters' BLAS left out).
- Live (prepared, not run): rel095_verify rv3-s8-mesh-cache-budget, the rv1 view and turns with release defaults,
  key 0 / 1 (two turn pairs) / 0 in one run; every window: capture, cascade, stride split, scene wait, `rejected N/0/0`.
  In game: S8 zoomed out, turn the camera; RT shadows/GI of buildings and props as before, smoother turns.

## Mesh cache headroom for the frame's other users; no build fails on hair bounds (branch claude/mesh-cache-rate-2, READY_FOR_LIVE, 2026-11-09)

- rv3 (183745, 191231): with `rt_mesh_budget_own=1` the cache filled (433 meshes, 3.6M triangles) and took the pool
  (2034 / 2047 MB) and the BLAS chunks (guard cap 4013 MB). The hair bounds pool request then failed `bvh_build`
  outright; freed ranges retire only behind a newer built scene, so no scene was built for the rest of the run
  (118 of 120 frames skipped, RT passes off).
- Fix 1 (bug fix, key-independent): a hair draw whose bounds do not fit is left out of that build (counted), never
  the whole build. Same output whenever the old path succeeded.
- Fix 2 (`rt_mesh_budget_own=1` only): the cache keeps out of the mandatory other users' peak demand + 25% in the pool
  (bounds, grooms, OMM indices) and in the BLAS chunk guard cap (per-draw, cascade, sun, groom BLAS); refused requests
  count as demand; a share pass evicts meshes unused for 8+ frames when the cache is over a share or declined a mesh;
  hold snapshots keep out of the reserve. Key off: identical.
- Tests: rt_capture_budget_test starvation model with the guard cap (old own rule: 300 of 300 builds fail, pool stuck;
  headroom: 0 fail, 99.9% visible cached, pool peak 1947 / 2047 MB), share/peak/hold-limit unit checks.
- Live: rel095_verify rv3 again (same plan); new gates in every window: 120 split builds per 120 frames, 0 skipped
  frames; ON windows: 0 hair draws left out. Report the cache size and captured triangles in the ON turns (smaller
  than the first rv3 fill, by the reserve) and the share/peak numbers of the mesh cache line.

## Launcher UX: Home dashboard, RTX Hair conversion panel, Hotkeys page (branch claude/launcher-ux, offline, 2026-11-09)
- Navigation: Home, Lighting, Surfaces, Performance, DLSS 5, Hotkeys, [Experimental], [Advanced: developer only],
  Help & support. Release Advanced held only the RT hotkey, so it is hidden (contract already said so) and the key
  moved to Hotkeys. DLSS 5 has its own icon.
- Home is a dashboard: Play card (preset, Play, version and GPU, and a Status list: game folder, GPU, mod files,
  unsaved changes with Save, restart pending); RTX Hair panel; Shaders; In-game keys. Removed the three performance
  shortcuts (SER and the RT pipeline are on in every preset; Frame Generation is the first Performance card).
- RTX Hair panel: the Lighting control and its in-game key (when the bridge has `hotkey_rtx_hair`), tiles Ready / Seen in
  game / In the game / On disk, progress with percent and ETA, the last error, Start / Pause / Resume / Try again,
  Convert: Styles seen in game / All styles (disabled with a one-line reason until granny2.dll and the game folder are
  set), granny2.dll picker with where players get it (their own LSLib ExportTool download, not shipped), When to convert.
- Launcher side of agent C's interface: `bg3rtx_hairconv --all --game <dir> --granny <dll>`; preparation.json `total`,
  `source` (or `mode`) and `error` are optional; launcher key `hair_granny`; prep actions start, convertAll,
  browseGranny, clearGranny. Unit tests in launcher_preparation_test (command lines, reasons, tolerant facts, JSON).
- Hotkeys: the bridge and launch code iterate `bg3::hotkeys::ids` (State shortcuts are std::array sized from it), send
  `shortcutDefaults`, and refuse a key used by any other binding. Agent A's third id appears without launcher changes.
- Badges: Applies now / Applies after saving / Restart to apply ("May briefly pause" folded into the title and Details).
- Help & support: no Preparation copy; compatibility and video memory merged into Graphics details.
- User text: "RTX Hair" everywhere (preparation title, `exp_hair_groom` label "RTX Hair in ray-traced lighting" shown by
  raise_needs; schema regenerated). Left for agent A: the RTX Hair knob description still says "(see Preparation)".
- Check: open BG3RTX.exe; Home shows Play with status, RTX Hair conversion, Shaders and keys; Hotkeys lists the keys.

## RTX Hair warm-up after a level load (branch claude/hair-warmup, READY_FOR_LIVE, 2026-11-09)
- Cause (he1/sh2 logs, code): the LoadGuard quiet window pauses every strand swap 300 frames after each level load.
  Grooms are joined (UV readback 1-3 frames), uploaded (2-3 frames) and keyed (+2) within ~15 frames of the first hair
  draw; then they wait ~250-290 frames. First strands came ~+300..+330 frames after the load (~10 s unfocused).
  Second cause: a new groom slot starts at LOD 0 (no bounds yet) and reaches its screen-size LOD only after a 30-frame
  vote (a second upload and full build 30-50 frames in; sh2 LOD 1/2 uploads 0.9 s after LOD 0).
- Fix, key `rt_hair_warmup` (live, off; knob exp_hair_warmup "RTX Hair fast start", developer): the quiet window ends
  after 30 settled builds in a row (no join pending, no LOD upload/vote, no full build, every groom keyed), 300 stays
  the upper bound; a slot's first screen-size measurement picks its LOD at once. All boundary resets and swap gates stay.
- Rejected: persistent join cache (the readback is the key; handles reused across loads), groom prefetch, all-LOD
  upload (lod_keep already hides LOD waits; 1.5-2x pool), see docs/RTX_HAIR_PRIMARY.md 5o.
- Lines (both key values): `[hair primary] warm-up: level load at frame B: load quiet ended ... first strand G-buffer
  swap at frame F, m frames after the load`, per-period first-swap and groom first-key waits.
- Expected: load to strands ~+300..+330 -> ~+55..+170 frames. Cost: CPU only; no shader/request/ABI change.
- Tests: hair_groom_asset_test (LoadGuard::settle, lod_vote). Live job tools/live_batches/hair_warmup hw1-dialogue
  (first load on, F8 reload off, F8 reload on; predicted.json), not queued.
- In game: knob on, load Test Dialogue1, watch the party's hair: strands within 1-2 s of the scene appearing.

## rt_hair_warmup on by default (branch claude/hair-warmup-on, 2026-11-09)

- hw1 (Test Dialogue1, 20:46, VALID): first strand G-buffer swap 73 and 66 frames after a level load with the key on,
  309 frames with it off (the fixed 300-frame pause); load quiet ended "groom state settled" at +63 / +57; dropouts 0
  in every window. Kept, on (it acts only with rt_hair_groom_primary=1).
- Same round (evening-20261109): sh2 scene handout passes every gate but the in-motion wait (0.6-0.7 ms, static 0.06),
  0 fallbacks, water on the handout 59/60, no false OMM downgrades; rv3 / rv5 VALID but the camera did not move in
  them (captured triangles flat at 1.3M), so the cache headroom and the BLAS cap by occupancy are still unmeasured
  under load.

## Static-camera strand flicker: diagnosis and staged burst job (branch claude/hair-static-flicker, READY_FOR_LIVE, 2026-11-09)
- User: strands appear and disappear very fast with a still camera; rv3 220145 rules out swapping (strand draws
  720/720, coverage 94-96%, 0 refusals).
- Code read: no jitter, depth or motion-vector mismatch for strand pixels (ray and depth from the jittered vp,
  hair_primary_gbuffer.hlsl:238-245/:374; motion vector = the native card formula, hair_primary_velocity.hlsl:141).
- Most likely: binary per-pixel strand coverage under the jitter (4-6% card pixels without a strand, card-edge pixels:
  the candidate region is the jittered card raster), possibly amplified by RR guides that change per strand (fibre
  normal) and by fg=3. Details: docs/RTX_HAIR_PRIMARY.md 5p. No shader change, no key.
- temporal_burst.py: class-5 toggle rate, stable vs toggling flicker, burst-half noise, RR attenuation (CTest
  hair_groom_test test_burst_coverage_split).
- Staged live (not queued): tools/live_batches_staged/hair_static/jobs/hs1-s8-static-hair.json, one S8 launch, 3
  bursts (strands aa 0, strands aa 2, cards), ~1.7 GB; decides coverage vs reconstruction (predicted.json).
- User check after a fix: Water Queens House, camera still on a party member's hair, no fizzing strands.

## Strand edge dithering: strand-aware dither resolve and analytic coverage (branch claude/hair-edge-quality, READY_FOR_LIVE, 2026-11-09)
- Diagnosis (docs/RTX_HAIR_PRIMARY.md 5n): binary sub-pixel strand coverage on the shared jitter, and the game's
  stencil dither resolve (7401d785, ported in rr_post.hlsl) mixing strands by the card dither tags the strand clones keep.
- Key `rr_hair_strand_aa` (knob exp_hair_strand_aa, Advanced, live 0/1/2, off): new request rr_strand_aa.hlsl dispatched
  instead of the resolve (layer/rrpost.cpp). 1: strand footprint untagged; 2: plus analytic coverage (found strands
  weighted by 1 / found probability, GBAA-style colour exchange). Key off: no existing request's inputs changed.
- Offline (tools/hair_groom/edge_stability.py, hair_groom_test test_strand_edge): edge flicker raw 0.0178, card-tag
  resolve 0.0117, analytic 0.0086; edge energy +0.03% / -4.2% / +1.3% vs the area reference.
- Rejected: decorrelated per-pixel ray positions (flicker 0.034 -> 0.056, card-edge loss).
- Layer edits: hair_primary.cpp logs each hair pipeline's stencil state (one block, 12 lines); ngx.cpp ngx_gbuffer_camera.
- Live: tools/live_batches/hair_edge he1-dialogue1-edge (Test Dialogue1, 0/2/0/2 + 1, PNG + K8 bursts), not queued.
- he1 #1 live: strands write no stencil; the game's resolve never touches hair (Xr = compose on every hair pixel), so
  mode 1 is moot; mode 2 changed no hair pixel (coverage path inactive on the GPU). Debug mode 3 and window f-debug
  added; bursts now static 6 s timed windows (the 60-frame window closed before the K8 burst completed).
- he1 #2: the record has no current seal at the post chain (f-debug); the strand data are now snapshotted at the lighting
  pass into a frame-parity image. Baseline burst flicker: edge band 0.067 vs hair core 0.032 (final image).
- he1 #3 freeze: lock-order inversion (rrpost g_mu -> ngx g_mu at the lighting snapshot vs ngx -> rrpost at the evaluate);
  fixed (camera read before rrpost's lock, none in the resolve), test added. No live rerun without the coordinator.
- he1 03:04-03:17: mode 2 applied in 3 of 8 aa trace frames (~930 ring px changed, ring HF -12%), energy -0.1..-0.2% within
  noise. Burst windows fixed (out of periodic gates; tighten keeps a burst window's declared 14 s).
- Snapshot moved to the scene G-buffer end (once per frame; the lighting hook fired in 188-590 of 600); fallbacks counted.
- he1 03:38: not a pass: Xr unchanged on the hair ring in both aa traces (G-buffer-end snapshot holds no strand data); bursts
  confounded (whole-frame flicker +130% between them). Energy +0.31%, cost +0.11 ms. tools/hair_groom/edge_burst.py added.
- Snapshot sites (rr_hair_strand_snap_site 0/1/2/3 merged) with per-site device-local counters; he1 with site 3, a counter
  window and counterbalanced bursts off/aa/off/aa; predicted.json revised before the run.
- he1 04:02: not a pass: band/core flicker ratio unchanged (no edge-specific temporal gain), edge hf -14%, d-aa trace
  unchanged, energy -0.55%, cost +0.18 ms. Not proposed for the bundle.

## Convert all hair styles from the player's own files (branch claude/hair-convert-all, offline, 2026-11-09)
- `bg3rtx_hairconv --all --game <BG3> --granny <granny2.dll> --out <dir> --report-dir <dir>`: native LSPK v18 / LSF / LSX
  reader (LZ4 only; zstd refused, none in BG3's paks), the catalogue (1,217 hair and beard rows, cached by package
  fingerprint), reach categories and prepare tiers, Pillow-exact DXT5 masks, GR2 via the player's granny2.dll used only
  for section decompression (stock Granny 2.11 cannot open BG3's GR2 itself). No Python, PowerShell 7 or LSLib.
  Without the DLL: exit 3 and preparation.json state "unavailable" (capture model stays). Roadmap 2f has CLI and fields.
- Equivalence vs the 2026-11-07 Python batch, 28 styles: catalogue 1,217/1,217 rows, GR2 dumps 28/28 byte-identical,
  masks 28/28 identical, index id and verdict 28/28. Strands differ by median 0.4% (max 1.2%) from that run because
  strands2d.trace uses numpy's non-stable argsort on tied distances; with a stable argsort the Python reference equals
  the native records exactly on 28/28. Fix proposal for the reference: kind="stable" (not done).
- Full catalogue on this PC (4 jobs x 4 threads): 1,217 rows in 532 s; 898 pass, 282 review, 37 reject; 2.72 GB of v3
  grooms (scratch, deleted). Against the 737 batch rows: 736 same verdicts (one review-threshold flip from the tie order).
- CTest hairconv_all_test (synthetic pak, LSF frames, patch override, catalogue, synthetic GR2 without the DLL,
  granny-less path). LSLib MIT text still to collect before distribution (THIRD_PARTY.md).

## Diagnostic report: opt-in latest save, Home link (branch claude/diag-save, ready for review, 2026-11-09)

- Report dialog: new checkbox "Include my latest save (helps reproduce the problem; contains your character and
  progress)", unticked by default. The native fallback preview asks the same question (default No);
  `Diagnostics.cmd` asks once when started without arguments (Enter = No), or takes `-IncludeLatestSave`.
- Save selection (launcher `diagnostics::find_latest_save`, ps1 `Find-LatestSave`): newest `.lsv` by last write under
  `%LOCALAPPDATA%\Larian Studios\Baldur's Gate 3\PlayerProfiles\<profile>\Savegames\Story\<folder>\`, plus its `.webp`.
  Bounded nonrecursive listing, links skipped. Copied read-only with writers denied (a save being written is
  skipped with "close the game"), into `saves/<folder>/` with the DOS read-only bit on the archived copy.
  `CONTENTS.txt` names the save and its size. Cap 200 MB (lsv + webp): a bigger save is left out and named.
- The save bytes never reach the preview (JSON `binary`, no contents view; native preview shows the note).
- Home: the Play card ends with "Problem? Create a diagnostic report…" (same `report` message as Support).
  Native fallback Overview has the same button. No new knob or category.
- Tests: launcher_diagnostics_test (fixture profiles: newest Story save picked over older/decoy files, exact bytes,
  source time/attributes unchanged, cap, writer-locked save, none found, ZIP read-only bit via Python),
  launcher_web_bridge_test (save flag default off, bytes not in JSON), launcher_ui_test (Home link, unticked by
  default, no contents view for a save), diagnostics_package_test (ps1 latest save, none found, refused while running).
- Docs: USER_SETTINGS.md, release README, changelog_0.9.5 (Launcher). Packaging lists unchanged (no new files).
- Check in the launcher: Home > "Create a diagnostic report…", tick the save box, Preview files: the save shows as
  `saves/<folder>/<name>.lsv` with its size and no contents view.

## RT capture stride split: 88 bytes only for strand-frame shaders (branch claude/capture-stride, READY_FOR_LIVE, 2026-11-09)

- Bug: with rt_native_hair=1 every vertex shader was captured at 88 bytes per vertex. The zoomed-out S8 view
  (rv1 12:13 / 12:53, user settings) wanted about 2065 MB of the 2047 MB capture (opaque peak 1613 + alpha 446)
  and dropped 1-8 opaque draws per frame.
- New key `rt_capture_wide_split` (startup, default 0, needs rt_bvh_split=1): only the vertex shaders that
  export the native strand frame (236 of 2403 dumped VS) keep 88 bytes; all others are patched at the stride
  without native hair (64 with rt_hit_material). Wide draws get their own region at the top of the alpha
  region; cascade wide casters are a second geometry of the cascade BLAS. No native hair data is dropped.
- Readers: no shader change (split-mode readers take the stride per instance/geometry/push constant).
  Shader identity vs origin/main: 226/226 requests byte-identical; shaders/variants.txt delta 0. CPU: per-draw
  stride in geocapture, bvhsplit (draws, pool meshes, per-draw BLAS, OMM/groom/hold/extract jobs), rtscene
  (3-region conversion). Key off: identical code paths and values.
- Tests: new CTest capture_stride_test (activation, module stride, real patch_vs decorations 88/64, wide
  placement, cache/refit guards, S8 bytes).
- Expected S8 bytes: opaque 1613 -> about 1173 MB, alpha <= 446 MB: about 1.5-1.6 GB of 2047.
- Live (prepared, not run): rel095_verify rv2-s8-capture-stride (rv1 plan, key on from launch; a startup key
  cannot toggle in one run). Every window: opaque dropped 0.00, made opaque 0.00, stride split "left out 0.00",
  cascade buffer full 0.0; opaque peak vs rv1's 1613 MB; `[geo] RT capture stride split` and `[cascade]
  wide-stride casters` lines present. Report the gi GPU ms against rv1 (the 88-byte layout cost about 2 ms).

## RT capture alpha floor yield, BLAS stall report persistence (branch claude/capture-fixes, 2026-11-09)

- Evidence (live rv1-s8-release-verify, Water Queens House, camera lowered 10 notches, release Ultra + FG 3,
  RTX Hair): RT capture "opaque draws dropped (buffer full) 1.27" per frame with alpha 435 MB in an 818 MB
  region (the 40% wide-stride floor) and an opaque peak of 1568 MB of the 1229 MB left in 2047 MB. The floor
  also bounded the opaque cap and the demand split from below, so opaque never got more than 60%.
- `rt_alpha_floor_yield` (new, default 0): the opaque cap and the demand split may go down to the
  stride-scaled rt_alpha_mb floor (171 MB at 64 B); when both reserves do not fit, the split decides alone.
  S8 case: 444 / 1603 MB, both fit. The load frame keeps the 40% floor. Key off: identical (golden tests).
- `[vram] BLAS ranges awaiting retirement` now needs the floor exceeded and the oldest live scene frame
  unchanged for 300 frames. rv1 11:30 fired on a rebuild burst (1615 MB pending, all 7 slots fresh).
- Tests: rt_capture_budget_test (S8 split, base floor bound, unchanged fit and load cases).
- Check in game: rv1 job with rt_alpha_floor_yield 0 / 1 / 0 in the lowered S8 view: opaque drops 0.00 and
  alpha made opaque 0.00 with the key on.

## Scene slot material records pinned against pruning (branch claude/blas-retire-stall, READY_FOR_LIVE, 2026-11-09)

- Evidence: player log 2026-11-09. From 08:01:07 to 08:01:39 no scene was committed ("0 got this frame's
  scene"), and the mesh pool was full (1022 / 1024 MB) with its growth refused. 534 MB of freed BLAS ranges
  stayed pending. After builds resumed the pending bytes grew to 8 GB (15 MB live) by 08:02:05.
- Cause: the current scene slot is never re-picked while builds fail (pick_scene_slot skips g_scene_cur).
  Its material record ages past 4096 newer records, and bvh_rec_register prunes it. slot_retired
  (rtscene.cpp ~181) then reads state -1 and returns false forever. So the slot is never reused, and
  update_oldest_live (~194) keeps its old built frame. as_chunks::retired requires frame < oldest_live,
  so no freed range retires again. The other 7 slots keep rotating, so no "every scene slot" line appears.
  A likely trigger for the failed builds is the native-hair bounds pool_alloc (bvhsplit.cpp ~2328): it
  aborts the whole build when the pool is full. The F9 toggles coincided with this.
- Fix: the registry prune moved to vram_compact::prune_records and skips pinned records. rtscene pins a
  slot's material record (bvh_rec_pin) from assignment until the slot is built again. With at most 8
  pins, the registry stays bounded.
- Test: vram_compact_test replays 10000 records, the slot's record, then 30 s x 60 fps x 4 records. The
  record is lost unpinned and kept pinned, and is pruned normally after release. Limit and keep edges
  are covered.
- Live check (with claude/blas-chunk-growth): after a stretch with no commit, `[mem] ours` used minus live
  drops back within seconds of builds resuming. No `[vram] BLAS ranges awaiting retirement` line.

## BLAS chunk cap by occupancy for oversized BLAS (branch claude/blas-frag, READY_FOR_LIVE, 2026-11-09)

- Evidence: rv3 191231 key off, S8 zoomed-out turns 19:15:01-14: BLAS chunks 4189 MB reserved in 50 chunks,
  1237 MB live, little pending (56-500 MB), 0 empty, at the guard's cap (4013-4189 MB). 290-575 refusals per
  window, every sampled one larger than a 64 MB chunk: cascade casters' BLAS 64-111 MB (left out of the sun's
  RT that frame), grooms 72-136 MB, a groom compaction copy 113 MB.
- Cause: the reserve is about 48 sparse 64 MB base chunks (about 26 MB live each after the turns: per-draw and
  cached BLAS of different lifetimes; no relocation for per-draw BLAS) plus the oversized chunks. Empty chunks are
  freed (or freed at the cap) and retirement is not the limit (pending small). A range larger than 64 MB needs a
  chunk of its own and can never use the 2.9 GB free inside base chunks, while the guard counts reserved bytes.
- Fix, `rt_as_cap_occupied` (live, default 0, NO_PIPELINE): a chunk larger than a base chunk may pass the reserved
  cap while live + pending BLAS bytes plus it stay within the cap, up to cap + 25% reserved; an unguarded caller
  (cascade, compaction copies) that passes the reserved cap this way also passes the guard's Low check. Base
  chunks and key off: exactly the old check. Mesh cache line appends refusals, reserved, occupied and the cap.
- Rejected: rounding oversized chunks to multiples of 64 MB so emptied predecessors fit the next rebuild. In the
  replay it cut chunk creations 72 -> 18 but raised refusals 60 -> 104 (the rounding waste fills the cap).
- Test: as_chunk_alloc_test replays rv3's sizes (48 sparse base chunks, a 64-136 MB cascade rebuild per frame, a
  groom every 10 frames, 6-frame retirement, 120-frame reuse, 4013 MB cap): refused key off 60 / on 0 in 480
  frames, peak reserve 4013 / 4233 MB; chunk_within_cap edge cases and key-off identity with unguarded_chunk_ok.
- Live (prepared, not run): rel095_verify rv5-s8-blas-cap-occupied, the rv3 plan with the key 0 / 1 / 0; ON windows:
  `0 BLAS requests of the other users refused`; every window: 120 split builds, 0 skipped, capacity gates.

## BLAS chunks capped for unguarded callers; retirement-stall report (branch claude/blas-chunk-growth, READY_FOR_LIVE, 2026-11-09)

- Evidence: player log 2026-11-09 (0.9.5, RTX 5090, Ultra, RTX Hair on). After about 10 Ctrl+Shift+F9 toggles,
  BLAS chunks went 553 MB / 6 chunks (19 MB live) to 8196 MB / 91 chunks (15 MB live, 0 empty) at 08:02:05.
  The guard was Low and refused 64640 guarded requests. VRAM 32056 of 30392 MB, then a DLSS-G resource
  recreation failed and sl.common crashed (0xc0000005).
- Cause 1, the growth path: `as_create` (layer/bvhsplit.cpp) checks Low and the chunks' cap only for callers
  that pass `guard_what`. The cascade casters' BLAS and the mesh and groom compaction copies pass nullptr and
  could add chunks of max(64 MB, size) without limit. The cap at that budget is about 3.9 GB, so the 4.3 GB
  above it came only from these callers.
- Cause 2, why nothing came back: used minus live was about 8 GB. Freed ranges retire only when
  `frame < rt_scene_oldest_live()` (as_chunks::retired). A scene slot that never retires (or no new scene
  commit) pins that value, so every rebuild leaves a range pending and the cascade BLAS needs a new chunk
  each time. The log does not show which slot pinned it. 534 MB were already stuck from 08:01:14 to 08:01:39.
- Fix: `as_chunks::unguarded_chunk_ok` (common/as_chunk_alloc.h). An unguarded caller may add a chunk only
  while the reserve stays within the cap. Otherwise it is refused and logged in the guard window: the cascade
  casters are left out that frame, and compaction keeps the original. Retained empty chunks are freed before
  the cap check for every caller. Guard off: no change.
- Diagnostic: `BvhVram.as_pending`. Once per session, when pending passes 1408 MB (the chunks' floor, the
  largest live AS of the qualified long session), the layer logs `[vram] BLAS ranges awaiting retirement`
  with `rt_log_state` and `bvh_log_state` (slot states, oldest live slot).
- Test: as_chunk_alloc_test replays the stall: a 90 MB cascade BLAS rebuilt every frame for 120 frames. The
  reserve is 10800 MB uncapped and 3870 MB capped (77 rebuilds refused). Edge cases are covered too.
- Left: the root cause of the stalled retirement. Live check, same toggling in a busy scene: `[mem] ours`
  reserved stays at or below the cap (about 3.9 GB at a 30.4 GB budget), never 8 GB. If the stall happens
  again, the one-shot retirement-stall block names the slot that never retired.

## Background runs: the user's mouse no longer reaches the game (branch claude/bg-input, READY_FOR_LIVE, 2026-11-09)
- Cause: BG3 reads the mouse only through SDL2 2.30.6 window messages (no raw input, no relative mode;
  only SDL2.dll imports GetCursorPos/GetAsyncKeyState). Each runner-posted WM_MOUSEMOVE re-arms
  TrackMouseEvent; with the user's cursor outside the game (1066 visible client pixels in rv3/rv5),
  Windows answers with WM_MOUSELEAVE and SDL moves the game pointer to the REAL cursor and clears the
  mouse focus. With a still cursor (user away, rv3 .1.191231) the jumps cancel out; with the user
  moving it, the user's motion is added to the runner's drag and wheel (rv3 .1.203607, rv5 .1.204140:
  captured triangles flat at 1.3M in every phase vs 4.7M). Hover and inactive-window wheel also leaked.
- Fix (only BG3RTX_BACKGROUND=1 and the real foreground window belongs to another process): the runner
  marks every posted mouse message with wParam bit 0x0800 (`$BgMouseMark`); SDL2.dll's PeekMessageW/
  GetMessageW drop unmarked mouse messages and WM_MOUSELEAVE/HOVER (rewritten to WM_NULL), strip the
  mark from runner messages, and the game's SDL_GetGlobalMouseState returns the runner's last posted
  pointer and buttons. Keys untouched (real keys never reach an unfocused window). Game in front or
  normal play: unchanged (no hooks without the env var; mark stripped, nothing dropped when in front).
- Logs: `[bg] input filter on ... (peek 1 getmsg 1 global 1)`, `[bg] input filter active/idle`,
  every 10 s `[bg] dropped real input: mouse N leave N; runner mouse passed N; global polls answered N`.
- Test: CTest `bg_input_test` (classifier, button mask, every posted mouse message in auto-run.ps1 is marked).
- Next live (user at the PC, moving the mouse in another window): the wheel lowering and the rotation
  phases must change captured triangles as in the away run (about 4.7M, not flat 1.3M); `leave` grows
  with every posted move, `mouse` with the user's hover; the user's cursor never moves the camera.
- Not covered: SDL_WarpMouseInWindow still calls SetCursorPos if the game warps during a drag (not
  observed; would move the user's cursor, not the camera). A dropped leave leaves SDL's `mouse_tracked`
  set, so if the user later brings the game to the front SDL misses its mouse-leave (harmless in a test run).

## Release 0.9.5-beta: version bump and changelog (branch rel095, 2026-11-08)

- Version 0.9.5.0 in the launcher, UI preview, bridge JSON test and Nexus sticky post; new
  `docs/release/nexus/changelog_0.9.5.txt`; README notes the Preparation card and opt-in strand hair.
- Content: RT pipeline GI trace + SER on by default, OMM retry fix, AS chunk reuse, strand hair capture model
  (runtime capture, bg3rtx_hairconv, v3 storage, Preparation card), shader prepare before Play, hair fixes.
- Next: merge-green, package, setup, verify_rc under `.scratch/release/v0.9.5-beta/`.

## Packaged 2026-11-08 23:00
- Portable ZIP 73.3 MB (229 files, lean layer, bg3rtx_hairconv.exe; no Python/LSLib/granny2), Setup.exe 54.8 MB,
  Setup.zip, SHA256SUMS under `.scratch/release/v0.9.5-beta/`.
- verify_rc (-NoBuild, 229 files): Build, Archive, Manifest, Hashes, Exclusions, Licences, Version, Install, Update,
  Uninstall, Cleanup PASS. Setup FAIL only because this PC has an existing BG3RTX Apps entry (the user's install):
  verify_setup refuses to modify an existing installation. Not a package defect; the compiled-setup route is unverified here.

## Rebuilt 2026-11-09 (main b3cb21fa): RTX Hair visible in Lighting, launcher Home page
- Same layout, 229 files; new SHA256SUMS. verify_rc not rerun (Setup row still refuses on this PC: existing install).

## RTX Hair single option, OMM dyn refusal churn, scene wait cause (branch claude/opt-waits, READY_FOR_LIVE, 2026-11-08)
- RTX Hair: "Strand hair on screen" (exp_hair_groom_primary) On now writes the user-tested set from the root
  bg3rtx_user.ini: the 5j bundle plus rt_hair_groom, shadow_t, ao_t, cascade_drop, fibre, fibre_gi, fibre_spec,
  fibre_mapped, rr_hair_guides, rt_hair_physical_origin. Tips, coverage, macro_normal, coverage_footprint stay off.
  Off in every preset. Restart keys: rt_hair_groom_primary, rt_hair_physical_origin, rt_hair_tangent_world (the note
  says so). Choice kv 12 -> 18. settings_test test_strand_bundle extended; schema regenerated.
- OMM dyn churn (growth mechanism): the backend's fixed 256 MB (micro-map storage plus kept scratch) filled at 682
  micro-maps; the cache never evicts, so every masked draw without a micro-map was retried every build: 3 dedicated
  VkDeviceMemory buffers, a bake dispatch, 3 "[fault] range of ours: OMM dyn" lines, then the refusal. Refusals grew
  with the foliage seen (6003 -> 10846 per 300 builds, ~36 per build); 111k log lines in 4 minutes; scene wait mean
  2.0 -> 18.2 ms and frame 25.9 -> 52.6 ms with GI/RT GPU ms flat. Nothing leaked (ranges are untracked on free).
- Fix: `rt_omm_dyn_retry_frames` (default 64; 0 = old per-frame retry, common/omm_dyn_refusal.h): a refusal of n
  triangles at backend live bytes L defers every bake of >= n triangles before allocation until live bytes drop or
  64 frames pass. The same micro-maps are built (the budget test is monotonic in size). Fault-range log: first 32
  lines per 600 frames, then one summary line per name ("[fault] ranges of ours not logged: ..."), common/log_budget.h.
  Unit test omm_dyn_refusal_test.
- Scene wait: CPU ordering, not a GPU fence: the lighting thread waits for the G-buffer thread's scene commit; the
  12 ms cap cannot hold because rt_after_capture holds g_mu for the whole recording. Overlap plan (pin the slot at
  capture begin and hand it out at once) in TRACK_D_SCENE_BUILD.md; not implemented.
- Expected: about 14-16 ms less scene wait in foliage-heavy late-session play (back to the 2-4 ms baseline), ~36 x 3
  fewer allocations and ~108 fewer log lines per frame. No shaders touched; no ABI change.
- Follow-up: backend scratch is 1.39 MB of the 256 MB (storage 254.57 MB over 682 micro-maps): freeing it fits about
  3-4 more micro-maps, so it was not done. `rt_omm_dyn_evict_frames` (off): LRU eviction of micro-maps unused for N
  frames and held by no per-draw BLAS, 64 per build at most; unit-tested (omm_dyn_refusal_test). Suggested A/B:
  600 against 0 in the same foliage walk.
- Live check (one session, the same walk through foliage, rt_omm_dyn_retry_frames 64 / 0 / 64 toggled live):
  `[omm dyn]` failed per 300 builds near 0 with 64 (vs thousands with 0), `[rt] scene wait` mean back to 2-4 ms,
  cache size and micro-map share equal across the toggles.

## VRAM: capture reserve from the observed rise, BLAS chunk reuse (branch claude/opt-vram, READY_FOR_LIVE, 2026-11-08)

- Evidence (user log 2026-11-08, 4 min, RTX 5090): cascade capture 3536 MB (3 x 1232 MiB) for a
  370 MiB demand peak; RT capture 2047 MiB for about 840 MiB; 1531 BLAS chunk allocations.
- `rt_capture_rise_reserve` (new, default 0): the elastic movement reserve is the largest demand
  rise over the replacement delay, not the largest single step x 3 (cascade) / x 5 (RT); the RT rise
  is learned without the buffer's own 40% alpha region (its growth fed back as steps: 1024 -> 2047).
  Expected: cascade about -1.7 GB, RT about -0.4 to -1.0 GB. Off until a live shows buffer full 0.
- `rt_as_chunk_reuse_frames=120` (new, default on): empty BLAS chunks stay 120 frames and are reused
  by any arena instead of free + reallocate. Placement only, no rendering change. Churn model
  599 -> 6 chunk creations, same peak. VRAM guard asks still free them at once.
- Tests: rt_capture_elastic_test (ramp unchanged, cut 832 -> 480 MiB, 2000-sample replay covered,
  RT self-feedback gone), as_chunk_alloc_test (cross-arena reuse, window, yield, churn).
- Not changed (candidates): water compose records 560 MiB for about 292 needed at 2560x1067
  (capacity baked into patched shaders: needs a resolution-change plan); GI history ping-pong with
  RR at full res (62 MiB, core GI area); mesh pool 1024 MiB for 673 peak (shrink blocked by native
  hair ranges). The untracked 11.3 GB is mostly the game: core GI images are about 172 MiB.
- Check in game (lives paused): one run, toggle rt_capture_rise_reserve 0 / 1 / 0 at S8, read
  `[mem] ours` cascade / RT capture MB and `buffer full` in every window.

## SER / raygen GI trace on game frames (branch claude/opt-ser-gi, offline, 2026-11-08)
- Prototype: GI_TRACE_RAYGEN in gi/gi_trace_rt.hlsl (the hit-GI body as a raygen; far + sun port, far counters per
  wave), spec 1050 reorder hint (prepass of 2 rays, one OpReorderThreadWithHintEXT in uniform control flow).
  frame_replay --ab-rgen, --rgen-spec, same-pipeline determinism count, --seed-sweep. Fixtures: no gain (one material).
- Packs camp 013545-2, inn 013914-3, S8 142819-1, production far + sun compute without emission (no emissive set in
  the packs), 40 ABBA/BAAB pairs, idle GPU (every run guarded). Baseline camp 8.64 / inn 2.32 / S8 3.12 ms; self
  control [-0.11, +0.08] / [-0.16, +0.22] / [-0.01, +0.20].
- Raygen vs compute, median [95%] ms: no reorder camp -2.58 [-2.83, -2.39], inn -0.61 [-0.67, -0.44], S8 -0.28
  [-0.35, -0.15]. Kind hint: -3.21 / -0.16 / -0.20 (meets the 2-of-3 rule); kind + instance: camp only. The stage is
  the win; SER adds about -0.7 ms on camp only and the prepass costs 0.1-0.4 ms.
- Cost breakdown (compute, paired): far 1007 off -5.75 of 8.66 (camp), -2.5 (inn, S8); hidden-hit shading 1.05 / 0.34
  / 0.94; sun 0.31 / noise / 0.21; roof early 1038 saves 0.82 (inn) and 0.92 (S8); compact hit state: noise.
- Not bit-equal, cause found: not lane-dependent (compute output bit-equal under lane / wave / group remaps,
  GI_DEBUG_SWIZZLE, all packs). The driver compiles the same SPIR-V differently per stage: FMA contraction
  (DXC -Gis cuts camp 380866 -> 83825 px) and division / rsqrt sequences. Foliage amplifies last-bit ray
  differences. No small shader fix. Production unaffected.
- Statistical gate instead: tools/replay_seed_gate.py on frame_replay --seed-sweep (GI_SEED_PUSH offsets the random
  streams through Push::bounce_spec; thresholds from compute vs compute). Raygen without reorder PASSES on camp,
  inn and S8 (energy within 3 SE, per-pixel 4 sigma fraction, counters); far on/off negative control FAILS.
- Corrections: the inn sun-trust used level SCL_Main_A (the inn is The Blushing Mermaid, CTY_Main_A); the inn
  timings above ran with the wrong roof volumes (equivalence results stand, same inputs on both sides). The first
  per-pixel bound missed the control's own noise (sqrt 2); camp alpha passes only with the correct bound.
- Game path design: docs/TRACK_C_GI.md "Ray tracing pipeline GI trace"; implemented on claude/gi-rt-pipeline.
- Receipts: worktree .scratch/ser/ (run.log, ab.csv, gate.json, guards). Codex not used.

## Scene early handout: plan reviewed, not implemented; decision live prepared (branch claude/opt-scene-handout, READY_FOR_LIVE, 2026-11-08)
- The early-handout plan (TRACK_D_SCENE_BUILD.md, scene wait) cannot give "same content by construction" without large
  changes. Set 1 binds this frame's alpha texture views and buffers, known only at the capture end, and Vulkan
  invalidates a command buffer whose bound set is updated later. Fixing that needs UPDATE_AFTER_BIND device features
  and a set 1 layout change in layer and core. The slot TLAS, the instance ring and the pool reallocate on growth.
  A build that fails after the handout has no exact fallback (it needs a per-slot clone). No code, ABI or core change.
- Prepared, not queued: `tools/live_batches/scene_handout/jobs/sh1-camp-scene-wait-fg2.json` (camp, Ultra, FG 2x,
  static; rt_scene_wait 1/0/1, 240-frame cost windows with stats reset, 120-frame [pace] and scene wait lines,
  standard scene-wait and capacity gates; live_flow lint 0 failing, queue_gates_test and live_flow_test pass). The plan
  file must be copied to captures/live_batches/scene_handout/plans/ as with the other batches. Predictions in
  predicted.json: on windows wait mean 2-4 ms, off window 0 waited; the decision value is frame time(on) - frame
  time(off), expected 0-3 ms. Below 0.5 ms: drop the handout. 1 ms or more: build the UAB version.
- Tool: pipeline_readiness.py lists rt_scene_wait as a host-only key (NO_PIPELINE); it selects no shader.

## Launcher: a selected option turns on what it needs (branch claude/opt-knob-deps, done offline, 2026-11-08)
- `settings::raise_needs` (common/settings.h), called by the web bridge's "set": choosing a choice raises every
  knob it needs to the needed choice, recursively, all or nothing. Hidden, custom, experimental-without-opt-in or
  unsupported needs raise nothing (today's behaviour, the card keeps the reason). Status "Also turned on: ...".
  Turning a prerequisite off still lowers dependents at Save (enforce_needs, unchanged).
- RTX Hair: "Strand hair on screen" On from all-off (Ray-tracing detail full) now also sets Strand hair in RT
  lighting and Hair shadows; the saved file holds the whole strand set plus rt_native_hair/scalp.
- web_bridge_test: the new hair case, an unmeetable need (no LSS), lowering on prerequisite off; the FG -> Reflex
  and Play-review cases now expect the raise at selection. No presets, keys or shaders changed.
- User check: launcher with dev=1 and experimental shown, set only "Strand hair on screen" On, Save: Hair shadows
  and Strand hair in ray-traced lighting show On, the restart note appears.

## GPU cost breakdown, hair fibre timing, fibre compile time (branch claude/opt-gpu, offline, 2026-11-08)
- Breakdown of the user's session (bg3rtx.log 09:55, RTX 5090, 2560x1067 RR, Ultra): `gi: GPU cost 16.9 ms` =
  pre region 13.3 (`rt passes`: GI trace 10.6 + origin, sun trace, sun resolve, temporal, AO fold ~2.7) +
  post region ~3.6 (D-LS 1.97 incl. the hair fibre pass, D-RF 1.48 incl. the fibre spec pass, composite ~0.03).
  Split of the 2.7 ms from the last detailed_logging run (hair-ab-r, 23:25): origin 0.94, sun trace 0.68,
  temporal 0.04, AO fold 0.02. Outside gi: RR evaluate 2.33, water compose 0.42 + reflections 0.11 + build 0.14,
  cascade capture 0.36-0.52.
- GI trace: no new key. Every standard exact candidate is already in or measured and rejected
  (docs/OPTIMIZATION_20261105.md rounds 1-7: roof early 1038 and compact hit state 1029 on; lane permutation,
  sun wavefront, far lean, origin fast/lazy, GI-only TLAS, FAST_TRACE rejected; binary rays already
  accept-first; sky early-out exists; SER needs a raygen port). The trace is the hidden-hit far branch
  (attribution: 8.0 ms -> 2.1 without gi_hit_sun/far). Checked here and dropped: sun-first for far hits
  (with rt_cascade_casters the roof status never fires, so almost no skipped work), unlit/metal early-out
  (not exact through the temporal lerp). No replay packs exist after the reinstall, so no offline paired timing.
- Added: `[hair fibre] fibre pass` / `spec pass` GPU median lines (inside the ls/rf lines), hair epoch and
  stats-window resets honoured; CTest hair_fibre_timing_test, gpu_logging_gate_test covers the helpers.
- gi/hair_fibre.hlsl: one call site per heavy function (rt_visibility had three inlined copies: stubbing it
  alone cut the compile 12 -> 1.5 s). Offline compile 12.0 -> 2.9 s, driver pipeline 5.6 -> 1.3 s, ISA
  912 -> 260 KB, registers 168 -> 168; non-hair pixels exit before ls_lite. Not behind a key (a key would keep
  both bodies). Shader requests delta 0; only the two hair_fibre requests change SPIR-V.
- Open: GPU output equality of the restructured fibre pass (no replay contract); in game, rt_hair_fibre=2
  (the in-frame check) and Test Dialogue1 strands must look unchanged. Next live: read the two new lines.

## Runtime capture as the groom converter's source (branch claude/hair-runtime-capture, offline study, 2026-11-08)
- Why: granny2.dll cannot ship, so the release converter cannot read GR2. Design: docs/RTX_HAIR_ROADMAP.md 2c.
- Feasible: UV0 (soup +16), native triangle order and the index range, and the bound CHAR_Hair mask (plain 2D view)
  are all reachable; positions are posed (radius follows the pose's stretch), skeleton, names, LOD rows and
  catalogue categories are lost (index id, mask-image link and the seen order replace them).
- Offline equivalence (tools/hair_groom/capture.py, CTest hair_capture_test): with the index range, a rigid pose
  gives bit-identical strands, triangles and barycentrics, the same index id and UV samples; a 2% bend moves radii by
  1.5-6.6%; dedupe flips 3 of 125,678 strands on the largest style. Welding without indices breaks cards and the id.
- Stage times (12 cached styles, one thread, synthetic mask): tracing 4.6 s median, dedupe 0.7, validation 1.8,
  LODs + write 1.6; total 8.6 s median, 15.3 p90, 45 max. Minimal path without validation ~6.9 s typical.
- Recommendation: capture in game, convert in the launcher's existing child (C++ port needed for the release).
- Layer probe: `[hair capture]` mask format / size / mips / layout once per mask image (rt_native_hair only).
- READY_FOR_LIVE: tools/live_batches/hair_capture (hcap1-dialogue, Test Dialogue1), lint 0 failing, not queued.

## Strand fibre shading for shadow-mapped local lights (branch claude/hair-m4-locals, READY_FOR_LIVE, 2026-11-08)
- M4.3 `rt_hair_fibre_mapped` (spec 1030, knob exp_hair_fibre_mapped, off; needs rt_hair_fibre=1): gi/hair_fibre.hlsl
  now also replaces the game's hair term of lights with a game shadow map (E S with the cards' atlas term, transcribed
  from 3033c895) by the fibre response with the traced visibility and the strand count, where D-LS trusts the trace.
- Gates: removed game term GPU vs CPU twin max 5.4e-6 relative; N-light fixture (20,000 lights) white single
  scattering 0.99973 / 0.99997 (analytic 1); dual sums GPU = CPU within 3.4e-7.
- Identity vs main 7f86f343: 218/221 byte-identical, 3 hair_fibre requests equivalent at 1030 = 0; request delta 0.
- CTests: 207/208 on a fresh VS 2022 build tree; the stale schema was regenerated and settings_test passes.
- Cost unmeasured: one atlas PCF + scene ray + groom query per strand pixel and mapped light (as an unmapped light).
- Check: a torch-lit scene (Test Dialogue1, Campsite), hair under shadow-mapped lights, key on vs off in one run.

## Strand hair layer path: review fixes (branch claude/hair-layer-fix, READY_FOR_LIVE, 2026-11-08)
- Review of the layer path verified item by item (docs/RTX_HAIR_PRIMARY.md 5j); all confirmed except the core level-load
  counter (none exists; the image creation id covers handle reuse).
- Header version (1): the submit always applies the newest publish ordered before the G-buffer, rows remapped to the
  recorded slots; draws whose groom it lacks draw their card in the strand variant (card depth kept). No 2x memory.
- Mask holes (2): header slot mask of the swapping slots; the pixel trace skips other rows (3 skips), the probe traces
  privately. Count (3/4): colour, traced/hit and per-groom pixels for all four tag windows: C2 a_f measured again,
  dropouts always attributed.
- Coherence (6/18): "Strand hair on screen" On = primary + order + current + frame_tag + lod_keep + hold +
  tangent_world. The four separate knobs are gone (keys stay as ini diagnostics); passes/shadow_t/ao_t need the strand
  knob. Presets unchanged (strands off). settings_test test_strand_bundle.
- Also: lod_keep evicted-LOD restage and first-upload slot churn (5); cascade_drop gated on the core's RT sun persist
  flag (12, core/gi.cpp 5 lines); live-off latch fallback (13); LoadGuard image id (14); pool release when off (15);
  zero-length segments (17); watchdog and no-fallback orphan block (19); `header safety` period line (20).
- Gates: build -Test, shader identity (only the hair G-buffer and count requests change; variants.txt delta 0).
- Cost: unmeasured (no game). The probe's private trace is per fragment while a draw shows cards.
- User check: Test Dialogue1, strands on, characters walking in and out: no holes where hair overlaps, `header safety`
  mismatch 0, watchdog 0, shadow transmittance measured with shadow_t on.

## Strand groom storage, needed set and launcher Preparation card (branch claude/hair-groom-storage, offline study, 2026-11-08)
- Design: docs/RTX_HAIR_ROADMAP.md section 2b. Prototypes: tools/hair_groom/storage.py (scan, HGRZ codec, error
  study), tools/hair_groom/prepare.py (needed-first order, launcher status JSON), CTest hair_storage_test.
- Size: 871 files, 20.97 GB: barycentrics 39%, triangle 19%, radius 19%, segment list 17%, root 5%. Thinned LODs
  (keep 1/2, 1/4) are 43% and bit-exact `convert.thin(LOD 0)`; the segment list is derivable from strand lengths.
  7.1 GB (312 files) are not written by the last batch (older, wider runs). The batch cache adds 16.5 GB.
- HGRZ (LOD 0 only, strand lengths, deltas, zstd 19), 24-file sample: lossless 6.1x; per-file barycentric bits
  (16-20, chosen from the card mesh) 11.1x with max position error 0.35 um under the derived 0.38 um (one 8-bit
  level at a 4K 0.6 m close-up); 16-bit 11.5x but 1.2 um p99. zstd on the raw file: 2.2x.

## Grooms v3, seen list, launcher Preparation (branch claude/hair-groom-storage, implemented offline, 2026-11-08)
- User decisions applied: per-file bits (format v3), needed first with Convert all off, the player picks when to
  convert (default: launcher open and game closed), compile shaders before Play, batch cache deleted per style.
- Format v3: common/hair_groom_v3.h (layer + launcher read v1-v3), writer asset.write (v3 only). Codec LZMS through the
  Windows Compression API: no zstd source on this PC and vendoring needs a download the user must approve.
- 5-style dry run from cached grooms (paks on D:\ not read): 176.7 MB v2 -> 16.9 MB v3 (10.4x), max position error
  0.358 um. C++ decode equals the Python reference (CTest hair_groom_v3_test + --dir); all 3 LODs 98-110 ms per
  0.35-0.40 M-point style, 757 ms for the largest (3.3 M points), LZMS 80%; the layer decodes one LOD per upload.
- Layer: hair_seen.txt next to the assetdb (`note_seen` on the existing miss paths, `flush_seen` at a level load and
  device destroy). No shader or request change (shaders/precompile.txt is a list, not HLSL); export
  `bg3rtx_precompile` in layer.def.
- Batch: --seen / --all / --keep-cache / --no-reach, tiers from reach.json, index ids per row, index_map.json,
  preparation.json every 30 s, cache deletion.
- Launcher: tools/launcher/preparation.{h,cpp}; first card on Help & support with live lines, Pause/Resume, Convert all,
  When to convert; children in kill-on-close job objects at BelowNormal; settings in bg3rtx_launcher.ini.
  `BG3RTX.exe --precompile-shaders` compiled 2 test requests in 1.2 s, then 0 in 0.07 s (cached).
- Tests: hair_groom_v3_test, launcher_preparation_test, web bridge prep checks, renderer prep checks,
  hair_storage_test (v3 fixture, seen list, categories), hair_groom_test (v3 asset, cache deletion).
- Left before a release: the release has no Python, converter, LSLib or catalogue (the card says "Not available");
  the full conversion on the user's PC; a live check of the upload decode cost and of hair_seen.txt.

## The flash while anything moves: per-frame tags, LOD keep, counter fix (branch claude/hair-flash, offline, off, 2026-11-08)
- The 5h dropout counter judged the wrong G-buffer frame (the count reads the previous frame's markers): ~1000-1500
  false "no card marker" per period. Fixed: the frame the coverage evidence uses.
- Cause 1: two consecutive G-buffer frames sealed with one publish tag (current upgrade, then a different-grooms
  fallback): the second reuses the first's strand hits without tracing and the evidence turns ambiguous (coverage
  invalidated, cards for a while). Moving periods: ambiguous 24-28, invalidated 86-420, swap resumes 15-52; still: ~0.
  Key `rt_hair_primary_frame_tag` (live, off): the G-buffer frame is the tag.
- Cause 2: a LOD switch to a LOD still uploading drops the groom for 2-3 frames (cards); new slots start at LOD 0 then
  switch. Key `rt_hair_groom_lod_keep` (live, off): keep the built LOD until the new one is resident.
- Review fixes (always on with strands): the submit upgrade is taken whenever the recorded draws keep their slots
  (no stale header over BLASes refit in place); headers without a build camera are never applied (camera_gate key
  retired); tag and previous camera from the build's own frame; grooms without a primary key masked out of primary rays
  (shader mask 0xFE); counter state reset at level loads. Rejected: compaction (7), several G-buffer CBs (10).
- Live check: RTX_HAIR_PRIMARY 5i.

## Strand hair layer review round 3 (branch claude/hair-fix3-layer, READY_FOR_LIVE, 2026-11-08)
- H2 (the flash on every LOD change): a slot keeps its published key through the LOD switch frame; the draw slot, the
  swap and the coverage evidence survive it. Likely the user's dialogue/camera flash.
- M1: previous points only when last updated in the previous build frame. M2: card-mode pixels get the card's motion
  vector in the swapped velocity pass. M4: non-hair draws leave the hook early. M5: clones built outside the lock, the
  unused generic G-buffer variant dropped. M6: one LOD read and validated per upload.
- L1 header tag zeroed at a pixel-record reallocation; L3 per-key maps pruned after 600 frames; L4 submit_decision
  unit-tested; L5 cfg reads once per publish. L2 left (reason in docs/RTX_HAIR_PRIMARY.md 5k).
- Gates: build -Test; shader identity: 217/221 identical, only the hair G-buffer and velocity requests differ; variants.txt delta 0.
- Cost: unmeasured (no game). Card-mode velocity adds one marker read per swapped velocity fragment.
- User check: Test Dialogue1, zoom and pan so heads cross LOD thresholds: no strand-card flash; `[hair groom]` LOD
  switches counted, `header safety` card-draw upgrades not rising with them.

## Strand fibre passes now fire: strand-pixel depth test, sun mask (branch claude/hair-fix3-fibre, offline, READY_FOR_LIVE, 2026-11-08)
- H1 confirmed: hf_strand (gi/hair_strand_pixel.hlsli) and hv_strand_pixel (rt/hair_fibre_gi.hlsli) compared the entry's
  device depth e.z with gDepth's linear depth at 1e-6 relative: no pixel ever matched, so rt_hair_fibre, _spec and _gi
  returned on every pixel and the rt_hair_fibre=2 check of claude/hair-fibre-fix passed vacuously.
- Fix: shaders/rt/hair_depth_match.hlsli compares as device depths, d -> 0.1 / (d zr) (reversed infinite Z, near 0.1;
  zr the frustum rays' view depth), tolerance one D24 code plus 6 fp32 half ulps relative (derivation in the file).
- GPU test (hair_primary_gpu_test depth_match_check): 17810 real strand pixels of the GPU strand pass, game depth chain
  (z = 0.1 / w, D24 code, R32F linear, zr 1 and 1000): match 100%; twice the tolerance away 0%; the old rule 0%.
- M3 confirmed: the sun's fibre response ignored the game's mask m (sun under roofs once H1 works). Now E m F where our
  scene ray is lit (min(m, v)); n from the groom query only sets T_f.
- Identity vs main: 213/221; different only gi/hair_fibre (3), gi/hair_fibre_spec (3) and gi_composite's
  RT_HAIR_FIBRE_GI_REFERENCE variants (2), all behind fibre keys. Request delta 0.
- Cost: the passes now run on every strand pixel (one scene ray and one groom query per D-LS-traced light and the sun;
  one reflection ray for _spec). Not measured: needs the live cost window (the hair fibre GPU ms inside D-LS's region).
- Live check (when allowed): rt_hair_fibre 0/2 in one run, strand-pixel mean equal (now a real comparison); then 1 vs 0
  and the GPU ms.

## Strand fibre shading: review fixes 7-11 (branch claude/hair-fibre-fix, offline, READY_FOR_LIVE, 2026-11-08)
- Question for the user: the dual model cannot match the path-traced hair at both normal and diffuse incidence
  with one k. Taken: the diffuse fit (energy of a lit hair volume conserved). Hair under rt_hair_fibre reads
  ~0.76-0.91 of the normal-incidence reference (was 1.02) instead of 1.3-2.2x too much energy at diffuse
  incidence. Alternative: a k(mu_i) table from the strand macro normal (more work, needs rt_hair_macro_normal).
- 7 confirmed: k multiplied T_f f_s (a fibre behind one strand: up to 2x the f_s of a directly lit one). k now
  scales only the backward multiple scattering (`dual.single_part`, `hd_parts`). k refitted on the path-traced
  diffuse slab (`dual.make_table`): every grid entry equals the walks within 0.03%, white (c 0.9) 0.894-0.903 < 1,
  CHAR_Hair held out 1.001/1.002/0.998; k monotonic in roughness. The old zig-zag in colour (2.43, 3.55, 2.51) was
  the normal-incidence walk's heavy-tailed weights (12000-path walks at c 0.7, beta 0.5: 0.083..0.134).
- 8 confirmed: hair_fibre subtracted a re-derived D-LS term. It now replays D-LS's ray (`hf_ray`: trace_light's
  origin, soft sample, extent) and strand decision (`hf_groom`: rt_groom_blocks' draw, the groom's a_f) and counts
  n on that ray. rt_hair_fibre=2 (constant 1046) is the in-frame check: the game's terms at the fibre's
  transmittance model on the D-LS-traced lights; strand pixels must equal key off (exact without
  rt_hair_groom_shadow_t, mean equal with it). Limit: the physical hair origin (1009) is not in hair_fibre's request.
- 9 confirmed: all fibre light went to the diffuse buffer. Single scattering (all lobes) now to specular, where the
  game's R/TT/TRT are; multiple scattering to diffuse.
- 10 confirmed (and wider): no replaced term took the AO-on-direct factor. `hf_ao`: the game's factor, D-LS's
  ao_direct give-back for traced lights and the sun; the fibre response takes the factor of the term it replaces.
- 11 rejected: Lrt is last frame's scene radiance (D-RF uses it the same way); k = lScale 100 converts probe units
  and SO is the probes' occlusion, so neither applies to a traced hit.
- Gates: CPU test_dual/test_volume pass (new: furnace, offset-independence of the k part, single n=1 <= n=0 on
  every entry); GPU dual check adds the split and n=1 <= n=0 on every entry. Shader identity vs main: 216/221;
  different only gi/hair_fibre (3, fibre key) and rt/hair_primary_count (k is in the same constant array it reads
  a_f from; the a_f values are unchanged). A separate k array read shifted entries on the GPU (DXC SPIR-V; the GPU
  check caught it), so the table stays one array. Request delta 0.
- Live check (when allowed): one scene with hair, rt_hair_fibre 0/2 toggled in one run: strand-pixel mean equal; then
  1 vs 0 for the look; GPU ms of the hair fibre pass (inside D-LS's timed region).

## One-frame strand dropouts: counter and camera gate (branch claude/hair-dropout, offline, off, 2026-11-08)
- Also lands tools/hair_groom/temporal_burst.py (pan burst temporal metrics) from claude/hair-temporal.
- Cause found by reading (not yet proven live): the scene build's camera is stored under frame_index F, the groom
  publish inside the same build reads frame_index after a present on another thread advanced it, so the header has no
  build camera; every strand fragment discards (hp_rebase) while the swap gates pass: a frame without hair.
- Counter `[hair primary] dropouts (last 300 frames)`: swapped G-buffer draws whose strand pass wrote under a tenth of
  their card pixels, by first explaining check, first 4 frames, plus publishes without the build camera. CPU only,
  from the existing marker counts.
- Key `rt_hair_primary_camera_gate` (knob exp_hair_primary_camera_gate, Advanced, live, off): the build's own camera,
  and headers without one are never applied (cards that frame). No shader change, no ABI change.
- Live check: RTX_HAIR_PRIMARY 5h.

## Native strand groom converter bg3rtx_hairconv (branch claude/hair-converter-native, offline, 2026-11-08)
- C++ port of the Python converter (tracing, 2-D tracer, dedupe, analysis, validation renderer, verdicts, v3 writer):
  tools/hairconv, `bin\bg3rtx_hairconv.exe` (static CRT). Input .hcap (tools/hair_groom/hcap.py), output .hgrm v3.
- Equivalence: 24 cached styles (synthetic masks, 2 curly) identical to the Python reference: verdicts, IoU /
  coverage / radiance, strand counts, and decoded grooms bit-identical. CTest hairconv_test on a synthetic fixture.
- Speed: see docs/RTX_HAIR_ROADMAP.md 2d (C++ 4 threads: 0.51 s median, 1.78 s max per style).
- Launcher Preparation runs bg3rtx_hairconv over data/assetdb/hair_capture (no Python); Convert all removed (only
  captured styles exist). Release ships the exe; verify_rc requires and allows it.
- 2c updated with the hcap1-dialogue live result: BC3 masks, 128x128 placeholders (capture waits for full mips),
  every hair draw indexed, unmatched draws are all "count unknown", card stretch stat added to the capture spec.
- Left: the layer capture (.hcap writer), groom re-index at a level load, one live capture -> strands run.

## Layer hair capture writer and groom re-index (branch claude/hair-capture-writer, offline, 2026-11-08)
- Key rt_hair_capture (startup; On with "Strand hair on screen"). layer/hair_capture.cpp writes .hcap v2
  (common/hair_capture.h) for each hair style drawn without a groom: index range, soup, mask mip 0 as raw BC3; full-mask
  rule (no 128x128 placeholders, re-capture on upgrade), card stretch stat, one capture per frame at most, worker
  thread, 512 MB folder cap, per-capture log and a 300-frame summary. The tracker adds TRANSFER_SRC to BC3 images.
- Cost once per style: GPU copy ~4.3 MB (typical) to ~11 MB; render thread 0.1-1 ms; worker 10-30 ms; none per frame.
- Re-index at a level load: new groom files only (hair_groom::rescan).
- bg3rtx_hairconv reads .hcap v2 (BC3 decode). CTest hair_capture_cpu_test (rule, stretch, round trip, BC3).
- READY_FOR_LIVE: tools/live_batches/hair_capture hcap2-capture then hcap2-strands (two launches: startup key, no
  in-run reload); lint 0 failing; not queued.
- "Strand hair on screen" (and "in ray-traced lighting") need hardware only now: local groom files no longer required.

## Live e2e 2026-11-08 22:40 (hcap3, Test Dialogue1, scratch assetdb without groom 8b32681bb9ecfc30): both VALID
- Capture: 8b32681bb9ecfc30 captured once its full mask arrived (121368 indices, 36112 vertices, 2048x2048 BC3, 12 mips,
  5.2 MB); 2-3 other styles stayed "waiting for a full mask" (placeholders). nv153 0 in both launches.
- bg3rtx_hairconv on the real capture: pass, IoU 0.966 / 0.939 / 0.926, coverage -0.7 / -1.6 / -1.2 points; 1.8 s
  (trace 0.08, dedupe 0.68, validation 0.21, write 0.84).
- Against the removed original (Python, GR2 bind pose): same index id, strands 153473 vs 154531 (-0.7%), points -0.2%,
  median radius ratio 0.998, 62% identical strand starts (the rest re-traced from the posed capture; quality gates pass).
- Second launch: the v3 groom decoded at level load (LOD 0 140 ms, LOD 1 141 ms) and drawn as strands (900 G-buffer
  strand draws per 300 frames). Scratch copy removed; the real assetdb still has 871 grooms.

## Ray tracing pipeline GI trace (branches claude/gi-rt-pipeline, claude/gi-rt-pipeline-ultra; on by default, 2026-11-08)
- What: the hit-GI trace (gi_trace_rt_hit_sun[_em].hlsl without SHaRC) runs as a ray generation shader over the
  same grid and descriptors (docs/TRACK_C_GI.md "Ray tracing pipeline GI trace"). ABI 29 (Bg3HostApi::rt_pipeline,
  3 VkDeviceFuncs entry points). Layer: VK_KHR_ray_tracing_pipeline + VK_EXT_ray_tracing_invocation_reorder when
  both are supported, dropped first on a device creation failure. Compute fallback for any missing piece, logged once.
- Shaders: +4 lib_6_6 requests (hit_sun, hit_sun_em, each with and without OMM); every other request byte-identical.
  Reorder is constant 1050 (gi_rt_pipeline_reorder; no prepass at 0).
- Offline seed gate on the shipped request (raygen vs compute, 32 draws per set, thresholds from compute vs compute):
  PASS on camp, inn and S8 (energy, per-pixel 4 sigma, counters).
- Live inn (The Blushing Mermaid, Ultra, FG): trace pass 12.09 / 11.88 ms OFF vs 10.03 / 9.86 ON, about -2.0 ms;
  the emission variant ran as a ray tracing pipeline, FG status 0x0, no device loss. Energy -0.32 / -0.35%
  (OFF-OFF spread 0.27%, one pair).
- Live camp (Test Campsite): clean window cost-on-b trace -2.12 ms (OFF spread 1.48); energy +0.13 / -0.04% (spread
  0.13%). Across both scenes the energy signs disagree, so no energy shift is shown.
- First raygen build about 60 s on a cold driver cache (compute meanwhile); instant once the driver caches it.
- Open caveat: one nvlddmkm event 153 at 17:18:05 in the camp's first ON window (cost-on-a). The GPU recovered and the
  game continued, with no fault line and no device loss. It is the only one in 42 queue runs over 30 s. Not
  explained. Check offline: frame_replay --ab-rgen with validation and GPU-assisted validation on the packs, and
  layer/devfault.cpp (VK_EXT_device_fault) output if it recurs.
- User decisions 2026-11-08: ship it ("in game mi sembra tutto ok, quindi se gira meglio teniamolo"), then on in
  every preset ("perchè non attivarlo sempre?").
- Ultra branch: exp_gi_rt_pipeline qualified Performance control, preset {1,1,1,1}, default 1; no-op without the
  hit-GI trace. Release controls 17 -> 18; no shader change.
- SER live (gi_ser_reorder, 18:09 and 18:19 reruns, nv153 0, no device loss): camp trace -1.61 (first run on-a), then
  -1.51 / -1.60 ms vs mean OFF (OFF spread 1.40, every ON below both OFFs); energy -0.21 / +0.02% (band 0.36%).
  Inn: one clean window, -0.79 ms vs OFF-a; both inn runs INVALID on scene-wait timeouts in OFF windows (the inn's
  scene thread is at its limit, not SER). An nv153 also appeared at 18:09:22 with no BG3 running.
- claude/gi-ser-default: gi_rt_pipeline_reorder=1 (by hit kind) in every preset and as the code default
  (bg3rtx.ini, kDefaults, core fallback); exp_gi_rt_pipeline_reorder is a normal Performance control ("object" stays
  selectable), and its need on exp_gi_rt_pipeline stays (raise_needs checked). The committed two-scene rule is not met
  (inn unproven): this ships on the user's decision 2026-11-08 ("Yes, turn it on"). A driver without reordering
  gets constant 1050 = 0 (no hint, no prepass, logged); without the reorder extension the compute trace stays.
  Release controls 18 -> 19. No shader change (225 requests byte-identical).
## Hair smoke 2026-11-08 19:10 (Test Dialogue1, Strand hair on screen bundle, after the publish fix 782a6ad4): VALID
- Strands on screen: 900 G-buffer / 900 velocity / 901 prepass strand draws per 300 frames (refusals: coverage low ~570,
  card depth 20-29 of 1500); strand G-buffer draws 0.21 ms. The 18:53 run had 0 (round 3 publish bug).
- Compute vs RT pipeline + SER: trace pass OFF 8.33 / 9.44 ms, ON 6.30 / 6.49 ms: -2.59 / -2.40 vs mean OFF (spread 1.11);
  every ON below both OFFs by about 1.9 ms. Whole GI -2.7 / -2.4 ms.
- Energy -1.02 / +0.06% (band 3.35%, OFF-OFF spread 0.86%: the dialogue animates): within. nv153 0, no device loss.

## Release 0.9.4-beta: version bump and changelog (branch rel094, offline, 2026-11-07)

- Version 0.9.4.0 in the launcher, the UI preview, the bridge JSON test and the Nexus sticky post;
  new `docs/release/nexus/changelog_0.9.4.txt` (0.9.3 notes kept: 0.9.3 may already be public).
- Content: launcher picks the RTX GPU next to an iGPU (main 96ef8477); diagnostics reports complete on a
  disk mounted as a folder (main 69a32a06). No layer, core or shader change since 0.9.3.
- Next: merge-green, verify_rc, then Setup and Portable packages under `.scratch/release/v0.9.4-beta/`.

## Release candidate 0.9.4-beta PASS (branch rc094, 2026-11-07)

- verify_rc PASS 12/12 at efc9f6d6 (rc-20261107-efc9f6d6-e966dcdf): build, archive 72.60 MiB, 223 files,
  hashes, exclusions, licences, install/update/uninstall, compiled Inno setup 6.7.3, cleanup.
- Packages in `.scratch/release/v0.9.4-beta/`: Setup.exe 54.4 MB, Setup.zip 54.0 MB, Portable.zip 72.6 MB,
  SHA256SUMS.txt; no DLSS 5 runtime in the payload. Nothing uploaded.
- Machine note: the setup row needs Windows long paths on (`LongPathsEnabled=1`): its throwaway staging path
  reaches 260 characters under TEMP. A real install path is about 199 characters.

## Packaging allows a checkout on a disk mounted as a folder (branch package-mount-points, verified offline, 2026-11-07)

- After the PC reinstall the checkout sits on a 4 TB disk with no drive letter, mounted at `E:\Storage`.
  package.ps1, build_setup.ps1 and verify_rc.ps1 refused every reparse point on the output path, so
  `package_runtime_test` failed ("linked package output: E:\Storage") and no setup could be built.
- Fix: one shared helper, `scripts/linked-path.ps1` (`Find-LinkedAncestor`). Junctions and symlinks are still
  refused; a whole-volume mount point (target `Volume{guid}\`) is a real disk and is allowed.
- Tests: new `linked_path_test` (volume targets, a real junction refused and unlinked safely, the checkout
  allowed); `package_runtime`, `package_licences` (copies the helper into its fixture), `release_setup`,
  `release_install` and `test_registration` pass.
- No shipped file changes (install/uninstall scripts untouched). Cost: none.
- Same cause in the shipped launcher: diagnostics `safe_path` refused the mount point, so a report from a
  game or fixture on such a disk came out incomplete (`launcher_diagnostics_fixtures` failed on this PC).
  It now accepts a reparse point only when `GetVolumeNameForVolumeMountPointW` resolves it to a volume;
  the existing "ancestor junction refused" check still passes. Launcher, package and link tests: 10/10.

## Launcher picks the RTX GPU when an integrated GPU is also present (branch launcher-default-gpu, verified offline, 2026-11-07)

- Bug: with two Vulkan adapters (AMD Radeon iGPU plus RTX 5090, seen on the user's PC after a Windows
  reinstall added the AMD driver) the launcher showed "GPU not chosen" and every option stayed disabled
  until the user picked a GPU under Help & support. The choice was not saved, so it came back on every start.
- Fix: `launcher::default_adapter` (tools/launcher/compatibility.cpp) picks the single best adapter, scored
  like `best_gpu`: discrete, then ray tracing, then NVIDIA. A tie at the top (two equal RTX cards) still asks
  the user. Used by the web bridge and the old product UI. The user can still change it in the GPU menu.
- Tests: `launcher_web_bridge_test` gains the iGPU + RTX, twin RTX, single and empty cases (pass).
  `launcher_diagnostics_fixtures` fails on main too ("stale report is partial"), not caused by this branch.
- No ABI, shader or settings change. Cost: none (one loop over the adapters at startup).
- User check: start BG3RTX.exe on a PC with an iGPU: the Home page shows the RTX name and options are enabled.

## Temporal grain from the hair-ab7 bursts (branch claude/hair-temporal, offline, 2026-11-07)
- New tool tools/hair_groom/temporal_burst.py: per pan burst, inside the hair mask, temporal flicker, motion-compensated
  frame difference and spatial HF of the pre-RR colour and of the RR output (B10G11R11 / A2B10G10R10 raw decode).
- Tips off (live): main draw 0.921-0.933 -> 0.958-0.972; 210b and 35ce reach 0.93-0.98 and swap; f5c0 and 6e26 do not;
  hair share 0.847/0.849 -> 0.876/0.881. Prediction met for the main draw and hair share, partly for small draws.
- Clean frames (0-7): strands flicker less than cards before RR (0.455 vs 0.571) and after RR (motion-compensated
  difference 0.0146 vs 0.0199); RR output HF 0.029 vs 0.037 (strands softer, like skin at 0.029).
- Found: a one-frame strand dropout in the strands burst (frame 8: 4,689 hair pixels vs ~41,500, no camera cut);
  the RR output loses the hair for that frame and the hair difference stays 3.3x higher for 7 frames.
  The 220524 warm-up window showed the same bald frame. Cause not yet located (no dropout counter in the log).

## Strand swaps never started: prepass/G-buffer card-depth deadlock (branch claude/hair-swap-fix, READY_FOR_LIVE, 2026-11-07)

- Symptom (user run 2026-11-07 19:08-19:12, Test Dialogue1, primary_order 1): grooms join (matched 1297), but every
  period line has 0 prepass, 0 G-buffer, 0 velocity strand draws. A close-up draw (483b, LOD0, 75k card pixels) held
  coverage 0.918 inside [0.891, 1.095] for 600 frames, probed in every window, and still 0 swapped frames.
- Cause: a circular gate. The G-buffer swaps only after this key's prepass dropped its card depth
  (`!g_depth.dropped(key, now)`, layer/hair_primary.cpp, added by 5f3b8a88 on 2026-11-04 19:01, after the hv9
  18:02 run that drew strands). The prepass drops only after a G-buffer strand draw (`SwapGate::allow_prepass` needs
  `gbuffer_prev`, e43731cc). With every other gate passing, neither can go first.
- Second limit, not changed: most draws in this scene are small (100-1800 card pixels) with ratios 0.60-0.89, below
  kCoverMin. Its noise term is derived at the offline footprint (about 9400 samples); at 800 pixels the same formula
  gives sigma 0.06, not 0.019. That is a decision for the hair owner and is not part of this fix.
- Fix: SwapGate `ready_now/ready_prev`. A G-buffer draw that passes every gate except the card-depth drop (and has its
  strand variant) marks the key ready; the next prepass may then drop. If that G-buffer still refuses, its cards take
  the existing native-depth fallback. Velocity still needs a real strand draw. No shader, key or ABI change.
- Diagnostic: the `[hair primary] frame N: ...` period line ends with `swap refusals (main-view hair draws kept as
  cards, first refusing gate): G-buffer: coverage low n, card depth not dropped n, ...; prepass: ...`.
- Tests: hair_groom_asset_test `test_prepass_gbuffer_bootstrap` (0 swaps without readiness, first swap within 3
  frames and 297 of 300 with it), `test_refusal_tally` (coverage reasons, tally format).
- Next live (same save and keys): prepass and G-buffer strand draws > 0 for draws inside the coverage bounds (the
  close-up 483b), `swapped frames` > 0 in the coverage line, `prepass card depth dropped without its G-buffer swap`
  small, depth orphans none; the refusal tally names the rest (expected mostly `coverage low` for small draws).

## Strand flicker and lag: coverage hold, this frame's publish (branch claude/hair-stability, READY_FOR_LIVE, 2026-11-07)

- Flicker (2026-11-07 hair-flicker): draws near the coverage bound (9861, about 430 card pixels, 0.833..0.925 per
  window) swapped 0..300 frames per window; refusals `coverage low` 521-896 and `coverage stale` 19-300 per window.
  The strict bound's noise term is the offline footprint's (0.019); at 430 pixels the same formula gives 0.088.
  Empty measurements (0 card pixels) replaced a good ratio and read as stale.
- Key `rt_hair_primary_hold` (off): a swapping key keeps strands while its newest non-empty measurement is inside
  [0.9105 - sigma, 1.0753 + sigma] with sigma = (sqrt(S) + r sqrt(C)) / C of that measurement, and at most
  kHoldFrames = 12 + 4 x 4 = 28 frames old (freshness plus one full count readback ring). An outside measurement,
  an invalidation or an older one returns it to cards; a new swap keeps the strict bound. Strike, orphan and device
  fail-safes unchanged. Test: hair_groom_asset_test test_coverage_hold (strict flickers, hold 60/60 on the same
  measurements; 0.60 drops it; invalidation and age drop it).
- Lag: the strand G-buffer is recorded before the frame's scene build, so it applied the previous frame's publish
  (rebased for the camera only): strands trailed the animated cards by one frame and their velocity carried the
  previous frame's motion. Key `rt_hair_groom_primary_current` (off, needs rt_hair_groom_primary_order=1): the
  G-buffer copies its header from a host slot written at recording with that safe publish and upgraded at
  vkQueueSubmit to this frame's when its build is submitted earlier or earlier in the same submit, with the same
  grooms in the same draw slots. Count tag, GbFrame tag and the repeated-tag check follow the final choice.
  Tests: test_current_pick (submit orders, groom/slot changes), hair_groom_test primary twin (previous publish:
  > 1 px behind an accelerating card and a wrong MV; this frame's: card position and MV exactly).
- Temporal reconstruction (item 3, no change): the strand rays use the inverse jittered view-projection at the
  fragment's raster position (hair_primary_gbuffer.hlsl:129-135), the cards' own sample. Tips use the native TAA
  alpha-test dither (twin tips_keep). Scratch temporal model (Halton 18 phases, 72 frames, per-pixel area
  reference): single ray residual 0.056, EMA flicker 0.043; M3.2 pair with hashed pick 0.091 / 0.052; a parity pick
  0.115 / 0.041. The CTest gate judges the pair on a 3 x 3 box instead, where it wins. Live A/B of
  rt_hair_groom_coverage with the two keys above on decides; no new pick adopted.
- Period line `[hair primary] frame N:` adds `coverage hold on/off, swap frames only the hold allowed n, strand swaps
  resumed after card frames n; G-buffer headers at submit: this frame's publish n, record-time because not
  recorded n, build after the G-buffer n, different grooms n`.
- Next live (Test Dialogue1 close-up, rt_hair_groom_primary_order=1): toggle rt_hair_primary_hold 0/1 and
  rt_hair_groom_primary_current 0/1 in one run. Expect with hold: `resumed after card frames` near 0, swapped frames
  per draw 300 for in-bound draws, `coverage stale` refusals gone for empty measurements. With current: `this
  frame's publish` close to 300 per window (the adjacent-tag counted frames stay: the count still reads the previous
  G-buffer's pixels); strands no longer trail
  the head. Cost: one 3.5 KB host write per G-buffer submit; no GPU pass added.

## Strand quality: footprint coverage bound, pair gate bias, item 4 design (branch claude/hair-quality, READY_FOR_LIVE, 2026-11-07)

- Small draws (2026-11-07 A/B: three per frame refused `coverage low`, 600-900 per window): the strict bound's noise
  term is the offline footprint's (9397 samples, sigma 0.019). Key `rt_hair_coverage_footprint` (off): every
  measurement, also a first swap, is judged by [0.9105 - sigma, 1.0753 + sigma] with sigma = (sqrt(S) + r sqrt(C)) / C
  of that measurement; at the offline footprint this is the strict bound. Live draws: 0.842 of 443 passes (own bound
  0.827), 0.792 of 610 stays on cards (0.842), 0.31 of 51 and 0.674 of 46 stay on cards. Test:
  hair_groom_asset_test test_coverage_footprint (never stricter than the strict bound, freshness unchanged).
- rt_hair_groom_coverage=1 lowered the pass rate (600 vs 900-945 swaps per window): the gate counts card pixels and
  accepted strand pixels where the card covers the jittered pixel centre; the pair's rays sit a quarter pixel off
  it, so near a card edge one falls off the card. Model (hair_groom_test test_coverage_pair_gate_bias, scratch run):
  pair/single ratio 0.884 on 2 px cards, 0.943 on 4 px, 0.970 on 8 px, 0.992 on 32 px. The pair is still unbiased
  against the pixel's area coverage (existing CTest), so this is a gate bias on narrow cards, plus the temporal model
  of claude/hair-stability (pair residual 0.091 vs single 0.056). Recommendation: keep rt_hair_groom_coverage off.
- Item 4 (minimum on-screen strand footprint), not implemented: the strand radius is the LSS BLAS's static per-point
  radius array (bvhsplit.cpp groom upload, radiusData), shared by every pass that traces the groom TLAS (strand
  shadow/AO transmittance, fibre GI). A primary-only clamp r' = max(r, 0.5 px x distance) needs a second
  per-frame radius array written by hair_groom_update.hlsl from the publish camera, a second LSS BLAS per groom
  (refit with the points) and a primary-only groom TLAS, plus coverage conservation in the G-buffer variant (keep a
  widened hit with probability r / r' through the native TAA dither, as the cards' alpha). Cost: one more groom
  BLAS refit per groom per frame and one TLAS. Needs an owner decision and its own branch.
- Item 6: the coverage line now prints each draw's own footprint bound (`own=`) and `draws outside bounds n
  (outside their own footprint bounds n)`; the period line shows `footprint bound on/off` next to the hold, the
  refusal tally, header choice and resumes. Jitter needs no counter: the strand rays use the inverse jittered
  view-projection at the fragment's raster position (structural match, hair_primary_gbuffer.hlsl:129-135).
- Next live (Test Dialogue1, hold and current on, coverage and tips off): toggle rt_hair_coverage_footprint 0/1.
  Expect `coverage low` refusals to drop to the draws outside their own bound (`outside their own footprint
  bounds` small), main strand G-buffer swaps above the 900-945 of 1500 per window, no resumed-after-card increase.

## Hair M5 participation, M3.4 post depth, M2.2 cascade cards (branch claude/hair-m5-m2, READY_FOR_LIVE, 2026-11-07)
- Found in the joined run (.scratch/coord/hair-joined-20261107.log): 0 prepass/G-buffer/velocity strand draws in
  every period, "header order prepass": with rt_hair_groom_primary_order=0 no pass swaps (hair_primary_swap), so the
  strands the user saw were RT-only (shadow/GI). On-screen strands need rt_hair_groom_primary_order=1.
- M5: `hair_primary::Participation` counts every hair pipeline draw (static and instanced FS 0ca5ee93) by pass and
  view in the period line ("hair draws strand/total ...; card-only pass groups k"). Portrait/instanced variants not
  built: one header and pixel record serve the main view; a portrait draw would seal main-view entries, and the
  groom follows the main view's character, not a portrait instance (RTX_HAIR_PRIMARY 5f).
- M3.4: every post consumer reads the D24S8 after the G-buffer; CPU proof (`primary.post_depth`, test_post_depth):
  0 halo pixels of 1576 card pixels, error within one D24 step; control with card depth kept fails all. No fix.
- M2.2 `rt_hair_groom_cascade_drop` (off, live, no spec id): in sun cascade passes, static hair card draws of
  published grooms use a depth-write-free clone (XFB unchanged). Trade-off: no ribbon/double shadows; no hair shadow
  where receivers fall back to the game mask. Counter "sun cascade hair card draws T, without card depth D".
- Tests: hair_groom_asset_test (participation, cascade drop), hair_groom_test (post depth). Shader identity
  221/221 byte-identical to main, request delta 0. Full CTest numbers in the last commit.
- Live (one strands-on run, order=1): period line shows other views / instanced counts and D = T with the key on;
  neck/shoulder card silhouettes gone in Test Dialogue1 with the key on; DoF halo check in the L2 PNG pairs.

## Hair M3.2 sub-pixel coverage and M3.3 RR hair guides (branch claude/hair-m3-image, READY_FOR_LIVE, 2026-11-07)
- M3.2 `rt_hair_groom_coverage` (spec 1042, off, needs rt_hair_groom_primary): two stratified rays per strand
  candidate pixel in `rt/hair_primary_gbuffer.hlsl` (`rt/hair_primary_coverage.hlsli`), a quarter pixel either side
  of the jittered centre; both hit: nearer; one hit: frame-hashed checkerboard; unbiased (h_A + h_B) / 2.
  Compiled in at pipeline creation (restart to enable); header flag 8 switches it off live for a same-run A/B.
- Footprint inflation was not possible: the LSS radius is in the groom BLAS shared with shadow/GI rays.
- Offline gate (CPU `primary.coverage_reference` 8 x 8, 200 strands 0.25 mm at 0.6-3 m, 16 Halton frames):
  single / pair bias +0.10% / +0.63% (4 sigma 2.8%), 3 x 3 box error 0.0649 / 0.0603 (-7%), box flicker
  0.0567 / 0.0531 (-6%); other seeds -4 to -5%. GPU pick = CPU twin on >= 99.95% of hit pixels, same metrics.
  Small gain: G-buffer pixels stay binary. Cost: 2x the strand trace (estimate +0.4 ms at a close-up), unmeasured.
- M3.3 `rr_hair_guides` (spec 1043, off, live): rr_guides read RT2.z as metalness on hair, but class 5 RT2.z is
  the backlit factor: backlit hair fed RR a metal. With the key class 5 gets metalness 0, roughness >= 0.09, fibre
  F0 0.0465 (the game's R lobe). Hit distance unchanged (D-RF does not trace class 5: kNoHit).
- Key off: 215/218 requests byte-identical to main, 3 strictly equivalent with 1042/1043 at 0; request delta 0.
- Tests: hair_groom_test (test_coverage, test_rr_hair_guides), hair_primary_gpu_test (coverage check); build -Test
  207/207 (rr_r11 skipped). Fixed on the way: `primary.trace` failed on a chunk without hits.
- Live (L2, Test Dialogue1, strands on): toggle rt_hair_groom_coverage 1 -> 0 -> 1 and rr_hair_guides 0 -> 1 in one
  run. Predicted: hair pre-post mean unchanged within noise for both; edge-flicker on strand pixels a few % lower with
  the pair; strand pass GPU ms about 2x with the pair; with the guides, backlit hair less smeared under RR.

## Strand AO transmittance and fibre specular indirect (branch claude/hair-m2-m4-light, READY_FOR_LIVE, 2026-11-07)
- M2.3 `rt_hair_groom_ao_t` (spec 1044, knob exp_hair_groom_ao_t, off): GI trace and RT-hit AO rays keep the
  closest strand hit with probability 1 - d_f lum(a_f^n) (rt_groom_blocks), else pass on. Before: strands opaque.
  Layer packs a_f when shadow_t or ao_t is on (restart for the layer).
- M4.2 `rt_hair_fibre_spec` (knob exp_hair_fibre_spec, off): new pass gi/hair_fibre_spec.hlsl after D-RF replaces
  the game's class-5 probe term (transcribed from 3033c895) with the R lobe: one exact lobe sample (hb_sample_r),
  on-screen radiance (D-RF tier a) or the game's probes in that direction.
- Gates: AO bench crossings 3000/3000, closest t error 9.8e-8 m, occlusion 1767.65 vs 1765.57 (2.0 sigma);
  R-lobe furnace worst 0.30% (limit 3%); GPU vs CPU direction p99 9.1e-7, weight 2.4e-5 relative.
- Identity vs main: 160/210 byte-identical, 50 equivalent at default (1026/1044 = 0), 0 failed; hair_fibre 3/3
  identical. Request delta: variants.txt +2 (hair_fibre_spec with/without OMM). Spec id 1044 (1042/1043 are stream A).
- CTests: 206/207 full run, the failure (core_resource_init_test: ensure_rf_layout fixture) fixed and rerun green.
- Cost unmeasured (lives paused). Predicted L2: skin/neck under hair brighter in GI AO with ao_t on (fixture: 22%
  less strand AO at a_f 0.5). Predicted L3: strand specular changes only on strand pixels, mean within the game's
  term where tier (a) is rare; check the Tp transcription with key on vs off on a static camera (no tier a hits:
  difference = A_0 probe(wi) - Tp only).

## Hair groom: 2-D curve tracer for curly atlases (branch claude/hair-m1-curly, READY_FOR_RERUN, 2026-11-07)
- New `tools/hair_groom/curves2d.py`: follows each painted strand along its own direction (profile across the
  strand, recentre on the run). Crossings: keep direction and curvature, follow the strand's R (ID) pixels; a
  duplicate along an already traced strand stops after two widths, as does coasting without own pixels.
  Strands are ordered root to tip by B; points keep the centreline within a quarter width, at most 16 px apart.
- `convert.convert(tracer="auto")` uses it only when the mask's root/row correlation (median over tiles) is below
  0.3, the batch's old reject threshold. The width is taken along the strand normal through the full UV Jacobian.
  The trace is memoised per mask; each card only gets the samples inside its UV box.
- The batch root check for these atlases uses the same correlation along the traced strands (B vs arc length,
  length-weighted median per tile), same 0.3 threshold. Versions: converter 2, batch 5. No gate changed.
- Representative set + all 42 HAIR_Afro_B styles (119 styles, 4 jobs; reports in .scratch/hair-c/base and new):
  50 / 60 / 9 -> 69 / 47 / 3 (pass / review / reject). Sample (80): 50 / 23 / 7 -> 54 / 23 / 3.
  Afro_B: 0 / 38 / 4 -> 17 / 25 / 0 (Afro_Short_B LOD0 IoU 0.95, coverage +0.01). Bugbear beard
  (corr 0.24): reject -> pass.
- Bit identity: the 75 non-curly styles give identical grooms, verdicts and IoU (cached npz compared array by array).
- Cost: 520 s vs 418 s for the 119 styles. The trace takes about 2 min per curly mask per worker process, once.
  Other styles cost the same.
- Review classes in the representative set (the 1217-style report was lost with the reinstall): silhouette IoU on
  sparse stubble (GithyankiCut_Short_D, short beards: coverage within 0.01, noise of the thresholded IoU),
  radiance (beards, mixed signs), helmet and braid cards with roots on the rim. These are gate or reference
  effects, not converter defects. The root channel class was the converter defect, fixed here. Seam stitching and
  tip taper have no failing check on the set: not done.
- Left on Afro_B: B helmets at IoU 0.79-0.85 (the same before), and 17-40 hat-hair cards rooted on the rim.
- Tests: `tests/hair_groom_test.py` test_curves2d (synthetic curl with self-crossing and a crossing strand; radius
  in every direction; auto == rows bit-identical on a row atlas).
- Next: one full rerun (~1 h, 4 jobs) when the other converter or reference changes are in.

## Hair grooms never joined: BVH split ring failed in the BAR heap (branch claude/hair-join-fix, READY_FOR_LIVE, 2026-11-07)

- Symptom (user run 2026-11-07 18:02, Test Dialogue1, and five runner logs): no `[hair groom] ... LOD n uploaded`,
  no `draw with N indices`, no `unmatched hair draw`, no `groom instances` stats line.
- Cause: every one of those logs has `[bvh] allocation failed (N instances); the split is off, the legacy build
  takes over` at the first scene build. Groom joins, uploads and the stats line exist only in the split path
  (layer/bvhsplit.cpp build: ensure_ring at :2372 on main), so no groom ever joins. No ring line and no
  fault range for "instance/InstInfo ring": make_buf failed in AllocateMemory for a 2.9 MB host buffer.
- Why now: after the 2026-11-07 PC reinstall Resizable BAR is off. vulkaninfo: the DEVICE_LOCAL|HOST_VISIBLE
  type sits in a separate 214 MiB heap (157 MiB budget on the desktop); the `[mem]` heaps sum went from
  32187 MB (2026-11-06) to 32404 MB (+214). make_buf (bvhsplit.cpp:137, unchanged since 2026-10-26) asked
  for that type and fell back to system memory only when the type did not exist, never when its heap was full.
  Not a code regression after hv9: the same code drew strands on 2026-11-04 with ReBAR on.
- Fix: `common/host_memory.h` allocate_host_visible: BAR VRAM first, system memory when that allocation
  fails. Used by bvhsplit make_buf and hit_material create (same pattern). With room in the BAR heap the
  choice is unchanged. No shader, key or ABI change.
- Diagnostics: `[bvh] host-visible VRAM (BAR heap) full: <buffer> (<KB>) in system memory` (first 8);
  `[hair groom] join gate: rt_hair_groom 1, linear swept spheres L, native hair capture N -> groom joins on/OFF`
  on each gate change; the allocation-failed line now names ring/TLAS state and the groom consequence.
- Test: CTest host_memory_test (the user's no-ReBAR memory table: BAR full -> system type, ReBAR unchanged).
- Next live (same save, same keys): expect `[bvh] instance ring: 4096 instances` and the 8 `TLAS storage`
  lines, probably one or more `BAR heap full` lines, `join gate ... -> groom joins on`, no `[bvh] allocation
  failed`, then `[hair groom] <id> LOD n uploaded` and `frame N: K groom instances ... matched M` with K, M > 0.
  Alternative without a build: enable Resizable BAR in the BIOS.

## Strand grain attribution, card normal and world tangent keys (branch claude/hair-grain, offline, off, 2026-11-07)
- New tool `tools/hair_groom/grain_stages.py`: per image-chain stage HF energy and line coherence over the class-5
  hair mask; `--ab on,off,on <dirs>` prints the 5g live gates.
- 2026-11-07 Test Dialogue1 traces (strands a, f, fp0, fp2 vs cards e; a/e matched lit mean): strands never carry more
  per-frame HF than cards (a/e: albedo 0.99, lit 0.91, RR output 0.78, final 0.96); motion vectors vary 2.1x more per
  pixel with the cards' outlier rate. Line coherence differs: albedo 0.31-0.33 vs 0.35, lighting 0.28 vs 0.32 (4x the
  strand spread), final 0.75-0.79 vs 0.84. Attributed to the camera-facing fibre normal (N.V = 1 on strand pixels).
- `rt_hair_macro_normal` (knob exp_hair_macro_normal, Advanced): RT0 = the card's own normal, exactly the native RT0.
- Found from the decompiled hair VS 41a168e2: the native RT3 tangent is world space, the strand variant wrote view
  space, so the strands' Marschner lobes used a rotated tangent. `rt_hair_tangent_world` (knob
  exp_hair_tangent_world, Advanced) writes the world tangent. The suspected grazing-term space mismatch is not one
  (frame and ray both world).
- Both under specialization 1045 (restart to enable, then live via header flags 16/32); no cost; request delta 0.
- Live check (Test Dialogue1, one session): RTX_HAIR_PRIMARY 5g lists windows and thresholds.

## 5g live gates evaluated (branch claude/hair-grain-gates, 2026-11-07)
- grain_stages.py: the RR guide pass is found by its RGBA16F normal guide (the 22:05 dump set has no RR pass);
  `--tangent` gives the tangent gate its own grouping; a window with under half the median hair pixels is INVALID.
- Test Dialogue1 one session: both-on #1 (220524) INVALID, a warm-up dump (5790 hair pixels vs ~40,000, strands in
  264/300 frames of that period): scalp only, no hair.
- rt_hair_tangent_world: |T.N| 0.0015 (tangent on, macro off) vs 0.297 (off): PASS. Mean +2.3%. Default-on candidate.
- rt_hair_macro_normal: lit coherence +0.0096 vs margin 0.01: FAIL (one valid on window); RR specular guide HF x2.83:
  PASS; hair mean +31%. Kept off: the brightening is unexplained without a same-session card window.

## build.ps1 prefers VS 2022 (branch build-pin-vs2022, 2026-11-07)

- VS Build Tools 2026 was installed next to VS 2022 on 2026-11-07 13:14; `vswhere -latest` then picked its
  environment while the build trees cache the 14.44 compiler, so every compile failed with STL1001.
- `scripts/build.ps1` now asks vswhere for `[17.0,18.0)` first and falls back to the latest only without VS 2022.
- Moving to the VS 2026 toolset stays a separate step (clean build trees, full RC verification).
- Check: a forced launcher recompile from a plain shell builds cleanly (no STL1001).

## Suspect review (branch suspects, FIXED_OFFLINE, 2026-11-06)
- Confirmed NGX partial installation: independent immediate patches left one Shutdown path stubbed after the other failed (layer/ngx.cpp:138,1800).
- Fixed by preparing both writable targets before either patch; reverse protection restoration handles shared pages and aliases.
- Actual hook/helper regression fails before (exit 2), passes 24 failure/missing-export/alias combinations after.
- Confirmed NR teardown order: slot_destroy freed images before the final global feature release (layer/dlss5.cpp:350).
- Drop every slot/global feature reference before destroying resources; retained NGX owner is unchanged.
- Actual teardown fixture fails before (exit 2), passes shared features, failed creates, foreign device and repeat teardown after. No driver crash reproduced.
- Confirmed polling race: cfg_poll precedes queue_lock (layer/layer.cpp:801,867); config polling locks only after ab_poll_hotkey.
- A local polling mutex protects edge/timing state (layer/util.cpp:224); concurrent production-body fixture fails before (duplicate toggle, exit 1), passes after.
- BelowNormal scripts/build.ps1 -Test: PASS 201/201, 242.06 s CTest; build 55 s. git diff --check passes.
- Shader/manifest request delta 0; no ABI/default changes. No game, queue, Git writes or rf replay edits. GPU cost unmeasured.
- Evidence: .scratch/suspects/build-test.log. Updated TRACK_DLSS5 lifetime and shortcut notes.

## S-F05 R11G11B10 boundaries (branch rrpost-r11, fixed offline, 2026-11-06)

- Confirmed: `shaders/rr_post.hlsl:67` masked the FP32 mantissa without exponent limits.
- Built the real Vulkan R11 image store/load fixture before editing the shader.
  Old emulation: 2250 subnormal, 3 finite-overflow and 12 special channel mismatches
  in the initial 214290 comparisons; no normal-range changes.
- Fixed subnormal steps, finite saturation and NaN payloads; +Inf preserved, negatives zeroed.
  R/G maximum 65024; B maximum 64512. No new key, ABI change or shader request.
- Final fixture: 77933 mixed RGB inputs, 233799 exact channel matches; 0 normal changes,
  0 independent numeric-oracle mismatches and 0 Vulkan validation errors (RTX 5090).
- Validation first found installed ReShade loader interference; reused existing fixture
  isolation (implicit layers disabled), then passed. No game or system settings changed.
- Normal-domain and quantisation-OFF release SPIR-V equivalence: both RR requests pass.
  All 213 unrelated requests are byte-identical. Full-range equivalence is not claimed.
- CPU unit tests check the numeric oracle and boundary corpus; fixture is a build-only CTest.
- Full `scripts/build.ps1 -Test`: 196/196 pass (4m23s); final fixture rebuild/rerun passes.
  Updated merge-scope regression: 9 tests pass. Baseline proof: `--baseline HEAD` (5256bec2d25a).
  Logs: `.scratch/rrpost-r11-build.log`, `.scratch/rrpost-r11-final.log`.
- No game launch, queue write or git write. GPU work only with all three games absent.
- Live GPU cost unavailable (offline only). Existing `rr_post_check` is the later replication check.

## Default Ultra RF replay (branch rf-replay, READY_FOR_LIVE, 2026-11-06)

- Added `pass reflections` for the actual default `gi/rt_reflections.hlsl`
  request; captured defines/specs/push bytes. No hit shading, SHaRC or OMM.
- `replay_pack_pass=1,replay_pack=N` selects one RF frame per positive serial;
  both keys default 0. Selector 0 preserves GI/LS capture. ABI stays 27.
- Reused LS pre/post capture, raw BCn and bounded lossless codec (2048 MiB
  compressed disk cap). Includes RF-time lighting/probes/AO/DFG, alpha/TLAS,
  previous radiance, G-buffer/velocity/origins and shadow inputs.
- Restores native packed diffuse/specular, R16 hit distance and raw counter
  bins before every dispatch, outside timing. R16 diagnostics now use 2 bytes.
- Preserves the soup range when both RF ray masks exclude cascade geometry,
  reusing the LS fix for the NVIDIA 2 GiB GetDimensions limit.
- Synthetic CTest PASS 2.72 s: screen hits, changed history/probe controls,
  finite outputs, zero guards, repeat and paired output/counter byte identity.
- Explicit --ab-spec/--pipeline-stats control: 2 pairs, outputs/counters 100%
  equal; all 3 synthetic references differ by 0 bytes. Synthetic executable
  128 registers, shared 0 bytes; anomalous local-memory value not interpreted.
  Receipt: .scratch/rf-replay-pipeline.log. No production cost/saving claim.
- BelowNormal build.ps1 -Test PASS 200/200 (176.31 s CTest).
  Log: .scratch/rf-replay-final-build.log. Shader identity 218/218 versus HEAD;
  variants.txt request delta 0. Receipt: .scratch/rf-replay-shader-identity.log.
- Job tools/live_batches/rf_pack/jobs/rf-pack-inn.json: inn Ultra, stock hair,
  visible_unfocused_fg, static 45 s plan. Frozen/non-frozen lint: 0 failures.
  New RF expect has no historical receipt; capture remains unqualified.
- Offline only: no game launch, queue mutation or Git writes. Coordinator
  captures the pack, then compares live output/counter bytes before looking
  for exact savings. Actual inn size and capture GPU cost remain unmeasured.

## Default Ultra RF follow-up (branch rf-opt2, NOT QUALIFIED, 2026-11-06)

- Corrected inn packs 093603-61 (stats OFF), 093623-62 (ON): strict
  live equality PASS 2/2 dispatches each, all 3 outputs and raw counters.
- This does not hold under sustained replay: baseline identical-pipeline
  40-pair control fails exactness. First failure: 206 specular bytes / 63
  pixels and 18 R16 distance bytes / 9 pixels; counters equal (stats OFF).
- Diagnostic shader records identical full-float origins, directions and
  closest-hit distances at those 9 pixels. Projected UVs differ by about
  1 ULP across dispatches, cross integer pixel boundaries and select different
  depth/origin/history pixels. Root cause of that arithmetic instability is
  unresolved; no replay fix or durable live-equivalence claim.
- Validation OFF, layer BLAS flags, pipeline statistics, per-dispatch UBO
  re-copy and host-visible UBO allocation all fail to remove the instability.
  Driver executable register/binary-size statistics stay unchanged; driver
  recompilation is not established. Experimental harness edits removed.
- Profile ablations, 40 ABBA/BAAB pairs, 8 warmups: no-probe delta
  -0.216616 ms, 95% CI [-0.409168,-0.214240]; no-trace -1.279008 ms
  [-1.756400,-1.003488]. Exploratory, non-additive, not qualified live costs.
- rf_fast/1041 deferred the unchanged receiver-probe block until after tracing:
  -0.126424 ms [-0.128240,-0.125200], timing bar >= 0.05 ms met;
  registers 128 -> 96, shared bytes 0 -> 1536. Exactness FAIL: REMOVED.
- Control delta +0.000088 ms [-0.000608,+0.000400], exactness FAIL.
- Receipts: .scratch/rf-opt2/{state1-off,state1-on,control,candidate,
  projection-diagnostic,profile-probes,profile-trace}.log and ab.csv files.
- Production shaders unchanged; request delta 0, 218/218 byte-identical to HEAD.
- Relevant CTests PASS 6/6 (28.59 s); merge-scope regressions PASS 9/9.
- No new build-dependent test; merge_scope.py already excludes the 8 native/DXC
  Python gates. No game, queue action or Git writes. Next: resolve sustained
  projection instability before another candidate can qualify.

## Default Ultra RF mismatch (branch rf-opt, READY_FOR_RECAPTURE, 2026-11-06)

- Inn pack frame 1981: specular differs in 1,270,225 bytes / 420,526 pixels;
  diffuse, R16 hit distance and every raw counter match; guards 0.
- (a) RF uses the LS two-phase path: raw pre-dispatch `img6.bin` restores
  gSpecular before every replay; post-dispatch reference is `s0b7.live`.
  They have separate files and disjoint staging ranges, both format 122.
- (b) core/gi.cpp brackets exactly one RF dispatch with phase 1/2 callbacks
  in one command buffer. LS precedes it; GI composite/rrpost/water follow it.
  No other rendering pass writes between these snapshots.
- (c) All 2,234,064 depth-valid no-hit pixels round-trip initial/live/replay
  exactly. Hit differences reach hundreds of native channel codes, not a
  one-code packing rounding error. Captured view formats are recreated raw.
- Missing sampler bias/minLod and other ordinary state is a confirmed capture
  gap, not the proven cause. STATE1 now preserves it; unsupported extensions
  fail capture. The old pack cannot recover lost fields. Host-only UBOs still
  use record-time mapped bytes; the new contract reports their count.
- Added tested two-phase completeness checks and a fixed-writer marker for
  all three raw output pairs and 20/20 STATE1 sampler descriptors.
- Prepared tools/live_batches/rf_pack_state1 (copied rf_pack): serial 61
  stats OFF, serial 62 rt_stats/rf_stats ON; 65 s static plan, about 1 GB.
  Both new packs must pass `--require-live-equal` before optimisation resumes.
- Baseline module equals runtime cache SHA256 27ea62e4...; repeats exact.
  Baseline registers 128, shared bytes 0; replay ~0.79 ms is not live ~2.2 ms.
- No rf_fast/1041 allocation or gain claim. Cost ablations prepared, not run.
  Shader requests unchanged; 218/218 byte-identical to HEAD, ABI unchanged.
- BelowNormal build succeeded: full CTest 199/200; missing frozen regex entries
  fixed additively, then batch lint/live_flow/merge_scope recheck PASS 3/3.
- Evidence: .scratch/rf-opt/{validation,strict-validation,packed-analysis}.log.
- Offline only: no game launch, queue staging or Git writes. Stop for capture.

## RF compact probe state (branch rf-on, qualified, 2026-11-06)
- `rf_compact_probe_state` / SpecId 1031 always on: default, presets and release policy 1; `dev=1` keeps raw 0.
- Core fallback and INI are 1; no GUI knob. Settings tests cover the policy.
- S8 default Ultra `opt-compact-rf-s8.1.102848`: VALID SAVING.
- Reflections OFF/ON/OFF/ON 1.200/1.034/1.224/1.066 ms; OFF spread 0.023 ms.
- Float energy within the OFF band on all channels; luminance -0.00025/-0.00018, band 0.0247.
- Inn live INVALID (readiness); offline paired -0.123/-0.129 ms. Registers 128 -> 96.
- Replay differs only in 63/9 projection-unstable pixels also differing OFF/OFF; no strict equality claim.
- Check S8 default Ultra reflections; developer raw-key A/B. Shader request delta 0; ABI unchanged.
- Defaults and schema regenerated; scripts/build.ps1 -Test PASS, 203/203 CTests (195.01 s).

## Default Ultra receiver probe deferral (branch rf-default, NOT QUALIFIED, 2026-11-06)
- Core applies rf_compact_probe_state / SpecId 1031 to every receiver-probe RF variant,
  including default Ultra rf_hit_shade=0,rf_hit_gi=0. Default remains 0; request delta 0.
- Construction audit: identical expressions/order; inputs unchanged, outputs unread,
  no caller return/discard between blocks, including the default rt_closest path.
- Key-off equivalence PASS 218/218 unchanged compiler inputs; default compile-only
  registers OFF/ON 128/96, shared bytes 0/1536, binary bytes 109312/110592.
- Numeric replay FAIL on corrected inn packs 093603-61 and 093623-62: 40 balanced
  adjacent phase 0/1 comparisons per OFF/OFF and OFF/ON run; 8 warmups/sample.
  Empirical nearest-rank control p99=0 in every channel; no tolerances or reruns.
- Pack 61: specular 63 differing pixels in 4/40 pairs; max RGB .0234375/.015625/.015625;
  mean signed RGB over 40 pairs 3.608e-10/4.963e-10/6.257e-10. R16 9 pixels,
  max 65503.992012, mean signed .0071942075 (sentinel differences included).
- Pack 62: specular 63 pixels in 40/40; max RGB .00604248/.00317383/.00244141;
  mean signed RGB -5.965e-11/-5.174e-11/-5.642e-11. R16 3 pixels in 7/40,
  max 65503.973465, mean signed -.00059951125. Diffuse exact, no nonfinite values.
- Mean-absolute/absolute-signed-mean gates fail too. Legacy whole-quartet controls
  vary outside the selected numeric samples; pack 62 raw counters vary in both runs.
- Paired ms median [95% CI]: candidate 61 -.122904 [-.157664,+.019344],
  control 61 -.000104 [-.125696,+.203392]; candidate 62 -.128760 [-.228944,-.016736],
  control 62 -.000432 [-.000976,+.015328]. Offline noise; no live saving claimed.
- Durable full-channel summary: docs/evidence/rf-default.json; raw replay logs/CSVs
  tools/replay_numeric.py reproduces the gate; tools/frame_replay.md documents it.
- Prepared default inn/S8 OFF/ON/OFF/ON, cost then energy, keep_reshade=false;
  both lints pass. Check reflection edges/cost and pre-post energy in these scenes
  only after further steering; not staged or run. Offline gate failed, key remains 0.
- Build/test: final build.ps1 -Test PASS 203/203 (174.65 s tests; 2 s build).
- No game, queue or Git writes. No junction cleanup. Raw outputs about 302 MB.

## Launcher defect review (branch review-launcher, PASS offline, 2026-11-06)

- Fixed runtime paths containing `#`: INI output quotes them; parsing preserves hashes and literal Windows backslashes. Regression reproduced truncation before the fix.
- Fixed web shortcut capture: Tab traverses normally, repeat events do not rebind, and shifted digits map to the host's digit keys. Each regression failed before its fix.
- Fixed shortcut repair deadlock: editing/resetting one binding no longer requires the other saved binding to be valid. Full-pair validation still gates Save. Two bridge regressions failed before the fix.
- Both GUI launch paths now display the game DLSS guard's repair/error note instead of discarding it.
- Corrected README/USER_SETTINGS: game graphics repair and backup, opt-out, runtime versions/discovery/staging. No settings defaults changed.
- Shader identity: 218/218 byte-identical to HEAD b9377fe237a4 by identical input keys; variant request delta 0.
- Final BelowNormal `scripts/build.ps1 -Test`: PASS 197/197 (10 s build, 252.82 s tests). Settings: 4,165 checks pass. Regression fixtures failed before their fixes. `git diff --check` passes.
- No game launch, queue writes, Git writes, ABI edits or changes to the other lane's files. Logs: `.scratch/review-launcher/`.
- Build environment: prepended the installed VS Installer directory to process PATH for vcvars64's `vswhere.exe` lookup; no build-script change.
- Unconfirmed: concurrent presents on different Vulkan queues may race on `ab_poll_hotkey` static edge state (`layer/util.cpp:224`); no multi-queue reproduction, no speculative fix.

## DLSS 5 offline defect review (branch review-dlss5, FIXED_OFFLINE, 2026-11-06)
- Fixed Streamline deferred-init race: pending scratch check/move and trigger logging share the mutex.
- Fixed stale initialized state after device destruction: cleared SDK callbacks can no longer be
  called by a replacement device; pending scratch, instance, requirements and OF queue state reset.
- Regression executes the actual teardown body with a deferred initialization and a foreign device:
  previous body fails (exit 2); fixed body passes. Existing trigger decision coverage retained.
- Reviewed NR boundary, guide/codec, render-size retirement, bypass, device requirements, runtime
  failure fallback, FG and pre-post HDR contract. No further confirmed defects in these paths.
- Private images remain bounded at 8 x 210.9 MiB for 3840 x 1600, plus NGX history; no new allocations.
- Unconfirmed runtime impact, unchanged: partial NGX hook-install failure can leave shutdown hooks
  installed; NR device teardown destroys bound images before the last feature release.
- Shader sources and manifest unchanged (request delta 0). No game/queue/Git writes; no GPU cost measured.
- Validation: full build + CTest 197/197 (434.18 s); rebuilt final layer + targeted checks 5/5.
  Teardown regression fails before the fix and passes after. No live FG/HDR/recreation qualification.

## Release 0.9.3-beta (branch rel092b, 2026-11-06)

- Version 0.9.3.0 (0.9.2 was never published): launcher version.h, UI sample state, bridge test, Nexus sticky post.
- changelog_0.9.3.txt = the unpublished 0.9.2 notes plus the FG and DLSS 5 release audits (fgx3, dlss5-audit) and the FG focus note.

## RC receipt 0.9.3-beta (branch rc093, 2026-11-06)

- verify_rc PASS 12/12 at e1ce7329 (rc-20261106-e1ce7329-291da9f8); 223 payload files; no DLSS 5 runtime bundled.

## Queue cancellation, ReShade isolation and release-policy lint (branch queue-tooling, offline PASS with limits, 2026-11-06)

- T2: owned phase and command roots use taskkill /T /F after cooperative runner cleanup. Mocked tree cancellation: 8 checks pass. Native descendant termination remains unverified here; the previous sandbox attempt denied taskkill.
- Added keep_reshade (boolean, default false). True clears DISABLE_VK_LAYER_reshade_1 before launch; ordinary jobs set it to 1. Both policies are logged. Session reuse includes this field, so differing policies cannot share a game.
- Reproduced merge-green failures: two obsolete compact-hit assertions and release-profile lint rejecting qualified preset values. Tests now use water Fresnel; lint accepts current schema preset choices while rejecting unqualified overrides.
- data/settings_schema.json matches current main, including DLSS 5 and the removed compact-hit knob. No schema copy or hardcoded knob list added.
- merge_scope.py --ref HEAD --run tooling: all 15 selected Python test scripts and both PowerShell camera tests pass. live_flow: 81 tests; camera input/permission: 692 checks; runner trace/ReShade/session: 29 checks.
- All 17 active jobs across 7 folders audited against main, frozen and non-frozen. Frozen: 0 failures on both. Non-frozen: the same 5 failing jobs and identical problem lists; no new failures.
- Existing non-frozen failures: pt-dialogue3, pt-s8, su1-ultra-s8-fg2, wf9-s8-compose-shift, wsf1-s8-interface-column. Historical replay evidence remains unsatisfied for all 17; no live qualification claimed.
- Logs, corpus caches and comparison reports: .scratch/queue-tooling/. Full build -Test already passes per coordinator; not rerun. Shader request delta: 0. No game launch, captures/queue writes or Git writes. external/ junction untouched.

## L11/L12/L17 submission redo (branch layer-submit2, READY_FOR_LIVE, 2026-11-06)

- Confirmed in code: unowned RR descriptor ring, NGX recording-time initialization,
  and temporal history published from unsubmitted recordings.
- Causes in ae625cef: layer/ngx.cpp:1264; layer/rrpost.cpp:470,1152.
- Read reverted ae625cef. Its global pending guide gate rejects an evaluate recorded
  before the separate producer is submitted; no temporal/final is reached there.
  A later submit is too late for that evaluate. The fixed 256 leased sets can also fill.
- Primary submits publish exact secondary IDs captured at ExecuteCommands, never
  the secondary handle's newer recording. Failed Begin retains the previous ID.
- Descriptor sets stay immutable until recording discard and proven GPU completion.
  New pool pages handle retained executable recordings without dropping post passes.
- Every full-overwrite NGX guide writer carries its own ordered layout transition.
  Only successful submission publishes initialization; no global pending gate blocks RR.
- Per-slot history metadata survives recording N+1 before submit N. Only submitted
  previous-frame history is sampled; absent history still dispatches the fresh temporal
  sample and final grade. Discard cannot publish; an older resubmission invalidates metadata.
- CPU tests execute the production allocator, submit walker and temporal function:
  600 retained sets, primary/secondary IDs, overlapping N/N+1, discard/re-begin,
  97 steady-state history reads, frame gap and older writer resubmission.
- post_recording_test is registered in CMake and merge_scope.py's build-only list.
- Shader identity: 218/218 requests byte-identical to HEAD 5256bec2d25a by identical
  input keys (0 compiles); shaders/variants.txt request delta 0. ABI and presets unchanged.
- Validation: final scripts/build.ps1 -Test PASS 196/196 (build 20 s, CTest 180 s).
- No game launch, queue writes or git writes. build.ps1 and its CTests ran BelowNormal.
- Coordinator gate: rel1-smoke-ultra-s8 must have '[rrpost] gpu temporal' lines and
  output-res grade > 0 (rejected: 0 lines / 0.000 ms; qualified: 32 / 0.079 ms).
- GPU cost and live recovery remain unconfirmed; no live authorization was requested.

## FG release audit, Round 3 (branch fgx3, offline verified, 2026-11-06)

- Baseline da6ffdf8; full checklist and exact references:
  [analysis](../evidence/fgx3-analysis.md), Round 3 audit and linked live/release tables.
- Warmed S8 x3 gives SDK/proxy ratio 3 at ~30 rendered fps, p99 comparable to x2.
  This does not qualify uncapped foreground throughput. Metering stays rejected/off;
  exact retirement is unmeasured because every new window has 0 FG destroy waits.
- Lead 1 is real at first x2 enable in 4/4 initial loads: proxy mean 34-46 ms,
  up to 108 ms with metering, then ~0.2-0.4 ms. GPU intervals also inflate.
  GetState <=0.002 ms max, ReflexSleep ~0.02 ms mean and zero FG waits exclude
  those operations there. SDK-vs-raw-driver/WSI backpressure remains OPEN.
- Fixed release leaks: raw capture/trace/dev SDK/reference/stat overrides now
  respect packaged zero pins after layering and startup Detailed logging latch.
  Explicit support statistics work with Detailed On; correctness inputs remain.
- Quiet release skips FG/tracker timers/counters/cadence formatting, per-frame
  pace sampling/sorting and premature VRAM driver queries. Required fences stay.
- Routine HUD/UI identity logs are stats-only and de-duplicated: 764 menu UI
  lines represent 8 handle changes. Fault/state/input-decision lines remain.
- Developer fallback checks no longer format/lock per release lookup. Hot cache
  refreshes on startup/files/hotkey changes: 100 unchanged CPU-test polls ->0
  refreshes of the 47-key cache. No measured ms saving is claimed.
- Fixed phase-reset contamination; SDK/CPU/raw/wait counts and ratio baselines
  reset together. Detailed-only rawPresent host/worker timers target the open stall.
- Missing mandatory SDK exports fail closed; generated count respects adapter max.
  Current 5090 x3 still requests 2 generated, max 5. No valid-path default change.
- fps_cap 58 remains a rendered 17,241 us interval, not divided for MFG.
  New lives cap 0; their 30 fps ceiling is not either explicit layer limiter.
- Copy remains default 1 for correctness: median GPU 0.026 ms. NR 6.2-7.1 ms
  is external ownership; no ngx.cpp/dlss5.cpp edit. Camera-cut reset/synthetic
  simulation markers remain integration gaps; exact SDK startup wait is unproved.
- Release values: captures/trace/verboseSDK/stats/timing 0 with dev=0/detail=0;
  fg_input_copy/current_destroy_only/letterbox 1; UI PQ/exact/metering 0 by default.
- Validation: build.ps1 -Test PASS 206/206 (CTest 139.14 s), 4,480 settings checks.
  Lean -ReleaseLayer PASS 15 s, BG3RTX_DIAGNOSTICS=OFF; defaults/diff checks pass.
  Shader request delta 0; ABI unchanged; no new key, game, queue write or push.

## DLSS 5 Release audit (branch dlss5-audit, offline fixes tested, 2026-11-06)

- Scope: NR provider, NGX frame contract/hooks, runtime staging and fixture review.
  No game, queue writes, staging, push, Streamline or common/fg_* edits.
- Fixed latent use of a feature whose GPU initialization was never submitted:
  wait without blocking for initial submit; recreate if that recording is discarded.
  Production retirement-function tests cover discard, pending submit and GPU lifetime.
- Fixed six timestamps continuing after statistics turn off with a retained pool;
  gated readbacks too. Quiet Release records/reads zero NR queries.
- Fixed watchdog reason ping-pong after 120 presents (extra flushed state logs).
  Empty submits/NR never recorded now avoid the NR mutex.
- Checklist: CPU waits OK/COST; release logging BUG fixed; policy OK; history OK;
  lifetime BUG fixed/COST; FG ordering OK. Details and source lines: TRACK_DLSS5 §9.
- User log: 18 unavailable-chain transitions, nearest rrpost reasons 14 unknown
  compute effect 4681b95ef9f2e54d, 3 bloom order, 1 chain start. Not proven menus/cuts.
  17 resume logs reset=1 are transition-only logs, not evidence of per-frame resets.
  A true one-frame skip requires reset: MV references the immediately prior frame.
- One feature create, zero recreates; three slot allocations only near startup.
  Private images 632.8 MiB observed, up to 1687.5 MiB for 8 slots, plus NGX/padding.
  Bypass/reset reuses resources; 27 descriptor writes and 400 uniform bytes/frame.
- Contract OK: output-sized 3840 x 1600 NR, guides from 2560 x 1067, MV -1/render,
  NGX MV scales output-sized, no MVJittered flag, inverted depth, exposure 1.
- Existing fx1/fx2 logs, all 49 windows/run: weighted total 6.438/6.524 ms,
  evaluate 6.241/6.325, encode+guides 0.123/0.126, decode 0.071/0.070 ms.
  No NR copy, extra submit, FG-specific wait or intentional FPS cap found.
  Eight of 16 image barriers precede NR timestamps; their boundary cost is unknown.
- Open: rrpost unknown-effect coverage (outside ownership), in-game vendor CPU
  latency/VRAM, cross-recording multiple real RR calls (no observed duplication),
  existing internal NGX validation VUID/Shutdown workaround. No live quality pass.
- Lean Release/DIAGNOSTICS=OFF build passes; shader request delta 0.
  Full build.ps1 -Test: 205/206 pass, CTest 141.86 s (Windows PSModulePath fixed).
  Only inherited batch_jobs_lint fails: snapshot lacks fx1/fx2 regexes.
  Its snapshot/jobs/test are unchanged from da6ffdf8; FG files left unchanged.
  Final incremental build + NR/logging tests 5/5 pass; lean Release rebuilt.
  Logs: .scratch/dlss5-audit-{final-test,final-regressions,release-final}.log.
- Future authorized check: S8 Ultra with NR on, move/toggle Ctrl+Shift+F8 and cross
  the transition that breaks rrpost; check detail/flicker and resume. No new job queued.

## Core completion contract (branch core-contract2, READY_FOR_LIVE, 2026-11-06)

- ABI 27 -> 28: present-queue completed frame and retained exact recording lifecycle queries.
  Both VkLayer_bg3rtx.dll and bg3rtx_core.dll rebuilt together.
- Reused layer-submit2 submit/discard and secondary snapshots. Per-queue fences cover submit
  frame + 1; a covering device-idle wait also completes work. Unknown queries fail closed.
- C7: light/trust/water mapped slots retain all consumer recordings. Reuse requires discard
  plus GPU completion; busy selected slot skips the update and logs once.
- C8: GI initialization/parity commit after submission or roll back after discard;
  stale snapshots are rejected. Ordered submitted GPU history does not wait for completion.
- C12: GI/occlusion counters read only submitted, discarded, completed slots; busy slots preserved.
- C4 remains not confirmed, with the existing ordered-frame proof and limitations unchanged.
- CPU gates model 2/3 frames in flight, FG display ticks, discarded recordings, a stall,
  resubmission, multiple consumers, queue-specific completion and balanced recording references.
- Shader identity PASS 218/218 by identical inputs; variants.txt request delta 0.
- Validation: BelowNormal scripts/build.ps1 -Test PASS 199/199; build 85 s, CTest 309 s.
  git diff --check PASS. Steady models: 80 frames each at 2/3 in flight, zero skips, 72 reads each.
- No game, queue preparation or Git writes. Coordinator Ultra smoke pending;
  check no completion-guard warnings in steady state and existing Ultra effect/temporal gates.
- GPU cost unmeasured (offline only); no new GPU commands or shader changes.

## Core completion contract audit (branch core-contract, ABI_BLOCKED, 2026-11-06)

- C4 not confirmed for ordered submitted frames: eight completion-guarded scene slots x two
  handout frames x two sun sets = 32, matching the descriptor ring. Delayed/discarded
  recordings remain outside this proof; no observed BG3 overlap claimed.
- C7/C12 confirmed synchronization gaps: five scene builds at frames 1,3,5,7,9 permit
  eight-slot upload/readback wrap at frame 9 while frame 1 can remain pending.
  Trust staging precedes scene availability; mapped reads do not wait for completion.
- C8 confirmed recording/discard hazard: failed EndCommandBuffer/reset can discard
  initialization/history while g_images_ready/parity remain committed. Normal ordered
  GPU-only ping-pong does not require CPU completion between frames.
- Host life_gpu_done exists privately; current ABI 27 exports only the CPU frame index.
  FG allows up to three generated frames per app frame, not three pending app frames.
  Present fences poll without throttling; FG copy-slot tokens protect only those slots.
- Stopped before ABI changes as requested: completion must cover the consuming recording;
  C8 also needs submit/discard notification. No runtime, settings, or shader edits.
- Updated all four CONSOLIDATION rows and added source-linked proof/counterexamples there.
- Validation: scripts/build.ps1 -Test PASS 195/195 (build 61 s; CTest 248 s), BelowNormal;
  git diff --check PASS. No game launch, queue writes, or repository git writes.
- Shader request delta 0; all shader inputs unchanged, so ordinary/key-off bytes unchanged.
  No new CPU logic or build-dependent tests; merge_scope.py needs no registration change.

## BLAS flags replay (branch blasflags, rejected, 2026-11-06)

- Replay-only --blas-policy legacy/layer/trace and --ab-blas retained; no product change; shader request delta 0.
- Exact classes from pool/soup/held InstInfo bits and manifest casters; no held instances in these packs. Layer A: static FAST_TRACE | ALLOW_COMPACTION, dynamic/cascade FAST_BUILD | ALLOW_UPDATE; B changes only
  dynamic/cascade to FAST_TRACE | ALLOW_UPDATE. Static BLAS shared; replay does not compact them.
- Sun/far request with Ultra native/instance/cascade/far specs; radius 6 m, 2 rays, rt_stats=0,
  compact-hit-state=1, roof-early=1, --sun-trust; 40 ABBA/BAAB pairs per trace/build/refit, 8 trace warmups/sample.
- LIMIT: packs lack emissive scene descriptors; supported sun/far without emission approximates full Ultra.
  Frozen geometry cannot establish traversal/refit cost after animation. Build timings exclude allocation/TLAS.
- All values ms; trace gain = A-B; extra build/refit = B-A. Brackets are 95% median CIs.

| Pack | Trace gain | Extra AS/frame | Net gain/frame [conservative 95% model interval] |
|---|---|---|---|
| S8 | 0.046672 [0.043168, 0.049936] | 0.175191 | -0.128519 [-0.136867, -0.122779] |
| Camp | 0.377312 [0.232448, 0.641920] | 0.500884 | -0.123572 [-0.389910, 0.193367] |
| Inn | 0.075696 [0.039024, 0.189216] | 0.692690 | -0.616994 [-0.810208, -0.462765] |

| Pack/class | Extra build [95% CI] | Extra refit [95% CI] |
|---|---|---|
| S8 dynamic | 0.457704 [0.420960, 0.487472] | 0.174720 [0.173088, 0.177696] |
| Camp dynamic | 0.251400 [0.183968, 0.324512] | 0.334608 [0.331808, 0.337856] |
| Camp cascade | 0.607416 [0.546464, 0.648304] | 0.159064 [0.157968, 0.160048] |
| Inn dynamic | 1.391960 [1.121872, 1.701520] | 0.434976 [0.429584, 0.441536] |
| Inn cascade | 0.792368 [0.699072, 0.900816] | 0.247184 [0.245120, 0.249232] |

- Net = trace gain - (build + R * refit)/(R+1) extra, R=600 dynamic/60 cascade; Bonferroni component bounds.
- Every measured quartet: outputs/counters 100% bit-equal, 0 differing pixels, max diff 0 (candidates and controls).
- Same-flags trace B-A controls: S8 +0.004112 [-0.004304, 0.024368], camp +0.046920 [-0.103152, 0.239136], inn -0.021656 [-0.134704, 0.026384].
- Old camp runs remain INVALID (Stalker2 overlap); completed reruns guarded before launch/every second for Stalker2/bg3/Cyberpunk; no game detected, no game/queue/git writes.
- Receipts: .scratch/blasflags/{s8,s8-control,camp-idle,camp-control-idle,inn-idle,inn-control-idle}; summary.json, shader.json, command/log/CSVs; previous synthetic/class/AB tests passed, 0 validation errors.

## Exact GPU technique scan (branch techscan, analysis only, 2026-11-05)

- Added `docs/PERF_TECHNIQUES_20261105.md`: source/log evidence, exactness obstacles, net estimates, effort/risk and cheapest offline proofs.
- Ranked: lane/tile permutation 0–0.40 ms; auxiliary AO R16F 0–0.06; hidden-hit wavefront 0–1.50; reduced GI TLAS 0–0.70; async AS 0–0.40.
- All ranges are unmeasured planning estimates including zero; regression is possible. No achieved saving or quality pass claimed.
- Reduced TLAS needs bounds around visible origins, current submit-time transforms, original InstanceIndex mapping and cap/tie equivalence.
- Wavefront must preserve individual emission/shading addition order, fp32 operands, source-pixel seeds and full snapshot lifetimes.
- Most intermediates already use 16F or narrower. No active full-resolution RGBA32F GI image to halve in stock Ultra.
- Origin depth reuse cannot reproduce triangle normals/offsets; origin/sun fusion would retain the expensive primary traversal.
- No completed/rejected technique proposed again. Shader request delta 0; no implementation or ABI change.
- No build, compilation, replay, game, queue, Git mutation or live batch. Only the two requested documentation files written.

## Sun wave rejected; far GI attribution (branch sunwave, offline, 2026-11-05)

- Removed wave code/key/settings/spec wiring, scratch buffers, two requests and feature tests; SpecId 1034 retired, never reuse.
  Manifest requests 127 -> 125, no ABI change. Kept paired `--ab-spec` / `--ab-spv`, tests, CSV, equality checks and docs.
- Rejection ON-OFF median [95% median CI], 40 pairs, ms: S8 +0.735776 [0.540272, 0.849120];
  camp +3.114312 [2.451616, 3.897856]; inn +0.737392 [0.638336, 0.863264]. Outputs/counters were exact; all slower.
- Cleanup release proof vs main `5b076d9`: 215/215 ordinary requests byte-identical; negative control passes.
- Far attribution: original main shaders plus replay-only source snapshots, sun trust, radius 6 m, two rays, statistics off.
  Each value below is its own 40-pair median [95% median CI], ms; conditional execution/compiler effects are not additive.

| Pack | Traversal | Extra far hidden shading | Matched GI roof loop | Other far policy/context |
|---|---|---|---|---|
| S8 | 0.325 [0.324, 0.326] | 1.562 [1.227, 2.396] | 0.465 [0.351, 0.772] | 0.562 [0.561, 0.563] |
| Camp | 3.787 [3.641, 4.060] | 2.037 [1.500, 3.377] | 0.332 [-0.074, 1.362] | 3.217 [2.761, 3.822] |
| Inn | 2.065 [1.973, 2.329] | 0.926 [0.512, 1.154] | 0.374 [-0.007, 0.650] | 1.711 [1.372, 2.074] |

- Fresh original far totals: 2.762 / 9.132 / 4.702 ms; historical 2.430 / 5.460 / 2.448 are a different context.
  Exact frozen roof decisions preserve downstream work. Roof control retains both bodies/common cache load; only S8 excludes zero.
  N/Q outputs and near shading counters match; all roof controls match every output/counter bit, with zero cache/mismatch guards.
- Statistics atomics do no work at rt_stats=0; residual includes fade, screen/eligibility and static compiler context.
  Original near/far driver registers 168 -> 255. No spill/occupancy inference from anomalous memory figures.
- Memoized screen/hidden roof reuse has zero opportunities on all packs. Inn's extra hidden shading changes no stored output.
- Two candidates only, NOT implemented: early exact inside-roof rejection after emission, before far hidden material shading,
  planning 0.2-0.5 ms S8 / 0.4-0.7 ms inn (proportional qualified-hit model, less guard cost; camp could lose);
  exact identity-quaternion roof path, planning 0.1-0.2 ms S8 / 0-0.2 ms camp/inn. These are estimates, not measured gains.
- Cleanup `scripts/build.ps1 -Test` passed: full build and all 191 CTests (158.05 s).
  No game, queue or git writes; builds BelowNormal, GPU used only without bg3.exe.
- Design/proof caveats: TRACK_C_GI "Far GI cost split, 2026-11-05". Receipts: `.scratch/far-cost/summary.json`.
  Cleanup proof: `.scratch/sunwave/cleanup-equivalence.json`; historical rejection: `.scratch/sunwave/summary.json`.
- Analyzed image outputs removed safely; original packs, frozen input caches, shader hashes, counters, logs and CSV remain.

## SER feasibility study (branch ser-study, FEASIBLE_OFFLINE, 2026-11-05)
- Report: [SER_FEASIBILITY_20261105](../SER_FEASIBILITY_20261105.md). No game, queue or Git commands.
- RTX 5090 driver 621.14 advertises NV rev 1 and EXT rev 2 SER; EXT feature true, reordering hint REORDER.
- No SER in compute: both routes require raygen. EXT query-to-hit-object conversion does not remove that limit.
- SDK DXC 1.9.0.5399 compiles/validates explicit NV and EXT hit-object/reorder SPIR-V intrinsics.
  Native HLSL 6.9 calls are unimplemented; external DXC 1.7 rejects both SER capabilities. Plain raygen controls pass.
- Receipts: `.scratch/ser-study/probe.json`, sources/disassembly and vulkaninfo text; no GPU shader execution.
- Smallest renderer experiment keeps ray queries/manual material evaluation in raygen, adds SBT, RT bindings/barriers,
  host feature/dispatch support and likely one ABI bump. Full closest-hit material conversion can wait.
- NO-GO for immediate shipping; conditional GO for 1-2 focused weeks offline. Planning hypothesis 0-10% trace saving,
  with regressions possible; historical hidden-shading analogy gives only 0.15-0.68 ms gross before overhead/roof savings.
- No new measured GPU cost or quality pass. Request delta 0, ABI unchanged. Live checks require a separate authorized batch.

## Ultra scene build audit (branch scene-ana, analysis complete, 2026-11-05)

- Added the measured split and ranked conditional candidates to TRACK_D_SCENE_BUILD.
- Static Ultra receipts: S8 convert/BLAS/TLAS 0.595/0.375/0.203 ms, total 1.175;
  inn 1.219/0.773/0.324 ms, total 2.504. Component medians are not additive.
- Static/dynamic/cascade/hair BLAS GPU times cannot be separated by existing logs.
- Original Ultra already packed through hair trim; High on the same wide-stride
  device had disabled it. This explains why independent packing helped High.
- Static cache, held BLAS reuse, dynamic refits and transform-only TLAS updates
  already exist. Cascade immutability remains unproved and reuse stays rejected.
- No candidate has the requested existing-fixture proof; stopped at analysis.
  No implementation, raw key or specialization allocated; shader request delta 0.
- Read-only source/log review; no GPU, build, game, queue or Git mutation.
  Documentation checked with git diff --check; no runtime qualification claimed.

## Early roof rejection restored (branch roofrej, prepared READY_FOR_LIVE, 2026-11-05)

- Restored gi_far_roof_early, raw developer key default 0, SpecId 1038, request delta 0; release forces OFF. Identity stays removed.
- IDs 1035/1036 stay retired. The host forces 1038=0 whenever rt_stats != 0, retaining the original cached executable and diagnostics.
- Same original-origin/first-padded-roof/t > RB proof, after emission; finite, subnormal and unknown fallbacks remain conservative.
- Exactness PASS: S8/camp/inn outputs and every raw counter bin 100% bit-equal at rt_stats 0 and 1.
- Statistics off: 40 ON/OFF ABBA/BAAB pairs each. Statistics on: original main vs specialization 0 and host key ON/OFF 0/0, 2 pairs each.
- RTX 5090, sun trust, radius 6 m, two rays; ON-OFF median [95% median interval], ms:

| Pack | rt_stats=0, 40 pairs |
|---|---|
| S8 | -1.734 [-2.734, -1.398] |
| Camp | +0.495 [+0.103, +1.696] |
| Inn | -1.361 [-2.429, -0.798] |

- The 40-pair camp +0.495 [+0.103, +1.696] loss was not reproduced by the stability check below; no reproducible camp loss established.
- Camp identical OFF/OFF control, 40 pairs: +0.128 [-0.821, +0.323] ms, exact. All requested candidate results retained.
- Registers unchanged: compact state 0, near 168 / far 255; production compact state 1, near 168 / far 243. ON/OFF match main.
- Key-off release equivalence vs main 9b38f38c: all 215 requests / 2,048 assignments pass, negative control passes.
- scripts/build.ps1 -Test PASS: 192/192 CTests, 169.53 s. Updated build-free roof_jobs test also passes all four cases.
- tools/live_batches/opt_r6/roof: copied compact_hit profile/schedule, inn + S8 Ultra, dev=1, OFF/ON/OFF/ON 600 frames, fresh QUIET.
- Qualified rt_compact_hit_state=1 fixed throughout. GI trace decides; all six timers retained; JSON/stdout contain preceding-OFF pair deltas.
- pipeline_readiness TABLE uses [GI_TRACE]; snapshot extends six queries only, preserving all original receipts/models. Both lints pass 2/2.
- Main checkout .scratch/roofrej1038/summary.json: commands, modules, CSVs, raw counters, lints, timing/control receipts. Original packs unchanged.

- Camp stability check: three 80-pair A/B runs, each preceded by its own 80-pair OFF/OFF control; idle GPU, no games:

| Run | ON-OFF median [95% CI], ms | OFF/OFF control |
|---|---|---|
| 1 | -0.005904 [-0.143744, +0.059024] | +0.008064 [-0.132944, +0.245616] |
| 2 | -0.110912 [-0.211504, +0.079696] | +0.022808 [-0.174240, +0.104528] |
| 3 | +0.100592 [-0.012960, +0.200400] | +0.000448 [-0.076752, +0.093728] |

- Pooled 240 ON-OFF pairs: +0.005064 [-0.060240, +0.059024] ms; all outputs/raw bins exact.
- Count: 0 early rejections / 492,480 guard evaluations / 5,463,040 rays. Temporary count view only; production unchanged.
- If needed, test origin containment before direction rotation/slabs. Host zero-roof gate alone cannot help camp (15 volumes).
- Fresh receipts are worktree-local .scratch/roofrej-camp-repeat/summary.json; prior receipts remain at the main-checkout path above.
- Coordinator decision: camp settled with 3 x 80 pairs: pooled +0.005 [-0.060, +0.059] ms, early path taken 0 of
  492,480 guard evaluations. Candidate passes (S8 -1.73, inn -1.36, camp neutral); live confirmation next.

## Exact early roof rejection (branch roof-on, kept, 2026-11-05)

- Qualify `gi_far_roof_early` (SpecId 1038) like `rt_compact_hit_state`: default/presets/release forced 1.
- `dev=1` preserves raw 0 for A/B; no player knob. Host statistics mode keeps the original path.
- S8 Ultra `opt-roof-s8.1.211159` VALID: GI trace OFF/ON/OFF/ON 7.655/6.275/7.937/6.226 ms (-1.38/-1.71).
- GI total 12.349/10.514/12.386/10.382 ms; RR about +0.2 ms in ON windows.
- Inn INVALID (last window short); complete windows about 12.2 ms ON vs 12.5/13.2 ms OFF.
- Offline paired: S8 -1.73 ms, inn -1.36 ms; camp neutral, 240 pairs +0.005 [-0.06,+0.06] ms.
- Exact (bit-equal). Request delta 0; ABI unchanged. Supplied live evidence; no new game/queue or git writes.
- Check S8 Ultra roofed-room far bounce: developer A/B leaves the image unchanged and reduces GI trace cost.
- Regenerated defaults/schema; settings checks 4159/4159 pass. Full build succeeds; CTest 192/193 pass (170.48 s).
- Remaining: `tests/gi_far_roof_test.py:67` expects cfg fallback 0; change to 1 in the owning lane (outside edit scope).

## RF receiver probe live state (branch rf-ls-regs, READY_FOR_LIVE, 2026-11-05)

- `frame_replay --compile-only --compile-pass reflections|local-shadows` adds explicit production layouts;
  no new replay pass, pack, dispatch or ABI change. Uses cached SPIR-V; Vulkan validation passed.
- `rf_compact_probe_state=0`, raw developer key, no knob/preset; specialization 1031. Request delta 0.
  ON moves the unchanged receiver probe/DFG/occlusion block after hit traversal/shading.
  Same input loads and arithmetic, probe accumulation order, rays, samples and precision.
- RTX 5090 RF request: rt_reflections_hit_shade_em, RT_SPEC_NATIVE/HAIR_ORIGIN/HIT_INST/CASCADE=1,
  RF_SPEC_HIT_GI=1; specs 1000/1003/1004/1013=1, 1008/1009/1010/1020/1026=0.
  Baseline / candidate OFF / ON: 255 / 255 / 245 registers; Shared Memory Size 800 / 800 / 32 bytes.
- Hit GI disabled (1013=0): candidate OFF / ON 229 / 230 registers, shared bytes 32 / 32.
  Core therefore only activates 1031 when rf_hit_gi > 0. No default promotion or GPU-time claim.
- LS request: rt_local_shadows_box_k, RT_SPEC_NATIVE/HAIR_ORIGIN/CASCADE=1; spec 1000=1,
  1008/1009/1010/1020/1026=0: 168 registers, Shared Memory Size 7168 bytes. Unchanged split/reload;
  no LS candidate or spec 1032 added. All runs report Local Memory Size 68719476752 (anomalous),
  so actual spill bytes are unavailable; the Shared Memory Size figures above are driver labels.
- Exactness by construction and ON/OFF specialized SPIR-V review, not a rendered replay:
  4 image-write sites, 8 ray-query sites, 378 integer-atomic-add sites in both; float opcode counts match.
  All 2277 final composition/debug/output/statistics lines match after 8 reviewed substitutions for
  the relocated probe outputs / merge predecessor. Evidence: .scratch/rf-ls-regs/stored-result-review.txt.
- Production request OFF equivalence vs 6763587b PASS: all 256 specialization assignments covered,
  universal symbolic certificate and negative control PASS.
- Full OFF release equivalence vs 6763587b PASS: 214 requests (196 identical, 18 equivalent),
  2150 assignments covered, 18 universal certificates, 30 frozen checks, negative control PASS.
  Only 17 uncached entries compiled; final CPU comparison reused all 484 cached entries.
  Evidence: .scratch/rf-ls-regs/release-final.json and release-final-cached.log.
- scripts/build.ps1 -Test PASS: 184/184 tests (267 s); settings and frame replay included.
  Final core gating adjustment rebuilt with scripts/build.ps1 -Core PASS. All BelowNormal.
- Initial build/CTest interrupted when bg3 restarted; completed tests passed. No game was launched
  by this lane; no queue access or Git write commands. Checked Get-Process bg3 before GPU commands.
- Pending authorized live: inn, hit GI on, same-run OFF/ON/OFF energy/image/cost A/B and Ultra smoke.
  No RF/LS replay pass exists. 245 registers does not cross the next occupancy step; measure cost live.
  Reproduce statistics with tools/frame_replay.md. Changes left uncommitted for the coordinator.

## Compact RF live batch (READY_FOR_LIVE, 2026-11-05)
- opt_r5/compact_rf: inn/S8, 600-frame static OFF/ON/OFF/ON, dev=1, RF hit shading/GI held ON.
- Reflection medians, preceding-OFF pair deltas/verdicts; four separate rrpost slot-2 float exports.
- Frozen lint PASS (2 jobs); 9 additive readiness entries. Registered test: 8/8 PASS; parent/regression checks PASS.
- No launch or queue/Git writes. Readiness and completion within the 300 s cap remain unproven.

## ReShade compatibility: Streamline device init after vkCreateDevice (branch reshade-compat, READY_FOR_LIVE, 2026-11-05)
- Symptom: with ReShade 6 installed (implicit layer above ours) and fg=2, every start since 09:56 wrote a
  Streamline mini-dump; the game hid DLSS and our RR never ran.
- Cause: `sl_device_ready` ran `slSetVulkanInfo` at the first `vkGetDeviceQueue`. ReShade calls
  `vkGetDeviceQueue` inside its own `vkCreateDevice`, before the loader fills the device dispatch table.
  NGX (inside slSetVulkanInfo) resolves device functions through vulkan-1's `vkGetDeviceProcAddr`, got null
  for all of them (sl.log: `NGXCubinVulkan::LoadVkEntrypoints: Failed to load VK device function: vkAllocateMemory`
  ... `vkCmdWriteTimestamp`), then called a null `vkGetBufferDeviceAddress` (0xc0000005 at 0, eErrorExceptionHandler).
  Without ReShade (11:28 run) the same build starts RR and FG normally.
- fg=0 is not affected: Streamline is not started and the game's own NGX Init runs after vkCreateDevice returned.
- Fix: `sl_init_after_create` (default 0). With 1 (and sl_init_at=0, the shared-NGX default), slSetVulkanInfo
  runs inside the game's stubbed `NVSDK_NGX_VULKAN_Init*`, which is surely after vkCreateDevice returned and
  before the game uses NGX. First swapchain stays the fallback. Decision logic in `common/sl_init_trigger.h`,
  CTest `sl_init_trigger_test` (12 checks). New log lines: `[compat] ReShade loaded: <path>` / `not loaded`,
  `[sl] slSetVulkanInfo trigger: ...`.
- Live job ready (not run: needs the user's go): `tools/live_batches_staged/reshade_compat/jobs/rs1-reshade-fg2.json`
  (Water Queens House, about 30 s, `live_flow` clean; staged outside tools/live_batches because the CTest lint needs a frozen corpus match for its new log lines, which only a live can give: move it in after the run). It sets `keep_reshade: true`; the queue-tooling branch, which
  disables ReShade in test runs, must honour that field before this job runs there. On main the runner does
  not disable ReShade. Gates: ReShade loaded, trigger = the game's NGX Init, slSetVulkanInfo ok, RR upscaler,
  DLSS-G status 0x0 x2; forbid writeMiniDump and `Failed to load VK device function`.
- If it passes: default 1 (also good without ReShade: the same point is after create there too).

## Release smoke capture windows (branch relsmoke-fix, kept offline, 2026-11-05)

- Cause: make_final used frozen tightening. flow.retime -> flow.tighten -> duration_budget
  reduced energy_phase's 8 s to 3.8 s using stale capture-stall evidence. Current runner
  preflight requires more than 3.85 s, including serialized capture completion.
- Fix: regenerate with the runner's current VALID corpus through existing live_flow
  sizing. S8 energy windows are 3.9 s (minimum > 3.851297 s); Dialogue1 is 4.2 s
  (minimum > 4.1988 s). No preflight or gate was weakened. Regenerated
  rel1/rel2/rel3, final combined S8 and its stock-hair companion, manifests/predictions.
- Runner live_flow --preflight against main captures/queue/results.jsonl: 4 jobs,
  0 failing, 67 readable VALID logs. Rel1/rel2/rel3/final scheduled seconds:
  52.740 / 61.640 / 79.100 / 115.548. Readiness remains explicitly unproven where absent.
- Snapshot: only 3 readiness queries appended; every existing entry/model preserved.
- Tests: release_qual 6/6, batch_jobs_lint 12/12, live_flow 72/72. Added a regression
  checking generated energy windows exceed their serialized capture completion bounds.
- No shader/ABI/rendering change (variants delta 0); no GPU cost measurement needed.
  No commit, game launch or queue staging. READY_FOR_LIVE is not a quality pass.
- git merge main was denied by the filesystem at ORIG_HEAD.lock; HEAD already contains
  all of main (HEAD..main count 0), so checks used the current tools without merging.

## GI origin audit (branch origin-opt, rejected, 2026-11-05)

- Retained `frame_replay --origin` and `--compile-pass origin`; existing S8 pack has
  camera/depth/normal/geometry/alpha inputs. Full-resolution RGBA16F paired comparison.
- Trial `gi_origin_fast=0`, SpecId 1037: group-shared identical depth samples.
  S8 validated 40 pairs: ON-OFF +0.068368 ms, 95% CI [-0.093328,+0.333072] ms;
  every output and raw counter bit equal. No demonstrated saving: candidate removed.
- Driver OFF/ON: 80/80 registers; executable 59008/59776 bytes; shared 2304/2192 bytes.
  Stack 0; reported local memory 68719476752 (verbatim, not spill evidence).
  Achieved occupancy unavailable. Offline timings do not replace the live pass cost.
- Depth-derived interval already exists. Further narrowing lacks an exact nearest-hit,
  tie and candidate-cap proof. Alpha bypass / half-grid skip also lack equivalence.
- Original vs candidate-OFF outputs/counters bit-equal. Live-reference max difference
  6.103516e-05: reconstructed AS does not establish universal live equivalence.
- Shader/default/dispatch changes removed; 1037 retired; request delta 0, ABI unchanged.
  No game, queue or git writes. No in-game check needed for the removed trial.
- Evidence `.scratch/origin-opt/`: s8-valid, off-equivalence, stats-off/on and logs.
  Earlier binding-invalid runs corrected and repeated; not qualification evidence.
- Harness build and 2 CPU CTests pass; all 3 origin shader requests byte-identical to HEAD.

## Raw compressed LS replay (branch ls-pack2, offline verification, 2026-11-05)

- Inn log: four replay lines; only blocker was decoded BC4/BC5 exactness.
  No other replay warning was present. The failed capture published no pack.
- Diagnostic sampled BC1-BC7 allocations now carry TRANSFER_SRC, including
  BC4/BC5 UNORM/SNORM. Raw copies use bound mip/layer ranges and block rows;
  manifest/replay retain VkFormat and component swizzles. LS rejects decoded BCn.
- Decoded compatibility reads remain available for other passes.
- Synthetic tests cover odd sizes, partial blocks, nonzero mip/layer bases,
  multiple layers/mips, lossless file bytes and sampled-value identity.
  Production LS fixture samples a BC4 atlas and BC5 cookie with control runs.
- Disk cap remains 2048 MiB, including manifest and codec overhead. Block writes
  now check the remaining budget before writing; undersized/exact-fit tests added.
- ABI unchanged at 27; shaders/variants.txt request delta 0; no LS shader change.
- BelowNormal build.ps1 -Test PASS 195/195 (166.33 s CTest).
  Log: .scratch/ls-pack2-final-build.log. Raw byte/range differences 0 across
  110 upload pieces; sampled differences 0. LS fixture PASS in 12.91 s.
- Synthetic pack directory: 4,610,652 bytes, below 2,147,483,648-byte cap.
  Codec cap tests pass: pre-write refusal, header accounting and exact fit.
- Offline only: no game launch, queue mutation or git writes. No GPU cost claim.
  Real inn pack size and live-reference equality still require a future capture.

## Production Ultra LS replay (branch ls-pack, READY_FOR_LIVE, 2026-11-05)

- Added `local_shadows`: production box exact-k request, recorded specialization
  constants and push bytes, no shader change; variants.txt request delta 0.
- `replay_pack=1` snapshots immediately around LS after its light-slot join.
  Captures shadow records/atlas, projector records/2D cookies, initial packed
  diffuse/specular, folded AO, sun mask, auxiliary state and output references.
- Replay restores mutable colour/auxiliary/counters before every dispatch,
  outside timing; existing `--pipeline-stats`, `--ab-spec` and raw comparisons apply.
- ABI 26 -> 27 is required for pre/post capture, extra descriptor sets and specs.
  Both host and core must be rebuilt together. No Git writes were performed.
- Pack budget: bounded lossless compression preserves required textures;
  compressed disk cap 2048 MiB. Actual inn size is pending capture.
- Prepared `tools/live_batches/ls_pack/jobs/ls-pack-inn.json`: inn save
  `The Blushing Mermaid - 96h 39m`, Ultra, one static LS frame, 45 s plan,
  visible_unfocused_fg. NOT authorized, staged or queued. Frozen/non-frozen lint pass.
- CPU tooling: live_flow (74), merge_scope (9), batch_jobs_lint (12) and
  queue_gates tests pass. Native pack/codec and ABI CTests pass.
- Synthetic LS CTest passes (12.74 s): guards 0, real cookie/atlas controls change
  output; paired outputs/counters 100% bit-equal and synthetic references 0 diff.
- `--pipeline-stats` paired replay: 168 registers, 7168 shared-memory bytes;
  2 control pairs 100% equal. No optimisation or production GPU cost claim.
- Synthetic referenced payload: 1,527,712 decoded -> 1,332,032 disk bytes,
  plus 1,412-byte manifest. Real inn size remains pending live capture.
- BelowNormal `scripts/build.ps1 -Test` PASS 195/195 (164.93 s CTest).
  Final log: `.scratch/ls-pack-final-build.log`. Both DLLs rebuilt for ABI 27.
- Fixture corrections: screen-sized froxel and an oblique projector make the
  cookie/atlas controls observable; invalid and zero-delta setups were rejected.
- No live result, real-pack size or production GPU cost claimed. Capture is diagnostic overhead;
  replay/live TLAS differences must be reported separately from exact offline A/B.
- Synthetic CTest checks packed initial restoration/repeat/A-B equality and format.
  Check in game only through the prepared job after explicit batch authorization.

## Exact local-shadow optimisation (branch ls-opt2, OFFLINE QUALIFIED, 2026-11-05)

- Offline only; no game, queue or Git writes. Inn pack frame 1984, production
  box exact-k request; shader request delta 0, ABI unchanged.
- Fixed replay: appending ray-mask-excluded cascade geometry enlarged soup from
  2,146,435,072 to 2,381,090,368 bytes. NVIDIA GetDimensions returned 0, causing
  92,389 alpha bounds guards and 45,674 diffuse / 41,431 specular differing bytes.
- Preserve the captured soup range when both effective LS specializations exclude
  cascade geometry, respecting common and paired overrides. Regression cases added.
- Fixed replay: 0 differing bytes in diffuse, specular and auxiliary outputs;
  every raw counter matches live, guards 0, two repeats deterministic.
  Receipt: `.scratch/ls-opt2/fixed-validation.log`.
- Baseline: 168 registers, 7,168 shared-memory bytes; replay 0.821296 ms median
  (two dispatches). Replay versus live about 2.0 ms is a context difference,
  not an optimisation result. Driver local-memory statistic is anomalous.
- Added replay-only `--ab-warmup` with complete GPU input restoration; default 0.
  Synthetic warmed/unwarmed output and counter identity passes. Final same-module
  control, 128 warmups/40 pairs: -0.001752 ms, 95% CI [-0.004256, +0.001008].
  Earlier 64-warmup control predates the final barrier; not the matched cost gate.
- Seven nonadditive ablations: rays -0.289352 ms, point/spot loop -0.536176 ms,
  boxes/projectors -0.232368 ms (each resolved at 95%). Ownership, atlas, weight
  BRDF and duplicate early tests remain unresolved. The captured join is not timed.
  Full intervals/registers: `.scratch/ls-opt2/profile-summary.md` and `.json`.
- Kept `ls_fast=0`, developer-only, SpecId 1040, forced off in release/presets.
  K1/NR1 omits independent unused reservoir 1; other modes retain the old path.
- Forty paired samples: saving 0.011552 ms, 95% CI [0.004832, 0.015232], about
  1.5% of the roughly 0.77 ms warmed replay; no live 2.0 ms extrapolation.
  Every output/counter bit equals OFF: 160 measured dispatches at rt_stats=0,
  eight at rt_stats=1. Final production SPIR-V equals the timed candidate bytes.
- Registers 168 -> 168; shared memory 7,168 -> 6,912 bytes. Duplicate-load and
  box lifetime trials gave no register reduction and were not retained.
- Key-off release equivalence: 203 identical + 12 equivalent, 0 different;
  260 assignments, universal certificates and negative control PASS.
  Receipt: `.scratch/ls-opt2/release-equivalence.json`.
- Recheck inn frame 1984 offline with `--ab-spec 1040=0,1 --repeat 40
  --ab-warmup 128 --ab-require-equal`; no live test authorized or run.
- BelowNormal `build.ps1 -Test` PASS 195/195 (173.56 s CTest).
  Log: `.scratch/ls-opt2/build-test.log`; filename classification/package guard pass.
- Coordinator decision: ls_fast dropped as negligible (0.012 ms, below the 0.05 ms bar used this round); code removed,
  SpecId 1040 retired. Kept: the LS replay fix (live outputs and counters bit-equal).

## Local-shadow replay feasibility (branch ls-opt, stopped at analysis, 2026-11-05)

- Audited all 5 `test-main/captures/replay-*` manifests: 20261101-142819-1,
  20261102-013032-1, 013545-2, 013914-3 and 035919-1.
- Verdict: these packs cannot drive the production Ultra box-k local-shadow pass.
  No candidate implemented; no `ls_fast` key or specialization ID allocated.
- Present: camera, full-resolution depth/normal/origin, point/spot flags, scale,
  froxel headers/index list/light array/constants/cam2 (set 0 b21-26, b29),
  joined light slots (b46), material/alpha geometry and TLAS reconstruction data.
  Albedo/material can be remapped from pack set 2 b2/b3 to LS set 0 b3/b4.
- Missing in every pack: LS set 0 b27 shadow-view records (game set 1 b40)
  and b28 shadow atlas (game set 1 b41). `bg3_lights.hlsli:192-242` uses
  both to reproduce the game's visibility term; zero dummies change that term.
- Missing: game set 1 b29 projector records, bound at LS set 3 b29, and
  bindless 2D cookie images at LS set 3 b45. Pack set 2 b45 contains only
  cube views (0 captured 2D cookies in every manifest); these cannot substitute.
  See `bg3_boxes.hlsli:25-27`; the cookie sampler alone is insufficient.
- Missing: initial R11G11B10 diffuse/specular (LS set 0 b6/b7), which
  `rt_local_shadows.hlsl:347-385` reads for AO give-back, addition and clamping.
  `prevRadiance` is not these two separate pre-LS buffers.
- Missing: sun mask b16 and auxiliary image b44, used conditionally by the
  production AO/sun tail; b44.zw is also written. Pack AO at set 2 b6 is
  captured before the RT AO fold, not a captured LS-time b12 input.
- Capture supplies GI push constants and GI outputs only (core/gi.cpp:2639-2662).
  LS additionally selects `ls_has_mask`, join-success flags and exact-k mode
  immediately before dispatch (core/gi.cpp:3315-3370); no LS output reference
  or LS counter reference is captured. Joined slots themselves are available.
- Required for faithful replay: an LS-time input snapshot including the missing
  resources, its actual push/constants and specialization values, plus output
  references. Restore both colour inputs, auxiliary state and counters before
  each paired dispatch, outside timing. Current packs cannot recover these bytes.
- Existing `--compile-only --compile-pass local-shadows` creates a pipeline only;
  its 168-register audit establishes no output equivalence or timing gain.
- Stopped per feasibility gate. No GPU, compiler, build, game, queue or git writes.
  No shader/core changes; request delta 0; release code unchanged. Build/tests
  and release-equivalence compilation not run for this analysis-only outcome.

## DLSS 5 page and keyboard shortcuts (branch hotkeys, offline verified, 2026-11-05)

- Dedicated DLSS 5 page in WebView and native fallback, visible without experimental opt-in. On/Off, runtime picker, discovery/signature/rejection status and shortcut capture. Experimental wording retained.
- NR remains Off in all four presets and requires Ray Reconstruction. Activation/runtime changes still require restart.
- hotkey_dlss5 defaults to Ctrl+Shift+F8. The live toggle uses the settings override for dlss5_nr_bypass, only when dlss5_nr was enabled at startup; logs [dlss5] hotkey: NR on/off. Unrelated reloads preserve the live bypass; explicit bypass INI changes supersede it.
- hotkey_rt_toggle defaults to Ctrl+Shift+F9. Capture, None, Reset to default, save/discard and conflict validation on DLSS 5 / Advanced. Ctrl+Shift+F10 stays reserved and unchanged, including its existing Alt-modifier behaviour.
- Parser runs once per settings reload. Invalid/conflicting raw INI bindings disable both configurable shortcuts and log a warning.
- Offline gates: build.ps1 -Test PASS 190/190 (189.39 s); final targeted build/tests PASS 8/8, including NR On without opt-in, parser, persistence, bridge and renderer. Defaults generation check and git diff --check pass.
- Shader request delta 0; no ABI or rendering-path change, no new GPU pass/cost. Live input confirmation remains untested (offline-only instruction); no game launch or queue operation.
- Check in game later: enable NR before startup with a compatible runtime; press the DLSS 5 shortcut twice in the same view and inspect the log. RT shortcut still switches all ray tracing off/on independently.
- No git writes; coordinator commits. layer/dlss5.cpp and tools/dlss5_fixture.cpp untouched. external/ junction preserved.

## Conservative GI-only TLAS (branch gitlas, rejected, 2026-11-05)

- STEP 1 only, offline RTX 5090, production sun-trust far GI, 6 m, two rays, compact hit state 1.
- Full instance slots/order, indices, transforms, flags, SBT offsets and BLAS addresses preserved; exclusions change only masks to 0.
- Only GI closest-hit uses the second TLAS; nested lighting visibility retains the full TLAS.
- All positive-depth surfaces, captured geometric offsets, bias/footprint fallbacks and outward rounding define conservative reach bounds. Unknown bounds retain instances.
- S8/camp/inn axis expansion E: 6.486/7.110/6.787 m; safe spherical R_max: 6.842/7.922/7.363 m. Culling uses the tighter per-axis envelope, which contains every GI-origin radius-6 m sphere.
- Culled GI-eligible instances: S8 10/874 (1.14%); camp 9/1039 (0.87%); inn 53/1506 (3.52%). Original slots: 875/1040/1507.

Each row: 40 ABBA/BAAB pairs, four excluded warmups; ms GPU. Delta is B minus A; intervals are exact 95% median intervals. A/B are separate medians of pair means; their difference need not equal the paired median delta.

| Pack / rt_stats | GI full / reduced | GI delta [95%] | Extra BUILD + dependency | Net delta [95%] |
|---|---|---|---|---|
| S8 / 0 | 5.068 / 4.703 | +0.216 [-0.496, +0.442] | 0.326 | +0.535 [-0.175, +0.815] |
| Camp / 0 | 12.374 / 12.621 | +0.449 [-0.532, +1.137] | 0.267 | +0.712 [-0.269, +1.461] |
| Inn / 0 | 4.589 / 4.848 | +0.248 [-0.470, +0.990] | 0.362 | +0.590 [-0.129, +1.354] |
| S8 / 1 | 27.748 / 27.826 | +0.511 [-0.517, +1.357] | 0.253 | +0.740 [-0.261, +1.616] |
| Camp / 1 | 100.295 / 100.161 | +0.233 [-1.097, +1.417] | 0.167 | +0.400 [-0.930, +1.582] |
| Inn / 1 | 28.569 / 28.372 | +0.033 [-0.933, +0.544] | 0.281 | +0.314 [-0.664, +0.795] |

- All stored outputs and every raw counter/bin were 100% bit-equal in all six comparisons, with validation enabled and exit 0. Each B dispatch builds the extra TLAS afresh.
- No interval establishes a net gain. STOP: no STEP 2 product design or product implementation. This rejects the tested enclosing-AABB candidate; no general production exactness claim.
- Net excludes offline CPU bounds/filter/upload costs: depth bounds 156-157 ms; filter context 6.7-12.9 s includes replay BLAS reconstruction, not a product-frame estimate. GPU net is already an optimistic lower bound.
- Initial S8 run INVALID: missing production sampler layout set 1/b19. Stopped, fixed replay layout and pipeline fail-fast, rebuilt; all six new valid runs retained separately.
- BelowNormal minimal build; focused CPU CTests 3/3 PASS, cached production SPIR-V validation PASS. The new bounds test is a native CTest; no build-dependent Python test needs merge_scope exclusion.
- Receipts: `.scratch/gitlas/summary.json`, six `*-valid` directories/logs, raw outputs/counters, `ab.csv`, build/net pairs and full intervals. Historical receipt field `R_max` means E; summary normalizes it explicitly. Replay limitations retain masked unsupported geometry and no OMM.
- Shader request delta 0; ABI unchanged. No game, queue or git writes; GPU ran only after checking no bg3.exe. Reproduction: [frame_replay](../../tools/frame_replay.md).

## Compact GI hit state (branch gi-livestate, qualified always on, 2026-11-05)

- Added frame_replay `--pipeline-stats`, pack-free non-SHaRC GI `--compile-only`,
  uint32 `--spec`, exact `--reference-out` output/counter gates and repeat median/range/spread.
- `rt_compact_hit_state=1` by default and in every preset, specialization ID 1029.
  No UI knob; release policy forces 1. `dev=1` preserves a raw 0 for A/B.
  ON retains the already evaluated triangle normal across local-light queries instead of
  retaining the complete hit for sun/AO normal reconstruction. Guard increments are replayed.
  Rays, samples, precision and accumulation order are unchanged. Request delta: 0; ABI unchanged.
- RTX 5090 production `gi_trace_rt_hit_sun_em` request: RT_SPEC_NATIVE/HIT_INST/CASCADE/GI_FAR=1;
  specs 1000/1003/1007=1, 1008/1010=0, 1004 tested at both 0 and 1 (remaining defaults).
  Registers baseline/OFF/ON: 255/255/243 for both wind values; spill bytes 816/816/44.
  Occupancy unchanged at 8 warps; the gain is from spill reduction.
  Replay `gi_hit_direct` registers: 168/168; shared bytes 8192 -> 4608 (all three packs).
- Offline replay (24 dispatches per run): ON differences are within repeated OFF noise.
- Live inn Ultra: two VALID runs `opt-compact-hit-inn.1.105908` and
  `opt-compact-hit-inn.1.110402`. GI trace OFF/ON/OFF/ON:
  12.721/12.555/13.228/12.634 and 12.683/12.563/13.115/12.843 ms.
  Every ON below preceding OFF (-0.17, -0.59, -0.12, -0.27 ms; mean about -0.29 ms).
  GI total and other passes within OFF spread. Camp was confounded by scene drift:
  all passes moved about 7 ms between pairs; not used.
  Qualified as exact (100% bit-equal offline), with no regression. Gain about 0.3 ms
  on inn GI trace, not separated from OFF repeat spread (0.43-0.51 ms).
- Every final ON and current OFF output matches baseline: giRaw 10,926,080/10,926,080,
  giRawDepth 2,731,520/2,731,520 components; raw counters 32,768/32,768. All 100% bit-equal.
  Packs use their recorded defines: S8 none, camp/inn RT_CASCADE_MASKS=1.
  Older camp/inn trials reused S8's module; confounded and superseded by these final runs.
- Rejected projection-matrix reload: registers 255 -> 247, but S8 changed 50 output
  components and 49 counter bins. Removed; no precision relaxation.
- OFF release equivalence against 79dc9c17: 214/214 unique requests (188 identical, 26 equivalent),
  2,120 specialization assignments, negative control PASS. Only 28 uncached entries compiled;
  132 entries reused preprocessed-text cache matches. Evidence: .scratch/gi-livestate/*final*.
- Promotion: scripts/build.ps1 -Test PASS, 184/184 CTests in 189.60 s, BelowNormal; no game or queue.
- Check offline with tools/frame_replay.md (`--spec 1029=0/1`); developer A/B at the inn
  uses `dev=1` and raw `rt_compact_hit_state=0/1` in the same run.
  Default-OFF requests retain release equivalence; default spec value is now 1.
  Promotion changes no shader requests (delta 0). Merge-green selects full scope for
  settings/native changes: build/CTest, runtime shader compilation and QA detectors;
  no separate identity/release-equivalence job is registered. Heavy DXC deferred to it.

## Far GI live state (branch farregs, rejected, 2026-11-05)
- Offline only. No game, queue, git writes or settings-lane edits. SpecId 1039 retired.
- Production Ultra sun/emission request, native/hit-inst/cascade/far; 1029=1, 1038=1:
  main near/far 168/243 registers. Roof-before-shading 240; plus fade rematerialization 239;
  plus lane-owned shared far stats 218 (2816 extra shared bytes). No requested occupancy step.
- Roof test retained one veto bit instead of the ray origin across hidden lighting; cheap base/far
  fades rematerialized at use. Far stats used lane-owned uint words with the original wave/group sums.
  Same query rays, FP32 math and radiance accumulation order; no approximate shading or fp16.
- Sun-trust S8/camp/inn, radius 6, two rays: each candidate has 40 ABBA/BAAB pairs per pack
  at rt_stats=0 and 1 (480 total pairs). Every stored output byte and raw counter bin matched.
- Roof/fade ON-OFF ms, 95% median intervals (S8 / camp / inn):
  stats 0: +0.439400 [-0.312720,+1.328608] / -0.176096 [-0.547168,+0.132832] /
  +0.353320 [+0.138256,+0.608576].
  stats 1: +0.437216 [-0.692576,+1.184160] / +0.398328 [-1.223680,+1.360672] /
  +0.001008 [-0.396512,+0.845216].
- Shared-stats ON-OFF ms (same pack order):
  stats 0: +0.958704 [+0.634592,+1.955568] / +0.131816 [-0.058944,+0.956672] /
  +0.093280 [-0.173152,+0.250624].
  stats 1: +0.667000 [+0.009856,+2.411616] / +0.052376 [-1.687568,+0.513840] /
  -0.537464 [-1.212224,+0.103904].
- Rejected: neither reaches 168/128 registers or a consistent timing win; inn first and S8 second lose.
  All candidate shader code removed; no developer setting ships. Request delta 0; ABI unchanged.
- Final release equivalence vs main: 215/215 byte-identical, negative control PASS.
- BelowNormal build.ps1 -Test PASS: 193/193 CTests (build 53 s, tests 164 s).
- Receipts: worktree .scratch/farregs/{summary.json,regs-*.log,main-far*.log,*stats*.log,
  release-equivalence.json,build.log}; candidate source/SPIR-V and paired CSVs retained there.

## Native DLSS 5 phase 2b (branch dlss5, READY_FOR_LIVE candidate, 2026-11-05)

- Added layer/dlss5.cpp/.h and a fixed codec/guide shader. Same command buffer:
  successful pre-post RR -> encode/guides -> NGX feature 18 -> decode g_z -> rrpost
  temporal/final -> FG. Linear pre-exposed HDR; no D3D12, split or host ABI change.
- Off in every preset. Experimental page dlss5_nr; RR/pre-post required, restart
  to enable. Browse field dlss5_nr_runtime in both UIs; live dlss5_nr_bypass for A/B.
- Original NGX owner preserved with FG 2+ and FG Off. Staged bin/sl path prepended
  to forwarded Init; feature created lazily, capability checked, resized/reset as
  needed. Missing inputs/debug/unsupported recording skip; failure disables session.
- Eight immutable recording slots retire using actual submitted queue completion.
  CPU tests include boundary, guide scale/subrect/jitter/reset and layout commit
  versus discarded/failed submissions, plus EXT/core BDA-chain policy. GPU reports
  NR+codec with separate intervals.
- Launcher: game-bin then user path, signed preferred; version/SHA-256/offline trust
  logged and UI warnings shown. Only changed bytes copied/verified into bin/sl.
  No real NR binary was staged in this work. NR is rejected by package guards/tests.
- NGX core pinned; armed Shutdown calls are explicit no-ops (known fixture stall).
  Device drain releases NR; abrupt exit performs no late GPU destructor work.
  Device recreation disables NR until restart. No on-disk NVIDIA changes or spoof.
- Offline: final BelowNormal build.ps1 -Test passed 189/189 (265.66 s CTest,
  11 s build): .scratch/dlss5-phase2b-last.log. No test failures remain.
- Shader manifest delta 0; one bare shader / three fixed entries compile + spirv-val.
  All 217 existing requests byte-identical to f8942a23 by equal inputs.
- Staged job: tools/live_batches_staged/dlss5/jobs/dlss5-s8-ultra-native.json.
  S8 Water Queens House, real Ultra/FG 2, OFF/ON/OFF/ON: 600 measured + 30 settling
  frames each, then PNG pair. Per-window NR cost/FG 2.00, no NR skip/failure/device
  loss; structural and synthetic gates pass. Not queued/run; fresh authorization needed.
- Prior standalone signed 310.8.0: NR median/p90 6.520/7.147 ms at 3840 x 1600;
  codec total 6.785/7.379 ms. In-game cost and visible quality remain unmeasured.
  Custom 310.8.3 is app-accepted but core-rejected (0xbad0000b). Internal NR VUID
  09600 remains unresolved; no validation suppression or quality pass is claimed.
- No game launch, queue write, git write, preset promotion or common/host_api.h change.

## NR render-size feature retirement (branch dlss5-res, offline verified, 2026-11-05)

- Fixed immutable feature and recording-image keys: render/output sizes, guide
  extents/subrects/scales/convention, source formats, HDR and depth inversion.
  A changed key stops NR, polls existing submitted-work completion and waits for
  recording discard, releases the feature before its images, recreates and resets.
  No device-wide per-frame stall. Successful replacement logs both render sizes.
- Signed 310.8.0 Vulkan fixture: raw and layer-equivalent converted guide modes,
  reuse/recreate, 12 frames each: finite changed output, API success 0x1.
  Converted recreate also completed 48 frames / four creates. Crash not reproduced.
- Validation: only the known NGX-internal VUID 09600, once per feature
  (reuse 1, recreate 4); zero other errors. Exit 1 preserves validation failure.
  NR median 7.118 ms in the 48-frame diagnostic, not an in-game cost.
- CPU recreate test covers every immutable key field and ignores per-frame
  jitter/reset. Shader manifest delta 0. No git writes, game launch or queue use.
- `scripts/build.ps1 -Test`: 189/189 pass, build 21 s, CTest 266.03 s.
  Visual Studio environment was preloaded to avoid vcvars stderr aborting setup.
- Evidence: `.scratch/dlss5-res-*.log`; design/details in TRACK_DLSS5 section 7.
  Installed previous log read was denied. User-report chronology remains supplied evidence.
- In-game follow-up: after coordinator build/restart, Quality -> Balanced ->
  Performance -> Quality should log NR recreation, reset history and retain RR
  while old recordings retire. No in-game qualification was requested or run.

## Game upscaler guard: BG3RTX.exe puts the game's DLSS setting back (branch claude/confident-diffie-fb694f, READY_FOR_LIVE, 2026-11-05)

- Incident: after the RenoDX DLSS5 ReShade addon was installed, two player sessions logged
  `[ngx] no DLSS feature has been created through _nvngx.dll`. Cause confirmed offline: the game's
  `graphicSettings.lsx` (10:02) had lost `ResolutionUpscaleType` and `DLSS2Mode`; the 08:28 backup had 3 (DLSS)
  and 2. BG3 rewrites the file from defaults after a start where DLSS failed (the addon's first launches exited
  after 2-5 s), and every later start then runs with no upscaler.
- Not the cause: the game-folder `nvngx_dlss*.dll`. With fg=2 the game's NGX Init is stubbed (fg_game_ngx=2) and
  Streamline's NGX loads our `bin\sl` snippets (ReShade.log lists `bin\sl\nvngx_dlss.dll` and `nvngx_dlssd.dll`).
  The game-folder copies are NVIDIA-signed 310.9.16. A path-list hook of the game's NGX Init was not built: it
  matters only with fg=0, and the driver's Init exports take the static client's undocumented arguments.
- Fix: `game_dlss_guard` (bg3rtx.ini, default 1 since the user go of 2026-11-05; same rule as ensure-settings.py). With it at 1 and rr=1, BG3RTX.exe sets
  `ResolutionUpscaleType=3` before every game start, and `DLSS2Mode=2` only when absent (the player's DLSS mode is
  kept). It backs the file up to `graphicSettings.lsx.bg3rtx-bak` first, like `scripts/ensure-settings.py`.
  Launcher only: no layer, core, shader or ABI change.
- Tests: `launcher_launch_test` covers the reset file, idempotence, an FSR file, CRLF, an unknown file, and an
  end-to-end run on a temporary LOCALAPPDATA (off by default, rr=0, repair with backup).
- Live (not run, needs the user's go): start BG3RTX.exe with `game_dlss_guard=1` on a copy of the reset
  graphicSettings.lsx. Gate: the game creates DLSS (`[ngx] upscaler for feature ...: Ray Reconstruction`), no
  `no DLSS feature` line. Then decide whether the guard goes on in the presets.

## Ultra GI attribution batch (abandoned after the S8 result, 2026-11-05)

- `tools/live_batches/opt_r6/attr`: inn and S8, one launch each, Ultra dev profile.
  Windows: base-a, near-only (gi_far=0, sun on), base-b, sun-off (gi_hit_sun=0), base-c;
  480 frames each, both variants prewarmed first. Diagnostic only: image changes allowed.
- analyze.py reports variant minus the mean of the adjacent baselines, and baseline drift.
- First S8 result (VALID, opt-attr-s8.1.173918): GI trace 8.0 -> 2.1 ms with sun-off
  (which also removes gi_far), -> 1.5 ms with direct-off. Sun and far are confounded;
  the near-only window separates them.
- Readiness fixes: cached GI variants need only a fresh QUIET after the settings
  acknowledgement; at the inn QUIET came while 3 GI builds were pending, so the ready
  phases now wait for GI idle or burst completion after the acknowledgement.
- OMM, AO and direct-off windows dropped to keep each launch under 5 minutes.
- Lint frozen and non-frozen pass; attr test 5/5.
- Abandoned: the inn never passed readiness; the batch, its test and the GI-idle readiness change
  were removed (readiness back to eb935214). The far/sun split continues offline in frame_replay.

## Hair cascade consistency (branch claude-hair-cascade, NEEDS_IMPLEMENTATION, 2026-11-04)
- Ultra uses RT sun shadows and cascade casters (`common/settings.h:173`); current-native mode stays off (`:645`).
- Skin/neck/shoulders use the same receiver trust as other materials; skin transmission is traced too
  (`shaders/gi/rt_shadow.hlsl:87`). Groom-live status does not override trust.
- At strength 1, trusted traced pixels use strand visibility; roof pixels retain native mask * RT visibility,
  off-screen trust blends that fold with RT, and unknown rays retain the native mask exactly
  (`rt_shadow.hlsl:195`, `rt_sun_resolve.hlsl:38`, `core/sun_trust.cpp:178`).
- Missing TLAS/trust/dispatch/resolve also prevents full replacement (`core/gi.cpp:2961`, `:3111`).
- Cascade capture adds lagged off-camera casters (`layer/rtscene.cpp:1005`); it does not remove native draws.
- Camera cards get kInfoGroomReplaced (`layer/bvhsplit.cpp:2885`); native cascade records lack that identity
  (`layer/host.h:615`). Index-range keys and live membership are separate (`layer/hair_primary.cpp:527`, `:955`).
- A rejected: removing a shared cascade draw also removes its shadow on fallback receivers. Draw-time
  groom membership cannot certify later per-pixel roof, off-screen, unknown-ray or resource fallback.
- B not implemented: the native R8 mask has no separate hair term. Replacing it wholesale would also discard
  unknown non-hair occluders; the native-domain path needs an epoch-matched cascade-card/groom join first.
- No key, CPU skip decision, counter, shader change or variant request added (request delta 0).
- Required future live gate: every ON window has affected receivers/skips > 0; PNG skin/neck under hair
  neither black nor unshadowed; preserve shadows where RT falls back. No live or GPU cost measured.
- Validation: build passed; 149/150 CTests passed, water shader gate failed opening shared SQLite cache;
  that gate passed on retry with a local read-only cache snapshot (1.05 s). No commit or game launch.
