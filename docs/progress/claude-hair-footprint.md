## Primary-only minimum strand footprint: rejected offline (branch claude/hair-footprint, REJECTED, 2026-11-07)

- Design (item 4): r' = max(r, d / (p11 H)) per point written by the groom update, a second LSS BLAS per groom and a
  G-buffer-only groom TLAS, a widened hit kept with probability r / r' through a dither. Implemented on the branch
  in full (layer, update and G-buffer shaders), then reverted before any build: its offline proof failed.
- Model (thin strands 0.1-0.6 px, 18 DLSS jitter phases, 72 frames, per-pixel area reference; scratch, and
  hair_groom_test test_footprint_rejected as the decision record):

  | path | mean (ref 0.332) | accumulated residual | EMA flicker | single-frame 3x3 box error |
  |---|---|---|---|---|
  | thin single ray (today) | 0.332 | 0.059 | 0.044 | 0.103 |
  | widened, nearest hit rejected (cards' native dither) | 0.250-0.315 | 0.17-0.20 | 0.038-0.040 | 0.116 |
  | widened, nearest hit rejected (R1 sequence) | 0.262 | 0.159 | 0.043 | 0.133 |
  | widened, any-hit candidates kept r / r' | 0.329 | 0.079 | 0.047 | 0.115 |
  | cards, alpha = area coverage, native dither | 0.32-0.39 | 0.13-0.15 | 0.026 | 0.100 |

- Why: a pixel's binary coverage p has variance p(1 - p) whatever the sampler; the DLSS jitter already stratifies the
  thin strand's sample over the pixel (low discrepancy), the keep-dither is a second, less stratified one. Rejecting
  the nearest widened hit also drops the strands behind it (energy loss where strands overlap); the per-draw native
  dither is biased per draw (its unbiasedness needs the jitter sweep, test_tips_dither). The strand "low resolution"
  look is therefore not a coverage-sampling gap; next candidates are the strand shading inputs (normal/colour
  aliasing per strand vs the cards' mip-filtered textures) and RR's history with current MVs.
- Cost it would have had: one more LSS BLAS refit per groom per frame, 8 bytes per point in the pool, one TLAS.
