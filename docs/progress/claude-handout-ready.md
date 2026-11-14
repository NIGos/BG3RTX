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
