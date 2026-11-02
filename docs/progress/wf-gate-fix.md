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
- Rerun prepared: cal-static burst first (4 bursts, about 9.4 GB transient, deleted after). The run's bursts and
  traces deleted (7.1 GB).
