## wf1 water flicker run 093458: gate fix, analysis, rerun prepared (branch wf-gate-fix, 2026-11-02)
- INVALID only through the job: the smoke-ultra "one trace written in each static window" every-window gate was
  pointed at the cost windows (no trace there). Now the shot windows; with it the run passes all 7 gates
  (queue_gates on the kept log). The 3 K16 bursts were written (2.4 GB each, transport ok).
- Key rt_water_rr_hitdist toggled live as planned (`WATER_RR_HIT=1 ... built` at 09:36:54 and 09:38:14).
  Energy: shot-on -0.07% (band 1.25%, predicted null): pass. GPU: water reflections 0.128 ms, GI trace 4.00,
  reflections 0.570 ms ON vs 0.569 OFF: no cost.
- Flicker: not decided. pan_artifacts needs a static calibration burst; the job had none, so every rotation burst
  is INCOMPLETE (and the tool crashed on a numpy bool in its JSON: fixed). Own metric (second temporal difference of
  the reprojected final luma, all valid samples): OFF-a 0.00870, ON 0.00873, OFF-b 0.00761: no difference beyond
  OFF-OFF. It is dominated by reprojection error at edges, not water-specific (the bursts carry no water mask), and
  the key changes RR's hit distance only where the water reflection resolves on screen (8-15% of the water rays).
- Rerun 100826 (foreground, calibrated): pan_artifacts final-luma transient pixel-frames per calibrated pixel-frame,
  whole frame (no water mask in the bursts): rot-off-a 2.20%, rot-on 1.65%, rot-off-b 2.18% (events 1.68 / 1.31 /
  1.67%): ON 25% below both OFFs, OFF-OFF spread 1%. Caveats: ON rotates the other way (other content, 80k vs 112k
  calibrated px) and its burst started at the toggle (the variant built 6 ms after arming); gi_luma events rose ON
  (0.31% vs 0.14 / 0.15%). Energy -0.10% (band 1.25%); water reflections 0.119 ms in all three cost windows.
  Verdict: consistent with a partial fix, not proven; next run: ON burst a few seconds after the toggle, same
  rotation direction for all three, and a water-coverage mask (rt_debug 21 trace at the static pose).
- Rerun prepared earlier: cal-static burst first (4 bursts, about 9.4 GB transient, deleted after). The run's bursts and
  traces deleted (7.1 GB).
