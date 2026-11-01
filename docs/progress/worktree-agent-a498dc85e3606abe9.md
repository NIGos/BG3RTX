## Hit-material stale scene: per-frame GPU allocations off the commit path (branch worktree-agent-a498dc85e3606abe9, READY_FOR_LIVE, 2026-11-01)

- Symptom (user, in game): with native hit materials on, RT shadows detach while turning and flicker. `[rt] scene
  wait` per 600 frames: 0 timeouts without hit materials (lr1 camp, mean ~5 ms); with them 15-278 (m4i), 3-232
  (m4p4 S8), 5-456 with mean 9.8-12.5 ms in the user's run (the 12 ms cap: frame N traced frame N-1's scene).
- Cause (code read, no timer existed): the provider allocated and freed hundreds of MB of GPU buffers every frame
  (deferred packet 1-4 x 32 MB chunks + rows, query up to 256 MB, UV/basis jobs), freeing in `collect()` from the
  prepass/`scene_prepare` path (under the scene lock) and from `deferred_begin`, which also ran `collect()` on every
  G-buffer draw under the mutex the prepass hooks need per draw; the lighting thread's query held the scene lock for
  its whole recording.
- Fix `rt_hit_material_recycle=0/1` (live, default 0, layer): retired buffers reused (common/native_hit_recycle.h,
  rounded sizes, cap 512 MB, idle 120 frames), `collect()` once per packet/query, query recorded without the scene
  lock. Frame-pairing checks unchanged (rows of another frame still rejected). GPU work and commit order unchanged.
- Measure: `cpu_timing=1` now logs `[cpu] hit material ...` (per phase ms, allocations/frees per frame, MB/frame,
  reused, retained) and "capture end to commit, of which scene lock wait, hit material scene_prepare" on the scene
  build line. `rt_scene_wait_log_frames` (default 600; lean jobs 120).
- Gate: `queue_gates.py --lint-scene-wait` (CTest queue_gates_test, lean_dryrun check): every lean job gates
  `[rt] scene wait` with 0 timeouts in every traced cost window. 27 existing jobs migrated (gate + cadence 120).
  Note: hit-material jobs (m4p*, m4i) now FAIL that gate until their set has `rt_hit_material_recycle=1`.
- Live A/B prepared, not authorized: `tools/live_batches/scene_wait` (S8 + camp, 176 s each, recycle 0/1/0 in
  static and pan windows, exposure held, dryrun passes). Expected with 1: 0 timeouts, wait mean 2-5 ms.
- Tests: native_hit_recycle_test (new), queue_gates_test (+13 checks); `build.ps1 -Test` passes.
- User check after the batch passes: FG on, S8 or camp with the hit-material bundle, rotate with Q/E: shadows stay
  on their casters.
