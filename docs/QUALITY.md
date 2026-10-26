# Quality audit and the "always implemented at best" rules

Scope: the effects on `main` as of 2026-10-26:
- Track A, RR guides (`shaders/rr_guides.hlsl`, `layer/ngx.cpp`);
- Track C, GI (`core/gi.cpp`, `shaders/gi/*`);
- Track D, RT sun shadows, RT AO, hybrid RT GI and the alpha test (`shaders/rt/*`,
  `layer/rtscene.cpp`, `layer/alphatest.cpp`).

They are compared against best practice: RTXGI/ReSTIR GI, NRD and the DLSS-RR
integration guide (`external/DLSS/doc/DLSS-RR Integration Guide.pdf`, section 3.5
"Sampling and Noise"), SHaRC, and BG3's own lighting (`f469db78`; see
[TRACK_D_RT.md 8.1](TRACK_D_RT.md#81-what-the-game-gives-us-data-inventory)).

This is a code and shader analysis. The game was not run for it. Gains marked
"expected" are estimates and must be confirmed with the A/B protocol in section 3.

## 1. Findings

Priority: **P1** do next (correctness or a large cheap gain), **P2** soon, **P3** polish.

### Correctness of the composition (no double counting)

**Q1 (P1). Bounce radiance is read from last frame's image at this frame's pixel.**
- `gi_trace.hlsl:60` and `gi_trace_rt.hlsl:63` project the hit with the *current*
  camera and read `prevRadiance[qp]`, but `prevRadiance` was written by the previous
  frame's `gi_composite` (`gi_composite.hlsl:28`).
- During camera motion every on-screen hit takes the colour of whatever was at that
  pixel one frame earlier. The depth test uses the *current* depth, so it does not
  catch this: wrong-surface colour bleeds at silhouettes, and the bounce smears
  during pans.
- **Fix:**
  - read at `qp + mv(qp)·mv_scale` (the game's MVs are already bound, b17);
  - store last frame's linear depth in `prevRadiance.a`, and accept the hit only if
    it matches the reprojected depth;
  - otherwise treat it as hidden (the fallback, or the cache once it exists).
- **Expected gain:** a correct bounce in motion; removes colour bleeding on pans. It
  is also a prerequisite for D-RF tier (a).

**Q2 (P1). RT AO is multiplied into the game's SSAO: double occlusion.**
- `gi_ao_apply.hlsl:13` does `ao.x *= lerp(1, vis, gi_ao)`. The game's AO target is
  already its own half-res SSAO (units 88-91), so every contact occluder darkens
  twice: once by SSAO, once by our visibility.
- In addition, `gi_ao=0.7` is not consistent with the bounce estimator. The bounce
  replaces the IBL in occluded directions with hit radiance, but only 70% of the
  IBL is removed there, so 30% of the occluded IBL is counted on top of the bounce.
- The 5% lower mean luminance measured with GI on (TRACK_C) is the sum of the two
  errors, which partly cancel.
- **Fix:**
  - when the visibility is ray-traced (`rt_ao` or `gi_rt`), **replace**:
    `ao.x = vis`;
  - keep `min(ssao, vis)` as an A/B option;
  - set `gi_ao=1` (an energy-consistent "IBL × visibility + bounce").
  - For the screen-space trace, which misses what SSAO also misses, keep the
    multiply, but at full strength and with an A/B.
- **Expected gain:** removes the over-darkening in corners and under objects
  (estimated 5-15% of ambient in creases), and gives a consistent energy balance
  with the bounce.

**Q3 (P1, verify). The game multiplies *direct* light by AO.** **Status: resolved by RT audit fix 4**
(`ao_direct=1`: no AO on ray-traced direct light, exact compensation; AUDIT_RT §4). Answered by D-LS
(TRACK_D 9.1): `set1/b0 _m0.y = 0.5` in gameplay, so the AO fold darkens direct light
by half its occlusion. Kept as the game's art intent.
- `f469db78` line 1451/1452: local-light and sun diffuse are × `mix(1, AO, b0._m0.y)`,
  and specular × `mix(1, specOcc, b0._m0.y)`.
- If `_m0.y > 0`, our AO fold also darkens sun-lit and torch-lit surfaces that
  already have (RT) shadows.
- **Fix:**
  - log `set1/b0._m0.y` (EN1);
  - if it is nonzero, either accept it as the game's art intent, or move the
    ambient visibility out of the AO fold into a delta on the diffuse ambient
    (EN4) and leave the game's SSAO in `.x`.
- **Expected gain:** none if it is 0. If it is not 0, it removes darkened sunlight
  in contact areas.

**Q4 (P1). RR gets a pre-denoised GI: double denoising and correlated input.**
- With `rr=1` the GI is already:
  - temporally accumulated (EMA α = 0.1, an effective history of about 19
    frames);
  - traced on a 2x2 checkerboard (`gi_trace.hlsl:12`; each full-res pixel gets
    a new sample every 4th frame);
  - spatially upsampled (3x3 bilateral);
  
  before it enters the colour that RR denoises.
- The RR guide asks for the opposite: independent per-pixel samples, no
  checkerboard, no shared patterns, and no pre-denoising pass, because RR "assumes
  independent samples".
- The result is lag that compounds (our EMA plus RR's history), blur that RR cannot
  undo, and RR treating our upsampling footprint as signal.
- **Fix: two pipelines, selected by `rr`.**
  - `rr=1`, the **RR path**:
    - full resolution, 1 ray per pixel (`gi_rays=1`). Same ray count as today's
      half-res × 4 rays, so cost-neutral;
    - no temporal pass, no upsample, so `gi_composite` reads `giRaw` at full
      resolution;
    - white noise from a high-quality hash (Q5);
    - the visibility folded raw into AO and the bounce added raw;
    - optionally `DLSSD.DiffuseHitDistance` from the hit distance.
  - `rr=0` (SR or TAA, which are not denoisers): keep and improve our
    accumulator (Q6, Q7) and use blue noise (Q5).
- **Expected gain:** less lag (the bounce follows moving characters at RR's own
  responsiveness), crisp contact detail at full resolution, no half-res halos. Risk:
  residual noise in very dark interiors. A/B, and fall back to 2 rays per pixel
  there.

**Q8 (P2). Wrong radiance at the bounce source.**
- `prevRadiance = diffuse + specular` (`gi_composite.hlsl:28`) has three problems:
  - (a) it contains view-dependent specular: a shiny floor bounces its camera-facing
    highlight;
  - (b) it contains the atmosphere's in-scatter, which the lighting adds into b10;
  - (c) it lacks the pre-lighting emissive/sky/FX target that compose adds
    separately (TRACK_D 8.1), so emissive surfaces do not light their surroundings.
- **Fix:**
  - use the diffuse only, or diffuse + specular × roughness² as a "diffuse-like"
    lobe proxy;
  - add the emissive target (D-EM a);
  - ignore (b) at gameplay distances.
- **Expected gain:** no specular "hot spot" bounce on polished floors and metal;
  emissives contribute to the GI.

**Q9 (P3). GI composite energy factors.**
- `gi_composite.hlsl:19` adds `gi · albedo · (1−metal)`. The game's diffuse ambient
  is `irradiance · diffuseColour · DFG.z(N·V, r) · AO`, × the aerial-perspective
  transmittance.
- **Fix:** multiply by the same DFG.z (LUT b38, already in the lighting's set). The
  transmittance can be ignored near the camera.
- **Expected gain:** a consistent split between the diffuse and specular ambient at
  grazing angles (a few %).

### Sampling and noise

**Q5 (P1). Hash seeding and noise choice.**
- Seeds are a linear combination hashed once:
  `hash(id.x·1973 + id.y·9277 + frame·26699)`
  (`gi_trace.hlsl:43`, `gi_trace_rt.hlsl:47`; `rt_ao.hlsl:29` and
  `gi_contact.hlsl:24` likewise).
- The hash itself, lowbias32, is good. But a linear seed lets
  (pixel, frame) pairs collide on hyperplanes, and every dimension comes from one
  serial stream.
- **Fix:**
  - `pcg3d(x, y, frame)` or `xxhash32` chained per coordinate, with a separate
    stream per sampling dimension (ray index, bounce, light pick, disc sample);
  - per denoiser path: for **RR**, white noise exactly as above (the RR guide:
    "high quality hash functions and white noise", "avoid checkerboard and
    screen-space dithering"); for **our accumulator** (`rr=0`), spatiotemporal blue
    noise (STBN 64x64x64, to fetch) or Owen-scrambled Sobol with a per-pixel
    blue-noise Cranley-Patterson rotation.
- **Expected gain:** for RR, cleaner convergence and no structured artefacts. For
  `rr=0`, about 2× lower perceived error at equal sample count with a short EMA.

**Q10 (P1). Hard sun shadows from a point sun; bias.** **Status: done** (soft sun: fix 5,
with the half-angle corrected by the RT audit; bias: RT audit fix 6, `rt_origin=1`, see
[AUDIT_RT.md §4](AUDIT_RT.md#4-fix-batch-2026-10-26-branch-of-e285602)).
- `rt_shadow.hlsl:40` traces 1 deterministic ray to a point light. The game's own
  sun specular uses a disc with `sin θ = 0.00459` (TRACK_D 8.1), so shadow and
  specular disagree, and distant casters get a pixel-hard edge.
- The normal offset is `0.002 × camera distance` (`rt_shadow.hlsl:38`): 3-5 cm at
  BG3's usual camera distance. That loses contact shadows under small props
  (peter-panning).
- **Fix:**
  - sample a cone over the disc (half-angle `rt_sun_radius`, default 0.00459,
    artistic scale), 1 ray per pixel, white noise; RR denoises with `rr=1`, SIGMA
    with `rr=0` (TRACK_D 8.5);
  - offset by `max(k·pixel footprint, ε·|P|)` along a *geometric* normal
    reconstructed from depth (cross product of the position derivatives), with the
    shading normal only for the N·L test;
  - keep a separate screen-space contact pass for the first few cm if needed.
- **Expected gain:** physically sized penumbrae that match the game's highlights;
  restored contact shadows.

### Temporal accumulation and upsampling (the `rr=0` path; partly also `rr=1`)

**Q6 (P2). The temporal filter is minimal.**
- `gi_temporal.hlsl` has four limits:
  - nearest-neighbour reprojection (`floor(prev)`, line 17);
  - a fixed α = 0.1 with no history length;
  - validity only on a 3% depth match (line 22), with no normal or plane test;
  - no variance clamp.
- In addition the motion vector is read at `id·2+1` (line 14), while the sample
  actually traced rotates within the 2x2 (`gi_trace.hlsl:12`). On sloped or
  grazing surfaces the rotation's own depth change can exceed 3%, so history is
  rejected and the result flickers.
- **Fix (NRD/ReBLUR practice):**
  - bilinear reprojection with per-tap depth-plane and normal weights (fall back to
    the valid taps);
  - a per-pixel history length `n` with α = max(1/(n+1), α_min);
  - a YCoCg 3x3 neighbourhood variance clamp on the bounce (not on the
    visibility);
  - the MV read at the traced sample `fp`;
  - a plane-distance test instead of a relative depth.
- **Expected gain:** 2-4× faster convergence after disocclusions, no ghost trails
  behind moving characters, stable grazing floors.

**Q7 (P2). The upsample is depth-only.**
- `gather_history` (`gi_common.hlsli:134`) weights by depth alone, so the bounce and
  the visibility bleed across creases and thin geometry with the same depth.
- **Fix:**
  - store the half-res normal (or recompute it from RT0) and add a normal weight
    `pow(saturate(dot(n, n_i)), 8)`;
  - use a plane-distance weight instead of the relative depth;
  - on the RR path the upsample goes away entirely (Q4).
- **Expected gain:** no light leaking across 90° edges; sharper AO in corners.

### DLSS Ray Reconstruction inputs (Track A)

**Q11 (P1). The normal guide is view space; N·V is a constant; matrices are missing.**
**Status: done (2026-10-26), `rr_guides_v2=1` (the default).** Implemented as
proposed below. The camera is read on the CPU from the G-buffer pass's own
set 0 b2 when the pass ends. Details in
[TRACK_A_RR.md "Guides v2"](TRACK_A_RR.md#guides-v2-rr_guides_v21-the-default).
Evidence, all in game:
- Floors decode to +Y world: walkway median (0.000, 0.998, 0.037), and still +Y
  after the camera pans to the docks.
- N·V rises from 0.42 to 0.92 down the walkway, matching the value predicted
  from the camera (0.69 at the centre, 0.91 at row 500; a flipped y would give
  0.38). Only 0.8% of pixels are negative, at silhouettes.
- RR output, static: v1 → v2 differs by 0.20/255 on average. The moving pan
  frame is clean.
- No non-finite values in 21 traces. The `-Validation` message IDs and counts
  are identical with v2 on and off.
- Specular hit distance: done with D-RF (2026-10-26, TRACK_D 10.3).
  `DLSSD.SpecularHitDistance` is the reflection ray's hit distance for on-screen
  hits and 65504 elsewhere; RR accepted it in every run, including `fg=2`.

The original finding:
- `rr_guides.hlsl:59` writes the decoded RT0. Track D proved those normals are
  *view* space (TRACK_D section 1), but the guide comment and TRACK_A treat them as
  world space.
- The RR guide: "If the app relies on RR computing specular motion, these should be
  World Space". We give no specular motion vectors, so RR computes them itself.
- `rr_nov=0.5` makes the specular albedo wrong at every angle that is not
  N·V = 0.5. Off by a factor of up to 2-3 at grazing angles on dielectrics.
- `WorldToViewMatrix` and `ViewToClipMatrix` are not set.
- **Fix:** at the G-buffer pass end the camera UBO (set 0 b2: `_m0` view, `_m1`
  projection) is already known to the host (Track D captures it).
  - Rotate the normal to world space (`n_w = n_v · R⁻¹`).
  - Compute V per pixel from uv and the projection; N·V = `dot(n_v, −v_view)`.
  - Pass both matrices, row-major as the guide says.
- **Expected gain:** a correct specular albedo, so RR demodulates the specular
  correctly and grazing reflections stop smearing. Required before D-RF, which also
  needs `SpecularHitDistance` with these matrices.

**Q12 (P2). RR guides for SSS, transparency and particles.** **Status (2026-10-26): CBT and SSS guides set with `rr_post=1`** (TRACK_A "RR before post"): the compose output as colour before transparency, and `lum(after − before·albedo)` of the SSS blur. The caveat below is resolved: RR now takes its colour before the render-resolution post, in the same space as these guides. Particles/transparency overlay: still not separated.
- RR sees background albedo under particles, fog and water.
- The frame has free sources for the guides:
  - the SSS passes' input and output (units 121/122) → `ScreenSpaceSubsurfaceScatteringGuide`
    and `ColorBefore/AfterScreenSpaceSubsurfaceScattering`;
  - the colour copy before the forward pass (unit 124) → `ColorBeforeTransparency`.
- The noisy GI (Q4) passes through the SSS blur, which spatially correlates its
  noise. That is exactly the case the SSS guide pair is for.
- Caveat: render-resolution post (DOF, bloom, exposure) runs between compose and
  the DLSS input. Check whether RR needs the before/after pairs at the same stage
  as the input colour.
- **Expected gain:** less smearing on skin, particles and water with RR. It matters
  most once the GI is fed noisy.

**Q13 (P3). Jitter sequence length.** The RR guide recommends at least 32 jitter
phases. Log the game's Halton period from the evaluate jitter; it can be extended
only by overriding the projection jitter, which we do not control. Diagnostic only.

### Alpha handling (Track D phase 3)

**Q14 (P3). Dithered LOD cross-fades and unrecognised cutouts.**
- The 0.73% "opaque in front" is screen-door dithered geometry. During a LOD
  cross-fade both LODs are in the BLAS, solid, which can self-shadow (acne between
  coincident LODs).
- `781cd8d1`-style and blended `tex` cutouts stay opaque (7.4).
- **Fix:**
  - detect dither-discard prepass shaders in `alpha_table.py` and mark them
    non-opaque with a stochastic test (the same dither hash, keyed by the
    instance's fade value captured per draw), or keep only the incoming LOD;
  - add opacity micromaps later, for cost.
- **Expected gain:** small. Removes rare acne during LOD transitions.

### Coverage

**Q15 (P2). A TLAS miss is not sky.**
- The TLAS lacks cut-away roofs and culled geometry.
- `gi_rt_fallback=0` (miss → the game's IBL, through the AO fold) is the right
  choice and must stay the rule for every effect (TRACK_D 8.6, D-GI2) until
  cascade-caster capture closes rooms.
- Record it as a rule (R3), so that no new effect returns the sky on a miss indoors.

## 2. How the GI's noisy signal can benefit from RR (summary)

1. **Feed RR raw, independent samples** (Q4, Q5):
   - full resolution, 1 ray per pixel, white noise from a proper hash, no
     checkerboard, no temporal accumulation and no upsample of our own;
   - the visibility folded raw into the game's AO, the bounce added raw before SSS.

   Cost-neutral against today's half-res × 4 rays.
2. **Keep the guides consistent with what the signal is modulated by**:
   - the bounce is added as `albedo · (1 − metal) · L`, which is exactly the
     diffuse-albedo guide, so RR demodulates it correctly;
   - fix the specular guide (Q11), so that the specular ambient occluded by our
     visibility is demodulated correctly too.
3. **Give RR the extra inputs it can use**:
   - `DiffuseHitDistance`, our GI hit `t` (miss → FP16_MAX);
   - for D-RF later, `SpecularHitDistance` plus the two matrices;
   - the SSS guide pair, since the SSS blur runs on our noisy diffuse (Q12).
4. **Exactly one denoiser per signal.** With `rr=1` none of ours. With `rr=0` our
   accumulator (Q6/Q7) or NRD (REBLUR diffuse, SIGMA shadows). Never both.
5. **ReSTIR GI/DI with RR** (when it comes): randomise the temporal reuse and use
   permutation sampling for the spatial reuse, as the RR guide demands.

## 3. Rules: "always implemented at best"

Every new effect, and every change to an existing one, must satisfy these before it
can become a default.

**R1 Sampling.**
- Importance-sample the integrand:
  - cosine-weighted for diffuse;
  - bounded GGX VNDF for specular;
  - uniform over the disc/cone for the sun (half-angle 0.00459 rad × an artistic
    scale);
  - solid angle for sphere lights;
  - RIS over lights by unshadowed contribution.
- Random numbers: `pcg3d`/`xxhash32` keyed by (pixel, frame, dimension), never a
  linear seed.
- The noise type follows the denoiser:
  - RR: white noise, no checkerboard, no screen-space dither patterns, no pattern
    shared across the screen;
  - our own accumulator: STBN or scrambled Sobol with blue-noise rotation;
  - ReSTIR under RR: randomised temporal reuse and permutation sampling.

**R2 Denoising path.**
- Exactly one denoiser per signal.
- `rr=1`: RR, with raw, full-resolution, independent samples injected where the
  game's own term lives, plus correct guides (world normals, true N·V specular
  albedo, matrices, hit distances).
- `rr=0`: NRD (REBLUR/RELAX diffuse-specular, SIGMA shadows) or our accumulator,
  meeting Q6/Q7: bilinear reprojection with plane and normal weights, history
  length, variance clamp, and disocclusion handling.
- No hidden pre-filtering under RR.

**R3 Composition.**
- Each effect names the game term it replaces (TRACK_D 8.2 table) and uses fold
  (replace semantics when the new signal subsumes the old), exact delta (the game's
  term replicated from the decompile and validated to < 1% mean error), or a patch.
- Apply the game's own factors: the DFG LUT, the specular occlusion, the global
  light scale `b13[0]`, the AO-on-direct factor, the light-channel masks.
- A TLAS miss is never sky indoors.
- Bounce sources are view-independent radiance (diffuse + emissive), reprojected
  with the MVs.

**R4 Spaces and units.**
- All in the lighting space (camera-relative world). Absolute world only for
  hashing and caches.
- Radiance in the units of the game's b10/b11.
- Every new space conversion gets a round-trip debug view (as `gi_debug=4`,
  `rt_debug=3`).

**R5 Validation (each item is a gate).**
- A debug view per effect (`rt_debug`/`gi_debug`), and `rt_stats` counters for its
  hit, miss and tier fractions.
- Non-finite guard on every write into a game resource, and only bounded factors
  folded into game inputs.
- `auto-run.ps1 -Validation`: the message IDs must equal the baseline (no new IDs).
- `-Move` camera pan, a level load, a live on/off toggle, and `fg>=2` together with
  the effect.
- Game resources go back to their original layout; compute bindings are restored.

**R6 A/B images and metrics.**
- Fixed scenes:
  - Steel Watch Foundry interior (the auto-run save);
  - the docks (outdoor sun);
  - a foliage scene;
  - a torch-lit night interior (for D-LS).
- For each: effect off/on with the same camera; tonemapped output plus the raw
  b10/b11; absolute difference ×4; three 1:1 crops.
- Into `docs/img/<effect>_<scene>_{off,on,diff}.png`.
- Metrics:
  - the mean luminance change;
  - FLIP (or PSNR) against a reference (the same view, static camera, 256 frames
    accumulated at 16× the rays, or D-PT within the captured geometry);
  - temporal stability: the mean frame-to-frame difference on a static camera,
    and during `-Move`.

**R7 Cost budget.**
- RTX 5090, 2560x1067 render resolution, GPU timestamps, medians of 300 frames,
  static and moving camera, logged by the core.
- Budgets, excluding RR's 2.6 ms and the scene build:

  | Effect | Budget |
  |---|---|
  | sun shadows (soft) | 0.15 ms |
  | AO + GI (incl. cache) | 0.6 ms |
  | local-light shadows | 0.5 ms |
  | reflections | 1.0 ms |
  | **all Track D effects together** | **≤ 2.5 ms** |

- The scene build (XFB + convert + BLAS/TLAS) stays ≤ 1.5 ms static and ≤ 2.0 ms
  in motion, once the static/dynamic split exists.
- Memory: the TLAS/BLAS/XFB budget is 700 MB; caches ≤ 256 MB.
- A regression > 10% needs a written justification in the track doc.

**R8 Fallbacks.**
- Every effect degrades to the game's own term when an input is missing: no TLAS,
  a stale TLAS (more than 1 frame), stale MVs, a level load, a missing binding.
- It must never leave the frame darker than the game's own result because of a
  missing input.

**R9 Defaults and documentation.**
- New effects are off by default until R5 and R6 are done and R7 holds.
- Then ROADMAP status, the track doc (settings, cost table, images) and
  `bg3rtx.ini` are updated in the same change.

**R10 Hot reload.**
- Shaders stay hot-reloadable; new settings are read live where possible.
- Anything read at device creation is marked "restart" in the ini and the doc.

## 4. Fix order

| Order | Items | Why first |
|---|---|---|
| 1 | Q1, Q2, Q11 (Q11 done) | correctness bugs; tiny code changes |
| 2 | Q4 + Q5 (RR path) | the biggest quality gain for the GI at zero cost; defines how every later effect feeds RR |
| 3 | Q10 (soft sun + bias) | together with TRACK_D 8.5 |
| 4 | Q3 (read `b0._m0.y`), Q8, Q12 | small, needs EN1's bindings |
| 5 | Q6, Q7, Q9 | the `rr=0` path quality |
| 6 | Q13, Q14, Q15 | polish and rules |


## Status of the audit fixes (2026-10-26)
| Fix | State | Evidence |
|---|---|---|
| 1. Bounce radiance reprojection | **Done.** `prev_radiance_at()` follows the game's MVs back to last frame; the MV layout transition now covers the traces. | Stable in `-Move` runs, no non-finite values |
| 2. Double darkening with the game's SSAO | **Done.** The traced visibility *replaces* the SSAO (`gi_ao=1.0`, lerp instead of multiply). | `img/quality_ab_ao_rr_mode.png`: top before, bottom after. Shadowed areas are about 5% brighter, and contacts are still occluded. |
| 3. RR fed an already-denoised GI | **Done.** RR mode (`gi_rr=-1` follows `rr`): full-res, 1 spp, no own history or spatial filter; RR denoises. Half-res accumulation remains for `rr=0`. | Same A/B: no visible noise. GI cost unchanged (about 0.34 ms). |
| 4. RR guides (world normals, N·V, matrices) | In progress (separate branch). | |
| 5. Noise and sun shadows | **Done.** pcg3d per-pixel/per-frame seeds; soft sun shadows sampling the sun disc, 1 ray, denoised by RR. **Corrected (RT audit fix 1):** the default was 0.0023 rad, described as "BG3's 0.263° diameter"; the game's 0.0045900648 (`f469db78:1428`) is the sine of the **half-angle** (0.263°), and the shader samples a cone of half-angle `rt_sun_radius`, so the default is now `rt_sun_radius=0.00459`. | Validation pending in the next combined run |
| RT audit 2. Back-lit translucency | **Done.** Skin, thin-translucency and hair pixels are traced for N·L ≤ 0 too (their transmission reads the sun mask); other back faces are left untouched. `rt_backlit`. | AUDIT_RT §4 #2: game mask lit on 66% of back-lit transmitting px at the docks, all of them zeroed before; `img/rtfix2_backlit_docks_*` |
| RT audit 3. Material classes | **Done.** RT2.a & 7 and RT3 decoded as the game does. D-LS evaluates each light with the class's exact term, and the GI composite uses the class's diffuse colour (no albedo² on skin, none on unlit). `mat_classes`. | AUDIT_RT §4 #3: the recompute never exceeds the game on those px; `img/rtfix3_materials_*` |
| RT audit 4 / Q3. AO on direct light | **Done.** `ao_direct=1`: no AO factor on ray-traced direct light (D-LS lights; the sun, recomputed exactly). The RT visibility stays on the ambient only, so no contact is darkened twice and direct light carries no 1-spp AO noise. `ao_direct=2`: the game's SSAO factor instead. | AUDIT_RT §4 #4: 1.9% of the locally lit luminance given back in the foundry; sun recompute = game − in-scatter at the docks |
| RT audit 5. Alpha region / textures | **Done.** Sized from demand, 4096 textures, overflow logged loudly. | AUDIT_RT §4 #5 |
| RT audit 6 / Q10 bias. Ray origins | **Done.** Exact TLAS surface + Wächter & Binder offset along the geometric normal, in every RT pass. `rt_origin`. Q10 is now fully done (soft sun: fix 5; bias: this). | AUDIT_RT §4 #6; `img/rtfix6_origin_*` |
| RT audit 10. Dropped draws | **Done.** Counted and logged; strips and fans captured. `rt_strips`. | 0 dropped in the foundry, the docks and the menu scene |
| RR before post (AUDIT_SOTA #1) | RR denoises the pre-DoF, pre-grade scene colour; the game's post chain (DoF weights, exposure, bloom, grade UBO + LUT) is replayed at output resolution. R3: bit-exact replication at render resolution (100.00% identical texels, foundry). R5: non-finite 0, fallback per frame on unmodelled post effects, `-Validation`/`-Move`: see TRACK_A. R6: foundry A/B, difference at the frame-to-frame floor; motion/dark-scene metric not done. R7: +0.15-0.18 ms. Effective RR (SETTINGS_DESIGN 7.3.1): the core's RR modes follow `rr_active()`, not the key. | `img/rrpost_foundry_*.png` |
| D-RF against the rules (TRACK_D 10) | R1: bounded GGX VNDF, pcg3d white noise on its own stream, mirror below a = 0.1. R2: RR only, no history or filter of ours; without RR deterministic mirror rays only (a < 0.3). R3: exact delta on the game's probe term (D-EN4, mean error 0.58%), TLAS miss keeps the probe, the game's DFG LUT as env BRDF, its SO kept for the probe and not re-applied to traced hits. R5: debug views 1-6, `rf_stats` tier fractions, non-finite guards, identical `-Validation` IDs, `-Move`, `fg=2`. R6: foundry A/B + crops; **reflective-scene A/B and stability metric not done**. R7: 0.33 ms. | `img/rf_foundry_*.png`, `img/rf_docks_*.png` |
