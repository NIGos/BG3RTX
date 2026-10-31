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

## Status (2026-10-26, v1)
- v1 does two things. Before the lighting, ray-traced visibility is multiplied into BG3's
  AO target, so the game's own IBL ambient (diffuse and specular) is occluded. After the
  lighting, a one-bounce term from last frame's radiance is added, which also gives
  multi-bounce over frames.
- Verified in game. Contact occlusion is visible under pipes and railings and around
  characters, and the mean frame luminance is about 5% lower than with GI off. No
  non-finite values and no faults, including together with Ray Reconstruction (Track A).
- GPU cost, from timestamps: about 0.18 ms at 2 rays / 12 steps and about 0.28 ms at
  4 rays / 16 steps (the new default). Half resolution, 2560x1067 internal.
- Bugs fixed during the bugfix pause:
  - Ray lengths must scale with the camera distance: the frustum rays carry the
    far-plane scale.
  - The GI must never write non-finite values into game buffers.
  - `dumps.json` wrote `inf`/`nan`, which is not valid JSON.

- Temporal reprojection uses the motion vectors the game passes to DLSS (exposed by the NGX hook,
  `host->dlss_motion_vectors`; their pass leaves them in COLOR_ATTACHMENT layout, so they are
  transitioned around the read). Verified with `auto-run.ps1 -Move` (camera pan): no visible ghosting.
- Sun contact shadows refine the game's screen-space sun shadow mask (`contact_shadows`, `cs_*`).

## Validation pass (2026-10-26)
`auto-run.ps1 -Validation` runs the game under the Khronos validation layer, placed below our layer so
it also checks what we record. Comparing message IDs with GI off and on isolated our errors:
- A stale motion-vector image after a level load (render targets are recreated) made the GI barrier
  an invalid handle. Under validation this faulted inside the validation layer's locks and hung the
  game. Fixed two ways: the NGX hook drops a destroyed MV image, and the GI uses MVs only if the
  velocity pass was seen writing them in the current frame.
- `restore_bindings` rebound sets with too few dynamic offsets (7 required, 5 tracked). The tracker
  now records descriptor set layouts, so dynamic offsets are split exactly. Restore replays the game's
  own bind calls verbatim, and `query_binding` resolves dynamic UBO offsets from the layout.
- Barrier access masks were invalid for their stages; they now use MEMORY_READ/WRITE.
- Remaining: one `Threading-MultipleThreads-Write` (`vkDeviceWaitIdle` vs `vkQueueSubmit`) at
  shutdown, related to headless presentation. It is low priority.

## Hybrid ray-traced trace (`gi_rt=1`, Track D phase 3, off by default)
With `rt=1` the trace can use hardware ray queries against the TLAS
(`shaders/gi/gi_trace_rt.hlsl`); temporal, AO fold and composite are unchanged.
Hits that project on screen with matching depth take last frame's radiance;
hidden or off-screen hits count as occlusion with no bounce (`gi_rt_fallback`).
Trace pass 0.175 ms vs 0.248 ms screen-space (4 rays, half res); see
TRACK_D_RT.md section 7.2 for numbers and comparison images.

## SHaRC radiance cache (`sharc=1`, off by default)
A screen-injected world-space hash grid gives hidden and off-screen GI hits (and glossy
reflection fallbacks) a bounce, and lengthens the GI rays to `sharc_gi_radius` (8 m). Design
and status: DESIGN_SOTA_NEXT.md 2.9 (`core/sharc.cpp`, `shaders/gi/sharc_*.hlsl`).

## Qualified hidden-hit direct bounce (`gi_hit_direct=0`, 2026-10-31)

R2 key-on images are **rejected**: visible new dark stippling despite positive GI
counters. The original dedicated shader was mistakenly dispatched through the
contracted D-LS set0 layout/writes, missing GI output/history/MV bindings and the
SHaRC/rebase camera. A source-only fix supplies one typed GI/native-light contract
for both layout creation and pushed writes (19 descriptors, 20 with physical Hair).
Actual compiled SPIR-V descriptor types/counts/push range are checked against it.
The direct/two-surface experimental reference also uses its own typed 18-slot
layout/write contract, including rebase camera 48; ordinary local shadows retain
their existing contraction. Deep reference variants need fresh image validation.
The key stays off pending a new authorized image A/B; compilation and positive
radiance counters alone do not qualify image quality.

Experimental and off in every preset. Requires native-material startup,
`gi_rt=1` and `gi_rt_fallback=0`. Successful screen and SHaRC hits keep priority.
A qualified fresh native VT hit may replace only the zero-radiance fallback with
`bounce += weight * direct_radiance`, preserving exactly the old `occ += weight`.
The existing primary diffuse colour and GI strength are applied once by the
composite. Direct-only light never replaces full probe or cache radiance.
Unknown material, source, visibility or query registration retains the original
fallback. Misses remain misses; no sky/environment constant is invented.

The declared opaque FresnelBlend model uses actual native point/spot inputs.
It includes directional diffuse and specular scattering along the secondary ray;
legacy `gi_bounce_spec=0` still controls only the screen-radiance source. This is a
physical directional addition, not a promise of identical diffuse-only semantics.
The current native froxel provider supports hidden points inside the view
frustum. True offscreen lights, sunlight, projector and emissive terms remain
unsupported. ABI 21 registers the exact tuple and returns its query-owned set
before tracing; startup-off material resources still require restart.

`rt_stats=1` reports candidates, qualified materials, supported direct terms,
nonzero additions and weighted luminance as `GI hit direct`. Cost remains inside
the existing GI trace interval. Live contribution, motion and cost remain
unqualified until same-run A/B. This changes lighting by an accounted addition;
it does not claim unchanged-transport energy preservation or full M4 completion.

## Next
- QA and defaults for the radiance cache (above); its traced update (v2).
- ReSTIR-style reuse.
- Screen-space contact shadows for the direct lights. Real RT shadows belong to Track D.

## Derived depth tolerances (2026-10-27)
Every same-surface depth test now comes from one helper, `shaders/rt/depth_tol.hlsli`
(QUALITY R0). Two depth values of one surface, `lat` render pixels apart on screen, differ
along the view ray by at most

    depth_tol = lat · f · (tan θ + 1) + n · prec(dist)

- `f = dist · pixel angle` (0.835 mrad at 2560x1067, vertical FOV 0.838 rad): 4.17 mm at 5 m,
  25.0 mm at 30 m. `tan θ` is the plane's depth slope (θ between the GEOMETRIC normal and V);
  the `+1` bounds curvature (the sag of any circle over a chord s is ≤ s).
- `prec`: the game rasterises D24, reversed Z, infinite far, near 0.1: one rounding moves z by
  z²/0.1·2⁻²⁵ (+ the R32F's z·2⁻²⁴): 7.7 µm at 5 m, 0.27 mm at 30 m.
- `|N·V|` is moved towards grazing by its own error ε = max(fp16 storage √3·2⁻¹², depth-normal
  2·prec/f): 0.0037 at 5 m, 0.022 at 30 m; floored there (tan θ ≤ 1/ε).
- Jitter (Halton ±0.5 px per axis) adds 0.71 px per jittered depth sample.
- Measured, not derived: TLAS against raster, 0.5% of the distance (TRACK_D_RT 7.1: 99.09% of
  primary rays within the 0.5% bin; the rest is other geometry). Only where no per-pixel TLAS
  reference exists; `rtOrigin.w` is that reference (rt_origin=1).

| Test | lat (px), n | Old | New, 5 m, N·V 1 / 0.2 | New, 30 m, N·V 1 / 0.2 |
|---|---|---|---|---|
| GI on-screen hit, rtOrigin reference | 1.41, 1 | 3% (150 / 900 mm) | 6.4 / 35 mm | 43 / 231 mm |
| same, no reference (rt_origin=0) | + 0.5% | 3% | 31 / 60 mm | 193 / 381 mm |
| rt_origin match (and search window) | 0.71, 1, + 0.5% | 17 / 83 mm (window 67) | 28 / 43 mm | 172 / 266 mm (window was 401) |
| GI temporal, derived part (typ.) | 2.4-3.5, 2 | 3% of z | 11-16 / 60-89 mm | 74-108 / 394-577 mm |
| bilateral σ at 0.71 / 2 / 3.3 px | as is, 2 | 1% (50 / 300 mm) | 3.2 / 9.1 / 15 mm; 18 / 50 / 83 mm | 22 / 61 / 101 mm; 116 / 327 / 543 mm |

- The temporal and bilateral tests compare camera distances (rotation invariant), not view z.
- The temporal test keeps the old 3% as a separate camera-translation term: not derivable in
  that pass (no previous camera position bound). Open: pass |Δcam| per frame.
- The bilateral's centre ×2 is the denoise kernel's shape, unchanged.
- `gi_rt_depth_tol` / `rf_depth_tol` are gone; `gi_rt_depth_k` / `rf_depth_k` (1) multiply the
  derived tolerance, for A/B only (new names so an old 0.03 is not read as a multiplier).
- An aborted GI ray query (candidate cap, `RtHit.aborted`) is a hidden hit under
  `gi_rt_fallback`, weighted by the fade's mean over its unknown distance, never a miss.
- `qa_zero` bit 2 (gi_ao_apply.hlsl) writes the AO 0: the diffuse AND the specular ambient go
  (SO = 0 at AO 0); direct light keeps 1 − m0.y = half (traced local lights get it back with
  ao_direct=1); the GI bounce and rf_so=0 reflections stay.
