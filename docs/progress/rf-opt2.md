## Default Ultra RF follow-up (branch rf-opt2, NOT QUALIFIED, 2026-11-06)

- Corrected inn packs 093603-61 (stats OFF), 093623-62 (ON): strict
  live equality PASS 2/2 dispatches each, all 3 outputs and raw counters.
- This does not hold under sustained replay: baseline identical-pipeline
  40-pair control fails exactness. First failure: 206 specular bytes / 63
  pixels and 18 R16 distance bytes / 9 pixels; counters equal (stats OFF).
- Diagnostic shader records identical full-float origins, directions and
  closest-hit distances at those 9 pixels. Projected UVs differ by about
  1 ULP across dispatches, cross integer pixel boundaries and select different
  depth/origin/history pixels. Root cause of that arithmetic instability is
  unresolved; no replay fix or durable live-equivalence claim.
- Validation OFF, layer BLAS flags, pipeline statistics, per-dispatch UBO
  re-copy and host-visible UBO allocation all fail to remove the instability.
  Driver executable register/binary-size statistics stay unchanged; driver
  recompilation is not established. Experimental harness edits removed.
- Profile ablations, 40 ABBA/BAAB pairs, 8 warmups: no-probe delta
  -0.216616 ms, 95% CI [-0.409168,-0.214240]; no-trace -1.279008 ms
  [-1.756400,-1.003488]. Exploratory, non-additive, not qualified live costs.
- rf_fast/1041 deferred the unchanged receiver-probe block until after tracing:
  -0.126424 ms [-0.128240,-0.125200], timing bar >= 0.05 ms met;
  registers 128 -> 96, shared bytes 0 -> 1536. Exactness FAIL: REMOVED.
- Control delta +0.000088 ms [-0.000608,+0.000400], exactness FAIL.
- Receipts: .scratch/rf-opt2/{state1-off,state1-on,control,candidate,
  projection-diagnostic,profile-probes,profile-trace}.log and ab.csv files.
- Production shaders unchanged; request delta 0, 218/218 byte-identical to HEAD.
- Relevant CTests PASS 6/6 (28.59 s); merge-scope regressions PASS 9/9.
- No new build-dependent test; merge_scope.py already excludes the 8 native/DXC
  Python gates. No game, queue action or Git writes. Next: resolve sustained
  projection instability before another candidate can qualify.
