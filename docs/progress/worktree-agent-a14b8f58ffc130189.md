## M5 water promotion package: batch m5_promotion, gate script, Ultra preset commit (branch worktree-agent-a14b8f58ffc130189, offline, READY_FOR_LIVE, 2026-11-01)

- Gate table of today's S8 runs (m5w2-m5w6, each key alone): PLAN_M5_WATER.md "Promotion". Participation passes in
  every window for all six keys; sun shadows with the R = C - D fix: clamped 0 in every window (was 145573), occluded
  62.7%. GPU: build 0.28, reflections 0.12, compose 0.10 (reflections) to 0.24 ms (refraction variant).
- Energy observable corrected. The m4 gate's 5c3fdabc is the lighting before the forward water pass: m5w4 read
  "+2.20% brighter" only because the darker water raised the exposure 2.74%. The post-water image (RR colour input,
  over the probe's b0[0]) gives -9.10% against the compose's own prediction -9.39%: matches.
- Glint noise: one frame per state cannot judge the small steps. Sun glints (up to ~1000) carry about 100% of every
  frame difference; compose and ls single frames read -2.2% / -2.6% against ~0 predicted (open, inside the pooled
  OFF noise of up to 1.7%). The batch takes 3 OFF-a + 3 OFF-b + 3 ON frames with `qa_exposure_hold=1`.
- `tools/m5_promotion_gate.py` (+ `tests/m5_promotion_gate_test.py`, 12 checks; both promotion gate tests now in
  CTest): energy two-sided against the in-run prediction (2 SE of the OFF frames + 5c3fdabc control + window spread),
  non-finite pixels, participation per key over every window, motion rows, water GPU lines.
- Batch `tools/live_batches/m5_promotion` (not authorized): `m5p1-s8-fg2` (Water Queens House) and `m5p2-flymm-fg2`
  (Flymm Cargos Basement - 101h 05m: open water in the save thumbnail; S1 docks loads the foundry now). Chain on at
  launch, live OFF / 4 cumulative steps / ON / OFF, cost windows diag-off, one rt_stats window, 3 debug views, pans
  Q/E with the chain on then off. Dry run passes: 11.2 min, 36 traces, about 1.6 GB.
- Preset commit (separate, merge only if the batch passes): the six water knobs leave Experimental, on in Ultra only;
  the restart ones require sharc_device=1. Settings test checks presets, needs and restart rules.
- Decision rule: both runs valid, every energy row matches with no new non-finite pixel, participation and motion
  rows pass. A Flymm run with an unmapped water family is INVALID: map that family before promoting.
- User check after a merge: Water Queens House on Ultra, the pool next to the columns: the column and rim shadows on
  the water, the tiles under the clear pool shifted toward the camera, reflections of the stone on the water.
