## RTX Hair one-frame holes: cause not established, deciding diagnostic added (branch claude/hair-frame-miss, READY_FOR_LIVE diagnostic, 2026-11-10)

- hk1 is INVALID and disproves the shared-key prediction: the gate of 0 watchdog trips failed in turn-shared-r (8
  trips; 3 to 10 per 120 frames without the key), later windows did not run, so `rt_hair_watchdog_one_key` is
  unmeasured in turns. "shared-key rows withheld" 0: both rows of the dropping key were in every publish.
- hs1 (static): pre-RR flicker strands / cards 1.02, final 1.50 (reconstruction side of predicted.json). Not clean:
  12 dropouts of key 6047 in frames 2244 to 3322 with watchdog trips 0; in the bursts one head loses its hair for one
  frame (class-5 pixels 232 against about 2100 at frame 2582, 332 against about 1900 at 2904; final luminance
  0.44 -> 0.32). Frame 2582 is not in the dropout list: the counter sums the two draws of a key.
- 6047 (hs1) and 3b08 (hk1) are the keys on two groom rows (two characters, one hairstyle). Their rows exchange
  content between frames (row 10 / row 11 strand pixels 2206 / 3233 -> 3185 / 2177): a dynamic slot is the n-th draw
  of its mesh in the capture (layer/bvhsplit.cpp `g_dyn_seen`), so a draw-order swap puts the other character's points
  into each slot. Proven effect: previous points of the other character (wrong motion vectors that frame). Not proven:
  that the hole happens in that frame, nor why. "rejected after the hit" for one key does not exclude missed rays
  (dropout_reason uses frame-wide totals), so the static holes and the frame-wide miss in turns may be one class;
  undecided offline.
- Added, CPU only, no key, no shader, no ABI change, variants.txt delta 0: `[hair groom] slot content jumps` line
  (hair_primary::groom_bounds_jump on the per-slot bounds readback, threshold = the bounds diagonal) and
  `[2 rows: a+b]` per-row strand pixels on listed dropouts of a two-row key.
- Deciding observable in the next S8 static live: every 6047 dropout frame F has a jump at build frame F or F - 1,
  jumps only on two-row keys. Otherwise the exchange is ruled out and the per-row pixels name one row or both.
- Installed build: strand primary is off by default (here and at cb2d03f2); the live jobs turn it on.
- Details: docs/RTX_HAIR_PRIMARY.md 5q. No fix shipped (no guessed fix).
