## Strand grain attribution, card normal and world tangent keys (branch claude/hair-grain, offline, off, 2026-11-07)
- New tool `tools/hair_groom/grain_stages.py`: per image-chain stage HF energy and line coherence over the class-5
  hair mask; `--ab on,off,on <dirs>` prints the 5g live gates.
- 2026-11-07 Test Dialogue1 traces (strands a, f, fp0, fp2 vs cards e; a/e matched lit mean): strands never carry more
  per-frame HF than cards (a/e: albedo 0.99, lit 0.91, RR output 0.78, final 0.96); motion vectors vary 2.1x more per
  pixel with the cards' outlier rate. Line coherence differs: albedo 0.31-0.33 vs 0.35, lighting 0.28 vs 0.32 (4x the
  strand spread), final 0.75-0.79 vs 0.84. Attributed to the camera-facing fibre normal (N.V = 1 on strand pixels).
- `rt_hair_macro_normal` (knob exp_hair_macro_normal, Advanced): RT0 = the card's own normal, exactly the native RT0.
- Found from the decompiled hair VS 41a168e2: the native RT3 tangent is world space, the strand variant wrote view
  space, so the strands' Marschner lobes used a rotated tangent. `rt_hair_tangent_world` (knob
  exp_hair_tangent_world, Advanced) writes the world tangent. The suspected grazing-term space mismatch is not one
  (frame and ray both world).
- Both under specialization 1045 (restart to enable, then live via header flags 16/32); no cost; request delta 0.
- Live check (Test Dialogue1, one session): RTX_HAIR_PRIMARY 5g lists windows and thresholds.
