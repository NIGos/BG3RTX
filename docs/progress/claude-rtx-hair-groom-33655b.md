## Strand hair (rt_hair_groom): experiment, cost gate, prototype (branch claude/rtx-hair-groom-33655b, 2026-11-03)
- Converter `tools/hair_groom/` (cards -> strands from the CHAR_Hair mask), CTest `hair_groom_test`.
  Shadowheart LOD0: 656 cards / 40,090 tris -> 86k strands, 855k LSS segments; silhouette IoU 0.92-0.94.
- GPU bench `tools/hair_lss_bench` (RTX 5090, 3.7M rays from the hair): shadow/AO/GI cards 1.94/1.71/2.99 ms,
  groom 2 mm compacted 0.91/0.74/1.27 ms, refit 0.1 ms, BLAS 60 MB; LOD 1/2 0.71/0.62/1.03 ms.
- Prototype (key off, live A/B after a startup with it on): LSS groom TLAS traced by the secondary queries
  (constant 1020), card kept for primary; no ABI bump (TLAS address in scene stamp 48..63). Docs:
  RTX_HAIR_GROOM_EXPERIMENT.md, RTX_HAIR_INTEGRATION.md, RTX_HAIR_ROADMAP.md.
- Batch tool `tools.hair_groom.batch` (all CHAR_Hair styles, validation, incremental): 9 companions 7 pass,
  2 review. Full run on hold until Shadowheart works in game (user).

## rt_hair_groom live runs 2026-11-03: INVALID twice (no join), fixed offline, READY_FOR_LIVE
- 10:40: device feature and file OK, no hair draw with Shadowheart's LOD0 count: Dialogue1/camp draw LOD1.
  Added LOD1 grooms, radius caps, per-draw join verdict log, warm-nohair phase.
- 11:03: her LOD1 draw (6009 indices, Test Dialogue1) has no host-mapped index buffer. Fix: UV join (asset v2:
  32 corner UVs; the layer reads the same corners from its own soup and matches within 1e-3).
- Check in game: Test Dialogue1, `[hair groom] UV join 6009 indices: matched` and `frame N: K groom instances`
  with K > 0; job tools/live_batches/hair_groom (Dialogue1 only). Build, CTests, dryrun pass.
