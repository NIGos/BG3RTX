## Pixel-footprint strand LOD: decode-time keeps built, camera BLAS designed (branch claude/hair-pixel-lod, offline, 2026-11-10)

- Step 1 built: decode_v3_lod(..., extra_keep) rebuilds keeps 1/8, 1/16, 1/32 from LOD 0 with the table's nested
  hash, radii x 1/keep under 9 mm, without changing the 888 files. hair_groom_v3_test: all three non-empty on the
  fixture, nested, radii exact to 1e-6. No layer use yet: behaviour unchanged, no key.
- Step 2 not built (larger than a prototype): it needs a camera-only BLAS per groom and a camera groom TLAS in
  bvhsplit. Cheapest correct route: reuse the groom's points (no second update), a static widened radius buffer and
  a kept-segment index buffer per LOD/keep pair, refit every frame; header with the camera TLAS and row.z = the camera
  index buffer, so the strand shaders stay byte-identical (variants 0). The hp_nearest candidate-filter route cannot
  widen strands (LSS radius lives in the BLAS). Estimated cost: ~18 MB mesh pool (radius buffers, S8), ~5-15% more
  BLAS memory, ~10-30% more groom refit ms, a cheaper strand trace. Cross-fade needs a shader change (open).
- Details and the pick rule (k = 1.0 px, no pixel LOD under 60 px, quarter-octave hysteresis + vote):
  docs/RTX_HAIR_PRIMARY.md 5v.
