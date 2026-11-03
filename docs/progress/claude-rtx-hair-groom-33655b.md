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

## rt_hair_groom pool budget and fibre BSDF prototype (2026-11-03)
- Groom pool: at most 1/4 of the mesh pool (min of rt_mesh_pool_max_mb and VRAM cap); only the selected LOD
  uploaded; headers indexed at start, files read on first match; LRU eviction after 120 unused frames.
- Fibre BSDF offline (tools/hair_groom/bsdf.py, Chiang 2016): white furnace 1.00; GI hit today (Lambert)
  3-6x darker than Chiang; single-scatter Chiang is pale, needs dual scattering for the brown (ROADMAP step 4).

## rt_hair_groom live hg-20261103e (Test Dialogue1, 17 grooms): works, cheaper; INVALID on two gate bugs
- 5 groom instances, 1500 matched per 300 frames (LOD 600/0/900), refit +0.57 ms (BLAS 1.04 vs 0.47 ms).
- Cost groom vs cards: rt 9.07 vs 10.2-10.57 ms, gi 12.51 vs 14.1-14.3 ms; no hair 7.14 / 9.08 ms.
- Character energy on vs off +0.17% / +2.83% (off spread 1.38%); overcast shot, no visible change beyond animation.
- Gates fixed: participation only in cost-groom (8 s shots < 300 frames); freeze judged inside the plan
  (the 1088 ms startup burst was the vt_alpha mask load). The old log passes the new gates.

## Primary strand hair, increment 1a (offline, 2026-11-03; user: "si facciamolo")
- Design docs/RTX_HAIR_PRIMARY.md: strands replace the matched cards inside the game's own hair draws
  (prepass, G-buffer, velocity) by FS variants (pipeline clone like water); one camera ray per card fragment
  into the groom TLAS, discard on miss; depth, stencil, lighting, MV, DLSS and post stay the game's.
- `shaders/rt/hair_primary.hlsli` + twin `tools/hair_groom/primary.py`: strand depth on the card ray
  (exact vs projection, rel. error < 1e-9), RT0/RT3/RT2.w encodings through the game's decoders
  (normal < 0.2 deg in RG16F, tangent < 0.1 deg), CPU primary tracer (Shadowheart 512 px, 3.6 s).
  DXC + spirv-val pass; no request uses it yet (variants delta 0). Next: 1b FS variants + GPU bench vs CPU.
