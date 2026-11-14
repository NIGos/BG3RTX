# BG3RTX progress - 2026-11-11 (main 4e5e435d)

Live tests are PAUSED by the user (2026-11-11, flag `.scratch/coord/PAUSE`). No live without a fresh explicit go.
State and next steps: [HANDOFF_SESSION](HANDOFF_SESSION.md). RTX Hair design and history: [RTX_HAIR_PRIMARY](RTX_HAIR_PRIMARY.md) 5v-6f.
Full text of every entry below (about 2900 lines): [archive](archive/PROGRESS_CODEX_2026-11-11.md).

## VRAM guard no longer latches LOW after one budget drop (branch claude/vram-guard-latch, READY_FOR_LIVE, 2026-11-14)

- Bug confirmed in the user's `bg3rtx.log` of 2026-11-13: budget 31419 MB at 13:41:10, 15477 MB at 13:41:15
  (used 13256 MB), 13599 and 13522 MB at 13:41:41. The guard kept the largest drop of the session (15942 MB)
  as slack: margin 16454, then 17352 and 17672 MB, more than the budget. Leaving LOW needed
  used + 2 x margin <= budget (32908 MB of margin against a 31419 MB card): impossible for the rest of
  the session, and `allow()` refused every growth above 31419 - 15942 = 15477 MB.
- While LOW the layer asked the yielders at every sample (16 frames): empty BLAS chunks freed, the
  hit-material recycle pool cleared, LRU meshes evicted, new chunks refused (draws left out of the RT scene).
  The log ends 27 s after the drop (52 Streamline "Frame rate over 100 ms" lines, DLSS-G eWarnOutOfVRAM), so it
  has no evictions per minute and no budget recovery: the rate comes from the code (one ask per 16 frames).
- Fix (`common/vram_guard.h`, `layer/layer.cpp` vram_guard_tick), always on with `rt_vram_guard=1`:
  - The observed drop is the largest single-sample drop of this and the last stats window (`rt_timing_frames`,
    300 frames each; the same two-window memory as the largest grow step), and it is void at once when the
    budget is back at the level before the drop.
  - Slack floor: 2.5% of the budget (unchanged, the 789 of 31419 MB evidence). Slack cap: what the yielders can
    give back at that budget = caps of BLAS chunks + mesh pool + recycled pool (6783 MB = 21.6% on the 32 GB
    reference, 3340 MB at 15477 MB): a reserve beyond what the guard could free protects nothing.
  - One trim ask per LOW episode for the whole need; a new ask only when the need is worse than the one
    acted on by one BLAS chunk (64 MB, the smallest amount a yielder returns). An unmet ask is kept until its
    consumer takes it. bvhsplit's demand-driven cache share eviction is unchanged.
  - New always-on lines, one per transition: `[vram] OS VRAM BUDGET TOO LOW: ... grants this process X MB, it
    uses Y MB (budget peak ..., heaps ...)` when used > budget, and `[vram] OS VRAM budget covers this process
    again` when LOW is left.
- Replay of the logged sequence (unit test): margin at 15477 MB is 3852 MB (was 16454); LOW with one ask,
  a second ask at 13599 MB (used 14747, starved), none at 13522 MB; back to "growth allowed" at the first
  sample with 31419 MB; no refusal at full budget from 14 to 28 GB used; still LOW when used + margin
  exceeds the budget. Steady 13522 MB budget with 14.7 GB wanted, 4000 samples: 1 ask, 1 log transition
  (before: 4000 asks). With the budget staying low the drop leaves the margin after two windows.
- Tests: `vram_guard_test` (new checks for the replay, decay, cap, no-thrash); `.\scripts\build.ps1 -Test`.
- Cost: none (CPU policy on the 16-frame sample). No new key, no shader or ABI change, variants delta 0.
- Not changed: required RT capture growth (`allow_capture`, real budget), the per-consumer caps.
- Open question for the user: the budget halved while the game ran (31419 -> 15477 MB). Windows does that to a
  process whose window is not in the foreground, or another program took VRAM; the new log line names it.
- Live check (any save, normal play): alt-tab away and back, or start a VRAM-heavy program. Expect one
  `headroom LOW` line with a margin of a few GB, at most one `evicted ... LRU meshes` line per episode,
  `OS VRAM BUDGET TOO LOW` only if used > budget, and `headroom back, growth allowed` within one sample
  (16 frames) of the budget returning; no `refused` lines at 31419 MB with used below about 28 GB.

## Launcher UI rework, Direction A with instant apply (branch claude/ui-rework, ready for review, 2026-11-14)

- User decision 2026-11-14: Direction A; a setting applies when clicked (no Save, no review, no unsaved state);
  dark only; Low-latency scene on the Experimental page. Audit, proposal, mockups: `docs/ui_rework/`.
- Phase 1, layout: sidebar with Play and version, preset in the header, setting rows in groups with indented
  children (RTX Hair with Detail at distance and Motion stability; Tracing speed collapsed), detail panel
  (inline below 1100 px), one amber restart badge with its direction, cost chip, Experimental page with its
  switch, Home reduced to attention plus preparation, Help regrouped with the version.
- Phase 2, instant apply: every change is written at once through the save hook (`Bridge::commit`).
  Dependencies resolve at the click (settings.h logic unchanged); the reply names what else changed, shown in
  the clicked row and in the status strip with Undo (one write). A dependent lowered with its prerequisite
  returns with it in the same session only (the file has no memory). A failed write changes nothing and offers
  Retry. Restart indicator by name, rows carry "Restart pending". Play only launches; a file that lacks a
  prerequisite or startup key is completed first and named. Removed: save, confirm, cancel, discard,
  `dirty()`, the review dialog, the close prompt.
- Phase 3, copy: one benefit line per setting without numbers, cost in the `Cost` struct, the rest in the
  detail text; "Off" of Detail at distance is "Full"; "(experimental)" dropped from the DLSS 5 label; the
  bridge sends what each preset sets. Ids, keys and preset values unchanged; defaults check clean; schema
  regenerated (`bin/settings_test.exe --write-schema`). `docs/USER_SETTINGS.md` rewritten for instant apply.
- Phase 4, keyboard and screen readers: one Tab stop per group of choices, arrows move without writing,
  Up/Down in the sidebar, Ctrl+Z undoes, the status strip stays a live region while empty, switches have
  `role="switch"`, child rows keep their full name for screen readers.
- Behaviour changes to know: an option whose prerequisite cannot be met is refused at the click (it used to be
  accepted and blocked at Save); `--no-game` review mode shows a write error on a click; sliders write on
  release; the basic fallback window (`ui_product.h`) is unchanged and keeps its own Save.
- Tests: `launcher_web_bridge_test` (instant apply, dependency apply and undo, remembered dependents, failed
  write and retry, restart names, opt-in off), `launcher_ui_state_test`, `tests/launcher_ui_test.js` (shell,
  rows, notice, undo, retry, restart pending, keyboard, sample data against the table), JSON test (copy
  contract). Full `build.ps1 -Test`: 226 of 226 (2026-11-14).
- Screenshots of the real UI with sample data (headless Edge): `docs/ui_rework/shots_impl/` (17 files).
- Not done, not verified: nothing was run in the real WebView2 window or with the game (no live); the page
  was rendered in headless Edge only. Measured GPU cost is still missing for most settings (chip only where
  the table has a number). The sample after a copy change: `py -3 tools/launcher/sync_ui_sample.py <exe>`.
- How to check: open `tools/launcher/ui/index.html?sample&chrome=1` in a browser (`&view=notice`, `restart`,
  `error`, `missing`, `hairerror`; `&experimental` shows the opt-in options). Not merged.

## Scene handout made ready to qualify and to try by hand (branch claude/handout-ready, READY_FOR_LIVE, 2026-11-14)
- Why: every frame waits for this frame's RT scene (`[rt] scene wait` mean 7.9-9.6 ms at the inn on Ultra, 3.2-5.6 ms at
  S8, main 6f6cef07). The fix (scene handout, increments 1-5, merged 2026-11-09) was never qualified or turned on.
- State: all increments complete. Keys `rt_scene_handout_layout` (restart) and `rt_scene_handout` 0 / 1 / 2. Mode 2 runs
  with native hit materials, OMM and water; the ini comment "not with rt_hit_material" was stale and is corrected.
- It is not a one-frame-old scene: in order the lighting traces this frame's scene. Only a fallback frame (build not in
  the submit, failed, dropped) traces the previous scene, as a wait timeout does today. Counters on the
  `[rt] scene handout` line.
- Earlier live (sh2, S8 Ultra, 2026-11-09, logs pruned): wait 4.4-5.1 -> 0.47-0.69 ms, 0 fallbacks, rendered median
  24.6-25.2 -> 24.2-24.6 ms (GPU-bound). The inn, High and everything after 2026-11-09 are unmeasured.
- Compatibility (code read, docs/SCENE_HANDOUT_DESIGN.md 8.4): RTX Hair strands, groom TLAS, pixel LOD camera TLAS,
  wide split, OMM dyn, water, hit materials work; a build the VRAM guard refuses ends as the `build failed` fallback.
  No file of the handout changed since increment 5 except the build-camera accessors. No code fix was needed.
- Launcher: Performance, "Low-latency scene" (`scene_handout`) Off / On. On writes both keys, needs a game restart
  (Off is live), is marked Experimental (shown with "Show Experimental"), Off in every preset.
  `key_change_needs_restart` knows `rt_scene_handout_layout`. Schema regenerated.
- Staged, not run: `tools/live_batches_staged/handout_qual/` (predicted.json first): hq-s8-ultra, hq-inn-ultra,
  hq-s8-high, hq-inn-high, hq-smoke-s8; 0 / 2 / 0 inside each run, focused, FG 3x, uncapped; static and pan windows,
  RTX Hair pan windows, 3 float traces per state for energy, primary-ray check in motion at S8. Thresholds are the
  OFF repeat of the same run. Lint live and frozen 0 failing; snapshot +1308 lines, append only.
- perf_ab's `retime` and `focused_job` take the batch folder (one helper, no copy); its jobs are unchanged.
- Offline: build and 225 / 225 CTests after 14:18 (scene_handout_test, scene_handout_gpu_fixture, frame_replay_test
  included). No shader, ABI or variants change (variants.txt delta 0), so key-off SPIR-V is main's.
- Not done: a scripted character walk (the plan runner has static, pan and rotate only); the primary-ray match in
  motion and the fallback counter stand in for it.
- Cost: no GPU work added (one 8-byte stamp load per query, under 0.01 ms expected; the batch measures it).
- User check: launcher, Show Experimental, Performance, Low-latency scene On, restart the game; the log then has
  `[rt] scene handout layout on` and `handed out N (0 ms)` on the scene wait line.

## Launcher review: RTX Hair Off sticks, version from the package, save flow (branch claude/launcher-review, offline, 2026-11-11)

- RTX Hair could not be turned off (0.9.6-hairtest8). Cause: the page saves with policy "enable" and the experimental
  opt-in "Strand hair edge resolve" (Analytic coverage) needs RTX Hair On, so the review of Save and Play raised RTX
  Hair again (`common/settings.h` preview_dependencies, called from `tools/launcher/web_bridge.cpp` save_preview).
  Fix: a knob the player turned down since the last save stays down; what needs it is lowered and listed in the
  review (`lower_dependents`). Needs already unmet in the saved file keep the enable policy.
  Test: `launcher_web_bridge_test` loads the player's file (`tests/fixtures/launcher/user_hairtest8.ini`) over
  bg3rtx.ini + release.ini and runs set / save / play; 8 checks fail without the fix.
- Version: the window showed the constant 0.9.5.0. It now shows line 1 of the package's `VERSION.txt`
  (`tools/launcher/version.h` display_version); a developer tree shows "0.9.6.0 (developer build)". Tests: bridge
  test and `tests/launcher_version_test.py`.
- Save opened the review dialog every time, also with "No settings change". It now writes at once when there is
  nothing to review (as Play does). "Save" in the close prompt used the other dependency policy and left the
  window open; it now sends the Save button's request and closes once saved. Behaviour change: tell me to revert.
- Hair conversion (`tools/launcher/preparation.cpp`): a style captured while the converter ran waited for the next
  capture or Start; choosing granny2.dll again did not start a saved "Convert all styles"; a folder ending in a
  backslash broke the converter command line (test added).
- Reports: bg3rtx.ini, bg3rtx_user.ini and VERSION.txt older than 24 hours made every report "Partial" (exit 1). The
  staleness note is for logs only now (test added). The game upscaler guard no longer reports a fix it did not write.
- Debt: the page's sample data matches the table again (eight descriptions, RTX Hair marked "restart") and the JSON
  test compares it with the bridge; the user file heading "Hardware limits" names what is under it (hotkeys, DLSS 5
  runtime file); stale comments (Python converter, "C2 may register", "F9", "pair"); the native fallback counts
  every hotkey in its unsaved count.
- Key cross-check: every knob, bg3rtx.ini and release.ini key is read by layer/core/common (launcher-only keys excepted).
- No preset value, profile rule or knob table entry changed. No ABI or shader change (variants delta 0).
Decisions (user, 2026-11-11):
1. Save with nothing to review writes at once: kept.
2. dev=1: a saved RTX Hair part knob (the exp_hair_* ones the On bundle also writes) wins over the bundle. Done in
   `render_user_ini(..., dev, loaded)` (`common/settings.h`): a part changed since the last load or save, or already
   set in the file, is saved and its key written once; a part left alone while RTX Hair is turned On stays under
   the bundle. Release and non-dev are as before (bundle saved whole). Tests: `settings_test`, `launcher_web_bridge_test`.
3. bg3rtx.prev.log (and tagged .prev.log files) never makes a report "Partial": it is the last session's log by
   design. Other old logs still do. Test: `launcher_diagnostics_test`.
4. Native fallback kept; its Overview page says RTX Hair preparation and its shortcut are only in the full window.

Still open, not changed:
- The hair card trusts a stale preparation.json: with the game running and "Also while playing" off, a new capture
  still reads "All needed styles are done".
- Report redaction misses a game library path outside the Users folder and a bare user name.

## Performance A/B batch staged; release profile 2026-11-05 vs 2026-11-14 (branch claude/perf-ab, READY_FOR_LIVE, 2026-11-14)

- The user reports a large frame-rate loss in play with Ultra, FG 3x, RTX Hair On, distant detail 1/4,
  `rt_hair_mv_card=2`, `rr_hair_strand_aa=2`, sometimes DLSS 5 NR. Staged, not queued:
  `tools/live_batches_staged/perf_ab/` (generator, 5 jobs and plans, `predicted.json`, `analyze.py`, README).
- Release profile rerun today (`opt-profile-*.1.13*.log`, static windows, GPU ms) against the 2026-11-05
  "after" runs. The base path is not slower, except scene build at S8 and at the inn on High.

| Scene | Preset | Scene build | GI | Local shadows | Reflections | RR |
|---|---|---|---|---|---|---|
| Water Queens House | Ultra | 1.18 -> 1.84 | 12.10 -> 8.20 | 0.59 -> 0.79 | 1.10 -> 1.10 | 2.52 -> 2.42 |
| Water Queens House | High | 1.27 -> 1.69 | 2.24 -> 1.82 | 0.12 -> 0.17 | 0.44 -> 0.40 | 2.48 -> 2.80 |
| Inn | Ultra | 2.49 -> 2.59 | 21.18 -> 15.99 | 2.00 -> 1.97 | 2.11 -> 1.98 | 2.54 -> 2.76 |
| Inn | High | 2.14 -> 2.66 | 3.07 -> 2.66 | 0.87 -> 0.86 | 0.58 -> 0.56 | 2.57 -> 2.74 |
| Dialogue1 | Ultra | 1.76 -> 1.69 | 14.12 -> 10.94 | 1.25 -> 1.22 | 1.18 -> 1.05 | 2.54 -> 2.44 |
| Dialogue1 | High | 1.68 -> 1.70 | 2.80 -> 2.63 | 0.83 -> 0.77 | 0.36 -> 0.34 | 2.43 -> 2.51 |

- Camp could not run (its save "Campsite - 27h" no longer exists). Separate runs: under about 0.3 ms per pass is
  noise. Both were unfocused at the 30 fps cap, stock hair, FG 2x: no word on frame time or the hair configuration.
- Today's logs show the hold path copying a lot per 120 builds in the static windows: S8 Ultra hold pack 0 jobs
  and copy 29640 jobs (10.9 GB), S8 High pack 1200 and copy 38764 (14.7 GB), inn Ultra pack 120 and copy 63985
  (17.1 GB). No 2026-11-05 log is left to compare; job C measures it against the split off.
- Jobs A and B (`perf-ab-s8`, `perf-ab-inn`): one focused, uncapped launch each (`fps_cap=0`: the 58 fps cap
  would hide costs), static camera, 6 windows of 60 settle + 360 frames with a stats reset: stock cards / RTX
  Hair On / + distant detail 1/4 / without shadow and AO transmittance / strands in lighting only
  (`rt_hair_groom_primary=0`) / stock cards again (noise floor). All toggles are live because the game starts
  with the bundle, `rt_hair_mv_card=2` and the nearest trace; the origin reservation and the 1049 velocity
  pipelines stay in the off windows.
- Skipped: the bundle with `rt_hair_groom=0` (the groom key gates the strand swaps, `layer/util.cpp`: it would
  show cards). The lighting-only window is its valid complement.
- Job C (`perf-ab-wide-split-0`, `-1`): the opt-profile-s8 state without pans, `rt_capture_wide_split` 0 and 1
  (startup key, two launches): scene build, `[bvh]` hold pack and copy jobs, mesh pool MB.
- Job D (`perf-ab-dlss5-s8`, optional): `dlss5_nr=1` at start, `dlss5_nr_bypass` 1 / 0 / 1; valid only if the
  launcher has already staged `sl/nvngx_dlssnr.dll` for the test tree, else it fails fast on `NR created`.
- Analyzer: `<log>.perf.md`, one table per job (median and range of every GPU cost line, scene wait, `[pace]`
  median and p99, CPU submit, VRAM, guard lines, off-b minus off-a as noise). Checked on today's logs.
- Gates: live_flow preflight (`--tree .`, `--frozen`) 5 jobs, 0 failing; lint snapshot extended (append only);
  batch_jobs_lint, live_flow, opt_profile, test_registration and the merge_scope tooling step pass.
- Expected time after load: s8 135 s, inn 252 s by the lint model (about 125 s at 25 fps), wide split 47 s each,
  dlss5 74 s. No code change, no live run. After the go: read each `<log>.perf.md` against `predicted.json`.

## Layer hook CPU since 2026-11-05: no base regression; lock-free leave for the strand swap hook (branch claude/cpu-hooks, READY_FOR_LIVE, 2026-11-14)

- Where 12.4 came from: `[cpu] capture hooks ... total` minus `driver draw (XFB)` ("hook bodies"), the OFF windows of
  `opt-cpu1-inn.1.025350` (12.39 / 12.38; kept in `tools/live_batches/opt_r3/cpu2/profile-cpu1.md`). Job: the
  opt-profile inn job, Ultra static, `visible_unfocused_fg` (30 fps), fg=2, dev=0, RTX Hair never started,
  `cpu_timing=1 detailed_logging=1`. The timer sums QPC scopes of every recording thread (not wall time).
  11.7 was never measured: it is 12.4 minus the three separately qualified receipts (0.13 + 0.30 + 0.24).
- The same job today (`opt-profile-inn.1.131746`, main 6f6cef07, 9 static periods): total 12.13 [12.00-12.25],
  hook bodies 11.91. Against the measured 12.39 that is -0.48; against the derived 11.7 it is +0.2, inside what
  summing three receipts with OFF spreads of 0.02 to 0.09 can give. Slots, 2026-11-05 cpu_opt1 OFF -> today:
  pre-draw 8.66 -> 8.57, pass after 2.59 -> 2.27, bookkeeping 0.526 -> 0.503, driver draw 0.292 -> 0.224,
  hit material prepass 1.57 -> 1.39. Draws today 6920 per frame, 3217 captured (not in the kept 2026-11-05 receipts).
- The 13.3-13.8 of `perf-ab-inn.1.135354` is the same timer under other conditions: focused, no cap (34 fps), fg=3,
  dev=1. Every scope is about 10% up, also those no hair code touches (lighting-thread material query 0.89 -> 1.04,
  driver alpha descriptor updates 0.51 -> 0.55 ms/frame), and off-a to off-b drifts 0.43 in the same run. Not a
  like-for-like regression. "CPU dispatch tracking pre" (13.0-13.5) is another quantity: the lighting thread's
  CmdDispatch hook including the core's on_dispatch_pre and its scene wait (8.6-9.0 ms); it must not be added.
- Code diff 2aa5dcf8 -> main of the per-draw paths (`layer/cmd.cpp`, `tracker.cpp`, `geocapture.cpp`,
  `hit_material.cpp`, `alphatest.cpp`, `bvhsplit.cpp` bvh_classify/bvh_captured): with RTX Hair off only stride
  arithmetic on globals (`rt_capture_wide_split`) and one bool (`hair_capture::enabled`); no new lock, map or atomic.
  Started with the bundle and switched off live: `hair_primary_wanted` (cmd.cpp:820) costs one frame_index compare
  and three loads per indexed draw, and is false two frames after the switch; grooms, slot tracking and hair capture
  read 0 through the `rt_hair` gate. Nothing to fix there.
- Found, with RTX Hair on: every indexed game draw (about 6900 per frame at the inn) entered `hair_primary_swap`,
  took the global `g_mu`, did up to six hash lookups, and in D16 depth-only passes also the tracker lock
  (`geo_cascade_layer`), to learn it is not hair. This sits outside every `[cpu]` scope, so no log line shows it.
- Fix (no key, behaviour identical): `hair_primary::DrawFilter` (common/hair_primary.h), two never-cleared bit sets
  written under `g_mu` before a pipeline enters `g_hair_pipes`/`g_variant`/`g_cascade_clone` or a draw key enters
  the published set or the depth latch. `hair_primary_swap` leaves lock-free when the pipeline and the key were never
  added; a set bit only sends the draw to the unchanged locked path.
- Offline: `hair_draw_filter_test` (400000 random steps: the filter never skips a draw the locked rule acts on) and
  its synthetic cost for 7000 non-hair draws (3 runs after 14:18, thread sum; a model, not a game saving): 1 thread
  29 -> 6.3 ns/draw (0.20 -> 0.04 ms/frame); 4 recording threads 164-171 -> 10-11 ns/draw (1.15-1.19 -> 0.07-0.08).
  `scripts/build.ps1 -Test`: 226/226 CTests pass (204 s, run 14:19-14:23, none blocked).
- No offline harness runs the real hook path (replay packs replay GPU passes, the cpu_opt tests are helper
  microbenchmarks), so 2aa5dcf8 vs main cannot be measured offline. The one live that would: the staged perf-ab inn
  job from a test tree at 2aa5dcf8 and at main, RTX Hair never started, same window mode; compare `capture hooks`.
- To confirm this fix live: perf-ab inn, hair on windows, old vs new layer (restart): expect lower `[rt] scene wait`
  and rendered frame time with strands on; `[cpu] capture hooks` must not change (the swap hook is outside it).
  Cost: GPU 0. Shader request delta 0, ABI unchanged, no new setting.

## claude/queue-corpus

- Bug: `scripts/test-queue.ps1` pruned every log older than 3 days at start. After 3 idle days the preflight
  saw `VALID corpus: 0 logs` and refused every job (`corpus: no readable VALID logs`), 2026-11-14.
- Fix 1: `tools/live_batches/keep_logs.py` (called by the runner) keeps the newest 3 VALID logs per save
  (same set as `live_flow.corpus()`) plus siblings sharing the log name or stem; the rest older than 3 days goes.
- Fix 2: `live_flow.py` with an empty live corpus prints `live corpus empty: using the frozen snapshot` and lints
  with `lint_corpus_snapshot.json` (as `--frozen`). A job failing the frozen lint is still refused. No runner flag.
- Tests: `QueueCorpusFallbackTests` in `tests/live_flow_test.py` (fallback equals `--frozen` result; keep-list).
- Preflight with empty corpus now exit 0 for all four opt_profile jobs and hf2-s8-hair-final.
- No live run; not merged.

## Newest: RTX Hair, 2026-11-10/11

### Motion stability ON in High and Ultra (user decision, branch claude/hair-mv-preset)
- Control "RTX Hair motion stability" = `rt_hair_mv_card=2`. On in High and Ultra, Off in Low and Medium; release.ini pin removed.
- Gates: hm1 210705 camera-only share 11% (off 98%), hair/ring ratio 0.77; hf2 000619 pan 3.2%, ratio 1.00. Strand G-buffer 0.22-0.31 ms.
- Turning it on needs a game restart (specialization 1049). User check: Ultra, S8, walking characters or turning camera: no hair smear.

### Detail at distance: pixel LOD (branches claude/hair-pixel-lod*, claude/hair-detail-knob)
- Control "RTX Hair detail at distance": Off / 1/4 / 1/8 (keys rt_hair_pixel_lod, rt_hair_pixel_lod_small_keep). Presets Off, applies live.
- Heads 35-60 px get keep 1/4 (1/8 optional, about +2% outline). Under 35 px unchanged. Above 60 px the k rule over-covers about +15% (open).
- pl1 211046: strand G-buffer -27% (1/4), -34% (1/8). Camera BLAS about 16 MB per groom at 1/4, never compacted (possible saving).
- Image gain not resolved by the shots (poses change between shots); no hole, no card fallback, frame energy unchanged.

### Holes under a fast camera dolly (branch claude/hair-holes-root, READY_FOR_LIVE)
- Cause 1 found and fixed behind `rt_hair_header_cam_gpu=1`: header camera was a CPU read, now copied on the GPU. hl1 000345: coverage 2-7% -> 50-70%.
- Remaining 30-50% loss is open. Disproved: another build's TLAS, row order, full BLAS rebuilds, mesh pool reuse, VP eye, stale publish
  (k=1 is normal; `rt_hair_stale_publish` cannot act, retire it), newer build (build stamp hl1 003637), BLAS refit, game LOD switch, VP mismatch as stated.
- Next: hl1 with `rt_hair_dense_sample=1` (spec 1052), staged in `tools/live_batches_staged/hair_late/`, predicted.json lists the signatures.
  The hair G-buffer shader is not byte-identical with the key off (spec-constant branch); merge-green shader gates passed.
- Other findings (6c/6d): misleading counters ("own build frame's publish", "header/BLAS mismatch" always 0 with `rt_hair_groom_primary_current=1`);
  rejected-after-hit is frame-wide; the CPU camera also feeds the cascade caster rebase and groom LOD bounds; `lint_snapshot` shrinks the snapshot (needs a merge mode);
  possible full build on compacted BLAS storage at the `layer/bvhsplit.cpp` refit fallback.
- hf2 final qualification job `tools/live_batches_staged/hair_final/` (hole gate report-only) does not yet set `rt_hair_header_cam_gpu`.

### Other 2026-11-10 hair work (one line each)
- hair-v3-settle: nearest-strand trace for a groom joining while others swap. hair-swap-transitions: switch counter, retry latch.
- hair-stale-publish / stale-camera: stale check built, found inert (see above). hair-slot-identity, hair-shared-key, hair-row-stable: stable slots, rows, one-key watchdog.
- hair-release-policy: RTX Hair On was inert in release, policy reset fixed. hair-mv / mv-burst: strand motion vectors (rt_hair_mv_card).
- hair-keys-default, hair-nearest-default, hair-bundle-upgrade, hair-audit: RTX Hair On bundle keys and upgrade path.
- hair-coverage-cause, hair-groom-coverage, hair-frame-miss, hair-cam-step, hair-arrival-cause, hair-late-arrival, hair-pool-reuse, hair-fg-job: hole investigation steps and diagnostics.

## Current user decisions

- Stock visible hair ships; strand hair hidden until its post-release update.
- Experimental page: only Water surface reflections (Fresnel), off.
- The game-derived asset database ships in the archive (user's own risk on data rights).
- GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification. "Tested only on RTX 5090".
- Nexus: setup in a ZIP (main file) plus portable ZIP; manual download, no Vortex.
- Lean: only the tests that are really needed; one final live on the user's go.
- 2026-11-11: RTX Hair motion stability is On in High and Ultra. Live tests paused ("non avviare altre live") until a fresh go.

Full folded branch notes: [2026-11-05](archive/PROGRESS_CODEX_2026-11-05.md), [2026-11-04](archive/PROGRESS_CODEX_2026-11-04.md).
- Optimisations that qualify are always on; no player option.

Full notes of the night: [archive](archive/PROGRESS_CODEX_2026-11-05-night.md); release 0.9 report: [archive](archive/PROGRESS_CODEX_2026-11-05.md).

## Older entries (2026-11-04 to 2026-11-09, one line each; full text in the archive)

- RTX Hair strand/card flicker while turning: header camera from the GPU (branch claude/hair-header-cam, READY_FOR_LIVE, 2026-11-09)
- rt_capture_wide_split on by default (branch claude/wide-split-on, 2026-11-09)
- Strand resolve finds its frame's snapshot under a moving present (branch claude/strand-resolve-fallback, offline, READY_FOR_LIVE, 2026-11-09)
- Strand resolve paired with its own frame's snapshot by submission order (branch claude/strand-pair, offline, READY_FOR_LIVE, 2026-11-09)
- Scene handout design (branch claude/scene-handout-design, design only, 2026-11-09)
- Scene handout increment 5: traversal flag (ABI 31) and water on the handout (branch claude/scene-handout-5, 2026-11-09)
- Scene handout increment 4b: native hit materials under the handout (branch claude/scene-handout-4b, READY_FOR_LIVE, 2026-11-09)
- Scene handout increment 4: OMM on reserved slots; hit materials analysed (branch claude/scene-handout-4, offline-verified, 2026-11-09)
- Scene handout increment 3: handout without the scene wait (branch claude/scene-handout-3, READY_FOR_LIVE, 2026-11-09)
- Scene handout increment 2: scene TLAS by stamp address (branch claude/scene-handout-2, offline-verified, 2026-11-09)
- Scene handout increment 1: update-after-bind set 1 layout (branch claude/scene-handout-1, offline-verified, 2026-11-09)
- RTX Hair switchable while playing, hotkey Ctrl+Shift+F7 (branch claude/rtx-hair-live, READY_FOR_LIVE, 2026-11-09)
- Release verification lives 2026-11-09: rise reserve on, capture limits found (branch claude/rise-reserve-on)
- Mesh cache budget charged with its own BLAS (branch claude/mesh-cache-rate, READY_FOR_LIVE, 2026-11-09)
- Mesh cache headroom for the frame's other users; no build fails on hair bounds (branch claude/mesh-cache-rate-2, READY_FOR_LIVE, 2026-11-09)
- Launcher UX: Home dashboard, RTX Hair conversion panel, Hotkeys page (branch claude/launcher-ux, offline, 2026-11-09)
- RTX Hair warm-up after a level load (branch claude/hair-warmup, READY_FOR_LIVE, 2026-11-09)
- rt_hair_warmup on by default (branch claude/hair-warmup-on, 2026-11-09)
- Static-camera strand flicker: diagnosis and staged burst job (branch claude/hair-static-flicker, READY_FOR_LIVE, 2026-11-09)
- Strand edge dithering: strand-aware dither resolve and analytic coverage (branch claude/hair-edge-quality, READY_FOR_LIVE, 2026-11-09)
- Convert all hair styles from the player's own files (branch claude/hair-convert-all, offline, 2026-11-09)
- Diagnostic report: opt-in latest save, Home link (branch claude/diag-save, ready for review, 2026-11-09)
- RT capture stride split: 88 bytes only for strand-frame shaders (branch claude/capture-stride, READY_FOR_LIVE, 2026-11-09)
- RT capture alpha floor yield, BLAS stall report persistence (branch claude/capture-fixes, 2026-11-09)
- Scene slot material records pinned against pruning (branch claude/blas-retire-stall, READY_FOR_LIVE, 2026-11-09)
- BLAS chunk cap by occupancy for oversized BLAS (branch claude/blas-frag, READY_FOR_LIVE, 2026-11-09)
- BLAS chunks capped for unguarded callers; retirement-stall report (branch claude/blas-chunk-growth, READY_FOR_LIVE, 2026-11-09)
- Background runs: the user's mouse no longer reaches the game (branch claude/bg-input, READY_FOR_LIVE, 2026-11-09)
- Release 0.9.5-beta: version bump and changelog (branch rel095, 2026-11-08)
- Packaged 2026-11-08 23:00
- Rebuilt 2026-11-09 (main b3cb21fa): RTX Hair visible in Lighting, launcher Home page
- RTX Hair single option, OMM dyn refusal churn, scene wait cause (branch claude/opt-waits, READY_FOR_LIVE, 2026-11-08)
- VRAM: capture reserve from the observed rise, BLAS chunk reuse (branch claude/opt-vram, READY_FOR_LIVE, 2026-11-08)
- SER / raygen GI trace on game frames (branch claude/opt-ser-gi, offline, 2026-11-08)
- Scene early handout: plan reviewed, not implemented; decision live prepared (branch claude/opt-scene-handout, READY_FOR_LIVE, 2026-11-08)
- Launcher: a selected option turns on what it needs (branch claude/opt-knob-deps, done offline, 2026-11-08)
- GPU cost breakdown, hair fibre timing, fibre compile time (branch claude/opt-gpu, offline, 2026-11-08)
- Runtime capture as the groom converter's source (branch claude/hair-runtime-capture, offline study, 2026-11-08)
- Strand fibre shading for shadow-mapped local lights (branch claude/hair-m4-locals, READY_FOR_LIVE, 2026-11-08)
- Strand hair layer path: review fixes (branch claude/hair-layer-fix, READY_FOR_LIVE, 2026-11-08)
- Strand groom storage, needed set and launcher Preparation card (branch claude/hair-groom-storage, offline study, 2026-11-08)
- Grooms v3, seen list, launcher Preparation (branch claude/hair-groom-storage, implemented offline, 2026-11-08)
- The flash while anything moves: per-frame tags, LOD keep, counter fix (branch claude/hair-flash, offline, off, 2026-11-08)
- Strand hair layer review round 3 (branch claude/hair-fix3-layer, READY_FOR_LIVE, 2026-11-08)
- Strand fibre passes now fire: strand-pixel depth test, sun mask (branch claude/hair-fix3-fibre, offline, READY_FOR_LIVE, 2026-11-08)
- Strand fibre shading: review fixes 7-11 (branch claude/hair-fibre-fix, offline, READY_FOR_LIVE, 2026-11-08)
- One-frame strand dropouts: counter and camera gate (branch claude/hair-dropout, offline, off, 2026-11-08)
- Native strand groom converter bg3rtx_hairconv (branch claude/hair-converter-native, offline, 2026-11-08)
- Layer hair capture writer and groom re-index (branch claude/hair-capture-writer, offline, 2026-11-08)
- Live e2e 2026-11-08 22:40 (hcap3, Test Dialogue1, scratch assetdb without groom 8b32681bb9ecfc30): both VALID
- Ray tracing pipeline GI trace (branches claude/gi-rt-pipeline, claude/gi-rt-pipeline-ultra; on by default, 2026-11-08)
- Hair smoke 2026-11-08 19:10 (Test Dialogue1, Strand hair on screen bundle, after the publish fix 782a6ad4): VALID
- Release 0.9.4-beta: version bump and changelog (branch rel094, offline, 2026-11-07)
- Release candidate 0.9.4-beta PASS (branch rc094, 2026-11-07)
- Packaging allows a checkout on a disk mounted as a folder (branch package-mount-points, verified offline, 2026-11-07)
- Launcher picks the RTX GPU when an integrated GPU is also present (branch launcher-default-gpu, verified offline, 2026-11-07)
- Temporal grain from the hair-ab7 bursts (branch claude/hair-temporal, offline, 2026-11-07)
- Strand swaps never started: prepass/G-buffer card-depth deadlock (branch claude/hair-swap-fix, READY_FOR_LIVE, 2026-11-07)
- Strand flicker and lag: coverage hold, this frame's publish (branch claude/hair-stability, READY_FOR_LIVE, 2026-11-07)
- Strand quality: footprint coverage bound, pair gate bias, item 4 design (branch claude/hair-quality, READY_FOR_LIVE, 2026-11-07)
- Hair M5 participation, M3.4 post depth, M2.2 cascade cards (branch claude/hair-m5-m2, READY_FOR_LIVE, 2026-11-07)
- Hair M3.2 sub-pixel coverage and M3.3 RR hair guides (branch claude/hair-m3-image, READY_FOR_LIVE, 2026-11-07)
- Strand AO transmittance and fibre specular indirect (branch claude/hair-m2-m4-light, READY_FOR_LIVE, 2026-11-07)
- Hair groom: 2-D curve tracer for curly atlases (branch claude/hair-m1-curly, READY_FOR_RERUN, 2026-11-07)
- Hair grooms never joined: BVH split ring failed in the BAR heap (branch claude/hair-join-fix, READY_FOR_LIVE, 2026-11-07)
- Strand grain attribution, card normal and world tangent keys (branch claude/hair-grain, offline, off, 2026-11-07)
- 5g live gates evaluated (branch claude/hair-grain-gates, 2026-11-07)
- build.ps1 prefers VS 2022 (branch build-pin-vs2022, 2026-11-07)
- Suspect review (branch suspects, FIXED_OFFLINE, 2026-11-06)
- S-F05 R11G11B10 boundaries (branch rrpost-r11, fixed offline, 2026-11-06)
- Default Ultra RF replay (branch rf-replay, READY_FOR_LIVE, 2026-11-06)
- Default Ultra RF follow-up (branch rf-opt2, NOT QUALIFIED, 2026-11-06)
- Default Ultra RF mismatch (branch rf-opt, READY_FOR_RECAPTURE, 2026-11-06)
- RF compact probe state (branch rf-on, qualified, 2026-11-06)
- Default Ultra receiver probe deferral (branch rf-default, NOT QUALIFIED, 2026-11-06)
- Launcher defect review (branch review-launcher, PASS offline, 2026-11-06)
- DLSS 5 offline defect review (branch review-dlss5, FIXED_OFFLINE, 2026-11-06)
- Release 0.9.3-beta (branch rel092b, 2026-11-06)
- RC receipt 0.9.3-beta (branch rc093, 2026-11-06)
- Queue cancellation, ReShade isolation and release-policy lint (branch queue-tooling, offline PASS with limits, 2026-11-06)
- L11/L12/L17 submission redo (branch layer-submit2, READY_FOR_LIVE, 2026-11-06)
- FG release audit, Round 3 (branch fgx3, offline verified, 2026-11-06)
- DLSS 5 Release audit (branch dlss5-audit, offline fixes tested, 2026-11-06)
- Core completion contract (branch core-contract2, READY_FOR_LIVE, 2026-11-06)
- Core completion contract audit (branch core-contract, ABI_BLOCKED, 2026-11-06)
- BLAS flags replay (branch blasflags, rejected, 2026-11-06)
- Exact GPU technique scan (branch techscan, analysis only, 2026-11-05)
- Sun wave rejected; far GI attribution (branch sunwave, offline, 2026-11-05)
- SER feasibility study (branch ser-study, FEASIBLE_OFFLINE, 2026-11-05)
- Ultra scene build audit (branch scene-ana, analysis complete, 2026-11-05)
- Early roof rejection restored (branch roofrej, prepared READY_FOR_LIVE, 2026-11-05)
- Exact early roof rejection (branch roof-on, kept, 2026-11-05)
- RF receiver probe live state (branch rf-ls-regs, READY_FOR_LIVE, 2026-11-05)
- Compact RF live batch (READY_FOR_LIVE, 2026-11-05)
- ReShade compatibility: Streamline device init after vkCreateDevice (branch reshade-compat, READY_FOR_LIVE, 2026-11-05)
- Release smoke capture windows (branch relsmoke-fix, kept offline, 2026-11-05)
- GI origin audit (branch origin-opt, rejected, 2026-11-05)
- Raw compressed LS replay (branch ls-pack2, offline verification, 2026-11-05)
- Production Ultra LS replay (branch ls-pack, READY_FOR_LIVE, 2026-11-05)
- Exact local-shadow optimisation (branch ls-opt2, OFFLINE QUALIFIED, 2026-11-05)
- Local-shadow replay feasibility (branch ls-opt, stopped at analysis, 2026-11-05)
- DLSS 5 page and keyboard shortcuts (branch hotkeys, offline verified, 2026-11-05)
- Conservative GI-only TLAS (branch gitlas, rejected, 2026-11-05)
- Compact GI hit state (branch gi-livestate, qualified always on, 2026-11-05)
- Far GI live state (branch farregs, rejected, 2026-11-05)
- Native DLSS 5 phase 2b (branch dlss5, READY_FOR_LIVE candidate, 2026-11-05)
- NR render-size feature retirement (branch dlss5-res, offline verified, 2026-11-05)
- Game upscaler guard: BG3RTX.exe puts the game's DLSS setting back (branch claude/confident-diffie-fb694f, READY_FOR_LIVE, 2026-11-05)
- Ultra GI attribution batch (abandoned after the S8 result, 2026-11-05)
- Hair cascade consistency (branch claude-hair-cascade, NEEDS_IMPLEMENTATION, 2026-11-04)


# Part 2: entries folded on 2026-11-14 evening (main d2cceda0), full text

The ten fragments of the afternoon and evening (RTX Hair stability, scene handout, sensible gates), as written by their
branches. The short form is in PROGRESS_CODEX.md.

## Low-latency scene On in High and Ultra (branch claude/handout-on, ready to merge, 2026-11-14)
- User decision "Si, accendilo": `scene_handout` preset row Off / Off / On / On; a normal Performance control (Frame rate
  group), no longer awaiting qualification, not an opt-in knob; no release pin involved. `bg3rtx.ini` carries the High keys.
- Evidence (handout_qual, focused, FG 3x, OFF/ON/OFF): inn Ultra 30.0 -> 26.5 ms (wait 8.5 -> 3.0), inn + RTX Hair
  31.1 -> 28.6, S8 Ultra 20.9 -> 19.9 (pan 20.8 -> 19.8), S8 + hair 22.1 -> 21.2, S8 High pan 12.1 -> 11.6; fallbacks 0,
  pairing mismatches 0 everywhere. Energy inn +0.11%, S8 -0.72% (OFF spread 647.8-656.6): OPEN, 3 traces per state.
- Release player: High and Ultra On, Low and Medium Off; a user file with only a preset line becomes On for High and
  Ultra at the next game start, untouched. Tests: settings_test `test_scene_handout`, launcher UI and bridge tests.
- Analyzer: per-pass costs (RT, GI, scene build) now "not higher beyond noise" (the first run failed when they fell).
  Pans are 3 periods (every window at least 2 complete periods); `hq-s8-energy` (8 traces per state) and `hq-s8-check`
  (primary-ray match, split to stay under 15 minutes) are staged, not queued. Lint 0 failing, live and frozen.
- Check in game: restart on High or Ultra; `[rt] scene handout` line with 0 fallbacks; turn it Off on the Performance
  page (live) to compare. Docs: SCENE_HANDOUT_DESIGN.md 8.5, USER_SETTINGS.md.

## Scene handout modes job: warm-up stop was a job fault; hx1 control gate relaxed (2026-11-14)

- `hq-s8-modes.1.204217` stopped in phase 1 of 19 (`producer-sanity-warmup`, 144 s): "producer silent" on the GI far
  and `[core] scene handout` lines. Job fault, not code. Both lines were in the any-line `expect`, so live_flow asked
  for them right after the OFF warm-up. There `rt_stats=0` (GI far is an rt_stats line) and `rt_scene_handout=0`
  (core/gi.cpp `handout_log` returns before printing under key 2). No handout or fix state was ever reached.
- So the run says nothing about `rt_scene_handout_hit_wait`: 0 `[hit material submit]` lines (rt_stats off), 0 core
  handout lines. The fix is still unverified live (READY_FOR_LIVE).
- What the warm-up shows (OFF, static S8, 23 periods of 120 frames): scene wait mean 3.60 ms, 120 of 120 waited,
  0 timeouts; GI trace GPU median 4.65 ms; rt passes 6.6 ms; GI 8.7 to 8.9 ms; rendered median 21.5 ms; layout on,
  dummy TLAS recorded. These match the OFF states of hq-s8-energy (4.83 / 4.69 ms GI trace).
- Job fixed (`handout_qual/make_jobs.py` modes): the two lines left `expect`; the runner gates are "the line exists"
  per state: GI far and scene wait in every hit window, `[rt] scene handout` and `[core] scene handout` in hit-m2 and
  hit-fix. Plan is 18 phases, 339 s estimated, one launch, five states, 8 float traces each.
- Analyzer (`handout_qual/analyze.py`): the state table gained the rendered frame time; `state_costs` now judges GI
  trace GPU ms ("not higher"), scene wait mean and frame time ("lower") of every state against the OFF pair with
  sane_gates; the deliverable deltas are always printed. tests/sane_gates_test.py covers it (8 tests pass).
- hx1 (`hair_stable/make_job.py`): the keep-off control windows (in-nokeep, out-nokeep) only require their switches
  line. Run `hx1-s8-hair-stable.1.202905` (16 of 16 phases) was INVALID only because in-nokeep required `other 0` and
  saw `coverage stale 2, other 1`. Replayed with the new gates: every-window gates ok (26).
- Card runs of entering heads in 202905 (frames on cards, min / median / max): keep on: in-on 3 / 3 / 9, out-on
  3 / 3 / 3, orbit-r 3 / 6 / 6, orbit-l 3 / 3 / 3, in-on2 3 / 3 / 3, out-on2 3 / 3 / 3. Keep off: in-nokeep 1 / 9 / 10,
  out-nokeep 10 / 10 / 72. Predicted 3-4 against 10: held at the median. Two keep-on runs exceed the predicted cap of
  5 (9 in in-on, 6 in orbit-r). All six bundle windows: 0 strands->cards, 0 frames on cards, 0 watchdog trips.
- Lint live and frozen: 0 failing for both jobs. Snapshot: 0 new entries. No core, layer or shader change, no build.
- Left: live_flow's producer-sanity phase does not know that GI far needs rt_stats=1 or that the core handout line
  needs key 2 (`PRODUCER_SETTINGS`); lint passed the doomed job. Not changed here (shared tool, frozen lint).

## Scene handout: hit-material fix on by default with Low-latency scene; inn decision job staged (branch claude/handout-fix, READY_FOR_LIVE, 2026-11-14)

- `hq-s8-modes.1.205520` (S8 Ultra static, 8 traces per state): mode 2 alone drops the native hit work (light rays at
  hits 0.14 M against 3.84 M, ordered 0, diffuse light -0.232% at -6.2 se, GI trace 4.6 to 3.6 ms). With
  `rt_scene_handout_hit_wait=1`: 3.83 M rays, ordered 1, diffuse +0.064%, scene wait 0.28 ms (OFF 3.5), 0 timeouts,
  0 fallbacks, frame time 21.58 against OFF 21.29 / 21.49 ms: correct, no gain at S8 Ultra (GPU-bound), no cost.
- Kept, now the default: the Low-latency scene On choice writes `rt_scene_handout_hit_wait=1`; code default and
  `bg3rtx.ini` are 1; the release policy keeps it at 1 (a player file from before the fix, or with only a preset,
  loads with it). settings_test `test_scene_handout` pins the keys, old files and a stale 0. Schema and UI sample
  regenerated. No ABI, shader or variant change (`shaders/variants.txt` delta 0).
- Card text corrected: "Saves 0 to 0.5 ms" (wait 3.5 to 0.3 ms at Water Queens House, frame time unchanged there on
  Ultra, about 0.5 ms on High). The "1 to 3.5 ms" claim is withdrawn: at the inn the GI trace fell 3.56 ms of the
  3.51 ms frame-time gain, so that gain was skipped work. SCENE_HANDOUT_DESIGN 8.5, 8.6, new 8.7; USER_SETTINGS.
- Analyzer: the mode 1 "FAIL" (+0.230 ms against pair noise 0.200, floor 0.214) was a noise estimate from two medians.
  `state_costs` now also uses the spread of the single OFF periods, as the window groups do (noise 0.315): INFO, the
  run is PASS. A rise beyond that spread and the floor still fails (tests/sane_gates_test.py). New: light rays at hits
  must equal the OFF pair within noise and 1% (FAIL otherwise); a `GI trace` row in the window groups.
- Staged, not queued: `handout_qual/jobs/hq-inn-fix.json` (predicted.json first). The inn, Ultra, focused, FG 3x, no
  cap: OFF / mode 2 + fix / OFF; static, pan Q and E (3 periods each), the pans with RTX Hair On, 4 float traces and a
  short rt_stats window per state. 30 phases, 807 s estimated (cap 14 minutes). Runner gates are "the line exists" in
  the windows whose state prints it (mode 2 lines only in ON windows, GI far only in rt_stats windows; no periodic
  line in the any-line expect). Lint live and frozen: 0 failing. Snapshot: 6 entries appended.
- Predicted at the inn: wait 8.5 to 0.3-3 ms, frame time gain 0 to 1 ms, GI trace and hit rays equal to OFF, energy
  inside the OFF range. Decision rule: the handout stays On in Ultra unless ON is slower beyond noise and floor or
  the hit rays differ; then Off in the Ultra preset until the query moves before the lighting (design 8.6).
- User check in game: restart on High or Ultra; `[core] scene handout: ... hit material passes on the committed scene
  N` has N above 0 and `[rt] scene wait` is near 0.3 ms; Performance > Low-latency scene Off (live) to compare.

## Scene handout: S8 energy loss is the unpublished hit-material query; fix behind a key; sensible gates (2026-11-14)

- Energy run `hq-s8-energy.1.191419` (8 traces per state): OFF-a 648.51, ON 647.24, OFF-b 648.57. ON -0.20%, 4.7
  standard errors, 7 of 8 below every OFF sample. Diffuse light -0.31%, G-buffer equal. Real, under visibility.
- Cause (code + logs on disk): the 4b commit-time hit-material query sits in the capture command buffer, before the
  G-buffer's deferred packet, and is never published (`[hit material submit] ordered 0` on 10 of 10 handout frames).
  Hit GI has no qualified hits and no light rays at hits; reflections hit shading uses the same query.
- So the GI trace GPU drop (S8 4.83 -> 3.49 ms, inn 8.2 -> 4.7 ms) was dropped work: the Ultra frame-time gains in
  SCENE_HANDOUT_DESIGN 8.5 are overstated. High (no native hit materials) is not affected.
- Fix, off by default: `rt_scene_handout_hit_wait=1`. Hit-material passes of a handout frame take the committed pair
  (one wait, at the first such pass); the layer skips the commit-time query. No ABI, shader or variant change.
  Build and 226 CTests passed; unit case in native_hit_order_test. Cost unknown until live.
- READY_FOR_LIVE, staged: `handout_qual/jobs/hq-s8-modes.json` (0 / 1 / 2 / 2 + fix / 0), lint 0 failing.
- Sensible gates (user rule 2026-11-14, AGENTS.md): `tools/live_batches/sane_gates.py`, used by handout_qual and
  perf_ab. FAIL only beyond the OFF noise and a floor (cost max(0.1 ms, 1%), energy 0.5%, match 1 point); under it
  INFO; an unpredicted cost drop or a sub-visible energy difference is EXPLAIN; short windows are "not judged".
  hq-s8-modes runner gates only check that the lines exist. tests/sane_gates_test.py.
- Today's logs under the new rules: hq-smoke-s8 PASS; hq-s8-high PASS; hq-inn-high PASS; hq-s8-energy EXPLAIN
  (-0.20%, GI trace -1.27 ms); hq-inn-ultra EXPLAIN (RT -3.5, GI -3.4 ms in the pans); hq-s8-ultra FAIL (hair-pan
  frame time p99 +4.4 ms: ON periods 25.5 / 32.0 / 30.4 against OFF 25.0-27.1; energy not judged, 0 traces mapped).
- Disk: the 24 trace folders `test-main/captures/20261114-191625` to `-191832` (1.17 GB) are analysed: all deletable.

## RTX Hair stability: GPU header camera in the bundle, per-pixel card fill, refit disproved (branch claude/hair-stable, READY_FOR_LIVE, 2026-11-14)

Design and history: docs/RTX_HAIR_PRIMARY.md 6g. Offline only; no live run.

- What the user saw (play log of 0.9.6-hairtest13, about 2 minutes of hair, Ultra): 2927 hair frames, 960 (33%) with
  every hair draw on cards; 139 switches to cards (frame watchdog 72, coverage low 35, other 26, own toggle 6);
  161 draws under a tenth of their card pixels (the holes). Still camera: 0 switches.
- Cause of nearly all of it: `rt_hair_header_cam_gpu` was 0. Every trip had its ray origin one camera step off
  (0.08-0.57 m; sampled 0.308 and 0.570 m). The losses were real (252 draws under their bound, 8 above), not gate noise.
- Kept: RTX Hair On now writes `rt_hair_header_cam_gpu=1` (old files get it on load). Proven live before
  (hc1 234025 strand share 0.93-0.98, hl1 zoom 2-7% -> 50-70%); cost a 16-thread dispatch and two 12-byte copies.
- New, off: `rt_hair_card_fill` (Advanced knob, restart to turn on; specialization 1053 + header flag 512). A hair
  pixel without a strand draws the game's card instead of a hole (behind a strand another fragment writes). With it
  the frame watchdog only reports and a swapping key returns to cards after 28 low measurements in a row, not 2.
  Visible change when still: the 3-7% of card pixels without a strand show the card.
- Proof offline: key off equals origin/main after specialization (canonical SPIR-V, 16 combinations); raw identity
  225 of 227 (the two are rt/hair_primary_gbuffer.hlsl, spirv-val passed); shaders/variants.txt delta 0.
  CTest test_card_fill; settings_test and schema updated.
- Disproved: the LSS refit as the cause of the dolly loss. In-place refit of a compacted BLAS under 0.5-1.3 m per
  frame keeps 61347 of 61347 hits and differs from a fresh build in at most 1 of 65536 pixels (RTX 5090, CTest
  hair_primary_gpu_test refit_check).
- Open: the 30-50% strand loss under a fast dolly with the GPU camera (6d-6f) is NOT solved. It cannot be proven
  from the code or the logs on disk (hl1 logs pruned). Six named candidates with the dense-sample signature of each
  are in `tools/live_batches_staged/hair_stable/predicted.json`. Strands in every moving frame depend on that live.
- Not changed: styles without a converted groom stay cards (3399, 7086, 4077, 1623, 9384 indices in the play log).
- Staged, not queued: `tools/live_batches_staged/hair_stable/` job hx1-s8-hair-stable (S8, one launch, about 65 s of
  phases): wheel dolly in and out and orbit with the fill on, the same dolly with the fill off, dense samples.
  Gate: every switches line in the fill windows has "frame watchdog 0, key watchdog 0, coverage low 0".
  Lint frozen 0 failing (snapshot appended only); merge_scope tooling exit 0.
- User check after a go: S8, RTX Hair On, "Strand hair: card under missing strands" On, restart; zoom in fast with
  the wheel and pan: no hole, no head popping to cards.
- Next: if the gate holds, add `rt_hair_card_fill` to the On bundle; fix the cause the dense samples name.

## RTX Hair: the dolly loss named (the decal pass took the header's label), key rt_hair_header_scene_pass (branch claude/hair-residual, READY_FOR_LIVE, 2026-11-14)

Design and evidence: docs/RTX_HAIR_PRIMARY.md 6h. Offline only; live hx1 180928 analysed, no new live.

- hx1 180928 (S8, 12 of 12 phases, INVALID for two job faults). Card fill on: frame watchdog 0, key watchdog 0,
  coverage low 0 in all five windows, 0 frames on cards (6g's gate held); 12 and 10 reported trips in the two dollies.
  Fill off: 6 trips, 10 switches to cards, 28 of 61 frames with every draw on cards.
- Dense samples, card-marked pixels: still 84-88% accepted, 4-5% no strand; moving 53-63% accepted, 25-37% no strand.
  All 600 lines: origin 0, 0 mrad, anchors still (median 1.3 mm), strand of another draw 0. The loss follows camera
  step x sin(angle from the view axis): accepted 0.79 under 0.1 m, 0.16 at 0.3-0.5 m, 0 above 0.5 m.
- Candidate named: P5 (acceleration structure). Points, BLAS primitives and rays are of the frame, the TLAS is the
  publish before (boxes one camera step old). P1-P4 and P6 are out.
- Cause: the header is applied once per frame label at the first pass with 4 or more colour targets on the G-buffer
  depth. The decal pass (4 targets) matches too. When the present falls between the scene G-buffer pass and the decal
  pass, the decal pass takes the next label and the next frame's hair runs on the header it applied: the old TLAS
  and rows over BLASes, points and camera of the new build. The build stamp was copied at that apply, so 6e read
  "same build". Rows read on the GPU in 11 frames are those of build label - 1.
- Kept, off: `rt_hair_header_scene_pass` (Advanced knob, live, CPU only): only the scene G-buffer pass (5 targets)
  applies the header; velocity draws accept the label of the interval before. Not a pure fix (it changes the label
  the velocity draws and the count see in that phase), so a key. No shader, no ABI, variants.txt delta 0, GPU cost 0.
- Proof offline: CTest hair_groom_asset_test test_header_pass (present between the two passes: key off 59 of 60
  frames trace the publish before, key on 0, no velocity draw refused; present elsewhere: no change).
  build.ps1 -Test 226 of 226.
- Open: two scene passes in one present interval (2 of the dolly's 16 frames, after LOD decode hitches of 47-94 ms):
  the second runs on the first one's header and reuses its entries, with or without the key. Counted on the new
  `[hair primary] header pass` line. Needs a header and tag per scene pass recording; not done here.
- Diagnostics: dense sample lines 96 per stats period instead of 600 a session (the first dolly used them all).
- Card fill into the On bundle: gate held, cost in the noise (0.30 against 0.39 ms, 4-7 frames), still image 4-5% of
  the card pixels show the card, no hole in the 1280 px PNGs, seam not judgeable there. Its own commit on branch
  `claude/hair-fill-bundle` (user decision; not needed for the dolly once the key is in).
- hx1 regenerated (predicted.json first): dolly windows of 180 frames (the 60-frame out window ended before the
  tenth notch), sample pattern allows `camera step -1.000`, PNGs only in still windows at full width. Key on without
  and with the fill, then key off, in one launch (about 110 s of phases). Gate with the key: no label taken by the
  decal pass, and in the four dollies frame watchdog trips 0, no frame on cards, no watchdog or low-coverage switch.
  Key off: label taken by the decal pass 1 or more. Lint 0 failing (frozen), snapshot appended only.
- User check after a go: S8, RTX Hair On, Advanced "Strand hair: header at the hair's own pass" On; zoom in and out
  fast with the wheel: far heads and heads at the screen edge keep their strands.
- Next: the live; if it holds, the key joins the On bundle; then the hitch frames (per-recording header, LOD decode
  off the render thread).

## RTX Hair: final live hx1 194658 read, the bundle holds; coverage keep and card fill recommendations (branch claude/hair-final-report, docs only, 2026-11-14)

Table, gates and evidence: docs/RTX_HAIR_PRIMARY.md 6j. Image pairs: docs/hair_fill/. No code, no live, no new tool.

- Run: hx1 194658, S8, VALID, 25 of 25 phases, 229 s. Window occluded (372 of 372 checks): no frame time judged.
- Bundle alone (rt_hair_header_scene_pass 1, card fill 0, keep 0), 6 moving windows (2 dollies and 2 orbits of 180
  frames, 2 dollies of 120): 0 strands->cards of any cause, 0 frame watchdog trips, 0 frames with every draw on cards,
  own-bound fails 0, label taken by the decal pass 0 in all 32 key-on lines. Strand draws 97.4-99.1% of the hair draws;
  the rest are heads that enter the view (2-3 per window, cards for 10 frames). PASS.
- Dense samples moving 79-84% accepted / 3.9-5.6% no strand, still 80-87% / 3.4-4.8%: inside the still spread
  (78-90% / 3.0-6.5%). Control without the key: 59% / 27.4% and 70% / 14.4%.
- Control (in-base, out-base): decal pass took 179 of 216 and 129 of 184 labels; 7 and 1 frame watchdog trips (all 8
  lines of the run, frames 4974-4980 and 5157); 10 and 17 switches to cards; 29 and 23 of 181 frames all on cards.
- The 3 switches left with the key (pre-in, shot-on, shot-fill): all "coverage stale", all key 244b at 0-1 card
  pixels (a head off the screen for 28 frames). Unseen. Two fell into still windows (predicted 0 there: timing only).
- rt_hair_coverage_keep (in-keep, out-keep): 0 switches of any cause, 0 trips, "coverage stale" refusals 16-35 -> 0,
  card run of an entering head 10 -> 5 frames (3 "strike" + 2 "card depth not dropped" remain; predicted 0-1
  cards->strands, measured 3 and 2). The 0 switches alone do not separate it from the bundle (in-on, out-on also 0).
- Hitch frames: none ("by another scene pass" 0, second passes with their own header 0 in all 36 lines). The 6i
  per-recording header stays proven only by the CTest.
- Cost: strand G-buffer draws 0.13-0.20 ms zoomed out, 0.27-0.35 ms near. Keep 0.31 / 0.17 ms against 0.27 / 0.16;
  fill 0.15 / 0.35 against 0.20 / 0.32: no difference outside the spread.
- Card fill pairs (still-on 194931 | still-fill 195009, shot-on 194942 | shot-fill 195020; final image at 2560,
  3x nearest crops of 4-5 heads, |difference| x4, G-buffer albedo crops, full frames): the fill draws the card on
  5.2-5.6% of the card pixels zoomed out (151-161 of about 2900, about 13 pixels a head) and 4.5-5.0% near (883-1005 of
  about 19700), scattered between strands and along the outline. Final image: mean RGB inside the hair equal within 1
  of 255, no seam, no colour step; the visible differences are idle animation.
- Recommended: (1) rt_hair_coverage_keep into the On bundle. (2) Card fill stays an Advanced option (no visible gain,
  watchdog only reports with it, restart). (3) Remove rt_hair_stale_publish, rt_hair_row_stable, rt_hair_watchdog_draw,
  rt_hair_watchdog_one_key, rt_hair_swap_latch, rt_hair_coverage_min_pixels (low risk, all off in this run); the
  key-off header path later (medium: the only A/B of the fix). Keep the frame watchdog and the period lines.
- Left for "100%": styles without a groom file (S8: index counts 3399 and 7086, 2 draws a frame); hitch frames
  unproven in game; 5 card frames of an entering head with keep.
- User check: S8, RTX Hair On alone, zoom fast and orbit: heads in view keep strands. Fill decision: open
  docs/hair_fill/shot_heads_final_a.png, shot_heads_final_b.png, still_heads_final.png.
- Status: user decision pending on the card fill; coordinator decision on keep and the removals.

## RTX Hair: coverage keep in the On bundle, entering head 5 -> 3 card frames, six obsolete keys removed, hitch frame line (branch claude/hair-cleanup, READY_FOR_LIVE, 2026-11-14)

Design and evidence: docs/RTX_HAIR_PRIMARY.md 6k. Offline only; evidence is hx1 194658 (6j). No live.

- Bundle: RTX Hair On writes `rt_hair_coverage_keep=1` (26 keys). Old files get it on load. hx1 194658: "coverage
  stale" refusals 16-35 -> 0, entering head 10 -> 5 card frames, 0 switches of any cause, no cost outside the spread.
  The Advanced knob stays as the A/B. `rt_hair_card_fill` stays an Advanced option outside the bundle,
  `rt_hair_dense_sample` a diagnostic, `rt_hair_header_scene_pass` and `rt_hair_header_cam_gpu` stay switchable.
- Entering head, with keep on (`SwapGate::early`): the 5 frames were 2 without the groom in any publish (its build
  latency: kept), 1 with it in one publish only (kept: the frame may still read the publish before) and 2 of the
  prepass and G-buffer handshake. Both handshake frames go: a ready mark counts in the same publish, and the frame
  the groom is first published arms the prepass. Offline 5 -> 3 (4 when that frame's header lacks the groom).
  A prepass that went first for a refusing G-buffer meets the existing native-depth fallback. CTest
  `test_entering_head`. Not exercised in game.
- Removed with their code, knobs, ini lines and tests: `rt_hair_stale_publish` (the submit-time camera check and its
  UBO lookup at every hair draw), `rt_hair_row_stable` (GroomRows), `rt_hair_watchdog_draw` (DrawWatchdog),
  `rt_hair_watchdog_one_key`, `rt_hair_swap_latch`, `rt_hair_coverage_min_pixels`. 394 lines of layer and header code
  net (48 added, 442 removed), 223 of tests, 5 Advanced knobs, 25 ini lines. An old user file with the keys loads
  (carried, read by nothing: settings_test). All six read 0 in the qualifying run: no behaviour change by the removal.
- Log text: the switches line has seven causes (no "key watchdog", "coverage latched", "key watchdog trips", "latched
  keys"); the header safety line lost the per-draw and one-key fields; the "stale publish check" line is gone.
- Hitch frames (two scene G-buffer passes in one present interval, never seen live): the first one of a session now
  writes one line also with `detailed_logging=0`: "[hair primary] hitch frame: ...". Cost: two sums per pass.
- No shader, no ABI. `shaders/variants.txt` delta 0. GPU cost 0; the per-draw UBO binding query of 6a is gone (CPU).
- hx1 regenerated (predicted.json first): 16 phases, about 98 s, the bundle as the launcher writes it (no card fill,
  no dense sampler). Gated at 0 in 4 dollies and 2 orbits of 180 frames: label taken by the decal pass, by another
  scene pass, strands->cards of any cause, frames with every draw on cards, frame watchdog trips, rebase origin.
  A/B inside the run: `rt_hair_coverage_keep=0` (2 dollies). Reported: card runs (3 or 4 predicted, 10 with keep
  off), refusals, dropouts, GPU ms, a hitch frame. The job expects the seven-cause line: an older layer is INVALID.
  hc1, hk1, hl1, hf2 marked historical, pl1 not current (generators' docstrings); they still lint.
- Tests: `build.ps1 -Test` 226 of 226, lint live and frozen 0 failing for the staged hair jobs, snapshot appended
  only (3 readiness queries, 1 pattern), `merge_scope --run tooling` 0.
- User check after a go: S8, RTX Hair On alone. Zoom fast with the wheel and orbit: heads in view keep strands; a
  head that enters the view shows cards for about 3 frames.
- Next live (hx1, one launch): 0 switches of any cause in every bundle window; card runs 3 or 4 (5 = the handshake
  change had no effect; then say so); "prepass card depth dropped without its G-buffer swap" 0; keep off: card runs 10.
- Left: styles without a groom file (S8: index counts 3399, 7086); the key-off path of the header pass key (after a
  release); the 2 build-latency frames of an entering head; docs/HANDOFF_SESSION.md still names the removed keys
  (coordinator's file).

## RTX Hair: scene pass header confirmed and into the On bundle, hitch frames fixed, key rt_hair_coverage_keep (branch claude/hair-adopt, READY_FOR_LIVE, 2026-11-14)

Design, table and evidence: docs/RTX_HAIR_PRIMARY.md 6i. Offline only; live hx1 190905 (VALID) analysed, no new live.

- hx1 190905, key off (rt_hair_header_scene_pass 0): the decal pass took the label in 31-42 of 60 frames while still
  and 165 of 217 / 131 of 183 in the two dollies (52-76% of all frames traced the publish before). in-base: 6 frame
  watchdog trips, 9 switches to cards, 29 of 181 frames with every draw on cards; out-base: 3, 12, 29. Dense samples
  moving: accepted 61-71%, no strand 16-25%. All 8 watchdog lines of the run are in these two windows.
- Key on, 24 period lines (still, 7 moving windows): label taken by the decal pass 0, by another scene pass 0, frame
  and key watchdog 0, coverage low 0, coverage high 0, 0 frames on cards, dropouts 0, own-bound fails 0.
  Card fill off (in-fix, out-fix): 0 switches to cards, 181 of 181 hair frames with strands, moving accepted 81%,
  no strand 4.0-4.1% (still 81-85%, 4.9-5.0%): the loss under motion is gone. No hitch frame in the run.
- The 3 strands->cards switches left with the key (pre-in, in-fill, out-fill) are all "coverage stale" of key 22a2:
  no card pixel in the counted frames after the dolly in, stale 28 frames later, cards unseen. Visible is only the way
  back: a head that comes into view again shows cards for 8-10 frames (2-3 cards->strands per dolly, card runs of 10).
- Gates quiet with the key: the worst used ratio of a swapped draw stayed 0.017-0.097 above its own bound. No
  threshold changed. 6e's "same build" reading corrected in 6i (the stamp was copied at the decal pass's apply).
- Adopted: RTX Hair On writes `rt_hair_header_scene_pass=1` (`Choice::kv` 24 -> 32; old files get it on load).
- Fixed: two scene G-buffer pass recordings in one present interval. The scene build runs once per label, so both
  trace the same publish; the second now applies the header again with the next tag (never a tag twice), its own
  pending header and GbFrame (`hair_primary::HeaderLabels`, `HeaderPass::Again`). Counter on the header pass line:
  "second scene passes of an interval with their own header and tag".
- New, off: `rt_hair_coverage_keep` (Advanced knob "Strand hair: keep the last coverage verdict", live, CPU only):
  coverage evidence does not expire by age; a newer measurement, an invalidation, a latch or a watchdog still replace
  it, a new key and every key after a level load still need a measurement. Not in the bundle (a gate change, no live).
- Proof offline: CTest hair_groom_asset_test test_header_pass (two and three frames in one interval: key off 1 frame
  on the other's header and its entries reused; key on 0, tags unique, tags meet the labels again) and
  test_coverage_keep (stale after exactly 28 hidden frames without the key, never with it; failed, invalidated and
  new keys never passed). No shader, no ABI, variants.txt delta 0, GPU cost 0. build.ps1 -Test 226 of 226.
- hx1 regenerated (predicted.json first): 25 windows, about 140 s. Bundle alone, card fill flag off, in 2 dollies,
  2 orbits (180 frames, turning; the old orbit windows turned for a second), 2 short dollies and 2 still windows:
  header at the scene pass only, decal 0, another scene pass 0, every gate's switch count 0 (coverage stale open),
  0 frames on cards, 0 trips. Then keep on (0 switches of any cause), card fill on (PNG pairs, still and a second
  after the dolly, at 2560), control without the key (decal 1 or more). Lint live and frozen 0 failing, snapshot
  appended only, merge_scope tooling 0.
- User check after a go: S8, RTX Hair On alone: zoom fast with the wheel and orbit; far and edge heads keep strands.
- Open: card fill decision (user, the PNG pairs); `rt_hair_coverage_keep` into the bundle if its windows hold;
  styles without a groom file keep cards (S8: 3399 and 7086 indices). Removable after the live: rt_hair_stale_publish,
  rt_hair_row_stable, the per-draw and one-key watchdogs, the swap latch, the key-off header path.

## Sensible gates refined, producer lint, handout decision recorded (branch claude/gates-refine, tools and docs, 2026-11-14)

- Cause: `sane_gates.compare` failed a difference beyond max(noise, floor). hq-inn-fix.1.211816 said FAIL on six
  lines that were no fault (RT +0.130 ms, 0.017 ms over the floor on 11.3 ms; GI +0.164 against noise 0.160; a p99 of
  +1.075 from 2 ON periods; timeouts 1 and 3 in the OFF hit windows; light rays +3% on an OFF pair 3% apart).
- Rule now (`tools/live_batches/sane_gates.py`, AGENTS.md): FAIL beyond noise plus floor, INFO between noise and
  noise plus floor. A p99 takes the full range of the OFF periods as noise. Events (`sane.events`): OFF windows are
  the baseline (INFO), ON fails only above the worst OFF window; counters without a baseline (fallbacks) still fail
  at 1. Equal counts (light rays at hits): OFF spread plus 1%, INFO inside it.
- Still failing (tests/sane_gates_test.py, 10 tests): mode 2 light rays 135707 against 3.84e6 (hq-s8-modes 205520),
  +1 ms frame time at 21 ms, the +4.4 ms hair-pan p99, a fallback, ON timeouts above OFF; energy -0.20% is EXPLAIN.
  Changed expectation: +0.5 ms at 21 ms with OFF periods 0.63 ms apart is now INFO (was FAIL).
- Re-read of the logs on disk (job files of today's main): hq-smoke-s8 PASS; hq-s8-high PASS; hq-inn-high PASS;
  hq-s8-modes.205520 PASS; hq-inn-fix EXPLAIN (RT and GI 1.5 ms lower in the rt_stats hit window; 0 failing, was 6);
  hq-s8-energy EXPLAIN (-0.20% energy, GI trace drop: the pre-fix defect); hq-inn-ultra EXPLAIN (7, the same pre-fix
  dropped hit work); hq-s8-ultra FAIL (hair-pan p99 +4.410 against noise 2.100 plus floor 0.260); hq-s8-modes.204217
  INVALID (stopped in the warm-up); perf-ab-inn, wide-split-0, wide-split-1 REVIEW_REQUIRED (tables only, no
  verdict rule); perf-ab-s8 INVALID (windows under 2 periods). The perf.md files in captures were not rewritten.
- Lint: `live_flow.PRODUCER_SETTINGS` knows `[core] GI far` needs `rt_stats=1` and `[core] scene handout` needs
  `rt_scene_handout=2` (the hq-s8-modes 204217 stop). The producer-sanity phase then switches them on itself. Frozen
  preflight unchanged: staged 23 jobs, 0 failing; live_batches 124 jobs, 71 failing, identical output before and after.
- Docs: SCENE_HANDOUT_DESIGN 8.8 and USER_SETTINGS record the user's decision (Low-latency scene On in High and
  Ultra, always with the hit wait fix) and the numbers (wait 3.5 to 0.3 ms S8, 8.5 to 2.9 ms inn; frame time
  unchanged on Ultra; about -0.5 ms on High at S8; 0 fallbacks; energy equal).
- Open: benefit in CPU-bound scenes unmeasured; the hq-s8-ultra hair-pan p99 FAIL never re-examined (its state
  columns may be shifted: energy OFF b scene wait reads 0.000 ms).
- No C++, shader or settings change; no build needed. variants.txt delta 0.

