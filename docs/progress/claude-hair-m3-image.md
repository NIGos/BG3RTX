## Hair M3.2 sub-pixel coverage and M3.3 RR hair guides (branch claude/hair-m3-image, READY_FOR_LIVE, 2026-11-07)
- M3.2 `rt_hair_groom_coverage` (spec 1042, off, needs rt_hair_groom_primary): two stratified rays per strand
  candidate pixel in `rt/hair_primary_gbuffer.hlsl` (`rt/hair_primary_coverage.hlsli`), a quarter pixel either side
  of the jittered centre; both hit: nearer; one hit: frame-hashed checkerboard; unbiased (h_A + h_B) / 2.
  Compiled in at pipeline creation (restart to enable); header flag 8 switches it off live for a same-run A/B.
- Footprint inflation was not possible: the LSS radius is in the groom BLAS shared with shadow/GI rays.
- Offline gate (CPU `primary.coverage_reference` 8 x 8, 200 strands 0.25 mm at 0.6-3 m, 16 Halton frames):
  single / pair bias +0.10% / +0.63% (4 sigma 2.8%), 3 x 3 box error 0.0649 / 0.0603 (-7%), box flicker
  0.0567 / 0.0531 (-6%); other seeds -4 to -5%. GPU pick = CPU twin on >= 99.95% of hit pixels, same metrics.
  Small gain: G-buffer pixels stay binary. Cost: 2x the strand trace (estimate +0.4 ms at a close-up), unmeasured.
- M3.3 `rr_hair_guides` (spec 1043, off, live): rr_guides read RT2.z as metalness on hair, but class 5 RT2.z is
  the backlit factor: backlit hair fed RR a metal. With the key class 5 gets metalness 0, roughness >= 0.09, fibre
  F0 0.0465 (the game's R lobe). Hit distance unchanged (D-RF does not trace class 5: kNoHit).
- Key off: 215/218 requests byte-identical to main, 3 strictly equivalent with 1042/1043 at 0; request delta 0.
- Tests: hair_groom_test (test_coverage, test_rr_hair_guides), hair_primary_gpu_test (coverage check); build -Test
  207/207 (rr_r11 skipped). Fixed on the way: `primary.trace` failed on a chunk without hits.
- Live (L2, Test Dialogue1, strands on): toggle rt_hair_groom_coverage 1 -> 0 -> 1 and rr_hair_guides 0 -> 1 in one
  run. Predicted: hair pre-post mean unchanged within noise for both; edge-flicker on strand pixels a few % lower with
  the pair; strand pass GPU ms about 2x with the pair; with the guides, backlit hair less smeared under RR.
