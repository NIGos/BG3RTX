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
