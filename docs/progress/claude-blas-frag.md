## BLAS chunk cap by occupancy for oversized BLAS (branch claude/blas-frag, READY_FOR_LIVE, 2026-11-09)

- Evidence: rv3 191231 key off, S8 zoomed-out turns 19:15:01-14: BLAS chunks 4189 MB reserved in 50 chunks,
  1237 MB live, little pending (56-500 MB), 0 empty, at the guard's cap (4013-4189 MB). 290-575 refusals per
  window, every sampled one larger than a 64 MB chunk: cascade casters' BLAS 64-111 MB (left out of the sun's
  RT that frame), grooms 72-136 MB, a groom compaction copy 113 MB.
- Cause: the reserve is about 48 sparse 64 MB base chunks (about 26 MB live each after the turns: per-draw and
  cached BLAS of different lifetimes; no relocation for per-draw BLAS) plus the oversized chunks. Empty chunks are
  freed (or freed at the cap) and retirement is not the limit (pending small). A range larger than 64 MB needs a
  chunk of its own and can never use the 2.9 GB free inside base chunks, while the guard counts reserved bytes.
- Fix, `rt_as_cap_occupied` (live, default 0, NO_PIPELINE): a chunk larger than a base chunk may pass the reserved
  cap while live + pending BLAS bytes plus it stay within the cap, up to cap + 25% reserved; an unguarded caller
  (cascade, compaction copies) that passes the reserved cap this way also passes the guard's Low check. Base
  chunks and key off: exactly the old check. Mesh cache line appends refusals, reserved, occupied and the cap.
- Rejected: rounding oversized chunks to multiples of 64 MB so emptied predecessors fit the next rebuild. In the
  replay it cut chunk creations 72 -> 18 but raised refusals 60 -> 104 (the rounding waste fills the cap).
- Test: as_chunk_alloc_test replays rv3's sizes (48 sparse base chunks, a 64-136 MB cascade rebuild per frame, a
  groom every 10 frames, 6-frame retirement, 120-frame reuse, 4013 MB cap): refused key off 60 / on 0 in 480
  frames, peak reserve 4013 / 4233 MB; chunk_within_cap edge cases and key-off identity with unguarded_chunk_ok.
- Live (prepared, not run): rel095_verify rv5-s8-blas-cap-occupied, the rv3 plan with the key 0 / 1 / 0; ON windows:
  `0 BLAS requests of the other users refused`; every window: 120 split builds, 0 skipped, capacity gates.
