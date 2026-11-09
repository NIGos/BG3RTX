## rt_hair_warmup on by default (branch claude/hair-warmup-on, 2026-11-09)

- hw1 (Test Dialogue1, 20:46, VALID): first strand G-buffer swap 73 and 66 frames after a level load with the key on,
  309 frames with it off (the fixed 300-frame pause); load quiet ended "groom state settled" at +63 / +57; dropouts 0
  in every window. Kept, on (it acts only with rt_hair_groom_primary=1).
- Same round (evening-20261109): sh2 scene handout passes every gate but the in-motion wait (0.6-0.7 ms, static 0.06),
  0 fallbacks, water on the handout 59/60, no false OMM downgrades; rv3 / rv5 VALID but the camera did not move in
  them (captured triangles flat at 1.3M), so the cache headroom and the BLAS cap by occupancy are still unmeasured
  under load.
