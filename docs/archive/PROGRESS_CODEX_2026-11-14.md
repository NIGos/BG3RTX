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
