## RTX Hair On now includes the union count, the confirmed coverage failure, stable slots and the live warm-up (branch claude/hair-keys-default, offline gates passed, 2026-11-10)

- User decision 2026-11-10: keys that are useful and fix the problem go in. RTX Hair On (common/settings.h
  `exp_hair_groom_primary` choice "on") also writes `rt_hair_coverage_union=1`, `rt_hair_coverage_confirm=1` and
  `rt_hair_slot_stable=1`. The live switch `rt_hair=0` gates them with the rest (layer/util.cpp `hair_gated` reads the
  same list); a file with only `rt_hair_groom_primary=1` still reads as On.
- Evidence (docs/RTX_HAIR_PRIMARY.md 5u): VALID hs1 095322 with the three keys on: coverage low / other switches 0
  in 30 periods (25 and 22 with the keys off in 085153), dropouts 0 in 28 of 29 periods, judged ratio 1.00 to 1.04.
- `rt_hair_live_warmup=1` is in the bundle too: VALID hs1 103633 (five keys on), 30 periods, frame watchdog trips 0
  (095322: 5 after the toggle), dropouts 0 in 27 of 27 footprint lines, 11 keys back on strands after the live toggle
  (card runs 80 to 105 frames) without a trip.
- Disproved prediction: `rt_hair_coverage_min_pixels` was to give 0 "coverage stale" switches on tiny hair; 103633
  shows 5 (key 2aa3 at 57 to 70 card pixels, across the 64 pixel threshold, which has no hysteresis; key 9b09 four
  times). Not qualified, kept out, not fixed here.
- Not in the bundle (settings test checks it): `rt_hair_coverage_min_pixels`, `rt_hair_swap_latch`.
- Code defaults stay 0: with RTX Hair off nothing changes. No shader edit; the union count request
  `rt/hair_primary_count.hlsl?HP_COUNT_UNION=1` was already in shaders/variants.txt (delta 0).
- Staged hs1 job: its hard-coded hair set got the four keys (job `set` and make_job.py).
