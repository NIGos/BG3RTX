## Strand edge flicker: he1 method audit and a still-pixel live job (branch claude/hair-edge-still-2, READY_FOR_LIVE, 2026-11-09, rebased 2026-11-15)
- Question (user): does rr_hair_strand_aa=2 not work, or was the verification wrong? The he1 04:02 burst verdict
  (band/core flicker ratio unchanged) cannot judge it: method faults, details in docs/RTX_HAIR_PRIMARY.md 5n.
- Faults: animation counted as flicker (raw temporal std on the silhouette band, no motion exclusion; off bursts alone
  30-80% apart); no settle after the mode switch (0.31-2.3 s, against rr_post_temporal's 108 frames); exposure free
  (qa_exposure_hold=0 in every he1 window); mean of per-pixel ratios. MV sign/units and masks are right.
- Fix: tools/hair_groom/edge_burst.py still-pixel metrics (summed RR motion <= 1/4 px), ratio of means, half-burst
  drift, judge() with the threshold from the key-off repeats' spread (2 sigma), `--modes`; hair_groom_test
  test_edge_burst_still.
- Job: tools/live_batches_staged/hair_edge he2-dialogue1-still (Test Dialogue1, 8 bursts ABBA ABBA, 108-frame settle
  each, exposure held). Regenerated 2026-11-15: today's RTX Hair On bundle (26 keys, rt_hair=1) read from
  common/settings.h, no old capture pins, none of the six removed keys. live_flow live and frozen 0 failing; the
  lint snapshot grew by 462 lines, none changed. ~6.3 min, ~4.4 GB of bursts (delete after the summary). Not queued.
- Predicted (predicted.json): final band_still flicker aa/off -10 to -50%; core and band mean within 2 sigma.
- Check after the run: `py -3 tools/hair_groom/edge_burst.py <bursts> --modes off,aa,aa,off,off,aa,aa,off`.
- A live run must show: `edge_burst.py --modes` prints PASS for final (band_still flicker of the aa bursts below the
  off bursts by more than 2 sigma of the key-off repeats, core and band mean within 2 sigma), the positive control
  holds (rr_color band hf of every aa burst below the off mean), and at least 256 still px per burst (else INVALID).
- Cost: no runtime change on this branch (analysis and job only). The key's cost stays he1's +0.18 ms render-res chain.
