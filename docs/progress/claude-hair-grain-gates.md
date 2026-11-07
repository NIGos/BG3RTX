## 5g live gates evaluated (branch claude/hair-grain-gates, 2026-11-07)
- grain_stages.py: the RR guide pass is found by its RGBA16F normal guide (the 22:05 dump set has no RR pass);
  `--tangent` gives the tangent gate its own grouping; a window with under half the median hair pixels is INVALID.
- Test Dialogue1 one session: both-on #1 (220524) INVALID, a warm-up dump (5790 hair pixels vs ~40,000, strands in
  264/300 frames of that period): scalp only, no hair.
- rt_hair_tangent_world: |T.N| 0.0015 (tangent on, macro off) vs 0.297 (off): PASS. Mean +2.3%. Default-on candidate.
- rt_hair_macro_normal: lit coherence +0.0096 vs margin 0.01: FAIL (one valid on window); RR specular guide HF x2.83:
  PASS; hair mean +31%. Kept off: the brightening is unexplained without a same-session card window.
