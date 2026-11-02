## rf_hit_gi re-measure prepared; the S6 light-visibility suspect tested offline (branch rf-hit-gi-2, READY_FOR_LIVE, 2026-11-02)
- The tier (a) ratios S6 1.090 (m4p3 161852) and S8 0.925 (m4p4 113146) come from `diag-*` windows without a trace, so
  the 385efd1 trace flaw (hit material skipped on traced frames) did not touch them. They predate the game's lobes at
  hits (RF_HIT_GI -> NATIVE_HIT_GAME_BRDF, merged with rf-hit-box): expected now S6 1.064 (1.17 with rf_hit_gi), S8
  0.925 (1.015 with rf_hit_gi).
- S6 visibility suspect (rf light rays lit 71% vs 56% in the GI): the light ray's alpha footprint is not it. On the S6
  pack 035919 the GI pass gives lit 16512 / 29609 (55.8%, as live) with the footprint x 1, x 16 and x 256 (identical
  counters). The two lit shares belong to different hit populations (reflection hits see more of the lava-lit far
  side; rays to lights of radius >= 10 m 90% vs 80%). No visibility fix: nothing found to fix offline. The reflection
  pass cannot be replayed yet (frame_replay replays gi_hit_direct only).
- Live job `tools/live_batches/rf_hit_gi_ab` (S6 + S8, 2 x 190 s, about 10 min, 0.4 GB): diag-rf / diag-gi1 / diag-gi2 /
  diag-gi1-box2 with rf_stats, cost and shots rf off / on / off on Ultra as merged. Predictions in its predicted.json.
  Dry run and lints pass.
