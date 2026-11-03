# RTX hair groom: integration design (no layer code yet)

2026-11-03. Follows docs/RTX_HAIR_GROOM_EXPERIMENT.md (converter, quality, GPU cost gate).
Decision so far: the converted groom replaces the card hair **in secondary rays only** (sun and local
shadows, AO, GI, reflections). The game's raster cards stay the primary image. Off by default behind
one key (`rt_hair_groom`, proposed), A/B in one run against the current native hair path.

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
- Memory per hair instance: BLAS 73 MB at 2 mm, 38 MB at 4 mm, plus 15 MB / 9 MB points. A party of
  4 at 2 mm ~ 350 MB. Policy: 2 mm for the nearest N characters (dialogue), 4 mm otherwise, cards
  beyond a distance (or for unmatched styles). VRAM budget to confirm on 32 GB.
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
