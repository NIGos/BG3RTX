## Card-to-strand groom experiment, Shadowheart (branch claude/rtx-hair-groom-33655b, offline, 2026-11-03)
- New `tools/hair_groom/` (converter, analysis, CPU comparison renderer) and CTest `hair_groom_test`.
  No runtime, shader, ABI or settings change.
- Shadowheart LOD0: 656 cards, 40,090 triangles -> 86,196 strands, 856,802 segments (451k at 4 mm
  segments); LSS positions+radii 15.1 MB per character, refit 11..21x the card primitive count.
- Offline render vs cards: silhouette IoU 0.92..0.94, coverage +1..+4 points, radiance -11..+9%.
- Verdict: approximate (short strands lying on card sheets). Production path and costs in
  docs/RTX_HAIR_GROOM_EXPERIMENT.md. Paused: needs a GPU LSS refit/trace gate before any runtime work.
