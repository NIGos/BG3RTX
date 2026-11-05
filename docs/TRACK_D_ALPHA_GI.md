# Track D: alpha test, hybrid RT GI and effect catalogue

Part of Track D (hardware ray tracing); index and old section map: [TRACK_D_RT.md](TRACK_D_RT.md).
Sections 7 and 8 keep their original numbers.

## 7. Phase 3 (2026-10-26): alpha-tested geometry and hybrid RT GI — PAUSED, WIP

Status: both parts work in game behind switches that are **off by default**
(`rt_alpha=0`, `gi_rt=0`). Paused on request before the final write-up; the
numbers below are from the runs done (Steel Watch Foundry, static camera,
RTX 5090, 2560x1067, GPU timestamps, 300-frame medians/averages).

### 7.1 Alpha test (`rt_alpha=1`, restart; `rt_alpha_test` is a live A/B switch)

- **Where the alpha test lives.** BG3's G-buffer FS never discard; the depth
  prepass does (G-buffer then depth-tests against it). So with `rt_alpha=1` the
  scene is captured from the **depth prepass** (depth-only D24S8 at render
  resolution; its small 5-draw sibling pass is learned and skipped), XFB stride
  24: `gl_Position` + the VS's lowest-location vec2 output (the UV in every
  prepass VS). `tools/xfb_patch_test <dump> <out> 24`: 2385/2385 VS patch and
  pass `spirv-val`.
- **Which draws alpha test, and with what.** `scripts/alpha_table.py` decompiles
  every depth-only discarding FS in the shader dump (1243) and writes
  `shaders/rt/alpha_fs.txt` (372 shaders):
  - `vt` (252): BG3's virtual texturing (Granite). Alpha = `.w` of the albedo
    tile, through an indirection texture (`texelFetch`, page bits in `.x`) into
    the tile cache array; per-draw UV transform + tile-set index in the material
    UBO (set 1 b0), tile-set constants (2 row-major mat4) in SSBO set 0 b12.
    `rt_common.hlsli` `vt_alpha()` replicates the lookup; mip from a ray cone.
  - `tex` (120): a plain texture's `.w`/`.x` (optional UBO uv scale). Only
    where our test is conservative: condition reads the sample directly; blends
    (`mix`) and branch-selected alpha are rejected (they let rays through solid
    pixels: 0.30% "behind" before this rule).
- **Per draw** (`layer/alphatest.cpp`): UBO/SSBO constants read from the game's
  mapped memory at the draw; views recorded; after the pass the records go to a
  buffer and the views into one of 8 ring descriptor sets (set 1 of the core's
  RT passes: records, UV soup, Texture2D[256], Texture2DArray[4], 2 samplers).
  Only views bound that frame. Alpha draws go to BLAS geometry 1 (not opaque);
  ray queries loop `Proceed()` and commit non-opaque candidates that pass.
- **Primary-ray check** (`rt_debug=3/5`, `rt_stats=1`, same view, live A/B):

  | | alpha test off | on |
  |---|---|---|
  | match | 98.12% | **99.09%** |
  | in front | 1.88% (vt cutouts 1.13, tex 0.01, opaque 0.73) | **0.91%** (vt 0.17, opaque 0.73) |
  | behind | 0.002% | 0.007% |

  The remaining 0.73% "opaque" is screen-door dithered geometry (camera
  cut-away / LOD fades, [rt_alpha_primary_check.png](img/rt_alpha_primary_check.png),
  purple) — correctly solid for secondary rays — plus unrecognised cutouts.
- **Cost**: shadows + AO 0.201 → 0.22 ms with the alpha test; BLAS: 2.23M opaque
  + 0.83–0.93M alpha triangles, refit 0.88 ms (convert 0.40 at stride 24),
  rebuild 3.4 ms; XFB buffer 384 MB, alpha region 64 MB.

### 7.1b Alpha test at secondary hits: grass fix (2026-10-26)

**Symptom.** Campsite: black, sharp-edged polygons on the ground under grass
(GI visibility 0; gone only with `gi=0`), the same under a twig in reflections,
and sun RT shadows of grass shaped like the card, not the blades.

**Root cause.** The grass and ground-cover family (`73849471`, 148 prepass draws
in Campsite; also `2f1b0ffd`, `b2d72ea7` and 21 other shaders, 24 in all) was
**not in `alpha_fs.txt`**, so its draws went into the TLAS as opaque geometry.
Its Granite alpha test keeps the selected sample in a `vec4` and reads `.w` from
it in the next statement (`_225 = mix(textureGrad(...), vec4(1,0,0,1), bvec4(... != 2202.0)); _226 = _225.w`);
`alpha_table.py` only recognised the inline `...2202.0)).w`. Flat cards lying
on the ground then blocked every GI ray from the pixels under them at t ~ 0.
Evidence (same view, `rt_debug=5`): the patches are "in front, opaque
geometry" (purple), not alpha-tested hits; `rt_debug=4` shows visibility 0 in
the same polygons; with `rt_stats=1` the alpha path itself returned solid only
by evaluation (no record 0%, no UV 0%, prim out of range 0% in steady state,
tile set not resident 0%, page from a coarser level ~1%), so none of the
fallbacks was the cause. Alpha-tested draws per frame went 108 → 412 once the
family was recognised. This is in the TLAS build, so it fixed every ray type
(GI, AO, sun and local shadows, reflections) at once.

**Second table bug (fixed, not the Campsite cause).** 76 vt shaders discard
twice: an early-out `alpha * mask < 0.001` (or 0.01), then
`fade * alpha * mask < 0.5`. The table took the first test's cut-off, so every
texel with any coverage was solid (whole cards solid at a filtered mip).
`effective_cut()` now takes the strictest constant cut-off of the tests that
keep the alpha as a factor (through `mix` only when both operands carry it,
`bvec` selects of the Granite sample allowed; UBO cut-offs and
`gl_FragCoord` screen-door dithers skipped). Line format: `cut=<effective>
cut0=<first>`; `rt_alpha_cut=0` (live) restores the first.

[img/alpha_campsite_crop0.png](img/alpha_campsite_crop0.png) (before | after | diff x4, render-res compose),
[img/alpha_campsite_crop1.png](img/alpha_campsite_crop1.png) (sun shadow of a twig clump: a rectangle before, the twigs after),
[img/alpha_campsite_vis_crop0.png](img/alpha_campsite_vis_crop0.png) (`rt_debug=4`),
[img/alpha_campsite_primary_crop0.png](img/alpha_campsite_primary_crop0.png) (`rt_debug=5`: purple opaque-in-front gone).

**Evaluation changes in `rt_common.hlsli`** (live switches, all on by default):
- `rt_alpha_stochastic=1`: the mip comes from the ray cone with the game's own
  Granite formula; the indirection entry already points at the finest resident
  ancestor of a missing page (its tile scale `e & 15` is smaller), and the read
  reports that level. Where the read is blurred (level > 1 above mip 0), the
  hard cut-off blends over one level into a hashed threshold (pcg3d of the hit's
  uv bits, the triangle and the frame), so P(solid) tends to the filtered alpha,
  i.e. the coverage: partial coverage of distant grass is unbiased under RR
  instead of all-solid or all-clear. Levels 0-1 keep the game's exact cut-off.
- `rt_alpha_unknown=1`: a candidate whose alpha cannot be read (no record, no
  UV, prim out of range, non-finite, non-resident tile set) lets visibility rays
  (GI, AO, reflections) through and stops shadow rays. Evidence: these cases
  are 0% in steady state and 0.6-2% of visibility candidates for a frame after
  a capture change (prim >= tris: an InstInfo/BLAS mismatch, not
  investigated further); a false "solid" there gives a hard black polygon in
  GI (the artefact class above), a false "clear" in a shadow gives a light
  leak, which is the worse error for shadows.
- `rt_stats=1` logs per ray class (`alpha candidates of visibility/shadow
  rays`): each fallback, vt/tex solid/clear, records with a loose first
  cut-off, blur level >= 2 / >= 4, stochastic share, pages from a coarser level.

**QA harness** (`python -m scripts.qa run --scenes S3`, Campsite; "before" is the
same build with the old table and `rt_alpha_cut/stochastic/unknown=0`):

| detector (‰ of the frame) | before | after |
|---|---|---|
| hard-dark, GI (final / lighting) | 10.9 / 2.35 | **0 / 0** |
| hard-dark, all RT (final) | 20.4 | **0** |
| sun extra casters | 15.8 | 3.3 |
| flicker excess area (std on/off) | 76.4 (1.73) | 15.6 (1.28) |
| brightening, GI (final) | 2.26 | 7.86 |
| brightening, sun RT shadows (final) | 0.53 | 4.40 |

Brightening rose where the grass no longer blocks light: under the log at the
bottom right the traced visibility now replaces the game's SSAO (which treats the
grass as solid) with ×2.8 more ambient, and the RT sun now passes between twig
blades where the game's shadow map is a blurred mass. These are flagged for
review, not confirmed errors; a run with `rt_alpha_stochastic=0` to separate
the stochastic part did not complete (the game was closed by another session's
run). S1 (foundry) with the fix: hard-dark GI 0.066‰, no baseline compared.

**Regression checks (2026-10-27, after merging main).** `-Validation` in Campsite
gameplay: same message IDs as the HEAD baseline. Non-finite: 0 in every capture,
including a trace taken while panning (Goblin Camp, `-Move`). `QuickSave_3784`
(old table + legacy switches vs new, same view): mean luminance -0.11%, no
visible change on the piers, railings and boats. Goblin Camp foliage was not
compared: that save enters a cutscene a few seconds after loading, at a varying
time, so the two runs did not see the same view.

**Not verified.** No `-Move` run reached its end: three runs (Goblin Camp,
Campsite, and the QA run after the merge) stopped presenting a few seconds after
gameplay or a trace (0 submits, GPU idle, process alive; a CPU-side stall, not a
device loss). Another worktree's QA runs show the same stall in S3 and S7, so it
is probably not this change, but this was not proven with a run of main alone.

**Cost** (Campsite, 2560x1067, 300-frame averages, shared GPU, noise ~0.2 ms):
4 x more alpha-tested draws (1.85M alpha triangles), visibility candidates
2.0M → 7.1M/frame; GI 2.95-3.0 → 3.0-3.4 ms, RT passes 1.35-1.40 → 1.40-1.53 ms.

**Not done.** 20 vt shaders keep a loose first cut-off (their 0.5 test reads
the alpha through `mix(1.0, alpha, ubo)` or a UBO cut-off; reading the UBO at
the draw would settle them). Blended-texture `tex` shaders (`42507cb4`) stay opaque.

### 7.2 Hybrid RT GI (`gi_rt=1`, live; see TRACK_C_GI.md)

`shaders/gi/gi_trace_rt.hlsl` replaces `gi_trace` (+`rt_ao`): same samples and
outputs, `gi_rays` alpha-tested ray queries to `gi_rt_radius` (1 m). Hit on
screen with matching depth: last frame's radiance; otherwise occlusion only
(`gi_rt_fallback=0`). Hits: 18% of 2.73M rays/frame; 86% shaded from the
screen, 11.7% hidden, 2.2% off screen.

| trace pass, 4 rays/sample, half res | GPU |
|---|---|
| screen-space (16 steps) | 0.248 ms |
| ray-traced, alpha test on / off | **0.175** / 0.124 ms |
| GI total (pre + post) ss + rt_ao → gi_rt | 0.44 → 0.35 ms |

Images: [gi_ss_visibility.png](img/gi_ss_visibility.png) vs
[gi_rt_visibility.png](img/gi_rt_visibility.png) (`rt_debug=4`; mean
visibility 0.74 vs 0.80: no screen-space halos at depth edges, crisper contact
occlusion), irradiance [gi_ss_irradiance.png](img/gi_ss_irradiance.png) vs
[gi_rt_irradiance.png](img/gi_rt_irradiance.png) (`gi_debug=1`).

### 7.3 Validation

`-Validation` with `rt=1 rt_alpha=0 gi_rt=0` vs `rt=1 rt_alpha=1 gi_rt=1`
(toggling rt_ao, rt_debug=5, rt_stats, rt_alpha_test live): identical message
IDs (game's own 09600, 10788, StorageImage-FormatMismatch, OutputNotWritten,
CopyBuffer-00118, 08737, headless swapchain); none from our work.

### 7.4 Not done / next steps

- Not verified: camera motion (`-Move`) with `rt_alpha=1` (rebuild path, prepass
  ordinal relearning), outdoor/foliage scenes, `rt_alpha` + `fg>=2`.
- `rt_stats=1` costs ~2 ms (atomics); measurement only.
- 781cd8d1-style cutouts (alpha chosen by a vertex-colour branch) and blended
  `tex` shaders stay opaque; would need a second captured varying.
- Off-screen hit radiance: a world-space radiance cache instead of fallback 0.
- Opacity micromaps would remove most candidate shader work.
- Decide default-on for `rt_alpha`/`gi_rt` after the motion run; update ROADMAP.

## 8. Effect catalogue and roadmap (analysis, 2026-10-26)

A feasibility study of further ray/path-traced effects. It was done without running
the game, from the decompiled shaders of `captures/20261025-235042` (mainly the
tiled lighting `f469db78`), the report of that capture, and the code of
Tracks A, C and D. The plans in sections 5-7 stay as they are. This section only adds
effects that were not already planned, plus the enablers they share. The quality
audit of the existing effects, and the rules every new effect must follow, are in
[QUALITY.md](QUALITY.md).

### 8.1 What the game gives us (data inventory)

Everything below is in the **lighting space**, which is camera-relative world
(`P = ray(uv) × linear depth`, the same space as the TLAS; see 6.4). Absolute world =
`P + _m6` of the camera UBO (set 0 b2). Light positions in the light array are in
this space too: the lighting computes `light.m0.xyz − P`.

| Data | Where | Have it? | Notes |
|---|---|---|---|
| Position, view-space normal, base colour, roughness, F0 (`0.16·spec²`/metal), metalness, light-channel bits (RT2.w bits 4-6) | G-buffer RT0-RT2, linear depth; bound to the tiled lighting at set 1 b1/b2/b3/b5 | **yes** (core snapshot) | Primary hit only. RT3 is almost empty. |
| **Local lights** | lighting set 1: b23 cluster headers (ivec4: x offset; y bits 0-11 points, 12-23 spots, 24-31 env probes; z bits 0-9 projector lights), b24 index list, **b25 light array** (struct `_50`) | **readable** (same `query_binding` route as the camera UBO; not done yet) | `_50`: m0.xyz position, m0.w = 1/radius; m1.xyz colour, m1.w spot cos-outer; m2.xyz spot direction, m2.w cos-inner; m3 < 0 → × global scale `b13[0]`; m4 falloff exponent; m5 falloff model/bias (sign selects the model); m6 light-channel mask; m7 bit 0 = casts a shadow, m7>>2 = shadow-atlas slot; m8 shadow strength. Clusters are froxels (log depth), so they only index lights for points inside the view frustum. |
| Projector (textured) lights | b29 (struct `_53`, row-major mat4x3 projection, cookie index into b45 `texture2D[]`) | readable | Third light loop in `f469db78`. |
| Local-light shadows | b40 (per-face mat4 + atlas rect + bias), b41 atlas | readable | Point lights: 4-face **tetrahedral** maps. 12-tap Vogel-disc PCF rotated by interleaved gradient noise (`fract(52.98·fract(0.0671·x + 0.00584·y))`), faded by camera distance (`b39 _m34`) and by m8. Lights without m7 bit 0 have **no shadow at all**. |
| Sun | camera CB set 0 b1: `_m0` direction (= `cam.v0`), `_m2` colour, or a transmittance-LUT colour when the atmosphere is on; × `b13[0]` | **yes** | **Angular radius:** the sun specular uses a representative-point disc with `sin θ = 0.0045901`, `cos θ = 0.9999894` (θ = 0.263°, the real sun). Shadow = mask b7 (ours to fold into) × two cloud-shadow textures. |
| Ambient | global sky cubemaps b34-b37 (irradiance + prefiltered spec, two sets blended by `b39 _m29`), local parallax-corrected box/sphere env probes (b33 list + b45 cube array), DFG LUT b38, × `b13[0]·100` | readable | Diffuse ambient = irradiance · diffuse colour · DFG.z · AO. Specular ambient = prefiltered probe · (F0·A + B) · Lagarde specular occlusion(AO, N·V, roughness). **There is no SSR in BG3.** All indirect specular comes from probes. |
| AO | b6 RGB10A2 `.x` = the game's **half-res SSAO** (units 88-91: `0effa9ff` → `af81b62d` deinterleave → `a6bb35c6` blur) | **yes** (we fold into it) | The lighting also multiplies **direct** light (local lights and sun) by `mix(1, AO, b0._m0.y)` (diffuse) and `mix(1, specOcc, b0._m0.y)` (specular). That value has not been read yet. |
| BRDF | `f469db78` | **known exactly** | GGX D, height-correlated Smith (the approximate form), Schlick F; renormalised Burley diffuse × `mix(1, 0.662, r)`; roughness ≥ 0.09, plus specular AA from the quad normal variance (`sqrt(r² + min(var, 0.18))`). |
| Atmosphere | inside the lighting: aerial-perspective transmittance multiplies both outputs, and the in-scatter is added to diffuse (b10) | known | So the lit diffuse buffer contains in-scatter. Negligible at gameplay distances (the planet is scaled, R ≈ 636 km), but not zero. |
| Emissive / sky / FX | a B10G11R11 render-resolution target written **before** the lighting (units 78-85: sky `c9f013fc`, half-res clouds, projected FX) and added in compose `5c3fdabc`: `(diffuse_after_SSS + specular + this) · fogT + fogInscatter` | not captured | Not in our `prevRadiance` (QUALITY Q8). Where emissive *materials* go is unverified: RT3 is near zero, so either this target or the forward pass. |
| SSS | units 121/122 (`6f535075`, `d1669194`), profiles in an SSBO (set 2 b15); they read our diffuse buffer | yes (we inject before it) | Screen-space blur, two passes. |
| Fog / volumetrics | froxel grid 285x119xZ: `c3e212ad` density, `ca8adce7` scattering (sun cascades via samplerShadow + local lights), `8a4e0720` integration; tricubic lookup in compose | known | Volumetric sun shafts already exist (from the cascades). |
| Transparents | forward pass unit 125 (46 draws, water among them), after a colour copy for refraction (unit 124) | **not in the TLAS** | Would need XFB on the forward pass. |
| Off-screen geometry | — | **no** | The TLAS holds what the camera's G-buffer (or prepass) drew: frustum-culled, **roofs cut away indoors**, no forward geometry. The shadow cascades contain more (roofs, casters around the view): 6.7 item 2. |
| Materials at secondary hits | — | **partly feasible** | `vt_alpha()` already samples the Granite tile set for alpha. The same lookup in the albedo layer gives the hit's **base colour** for the 252 VT shader families. Normal/physical layers and plain-texture materials would need more decoding. |

### 8.2 Composition: how a new term enters without double counting

Every effect must name the game term it replaces and use one of three patterns:

1. **Fold** through a game input whose meaning is verified. Example: visibility into AO
   `.x`, or into the sun mask b7. Use *replace* semantics when the new signal contains
   the old one (RT AO contains SSAO's occluders). Use *multiply* only when the two are
   independent (RT on-screen casters vs off-screen cascade casters).
2. **Exact delta.** Re-evaluate the game's term `T_game` from the decompiled code with
   the same bound resources, then write `out += T_ours − T_game` into b10/b11 at
   the post-lighting hook (where `gi_composite` runs, before SSS). Since `T_game` is
   computed exactly, the result is `out − T_game + T_ours ≥ 0` whenever
   `T_ours ≥ 0`, with no clamping bias. This works for per-light shadow terms, the
   specular probe term and the diffuse ambient term.
   - Limitation: the aerial-perspective transmittance is not replicated, so
     deltas fade back to the game's term beyond about 100 m.
3. **Lighting-shader patch** (enabler D-EN5): a SPIR-V patch of the `f469db78` family
   that multiplies or replaces specific terms by values from our images. It is the
   cheapest at run time, but the most fragile.

| Game term | Replaced by | Pattern |
|---|---|---|
| SSAO (b6 `.x`) | RT ambient visibility | fold, **replace** (today: multiply, see QUALITY Q2) |
| Sun mask (b7) | RT sun visibility (hard, then soft) | fold; multiply until cascade casters are captured, then replace within TLAS coverage |
| Per-light atlas shadow, or no shadow | RT per-light visibility (D-LS) | exact delta (v1), patch (v2) |
| Specular ambient (probes) | RT reflection (D-RF) | exact delta (v1), patch (v2) |
| Diffuse ambient (probes · AO) | RT/cached irradiance (D-GI2) | fold (today's hybrid) → exact delta |
| Emissive/FX target | (source of bounce only) | read-only (D-EM) |

### 8.3 Feasibility of the candidates (summary)

Costs are for the RTX 5090 at 2560x1067 (2.73 M pixels), scaled from what was measured:
- sun shadows: 2.73 M rays, 0.08-0.11 ms;
- hybrid GI: 2.73 M alpha-tested rays of 1 m, 0.175 ms.

That works out to about **0.04-0.07 ms per million short rays**. For long closest-hit
rays, assume 2-3× that.

| Candidate | Data needed → have? | Injection | Gain | Cost | Risk | Verdict |
|---|---|---|---|---|---|---|
| Soft sun shadows | sun dir + **angular radius 0.00459 rad** (found) → yes | sun mask b7 (as now) | medium: penumbrae grow with caster distance (~0.9% of the distance), mostly for buildings and trees | 0.10-0.15 ms + denoise (RR: 0) | the fold only darkens, so penumbrae appear only where the cascades are lit | **already planned** (6.7 item 5); refined in 8.5 |
| RT shadows for local point/spot lights | light array b25 (pos, 1/r, colour, cone, flags), clusters b23/b24, atlas b40/b41 → **readable** | exact delta at post-lighting | **high**: BG3 has many unshadowed fill and torch lights that leak through walls and floors, and its shadowed lights use a low-res tetrahedral atlas | 0.3-0.6 ms (stochastic 1-2 rays/px + RR) | art intent: some fill lights are meant to leak, so shadowing is per-light and opt-in | **new: D-LS** |
| ReSTIR DI / RTXDI | as D-LS; RTXDI SDK **not** in `external/` | sampler inside D-LS | medium: BG3 has tens of lights per view, not thousands. Worth it only with emissive triangles. | +0.2 ms over D-LS | RR wants permuted/randomised reuse | **new: D-DI** (after D-LS) |
| RT reflections (glossy and mirror) | roughness/F0/metal decode → yes; hit radiance: on-screen → yes (reprojected); off-screen → needs EN1 + EN2 (+EN3) | exact delta on the specular ambient (probes) | **very high**: there is no SSR in BG3; wet stone, polished floors and metal show only probe reflections | 0.5-1.0 ms | the TLAS lacks geometry behind the camera and above the screen (culled), so the probe is the fallback; RR needs specular hit distance and matrices | **new: D-RF** |
| Multi-bounce GI + radiance cache | TLAS, hit albedo (EN2), lights at hits (EN1), sky (b34) → mostly | today's hybrid (fold) → delta on the diffuse ambient | medium-high: the GI reach grows from 1 m to the room scale, with colour bleeding | 0.4-0.6 ms, 160 MiB (SHaRC) | **roofs are cut away indoors**: a TLAS miss is not sky, so misses must keep the game's probe; that needs cascade casters for closed rooms | radiance cache **planned** (C/D "next"); the multi-bounce, screen-seeded SHaRC design is **new: D-GI2** |
| Emissive surfaces as light | on-screen: the emissive/FX target (8.1) → capturable; as RTXDI lights: emissive triangles + radiance → **no** | bounce source (`prevRadiance`) | low-medium (glyphs, lava, braziers; flames are particles with their own point lights) | ~0.02 ms | where emissive materials go is unverified | **new: D-EM** (part a now, b deferred) |
| Translucency / SSS | SSS pixel mask (stencil/flags, unverified), TLAS for thickness → partly | delta into diffuse before the SSS blur | low-medium (back-lit ears, leaves, cloth) | 0.05-0.1 ms | finding the SSS/foliage classification | **new: D-TR** (low) |
| RT refraction (glass, water) | refractive surfaces are forward-drawn and not in the TLAS; the game's refraction samples the unit-124 copy | patch the forward FS families | low (BG3's water is stylised; screen-space refraction rarely fails at the isometric angle) | 0.3 ms + XFB on the forward pass | high: shader patching of forward materials; RR resampling caveats | **new: D-RFR**, deferred |
| RT for transparents/particles | forward pass not captured | — | low | — | — | **not recommended**. Do RR transparency guides instead (QUALITY Q12) |
| Volumetric light shafts with RT visibility | froxel grid known; sun-view geometry needs cascade capture | patch `ca8adce7`'s cascade lookup, or replicate it | low-medium: the game's froxels already sample the cascades; RT adds only on-screen detail | ~0.1 ms (1 ray per froxel) | patching a 1227-line compute shader | **new: D-VOL**, low |
| Full path tracing replacing the lighting | geometry incomplete (culled, roofs cut away, no forward), secondary materials approximate, light list view-culled, baked probe look, unshadowed art lights | replace b10/b11 | — | 5-8 ms | **infeasible as a product**: it cannot reproduce what the game does not draw | **new: D-PT**, a reference/debug mode only |

### 8.4 Enablers (shared by several effects)

**D-EN1 Light-list snapshot.**
- Add set 1 b13, b23, b24, b25, b29, b39 (the lighting constants), b40 and b41 to the
  core's `Snapshot` at the tiled-lighting dispatch, and bind them to our passes
  read-only.
- Write `shaders/rt/bg3_lights.hlsli`: the struct decode, the cluster walk (same
  froxel formula as `f469db78`: `log2(depth·far)·_m21 + _m22`), the exact
  unshadowed contribution (falloff models, spot cone, channel mask, ×`b13[0]`, AO
  factor `mix(1, AO, b0._m0.y)`), and the atlas PCF including its IGN rotation.
- Validation: `light_debug=1` recomputes Σ of all lights and compares it with the
  game's b10/b11 minus ambient and sun (by toggling the ambient terms via debug
  modes), so the mean error must be < 1%.
- Log statistics: number of lights, number shadowed, lights per pixel.
- Also read `b0._m0.y` (the AO-on-direct factor) and log it.
- Cost: 0.

**D-EN2 Hit material.**
Updated 2026-10-31: source checkpoint 177ef6e replaces the old guessed-colour/
G-buffer-median plan with an explicit native tuple. `rt_hit_material` is startup
opt-in, OFF in presets; ABI 20 adds a typed query-use registration callback.
The first domain is static VS3812/deferred FS8995, fresh dynamic alpha soup,
16 accepted draws. Prepass/deferred/query may use separate one-time command
recordings; actual camera/scene/draw identity and GPU clip/UV comparison join them.
Execution-time gather retains native VT parameters; actual normal/albedo/physical
views, sampler and deferred tangent frame feed native fields and the declared
shared FresnelBlend model. Ray-cone gradients are explicit, not native raster
quad derivatives. Unknown inputs remain unsupported, never default grey/material.
`rt_debug=9` and `rt_stats` check acquisition/primary native-field residuals.
Normal GI/reflections retain existing output; direct-only reference is next.
Build/parity/source lifetime review pass; participation, quality and GPU cost
remain unmeasured until the bounded S1 smoke. Exact tuple, capacities, tests and
limits: [native material checkpoint](evidence/native-hit-material-20261031.md).

**D-EN3 Geometry retention and closure.**
- The static/dynamic split (6.7 item 3) plus **retention**: a static instance
  stays in the TLAS for `rt_retain_frames` (e.g. 300) after the game stops
  drawing it (culled, behind the camera).
- Together with cascade-caster capture (6.7 item 2, which also brings the roofs the
  camera cut-away hides), this gives off-screen geometry for reflections and GI,
  and closed rooms.
- Instance masks: bit 0 G-buffer, 1 cascade-only, 2 retained, 3 dynamic. Each
  effect chooses its masks. For example, the sun uses all of them; reflections
  exclude cascade-only geometry if it is missing faces that face the camera.

**D-EN4 Exact-delta library.**
- Replicate the game's specular ambient (global and local probe loop, DFG,
  Lagarde specular occlusion) and diffuse ambient from `f469db78` lines
  ~1210-1450, in `shaders/rt/bg3_ambient.hlsli`.
- Validate it against b10/b11 the same way as EN1.

**D-EN5 Lighting-shader patch hooks (v2 of the deltas).**
- A SPIR-V pass over the ~15 `f469db78` permutations that adds
  `ourFactor(pixel, term)` reads at three sites:
  - the per-light shadow factor (`_802`/`_1087`/`_1427`);
  - the specular ambient `_1684`;
  - the diffuse ambient `_1687`.
- This removes the ALU of re-evaluating the lights and makes the composition exact,
  including the transmittance.
- Do it only after the deltas are validated, as an optimisation.

### 8.5 Refinements to already-planned items

- **Soft sun shadows (6.7 item 5).**
  - Sample a cone of half-angle `rt_sun_radius` (default **0.00459 rad**, the game's
    own value) with uniform disc samples, 1 ray per pixel at full resolution.
  - With `rr=1`: per-pixel white noise from a proper hash (no checkerboard), fed to
    the mask raw; RR denoises.
  - With `rr=0`: NRD SIGMA (in `external/NRD`) from the occluder distance, or a
    penumbra-sized bilateral blur.
  - Keep the multiply fold until cascade casters exist, then replace within TLAS
    coverage (`mask = vis_rt` where the cascades' casters are all in the TLAS).
  - An artistic `rt_sun_scale` multiplier, since BG3's PCF cascades are softer than
    the real sun.
  - Cost 0.10-0.15 ms.
- **Cascade-caster capture (6.7 item 2).**
  - It is now also the prerequisite for closed interiors in GI and for volumetric
    shafts.
  - Capture the largest cascade first: one more XFB pass of about +0.3 ms, and
    duplicates with the G-buffer soup are harmless for any-hit rays.
  - **Implemented 2026-10-29 (`rt_cascade_casters=0`, off until gated), see below.**

#### Cascade casters as implemented (`rt_cascade_casters`)

Goal: RT sun shadows from what the camera does not draw but the cascades do, mainly
cut-away roofs, so the RT sun can replace the game's mask under roofs too.

- **Matrix source (established).** Every cascade VS in the shader dumps of
  `captures/20261026-165848` (`1cf36b97`, `a7bf37c6`, `646a930e`, `dcb21688`, ...)
  ends like the camera's: `gl_Position = vec4(W, 1) * set0.b2._m2`, y negated. So each
  cascade pass binds its own camera-layout UBO at set 0 binding 2 with its orthographic
  view-projection at +128, and W is the same camera-relative world (the prepass and the
  cascades share VS `646a930e`). The Shadow UBO `_m10[k]` is not needed.
- **Detection.** Depth-only, D16, square >= 1024, a layer of an array of >= 3 layers
  (the far shadow cache has 2). The cascade index is the depth view's base layer.
- **Capture** (`layer/geocapture.cpp`). Cascade `rt_cascade_index` (default 1: cascade 0
  is the smallest, 30 draws in the foundry; 1 covers the player's building and its
  neighbours; check the extents in the `[cascade] matrix` line) is captured by the
  existing XFB patch into its own buffer per frame parity (`rt_cascade_mb`, x2). Skipped:
  draws the camera source pass drew in this or the last frame (key: kind, counts, firsts,
  vertex binding 0, index range, and the instance stream binding or the set 1 binding 0
  UBO that places it); alpha-tested draws (the FS can discard; no UV or material here);
  indirect and non-sizeable topologies; draws with another set 0 binding 2.
- **Conversion.** Right after the pass, in its command buffer:
  `shaders/rt/xfb_to_world_ortho.hlsl` solves W from the x, y, z rows (the w row of an
  orthographic matrix is (0, 0, 0, 1)); a vertex whose w is not 1 collapses its triangle.
- **Next frame** (`rtscene.cpp`, `bvhsplit.cpp`; split builds only). The build of frame F
  takes F-1's soup (its command buffer submitted) and adds one BLAS (new each build; the
  old one is released once no live scene slot uses it) and one instance: translation
  `origin_cascade(F-1) - cam(F)`, mask 0x02, custom index 0xFFFFFF, not opaque. The cascade
  VS's world position is relative to the cascade UBO's own origin (byte 208), which is
  92-96 m from the camera's in S7 (`rt_cascade_origin=1`; 0 = the first version's
  `cam(F-1) - cam(F)`, which put the casters 92 m off and leaked).
- **Alpha-tested casters** (`rt_cascade_alpha=1`): 435 of 694 cascade-2 draws in S7 are
  alpha-tested; they go in as opaque (cutouts solid). Without them the RT still saw
  "game dark & RT lit" on 9.5% of traced px; with them 0.47%. `rt_cascade_mb=160`: cascade 2
  with alpha holds 1.47M triangles (~100 MB); cascade 3 overflows (340 draws dropped).
- **Shaders.** `RT_VIS_MASK` / `RT_HIT_MASK` in `rt_common.hlsli` are the literal 0xFF in
  ordinary builds (SPIR-V of all 66 existing outputs byte-identical). The core picks
  the `RT_CASCADE_MASKS` variants (`common/shader_variant.h`; once `gi/cascade/` wrappers) with the key on:
  mask 0x01 for GI, reflections, AO, local lights, origin and primary checks; the sun
  ray (`rt_shadow.hlsl`) traces 0xFF and ignores cascade hits nearer than
  `RT_CASCADE_TMIN` = 0.5 m (a coarser copy of the shaded surface; a roof is >= 2 m up).
- **Trust** (`core/sun_trust.cpp`). With `rt_cascade_roofs_trust=1` the roof volumes are
  not uploaded: roofs no longer stop the replace. The off-screen test stays.
  `rt_cascade_roofs_trust` (rejected, interior leak): removed 2026-10-31, see CONSOLIDATION.md C1.
- **Logs.** `[cascade] sun cascade pass found` once per layer; every `rt_timing_frames`:
  `[cascade] casters:` (draws seen / duplicates / alpha / added, triangles, MB, builds with
  the instance and why not, captured pass GPU ms) and `[cascade] matrix of cascade k`
  (w column error, det, extents of every cascade, UBO origin vs camera in m: the
  inversion's premises).
- **Gate result 2026-10-29 (S7 Blushing Mermaid, static, FG 2, live A/B in one run,
  sup-cascade2/3/4-s7-fg2).** Sun stats per 600 frames: key off (roof volumes): game dark &
  RT lit 29-31% of traced px, kept by roofs 99%, mean brightening 0.0004-0.0005/px. Key on
  (index 2, alpha 1, origin 1): game dark & RT lit 0.42-0.47%, kept by roofs 0%, replaced
  (lit) 13-15% of those, mean brightening 0.0010/px; game lit & RT shadowed 0.95% -> 1.2%.
  Origin 0: 0.0296/px (the leak). Cost: scene build 1.42 -> 2.72 ms GPU, cascade pass +0.43
  ms; rendered rate unchanged (30.0 fps cap in unfocused runs). Pending: the user's visual
  check (sharper roof shadow edges; no light through cut roofs; solid foliage cutouts in
  shadows would be the artefact to look for).
- **Gate (first plan).** Interior with the roof cut away, sun through windows: live A/B of
  `rt_cascade_casters`. Pass: matrix line orthographic, origin ~0 m; builds with the
  instance ~100%; with the key on `kept by roofs` 0% and no new light under the roof
  (sun check: brightened px not above key-off); outdoors, mean brightening equal within
  noise (no acne: `game lit & RT shadowed` not up). Cost: the casters line's pass ms and
  the `[rt] scene build GPU` split lines (the BLAS build, TLAS rebuilt every frame).
- **Known limits.** One frame of lag for moving casters (a moving character's shadow is
  the union of two poses where not deduplicated). Roofs outside cascade k's extent are
  not captured, but are trusted: raise `rt_cascade_index` if the extent is smaller than
  the view. Alpha-tested cascade casters are left out. Legacy (non-split) builds get no
  instance. The layer code needs a game restart once; the keys are live.
- **Radiance cache (TRACK_C next, 7.4).** D-GI2 below fixes the design.

### 8.6 New effects

Each entry gives the ID, the data needed, the injection point, the cost, the priority
and the steps. Priority: P1 next, P2 after P1, P3 later, P4 not recommended.

#### D-LS: ray-traced local-light shadows (P1)
- **Status (2026-10-26): implemented, on by default (`rt_local_shadows=1`).** Results, choices
  and numbers in [section 9](TRACK_D_LOCAL_LIGHTS.md#9-d-ls-ray-traced-local-light-shadows-implemented). The plan below is kept as written.
- **Data:** EN1 (lights, clusters, atlas), the G-buffer, the TLAS (+ alpha).
- **Injection:** the post-lighting hook. Per pixel, the exact delta
  `Σ_i c_i·(v_i − s_i)`:
  - `c_i` is the unshadowed contribution (diffuse to b10, specular to b11);
  - `s_i` is the game's own shadow term (atlas PCF × fade × m8, or 1 when the
    light casts no shadow);
  - `v_i` is the ray-traced visibility.
- **Which lights:** `ls_mode=1` shadowed lights only (the game's intent, better
  shadows); `ls_mode=2` also unshadowed lights with radius > `ls_min_radius`
  (stops leaks); `ls_exclude_channel` for art lights.
- **Sampling:** v1 is deterministic, 1 ray to the light centre per light (hard).
  v2 samples the sphere by solid angle, using the radius from `_m5` if it encodes a
  source size (to verify), with 1-2 rays per pixel chosen by RIS over the cluster's
  lights weighted by `c_i`. The estimate is `Σ c_i s_i` (exact, subtracted) plus
  `c_j v_j / p_j` (stochastic, ≥ 0). RR denoises; with `rr=0`, SIGMA.
- **Cost:** v1 about 0.05 ms per light-per-pixel on average (measure the light
  count first); v2 0.25-0.4 ms.
- **Steps:**
  1. EN1, with its validation (Σ of lights matches the game's buffers).
  2. Debug view: per-pixel light count, and shadowed vs unshadowed lights.
  3. v1 deterministic, for shadowed lights only. A/B against the atlas in a
     torch-lit interior.
  4. `ls_mode=2`. A/B for light leaks under floors and through walls.
  5. v2 stochastic plus RR.
  6. Validation, motion run, cost table. Then default-on decision.

#### D-DI: ReSTIR DI over the local lights (P3)
- **Data:** as D-LS, plus emissive triangles (D-EM b) if they ever exist.
- **Injection:** the sampler inside D-LS v2; the composition is unchanged.
- **Cost:** +0.15-0.25 ms (temporal and spatial reuse, 1 shadow ray).
- **Steps:**
  1. Fetch RTXDI (not in `external/`).
  2. Initial RIS over the cluster lights.
  3. Temporal reuse with randomised neighbours, then spatial reuse with permutation
     sampling (the RR guide's rule, QUALITY R1).
  4. Visibility reuse.
  5. Compare its variance with D-LS v2 at equal cost. Keep it only if it wins in
     scenes with more than 8 lights per pixel.

#### Cache-free direct / two-surface reference increment (2026-10-31)

Default-off `rt_hit_reference=0` now selects the existing native-hit reference's
Direct only or Two surfaces mode through one Experimental control. Two surfaces
adds one shared FresnelBlend BSDF continuation with f*cos/pdf throughput and
secondary point/spot NEE, then explicit depth2 termination. Null samples retain
their probability; unavailable geometry/material/light inputs remain incomplete
and keep the direct prefix. This is a bounded class0/in-frustum reference, not
complete hit radiance or full M6, and it is not injected into RF's probe delta.
Six targeted CTests pass; 334 new CPU checks, 292 settings checks and 16 ordinary
reference SPIR-V identities / 16 new feature validations pass. GPU cost/runtime
participation are unknown; acquisition and later same-run image checks remain
open. Details and exact limits: [OPAQUE_TWO_SURFACE_REFERENCE.md](OPAQUE_TWO_SURFACE_REFERENCE.md).

#### D-RF: ray-traced reflections (P1)
- **Status (2026-10-26): tiers (a) + (c) implemented with D-EN4 and the RR hit distance.**
  Results in [section 10](TRACK_D_REFLECTIONS.md#10-d-rf-ray-traced-reflections-implemented-tiers-a-and-c). The plan below is kept as written.
- **Data:**
  - G-buffer roughness (after the game's specular AA: `sqrt(r² + min(var, 0.18))`,
    then `r ≥ 0.09`), F0 and metalness.
  - Hit radiance in three tiers:
    - (a) on screen with matching depth: `prevRadiance`, reprojected (QUALITY Q1);
    - (b) off screen but in the TLAS: EN2 albedo × (sun with an RT shadow ray +
      EN1 lights + sky irradiance b34) + emissive 0;
    - (c) miss, or a cascade-only hit: the game's probe.
- **Injection:** exact delta on the specular ambient (EN4):
  `b11 += (L_rt · DFG(F0, NoV, r) · SO − probeSpec_game)`.
  It is blended toward the probe with a confidence weight: tier (a) 1, tier (b)
  `rf_offscreen_w`, tier (c) 0. The weight also fades to 0 at roughness
  `rf_max_rough` (0.6).
- **Sampling:**
  - 1 ray per pixel, GGX bounded-VNDF sampling, for pixels with r < `rf_max_rough`;
  - mirror (r < 0.1) gets a single deterministic ray;
  - RR gets `DLSSD.SpecularHitDistance` (FP16_MAX for misses) plus the
    world-to-view and view-to-clip matrices (QUALITY Q11).
  - With `rr=0`: NRD REBLUR specular.
- **Cost:**
  - about 1.4 M rays (≈ 50% of pixels) at 0.15-0.3 ms;
  - tier (b) shading 0.2-0.4 ms;
  - EN4 0.1 ms;
  - total **0.5-1.0 ms**.
- **Steps:**
  1. QUALITY Q11, the RR world-space normals and matrices.
  2. EN4, with its validation.
  3. Tiers (a) and (c) only: on-screen reflections with probe fallback. This is
     already "SSR quality with RT accuracy". Debug view: tier per pixel.
  4. Hit-distance output to RR. A/B on a wet or polished floor scene and on metal.
  5. EN2 + EN1, then tier (b). Measure the tier fractions with `rt_stats`.
  6. EN3 retention, which shrinks tier (c).
  7. Then specular occlusion from the same rays replaces Lagarde's for those pixels.

#### D-GI2: multi-bounce GI with a screen-seeded SHaRC cache (P2)
- **Data:** TLAS, EN2, EN1, the sky b34, absolute world (`+ _m6`) for hashing.
  `external/SHARC` (needs `-enable-16bit-types` for DXC).
- **Design (screen-seeded):**
  - **Update** pass: about 1/16 of pixels trace paths with 2-3 bounces. At each hit:
    if it is on screen with matching depth, its radiance is the game's own lit
    diffuse (exact, with all the game's lights and baked probes) and the path
    terminates; otherwise it is shaded as D-RF tier (b) and the path continues.
  - **Miss** rule: a miss *indoors* (or anywhere until EN3 closure) returns the
    game's diffuse probe irradiance at the last vertex, never the open sky. Roofs
    are cut away.
  - **Resolve:** the SHaRC accumulation, with a 32-64 frame cap and stale eviction.
  - **Query:** the existing GI rays (radius raised from 1 m to `gi_rt_radius` 8-30 m)
    read the cache at off-screen hits instead of fallback 0.
- **Injection:** unchanged (AO fold + bounce add), until EN4 allows an exact delta on
  the diffuse ambient.
- **Cost:** update 0.1-0.2 ms, resolve 0.05 ms, longer query rays +0.2 ms;
  **0.4-0.6 ms**, 160 MiB.
- **Steps:**
  1. EN2, EN1.
  2. SHaRC update/resolve on our own passes; hash-grid debug view.
  3. Query in `gi_trace_rt` for hidden and off-screen hits (replaces
     `gi_rt_fallback`).
  4. Raise the radius; A/B for colour bleeding and interior leaks.
  5. EN3 closure, then misses use the sky irradiance outdoors only.

#### D-EM: emissive surfaces (a: P1-small, b: P4)
- **(a) Emissive in the bounce.**
  - Find the pre-lighting additive target: B10G11R11 + D24S8 at render resolution,
    the passes before the tiled lighting (units 78-85), and the third colour input
    of compose `5c3fdabc`.
  - Add it to `prevRadiance` (it is available during the lighting).
  - Verify which materials write there (glowing glyphs? lava? or only sky/FX), and
    exclude the sky (depth = 0).
  - Cost 0.02 ms.
- **(b) Emissive triangles as lights** (RTXDI). This needs per-draw emissive
  detection and radiance. Not recommended until (a) shows that emissive
  surfaces are common.

#### D-TR: ray-traced thickness for translucency (P3)
- **Data:** a mask of SSS/foliage pixels (to find: G-buffer flag bits in RT2.w, the
  stencil, or the SSS profile index); TLAS.
- **Injection:** a delta into b10 before the SSS passes. A sun and light
  transmission term `c·albedo·exp(−σ·thickness)`, with the thickness from one
  ray along −N, inside the object.
- **Cost:** 0.05-0.1 ms.
- **Steps:**
  1. Find the mask.
  2. Thickness debug view.
  3. Back-lit leaves and ears A/B.
  4. Set the RR SSS guides (QUALITY Q12) first.

#### D-RFR: ray-traced refraction for glass and water (P4)
- **Data:** XFB on the forward pass (unit 125) for the refractive surfaces; their
  IOR and tint from the forward FS.
- **Injection:** patch the forward FS families' refraction sample (the unit-124
  copy).
- Deferred. RR's guide warns that refraction resampling breaks its sample
  independence.

#### D-VOL: RT sun visibility for the volumetrics (P3)
- **Data:** cascade-caster capture (6.7 item 2) for the sun-view geometry; the
  froxel grid of `ca8adce7`.
- **Injection:** a per-froxel sun visibility volume (1 ray per froxel), read by a
  patched `ca8adce7` in place of its cascade compare.
- **Cost:** about 0.1 ms.
- Worth it only if cascade-resolution aliasing in the shafts is visible in A/B.

#### D-PT: path-traced reference mode (P4, a tool, not an effect)
- A debug mode accumulating 1 path per pixel per frame over a static camera.
  - The primary hit comes from the G-buffer; secondary hits use EN2 materials,
    EN1 lights with RT shadows, and the sky.
- It writes to `rt_debug` only and replaces nothing.
- Purpose: a "how wrong is the hybrid" reference *within the captured geometry*,
  for the A/B metrics of QUALITY R6.
- Infeasible as a product for these reasons:
  - **Geometry:** culled and cut-away surfaces are not in any capture (even with
    EN3, interiors are closed only where cascades reach).
  - **Materials:** only the primary hit's material is exact.
  - **Lights:** the list is view-culled; emissive materials are unknown.
  - **Look:** the game's look depends on baked probes and deliberately unshadowed
    fill lights.
  - **Cost:** 5-8 ms with RR.

### 8.7 Ranking by value/effort

| Rank | Item | Value | Effort | Why |
|---|---|---|---|---|
| 0 | QUALITY P1 fixes (Q1, Q2, Q4, Q11) | high | low | they make every later effect correct and cheaper to denoise |
| 1 | Soft sun shadows (planned, 8.5) | medium | **low** | data complete (angular radius known), 0.1 ms, RR denoises |
| 2 | D-LS local-light shadows | **high** | medium | fixes BG3's most visible lighting flaw (leaks from unshadowed lights); all data readable |
| 3 | D-RF reflections, tiers (a)+(c) | **very high** | medium | BG3 has no SSR at all; the on-screen tier needs no new data |
| 4 | D-EM (a) emissive in the bounce | low-medium | **very low** | one target capture |
| 5 | Cascade-caster capture (planned) + EN3 retention | enabler | medium | closes interiors for GI, adds off-screen casters, shrinks D-RF tier (c) |
| 6 | D-RF tier (b) + EN2 | high | high | off-screen reflections |
| 7 | D-GI2 screen-seeded SHaRC | medium-high | high | room-scale GI; needs EN2 and closure to avoid leaks |
| 8 | D-DI ReSTIR DI | medium | medium | only after D-LS, and only with many lights |
| 9 | D-TR, D-VOL | low-medium | medium | niche |
| 10 | D-RFR, transparents/particles | low | high | forward-pass capture and shader patching |
| — | D-PT | reference only | medium | not a product |

## CPU descriptor batching (historical implementation, 2026-11-02)

`perf_submit_batch` retired 2026-11-04: no saving beyond noise (OFF 20.2/20.7 ms, ON 20.3/20.3 ms); hidden developer INI only, default 0; shared descriptor/submit paths retained.

`perf_submit_batch=1` (hidden developer INI only, live, default 0) coalesces hit-set
clone copies and texture-table overlays by excluding overwritten copy intervals.
Vulkan still applies writes before copies; final descriptors equal copy then overlay.
Sun clones keep their copy boundary and combine subsequent independent writes,
with image/sampler arrays owned until the call returns. Scene slots, pools and
retirement are unchanged. Game updates still reach the driver in original order;
tracker write bookkeeping takes one lock per call, then preserves the existing
OMM callback and sequential copy boundary. No ABI or shader request changes.
Historical offline checks and A/B: [codex-perf-submit2](progress/codex-perf-submit2.md).
No saving was demonstrated; no further qualification queue.

## Immutable alpha header cache candidate (2026-11-03)

`rt_alpha_metadata_cache=1` (specialization constant 1022, off by default) lazily
caches the bound record-buffer size and header count, stride and policy inside
one shader invocation. The descriptor and its header are immutable throughout
that dispatch. No values survive into another invocation, frame or scene slot.
Range arithmetic, texture reads, footprint-derived LOD, effective cutoff,
stochastic threshold and UNKNOWN behavior remain unchanged. A range-only lookup
does not read the header. This adds five live integer words in the enabled path;
the performance gate must reject a register-pressure regression.
The production helpers pass 3,840,000 exact integer output/range checks over
10,000 descriptor epochs in `alpha_metadata_cache_test`. This is a helper proof,
not a full-scene alpha/replay certificate. GPU output and key-off shader identity
receipts belong in [gpu-opt1](progress/gpu-opt1.md). Planned camp alpha saving is
0.1-0.3 ms; measured saving is unknown. Check camp and S8 foliage with the prepared
same-run OFF/ON/OFF static identity and separate 300-sample cost windows.

Production GPU replay also passes solid/clear native alpha fixtures (277 real candidates per case), independently and with the other candidates; GI/depth/statistics bytes match OFF. Full-scene coverage and GPU savings remain unmeasured.

Key-off gate: 173 requests/194 entries match HEAD after specialization and unused-capability removal; validation passes. All keys remain off until live cost and combined Ultra qualification.

## Origin normal load on demand (2026-11-05, opt-origin, rejected; historical design)

`rt_origin_lazy_normal=1` (specialization 1027, default 0, developer Performance
control) defers the shading-normal fetch/decode/lighting-space normalization until
the final geometric normal is degenerate, the output is nonfinite, or statistics
need the comparison. The original fallback order and orientation remain intact.
The origin texture, physical hair texture, ray queries and counters are unchanged.

At 3840 x 1600 the 8 x 8 pass launches 6,144,000 invocations / 96,000 groups.
Positive depth requires four neighbouring depth reads, a geometric normal, an
alpha-tested closest ray in a depth-tolerance window, and hit-normal reconstruction.
Native hair can add procedural intersection work; physical hair origin can add a
competing-receiver query. Cascade selects the instance mask, not another ray.
The supplied inn profile averages 1.18 ms; no per-operation timings are available.
All pixels can be read by later full-resolution or arbitrary hit-pixel consumers:
skipping rays based on the GI sampling grid is unsafe. Background already exits.
Aborted-hit normal lookup can update guard counters, so that skip is not exact.
No occupancy change is claimed; group size remains 64. ON removes a normal fetch
and its decode for finite nondegenerate output with statistics off, and shortens
the lifetime of three normal components across traversal.

OFF release equivalence against main passes all three origin requests, covering
17 assignments, with 1027 fixed to 0; request delta 0. The decision fixture covers
10 named cases and 16,384 edge states. Compiled ON has a conditional normal fetch
after traversal in the actual native inn state; OFF fetches before traversal.
Draft `tools/live_batches/opt_r2/origin/` uses one static inn run, OFF/ON/OFF/ON,
detailed logging, separate origin GPU cost / float pre-post energy / rendered K8
QA windows. Each ON must save more than OFF-repeat cost spread, preserve energy
within OFF-derived noise, and pass the existing temporal detector versus both OFFs.
GPU saving remains unmeasured; no game launch or queue staging occurred.


Round 1 outcome (2026-11-05, opt-apply1): rejected for no saving. Inn origin pass
OFF 1.514 / 1.495 ms, ON 1.634 / 1.520 ms (ON increases 0.1295 / 0.0155 ms
against OFF-repeat spread 0.019 ms in the retained analysis). Evidence:
`captures/queue/logs/opt-origin-inn.1.012845.log`. Code, key, shader branch,
prewarm registration, candidate-only tests and `tools/live_batches/opt_r2/origin`
were removed. Specialization ID 1027 remains reserved. The design and offline
receipts above are historical, not a pending candidate or quality pass.
