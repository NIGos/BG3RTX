## Scene handout: S8 energy loss is the unpublished hit-material query; fix behind a key (2026-11-14, claude/handout-energy)

- Analyzer: `hq-s8-energy.1.191419` was INVALID because `gi_1ray_qual` `float_samples` required exactly 3 traces
  (the job has 8). It now takes the plan's count; `handout_qual/analyze.py` judges every `energy-<state>` against the
  OFF repeats and prints per-pass float means and the hit-material lines per state.
- Result: OFF-a 648.51 (sd 0.49), ON 647.24 (sd 0.73), OFF-b 648.57 (sd 0.31). ON -0.20%, 4.7 standard errors, 7 of
  8 below every OFF sample; OFF-a against OFF-b 0.009%. Diffuse light -0.31%, G-buffer equal. Real, not noise.
- Cause (code + logs on disk): the 4b commit-time hit-material query sits in the capture command buffer, before the
  G-buffer's deferred packet, and is never published: `[hit material submit] ordered 0` on 10 of 10 handout frames,
  1 on 9 of 9 waiting frames. Hit GI then has no qualified hits and no light rays at hits (bounce 0.0099 against
  0.0103); reflections hit shading uses the same query. OMM ruled out (0 violations in both states).
- So the GI trace GPU drop (4.83 -> 3.49 ms) was dropped work (the relaxed "not higher" gate hid it): the Ultra
  frame-time gains in SCENE_HANDOUT_DESIGN 8.5 are overstated. High (no native hit materials) is not affected.
- Fix, off by default: `rt_scene_handout_hit_wait=1`. Hit-material passes of a handout frame take the committed pair
  (one wait, at the first such pass); the layer skips the commit-time query. Core + one layer condition, no ABI,
  shader or variant change. Build and 226 CTests pass; unit case added to native_hit_order_test. Cost: unknown
  until live (the wait that remains).
- READY_FOR_LIVE, staged, not authorized: `handout_qual/jobs/hq-s8-modes.json` (0 / 1 / 2 / 2 + fix / 0, 8 traces and
  an rt_stats window per state, about 5.5 minutes after load, lint live and frozen 0 failing).
- Disk: the 24 trace folders `test-main/captures/20261114-191625` to `-191832` (1.17 GB) are fully analysed and can
  all be deleted; nothing deleted by this branch.
