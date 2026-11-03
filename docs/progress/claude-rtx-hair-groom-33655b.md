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
  update. Requires LSS, shaderInt64, fragmentStoresAndAtomics. Not run live.
- 2: velocity variant `hair_primary_velocity.hlsl` (native FS b70cadc5 form, last frame's strand points,
  +12 B/point). Cost line `[hair primary] strand G-buffer draws GPU X ms/frame` (per-draw timestamps).
- 3a: fibre BSDF in HLSL (`hair_bsdf.hlsli`), GPU vs CPU max rel. error 1.5e-5; not wired yet.
- 3b-1: dual scattering `tools/hair_groom/dual.py` + path-traced per-channel correction: CHAR_Hair colour 1.01-1.02 of a
  path-traced fibre slab (single scattering 0.21-0.40, plain dual 0.70-0.75); held-out colours 0.97-1.04.
- 3b-2a: `hair_dual.hlsli` (generated tables) = `dual.shade_table` on the GPU (max rel. 1.4e-5). A 3-D static
  const HLSL array read shifted fields on the GPU: flattened (check other multi-dimensional tables).
- 3b-2b: key `rt_hair_fibre` (off, live): `gi/hair_fibre.hlsl` after D-LS lights strand pixels with the fibre BSDF
  (sun + unshadow-mapped local lights). Identity 236/237, CTests, GPU twin 6.7e-6. Not run live.
- Live job READY_FOR_LIVE: tools/live_batches/hair_primary (Dialogue1, cards/strands shots at full resolution,
  cost cards/strands/cards; gated on G-buffer and velocity strand draws > 0). Dryrun pass, not run.
- Fix after the user trial (device lost x2, player bald): no groom update for held/uncaptured draws (pool offset read
  as soup offset), every soup read bounded; card hiding gated on the G-buffer variant having run and on GPU-counted
  strand pixels (2 empty frames: cards back for the session). Tests in hair_groom_asset_test. Not run live.
