// M3.2 sub-pixel strand coverage (docs/RTX_HAIR_PRIMARY.md 5d, key rt_hair_groom_coverage, specialization 1042):
// two stratified camera rays per strand candidate pixel instead of one. Math twin of tools/hair_groom/primary.py
// coverage_offset / coverage_pick (CTest hair_groom_test, GPU check hair_primary_gpu_test).
// Only the G-buffer variant and the GPU check include it, so no other request's preprocessed text changes.
#pragma once
#include "hair_primary.hlsli"

// Header word 15 bit 3 (common/hair_primary.h kFlagCoverage): the layer sets it from the key every frame, so with the
// specialization on the key still switches off live (the pair then falls back to the single ray).
static const uint hair_primary_flag_coverage = 8u;

// Sub-pixel offset (pixels, added to the fragment's pixel centre) of ray i (0 or 1). The two rays sit a quarter pixel
// either side of the jittered centre on a diagonal that alternates with the pixel's checkerboard parity, so both axes
// are split into two strata and neither strand direction is favoured. Their mean offset is zero: the estimate is a
// symmetric 2-tap prefilter around the jittered sample, it does not shift the image.
float2 hp_cov_offset(uint2 px, uint i) {
  const float s = i == 0u ? -0.25 : 0.25;
  return ((px.x + px.y) & 1u) != 0u ? float2(s, -s) : float2(s, s);
}

// Which ray's hit the pixel keeps: 0 ray A, 1 ray B, 2 none (discard). Both hit: the nearer one. One hit: kept on half
// of the pixels, a checkerboard whose phase is hashed per frame (tag), so the pixel's expected coverage is the mean of
// the two rays' hits, (h_A + h_B) / 2: unbiased against the pixel's area coverage. The frame phase is a hash, not the
// tag's parity: the jitter sequence (Halton) is correlated with the frame parity.
uint hp_cov_pick(bool hit_a, bool hit_b, float t_a, float t_b, uint2 px, uint tag) {
  if (hit_a && hit_b) return t_b < t_a ? 1u : 0u;
  if (!hit_a && !hit_b) return 2u;
  const bool keep = (((px.x + px.y) ^ hp_mix(tag)) & 1u) == 0u;
  return keep ? (hit_a ? 0u : 1u) : 2u;
}
