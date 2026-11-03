## Strand hair (rt_hair_groom): experiment, prototype, live (branch claude/rtx-hair-groom-33655b, 2026-11-03)
- Converter `tools/hair_groom/` (CTest `hair_groom_test`): Shadowheart 656 cards -> 86k strands, IoU 0.92-0.94.
  Bench (RTX 5090, 3.7M rays): groom 0.91/0.74/1.27 ms shadow/AO/GI vs cards 1.94/1.71/2.99, refit 0.1 ms.
- Secondary-ray prototype (constant 1020, TLAS address in scene stamp 48..63, no ABI bump); UV join for
  host-invisible index buffers; groom pool <= 1/4 of the mesh pool, per-LOD upload, LRU eviction.
- Batch tool `tools.hair_groom.batch`: 9 companions, 7 pass, 2 review; full run on hold (user).
- Fibre BSDF offline (`tools/hair_groom/bsdf.py`, Chiang 2016): furnace 1.00; today's GI hit 3-6x too dark;
  single-scatter Chiang is pale (needs dual scattering).

## rt_hair_groom live hg-20261103e (Test Dialogue1): works, cheaper; gates fixed after two INVALID runs
- 5 groom instances, refit +0.57 ms; rt 9.07 vs 10.2-10.57 ms, gi 12.51 vs 14.1-14.3 ms (groom vs cards).
- Character energy within noise; no visible change in the overcast shot (user: "sempre card based").
- Gates: participation in cost-groom only; freeze judged inside the plan. Job: tools/live_batches/hair_groom.

## Primary strand hair, increments 1a-1c (offline, 2026-11-03; user: "si facciamolo")
- Design docs/RTX_HAIR_PRIMARY.md: the matched static hair draws run strand variants (pipeline clones):
  prepass without depth writes, G-buffer `shaders/rt/hair_primary_gbuffer.hlsl` (camera ray into this frame's
  groom TLAS, discard on miss, strand depth/normal/tangent, native material FS 42a95e17 at the strand's UV).
- 1a: `hair_primary.hlsli` + twin `tools/hair_groom/primary.py`: depth on the card ray exact (< 1e-9),
  encodings through the game's decoders (normal < 0.2 deg, tangent < 0.1 deg), CPU tracer.
- 1b: GPU check `hair_primary_gpu_check` + CTest `hair_primary_gpu_test` (RTX 5090): synthetic IoU 1.000,
  depth rel. 7e-7, normal 0.0004 deg; Shadowheart (tapered radii) IoU 0.995 (report only).
- 1c: key `rt_hair_groom_primary` (off, live, GUI "Strand hair on screen", needs rt_hair_groom);
  `layer/hair_primary.cpp` (variants, draw swap, header after the groom TLAS), per-point UVs in the groom
  update. Requires LSS, shaderInt64, fragmentStoresAndAtomics. Check in game: `[hair primary]` lines
  (header, pipelines, "N prepass and M G-buffer strand draws"). Not run live.
