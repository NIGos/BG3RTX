## Inn outward-dolly hitches: synchronous groom LOD decodes (branch claude/r4-hitch, READY_FOR_LIVE, 2026-11-21)
- Cause (queue log r2-inn.1.020820, Ultra, RTX Hair On, `rt_hair_groom_async=0`): all 36 frame gaps of 90 ms to 1 s
  in the 12 dolly windows follow a `[hair groom] <id> LOD n decoded (v3)` line within 33 ms (e.g. 02:10:15.416
  84ef20d4a0aa148b LOD 0 decoded in 157 ms, frame 2914 at 15.446 took 222 ms). The other 12 gaps are energy traces.
  The read and LZMS decode run inside the scene build (`groom_lod`, layer/bvhsplit.cpp), so the frame waits on the
  build: scene wait max 151-186 ms, frame time p99 110-118 ms in the outward windows (static and inward: 37 ms,
  3-10 ms). 6-11 decodes per outward window, 0.5-1.1 s of decode; the same LODs come back in every outward
  window (84ef20d4a0aa148b LOD 0 five times): the groom pool sits at 464-496 of 512 MB and evicts them.
- Not in the game's streaming: every long dolly frame has one of our decodes just before it.
- Fix: the existing `rt_hair_groom_async` (default 0, READY_FOR_LIVE since 2026-11-15, CTest
  `hair_groom_decode_test`) moves the read and decode to a below-normal worker. No new key. Added: the decode line
  says `decoded (v3, worker)` when the worker ran it, so a live proves the state per window; the key is listed as
  host-only in tools/live_batches/pipeline_readiness.py.
- Live staged (not queued): `tools/live_batches_staged/r4_hitch` r4-inn, 0 / 1 / 0 in one inn launch with r2-inn's
  windows, ~8.5 min. Gates: every ON outward decode on the worker, at least one in-build decode per OFF outward
  window. Predicted: ON outward p99 at the inward level (37-50 ms, at most 60), scene wait max under 40 ms, no
  Streamline "Frame rate over 100 ms" lines in ON dollies, energy equal within 0.5%, GPU ms equal.
- Open: the pool keeps evicting LODs the dolly brings back (re-decodes); a budget question, not this fix.
- Live r4-inn.1.030500 (2026-11-21): outward dolly p99 ON 39.7-40.6 ms vs OFF 98-111, scene wait max ON 6-7 ms vs
  OFF 163-167, every ON decode on the worker. Energy ON -1.12% (-2.5 SE), all 4 ON traces inside the OFF trace range
  (analyzer: ok). Not the key: in the energy windows every groom LOD is resident (same LODs, 0 kept, 0 dropped, strand
  coverage 19672 px ON between 18371 and 19343 OFF, 0.5% of the pixels), and `groom_lod` returns for a ready LOD
  before it reads `rt_hair_groom_async`. The traces follow the firelight: luminance correlates 0.82 with the dump's
  peak (111-776 per trace), the loss is red (R -1.6%, B -0.3%), the clipped PNG previews are equal (-0.06%). The
  predicted rule (OFF pair widened by 0.5%) was too tight for a scene with flames: use the OFF trace range.
