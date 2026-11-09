## RT capture alpha floor yield, BLAS stall report persistence (branch claude/capture-fixes, 2026-11-09)

- Evidence (live rv1-s8-release-verify, Water Queens House, camera lowered 10 notches, release Ultra + FG 3,
  RTX Hair): RT capture "opaque draws dropped (buffer full) 1.27" per frame with alpha 435 MB in an 818 MB
  region (the 40% wide-stride floor) and an opaque peak of 1568 MB of the 1229 MB left in 2047 MB. The floor
  also bounded the opaque cap and the demand split from below, so opaque never got more than 60%.
- `rt_alpha_floor_yield` (new, default 0): the opaque cap and the demand split may go down to the
  stride-scaled rt_alpha_mb floor (171 MB at 64 B); when both reserves do not fit, the split decides alone.
  S8 case: 444 / 1603 MB, both fit. The load frame keeps the 40% floor. Key off: identical (golden tests).
- `[vram] BLAS ranges awaiting retirement` now needs the floor exceeded and the oldest live scene frame
  unchanged for 300 frames. rv1 11:30 fired on a rebuild burst (1615 MB pending, all 7 slots fresh).
- Tests: rt_capture_budget_test (S8 split, base floor bound, unchanged fit and load cases).
- Check in game: rv1 job with rt_alpha_floor_yield 0 / 1 / 0 in the lowered S8 view: opaque drops 0.00 and
  alpha made opaque 0.00 with the key on.
