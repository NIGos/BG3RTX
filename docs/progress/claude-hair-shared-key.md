## RTX Hair: shared style key and one-key watchdog (branch claude/hair-shared-key, READY_FOR_LIVE, 2026-11-10)
- hc1 2026-11-09 (S8 turns): review item 11 "CPU reads the build camera late" disproved (0 of 120 publishes differ at
  submit; `rt_hair_header_cam_gpu=1` did not remove the trips; `rt_hair_watchdog_draw=1` worse).
- Cause: the dropouts are the keys with two groom rows (7a27 86, 5285 41, 93ea 13 of ~190 entries): two characters
  with one hairstyle share one swap key (index range) while the scene keys grooms per occurrence. A published key
  swapped the draw whose own groom was out of this publish (LOD upload, held soup, first update): 0 strand pixels,
  then a frame watchdog trip put every hair on cards for 28 frames.
- New `rt_hair_shared_key` (off, Advanced `exp_hair_shared_key`): a key publishes only when its keyed rows cover all
  the capture's groom-matched draws with it; else all its draws keep cards this frame. CPU only, no shader, no ABI.
- New `rt_hair_watchdog_one_key` (off, Advanced `exp_hair_watchdog_one_key`): one bad key next to a passing draw
  cards that key only for 28 frames instead of tripping the whole frame.
- Tests: hair_groom_asset_test test_watchdog extended (shared key, one-key watchdog, key-off unchanged).
- hs1 2026-11-09 INVALID: burst 99300 completed with "transport INVALID" (frame gap) because the pan pipelines
  compiled inside it (677 ms pipeline, 5.5 s DXC); 99301/99302 ok. Job fault: warm-inputs now 8 s with
  pan_prepare=1, settles keep pan_prepare=1 (make_job.py regenerated, live_flow 0 failing).
- Live check (not run): S8 turns, keys off / shared / shared+one_key / off; expect the dropouts line without
  7a27/5285/93ea, "watchdog trips" near 0, "shared-key rows withheld" > 0 while turning.
