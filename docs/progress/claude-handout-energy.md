## Scene handout: S8 energy repeat evaluated (2026-11-14, claude/handout-energy)

- Run `hq-s8-energy.1.191419` (Water Queens House, static, exposure held, `rt_scene_handout` 0 / 2 / 0, 8 float
  traces per state) was reported INVALID "energy-off-a: missing float sample" although all 24 traces were on disk.
- Cause: `tools/live_batches/gi_1ray_qual/analyze.py` `float_samples` required exactly 3 traces per phase; the job
  asked for 8. Fix: `float_samples(run, phase, count=3)`; `handout_qual/analyze.py` passes the plan's `trace_at`
  count and prints n, mean, sd and range per state. The job and plan are unchanged.
- Result (normalized 5c3fdabc scene colour, dumps.json float mean / pre-exposure): OFF-a 648.51 sd 0.49, ON 647.24
  sd 0.73, OFF-b 648.57 sd 0.31. OFF-a against OFF-b 0.009%; ON -0.20% (standard error 0.04%), 7 of 8 ON samples
  below every OFF sample. Gate FAIL: a real, small loss. Diffuse light (d1669194) -0.35%, final frame -0.13%.
- One ON trace is at the OFF level in the per-frame buffers: most likely a frame that did not take the handout
  (`committed first 2` in that period). So the loss belongs to the handed out frame itself, in that frame.
- Verdict: a defect to chase (the design predicts equality in order), not a shading-model consequence. First
  suspect: visibility rays on the handout's traversal TLAS (stamp byte 24, OMM) or its alpha records.
- Next: one launch with `rt_scene_handout` 0 / 1 / 2 to split the stamp TLAS path from the no-wait commit. Not staged.
- Kept: the default (user decision); the energy point stays OPEN in docs/SCENE_HANDOUT_DESIGN.md 8.5.
- Disk: the 24 trace folders `test-main/captures/20261114-191625` to `-191832` hold 1.17 GB (about 48.6 MB each);
  nothing deleted. They can go once this entry is folded.
