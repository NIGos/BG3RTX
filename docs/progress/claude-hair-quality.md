## Strand quality: footprint coverage bound, pair gate bias, item 4 design (branch claude/hair-quality, READY_FOR_LIVE, 2026-11-07)

- Small draws (2026-11-07 A/B: three per frame refused `coverage low`, 600-900 per window): the strict bound's noise
  term is the offline footprint's (9397 samples, sigma 0.019). Key `rt_hair_coverage_footprint` (off): every
  measurement, also a first swap, is judged by [0.9105 - sigma, 1.0753 + sigma] with sigma = (sqrt(S) + r sqrt(C)) / C
  of that measurement; at the offline footprint this is the strict bound. Live draws: 0.842 of 443 passes (own bound
  0.827), 0.792 of 610 stays on cards (0.842), 0.31 of 51 and 0.674 of 46 stay on cards. Test:
  hair_groom_asset_test test_coverage_footprint (never stricter than the strict bound, freshness unchanged).
- rt_hair_groom_coverage=1 lowered the pass rate (600 vs 900-945 swaps per window): the gate counts card pixels and
  accepted strand pixels where the card covers the jittered pixel centre; the pair's rays sit a quarter pixel off
  it, so near a card edge one falls off the card. Model (hair_groom_test test_coverage_pair_gate_bias, scratch run):
  pair/single ratio 0.884 on 2 px cards, 0.943 on 4 px, 0.970 on 8 px, 0.992 on 32 px. The pair is still unbiased
  against the pixel's area coverage (existing CTest), so this is a gate bias on narrow cards, plus the temporal model
  of claude/hair-stability (pair residual 0.091 vs single 0.056). Recommendation: keep rt_hair_groom_coverage off.
- Item 4 (minimum on-screen strand footprint), not implemented: the strand radius is the LSS BLAS's static per-point
  radius array (bvhsplit.cpp groom upload, radiusData), shared by every pass that traces the groom TLAS (strand
  shadow/AO transmittance, fibre GI). A primary-only clamp r' = max(r, 0.5 px x distance) needs a second
  per-frame radius array written by hair_groom_update.hlsl from the publish camera, a second LSS BLAS per groom
  (refit with the points) and a primary-only groom TLAS, plus coverage conservation in the G-buffer variant (keep a
  widened hit with probability r / r' through the native TAA dither, as the cards' alpha). Cost: one more groom
  BLAS refit per groom per frame and one TLAS. Needs an owner decision and its own branch.
- Item 6: the coverage line now prints each draw's own footprint bound (`own=`) and `draws outside bounds n
  (outside their own footprint bounds n)`; the period line shows `footprint bound on/off` next to the hold, the
  refusal tally, header choice and resumes. Jitter needs no counter: the strand rays use the inverse jittered
  view-projection at the fragment's raster position (structural match, hair_primary_gbuffer.hlsl:129-135).
- Next live (Test Dialogue1, hold and current on, coverage and tips off): toggle rt_hair_coverage_footprint 0/1.
  Expect `coverage low` refusals to drop to the draws outside their own bound (`outside their own footprint
  bounds` small), main strand G-buffer swaps above the 900-945 of 1500 per window, no resumed-after-card increase.
