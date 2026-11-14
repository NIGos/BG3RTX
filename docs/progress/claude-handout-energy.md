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
- READY_FOR_LIVE, staged: `handout_qual/jobs/hq-s8-modes.json` (0 / 1 / 2 / 2 + fix / 0, 8 traces and an rt_stats
  window per state, about 5.5 minutes after load, lint live and frozen 0 failing).
- Sensible gates (user rule 2026-11-14, AGENTS.md): `tools/live_batches/sane_gates.py`, used by handout_qual and
  perf_ab. FAIL only beyond the OFF noise and a floor (cost max(0.1 ms, 1%), energy 0.5%, match 1 point); under it
  INFO; an unpredicted cost drop or a sub-visible energy difference is EXPLAIN; short windows are "not judged".
  hq-s8-modes runner gates only check that the lines exist; values are judged by the analyzer. tests/sane_gates_test.py.
- Today's logs under the new rules: hq-smoke-s8 PASS; hq-s8-high PASS; hq-inn-high PASS; hq-s8-energy EXPLAIN
  (-0.20%, GI trace -1.27 ms); hq-inn-ultra EXPLAIN (RT -3.5, GI -3.4 ms in the pans); hq-s8-ultra FAIL (hair-pan
  frame time p99 +4.4 ms: ON periods 25.5 / 32.0 / 30.4 against OFF 25.0-27.1; energy not judged, 0 traces mapped).
- Disk: the 24 trace folders `test-main/captures/20261114-191625` to `-191832` (1.17 GB) are analysed: all deletable.
