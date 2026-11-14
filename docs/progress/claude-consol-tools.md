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
