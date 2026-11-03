# RTX hair, primary: the visible hair becomes strands

2026-11-03. Roadmap step 6 (docs/RTX_HAIR_ROADMAP.md), started on the user's decision after the trial build:
"i capelli mi sembrano sempre card based", so the strand groom in secondary rays alone is not enough.
Key `rt_hair_groom_primary` (proposed, off, live toggle, needs `rt_hair_groom=1`). Matched styles only; scalp,
accessories and unmatched styles keep the game's draws. Status: design + offline increment 1a.

## 1. Where the strands enter the frame (decision)
The game draws CHAR_Hair cards in four camera passes (FRAME_MAP, NATIVE_HAIR "Native source"):
depth prepass (native Depth FS, writes a fragment depth), G-buffer (Deferred FS, 5 targets), motion vectors
(Velocity FS, after linear depth) and the shadow cascades. Our RT scene is built from the prepass capture,
and its build (with the groom update, refit and groom TLAS) is recorded right after the prepass
(geocapture `rt_after_capture`), so this frame's strands exist before the G-buffer pass.

Options considered:
- (a) a separate full-screen RT pass after lighting, composited into the scene colour: no depth for post,
  DLSS, fog or transparents, and no background behind the hidden cards. Rejected.
- (b) hide the cards and add a full-screen pass of our own at the G-buffer end: needs our own render pass on
  the game's D24S8 and five targets, plus stencil and material values we do not know. Possible fallback.
- (c) chosen: **replace the fragment shader of the matched hair draws** in the prepass, G-buffer and
  velocity passes with a strand variant (pipeline clone, as `layer/water.cpp` does for the water FS).
  The card triangles become the rasterised *candidate region* (strands lie on the card sheets: every groom
  point is embedded in a card triangle, radius <= 0.35 mm). Per fragment: one camera ray through the pixel
  centre into the groom TLAS, closest strand hit or `discard`. Depth, stencil, blend, render pass, vertex
  shader and descriptor sets stay the game's, so lighting, decals, linear depth, fog, transparents, DoF,
  DLSS SR/RR and DLSS-G see an ordinary opaque surface. The XFB capture of the prepass draw is unchanged
  (the vertex stage is the game's), so the point update keeps working.

## 2. Per-pass variants (all behind the key; a pipeline without a variant draws cards)
- **Prepass (Depth FS).** Ray = camera (camera-relative origin 0, the TLAS's frame) through the fragment's
  camera-relative position (VS output, capture offset 60). Closest opaque groom hit `t_s` against the card
  point's distance `t_c`. Depth: on one camera ray view depth is proportional to distance, so for the
  native perspective depth `D = A + B/w` the strand depth is `D_s = A + (D_c - A) * t_c / t_s`
  (`A = near/(near-far)`, from the recorded near/far, NATIVE_HAIR "Native fragment depth"; orthographic:
  `D_s = D_c + (t_s - t_c) * dD/dt`). Writes SV_Depth. Also writes the hit to a per-pixel record
  (below). No hit: discard. The game's coverage/dither test is not run: strand coverage replaces it.
- **G-buffer (Deferred FS).** Same ray, depth test against the prepass value (the game's compare op).
  Writes RT0 = stereographic view-space normal, RT1 base colour, RT2 = (roughness, specular, backlit,
  class 5 | light-channel bits), RT3 = 12:12 octahedral tangent + scatter, RT4 = the draw's object ID.
  The material values come from the game's bound material descriptors (same pipeline layout); until their
  offsets are confirmed (open item 1) from a per-groom record filled from the CHAR_Hair defaults.
- **Velocity (Velocity FS).** Reads the per-pixel record (instance, segment, parameter along the segment),
  evaluates the point at the previous frame's positions (one previous-points buffer per groom instance,
  roadmap step 5), projects it with the previous view-projection and writes current minus previous pixel
  position with jitter removed, exactly the game's convention (TRACK_B_FG "Motion vectors").
- **Shadow cascades:** cards, unchanged (RT shadows already see strands with `rt_hair_groom`).

Per-pixel record (render resolution, 16 B: groom row, segment, v and t as half, frame tag), written by the
prepass variant, read by the G-buffer and velocity variants. Every fragment of the same pixel computes the
same ray and the same closest hit, so concurrent writes store identical values; a later fragment whose frame
tag matches can skip the trace (cuts the card overdraw cost to about one trace per pixel).

## 3. Shading
Increment 1 uses the game's own hair lighting (class 5, Marschner R/TT/TRT in the tiled lighting and our
`bg3_materials.hlsli`/`bg3_sun.hlsli` twins), now fed by the exact strand tangent and a fibre normal
(the view vector made perpendicular to the tangent). Increment 3 replaces it in our lighting replacement
with the fibre BSDF of `tools/hair_groom/bsdf.py` (Chiang 2016 from the papers, licence-clean) plus dual
scattering, fed by the sun, the local-light lists and our GI; colour from Hair_Color via Chiang eq. 9.

## 4. RR, DLSS-G, DoF
RR guides are built from the G-buffer at its pass end (`layer/ngx.cpp record_guides_at_gbuffer`): strands
there are ordinary pixels (albedo, normal, roughness). Specular hit distance: D-RF's, unchanged. Motion
vectors from the velocity variant; depth from the prepass/G-buffer variants: DLSS-G gets consistent
depth/MV. DoF reads the game's depth: strands are in it. Sub-pixel strands (0.25 mm, sub-pixel beyond about
1 m) alias per frame; the game's per-frame jitter plus DLSS accumulate the coverage like the card dither.
Measure the temporal variance on hair pixels (live metric) before adding more rays per pixel.

## 5. Cost model (bench numbers, RTX 5090)
Groom LOD 0: 0.25 ns per ray on hair (3.7M rays 0.91 ms, shadow-like). Primary rays are coherent, so this is
an upper bound. Close-up head at 1.6M hair pixels: ~0.4 ms prepass trace; the G-buffer and velocity
variants read the record (no trace). Plus refit 0.1 ms per character and 11 MB previous points at LOD 0.
Report per character: prepass variant GPU ms (timestamps around the hair draws), VRAM of previous points.

## 6. Open items (each a small offline check before the layer code)
1. Material bytes of the Deferred CHAR_Hair FS (Hair_Color, roughness, scatter, backlit offsets) from the
   recovered SHD8 sources; and the space of the RT3 hair tangent (the lighting does not rotate it).
2. Depth compare op and stencil of the three hair pipelines (the `[rt-capture] ... depth write/test` log).
3. shaderInt64 for the record address constant (water's precedent: `sharc_device=1`).
4. The fragment's camera-relative position is a VS output in all three passes (capture stream 60).

## 7. Increments and gates
- **1a (offline, this commit):** `shaders/rt/hair_primary.hlsli` (depth along the ray, G-buffer encodings
  inverse to the game's decoders, fibre normal) and its Python twin `tools/hair_groom/primary.py` with a CPU
  reference: perspective primary rays against the groom's capsules -> coverage, depth, tangent, G-buffer
  words. Gates: encode/decode round trips through the game's decoders, depth formula equals the explicit
  projection, Shadowheart reference images (CTest `hair_groom_test`).
- **1b (offline):** the three FS variants (DXC, spirv-val, key-off identity of every ordinary request),
  the GPU bench in a primary mode against the 1a CPU reference (coverage IoU, depth and tangent error).
- **1c (layer):** variant pipelines for matched hair draws, per-pixel record, key; live job Dialogue1:
  groom primary on/off in one run, full-resolution PNG pairs, prepass GPU ms, hair temporal variance.
- **2:** previous points + velocity variant. **3:** fibre BSDF in our lighting. **4:** DoF/RR post check
  with the pending `rr_post_temporal` fix. **5:** LOD by screen size (exists), ms per character, VRAM.
