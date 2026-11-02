## Event gates skip snapshots within settle_s of the settings read; hig1 camp rerun read (branch gate-event-settle, 2026-11-02)
- hig1 09:54 was INVALID only for diag-off-a: frame 1679 printed key-on IP grass rows 28 ms after the key-off read (a
  query recorded before it). queue_gates.py: event gates take "settle_s" (frames in flight x frame time: 8 x 0.11 s at
  rt_stats -> 1 s), set on every snapshot gate (hit material capacity / instanced). Re-gated: every-window gates ok (9).
- IP grass verdict: NOT qualified as is. Clip bit-exact (4.4M / 5.2M checked, 0 rejected), energy -0.01% (band 2.37%), cost
  -0.42 ms rt passes (OFF spread 0.18). But qualified share 26.4% OFF -> 24.3-24.7% ON: the IP grass rows (0.5-0.6M
  triangles) fill the deferred stream (4 x 32 MB, overflow 119-134) and evict the wind and wind RGB rows (captured 0);
  source stream 117/128 MB, basis 234/256 MB. Next: raise the layer caps (deferred_chunks, source/query caps), then rerun.
- OMM upper bound (rt_alpha_test 1/0/1, complete lines): rt passes 8.01-8.03 / 3.78 / 8.16-9.22 ms; gi trace 6.77 /
  3.08 / 6.96-6.98 ms; also ls 1.03 -> 0.41 ms, rf 1.16 -> 0.39 ms. "rt passes" includes the GI trace (core/gi.cpp
  rt_timestamp: sun shadow + GI trace/temporal/AO + sun resolve; ls and rf timed apart): total about 4.3 + 0.6 + 0.8 = 5.7 ms.
  Loose bound: forced opaque also ends every ray at the first cutout, also over clear texels (78% of evaluated alpha
  candidates are clear), which OMM still traverses.
