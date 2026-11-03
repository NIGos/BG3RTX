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

## rt_hair_groom integration prototype (branch claude/rtx-hair-groom-33655b, READY_FOR_LIVE, key off, 2026-11-03)
- Key `rt_hair_groom` (on at startup for VK_NV_ray_tracing_linear_swept_spheres, then live A/B; Developer GUI).
  Matched native hair draws (index-only content ID -> data/assetdb/hair_grooms/<id>.hgrm, generated locally by
  `python -m tools.hair_groom.asset`) get strand points (rt/hair_groom_update.hlsl from the captured card triangles),
  a compacted LSS BLAS with refit and screen-size LOD, and a groom TLAS per scene slot traced by the secondary
  queries (constant 1020); the card stays for primary queries. No ABI bump: TLAS address via scene stamp 48..63.
- Offline: build and all CTests pass (+ hair_groom_asset_test); key-off identity in the commit notes.
- Live job (not queued): tools/live_batches/hair_groom (camp + Test Dialogue1, groom off/on shots, cost
  off/groom/off/no-hair), predicted.json committed. Gate: groom instances > 0 in groom-on windows.

## rt_hair_groom live 2026-11-03 10:40: INVALID (no join), fixed offline
- Device feature and both files loaded, but no hair draw had Shadowheart's LOD0 index count (unmatched 8/frame
  at camp): the camp draws LOD1. Added her LOD1 groom (a97c222a4f2c0dda, 6009 indices), LOD1 in the batch tool
  (atlas grid from the LOD0 of the same GR2), radius caps (LOD1 cards: up to 6.7 mm from degenerate UVs; now
  3x strand median, 2 mm; thinned LODs < 9 mm, the parser's bound 10 mm), and a one-time log per unmatched
  hair draw with its index count and reason (counters in the 300-frame line). Live plan: warm-nohair phase
  before the cost windows (no-hair pipelines built inside cost-nohair). Build, 131 CTests, dryrun pass.
