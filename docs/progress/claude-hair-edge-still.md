## Strand edge flicker: he1 method audit and a still-pixel live job (branch claude/hair-edge-still, READY_FOR_LIVE, 2026-11-09)
- Question (user): does rr_hair_strand_aa=2 not work, or was the verification wrong? The he1 04:02 burst verdict
  (band/core flicker ratio unchanged) cannot judge it: method faults, details in docs/RTX_HAIR_PRIMARY.md 5n.
- Faults: animation counted as flicker (raw temporal std on the silhouette band, no motion exclusion; off bursts alone
  30-80% apart); no settle after the mode switch (0.31-2.3 s, against rr_post_temporal's 108 frames); exposure free
  (qa_exposure_hold=0 in every he1 window); mean of per-pixel ratios. MV sign/units and masks are right.
- Fix: tools/hair_groom/edge_burst.py still-pixel metrics (summed RR motion <= 1/4 px), ratio of means, half-burst
  drift, judge() with the threshold from the key-off repeats' spread (2 sigma), `--modes`; hair_groom_test
  test_edge_burst_still.
- Job: tools/live_batches/hair_edge_still he2-dialogue1-still (Test Dialogue1, 8 bursts ABBA ABBA, 108-frame settle
  each, exposure held), live_flow lint 0 failing, ~6.3 min, ~4.4 GB of bursts (delete after the summary). Not queued.
- Predicted (predicted.json): final band_still flicker aa/off -10 to -50%; core and band mean within 2 sigma.
- Check after the run: `py -3 tools/hair_groom/edge_burst.py <bursts> --modes off,aa,aa,off,off,aa,aa,off`.
