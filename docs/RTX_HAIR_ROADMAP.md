# RTX hair roadmap: from the strand prototype to a better hair look

2026-11-03, offline plan. Builds on RTX_HAIR_GROOM_EXPERIMENT.md (converter, bench) and RTX_HAIR_INTEGRATION.md
(prototype, key `rt_hair_groom`, off). User rule: physically correct is fine even if it differs from the game.
Costs are from `tools/hair_lss_bench` (RTX 5090, 3.7M rays per batch all leaving the hair: a full-screen
close-up worst case; scale by the share of rays that reach hair). Per near character: groom 2 mm compacted
0.91/0.74/1.27 ms shadow/AO/GI, refit 0.10 ms, BLAS 60 MB + points 11 MB; LOD 1/2: 0.71/0.62/1.03 ms, 30 + 6 MB;
LOD 1/4: 0.63/0.61/0.92 ms, 15 + 3 MB; static arrays 34 MB per hairstyle (mesh pool). Cards (3-sample test):
1.94/1.71/2.99 ms. Status tags: OFFLINE-NOW (no game), NEEDS-LIVE (user-authorized batch).

## Order (visual impact per effort)
| # | Step | Impact | Effort | Can start |
|---|---|---|---|---|
| 1 | Qualify the secondary-ray prototype | medium (shadows, AO, GI, reflections of hair) | days | NEEDS-LIVE (job ready) |
| 2 | Automated batch conversion of every style (first version done) | high (whole cast) | days | OFFLINE-NOW |
| 3 | Converter quality + artist-free validation | medium (fewer frizz tips, gaps) | 2-3 days | OFFLINE-NOW |
| 4 | Fibre BSDF at secondary hits | high in GI/reflections, backlight | 1 week | OFFLINE-NOW, then live |
| 5 | Animation robustness | medium (no lag/pop under motion) | days | mostly NEEDS-LIVE |
| 6 | Primary replacement of the visible hair | very high (the hair you see) | 3-5 weeks | after 1, 4, 5 |

## 1. Secondary rays (prototype, commit 104092ca)
On screen: hair shadows on face/neck/shoulders become fibre-fine instead of ribbon blocks; AO under the hair,
GI bounce and reflections see strands; the visible hair is unchanged. Method: as implemented (groom TLAS,
card skipped in software). Inputs: Shadowheart's .hgrm (generated), host-mapped hair index buffer (join).
Risks: join fails (index buffer device-local: log `[hair groom] draw with N indices`), self-shadow acne at the
card surface (strand lies on the card: tmin derived from 2 x max radius if seen), double shadow where a
groom and an unmatched card overlap. Cost: predicted rt -0.5..-1.5 ms, gi -0.8..-2.0 ms vs cards (card AABBs
still traversed), scene build +0.05..+0.3 ms per character. Gates: live job `tools/live_batches/hair_groom`
(groom instances > 0, no background build in judged windows, character float energy ON >= OFF - spread,
cost off/groom/off/no-hair). Offline already passed: build, 130 CTests, key-off identity vs main.
Next optimisation if the gate passes: hide replaced cards by instance mask instead of the software skip
(needs a mask bit free in cascade/current-sun passes) for the full card-cost saving. Deps: none.

## 2. Automated batch conversion and coverage (OFFLINE-NOW; first version done)
One command on the user's PC: `python -m tools.hair_groom.batch --report-dir <dir>` (`--match`, `--limit`,
`--jobs`, `--accept-review`). It enumerates the LOD0 CHAR_Hair objects of the mesh_materials catalogue (872 rows:
hair and beards), resolves each mask, reads GR2 and mask from the paks (read-only), decodes the GR2 with LSLib's
reader (`gr2_dump.ps1`, PowerShell 7, granny2.dll next to LSLib.dll; native vertex/index order, so the index-only
join ID holds; LSLib's glTF exporter fails on some meshes, e.g. Lae'zel, Jaheira), detects the atlas grid,
converts (strands of every tile a card touches, dedupe), validates (silhouette IoU, coverage and radiance vs the
cards in front/side/back renders at 2 mm, root channel, UV winding, reversed cards) and writes pass /
needs-review / reject, a front PNG per style, `summary.md`, `state.json`; assets (3 LODs, named by index ID)
only for pass (review with the flag), add-only. Incremental by pak entry (package, offset, size) + mesh +
converter version; resumable; progress every 30 s; BelowNormal priority.
First run (9 companion styles, 81 s, 3 jobs): 7 pass (Shadowheart, Astarion, Karlach, Lae'zel, Gale, Minthara,
Jaheira: IoU 0.92-0.97), 2 needs review (Gale's beard: IoU 0.76, small mesh at 2 mm pixels; Halsin: 44% of the
cards with the root away from the head). Atlas grids seen: 8x2, 8x4, 9x2, 8x16. All 872: ~27 s per style per
job: ~2.2 h with 3 jobs, ~1.1 h with 6. Next: LOD1 meshes, pubes/fur recipes, beards at finer render pixels, a
review page. Crowd budget: LOD by screen size (done) plus a VRAM cap dropping far characters to LOD 1/4 then
cards (10 close characters at LOD 0 ~710 MB + 34 MB per style). Gates: offline as above (CTest: catalogue
parsing, grid detection); live: matched/unmatched counts in a crowd scene (camp, Lower City). Deps: none.

## 3. Converter quality and validation (OFFLINE-NOW)
On screen: fewer fake tips (strands are 14 mm median pieces: crossings in the painted mask), no doubled
strands, cleaner partings. Method: (a) stitch across card seams (end of a strand on card A to the start on
the adjacent card B when positions and directions agree within the strand radius); (b) longer 2-D tracking
with the mask's R (strand ID) channel to keep identity through crossings; (c) taper radius to 0 over the
last mm at real tips only (B channel near 1), not at fragment ends; (d) keep dedupe (0.6% true copies).
Validation without an artist: silhouette IoU vs cards >= 0.92 (now 0.92-0.94), coverage within the cards'
+-4 points, mean radiance within the off spread, strand length p50 (now 14 mm) and tip count per strand as
tracked metrics; PNG sets front/side/back/backlit/crown (tools/hair_groom/render.py) per style.
Inputs: masks and meshes (paks, read-only). Risks: over-stitching across layered sheets (guard: same tile
and < 2 mm). Cost: none at runtime (same segment count +-5%). Gates: offline metrics above in a CTest on a
fixture style + the PNG review. Deps: none.

## 4. Fibre BSDF (OFFLINE-NOW, then live)
On screen: hair hit by GI/reflection rays gets a coloured specular R lobe and TT/TRT (rim light in backlight,
warm transmission) instead of today's unknown/approximate hair material at secondary hits; with step 6 the
visible hair too. Method: near-field Chiang 2016 / far-field Marschner-d'Eon written from the papers
(longitudinal Gaussian lobes, azimuthal logistic, absorption from colour). Dual scattering (Zinke 2008)
for the many-strand multiple scattering in shadow rays: forward-scattering transmittance and spread from
the strand count along the shadow ray (two scalars per ray), no extra rays. Mapping from CHAR_Hair: Hair_Color
-> absorption sigma_a (Chiang 2016 eq. 9: the colour is the multiple-scattering albedo of a hair volume), Roughness -> beta_m/beta_n,
Scatter / HairBacklit -> TT weight only as a sanity check (physically derived lobes win), cuticle tilt 2-3 deg.
Licence: pbrt-v3 hair is BSD-2 (port with notice) or own code from papers; RTXCR material library only after
reading its NVIDIA RTX SDK licence and recording notices (docs/THIRD_PARTY). Inputs: material parameters
(already in Materials.pak, read-only). Risks: energy: white furnace test must return 1 (no absorption) and
the expected albedo with absorption; brighter hair than the game's (allowed if physical). Cost: +0.1..0.3 ms
per hair hit batch (shading only; traversal unchanged). Gates: offline furnace + lobe tests (CTest) and a
CPU/PT reference render vs the shader on a fixture; live: character energy within spread of the analytic
expectation, backlit shot shows rim (PNG pair). Deps: step 1 (the groom hit carries the fibre tangent).
Prototype 2026-11-03 (RTX_HAIR_INTEGRATION.md 4): energy OK; one-bounce Chiang is pale, needs dual scattering.

## 5. Animation (mostly NEEDS-LIVE)
On screen: strands follow head turns, spring physics and wind exactly as the cards (points are embedded in the
captured deformed card triangles), no lag or pops at LOD switches. Method: already per-frame refit; add a
rebuild when the bounds grow > 20% over the build-time bounds or after a teleport/cut (refit after 2 cm sway
costs 0-5% trace, measured); LOD switch with a one-frame overlap (old BLAS kept until the new one is built);
previous points kept (one extra buffer, 11 MB at LOD 0) for step 6's motion vectors. Inputs: captured cards
(done). Risks: game culls card triangles (NaN-parked: points keep their last position) -> detached strands;
rebuild cost 1.3 ms spikes. Cost: rebuild 1.1-1.3 ms when triggered, +11 MB with previous points. Gates:
offline synthetic sway test in the bench (trace cost vs rebuild); live: dialogue with head turns, `[hair groom]`
build/refit counts, no visible lag in PNG pairs at motion, scene build GPU ms. Deps: step 1.

## 6. Primary replacement of the visible hair (after 1, 4, 5)
On screen: the hair you see is strands: no alpha-tested card edges, no dither/TAA shimmer (the edge flicker
the user reported), correct parallax, proper backlight. Method options: (a) RT primary: one ray per hair
pixel against the groom TLAS inside the game's hair mask (pixels the card raster covers, dilated by the strand
radius), shade with the fibre BSDF, composite into the game's lighting buffer before post; (b) raster strands
(camera-facing ribbons from the LSS points in a draw we inject): cheaper per pixel, needs a draw hook and
depth writes. Recommendation (a): no new raster hook, same geometry as the RT effects. Must provide: depth
(write the groom hit depth into the game's depth or our own for post), normals (fibre tangent-based shading
normal), motion vectors (current - previous point, step 5), DLSS RR guides (diffuse/specular albedo, roughness,
hit distance, specular motion) and DLSS-G (depth/MV consistent with the composite), alpha for DoF (the game's
DoF reads depth: strands must not punch through it; keep DoF on the game's depth if replacing breaks it).
Scalp and accessories keep their game draws. Fallback: card raster for unmatched styles, missing LSS, VRAM
guard. Inputs: the game's hair G-buffer pass and depth (already captured for native hair), its post chain
order (docs/ARCHITECTURE). Risks: high: G-buffer integration, RR noise on sub-pixel strands (strands ~0.25 mm
are sub-pixel beyond ~1 m: coverage AA needs several rays or a footprint-based coverage estimate), DoF/TAA
interplay, look change. Cost: hair pixels x 1 ray: full-screen close-up at DLSS 0.67 render scale (1.6M px)
~0.6 ms trace + 0.2-0.4 ms shading, 2-4 rays per pixel for coverage AA ~1.5-2.5 ms; typical (hair 10-25% of
the screen) 0.2-0.6 ms. Gates: offline: PT reference vs shader on a fixture (coverage, colour); live: same-run
A/B cards/strands, edge-flicker metric (temporal variance on hair pixels) lower, character energy within the
BSDF expectation, RR/FG artefact review (PNG + short float burst), cost < budget the user sets. Deps: 1, 4, 5.

## Top 3 next steps
1. Live run of `hair_groom` (step 1): proves the join, the energy and the real cost.
2. Batch over all 872 styles on the user's PC (step 2), review the needs-review list; then converter
   stitch/taper (step 3) and an incremental rerun.
3. Fibre BSDF from papers with furnace tests (step 4): needed for both secondary hits and step 6.
