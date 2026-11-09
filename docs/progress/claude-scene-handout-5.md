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
