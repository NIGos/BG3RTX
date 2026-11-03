## Card-to-strand groom experiment, Shadowheart (branch claude/rtx-hair-groom-33655b, offline, 2026-11-03)
- New `tools/hair_groom/` (converter, analysis, CPU comparison renderer) and CTest `hair_groom_test`.
  No runtime, shader, ABI or settings change.
- Shadowheart LOD0: 656 cards, 40,090 triangles -> 86,196 strands, 856,802 segments (451k at 4 mm
  segments); LSS positions+radii 15.1 MB per character, refit 11..21x the card primitive count.
- Offline render vs cards: silhouette IoU 0.92..0.94, coverage +1..+4 points, radiance -11..+9%.
- Verdict: approximate (short strands lying on card sheets). Production path and costs in
  docs/RTX_HAIR_GROOM_EXPERIMENT.md. Paused: needs a GPU LSS refit/trace gate before any runtime work.

## Hair LSS cost gate, quality options, integration design (branch claude/rtx-hair-groom-33655b, offline, 2026-11-03)
- `tools/hair_lss_bench` (CMake option BG3RTX_HAIR_LSS_BENCH, off): RTX 5090, 3.7M rays per batch from
  the hair. Shadow/AO/GI ms: cards with the 3-sample test 1.83/1.64/2.99, groom 2 mm 0.90/0.75/1.28.
  Refit 0.104 ms both; groom frame update 0.115 ms. BLAS 73 MB (2 mm), 38 MB (4 mm).
- Dedupe of stacked copies: 529 strands (0.6%), no visible or cost change. Asset depth (PixelDepthOffset
  1 mm x mask G) is a raster trick, not volume: off. Fibre split to 0.18 mm: +50% trace, 208 MB BLAS: off.
- Integration design: docs/RTX_HAIR_INTEGRATION.md (groom in secondary rays only, content-ID join,
  per-frame embedding update, masks in the four consumers, BSDF licence, fallback, ABI, gates).
- Optimisation sweep: 2 mm segments + BLAS compaction (60 MB, -17%) is the near setting; LOD by strand
  thinning (radius widened) 1/2: trace -22% (0.71/0.62/1.03 ms), 1/4: 0.63/0.61/0.92 ms, IoU 0.91/0.89.
  ALLOW_UPDATE costs ~15% trace but refit (0.1 ms) beats rebuild (1.3 ms); end caps NONE gains 0..3%: kept CHAINED.
