# RTX hair groom: integration design (no layer code yet)

2026-11-03. Follows docs/RTX_HAIR_GROOM_EXPERIMENT.md (converter, quality, GPU cost gate).
Decision so far: the converted groom replaces the card hair **in secondary rays only** (sun and local
shadows, AO, GI, reflections). The game's raster cards stay the primary image. Off by default behind
one key (`rt_hair_groom`, proposed), A/B in one run against the current native hair path.

## Prototype status (2026-11-03, branch claude/rtx-hair-groom-33655b, key off)
Implemented as below, with three simplifications against the design:
- No new set-1 binding and no ABI bump: the groom TLAS device address, its instance count and first InstInfo row
  go into scene stamp bytes 48..63; shaders turn the address into a TLAS with `OpConvertUToAccelerationStructureKHR`
  (uint2, no Int64; verified in the bench with zero validation errors). Groom rows follow the TLAS's InstInfo rows.
- The matched card instance is skipped in software in the three secondary queries (InstInfo bit 13 before
  `hair_intersect`), not by instance mask: masks are shared with the cascade and current-sun passes. Its AABBs are
  still traversed. A groom hit is reported as a procedural hair hit on the card triangle of the segment (a
  segment -> triangle table in the mesh pool), so hit material, normals and occluder kind are the card path's.
- Join by an index-only content ID (`common/hair_groom_asset.h`): hair draws have no host-mapped float3 positions,
  so `rt_mesh_content_id` v1 is never computed for them. Needs a host-mapped index buffer (logged when not).
Layer: `layer/hair_groom.cpp` (device feature, groom files), `layer/bvhsplit.cpp` (`groom_*`: pool upload,
screen-size LOD 0/1/2 with 30-frame hysteresis, `rt/hair_groom_update.hlsl`, LSS BLAS refit, compaction after the
first build, groom TLAS per scene slot). Shaders: `rt_common.hlsli` constant 1020 (`rt_groom_trace`). Core:
`core/gi.cpp` constant 1020 on gi/ files with its own pipeline slot (512). Key `rt_hair_groom` (Developer GUI
"Strand hair in ray-traced lighting"): live on/off; optional supported LSS hardware
is negotiated at device creation even with the effect off. Existing device retry
retains ordinary RT if the optional feature fails. Asset:
`python -m tools.hair_groom.asset`. Live job prepared: `tools/live_batches/hair_groom` (not queued).

## Why secondary only
- Primary hair stays the game's (its shading, dither, TAA/DLSS inputs and motion vectors unchanged).
- Secondary rays are where the card path costs (+2.3..4.5 ms rt, +3.4..6.9 ms GI measured live) and
  where the groom wins: 2.0..2.3x cheaper per ray in the bench, opaque, no any-hit, no texture reads.
- Shadows and GI then see fibres with the painted coverage instead of alpha-tested ribbons.

## 1. Visual-to-capture join (which captured hair draw is which style)
- Offline, per CHAR_Hair visual (869 visuals / 872 LOD0 rows in mesh_materials v2: hair, beards,
  NPC styles): decode the GR2 (LSLib on the user's PC: Larian-derived data is generated locally),
  convert (~6 s each, ~1.5 h total, parallel), and record for each LOD0 hair object:
  `rt_mesh_content_id` v1 (tools/assets/mesh_content_id.md: span, index count, 32 sampled
  index/position words, FNV-1a 64) computed from the **bind-pose** positions and native index order,
  plus vertex count, index count and a bind-pose bounds check.
- Runtime: the layer already hashes captured meshes with `rt_mesh_content_id=1` (host-mapped raw
  float3 sources). A hair draw whose source vertex/index buffers hash to a groom's ID, with equal
  counts, selects that groom. The deformed positions come from the XFB capture, not from the hash.
- Prerequisites, not done: (a) confirm hair source buffers are host-mapped float3 at the 88-byte
  stride the content ID can read (else extend the ID to the hair stream layout); (b) confirm the
  LSLib glTF export keeps native vertex and index order (compare against the raw GR2 arrays); the
  converter's embedding indexes triangles in that order. Any mismatch: no groom, card path stays.
- Collisions: the v1 hash samples 32 corners; require full index-count and vertex-count equality,
  and reject an ID claimed by two grooms (fall back to cards for both).

## 2. Per-frame point update
- Data per style (data/assetdb/hair_grooms/<content id>.bin, added, never overwriting):
  per point (triangle uint32, barycentrics 2 x float16 or float32, radius float16), segment index
  list (uint32), strand offsets. Shadowheart at 2 mm: 943k points, 855k segments: ~11..15 MB.
- Each frame, per matched hair instance: one compute dispatch reads the instance's world-space
  triangles from the existing XFB -> world soup (shaders/rt/xfb_to_world.hlsl output) and writes
  `point = bary . (v0, v1, v2)` into the instance's LSS position buffer; radii are static.
  Then an LSS BLAS refit (ALLOW_UPDATE, PREFER_FAST_TRACE). Bench: 0.008 ms update + 0.104 ms refit
  per character at 2 mm (0.061 ms total at 4 mm). Rebuild (1.3 ms) only when the draw re-appears
  or after N refits if trace cost drifts (measure; spring motion is small relative to strand length).
- Memory per hair instance (compacted BLAS + points): 60 + 15 MB at 2 mm, 30 + 8 MB with LOD 1/2,
  15 + 4 MB with LOD 1/4. Policy (optimisation sweep in the experiment doc): 2 mm for characters over
  ~10% of the screen, LOD 1/2 below, 1/4 for small ones, cards beyond or for unmatched styles. LOD
  levels are separate BLASes built once per instance and switched with hysteresis. Refit only
  instances inside the view or a shadow frustum; rebuild only after large pose changes.
- Accessories (separate Base_AlphaTest_VT material) and the scalp stay as they are.

## 3. Ray-query consumers (shaders/rt/rt_common.hlsli)
Four query loops: `rt_visibility` (sun/local shadows), `rt_closest_debug` (primary/debug/origin),
`rt_closest_hit` (GI and reflection hits with material), `rt_closest` (distance only: AO etc.).
- TLAS: the groom BLAS is a separate instance, own InstInfo kind and instance-mask bit. The procedural
  card instance of a matched style keeps a bit used only by `rt_closest_debug` (primary checks
  must agree with the raster cards). The other three consumers use a mask that selects the groom
  and excludes those cards. Unmatched styles keep today's procedural path in all four.
- Hit handling: the groom is opaque, so traversal commits LSS hits by itself (bench: plain HLSL
  RayQuery, hit share as expected). `rt_visibility`/`rt_closest`: nothing new beyond the mask.
  `rt_closest_hit`: for a groom instance, read points[i], points[i+1] of the committed primitive
  from the instance's position buffer (bound with the soup bindings) to get the fibre tangent and
  the hit's root parameter; no LSS-specific SPIR-V needed. Check first that the committed type of
  an LSS hit is reported as triangle in DXC's SPIR-V (one probe case).
- Origins on hair: a shadow/AO ray leaving a raster hair pixel starts on its card, where a groom
  fibre lies (radius up to 0.35 mm). Start such rays at tmin = 2 x max radius of the style
  (~0.7 mm, derived from the data) or skip hits on the own strand segment; measure self-shadowing
  on a static camera (energy rule: key on vs off mean within noise on non-hair pixels).
- Variants: one specialization constant (SpecId, not a define) gates the mask/kind branch;
  key off must compile to byte-identical SPIR-V for all ordinary requests (shader_identity).

## 4. Hair BSDF (GI and reflection hits on hair; shadows stay opaque)
- First step: opaque fibres with the existing hair colour (CHAR_Hair `Hair_Color` 0.136, 0.082,
  0.043, root/tip from the mask channel) and a Kajiya-Kay-like diffuse lobe, as the card path today.
- Physical upgrade: a far-field fibre BSDF (Marschner 2003 / d'Eon 2011 / Chiang 2016 near-field) written
  from the papers. pbrt-v3's hair BSDF is BSD-2 licensed (portable with its notice). RTXCR's hair
  BSDF is under the NVIDIA RTX SDK licence (notices, redistribution terms): use only after reading it
  and recording the notice in docs/THIRD_PARTY / package licences. No RTXCR code is used today.

- Offline prototype (2026-11-03): `tools/hair_groom/bsdf.py` (Chiang R/TT/TRT + residual, d'Eon M_p, own code
from the papers), the game's camera-pixel hair (bg3_hair_spec + bg3_hair_diff) ported for comparison, the
current GI hit (Lambert with Hair_Color on the round fibre); `python -m tools.hair_groom.bsdf_compare`.
CHAR_Hair defaults: Hair_Color 0.136/0.082/0.043, Roughness 0.41, Scatter 0.30, HairBacklit 0.40.
- Energy: white furnace Chiang 1.00 (beta 0.2/0.41/0.7, within 4 standard errors), Lambert fibre 1.00,
  game 0.89. CTest `hair_groom_test` (test_bsdf).
- Single-fibre albedo with Hair_Color: hit 0.136/0.082/0.043, game 0.33/0.27/0.21, Chiang 0.79/0.69/0.57
  (mostly TT, forward). Colour check: a semi-infinite slab of random Chiang fibres (multiple scattering,
  random walk) reflects 0.098/0.060/0.034 = 0.72/0.73/0.79 x Hair_Color: hue kept, 25% darker.
- Shadowheart render (LOD 1/2, single scattering + opacity shadows), mean hair luminance front / backlit:
  hit 0.024 / 0.011, game 0.046 / 0.036, Chiang 0.086 / 0.139. Images: docs/img/hair_groom/bsdf_*.jpg.
- Finding: with one bounce Chiang hair is pale grey-beige (each fibre transmits 70% of the light); the brown
  appears only with the multiple scattering between strands. So the shader integration needs dual scattering
  (above) or the GI path through hair; a single-scatter Chiang alone would look washed out. Today's GI hit
  (Lambert) is 3-6x too dark and has no forward scattering.
- Coloured transmission through hair in shadows (TT) would need non-opaque LSS + any-hit: not planned;
  opaque fibres at real coverage are the physically closer default than today's ribbons.

## 5. Fallback
- Feature check at device creation: `VK_NV_ray_tracing_linear_swept_spheres` (requires
  `VK_KHR_ray_tracing_pipeline` enabled too, which the game may not: the layer adds both) and the
  `linearSweptSpheres` feature. Missing, failed BLAS build, unmatched style, or over budget: the
  style uses today's native hair path (or `rt_hair_gi_cull`). DOTS (4 triangles per segment, 3.4M
  triangles per character) is not a fallback.
- The device-creation change is a layer change (restart). Everything after it lives in core where
  possible (point update, masks, shaders); the groom BLAS build sits next to bvhsplit in the layer.

## 6. Motion vectors and RR guidance
- Primary hair is raster: the game's motion vectors, depth and RR guides are unchanged.
- Secondary effects of the groom follow the captured deformation in the same frame, like every other
  captured mesh; nothing new for the denoiser. Hair-to-skin shadows will be finer (strand scale):
  check RR stability on a moving character (Test Dialogue1, S8).
- If the groom ever replaces primary hair: previous points = bary . previous frame's soup triangles
  (keep one frame of the matched instance's triangles) give per-point motion; RR then also needs
  hair-specific guides (specular albedo, roughness). Not in scope.

## 7. ABI and keys
- One ABI bump: InstInfo kind for groom instances, a binding for groom position buffers, the groom
  instance mask. Bundle it with the first layer step; rebuild both DLLs.
- Keys: `rt_hair_groom` (0/1), `rt_hair_groom_step_mm` (2/4), both in common/settings.h and the GUI,
  off in presets until the live gate passes.

## 8. Gates and order
1. Offline: decode + convert all CHAR_Hair visuals; content-ID join table; unit tests (done for the
   converter). Verify LSLib order vs raw GR2 on Shadowheart.
2. Probe (windowless, like native_hair_gpu_probe): the four consumers on a tiny LSS + card scene,
   committed type, masks, origin offset, zero validation errors.
3. Layer: device feature, groom BLAS per matched instance, point update, TLAS masks. Log matched
   styles per scene.
4. Live, user-authorized, one run per scene (S8, Test Dialogue1): key toggled in-run; gates: native
   hair total < 1 ms over no-hair (GPU ms lines), energy key on vs off within noise on a static
   camera, hair/skin shadows and backlight inspected. Predicted: rt and GI hair overhead drop by
   about half (bench ratio 2.0..2.3x on hair rays) plus 0.1..0.4 ms refit for the visible characters.
Estimate: steps 1..2 about a week, step 3 one to two weeks, before the first live run.

## User trial GUI (2026-11-03)
The groom toggle is visible in Experimental with `dev=0`, off in every preset.
Native hair + scalp is a separate live prerequisite; Full RT capture reserves
88-byte vertices at startup even with coverage off (see NATIVE_HAIR.md).
Local groom files and LSS support remain required; unmatched hair keeps cards.
Dialogue1 reference: groom refit +0.57 ms, RT -1.1..1.5 ms and GI -1.6..1.8 ms
versus native cards (RTX 5090). These are prior measurements, not a new gate.
This GUI/startup preparation has offline checks only; no game was launched.
Shader request delta 0; ABI unchanged; no preset/default promotion.
