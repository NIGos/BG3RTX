## Release verification lives 2026-11-09: rise reserve on, capture limits found (branch claude/rise-reserve-on)

- `rt_capture_rise_reserve=1` by default. rv1 (Water Queens House, camera lowered 10 notches, release Ultra,
  FG 3, RTX Hair, 12:11): cascade capture 4480-4640 MB off, 3120 MB with the key, 4096 MB off again
  (-1.0 to -1.5 GB); `[cascade] casters ... buffer full 0.0` in every window. BLAS chunks flat at about
  4.09 GB, no stall report. Kept.
- `rt_alpha_floor_yield` stays off: opaque drops 2-8 -> 0.03-1.4 per frame in that view, but 0.02-0.15
  alpha draws/frame made opaque. The view needs 2065 MB (opaque 1620 + alpha 445) of the 2047 MB cap
  (2^31, shader addressing): RTX Hair's 88-byte capture vertices (64 without) push it over. Next step for
  wide views: a per-draw stride or a second capture buffer, not the partition.
- Strand resolve (rr_hair_strand_aa=2): 0 fallbacks in he1 play windows (Dialogue1, FG 2; the morning fix
  works), but at S8 with FG 3 113-140 of 600 frames fall back with 600 snapshots and 600 resolves:
  overlapped frame recording, fix on branch claude/strand-pair.
- he1 VALID 11:30; rv1 attempts INVALID by design (gates written before the S8 overflow was known), mined.
