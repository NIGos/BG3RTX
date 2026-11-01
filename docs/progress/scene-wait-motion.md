## Scene wait in motion: phase probe, unlocked material captures, alpha floor by stride (branch scene-wait-motion, READY_FOR_LIVE, 2026-11-01)

- Live su1 21:26 (S8, Ultra keys, rt_hit_material_recycle=1): static windows 0 timeouts, wait mean ~2.8 ms (was
  456/600); pans and rotations 1-7 timeouts per 120 frames, mean 5.2-7.6 ms, max 12-22 ms. No cpu_timing in that run.
- Probe (always on, no cost): the scene thread's phase (prepass recording, scene lock, hit material prepare, BVH build,
  alpha commit, or no capture yet) is counted at every timeout and logged after the scene-wait line:
  "[rt] scene wait timeouts by the scene thread's phase: ...". It names the remaining delay in one run.
- Candidate fix `rt_hit_material_capture_unlocked=0/1` (live, default 0): the G-buffer material hooks
  (deferred_begin, pool_observe) read their draw's bindings without the provider mutex, which the prepass hooks
  take on every draw while the scene is recorded; in motion more draws are captured (new records), so the prepass
  thread waited longer. Only this recording's thread writes its packet until finalized (others read finalized ones).
- Alpha region: rt_alpha_mb left at 64 follows the vertex size like rt_xfb_mb (171 MB at 64 B). su1's first
  window made 0.58 alpha draws/frame opaque while the region sat at its floor after the load (demand 224 MB; later
  windows 0.00 at 89 MB region / 59 MB demand). rt_capture_budget_test covers it.
- Job: tools/live_batches/scene_wait/jobs/sw3-motion-s8-fg2.json (117 s, pans Q/E unlocked 0/1/0 + two rotations
  with 1, cpu_timing=1, gate 0 timeouts in cost-on-*, base windows log the baseline). Dryrun passes.
