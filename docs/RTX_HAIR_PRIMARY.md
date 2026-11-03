# RTX hair, primary: the visible hair becomes strands

2026-11-03. Roadmap step 6 (docs/RTX_HAIR_ROADMAP.md), started on the user's decision after the trial build:
"i capelli mi sembrano sempre card based", so the strand groom in secondary rays alone is not enough.
Key `rt_hair_groom_primary` (experimental, off, restart required, needs `rt_hair_groom=1`). Matched styles only; scalp,
accessories and unmatched styles keep the game's draws. Status: design + offline increment 1a.

Activation is latched at device creation after a reported hot-enable device loss; INI reloads and A/B cannot enable it. The safety gate does not qualify the pending crash fix.

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
- (c) chosen: **replace the fragment shader of the matched hair draws** in the G-buffer and velocity passes
  with a strand variant, and clone the prepass draw without depth writes (pipeline clone, as `layer/water.cpp` does for the water FS).
  The card triangles become the rasterised *candidate region* (strands lie on the card sheets: every groom
  point is embedded in a card triangle, radius <= 0.35 mm). Per fragment: one camera ray through the pixel
  centre into the groom TLAS, closest strand hit or `discard`. Depth, stencil, blend, render pass, vertex
  shader and descriptor sets stay the game's, so lighting, decals, linear depth, fog, transparents, DoF,
  DLSS SR/RR and DLSS-G see an ordinary opaque surface. The XFB capture of the prepass draw is unchanged
  (the vertex stage is the game's), so the point update keeps working.

## 2. Per-pass variants (all behind the key; a pipeline without a variant draws cards)
- **Prepass (Depth FS 5a03e5b6).** This frame's groom TLAS is built from the prepass capture, i.e. after the
  prepass. So the prepass only loses the card depth: a clone with depth writes off (the XFB capture of the
  draw is unchanged). The strands write their depth in the G-buffer pass.
- **G-buffer (Deferred FS 42a95e17, `shaders/rt/hair_primary_gbuffer.hlsl`).** Ray = camera (origin 0 of the
  camera-relative TLAS frame) through the card fragment's point (unprojected with set 0 b2 `m2`). Closest groom
  hit or discard; depth = the hit projected with the same `m2` (the clone tests GREATER_OR_EQUAL with writes).
  Writes RT0 = stereographic view-space fibre normal, RT1/RT2/RT3.w = the native material code evaluated at the
  strand's own UV (card UV at its embedding point; gradients from the card UV), RT3.xyz = view-space strand
  tangent (the native FS writes its tangent in the same space; root-to-tip sign: open item), RT2.w = class 5 |
  the draw's light channel. The draw's own material descriptors are bound (same pipeline layout).
- **Velocity (Velocity FS b70cadc5, `shaders/rt/hair_primary_velocity.hlsl`).** Reads the pixel entry (no
  trace), evaluates the strand point at the same parameter in last frame's points (the groom update copies
  them before writing the new ones; camera-relative to last frame's camera), projects it with set 0 b5 `m2`
  and writes the native form: fragment + jitter minus previous pixel + previous jitter, in pixels. No
  previous points yet: the current point moved with the camera only. Depth test GREATER_OR_EQUAL, no writes.
- **Shadow cascades:** cards, unchanged (RT shadows already see strands with `rt_hair_groom`).

Per-pixel record (render resolution, 16 B: groom k << 24 | segment, v, native depth, frame tag;
common/hair_primary.h), written by the first G-buffer fragment of a pixel, read by the later ones and the
velocity variant. Every fragment of the same pixel computes the
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
Measured live by `[hair primary] strand G-buffer draws GPU X ms/frame` (a BOTTOM_OF_PIPE timestamp pair around
each swapped G-buffer draw, up to 64 per frame, averaged per 300 frames). VRAM: pixel record 16 B per render pixel
(41.7 MB at 2560x1067) plus per point 8 B UV + 12 B previous position (LOD 0 ~19 MB, LOD 1/2 ~10 MB per character).
Live job: tools/live_batches/hair_primary (READY_FOR_LIVE).

## 5a. Cost per character and the crowd budget (increment 5, 2026-11-03, offline)
`python -m tools.hair_groom.cost --crowd`: per groom file and LOD the mesh-pool arrays, the per-instance memory
(points, primary UV and previous points, compacted BLAS at 70 B per segment), refit ms and trace ms per 100k hair
rays, from the RTX 5090 bench constants (Shadowheart 2 mm: BLAS 60 MB, refit 0.10 ms per 857k segments; shadow
rays 0.25 / 0.19 / 0.17 ns at LOD 0 / 1/2 / 1/4). 527 converted styles: LOD 0 segments median 541k
(33k-3.08M), instance memory median 57 MB (3-322 MB), static arrays median 14 MB (1-78 MB), refit median 0.06 ms
(max 0.36 ms). Scenario (2 close, 4 mid, 8 far characters, median style, primary + shadow ray per hair pixel):
0.80 ms and 343 MB at screen-size LODs (3.26 M segments: a 3500 k budget does not bind, 1800 k does).

Coverage (tools.hair_groom.batch, 2026-11-03, 1217 LOD0/LOD1 CHAR_Hair meshes, 4 jobs, 1 h 52 min, 22 s CPU per
style; report captures/hair_groom_batch/summary.md): 519 pass (written), 553 needs review (not written), 145 reject.
LOD 0 meshes: 465 / 296 / 106; LOD 1 meshes: 54 / 257 / 39 (the LOD 1 cards are coarser: IoU and coverage drift).
Min-view silhouette IoU median: pass 0.94 (p10 0.90), review 0.91, reject 0.70. Rejects, first reason: silhouette
IoU 74, root channel not along the atlas rows 48, a zero or invalid radius in a thinned LOD 22 (follow-up), one
converter IndexError. Review, first reason: mirrored UV triangles 203, coverage delta 192, mean radiance 72,
silhouette 25, roots away from the head 50. Written: 517 files, 13.3 GB in data/assetdb/hair_grooms (median
20.6 MB, max 115 MB; LOD arrays are read from disk on first use). `asset.write` now checks every LOD before it
creates the file: the 22 radius rejects of this run left header-only stubs (296 B), which the layer ignores and
logs as malformed.

Game use and the unconverted styles (`python -m tools.hair_groom.reach`: references in the paks: character creation,
character visual slots and the templates using them, hat hair-type maps, item and scenery templates; "unused": no
pak file outside its own visual bank names it, checked by a byte search of every lsf/lsx). After the fixes below
(rerun of the 200 affected styles: all 200 pass): 719 pass (was 519), 372 needs review, 126 reject; 717 files, 17 GB.

| Category | Styles | Pass | Review | Reject | Reachable |
|---|---|---|---|---|---|
| player (character creation) | 432 | 349 | 54 | 29 | yes |
| companion | 6 | 4 | 1 | 1 | yes |
| named NPC (S_ templates) | 211 | 148 | 37 | 26 | yes |
| generic NPC | 25 | 5 | 10 | 10 | yes |
| hat variant (hair under a hat or helmet) | 337 | 154 | 151 | 32 | yes, with that item |
| item or scenery (corpses, fur trims) | 49 | 32 | 17 | 0 | yes |
| character visual no template uses | 3 | 0 | 2 | 1 | no |
| unused | 154 | 27 | 100 | 27 | no |

Of the 698 unconverted styles of the first run, 130 are not reachable (127 unused, 3 CV only); 568 are. 150 of
those are LOD1 meshes whose LOD0 passed (drawn at distance), 55 are beards, brows or fur. Still unconverted and
reachable: 368 (247 LOD0), first reason: coverage 132, silhouette 81, radiance 64, roots away 45, root channel 38.
- Fixed: zero radii (19 reachable): a sample on a card triangle without world area (collapsed vertices) had width 0;
  it now takes its strand's median radius (`convert`, tested). Mirrored UV triangles (181 reachable, the only
  reason): the barycentric embedding is orientation-independent (tested); a fold can at most lose the strands of
  its cards, so the mixed-card share is reviewed against the coverage tolerance (median share 0.8%, p90 2.2%).
- Coverage (+0.08..+0.20, beards and hat hair) and radiance: the gates hold. Two hypotheses tested and rejected on
  34 styles: alpha-weighted strand width (the game's depth pass is a dithered alpha test, coverage = alpha, but the
  masks are nearly binary: deltas unchanged) and the reference mip from the cards' texel size (unchanged). The
  previews show the grooms faithful and the card reference noisy (holes); the next step is a reference that
  averages the game's dithered alpha test over several noise phases.
- Silhouette IoU 0: the leather headwear meshes that use CHAR_Hair for fur trim (no strand atlas). Other low IoU:
  LOD1 cards merge several LOD0 cards (63 of 205).
- Root channel (38): 32 use one mask, HAIR_Afro_B_MSKA, whose curls do not run along the atlas rows: the row tracer
  cannot follow them (needs a 2-D curve tracer).
- Roots away from the head (45 alone): hat hair hanging from a hat rim and buns; root/tip shading only. Trace cost follows the hair pixels and only logarithmically the segments; refit and
memory follow the segments: so the crowd budget is in segments.
`rt_hair_groom_crowd_ksegments` (off; GUI Performance "Strand hair crowd budget": 3500 / 1800 k): each frame the
groom instances of the last frame share the budget, largest on screen first, each at its screen-size LOD or the
first coarser one that fits; one that fits at none keeps its cards (the slot is kept: no rebuild when it fits
again). `hair_groom::plan_crowd` (unit-tested); segment counts per LOD come from the files' LOD headers at index
time. Log: `[hair groom] crowd plan: N instances, budget B k segments: LOD a/b/c, d keep their cards`. Live job:
tools/live_batches/hair_crowd (Dialogue1 + Campsite, cards / RT / on screen / crowd; READY_FOR_LIVE).

## 5b. Fail-safes (after the 2026-11-03 user trial: device lost twice, the player's hair gone)
- Device lost (17:01, 17:04, main ac1380e5): the groom point update read the matched draw's card triangles at its
  `soup_off` also for a *held* draw, whose offset is a mesh-pool snapshot offset: a read at +1.97 GB of a 2 GB soup.
  Now `hair_primary::soup_update_ok` (captured this frame, not held, the whole triangle range inside the soup, the
  groom's triangles <= the captured ones, stride >= 24) gates the update in bvhsplit (the slot is kept, no groom
  instance that frame: cards in every pass), and hair_groom_update.hlsl bounds every read by the draw's triangle
  count and the buffer size (a point outside keeps last frame's position).
- Bald player: the prepass clone dropped the card depth for every matched static draw, also when its G-buffer
  pipeline had no strand variant (another material permutation), whose card then failed its EQUAL depth test.
  `hair_primary::SwapGate`: the G-buffer swaps a groom published twice; the prepass and velocity passes only a draw
  whose G-buffer strand variant ran in the last completed frame. GPU feedback (`rt/hair_primary_count.hlsl`):
  strand pixels per groom of the last frame's pixel record, read back 3 publishes later; 2 counted frames in a row
  with the variant and no strand pixel fail that draw for the session (logged: "its strands drew no pixel"): its
  cards stay. Regression tests: hair_groom_asset_test (the crash's offsets, the no-variant draw, the strikes).
- Device lost after the F8 reload (hv1 18:40, cause not confirmed): a new level reuses Vulkan handles for other
  objects. The native hair FS module map is erased on vkDestroyShaderModule and on reuse (a reused handle could give
  a non-hair pipeline a strand variant). `hair_primary::LoadGuard`: a new G-buffer depth image marks a level load;
  the groom gate, pixel record and feedback reset and strand swaps pause 300 frames. bvhsplit drops the groom join
  verdicts, UV joins and UV readbacks in flight at the same boundary (their identities are built from handles).
  Live bisect: hair_validation hv5 (one F8 per stage: native + scalp, + physical origin, + groom, + primary, 60 s
  each); hv4 repeats the reload under GPU-AV.
- Overlapping grooms (review of f5b339eb): the pixel's closest strand of all grooms is cached once per pixel; a card
  fragment writes it only when it lies on its own card (`hp_on_card`: strand points lie on their card triangles, so
  within (radius bound 10 mm + segment length / 2) / |cos| of the card surface along the ray, plus the pixel's spread).
  The velocity variant applies the same test. Entries are sealed (`hp_seal`: word 3 mixes the tag with the other
  words), so a torn 16-byte store never pairs this frame's tag with an older key.

## 6. Open items
1. Resolved: the native G-buffer FS (captures/alpha_cache/42a95e17ec3adc51.glsl) is ported; its tangent is
   built from the interpolated frame (view space like the normal). Open: the root-to-tip sign of that frame.
2. Depth compare op / dynamic depth state and stencil of the hair pipelines: the clone overrides compare and
   write; a dynamic compare op would need a CmdSetDepthCompareOp around the draw (log it at clone time).
3. shaderInt64 and fragment stores (water's precedent: `sharc_device=1`); refuse the variant otherwise.
4. Instanced hair (FS 0ca5ee93, other bindings) keeps its cards in increment 1.

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
- **2 (done offline):** previous points + velocity variant; `primary.motion_vector` twin (CTest).
  **3a (done offline):** `shaders/rt/hair_bsdf.hlsli` (Chiang 2016 R/TT/TRT + residual, d'Eon M_p, sigma_a from
  the colour, `hb_eval_pixel` = mean over four fibre offsets), GPU vs `bsdf.py` in `hair_primary_gpu_test`
  (16k samples, 4 colour/roughness cases: relative error p99 4e-6, max 1.5e-5).
  **3b-1 (done offline):** `tools/hair_groom/dual.py`, dual scattering (Zinke 2008, paper densities d_f = d_b =
  0.7) from fibre averages integrated over `bsdf.evaluate`, plus a per-channel correction k(colour, roughness) of
  its multiple-scattering part (`TABLE_K`, 8 x 3, `python -m tools.hair_groom.dual --table`). Reference: a
  semi-infinite slab of random fibres, path traced with next-event estimates of every order, flux toward the
  upper hemisphere (the retro direction alone was too noisy: 10-32% between seeds); roughness < 0.35 is clamped
  (the walk does not converge there). Model / reference, CHAR_Hair colour: single scattering 0.21/0.29/0.40,
  dual 0.70/0.73/0.75, corrected 1.02/1.01/1.01; held-out colours corrected 0.97-1.04 (one 1.12 at 18%
  reference spread). A measurement bug found on the way (radiance vs flux: a cos(theta_o) factor) is fixed in
  the estimator. CTest `hair_groom_test` (test_dual).
  **3b-2a (done offline):** `shaders/rt/hair_dual.hlsli` `hd_shade` (fibre averages and k from the generated
  `hair_dual_table.hlsli`, bilinear in (log colour, roughness)) and its twin `dual.shade_table`: GPU vs CPU
  relative error max 1.4e-5 (9000 samples, 0-6 fibres in front); table model vs per-colour Monte Carlo averages
  0.967/0.972/0.981 (CHAR_Hair colour). Found on the way: a `static const float x[8][3][6]` table read shifted
  fields on the GPU (DXC SPIR-V); the table is flat now.
  **3b-2b (done offline, key `rt_hair_fibre`, off, live, needs rt_hair_groom_primary):** `shaders/gi/hair_fibre.hlsl`
  (includes D-LS with LS_NO_MAIN; dispatched by core/gi.cpp right after D-LS with its layout, sets and push
  constants, inside its timed region). Instead of changing D-LS (its key-off identity), a separate pass replaces,
  on strand pixels only, the sun and the local lights without a game shadow map: it subtracts what the buffer
  holds for each (D-LS's c * t, the game's sun term) and adds E * hd_shade_pixel * v. v: the scene without the
  strands (`RT_VIS_NO_GROOM` in rt_common's groom check: preprocessed text of every other request unchanged),
  n: up to 8 groom hits toward the light. Checks: compiles + spirv-val (plain and both variants.txt requests,
  +2), shader identity vs main 236/237 (only hair_groom_update), 134/134 CTests, GPU `hd_shade_pixel` vs
  `dual.shade_table_pixel` max rel. 6.7e-6. Live: tools/live_batches/hair_primary shot-fibre-a / cost-fibre.
  Original design notes: where: D-LS (`shaders/gi/rt_local_shadows.hlsl`) already runs after the tiled
  lighting at full resolution, re-evaluates every local light's exact game term c_i (bg3_light_eval_mat, class
  5 = the game's Marschner) and the sun's (`ls_sun`, bg3_sun_eval_mat), and adds deltas into the game's
  diffuse/specular buffers. For strand pixels (class 5 and a pixel entry with this frame's tag in the hair
  primary record; header address published by the layer under a persist key, no ABI change) a new
  specialization constant adds, per traced light and for the sun:
      delta = E_i * hd_shade(T, V, L_i, 0, n_i, RT1 colour, RT2 roughness) * v_i  -  c_i * s_i
  (T, V from the pixel entry and camera; hb offsets averaged as hb_eval_pixel; n_i = strands crossed by the
  shadow ray to the light: a groom query that counts up to 8 opaque hits by advancing tmin, replacing the
  current any-hit test for these pixels). Energy rule: on a static camera the mean of key-on vs key-off is
  NOT expected equal (the model is physically brighter: CHAR_Hair colour single scattering 0.21-0.40 of the
  path-traced hair; the game's term is in between): the gate is the path-traced expectation, measured on the
  pre-post scene colour of the hair ROI, and the visible hue. Key-off: byte-identical D-LS modules (spec
  constant default 0), checked with shader_identity. Cost: +1 counting query per traced light on strand pixels
  (bench: LSS 0.25 ns per ray) + hd_shade (~200 ALU): estimate +0.1-0.3 ms at a close-up.
- **4:** DoF/RR post check with the pending `rr_post_temporal` fix. **5:** LOD by screen size (exists), ms per
  character, VRAM.
