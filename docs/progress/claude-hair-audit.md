## RTX Hair audit after the keys-on run: live-on warm-up and the evidence floor (branch claude/hair-audit, offline, not live-verified, 2026-11-10)

- Before/after (hs1 S8 still): 085153 keys off -> 095322 union + confirm + slot_stable on: coverage low 25 -> 0,
  other 22 -> 0 switches over 30 periods, per-key switch periods ~15 -> 0, judged U/C 1.00-1.04 flat over 64..16383
  card pixels, own-bound fails 0 in 84 bins, dropouts 0 in 28 of 29 periods, 104 slot exchanges resolved per period.
- Left 1: strands-on transient (5 frame watchdog trips, 26 dropouts) inside the rebuild of every groom after the live
  switch (full builds 11, LOD waits 15, UV joins 12). Fix `rt_hair_live_warmup` (off): the switch opens the level-load
  warm-up window (rt_hair_warmup's settled signal, at most 300 frames). Mid-play rebuilds (new character, LOD upload)
  not covered: stage not decided (row keys changed in that period).
- Left 2: tiny hair (5364, 1..55 card pixels, ratio up to 6.0) cycles every ~30 frames by "coverage stale". Fix
  `rt_hair_coverage_min_pixels` (off): measurements under 64 card pixels (kDropoutMinCard) are no evidence.
- Upper bound never refuses (S <= C); real keys reach U/C 1.07-1.08 at 256+ pixels. Steady cards: 2 S8 draws without
  a groom (3399, 7086), tiny/hidden keys. Install has 29 grooms vs 871 in the repo; 7 of the user's unmatched counts
  exist in the repo catalogue (conversion pending), 4 nowhere.
- Gates: hair_groom_asset_test test_coverage_audit (tiny draw sequence, 5000 random cases identical at >= 64 pixels,
  LoadGuard::restart window); build.ps1 -Test. No shader, no ABI change, variants delta 0.
- Live check: toggle RTX Hair off/on in S8 with rt_hair_live_warmup=1: 0 frame watchdog trips and 0 dropouts in the
  period after the toggle, "warm-up: load quiet ended" 30-80 frames after it. Details: docs/RTX_HAIR_PRIMARY.md 5u.
