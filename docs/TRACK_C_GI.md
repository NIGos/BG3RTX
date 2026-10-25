# Track C: screen-space GI (v0)

## Where it runs
BG3's direct lighting is a tiled compute pass: about 15 `dispatch_indirect`
permutations of one shader family (e.g. `f469db78`). They write two
R11G11B10F storage images, both in GENERAL layout:

| Binding | Contents |
|---|---|
| `set1/b10` | diffuse radiance; the IBL ambient is already included as `_1687*_2104·AO` |
| `set1/b11` | specular radiance |

The subsurface-scattering blur passes and fog compose read them afterwards, from a
**different command buffer**.

GI is added into the diffuse image at the end of the lighting's command buffer
(`on_cmd_end`), or at the first render pass after the lighting if that comes first
(`on_pass_begin`). This means:
- SSS blur still applies to the bounce light on skin;
- fog, transparency, post and DLSS all see the result.

## How the inputs are found
At each indirect dispatch the core calls `host->query_binding` and recognises the
lighting shader from what is bound. It then reuses the game's own descriptors
directly.

| Binding | Resource |
|---|---|
| `set0/b1` | camera UBO (dynamic). `_m6` is the view-normal transform (row-major); `_m7[3]` are frustum rays. Positions are `ray(uv) * linearDepth` in a camera-relative space. |
| `set0/b4` | viewport UBO: `ab = ((uv - m1) / m0) * 0.5` |
| `set1/b1` | normals, RG16F stereographic (view space) |
| `set1/b2` | albedo |
| `set1/b3` | material: roughness, specular, metal, flags |
| `set1/b5` | R32F linear depth |

Projection back to the screen solves `P = d·(r0 + (r1−r0)a + (r2−r0)b)`. It uses
only the game's own constants, so no matrices are guessed.

## Algorithm (shaders/gi/, hot-reloaded)
1. `gi_trace`: half resolution, with a checkerboard rotation inside each 2x2.
   - Cosine-hemisphere rays (`gi_rays`, default 2), `gi_steps` (default 12) quadratic steps up to `gi_radius·depth`.
   - A hit needs to land within `gi_thickness` relative depth of the surface.
   - On a hit, the ray takes the game's freshly lit diffuse + specular at that pixel: one bounce of direct light.
2. `gi_temporal`: exponential history (`gi_history`, default 0.1) with depth-based rejection. There's no reprojection yet.
3. `gi_composite`: depth-aware 3x3 upsample, then `diffuse += GI · albedo · (1 − metal) · gi_strength`.

## Settings (bg3rtx.ini)
`gi=0/1`, `gi_strength`, `gi_radius`, `gi_thickness`, `gi_rays`, `gi_steps`,
`gi_history`, `gi_debug` (1 = GI irradiance only, 2 = added term ×4), and
`gi_describe=N` (logs the bindings of the first N indirect dispatches).

## Status (2026-10-26)
- Injection works in game: found every frame, no faults, no non-finite values in the dumps. `gi_debug=1` shows plausible warm bounce from the lamps.
- Not yet measured: cost, with the display on and `headless=0`, and an on/off comparison.

## Next
- Reprojection: the motion vectors used by DLSS, or the previous frame's camera UBO copied into our own buffer.
- A world-space radiance cache (SHaRC-style) for rays that leave the screen.
- ReSTIR-style reuse, and denoising by RR (Track A) once available.
- Replace, not just add to, the game's flat IBL ambient where the GI has coverage. This needs the ambient term isolated, possibly through shader replacement.
- Screen-space contact shadows for direct lights. Proper RT shadows belong to Track D.
