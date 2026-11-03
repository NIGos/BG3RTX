# Card-to-strand groom experiment (Shadowheart hair)

2026-11-03, offline (no game, runtime, layer shader, ABI or settings change). CTest `hair_groom_test`.
`python -m tools.hair_groom --gltf <LSLib export of HAIR_HUM_F_Shadowheart_Spring.GR2> --out <new dir>`.

**Verdict: approximate.** The strands reproduce the cards' silhouette, density and root/tip layout,
but this is not a real groom: short pieces lying on the card sheets, no volume between sheets.
Usable for RT secondary rays (shadows, GI, reflections); not for strand simulation or close-ups.

## Method
- Input: LOD0 hair (33,513 vertices, 40,090 triangles; RTX_HAIR_FEASIBILITY.md) and its CHAR_Hair
  mask `HAIR_Shadowheart_Evil_MSKA.DDS` (2048^2 DXT5, Textures.pak, read-only). A = coverage,
  B = root (0) to tip (1), R = per-strand ID.
- 656 cards (connected strips); each card's UV0 covers exactly one tile of an 8x2 atlas. Rows run
  root to tip: row vs B correlation 0.89..0.96 in all 16 tiles.
- Per tile, strands are traced in texture space: covered runs per row (alpha >= 0.5), clumps split
  by the median painted width, rows linked by slope-predicted nearest centre, pieces cut at crossings
  stitched back. They redraw 98.4..100% of the covered texels; binarising keeps the alpha mass (1.004).
- Each sample (u, v) is located in its card's UV triangles: barycentrics give the 3-D point, bone
  weights (4 largest) and radius (painted width through the UV->world Jacobian). Every point keeps
  (triangle, barycentrics), so at runtime it can follow the layer's captured deformed card vertices
  (spring physics included) without its own skinning.

## UV consistency
- No UV overlap inside a card (0 of 943k samples), no mirrored winding, 2 cards straddle a tile edge;
  22 of 656 cards (3.4%) have the root farther from the head than the tip (bun and braid loops).
- Layered cards: 19.5% of strand points have another card's point within 0.5 mm; 3,892 strands (4.5%)
  are >80% duplicated by a stacked card on the same tile. Kept: they are on screen too.

## Numbers (one character)
| | Cards now | Groom, 2 mm segments | 4 mm | 8 mm |
|---|---:|---:|---:|---:|
| Primitives | 40,090 tris | 856,802 segments | 451,094 | 248,889 |
| Points / vertices | 33,513 | 942,998 | 537,287 | 335,082 |
- 86,196 strands, length p5/p50/p95 2/14/71 mm; radius 0.054/0.125/0.35 mm (clumps, ~3x a hair); ~6 s/style.
- Memory at 2 mm: LSS positions+radii 15.1 MB (16 B/point), indices 3.4 MB, embedding 11 MB; cards
  today 2.9 MB captured vertices (88 B) + 0.5 MB indices.
- DOTS fallback (4 triangles/segment) not viable.

## GPU cost gate (2026-11-03, RTX 5090, driver 621.01, standalone device, no game)
`tools/hair_lss_bench.cpp` + `.hlsl` (CMake option `BG3RTX_HAIR_LSS_BENCH`; input from
`python -m tools.hair_groom.bench_data`). 3,686,400 rays (2560x1440) per batch, all starting on random
card points: worst case, every pixel's ray leaves the hair. Same rays for every scene; median of 9.
| ms per batch | shadow | AO 0.5 m | GI 10 m | BLAS MB | refit | frame update |
|---|---:|---:|---:|---:|---:|---:|
| Cards, 3-sample candidate test | 1.83 | 1.64 | 2.99 | 2.3 | 0.104 | 0.108 |
| Cards opaque (no alpha, floor) | 0.60 | 0.60 | 1.31 | 2.3 | 0.100 | 0.103 |
| Groom 2 mm (857k LSS) | **0.90** | **0.75** | **1.28** | 73.0 | 0.104 | 0.115 |
| Groom 4 mm (451k) | 1.12 | 0.93 | 1.66 | 38.4 | 0.055 | 0.061 |
| Groom 8 mm (249k) | 1.74 | 1.52 | 2.99 | 21.2 | 0.035 | 0.041 |
- Frame update = card deform + embedding point update (0.008 ms) + refit. Full LSS build 1.3 ms
  (fast-build 1.1 ms): refit is the per-frame path.
- 2 mm traces 2.0..2.3x faster than the card candidate test; longer segments trace slower (looser
  bounds). Hit share: groom 0.74..0.80, cards with alpha 0.73..0.77 (same coverage), opaque cards 0.90+.
- Plain `RayQuery` with opaque LSS needs no new SPIR-V instruction for hit/miss and distance; the
  device needs the NV extension (+ `VK_KHR_ray_tracing_pipeline`) and the `linearSweptSpheres` feature.
- Keep rule (< 1 ms over no-hair): cost scales with the share of rays that reach the hair. At 25% of
  the screen with one shadow + AO + GI ray each: groom ~0.7 ms + 0.1 ms refit vs cards ~1.6 ms. The
  game's card path (procedural AABBs + intersection) costs more than this triangle stand-in.
- Cost: 73 MB BLAS per character at 2 mm (38 MB at 4 mm); 4 mm is the VRAM-saving option.

## Visual check (CPU renderer `tools/hair_groom/render.py`; left cards, right groom)
Same camera, light, opacity shadow map, fibre shading, ellipsoid head; cards: mask alpha, strands: coverage.
![front](img/hair_groom/front.jpg) ![side](img/hair_groom/side.jpg) ![backlit](img/hair_groom/backlit.jpg) ![crown](img/hair_groom/crown_zoom.jpg)
- Silhouette IoU 0.92 / 0.94 / 0.94 (front/side/back); coverage +1..+4 points; radiance +1..+9%,
  backlit -11% (thin tips transmit, brighter rim, darker sheet interiors). Crown close-up coverage
  0.98 -> 0.91: stacked cards repeat the same strands, which then overlap instead of adding up.
- Same partings, bangs, braid and bun; continuous fibres instead of texel steps; the card-gap dashes
  (side view) stay, as they are in the asset; no strands between sheets; fragment tips add some frizz.

## Quality options (converter flags; same render and bench as above)
- `--dedupe`: true copies (>80% of a strand within 0.5 mm of one strand of a stacked card) are only
  529 strands (0.6%); the 4.5% above are sheets touching many strands, not copies. Removing the 529
  changes no view (coverage and IoU equal to 3 decimals) and no cost. Kept on.
- Volume between sheets: the asset's only depth data is G x PixelDepthOffset (1 mm; G mean 0.30 on
  91% of strands), a raster depth trick, not geometry. As an inward offset (`--depth-offset 0.001`)
  it moves strands 0.3 mm, fills no gap and costs 2 IoU points (strands sink into the head proxy).
  Off; no volume is invented.
- `--max-fibre-diameter 0.00018` (largest human hair, 0.18 mm): painted clumps split into side-by-side
  fibres of the same total width. 2.44M segments at 2 mm; hit share -2%; trace 1.36/1.06/1.89 ms
  (+50%), refit 0.48 ms, BLAS 208 MB per character. Not worth it at game resolution (a fibre is
  sub-pixel either way): off, painted clumps stay as RT proxies (radius p50 0.125 mm).

## Optimisation sweep (same bench, dedupe on; ms shadow/AO/GI per 3.7M-ray batch)
User rule 2026-11-03: hair may cost more than 1 ms, but must be as cheap as possible. Cards with the
3-sample test 1.94/1.71/2.99 in the same run; opaque cards (floor) 0.59/0.58/1.26.
| Groom | Segments | Trace | BLAS MB (compacted) | Refit ms |
|---|---:|---|---:|---:|
| 1 mm | 1.65M | 0.80/0.69/1.11 | 140 (115) | 0.38 |
| 1.5 mm | 1.12M | 0.88/0.72/1.19 | 95 (79) | 0.13..0.22 |
| **2 mm** | 855k | 0.91/0.74/1.27 | 73 (**60**) | 0.10..0.12 |
| 3 mm | 587k | 1.01/0.85/1.50 | 50 (41) | 0.07 |
| 4 mm | 450k | 1.11..1.16/0.94..1.01/1.66..1.77 | 38 (32) | 0.05 |
| 2 mm, LOD keep 1/2 (r x 2) | 429k | 0.71/0.62/1.03 | 37 (30) | 0.05 |
| 2 mm, LOD keep 1/4 (r x 4) | 215k | 0.63/0.61/0.92 | 18 (15) | 0.03 |
- Compaction (ALLOW_COMPACTION, copy 0.09 ms once): -17% memory, same trace and refit. Keep.
- `ALLOW_UPDATE` itself costs ~15% trace (a FAST_TRACE build without it: 0.78/0.68/1.11), but a
  per-frame rebuild costs 1.3 ms: refit wins. After a 2 cm sway, refit traces +0..5% vs a rebuild:
  rebuild only on large pose changes. End caps NONE vs CHAINED (same flags): -0..3%, hit share
  -0.1%: not worth it, CHAINED kept.
- LOD (`convert.thin`): keep a fraction of strands, widen radii by its inverse so projected area is
  kept. Render at 2 mm pixels (character a few metres away): IoU vs cards 0.92 (full), 0.91 (1/2),
  0.89 (1/4); mean radiance within 0.5% (1/2) and -4% (1/4). Hit share unchanged (0.79..0.80).
- Proposed policy: 2 mm compacted for characters covering more than ~10% of the screen, 1/2 below,
  1/4 for small ones, cards beyond; refit only characters in the view or shadow frustum.
  Worst case per near character (every ray of three full-screen batches from its hair): 2.9 ms
  trace + 0.12 ms refit, against 6.6 ms for the card stand-in (the in-game card path costs more).

## CPU converter optimisation (2026-11-03)

The numpy-only converter now batches mapped runs and their exact medians, UV search bounds and
triangle Jacobians. Dedupe uses scalar pair keys, filters same-card pairs early and looks up occupied
neighbour cells once. Pair order, radii, defaults, thresholds and the groom format are preserved.

`tools/hair_groom/speed_check.py <inputs dir>` compares every Groom field and the dropped count with
the saved real-style references. All three styles are bit-identical (max absolute/relative diff 0):

| Style | Convert seconds, old -> new | Dedupe seconds, old -> new | Total speedup |
|---|---:|---:|---:|
| Shadowheart | 8.4 -> 3.253 | 5.9 -> 1.431 | 3.05x |
| Gale beard | 4.8 -> 2.824 | 4.0 -> 0.898 | 2.36x |
| Halsin | 12.6 -> 4.475 | 12.5 -> 2.834 | 3.43x |

Old timings are the supplied baseline measurements. New timings ran at BelowNormal priority;
`python.exe` replaced the unavailable `py -3` launcher. The independent final
`tests/hair_groom_test.py` run exits 0 and prints `all passed` in 5.06 s;
the identity/timing checker exits 0, including the Halsin >= 3x gate. No game or GPU tools were run.

## Production path
Integration design: docs/RTX_HAIR_INTEGRATION.md.

## Licence
No RTXCR/NVIDIA SDK code or data used; LSS layout from the Khronos extension spec. Later RTXCR code or
hair BSDF: NVIDIA RTX SDK licence (see feasibility doc). Nothing downloaded: numpy 2.4.3 (BSD-3), Pillow 12.3.0 (MIT-CMU), scipy 1.17.1 (BSD-3) were already installed; the GR2 decode is the existing LSLib v1.20.4 export.
Batch tool (tools/hair_groom/batch.py): LSLib v1.20.4 ExportTool package (github.com/Norbyte/lslib, its LICENSE) reused from the
local m2-foundry copy, with its granny2.dll (RAD Game Tools, ships with that package; never redistributed by BG3RTX).
