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
  tangent (but the native FS writes a world-space tangent: 5g, key rt_hair_tangent_world), RT2.w = class 5 |
  the draw's light channel. The draw's own material descriptors are bound (same pipeline layout).
- **Velocity (Velocity FS b70cadc5, `shaders/rt/hair_primary_velocity.hlsl`).** Reads the pixel entry (no
  trace), evaluates the strand point at the same parameter in last frame's points (the groom update copies
  them before writing the new ones; camera-relative to last frame's camera), projects it with set 0 b5 `m2`
  and writes the native form: fragment + jitter minus previous pixel + previous jitter, in pixels. No
  previous points yet: the current point moved with the camera only. Depth test GREATER_OR_EQUAL, no writes.
- **Shadow cascades:** cards, unchanged. RT sees strands with `rt_hair_groom`, but replaces the native mask only
  on trusted receivers. Roof/off-screen fallback still retains card shadows, including on skin and neck.
  M2.2 remains open; see [the cascade audit](progress/claude-hair-cascade.md).

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

### M4.1: diffuse indirect response (offline, 2026-11-04)

`rt_hair_fibre_gi=0/1` is live, Developer only, and off in every preset. It requires on-screen strands.
The referenced `RTX_HAIR_COMPLETE_PLAN.md` is absent in this checkout; this implements C4 below.

Source: [Zinke et al. 2008, Dual Scattering Approximation for Fast Multiple Scattering in Hair](https://www.cemyuksel.com/research/dualscattering/dualscattering.pdf),
eq. 5 (forward attenuation), eqs. 10 and 14 (local backward scattering), eqs. 18-22 and Fig. 5
(angular integration and direct/global combination). The following slab integral and its calibration are
our extension, not a volume-albedo formula claimed by the paper.

Let q = 1 - a_f^2. The local multiple-backward fraction is
`B = a_b a_f^2/q + a_b^3 a_f^2/q^3`; `f_back` is its normalized angular kernel already implemented in
`dual.shade`. For the first visible fibre, sample depth `z = mu_o Exp(1)` and incident crossings
`n ~ Poisson(z/mu_i)`. Here `mu_i` and `mu_o` are positive slab-normal cosines, incident directions are
cosine distributed, view directions uniform over the upper hemisphere, and fibre tangent and offset
are random. With `I0 = 1[n=0]`, `I+ = 1[n>0]`, the integrated dual model is:

```
S = 2 pi mean[(mu_o/mu_i) I0 f_s]
M = 2 pi mean[(mu_o/mu_i) k {
      d_f a_f^n I+ f_s + d_b (I0 + pi d_f a_f^n I+) f_back }]
A = S + k_volume M
```

The `1/mu_i` changes incident flux sampling to the fibre's radiance measure. `d_f = d_b = 0.7` and
the existing directional correction `k` are unchanged. Directional `k` alone is not calibrated for
uniform incident radiance. A separate `k_volume = (A_MC - S)/M` is fitted on grey diffuse slabs:
100,000 adjoint paths per grid cell, two seeds, 4,000 bounces. The calibration JSON records S, M,
k_volume and both references. We bake A for a cheap lookup, linear in colour and roughness.
The nonabsorbing semi-infinite slab has `A(1,beta)=1` by conservation; it is an explicit endpoint,
not the direct table's clamped colour 0.9. Other inputs inherit the calibrated ranges: colour >= 0.02,
roughness 0.35-0.7 (clamped outside). This is an isotropic random-fibre volume response, not directional GI.

The diffuse reference reuses `bsdf.slab_albedo` with an analytic attenuation-mixture sampler for
`bsdf.evaluate`: longitudinal von Mises-Fisher marginal and inverse-CDF logistic azimuth. Its angular
moments agree with independent uniform-direction integration within four standard errors. This avoids
the old candidate walk's multiplicative weight noise near white; normal-incidence legacy tests are unchanged.

`dual.volume_albedo` and `hd_volume_albedo` use generated twin tables. Independent held-out RGB slab
walks (50,000 paths, seed 91) give maximum channel errors 1.3882% for CHAR_Hair at beta 0.41,
0.7563% for (0.6, 0.45, 0.3) at 0.55, and 0.8466% for (0.85, 0.7, 0.5) at 0.7.
White furnace: A = 1 at beta 0.35/0.5/0.7; an independent 20,000-bounce white walk returns 0.9842
(finite-path truncation). GPU vs CPU: 2,006 volume samples, maximum relative error 1.46e-7.

Integration is in `gi_composite.hlsl`, where `gather_history` already supplies the exact irradiance
used for `gi.rgb * diffuse_colour * strength`. On class 5 pixels with a sealed, current-frame strand
entry at the winning depth and a valid segment, replace the response with `gi.rgb * A * strength`
before adding it and before storing next frame's bounce source. This avoids adding and subtracting the
Lambert term, and needs no new irradiance binding or ABI. Native game ambient is not estimated or removed.
The earlier direct fibre pass cannot use its D-LS bindings to access this gathered GI result.

Only the two extra composite requests have `RT_HAIR_FIBRE_GI_REFERENCE=1`. The define isolates the
physical-storage-buffer/shaderInt64 capabilities as well as the arithmetic. Key off uses the original
Pipe, so a pending asynchronous feature pipeline cannot run after switching off. Request delta: +2.
All seven ordinary requests depending on the edited shaders are byte-identical to starting HEAD;
all nine explicit touched requests compile and pass spirv-val. Live appearance and GPU cost are unmeasured;
no game launch is authorized for this task. Keep the key off pending those checks.
Build succeeds and 145/146 CTests pass, including both hair tests. The inherited water surface gate fails
13 OFF comparisons against merge-base 009f60f, although all its shader inputs are unchanged from starting
HEAD 97e2487. The unmodified water gate passes with `--baseline HEAD` (16/16 OFF, 2/2 ordinary, 16/16
production validation). Full-suite acceptance remains blocked by that unrelated baseline difference.

### M4.2: specular indirect on strand pixels (offline, 2026-11-07, branch claude/hair-m2-m4-light)

`rt_hair_fibre_spec=0/1` (knob `exp_hair_fibre_spec`, Advanced, experimental, dev only), off everywhere. Needs
`rt_hair_groom_primary` and D-RF. A separate pass, `gi/hair_fibre_spec.hlsl`, runs right after D-RF on D-RF's
layout, sets and push constants (core/gi.cpp `hair_spec_dispatch`, inside D-RF's timed region), on pixels whose
G-buffer sample is a strand of this frame (`gi/hair_strand_pixel.hlsli`, shared with hair_fibre.hlsl):

```
b11 += A_0 L(wi) - Tp
```

- Tp: the game's class-5 specular ambient, transcribed from the lighting permutation 3033c895: two-sided, 0.3 of
  the probes about R = reflect(-V, N) and 0.7 about -R, each times F0 DFG.x + DFG.y at max(|N.V|, 1/512); local
  probes mixed the same way per probe; times k = b13[0] 100 and Lagarde's SO. D-RF leaves class 5 alone
  (`rf_models=3`), so b11 still holds exactly this term. Aerial perspective is not modelled, as in D-RF.
- wi: one exact R-lobe sample at a uniform offset h (`hair_bsdf.hlsli hb_sample_r`, twin `bsdf.py sample_r`): the
  d'Eon longitudinal lobe as the marginal of a von Mises-Fisher ring about the tilted cone, the trimmed logistic
  azimuth about -2 gamma_o by inverse CDF. Weight A_0 = Fresnel(cos theta_o cos gamma_o).
- L: D-RF's tier (a) (an on-screen hit the camera sees: last frame's radiance, screen-edge fade w), else (and for
  1 - w) the game's own probes in direction wi at its sharpest roughness 0.09, times k and SO.
- TT, TRT and multiple scattering of indirect light stay M4.1's volume response. Known overlap: M4.1's A also
  contains single-scattered R of the near-field GI bounce (a few per cent of A).

Gates (offline): R-lobe white furnace, sampled estimator against an independent quadrature of `bsdf.r_lobe`
(Gauss-Legendre in gamma), beta 0.09/0.41/0.7 x theta_o 0/30/60/80: worst 0.30% (limit 3%). Sampled directions
follow r_lobe (a test function's mean within 4 SE + 1%). GPU vs CPU, 16,000 samples: direction error p99 9.1e-7,
max 1.9e-5; weight relative error max 2.4e-5. Key off: the pass is not dispatched; ordinary requests untouched
(below). Request delta +2 (`gi/hair_fibre_spec.hlsl` with and without OMM). The Tp transcription has no offline
reference (no capture of b11 on strand pixels): live check is part of L3 (below). Cost unmeasured.

### M4.3: every traced local light on strand pixels (offline, 2026-11-08, branch claude/hair-m4-locals)

`rt_hair_fibre_mapped=0/1` (specialization 1030, knob `exp_hair_fibre_mapped`, Advanced, dev only), off; needs
`rt_hair_fibre=1`. Before: gi/hair_fibre.hlsl replaced the sun and every light without a game shadow map; lights
with a map kept the game's hair term with the cards' atlas shadow (RTX_HAIR_SHADOW_MAPS [H]). Now those too:
the buffer holds E S (bg3_hair_spec + bg3_hair_diff, texp = 1 - S) with the game's hair atlas term S
(3033c895: sampled from P + L 3.5 nb sqrt(d2), no N.L gate; `hf_atlas_hair`), and it becomes E F(n) v with the
scene's traced visibility v and the strand count n, as for every other traced light. Only where D-LS trusts
the trace for mapped lights (`ls_shadowed` 1, or 3 with the light on screen) and the ray is known; elsewhere the
game's term stays. Unmapped lights were already exact per light (the pass loops over every froxel light; D-LS's
RIS selection is unbiased against it). No request or ABI change.
Gates: the removed game term GPU (bg3_materials lobes) vs `bsdf.game_hair_light`, 12,000 samples: relative
error p99 9.5e-7, max 5.4e-6. N-light fixture (20,000 lights on a sphere, 4 pi / N each): white single
scattering sums to 0.99973 / 0.99997 (beta 0.41 / 0.7; analytic 1); the dual response summed over the lights
equals its CPU twin within 3.4e-7 at n = 0, 1, 3. (Its full-sphere sum at fixed n is 3.4-7.0 for colour
(0.85, 0.7, 0.5): fixed n over the whole sphere is not a volume, so this is not an energy bound; the dual
model's calibration is the path-traced slab of 3b.) Identity: 218/221 requests byte-identical to main 7f86f343;
the 3 hair_fibre.hlsl requests strictly equivalent at 1030 = 0. Cost (estimate, unmeasured): per strand pixel and
mapped light one 12-tap atlas PCF, one scene ray, one groom query (at most 8 crossings) and one dual evaluation,
the same as an unmapped traced light; the pass cost grows by the mapped share of the froxel's lights.
Check in game: Test Dialogue1 / Campsite with torches (shadow-mapped lights), hair lit by them: the card-shaped
self-shadow on the hair is replaced by strand shadows; key on vs off in one run.

## 4. RR, DLSS-G, DoF
RR guides are built from the G-buffer at its pass end (`layer/ngx.cpp record_guides_at_gbuffer`): strands
there are ordinary pixels (albedo, normal, roughness). Motion vectors from the velocity variant; depth from the
prepass/G-buffer variants: DLSS-G gets consistent depth/MV. DoF reads the game's depth: strands are in it.
Sub-pixel strands: section 5d (M3.2).

**M3.3 hair guides (`rr_hair_guides`, specialization 1043 `kSpecIdRrHairGuides`, live, off; offline 2026-11-07).**
Found: `rr_guides.hlsl` read RT2 as (roughness, specular, metalness) for every class. On class 5 (cards and strands)
RT2.z is the backlit factor (`bg3_mat_setup`, 3033c895: metalness forced to 0), so a backlit hair pixel fed RR a
metal: diffuse guide base (1 - backlit), specular guide the base colour. With the key: class 5 (RT2.w bits 0-2)
gets metalness 0 (diffuse guide = base colour, as `sf.diff_col = albedo` in the game's hair lighting), roughness
max(0.09, RT2.x) (the lighting's clamp), specular guide `env_brdf_approx2(F0 0.046520568, rough^2, N.V)` (the game's
R-lobe Fresnel constant, eta 1.55). The normal guide is RT0: the fibre normal on strands (section 2), the card
normal on cards. There is no material-class input in DLSSD (`nvsdk_ngx_defs_dlssd.h`); the class acts through
these guides only. Specular hit distance: D-RF's, unchanged: `rf_models=3` does not trace class 5, so hair pixels
carry kNoHit (the hair specular is direct light from R/TT/TRT, not a traced reflection); a hair hit distance
would need a gi/ change (another stream's files). Pipeline: `layer/ngx.cpp build_pipeline` passes the constant and
keys the pipeline on (shader generation, value), so a toggle rebuilds the compute pipeline like a shader edit.
Gates: CTest `hair_groom_test` test_rr_hair_guides (the terms match `bg3_materials.hlsli`, F0 of eta 1.55, the
layer wiring). Key off: rr_guides.hlsl strictly equivalent with 1043 fixed to 0 (identity numbers in 5d).
Live check (L2, Test Dialogue1): with `rr_hair_guides` toggled in one run, hair highlights stay put and the mean
hair luminance of the pre-post colour is unchanged within noise (guides change demodulation, not energy); hair
noise/smear under RR is the observable.

## 5. Cost model (bench numbers, RTX 5090)
Groom LOD 0: 0.25 ns per ray on hair (3.7M rays 0.91 ms, shadow-like). Primary rays are coherent, so this is
an upper bound. Close-up head at 1.6M hair pixels: ~0.4 ms prepass trace; the G-buffer and velocity
variants read the record (no trace). Plus refit 0.1 ms per character and 11 MB previous points at LOD 0.
Measured live by `[hair primary] strand G-buffer draws GPU X ms/frame` (a BOTTOM_OF_PIPE timestamp pair around
each swapped G-buffer draw, up to 64 per frame, averaged per 300 frames). VRAM: pixel record 16 B per render pixel
(41.7 MB at 2560x1067) plus per point 8 B UV + 12 B previous position (LOD 0 ~19 MB, LOD 1/2 ~10 MB per character).
Live job: tools/live_batches/hair_primary (READY_FOR_LIVE).

Fibre passes (branch claude/opt-gpu, 2026-11-08): `[hair fibre] fibre pass ... GPU median X ms` and
`[hair fibre] spec pass ... GPU median X ms` (timestamp pairs around the two dispatches, medians per 300
dispatched frames, common/hair_fibre_timing.h). They are inside the `ls:` and `rf:` lines: never add them twice.
gi/hair_fibre.hlsl now has one call site each for the scene visibility query, the groom query and the fibre BSDF
(the sun is the light loop's last iteration) and skips non-hair pixels before ls_lite's loads. Offline compile
12 s -> 3 s, driver ISA 912 -> 260 KB, 168 registers both; same expressions in the same order, GPU output
equality not yet checked (no replay contract for this pass).

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
- Roots away from the head (45 alone): hat hair hanging from a hat rim and buns; root/tip shading only.
- Companions and orientation (2026-11-03, user: companions first). Companion hair through their templates and the
  origin presets: Shadowheart, Astarion, Lae'zel, Gale, Karlach, Jaheira, Minthara and the generic origin pass;
  Halsin was reviewed only for "roots away from the head" (369 of 842 cards) with images within 1-3% of the cards:
  strands lying along the scalp (radial share 0.20) decided it. The orientation test now counts only strands more
  radial than tangential (|d_tip - d_root| > chord cos 45, tested); rerun of the 109 reachable styles with that
  reason: 26 now pass (Halsin LOD0 and LOD1 among them), 63 still have radial reversed cards (hat hair, buns).
  Gale's beard stays in review (LOD0: coverage +0.11, IoU 0.76; LOD1 reject IoU 0.55): unchanged under six atlas
  grids (IoU 0.755-0.759), the alpha-weighted width and the texel-size mip. Short beards share it (26 player, 13
  named NPC): a systematic card/strand difference the reference does not explain; to pass: a card reference that
  averages the game's dithered alpha test (C5), or an explicit visual review (batch --accept-review).
  Before M1: 745 pass, 346 review, 126 reject. Full rerun after M1 (2026-11-04, 1217 styles in 52 min, batch v4:
  fibre reference, radial orientation, root from geometry): 877 pass, 295 review, 45 reject (reachable: 785 / 234 /
  44). Player 373 / 40 / 19, companion 5 / 0 / 1 (Gale's beard LOD1, IoU 0.68; its LOD0 passes), named NPC 160 / 41 /
  10, generic NPC 13 / 10 / 2, hat variant 201 / 128 / 8, item 31 / 14 / 4, CV only 2 / 1 / 0; unused 92 / 61 / 1. Trace cost follows the hair pixels and only logarithmically the segments; refit and
memory follow the segments: so the crowd budget is in segments.
`rt_hair_groom_crowd_ksegments` (off; GUI Performance "Strand hair crowd budget": 3500 / 1800 k): each frame the
groom instances of the last frame share the budget, largest on screen first, each at its screen-size LOD or the
first coarser one that fits; one that fits at none keeps its cards (the slot is kept: no rebuild when it fits
again). `hair_groom::plan_crowd` (unit-tested); segment counts per LOD come from the files' LOD headers at index
time. Log: `[hair groom] crowd plan: N instances, budget B k segments: LOD a/b/c, d keep their cards`. Live job:
tools/live_batches/hair_crowd (Dialogue1 + Campsite, cards / RT / on screen / crowd; READY_FOR_LIVE).

## 5b. Fail-safes (after the 2026-11-03 user trial: device lost twice, the player's hair gone)
- Late recording (2026-11-04): retain unresolved depth keys across current and previous prepass epochs; resolve both before rotation, count only expired previous keys, and log the first 4 orphan key prefixes with prepass/drop frame_index per 300 frames (READY_FOR_LIVE; no shader/request or ABI change).
- Device lost (17:01, 17:04, main ac1380e5): the groom point update read the matched draw's card triangles at its
  `soup_off` also for a *held* draw, whose offset is a mesh-pool snapshot offset: a read at +1.97 GB of a 2 GB soup.
  Now `hair_primary::soup_update_ok` (captured this frame, not held, the whole triangle range inside the soup, the
  groom's triangles <= the captured ones, stride >= 24) gates the update in bvhsplit (the slot is kept, no groom
  instance that frame: cards in every pass), and hair_groom_update.hlsl bounds every read by the draw's triangle
  count and the buffer size (a point outside keeps last frame's position).
- Coverage repair (2026-11-04, branch claude/hair-coverage-accept, READY_FOR_LIVE): the ratio is now
  unique accepted strand pixels / unique card pixels from one G-buffer draw set and one applied tag.
  A card marker is written before ray/rebase/ownership/UV/tip rejection; a separate accepted marker is
  written after the last rejection. Rejected fragments never clear acceptance. Sealed hit entries remain
  reusable by velocity and other strand consumers; refused/other/traced diagnostics retain their meaning.
- Native FS inputs contain material, transform and UV, but no index-range draw key. A ray miss cannot
  supply groom attribution. Specialization constant 1102 supplies a compact draw-key slot in [0, 16).
  The layer assigns distinct live keys at publish, retains their slots while live, and stages the groom-to-slot
  map with that header. Crowds sharing a key share a slot; overflow keeps cards (no probe or swap) and logs
  `draws without a slot N`. Each supported G-buffer pipeline gets 16 strand and 16 masked-probe
  specializations, down from 64 + 64 (shaders/variants.txt request delta 0).
  One 8 B/pixel marker pair stores `(tag << 8) | slot` in each word; the count compares low 24 tag bits
  for four newest tags and accepts only identical card/accepted words. Including the 16 B cache,
  allocation is width * height * 24 B: 65,556,480 B (65.6 MB, 62.5 MiB) at 2560 x 1067, independent of grooms.
  In unswapped frames prepass depth admits only the front card layer (one owner); swapped overlapping keys use last-writer attribution for both words.
  GPU count/probe cost remains unmeasured; allocations beyond 32-bit count offsets keep cards.
- Card occlusion queries, precise-query feature requirement, private prepass probe header and cross-pass
  CoveragePairing are removed. Native-card draws get the masked G-buffer replay; swapped draws write the
  identical markers themselves. Both sides therefore remain valid without prepass card depth.
- A tag applied by multiple recorded G-buffer frames is ambiguous: invalidate prior evidence immediately,
  reject its readback, and log `ambiguous tags N`. Evidence retains its bounded lifetime and unchanged
  kCoverMin/kCoverMax. Counts with card pixels and zero accepted pixels are failures, not missing evidence.
- The old period log mixed newer failed ratios with swaps using earlier fresh evidence. Each key now logs
  `key:accepted/card=latest,swapped_frames,used=min..max` for all successful pass decisions. Every
  swapped key also has an untruncated `coverage used` line with its ratio range and decision count. New failed
  evidence prevents subsequent swaps; old late successes cannot replace it. Probes continue with cards.
- A frame/key latch records prepass depth drops at p for G-buffer draws in [p, p + 2].
  Refused strands restore native GREATER_OR_EQUAL depth with writes; sibling draws retain membership.
  A key seen on a G-buffer pipeline with neither strands nor fallback blocks depth drops for 300 frames.
  Only a latched native G-buffer draw with no fallback counts as an orphan, at that draw.
  The first four record key, prepass/draw frame, reason (no variant/no fallback) and pipeline handle.
  Pass boundaries, absent draws and hitches never increment the counter. Epoch bookkeeping is removed.
  CPU regressions cover both recording orders, interleaving, a hitch, an uncovered sibling,
  one true orphan and a 60-frame three-key hv9 aggregate fixture (180/180/180; 0 orphans).
  Historical hv9 aggregates cannot exclude unlogged native siblings: the old missing-variant,
  missing-fallback branch logged neither fallback nor refusal. No per-draw replay is possible.
  See section 9 for failure modes and docs/progress/claude-hair-consolidate.md for the evidence boundary.
- Offline validation: final build and all 149 CTests passed, including host atomics lint and the corrected
  prepass -> G-buffer -> delayed-publish ordering regression. Revision CTests took 121.24 s. G-buffer, velocity and count requests
  compiled and passed spirv-val (3/3; 2 rebuilt, velocity cached); no shared shader include changed. Cache: worktree build/shader_cache.
- Card markers use the native prepass dithered alpha test at card UV, including combined masks and grazing.
  Transparent card pixels are excluded before strand rejection, matching the bound's composited-opacity basis.
- Derived bound: `kCoverMin = 0.8913102349775665`. The unchanged batch-4 state has 877 passing styles
  (2,631 front/side/back views). It stores rounded `coverage_delta = mean(strands) - mean(cards)`, not ratios.
  `tools.hair_groom.coverage_bound` regenerates only opacity from the existing groom/card cache and DDS masks;
  all 2,631 differences reproduced within rounding (0 failures, 920.5 s). Expected sample totals are sums of
  composited opacity over the batch's shared foreground union. The minimum ratio is Minthara LOD1 side:
  `S = 8556.228538087944`, `C = 9396.798707214493`, `r = S/C = 0.9105471772550376`.
  First-order one-frame count uncertainty is `sigma = (sqrt(S) + r * sqrt(C)) / C = 0.01923694227747121`;
  the bound is `r - sigma`. This uses independent spatial Bernoulli samples (each count's variance <= its mean),
  and permits arbitrary covariance between card and strand totals. It is a model of one frame's count noise,
  not measured temporal/dither noise. The offline composited-opacity ratio models visible coverage; precise
  live markers count unique pixels rather than overlapping raster layers; live qualification remains open. No tolerance was tuned against the failed hc2 run. Full reproducible artifact:
  `captures/hair_groom_batch/coverage_bound.json`; compact provenance and unchanged state SHA-256:
  `docs/progress/claude-hair-coverage-bound.json`. Regenerate with
  `python -m tools.hair_groom.coverage_bound --report-dir <main>/captures/hair_groom_batch` (cached, BelowNormal).
- Cost: no game was launched. Marker memory is given above; GPU ms is unmeasured. Use the existing
  strand G-buffer timestamp line and a dedicated live cost window for the count/probe overhead before promotion.
- Device lost after the F8 reload (hv1 18:40, cause not confirmed): a new level reuses Vulkan handles for other
  objects. The native hair FS module map is erased on vkDestroyShaderModule and on reuse (a reused handle could give
  a non-hair pipeline a strand variant). `hair_primary::LoadGuard`: a new G-buffer depth image marks a level load;
  the groom gate, pixel record and feedback reset and strand swaps pause 300 frames (5o, rt_hair_warmup: until the
  groom state has been settled 30 frames, at most 300). bvhsplit drops the groom join
  verdicts, UV joins and UV readbacks in flight at the same boundary (their identities are built from handles).
  Live bisect: hair_validation hv5 (one F8 per stage: native + scalp, + physical origin, + groom, + primary, 60 s
  each); hv4 repeats the reload under GPU-AV.
- Device lost in the first level load with rt_hair_groom_primary latched at startup (hair_crowd hc1 2026-11-04
  00:25: first publish at frame 1479 while the level streamed, two strikes at 1482, device lost at 1489; the read at
  0x2270524000 is in no buffer of ours, cause not confirmed). The menu publishes no groom, so LoadGuard first saw the
  in-level image and opened no quiet window. Now the first image is a boundary too: no swap for 300 frames after the
  first publish. bvhsplit publishes a groom's draw key only from the second update of its points range (a new LOD or
  upload keeps the cards for that frame). Test: hair_groom_asset_test (level reload block, first image).
- Overlapping grooms (review of f5b339eb): the pixel's closest strand of all grooms is cached once per pixel; a card
  fragment writes it only when it lies on its own card (`hp_on_card`: strand points lie on their card triangles, so
  within (radius bound 10 mm + segment length / 2) / |cos| of the card surface along the ray, plus the pixel's spread).
  The velocity variant applies the same test. Entries are sealed (`hp_seal`: word 3 mixes the tag with the other
  words), so a torn 16-byte store never pairs this frame's tag with an older key.

- The former zero-pixel strike/retry rule is superseded by measured per-key coverage. Adjacent pixel-record
  tags remain diagnostic and cannot qualify a swap. Grooms sharing an index-range key are measured together;
  shader draw attribution uses their compact draw-key slot in both packed marker words.
- Rebase (2026-11-04): the scene build is recorded after the G-buffer pass, so the strand passes of frame N read
  frame N-1's publish. The publish carries its build camera; rays start at camera_N - camera_build in its frame
  (shaders/rt/hair_primary_rebase.hlsli). Publishes up to one frame old are accepted; no build camera: no strand.
- Coverage gate (2026-11-04): G-buffer and velocity strand swaps need header order G-buffer
  (`rt_hair_groom_primary_order=1`) and fresh per-draw coverage evidence; header order prepass keeps the cards (hc2:
  the G-buffer read an adjacent frame's header in 296 of 300 counted frames; the player was half bald).
- `rt_hair_groom_primary_order` (off, live): the header is written in the G-buffer pass's own command buffer (cmd.cpp
  pass_ready, before the game's begin) from the newest staged publish whose build is recorded earlier in that command
  buffer or already submitted (`hair_primary::pick_publish`); none: no strand swap that frame, and the next prepass
  keeps its card depth. The G-buffer and velocity swaps need the draw in the applied header's groom set. Prototype for
  the case the live run confirms ('carry an adjacent frame's tag' lines); not run live.

## 5c. Dithered strand tips (C3a, offline)
`rt_hair_groom_tips` is a live Developer switch, off in every preset. Header word 15 is flags;
bit 0 enables the native depth/velocity TAA alpha test at the strand UV. Both variants use the
mask combination M.w (native velocity m2/m3 map to G-buffer m5/m7), fragment pixel centres,
header tag minus 1 modulo 4, and the native auxiliary x jitter (G-buffer location 2, velocity
location 6). SoftenTipsAlpha = 0 and DontTouchMe_Beard_Alpha = 1; no card grazing factor on a
cylinder. Failed coverage discards the fragment after accepting/caching the strand hit; the
pixel record and its consumers are unchanged. Velocity reconstructs UV from the same segment
endpoints and cached parameter. Flag off bypasses the added test and velocity texture reads.

CPU twin `primary.tips_keep`, CTest `hair_groom_test`: 4 x 5 pixel centres, four frame phases,
20 uniform native-jitter phases, 1600 samples per alpha. Kept shares 0/0.25/0.5/0.75/1,
exact for the five tested alphas; integration tolerance is one jitter bin (0.05). The spatial
pattern alone is not uniform. Shader request delta: 0. Live appearance, matching coverage/MV
and GPU cost remain unmeasured. In Test Dialogue1, enable on-screen strands only after the
reload safety fix is qualified, then toggle tips and inspect fading tips and temporal edges.

## 5d. Sub-pixel strand coverage (M3.2, offline 2026-11-07)
`rt_hair_groom_coverage` is specialization 1042 (`kSpecIdHairGroomCoverage`), off in every preset, needs
rt_hair_groom_primary. Compiled into the strand G-buffer pipelines when on at their creation (restart to enable);
header word 15 bit 3 (`kFlagCoverage`, set from the key every frame) switches it off live, so an L2 A/B is
1 -> 0 -> 1 inside one run. Footprint inflation was the preferred design but needs a second groom BLAS: the LSS
radius lives in the BLAS (`bvhsplit.cpp` radiusData), shared by the shadow/GI queries, so a ray cannot test a
capsule widened to the pixel footprint. Implemented instead: two rays per strand candidate pixel
(`shaders/rt/hair_primary_coverage.hlsli`): a quarter pixel either side of the jittered centre on the diagonal of
the pixel's checkerboard parity (mean offset 0: a symmetric 2-tap prefilter, no image shift). Both hit: the nearer;
one hit: kept on a checkerboard whose phase is a hash of the frame tag (not its parity: Halton jitter correlates
with frame parity); none: discard. Expected coverage = (h_A + h_B) / 2, unbiased. The pair is traced once per
pixel by the first fragment and sealed like the single ray's entry (velocity, count and later fragments
unchanged); spec 0 leaves the single-ray block untouched.

Offline gate (CTest `hair_groom_test` test_coverage; CPU reference `primary.coverage_reference`, 8 x 8 stratified
rays per pixel; `primary.subpixel_fixture`: 200 wavy 0.25 mm strands at 0.6-3 m, 256 px, 20 degrees; 16 Halton
frames). Single ray / pair: total coverage bias +0.10% / +0.63% (4 sigma 2.8%); 3 x 3 box RMS error 0.0649 /
0.0603 (-7%); 3 x 3 box temporal std 0.0567 / 0.0531 (-6%). Seeds 2 and 3: -4% / -5%. The gain is small because a
G-buffer pixel stays binary (opaque, no blend): any per-pixel scheme keeps the Bernoulli variance c (1 - c); four
rays measured the same as two. GPU vs CPU (CTest `hair_primary_gpu_test`, `-DHP_CHECK_COVERAGE=1` build of the check shader, LSS on the RTX 5090): the GPU pick equals the twin on >= 99.95% of the pixels any ray hits in every frame; GPU masks give bias +0.63% and box error 0.0603, as the CPU.
Cost: two traces instead of one per strand candidate pixel (bench: ~0.4 ms at a 1.6M-pixel close-up, so about
+0.4 ms); unmeasured live. Recommendation: keep off unless the L2 edge-flicker detector shows strands above the
cards and the pair measurably below them in the same run.
Key off (both keys): 215/218 requests byte-identical to main 2dcf0a88 (identical inputs); the other 3 (rr_guides.hlsl, rt/hair_primary_gbuffer.hlsl as file and manifest entry) strictly equivalent with 1042 and 1043 fixed to 0 (`tools/release_shader_equivalence.py --freeze-spec 1042 --freeze-spec 1043`, cascade control PASS); spirv-val 5/5 modules. Request delta 0.

## 5e. Strand depth in post (M3.4, offline 2026-11-07: no mismatch, no change)
Every post consumer runs after the G-buffer pass (FRAME_MAP steps 4-14): linear depth (step 4, from the D24S8),
the camera motion-vector pass (6b), AO/SSR (8-9), fog, transparents and water (13), DoF (14). They all read the
D24S8 as the G-buffer leaves it, or the linear depth made from it. With strands: the prepass clone writes no card
depth, the strand G-buffer variant writes the cached hit's depth (SV_Depth = the hit projected with the pass's own
vp, GREATER_OR_EQUAL with writes) and discards elsewhere, so a pixel's depth and colour come from the same surface:
the strand, or what lies behind the card (skin, wall). Kept-card draws use the native fallback pipeline, which
restores the card depth with the card colour (5b, DepthLatch).
CPU proof (`primary.card_fixture` / `post_depth` / `post_depth_report`, CTest `hair_groom_test` test_post_depth):
a 35-degree card at 1 m with 60 strands up to 2 mm off the sheet, a wall 0.4 m behind, the D24 quantisation and
the linearisation. Strand path: 0 halo pixels of 1576 alpha-tested card pixels; worst depth error 0.00054 mm,
within one D24 step (0.00117 mm). Control with the card depth kept: all 1576 card pixels carry a depth of another
surface (1253 wall pixels up to 0.43 m off, 323 strand pixels off by the card band). DoF, fog and water therefore
see the strand depth; no fix, no key. Live: a DoF halo check stays in the L2 PNG pairs (Dialogue3 depth of field).

## 5f. Every view (M5, offline 2026-11-07: participation counters; variants blocked)
Today strands replace cards only in the main view: hair_primary_swap requires the G-buffer depth image at the pixel
record's size. Portraits and other small views draw the static hair FS at another size and depth image; instanced
hair (FS 0ca5ee93, CHAR_Hair_STI_DEF) has no variant. Counted now: every draw of a hair pipeline (static or instanced,
recorded at pipeline creation whether or not it gets a variant) by pass and view, appended to the period line
`[hair primary] frame N: ...`: `hair draws strand/total: main G-buffer a/b, velocity c/d, prepass e/f; other views
G-buffer g, velocity h, prepass i; instanced j; card-only pass groups k` (`hair_primary::Participation`, CTest
`hair_groom_asset_test`). k = 0 is "no card-only pass left". Only while rt_hair_groom_primary is on; no key.
Why no variant yet (not safe offline): the strand shaders use one header and one pixel record, sized and indexed
for the main view and baked into every variant as specialization 1100/1101. A portrait G-buffer draw with the same
game pipeline would read and seal the main view's entries with this frame's tag, so the main pass could reuse a
portrait hit (wrong strand, wrong depth) and the coverage evidence would mix views. A second view needs its own header,
pixel record and pipelines specialized to that header (2 x 16 draw slots), its own depth latch and probe evidence,
and the portrait character's hair in the groom set (the groom follows the main prepass capture). Instanced hair needs
one groom per instance (the groom set is keyed per draw index range). Next step: run the counters in a live to learn
which views draw hair at all before building the second record.

## 5g. Grain attribution (offline 2026-11-07, branch claude/hair-grain: two keys, off)
Tool: `tools/hair_groom/grain_stages.py <trace dirs>` (per-stage HF and structure-tensor coherence over the class-5
mask of each trace). Data: the 2026-11-07 Test Dialogue1 A/B traces (strands a, f, fp0, fp2; cards e = rt_hair_groom 0;
a and e have the same lit hair mean 0.4165/0.4169, the matched pair).
- Per-frame HF energy: strands never exceed cards. Ratio a/e: albedo 0.99, roughness 0.92, tangent 0.95, lit 0.91
  (mask) / 0.65 (hull), RR output 0.78, final 0.96. Demodulated shading bands (fine to coarse) a 0.48/0.11/0.08/0.06
  vs e 0.49/0.11/0.08/0.07. Motion vectors vary 2.1x more per pixel (0.14 vs 0.066 px) but are clean: outliers
  > 0.5 px from the 9x9 median 1.1-1.2% on both. The previews' pixel grain is therefore not where strands differ.
- Line structure (coherence) is where they differ: albedo 0.313-0.333 vs 0.348 (strand spread 0.021), deferred
  lighting 0.277-0.286 vs 0.320 (spread 0.009: gap 4x the spread), RR output 0.753-0.796 vs 0.842, final 0.749-0.792
  vs 0.839. The loss doubles in the lighting, whose strand-only input is the fibre normal (gbuffer.hlsl:248, :294):
  N = V made perpendicular to T, so N.V = 1 on every strand pixel (RR specular guide, env_brdf(N.V), HF 0.11 vs 0.31,
  mean 0.047 vs 0.058). The game's hair diffuse `bg3_hair_diff` weights wrap(N.L) 0.67; ambient, probes and the RR
  normal guide use N too; the Marschner lobes (`bg3_hair_spec`) use only T. A view-facing N removes the
  clump-aligned shading the card normal gives: the hair reads as speckle instead of strands.
- Implemented (branch claude/hair-grain, offline): specialization 1045 `kSpecIdHairShadingSpace`, compiled into the
  strand G-buffer pipelines when `rt_hair_macro_normal` or `rt_hair_tangent_world` is on at their creation (restart to
  enable), then header flags 16 / 32 switch each one live (on/off/on in one session works if the session starts on).
  `rt_hair_macro_normal=1`: RT0 = the card fragment's normal, view(card_n), which is exactly the native RT0 (VS 41a168e2:
  location 1 = view(n), location 3 = normalize(n)). The tangent stays per fibre, so R/TT/TRT stay per fibre.
- Found while checking the grazing side note: the VS 41a168e2 outputs locations 3-5 (normal, bitangent, tangent) in
  world space and only location 1 in view space. So the card grazing term (gbuffer.hlsl:154-157, world frame with
  the world ray `dir`) is consistent: no bug, no change (twin `primary.card_grazing`, test_shading_spaces). But the
  native RT3 tangent is world space (3033c895 reads class 5 T unrotated, correctly), while the strand variant wrote a
  view-space tangent (gbuffer.hlsl:247/298): the strands' Marschner lobes used a tangent off by the view rotation
  (median > 1 deg on random frames; on screen the whole camera rotation). `rt_hair_tangent_world=1` writes t_world.
- Live A/B (Test Dialogue1, one session started with both keys on; `grain_stages.py --ab <labels> <dirs>` prints the
  gates): windows macro+tangent on / both off / tangent only (macro off) / both on, with `dump_raw=1` for the tangent
  gate. Macro normal: lit coherence min(on) - max(off) >= max(0.01, 2 |on - on|) (expected ~0.28 -> toward the cards'
  0.32); RR specular-guide HF on/off >= 1.5 (strands 0.11, cards 0.31); hair mean reported (shading-model change).
  Tangent: median |T.N_guide| on strand pixels <= 0.02 with the tangent on and macro off (fibre normal perpendicular to
  the world tangent; oct12 < 0.1 deg), > 0.05 with it off. Plus by eye: highlights follow the strands.

## 5h. One-frame strand dropouts (offline 2026-11-08, branch claude/hair-dropout)
Seen live: the hair-ab7 strands burst (frame 8 of 16: 4,689 hair pixels vs ~41,500, normal motion vectors) and the
220524 dump: the swapped hair draws drew nothing for one frame, cards already gone, so no hair; RR keeps the hair
difference 3.3x higher for the next 7 frames.
- Mechanism found by reading: rt_after_capture stores the build camera under frame_index F (rtscene.cpp
  g_this_build_frame); hair_primary_publish, inside the same build, asks rt_this_build_camera(frame_index()). The
  present on the queue thread advances frame_index (layer.cpp QueuePresentKHR, ++g_frame) and recording straddles it
  (the period lines show 106-127 of 300 counted frames with adjacent-tag strand pixels), so the publish can read F + 1:
  no camera, header without kFlagCamera. hp_rebase then fails for every strand fragment, but no swap gate reads the
  flag (coverage, depth latch and applied keys all pass): the draws are swapped with their card depth dropped.
  Not yet proven live: the new counter attributes each dropout.
- Counter (always on, CPU only): `[hair primary] dropouts (last 300 frames): N swapped G-buffer draws wrote under a
  tenth of their card pixels (no card marker a, no trace b, rays missed c, rejected after the hit d); first (G-buffer
  frame:key prefix:card/strand pixels:check): ...; publishes without the build camera n (recovered r); camera gate:
  headers refused g, submit upgrades kept back u`. From the existing same-pass marker counts (card vs accepted pixels
  per draw slot and tag) of the counted G-buffer frame, its swapped slots (GbFrame::swapped_slots) and the frame's
  traced/hit entries; `hair_primary::dropout_reason`, CTest hair_groom_asset_test test_dropout.
- Fix `rt_hair_primary_camera_gate` (knob exp_hair_primary_camera_gate, Advanced, live, off): the publish takes the
  build's own camera (rt_current_build_camera: set at the build's start, never a previous build's), and a header
  without kFlagCamera is never picked in before_pass nor taken by the submit-time upgrade (cards that frame; the
  fallback pipeline restores the card depth as for any refused swap). Every existing fail-closed rule stays.
  No shader change.
- Live check (Test Dialogue1, one session, key on/off/on, with a pan burst in an on window): the dropouts line shows
  0 swapped draws under a tenth in on windows, "publishes without the build camera" > 0 with "recovered" equal to it;
  off windows may show dropouts with check "no trace (camera rebase)". In the burst: hair pixels per frame within
  +-5% of the burst median in all 16 frames.

## 5i. The flash while anything moves (offline 2026-11-08, branch claude/hair-flash)
User run 2026-11-08 00:04-00:08 (camera_gate on): flashes while the camera or a character moves, strands steady when
still. The 5h counter then reported ~1000-1500 "no card marker" dropouts per period: a misreading, fixed here.
- Counter bug: the count runs in the scene build, which the current-frame header orders before this frame's G-buffer
  on the GPU, so it reads the previous G-buffer frame's markers; the counter judged the next frame (s.gframe) and saw no
  markers for any swapped draw. Now it judges the frame the coverage evidence uses (newest tag with markers, unique
  G-buffer frame), and the trace counts only when they belong to that frame ("unattributed" otherwise).
- Cause 1, tag reuse: the strand passes seal pixel entries and markers with the header tag, the publish's frame. Frame N
  takes publish T at submit; frame N + 1 falls back to the same publish when its own is incompatible ("different grooms",
  0-47 per period, only in moving periods). Both seal with T: N + 1 reuses N's entries without tracing (N's camera,
  jitter and pose: holes and stale strands when anything moved) and the markers merge (ambiguous tags 24-28 per moving
  period, 0 when still), so coverage is invalidated (86-420 refusals) and the draws flip to cards and back (strand swaps
  resumed after card frames 15-52 per moving period, 0-9 when still; native depth fallbacks 21-56).
  Fix `rt_hair_primary_frame_tag` (live, off): the header tag is the G-buffer frame itself (common/hair_primary.h
  gbuffer_tag); the submit upgrade keeps it. Seals, markers, tips dither phase and coverage evidence are then per frame.
- Cause 2, LOD switches drop the groom: a groom whose screen-size LOD changes to one not yet uploaded is released
  (groom_lod stages the upload and returns nothing for 2-3 frames; bvhsplit releases the slot): its hair is cards for
  those frames and the key set of the next publishes differs (the different-grooms fallback of cause 1). A new slot
  also starts at LOD 0 and switches 30 frames later: the log uploads LOD 0 then LOD 1/2 about 1 s apart for every new
  groom (32 uploads, full builds 9-21 per moving period, 0 when still). Fix `rt_hair_groom_lod_keep` (live, off): keep the
  built LOD until the wanted one is resident (hair_groom::lod_this_frame); the [hair groom] line counts kept vs dropped.
- Review findings (2026-11-08), verified against the code:
  1 confirmed: a submit-time upgrade refused for a groom-set change ("different grooms") left the record-time header
    after this frame's build had refit the BLASes and rewritten the points in place (new pose, new camera origin): the
    G-buffer traced the old TLAS over them, rebased with the old camera. Fixed (always): the upgrade is taken whenever
    the recorded keys keep their draw slots (hair_primary::upgrade_safe; added grooms allowed, the header copy is full
    size). Left: a key dropped from this frame's publish (rare with lod_keep), counted under "different grooms".
  2 confirmed (5h): the camera check is now unconditional (rt_hair_primary_camera_gate retired): a header without
    its build camera is never applied.
  3 confirmed: the tag and the previous-camera link now use the build's own frame (rt_current_build_camera), not a
    re-read frame_index.
  4 confirmed: counter frame (above); its per-key state and the count slots are reset at a level load.
  5 confirmed: rt_hair_groom_lod_keep (above).
  6 confirmed: grooms without a primary key (no UVs yet, first points update) get instance mask 0x01; the primary
    strand rays use 0xFE, so they no longer hide a swapped draw's strands. Shadow/GI rays (0xFF) unchanged.
  7 rejected: the compacted copy leaves the previous TLAS on the old, unmodified BLAS (destroyed after kGroomFreeLag):
    consistent. 8 confirmed, low (one frame of motion vectors after a skipped update), not changed. 9 confirmed, low
    (rt_groom_links reads one word past a groom's start table for its last segment, inside the pool), not changed.
    10 rejected for the main view (one header per frame, applied in the first G-buffer command buffer). 11 reopened
    2026-11-09 (branch claude/hair-header-cam), below.
- Item 11, header camera vs groom points (2026-11-09, claude/hair-header-cam, READY_FOR_LIVE). Evidence (rv3-s8 220145,
  turns): sampled strand origin 0 -> 94-98.5% accepted/card (8 of 8 counted frames); origin 2.6-15.5 cm -> 18-80% (7 of
  7), although "this frame's publish" was applied in 291-299 of 300 frames (origin should be 0). The header camera is
  a CPU read of the game's mapped camera UBO at record time (rtscene g_this_build_cam), the points the GPU's read of
  the same UBO at execute time (xfb_to_world, Cam binding 0): a game write in between makes them disagree. The frame
  watchdog (item 19) then held every hair draw on cards for 28 frames (0-23 trips and up to 2045 watchdog refusals
  per 300 frames).
  - Proof (always on): the header safety line counts publishes whose UBO, re-read at the vkQueueSubmit carrying the
    build, differs from the record-time read, with the max distance; the sampled-ray line prints the header camera
    (camera - origin) and the evidence strand share (accepted/card pixels of the judged frame).
  - `rt_hair_header_cam_gpu` (live, off): the build's command buffer copies the UBO (geo_copy_camera) into a 256-byte
    device slot, moves the slot's previous position to kPrevCameraOffset (only when the previous publish, one frame
    back, ran the copy; else kFlagPrevCamera is off), then copies the new position to kCameraOffset; apply_header skips
    the camera region. The builds rewrite the points in place, so the header camera is the last executed build's, as
    the points are. No shader change. Cost: one 16-thread dispatch, two 12-byte copies, three barriers per publish.
  - `rt_hair_watchdog_draw` (live, off): the watchdog judges each swapped draw against its own bound (the frame
    watchdog's per-draw term; markerless draws by the same rule) and holds only that key, for kWatchdogFrames, after
    kWatchdogDrawBad = 2 bad judged frames in a row (one header per G-buffer frame: one bad publish makes one bad
    frame). CTest hair_groom_asset_test test_watchdog.
  - The "with a strand" trace counter was right but misnamed: it counts shared pixel entries, traced only for the slots
    that swapped; when every draw is refused, the probes trace privately and store nothing (0 while accepted is 95%+).
  - Live job (staged, not queued): tools/live_batches_staged/hair_cam (S8 turns, off / cam / draw / both / off).
    The user checks it at S8 or Test Dialogue1 by turning the camera: with the GPU camera the hair stays strands.
  - Live hc1 2026-11-09 (S8 turns): item 11 as written is DISPROVED. 120 publishes checked per period, 0 differ at
    submit; `rt_hair_header_cam_gpu=1` (origin always 0) did not remove the trips; `rt_hair_watchdog_draw=1` was
    worse. Static camera clean (0 trips, 0 dropouts).
- Shared draw key (hc1 2026-11-09, branch claude/hair-shared-key). Of about 190 dropout entries, 86 were key 7a27, 41
  5285 and 13 93ea: exactly the keys with two groom rows (two characters with one hairstyle, sometimes at different
  LODs), typically `7a27:39907/0` for 1-4 frames about every 30 frames, each one a frame watchdog trip (every hair on
  cards for 28 frames). Cause: the scene keys a groom per occurrence (layer/bvhsplit.cpp `fd.dyn_key = mix(dk,
  g_dyn_seen[dk]++)`, one DynSlot and GroomSlot each), the G-buffer swap per index range (`fd.hp_key =
  hair_primary_draw_key`, hair_primary_swap, `g_live_keys`, one draw slot per key). A key in the publish swaps every
  draw with it, also the one whose own groom is out of this publish: `!groom_src` / `groom_keep` (held or refused
  soup), `!ok` (LOD upload, pool room), `crowd_cards`, or its row without a primary key (`groom_primary_key`: a new
  LOD's first update, which the other occurrence at another LOD does not share). That draw's fragments find no strand
  on their card (hair_primary_gbuffer.hlsl miss or `hp_on_card` reject): 0 pixels.
  - `rt_hair_shared_key` (live, off): before the groom instances, a key keeps its rows only when its keyed rows are
    at least the capture's groom-matched hair draws with that key (hair_primary::shared_key_complete); otherwise all
    its rows lose the key (mask 0x01, no draw slot) and all its draws keep their cards this frame. CPU only, no shader,
    no ABI change. Period line: "[hair groom] ... shared-key rows withheld N".
  - `rt_hair_watchdog_one_key` (live, off): a frame whose only draw under its own bound is one key, next to a swapped
    draw above its bound, does not trip the frame watchdog; that key alone keeps its cards for kWatchdogFrames. Header
    safety line: "frame watchdog spared to one key N".
  - Why `rt_hair_watchdog_draw=1` was worse (code reading, likely): both occurrences of a shared key are one draw slot,
    so their card pixels are summed and the bad occurrence trips the key for the good one too; and a key held on cards
    by it keeps its groom rows in the publish (mask 0xFF), so other draws' strand rays can stop on its strands. With `rt_hair_shared_key` a key is judged only
    when all its draws carry their grooms, so the per-draw watchdog then judges a complete key.
  - Fixtures: hair_groom_asset_test test_watchdog (two draws share a key, one row: key withheld; complete keys and
    single keys unchanged; one bad key spared, two bad keys or no passing draw trip; key off trips as before).
  - The user checks it at S8 (two characters with the same hairstyle in view): turn the camera with
    `exp_hair_shared_key` on; the hair stays strands (or both on cards for a frame) instead of flashing all hair to
    cards; the dropouts line no longer lists 7a27/5285/93ea and "watchdog trips" falls near 0.
  - Live hk1 (2026-11-10, log hk1-s8-shared-key.1.004759): INVALID and the prediction is disproved. The gate of 0
    watchdog trips failed in the first ON window (turn-shared-r: 8 trips; windows without the key 3 to 10 per 120
    frames, a trip every 30 to 31 frames = the 28-frame hold plus the feedback latency), so the later windows did not
    run and `rt_hair_watchdog_one_key` is unmeasured in turns. "shared-key rows withheld" stayed 0: the key only acts
    when a shared key has fewer rows than draws, and every groom was in every publish (matched = instances x publishes).
    The dropping key had both its rows present. See 5q.
- Fixtures: CTest hair_groom_asset_test test_upgrade_safe, test_frame_tags (two G-buffer frames on one publish: ambiguous and reused with
  publish tags, distinct with frame tags; the counter judges the previous frame), test_lod_keep.
- Live check (Test Dialogue1 and the isometric view, one session, camera and characters moving, both keys on/off/on):
  on windows: ambiguous tags 0, "different grooms" near 0, coverage invalidated near 0, strand swaps resumed after card frames near the still
  windows' 0-9, "LOD switches ... (groom dropped)" 0; the dropouts line near 0 (not ~1000); no visible flash.

## 5j. Layer-path review fixes (offline 2026-11-08, branch claude/hair-layer-fix)
Independent review of the layer path; each finding verified in code before the fix.
- 1 confirmed: every build refits the groom BLASes and rewrites the points in place, so once a newer build is ordered
  before the G-buffer the record-time header traces a TLAS over refit BLASes with an old camera (upgrade_safe refusals:
  a key gone, crowd cards, soup refusal, key 0; and always with current 0). Fix (no 2x memory): at submit the newest
  publish ordered before the G-buffer is always applied (`hair_primary::ordered_publish`), its rows remapped to the
  recorded draw slots (`upgrade_plan`); a recorded slot whose key it lacks is set in the header card mask (slot mask
  word at kSlotMaskOffset 3504, bits 16-31) and its strand variant draws the card (card mode: native material at the
  card UV, prepass alpha test, card normal and tangent, card depth). current 0 is a diagnostic only: such frames are
  counted ("header/BLAS mismatch frames"). current on without header slots: no swap that frame.
- 2 confirmed: keyed grooms whose draws keep their cards (no slot, refusals, extras) still hid swapped hair. Fix: the
  header carries the slots that swap this frame (bits 0-15, decided at submit when every draw is recorded); the pixel
  trace skips any other row's strand (up to 3, TMin past the 2 cm diameter bound); a draw whose own slot is not in the
  mask (the coverage probe) traces privately for its own slot (not stored). Counter: mask-hole pixels (card marker flag
  0x20, a lower bound). Record-time headers keep all slots (0xFFFF): unchanged behaviour except no-slot rows are skipped.
- 3/4 confirmed: the count read colour and traced/hit for its own tag only, while the current header makes it judge
  tag - 1: C2 a_f was never measured (shadow_t / ao_t rows 0) and every dropout was "unattributed". Fix: those counts
  for all four tag windows; the layer takes the judged frame's window and maps groom rows with that frame's keys.
- 5 confirmed: lod_keep restaged an evicted built LOD and released a new slot every frame of its first upload. Fixed.
- 6/18: "Strand hair on screen" On writes the qualified bundle (order, current, frame_tag, lod_keep, hold,
  tangent_world = 1); those keys lose their own knobs (developer diagnostics in bg3rtx.ini). Passes, shadow_t and ao_t
  now need the strand knob. settings_test test_strand_bundle checks every strand key's dependency. Presets unchanged.
- 12 confirmed: cascade_drop now needs the core's RT sun shadow applied this or the last frame (persist
  `bg3rtx.rt_sun_applied`, core/gi.cpp; no ABI change) and strands on; kept draws are counted.
- 13 confirmed: strands turned off live after a prepass drop now still restore the card depth (the draw hook stays on
  while a latch is pending). 14: LoadGuard also compares the image's tracker creation id (a reused handle is a load);
  verdict caches and counts already reset there. 15: groom LOD arrays leave the pool when rt_hair_groom is off.
  17: zero-length segments rejected by the parser (0 in a 60-file sample) and discarded in the shader.
- 19: frame watchdog (`hair_primary::Watchdog`): bound min(kCoverMin, kCoverRatioMin - largest cover_sigma of the
  frame's draws), so it never trips while every draw passes its own gate; it catches marker-less frames the hold
  ignores; cards for kHoldFrames. A no-fallback orphan blocks that key's prepass drop 300 frames.
- 20: period line `[hair primary] header safety`: mismatch frames, card-draw upgrades, mask holes, watchdog trips, C2
  colour samples; the frame line counts cascade draws kept for no RT sun shadow.
- Cost: no game launched. Card mode and the skip loop run only where needed; the probe's private trace is one ray per
  probe fragment (not per pixel) while a draw shows cards: unmeasured, read the strand G-buffer GPU ms line.
- Live check (Test Dialogue1 + isometric, one session, strands on, characters entering and leaving): `header safety`
  mismatch 0, card-draw upgrades only when a groom leaves; `shadow transmittance` draws measured > 0 with shadow_t on;
  dropouts attributed (unattributed 0); no hole where two characters' hair overlaps; watchdog trips 0 in normal play.

## 5k. Layer review round 3 (offline 2026-11-08, branch claude/hair-fix3-layer)
- H2 confirmed (likely the dialogue/camera flash): a LOD switch reset prev_frames, so the switch frame published key 0
  and mask 0x01, DrawSlots freed the slot, the submit drew the card and the next frames refused: strand-card-strand on
  every LOD change. Now a slot that published its key keeps it (and mask 0xFF) on the switch frame
  (`hair_primary::groom_primary_key`); only row.w stays 0 until the second update.
- M1 confirmed: previous points are now published only when the slot's last update was the previous build frame
  (`groom_prev_points`); otherwise row.w 0 (camera-only motion for that frame, no wrong MVs after skipped updates).
- M2 confirmed: card-mode G-buffer pixels write the accepted marker with flag 0x40 (kMarkerCard, no card word: no
  coverage evidence); the swapped velocity variant then writes the card's native motion vector (b70cadc5 formula from
  location 1, the previous clip position) instead of discarding.
- M4 confirmed: non-hair draws now leave the hook unless their key is published (this or the last publish) or had its
  prepass depth dropped; no more `uncovered` entry per game draw. The hook still takes g_mu once per indexed draw.
- M5 confirmed: the clones are created outside g_mu and inserted under it; the generic G-buffer variant (never bound) is
  not created (32 + fallback per static hair G-buffer pipeline instead of 33 + fallback).
- M6 confirmed: a LOD upload reads the header and that LOD's bytes only (offsets from indexing; size and header
  re-checked) and validates that LOD (`parse_lod`); the whole-file parse is gone from bvh_build.
- L1: a pixel-record reallocation zeroes the header tag in the same command buffer. L3: per-key maps prune keys not
  published for 600 frames. L4: `submit_decision` composes the submit-time choice (unit-tested). L5: passes and
  cascade_drop are read once per publish.
- L2 not changed: a single non-opaque traversal would commit LSS candidates (unproven here: hf_groom only walks them)
  and costs every strand ray; the 3-restart skip only runs where a non-swapping groom's strand is in front.

## 5l. RTX Hair single option (2026-11-08, branch claude/opt-waits)
- The user tested and approved strands with a hand-written set (root `bg3rtx_user.ini`). "Strand hair on screen" On
  now writes that whole set: the 5j bundle plus rt_hair_groom, shadow_t, ao_t, cascade_drop, fibre, fibre_gi,
  fibre_spec, fibre_mapped, rr_hair_guides and rt_hair_physical_origin. Tips, coverage, macro_normal and
  coverage_footprint stay off. Off in every preset; the other hair knobs still show (and can override) their keys.
- Restart keys in the bundle: rt_hair_groom_primary, rt_hair_physical_origin (0 -> 1), rt_hair_tangent_world
  (compiled at pipeline creation). The knob's note says so. Choice `kv` grew from 12 to 18 entries.

## 5m. RTX Hair live switch and hotkey (offline 2026-11-09, branch claude/rtx-hair-live)
- RTX Hair (exp_hair_groom_primary) applies while playing. The restart keys of 5l became:
  - rt_hair_groom_primary (startup latch): gone. The strand header (3.5 KB) and the strand pipeline variants (per static
    hair G-buffer pipeline 32 + fallback, 5k M5; one per prepass/velocity pipeline; the cascade clones) are made on
    every device with linear swept spheres, shaderInt64 and fragment stores, whether or not the key is on.
    occlusionQueryPrecise is requested whenever supported (core feature, nothing runs while unused). Cost from the
    logs: hcap3-strands 2026-11-08 created the first batch (6+ G-buffer pipelines) 96 ms after the header, inside the
    game's 8.7-9.2 s pipeline burst (31118 pipelines; runs with and without the header within that spread); first
    launch with an empty shader cache +134 ms + 35 ms DXC (hair_primary_gbuffer/velocity). VRAM: the header; the 62.5 MB
    pixel record (2560 x 1067) is allocated at the first strand publish only and kept while off. New line
    `[hair primary] strand variants: N pipelines in X ms` measures the total in every session.
  - rt_hair_tangent_world (compiled at creation): specialization 1045 is always 1; header flags kFlagMacroNormal /
    kFlagTangentWorld switch it live (a header load and two uniform branches per strand fragment; with both flags off
    the output is that of constant 0). No SPIR-V change (a specialization value only).
  - rt_hair_capture (BC3 copyable at creation): a hair mask cannot be told from another BC3 texture at vkCreateImage,
    so every sampled BC3 image of an RT session gets TRANSFER_SRC. Measured on the RTX 5090 with
    vkGetDeviceImageMemoryRequirements: size, alignment and memory types identical with and without it (BC3 UNORM/SRGB,
    64 x 64 to 4096 x 4096, full mip chains). The capture itself was always live (hair_capture begin_capture).
  - sharc_device: unchanged; it already defaults to 1 (a requirement of the knob, not written by it).
  - rt_hair_physical_origin stays a startup reservation (43.7 MB R32G32B32A32 at 2560 x 1067, `[hair origin]
    auxiliary image`, and the GI's origin shader variants): 0 -> 1 needs a restart. A game started with RTX Hair off
    gets the strands live and the launcher's restart banner for that one key.
- Live key `rt_hair` (default 1): 0 makes every key of the RTX Hair On choice read 0 (layer/util.cpp hair_gated):
  strands on screen and in RT (rt_hair_groom, rt_hair_groom_primary), strand lighting passes, RR guides, cascade drop,
  physical origin and capture: cards only. `hotkey_rtx_hair` (Ctrl+Shift+F7, launcher Lighting page) flips it in memory
  and logs `[hair] hotkey ...: RTX Hair ON|OFF`; a later change of rt_hair or of the RTX Hair keys in the files ends the
  override. The settings change at a present (cfg_poll).
- State machine `common/hair_live.h` (tests/hair_live_test.cpp), ticked once per frame_index in layer/hair_primary.cpp:
  - Off: no swap from the first draw recorded after the switch; a G-buffer draw whose prepass already dropped its card
    depth (DepthLatch, prepass frame + 2) restores it with the fallback pipeline, so the cards are whole in the same
    frame or the next. The next scene build leaves the grooms out of BLAS/TLAS; a recorded frame keeps its TLAS.
  - On: staged publishes from before the switch are dropped and never applied (their TLAS may be refit or freed), the
    swap gate restarts empty; swaps resume after two fresh publishes, as after a level load (no 300-frame pause).
  - Lines: `[hair primary] live: strands ON|OFF at frame N (switch k)`; while off, the period line `[hair primary]
    frame N: RTX Hair off (live): 0 grooms published; ... 0 G-buffer ...`. A session that never turned strands on
    skips the submit-time header work entirely.
- rhl1 2026-11-09 01:07 (INVALID by a job readiness race, toggle correct): switch 1 ON -> strands after 12 s, OFF ->
  cards that frame and 0 strand draws per period, switch 2 ON -> full strands in 2 s, no device loss. The 12 s were
  3.6 s of groom-file indexing on the render thread (871 files at the first rt_hair_groom use) and the LoadGuard's
  300-frame pause, because the first G-buffer image seen while on counted as a level load. Fixed: the index is read
  at device creation on a below-normal thread, and LoadGuard observes the G-buffer image while strands are off.
  The job waits on pipeline quiescence only; the edges are expectations (a later settings read moved camera-plan's
  fresh-observation start past the edge line).
- Live job (READY_FOR_LIVE, not queued): tools/live_batches/rtx_hair_live (rhl1-dialogue, Test Dialogue1): starts cards
  only, first activation in warmup, then rt_hair 1/0/1/0 and a cards-only window; gates in predicted.json.

## 5n. Strand edge dithering (offline 2026-11-09, branch claude/hair-edge-quality: key rr_hair_strand_aa, off)
User (RTX Hair on): strands look good, their edges still dither. No capture of that session survives (the format);
the 09:51 bg3rtx.log has the full bundle, rr_post=1 and no stencil or flicker data. Diagnosis from the code and models
(`tools/hair_groom/edge_stability.py`, CTest `hair_groom_test` test_strand_edge):
- Binary sub-pixel coverage (cause, structural). One ray per strand pixel at the raster point: a strand of 0.1-0.5 px is
  a Bernoulli sample per pixel, all pixels on the screen-wide jitter (a dotted line that moves with the jitter). Unbiased,
  and Halton-stratified per pixel; a clamped history (the rejection every temporal reconstructor applies) loses 8-13% of
  the edge-band light (model taa_energy) and flickers.
- The game's dither resolve on strands (likely the visible "dither"). RR's input is X after the port of 7401d785
  (rr_post.hlsl `resolve`): per pixel, geometric means of the stencil bit-5 and other pixels of a 4 x 4 window, mixed by
  their share. It exists to undo the cards' ordered alpha dither. The strand clones keep the native stencil state (the
  prepass clone only drops depth writes, layer/hair_primary.cpp), so the tags follow the card dither (hp_tips_keep:
  screen-space, 4 frame phases), not the strands. Model (card strip, opaque core, fading edges): the resolve on card tags
  shifts the edge band by -4.2% and keeps the dither period; raw strands are unbiased (+0.03%). Unverified: which hair
  pass writes bit 5. The layer now logs each hair pipeline's stencil state ("hair pipeline stencil:").
- Ruled out by the code: screen-door alpha (tips off; coverage pair off; the shadow transmittance hash is per-ray white
  noise, RR-compliant), card/strand mixing (whole draws, held by the coverage hold), depth/MV at edges (the hit's own
  depth and the strand point's motion; 5e, 5g). Decorrelated per-pixel ray positions (white noise in the pixel box)
  were modelled and rejected: ideal-accumulation flicker 0.034 -> 0.056, card-edge strands lost (the ray leaves the card).
- Literature: NVIDIA LSS (Blackwell, RTX Character Rendering SDK) traces strands with per-pixel samples and relies on
  DLSS-RR; the DLSS-RR Integration Guide 3.5 forbids screen-space dithering, checkerboards and screen-shared patterns
  (jitter excepted) and 3.4.10 offers a transparency overlay (premultiplied, upscaled only). Frostbite (Tafuri 2019)
  and Unreal's groom (hair visibility buffer, sample coverage, hair composition) render strands apart and composite
  them with analytic coverage, with a minimum width and opacity fade (Persson's phone-wire AA, 2012). Stochastic
  transparency (Enderton 2010) is unbiased but keeps the c (1 - c) variance; GBAA (Persson 2011) blends neighbour
  colours by analytic coverage from geometry in the buffer. The Phantom intersector (Reshetov 2018) is a curve
  intersector, not a coverage method. Our G-buffer swap cannot blend, so the coverage enters before RR, GBAA-style.
- Implemented (`rr_hair_strand_aa`, knob exp_hair_strand_aa in Advanced, live 0/1/2, needs rr_post=1):
  `shaders/rr_strand_aa.hlsl` replaces the resolve dispatch (layer/rrpost.cpp strand_resolve; the game's resolve when
  the header or this frame's G-buffer camera is missing). 1: the resolve with every pixel a strand G-buffer fragment
  sealed this frame read as untagged. 2: also, on that footprint, c Lh + (1 - c) Lb, c = sum over the strands the 3 x 3
  rays hit of the strand's box coverage of the unjittered pixel (projected axis, radius from the hit's own depth) times
  the in-pixel length share, over P = the probability that the shared jitter puts one of those 9 rays on it (box
  coverage of the jitter square by 9 shifted bands); Lh the mean of the strand pixels, Lb the pixel's own colour or the
  5 x 5 mean of non-strand pixels. Depth, MV and RR guides unchanged. New request only: every other request has
  identical inputs (key off byte-identical). Camera: ngx_gbuffer_camera (the RR guide camera of this frame).
- Offline (card strip, 0.3 px strands, 54 frames of 18-phase Halton, area reference 8 x 8): accumulated edge flicker
  raw 0.0178, card-tag resolve 0.0117, analytic 0.0086; RMS error 0.0297 / 0.0354 / 0.0238; edge energy +0.03% /
  -4.2% / +1.3% (whole image +0.02% / -0.8% / +0.9%: found strands' summed overlap and occluded strands' P). At 0.8 px:
  flicker 0.0171 -> 0.0048. A 5 x 5 search over-counts (rejected). Cost: unmeasured (estimate +0.05-0.3 ms).
- Live: tools/live_batches/hair_edge (he1-dialogue1-edge, lint 0 failing, not queued): key 0/2/0/2 PNG traces, a key-1
  window, K8 pan bursts off/aa for temporal_burst.py. Predicted in its predicted.json (mc final -10 to -30% on hair,
  energy within 1%, the stencil lines name the tagging pass).
- he1 #1 (live 2026-11-09 01:39-01:42, mode switches 2/1/0/2/0 worked, strand resolve 600 frames per period): every
  G-buffer hair pipeline has stencil test 0 (strands write no stencil), and Xr ('Xr (RR colour: dither-resolved X)',
  i1120) equals the compose output on every hair pixel in every window (0 class-5 pixels > 2 codes in the 960 px
  previews; the ~50 differing pixels are off the hair). So the game's dither resolve never touches the hair: the
  card-tag mechanism is refuted and mode 1 is a no-op here. Mode 2 changed no hair pixel either: its coverage path did
  not apply on the GPU. Energy within noise (off 0.38364 / 0.38467, aa 0.38481 / 0.38263, untag 0.38204). The burst
  windows closed with their 60-frame period before the K8 burst completed (fail-fast): now static 6 s timed windows.
- Mode 3 (debug, ini only): Xr shows red on the strand footprint, green on strand pixels, blue the applied coverage,
  magenta with no usable header; he1 window f-debug traces it. Remaining cause candidate: binary sub-pixel coverage
  under RR (temporal). If f-debug shows the coverage path cannot see the strands at post time, the next step is to
  write each strand pixel's analytic coverage in the strand G-buffer pass itself (a per-pixel coverage word in the
  pixel record, from the hit's own capsule and the pixel footprint) and resolve from that.
- he1 #2 (02:04, VALID): f-debug grey everywhere, no magenta: the header is usable but no pixel reads as strand
  footprint at the post chain (the record no longer holds this frame's seal there; the fibre passes read it at
  lighting). b-aa / d-aa changed 650 / 610 hair pixels of Xr against the compose output (0 in a, c, e): the coverage
  applied only in some frames. Edge-only K8 bursts (final image, 8 frames): flicker (temporal std / mean) off: hair
  0.0409, edge band 0.0674, core 0.0324; aa 0.0465 / 0.0740 / 0.0344 (one animated sample each, mode 2 unreliable: no
  claim). Strand edges flicker twice as much as the hair core.
- Fix: strand_snapshot (rr_strand_aa.hlsl) runs at the lighting dispatch (layer/rrpost.cpp rrpost_dispatch_pre) and
  writes (coverage, strand pixel, footprint) into a frame-parity RGBA16F image; strand_resolve reads only that image.
  Line `[rrpost] strand snapshots (rr_hair_strand_aa=N): S at the lighting pass, R refused`. Bursts now full
  (pan_inputs=1) so temporal_burst.py has the RR inputs.
- he1 #3 (02:30, ad849740) froze the game in warm-all at frame 1095: GPU presents done to 1094, one present fence
  pending, no device fault (vkGetDeviceFaultInfoEXT -13), no TDR event in Windows over 941 s: the GPU waited, it was
  not hung. Cause (code): a lock-order inversion. ngx.cpp Detour_Eval holds ngx's g_mu and calls rrpost_take, which
  locks rrpost's g_mu; the snapshot (at the lighting dispatch, recorded on another game thread) held rrpost's g_mu and
  called ngx_gbuffer_camera, which locks ngx's g_mu. Two recording threads deadlocked, the evaluate's command buffer was
  never submitted, the present waited on it. The resolve path had the same call, but it records on the evaluate's
  thread (no hang in he1 #1/#2). Fixed: the camera is read before rrpost's lock; the resolve no longer reads it (the
  snapshot holds everything); CTest test_strand_edge checks that no ngx call sits under rrpost's lock. Shader review:
  every loop has a constant bound, the groom count is clamped to kMaxGrooms (key map and table reads inside the header),
  record and pool reads stay behind the existing bound checks. No synchronization-validation fixture was run.
- he1 03:04-03:17 (4 runs, no freeze, snapshots 188-590 per 600 frames): mode 2 changed Xr on the hair ring in 3 of 8
  aa windows (911 / 944 / 947 ring pixels of ~3600, ~600 hair pixels; 0-12 in every off and untag window); in the
  other aa trace frames no snapshot of that frame existed (the lighting hook does not fire every frame) and the
  game's resolve ran. Where it applied, the ring Laplacian HF of Xr fell 120 -> 105-106 (-12%). Xr mean aa/off
  -0.11% to -0.20% against an off-pair spread of 0.31-0.56%: energy within noise. Crop pair (a-off / b-aa, 03:11, x6):
  .scratch/hair_edge/he1_031114_xr_off_vs_aa_crop.png. The RR input inside the hair is heavily speckled in both.
- Bursts ended early because they sat in periodic gates' windows: live_flow.tighten made them 60-frame stats windows
  and auto-run ends a frame window on its count (03:17 burst-off closed 0.15 s after ready). Fixed: bursts only in plain
  expects (timed windows), and tighten keeps a burst window's declared seconds (14 s) above its 5.1 s model; a pan_inputs
  burst completed 7.5 s after arming. The fake-queue dry run (lean_dryrun) cannot time bursts (its fake game runs no
  plan); make_job asserts the burst phases are timed and >= 14 s.
- Per frame: the lighting hook (kCsLighting, the standard-class tiled-lighting hash) fired in 188-590 of 600 frames, so
  the resolve alternated with the game's. The snapshot now runs at the end of the scene G-buffer pass (rrpost_pass_end,
  after ngx_note_pass recorded the camera; size equal to the scene G-buffer, never a portrait pass), once per rendered
  frame; a frame without its snapshot runs the game's resolve and is counted ('fell back'). common/strand_snapshot.h,
  CTest hair_groom_asset_test test_strand_snapshot_ring (600 of 600 frames usable; a skipped frame never reads the
  snapshot of two frames ago).
- he1 03:38 (VALID; snapshots ~2 per frame, fallbacks 0-1 per period): NOT a pass. Xr changed on the hair ring in
  neither aa trace (7 and 10 ring pixels, as off): the G-buffer-end snapshot holds no usable strand data, though it runs
  every frame (the lighting-time snapshot applied in 3 of 8 aa traces). Bursts (16 frames, tools/hair_groom/edge_burst.py):
  final edge band flicker +5%, mc +17%, hf -9%; core flicker +19%; whole frame flicker +130%: the two bursts, 17 s apart,
  differ over the whole frame (animated dialogue), so this single pair cannot judge. RR input (rr_color) edge band mc -9%,
  hf -9%. Energy Xr aa/off +0.31% (off spread 0.20%). Render-res chain about 0.28 ms in aa windows vs 0.165 ms off
  (+0.11 ms, the resolve; the snapshot runs outside that line). Next: a per-period GPU count of the snapshot's footprint
  and strand pixels (device-local, per-workgroup reduced) to see what the G-buffer-end record holds, and bursts
  counterbalanced off/aa/off/aa.
- Snapshot sites and counters (offline): dev key rr_hair_strand_snap_site 0 G-buffer end, 1 decal end, 2 lighting, 3 every
  site merged (a later site writes only the pixels where it finds a strand footprint). The snapshot counts footprint,
  strand and coverage pixels and dispatches per site in a device-local buffer (per-workgroup reduction, copied to the host
  after each period: host_atomics_lint_test classifies it); line '[rrpost] strand snapshot site <site> (frames a-b): N
  dispatches; per dispatch footprint F px, strand S px, coverage C px'. he1 now runs with site 3, a counter window and
  counterbalanced bursts off/aa/off/aa (about 2.2 GB of bursts).
- he1 04:02 (bursts complete; INVALID only on the 'N frames, 0 fell back' expect, 1-3 per steady period): NOT a pass.
  Counters: every site 0 strand px before frame ~1363 (no strand swaps yet), then G-buffer end ~77k strand px per
  dispatch, decal end and lighting about half (the seal is already partly gone at the decal end); every measured window
  is after 1363. Bursts (edge_burst.py, final image): edge band flicker aa-1 0.136 vs mean(off-1, off-2) 0.106 (+29%),
  aa-2 0.136 vs off-2 0.074 (+83%); the core moves the same way (0.045 / 0.048 vs 0.045 / 0.028), and the band/core
  flicker ratio is unchanged (off 3.03 / 2.66, aa 3.02 / 2.82): no edge-specific temporal gain; the off bursts alone
  differ by 30-80% (animated dialogue). Edge band hf -13% / -15% (smoother single frames). Static: Xr changed on the hair
  ring in b-aa (864 px) but not in d-aa (11). Energy Xr -0.55% (off spread 0.49%). Render-res chain about 0.35 ms in aa
  windows vs 0.165 off (+0.18 ms; the snapshots now run inside run()'s timed dispatches).
- Fallbacks in play (user session 2026-11-09 07:59, 0.9.5, Ultra, fg=3, fps_cap=58; fixed, branch
  claude/strand-resolve-fallback): 145-395 of 600 resolves fell back while 599 of 600 snapshots ran. Cause: snapshot and
  resolve were keyed on frame_index(), which counts presents (layer.cpp g_frame); the present runs on another thread, and
  with the cap, Reflex and frame generation it often landed between a frame's G-buffer end and its post chain, so the
  resolve looked for the next frame's snapshot (the 1-3 per period of he1 04:02 were the same race). The ring now keys
  on recorded frames (common/strand_snapshot.h Ring begin/wrote/take): a frame begins at its first snapshot after the last
  resolve (or at a repeated G-buffer end / an earlier site) and ends at its resolve, which reads only the snapshot
  recorded since the resolve before it; a frame without one falls back once, never reading an older snapshot. CTest
  hair_groom_asset_test test_strand_snapshot_ring. Live check: in normal play the period line reads 'N frames, 0 fell
  back' with N equal to the snapshot count.
- Still 113-140 of 600 fell back in rv1-s8 (Water Queens House, fg=3, Reflex; 2026-11-09 12:11) with 600 snapshots and
  600 resolves per period (Dialogue1, fg=2: 0). Cause (code read): the G-buffer end and the post chain are recorded on
  different game threads into different command buffers, so snap N + 1 can be recorded before resolve N; the Ring then
  gave resolve N the image of N + 1 (on the GPU still frame N - 1: a wrong frame, silently) and resolve N + 1 fell back.
  No per-frame identity is visible at both sites (frame_index, the ngx camera and the hair tag are all present- or
  latest-based). Fixed by submission order (branch claude/strand-pair): one snapshot image, which at the resolve's GPU
  time holds the snapshot submitted last (a frame's G-buffer goes down before its post chain, frames in order). The k-th
  snapshot and the k-th resolve share an ordinal; rrpost_submit_begin (before vkQueueSubmit, both submit paths) writes
  a resolve's host-visible flag word with its token only when the snapshot submitted last carries its ordinal and was
  written; rr_strand_aa.hlsl strand_resolve otherwise reads no snapshot (the game's resolve). A mismatch resyncs the
  ordinal offset. CTest test_strand_snapshot_pairing (overlapped, inverted and mixed recording orders: 600 of 600 own
  frame; missing or refused snapshot, missing resolve, out-of-order batches, resubmission: refused, never a wrong frame).
  Period line: '... N frames, M fell back ... (no snapshot of the frame: a at recording, b at submit); pairing: r
  resyncs, snapshots recorded up to k ahead of their resolve'. Snapshot memory 2 -> 1 image (-22 MB at 2560x1067).

## 5o. Warm-up after a level load (offline 2026-11-09, branch claude/hair-warmup: key rt_hair_warmup, off)
User report: after loading a save the strands are absent, flickering or incomplete for a while, then stabilise.
Every delay between a character's first frame and its strands, from the code and three logs (he1 12:56 Dialogue1,
sh2 18:43 Water Queens House, rv3 18:37):
- **LoadGuard quiet window (5b): 300 frames, the whole delay.** Every new G-buffer depth image (level load, also the 3D
  main menu, hence two lines in he1) pauses all strand swaps and probes for kLoadQuietFrames. he1: load at frame
  ~1100, all 4 grooms joined and uploaded within ~15 frames, first strand G-buffer draws in the 1381-1440 window
  (~+300..+330 frames, ~10 s at the unfocused 30 fps); sh2 the same (~+300, grooms ready after ~+100 of slow
  streaming frames). 300 was a safety margin chosen after hv1/hc1 (device lost after F8 and 10 frames after a first
  publish mid-load, causes then unconfirmed). Section 9 closes them with the boundary reset itself (kept),
  soup_update_ok (held soup offsets) and the demoted-lane discard; no evidence needs the 300-frame length.
- UV join soup readback: 1-3 frames per draw whose index buffer is not host data (sh2: 31.311 pending -> 31.367 matched).
- LOD upload: 2-3 frames (staged, copied in the build, ready when that frame completed); the disk read is synchronous
  in the build (7-39 MB per LOD). Groom index: read at device creation on a background thread (31-47 ms, 871 files).
- First LOD: a new slot has no bounds yet, so groom_pick_lod starts at LOD 0, measures its screen size 2-3 frames
  later and then needs a 30-frame vote: the screen-size LOD's upload and full build come ~30-50 frames into the load
  (sh2: LOD 1/2 uploads 0.9 s after the LOD 0 ones; 14 full builds in the first window for 7 grooms).
- Key: a groom's draw key is published from the second update of its points range (+2 frames, also after a LOD switch).
- Swap gates after the window: two publishes, the coverage probe (kFreshFrames 12), the G-buffer proof, the prepass
  card-depth drop: ~12-15 frames.
Fix (rt_hair_warmup, live, default 1 since 2026-11-09 after hw1: strands 309 -> 66-73 frames; the experimental knob exp_hair_warmup was removed):
- The quiet window ends on a completion signal. bvhsplit reports per build whether the groom state is settled: grooms
  published and no hair draw with a join pending (UV readback in flight, or a full draw not yet joined), no LOD
  upload or LOD vote in progress, no first-LOD wait, no held soup, no full build, every groom keyed.
  `hair_primary::LoadGuard::settle`: kWarmupSettledFrames (30) such builds in a row (reports up to 2 frames apart)
  end the window; 300 stays the upper bound (a scene that never settles behaves as before). 30 = the LOD vote period
  (hair_groom::kLodVoteFrames), and the window cannot end within hc1's 10 frames after a first publish. Every reset at
  the boundary and every swap gate stay; final verdicts (count unknown, no file, UV mismatch, partial draw, crowd
  cards) count as settled.
- A slot's first screen-size measurement picks its LOD at once (hair_groom::lod_vote first_measure): the no-bounds
  LOD 0 was no choice at a threshold, so the 30-frame anti-flicker vote does not apply. Later changes vote as before.
- Rejected: a persistent join-verdict cache (keyed by UV hash it needs the same soup readback; keyed by draw identity it
  is invalid across loads, handles are reused: saves at most 3 frames, which the settled run absorbs anyway), groom
  prefetch from the save or the last session (index already off-thread at startup; a LOD read is 2-3 frames), upload of
  all LODs at once (rt_hair_groom_lod_keep already keeps the built LOD during an upload; 1.5-2x pool bytes per groom;
  the first-measurement rule removes the warm-up switch), full-build bursts (same count, the second one moves ~30-45
  frames earlier).
- Lines (always on, CPU only, both key values): `[hair primary] warm-up: level load at frame B: load quiet ended at
  frame Q (+n, groom state settled|frame count), first strand G-buffer swap at frame F, m frames after the load
  (rt_hair_warmup k)` once per load; per stats period `[hair primary] warm-up: last N frames: K draws swapped to strands
  for the first time since the level load; frames from their first publish: mean, max` and `[hair groom] warm-up: last
  N frames: K groom slots got their first primary key; frames from the first match: mean, max (rt_hair_warmup k)`;
  with the key, `[hair primary] warm-up: load quiet ended at frame Q, n frames after the level load (groom state
  settled for 30 frames, rt_hair_warmup)`.
- Expected: load to first strands ~+300..+330 frames -> ~+55..+170 (5-10 s -> 1-3 s at 30 fps). Cost: CPU counters
  and one call per build; no shader, request or ABI change.
- Tests: hair_groom_asset_test (settled run, gap, broken run, upper bound, reports before the boundary, identity
  boundary; lod_vote with and without the first measurement).
- Live (READY_FOR_LIVE, not queued): tools/live_batches/hair_warmup hw1-dialogue: Test Dialogue1 with RTX Hair on,
  first load key on, F8 reload key off, F8 reload key on (A/B between the reloads of one run); gates in predicted.json.
  In game: load Test Dialogue1 with the knob on and watch the party's hair; strands should replace the cards within
  1-2 s of the scene appearing.

## 5p. Static-camera strand flicker (offline 2026-11-09, branch claude/hair-static-flicker: no key)
User: with a still camera the strands appear and disappear very fast. Not swapping: rv3 220145 static windows show
rebase 0, strand draws 720/720, coverage 94-96%, no refusals. Code read (no defect found in the jitter chain):
- Ray, depth and motion agree with the raster sample. The ray goes through `pos.xy` with the inverse of the jittered
  `gCam.vp` (hair_primary_gbuffer.hlsl:238-245); the depth is the hit projected with the same matrix (:125-126, :374);
  the strand motion vector removes both jitters with the native card formula (hair_primary_velocity.hlsl:141-142 =
  :85-86) from the previous publish's points (:131-137). The groom soup is unprojected with the same frame's jittered
  `_m2` (xfb_to_world.hlsl), so the jitter cancels in the geometry.
- Most likely cause (a+b, structural): one jittered ray per pixel picks a binary strand/no-strand (or a different
  strand) each frame. The 4-6% card pixels with no strand and every pixel near a card edge flip with the jitter: the
  candidate region is the card raster at the jittered sample (:219, hp_on_card :348), so strands that leave the card
  are lost and the card edge moves +-0.5 px per frame. Unbiased, but the RR input carries the Bernoulli variance
  c (1 - c) per pixel (5d, 5n model).
- Amplifiers (c): with rr_hair_guides=1 the RR normal guide is the per-strand fibre normal (5g), so a pixel's guides
  change whenever the jitter picks another strand, which may cut RR's history; fg=3 interpolates two of three presented
  frames between uncorrelated strand pixels. FG is Apply::Restart (no in-run A/B). (d) The one-frame trail
  (hair_groom_update.hlsl:28) copies the points once per update: correct for one update per frame; low likelihood.
- he1 never measured strands against cards in one run, so it could not separate (a) from (c).
- Tool: tools/hair_groom/temporal_burst.py now prints the class-5 toggle rate, the flicker of stable vs toggling pixels,
  the flicker of each burst half (its noise) and the RR attenuation (final / rr_color). CTest hair_groom_test
  test_burst_coverage_split.
- Live (STAGED, not queued): tools/live_batches_staged/hair_static (hs1-s8-static-hair, S8, ~85 s of phases, 3 bursts
  ~1.7 GB): strands aa 0 / strands aa 2 / cards, static camera. Coverage if rr_color flicker strands/cards >= 1.5 with
  the same RR attenuation; reconstruction if rr_color is close to the cards and only final differs (predicted.json).
- Fix by outcome (owner of layer/hair_primary.cpp): coverage: a per-pixel analytic coverage word written by the strand
  G-buffer pass (5n next step) and/or a conservative-raster candidate region in the strand G-buffer clone;
  reconstruction: a stable RR normal guide on strand pixels (macro normal for the guide only, rr_guides.hlsl).

## 5q. One-frame holes of a two-row key and the frame-wide miss (offline 2026-11-10, branch claude/hair-frame-miss: diagnostic, no key)

Root cause NOT established. Verified facts, then the one hypothesis left and the measurement that decides it.

- Static camera, hs1 (log hs1-s8-static-hair.1.005103, bursts 99300/99301/99302). predicted.json verdict: pre-RR
  flicker strands / cards 0.4287 / 0.4198 = 1.02 (not coverage), final 0.0698 / 0.0465 = 1.50 (reconstruction side,
  borderline). RR guides on strand pixels are not less stable than on cards (normal delta median 0.022 against 0.039).
- The static scene is not clean: 12 dropouts in frames 2244 to 3322, all of key 6047, 0 to 2 per 60 frames, e.g.
  2904: 5382 card / 409 strand pixels. Watchdog trips 0 (one key does not trip the frame bound). In the bursts one
  head loses its hair for exactly one frame: class-5 pixels in x 1024..1279, y 320..639: 232 at frame 2582 against
  about 2100 in the other 15 frames, 332 at frame 2904 against about 1900; final luminance there 0.44 -> 0.32 and
  0.44 -> 0.34. The lost pixels show the head or the background (a hole, not cards). No such frame in the cards burst.
  This is a visible flash every 30 to 60 frames, on top of the 1.5x reconstruction flicker.
- Frame 2582's hole is not in the dropout list: the counter sums both draws of a key (one draw slot per key,
  layer/hair_primary.cpp `g_live_slots.find(key)`), so a hole of one of two characters can stay above a tenth.
  Open contradiction: at 2904 the counter lost 92% of the key's strand pixels while the burst shows one head lost.
- 6047 is the only key on two groom rows (10 and 11: two characters, one hairstyle). In hk1 the dominant dropout key
  3b08 is on two rows too (6 and 7).
- "rejected after the hit" for one key proves nothing about the hit: dropout_reason (common/hair_primary.h) uses the
  frame-wide traced/hit totals, so a key whose rays all missed while other keys hit is listed as rejected. The static
  one-key dropouts and the frame-wide "rays missed" of the turns (hk1 3571, 3725, 3884) may be the same class.
- The two rows exchange their content between frames: strand pixels row 10 / row 11 = 2206 / 3233 (frame 2546),
  3185 / 2177 (2606), 2167 / 3176 (2726), 3154 / 2131 (3274). Code: a dynamic slot (BLAS, groom points range, previous
  points, LOD votes) is the n-th draw of its mesh in the capture (`fd.dyn_key = mix(dk, g_dyn_seen[dk]++)`,
  layer/bvhsplit.cpp bvh_classify), so when the game swaps the order of two same-mesh draws each slot receives the
  other character's points in place and its BLAS is refit across the jump; `[bvh] static-camera stability` shows the
  dynamic draw list changing in 8 of 300 static frames.
- Hypothesis (unproven): a dropout of a two-row key happens in the frame of such an exchange. Proven by code for that
  frame: the rows' previous points are the other character's (wrong strand motion vectors for both heads). Not
  proven: why the strands are lost. Rows, points and TLAS of one publish stay consistent across an exchange, so a
  loss needs a G-buffer that traces a header older than the points and BLASes it reads; "header/BLAS mismatch
  frames" reads 0, and for a one-row key that error is only a frame of animation (invisible), so the existing
  counters cannot exclude it. Per-period parity of exchanges against dropouts in hs1 is suggestive, not conclusive:
  the log has no per-frame exchange record.
- Static flicker and frame-wide miss: one mechanism or two is NOT decided offline. The reconstruction flicker (1.5x)
  is a separate component in any case (5p).
- Diagnostic added (CPU only, always on, no key, no shader, no ABI change):
  - `[hair groom] slot content jumps (last N frames): J of M consecutive slot builds moved further than their bounds
    diagonal ...; first (build frame:key prefix)`: hair_primary::groom_bounds_jump on the existing per-slot bounds
    readback plus that build's camera (world centre). Threshold derived: a head cannot move its own hair's diagonal in
    one frame (0.3 m per frame is 18 m/s).
  - The dropouts line appends `[2 rows: a+b]` to a listed key that is on two rows: the judged frame's strand pixels
    of each row (one character lost, or both).
- Deciding observable (any S8 static live, 300 frames, no new job needed): the build frames of the jumps of key 6047
  against its dropout frames. Predicted if the hypothesis holds: every 6047 dropout frame F has a jump at build frame
  F or F - 1, jumps of 6047 come in pairs (both slots), and J > 0 only for keys on two rows. If J = 0 or the frames
  do not match, the exchange is ruled out and the per-row pixels say whether one row or both lost (one row:
  per-instance rejection; both: the key's slot).
- If confirmed, the fix is identity, not gating: a per-character identity for a hair draw's dyn slot instead of the
  occurrence index, and one draw slot per draw instead of per key.
- Installed build: `rt_hair_groom` and `rt_hair_groom_primary` are 0 in bg3rtx.ini here and at cb2d03f2, so stock
  cards are drawn unless the RTX Hair option is on. hs1/hk1 turn the strand path on (rt_hair_groom, _primary, _order,
  _current, rt_hair_primary_frame_tag, _hold, rt_hair_groom_lod_keep, rr_hair_guides, rr_hair_strand_aa=2 and the
  fibre keys).
- Fixture: hair_groom_asset_test (groom_bounds_jump: idle animation, another character, not consecutive, no bounds).

## 5r. Release policy made the toggle inert in 0.9.5-beta (offline 2026-11-10, branch claude/hair-release-policy: no key)
- 0.9.5-beta shipped with RTX Hair On doing nothing in release (dev=0): no strands were drawn. The user's session had
  the whole 5l set at 1 in bg3rtx_user.ini, but `[cfg] keys` showed rt_hair_groom, shadow_t, ao_t, cascade_drop, fibre,
  fibre_gi, fibre_spec, fibre_mapped, rr_hair_guides and rt_hair_physical_origin at 0.
- Cause: `enforce_release_policy` (common/settings.h) resets every developer-only knob to its first choice. Ten keys of
  the On choice are also owned by developer-only knobs (exp_hair_groom, exp_hair_groom_shadow_t, exp_hair_groom_ao_t,
  exp_hair_groom_cascade_drop, exp_hair_fibre, _gi, _spec, _mapped, exp_rr_hair_guides, exp_hair_physical_origin), so
  the reset undid the release control. The launcher still showed On (its `mark`, rt_hair_groom_primary, stayed 1).
- Fix: the selected choice of a release control keeps the file's values of the keys it writes through that reset.
  Only RTX Hair On overlaps a developer reset today (settings_test `test_release_rtx_hair` pins the list). Off, or a
  stale raw rt_hair_groom=1 without the control On, is reset as before; dev=1 is unchanged. `rt_hair`
  (Ctrl+Shift+F7) still gates the whole On list (layer/util.cpp `hair_gated`).
- Consequence: release players who turn RTX Hair on now really get strands, and with them the open strand flicker
  (5n, 5p: not fixed, hs1 staged). Until 0.9.5-beta is replaced, its RTX Hair toggle shows cards only.

## 5s. Stable character slots and the frame generation check (offline 2026-11-10, branch claude/hair-slot-identity: key rt_hair_slot_stable, off)
- Live result for 5q (hs1 015416, S8 static, main 026202b7): the exchange holds for the one-frame holes. The two-row
  key was 8c68; its dropout frames 2312, 2351, 2454, 2854, 2905, 3388 all lost both rows (`[2 rows: 0+0]`, 5645-5741
  card pixels, 0 strand); slot jumps only on 8c68 and always in pairs; 5 of the 6 dropouts sit on a jump frame.
- Why both rows are lost, and why it is not the owner test: both rows share one key and one draw slot, so the owner
  test passes for either. `[2 rows: 0+0]` counts sealed strand entries per row: the trace wrote none for either row
  while the card markers were written. The label "rejected after the hit" is by elimination from frame-wide totals; a
  ray that finds no strand where the card is gives the same line. Reading: the dyn slot is the n-th draw of the mesh
  in the capture, so when the order of the two draws changes, or only one of the two is in the capture (it then takes
  occurrence 0 whoever it is), a slot holds the other character's skinned points for a frame. Both rows then sit at
  one head or are exchanged against the cards' markers. Which of the two cases happens is not proven offline; the new
  counter line separates them.
- Jumps without a dropout (2289, 2778, 2928, 2967, 3061) and the dropout without a listed jump (2854): coverage of the
  diagnostic, not a second mechanism as far as the code shows. A dropout is judged only in frames whose count readback
  ran (`count_feedback` returns while one is in flight), a jump needs two consecutive bounds readbacks, and a period
  lists the first 6 jumps and the first 4 dropouts. Unproven.
- Fix (layer only, no shader variant, no ABI change): with `rt_hair_slot_stable=1` the occurrence index of a
  groom-joined hair draw follows the character, not the draw order. At classify the draw's placement is read on the
  CPU (the per-draw model matrix at set 1 binding 0 plus the camera block's origin, as `cascade_draw_id`); at the
  start of `bvh_build` `slot_stable_assign` groups the draws by mesh identity and `hair_primary::stable_occurrences`
  (common/hair_primary.h) pairs them nearest-first with the placements the occurrences had in the last capture. A
  draw without a partner takes the lowest occurrence nobody holds, so a character alone keeps its slot when the other
  one leaves the capture. Groups over 16 draws or without a readable placement stay in draw order.
- Not verified offline: that hair draws carry a distinct per-character placement in that matrix. If the game gives
  both the same matrix, the pairing falls back to draw order (no change, no harm) and the line shows 0 resolved.
- Counter line (only with the key on): `[hair groom] slot exchanges resolved (rt_hair_slot_stable, last N frames): A
  draws kept their slot against the draw order (B captures with the draws swapped, C with a draw of the mesh missing
  or new); D groups left in draw order (no placement on the CPU)`.
- Gate for the next authorized S8 static live (hs1 with the key toggled in one run): key on, A > 0 and the 8c68
  dropouts and the "slot content jumps" pairs go to 0; key off in the same run reproduces them. D > 0 with A = 0 means
  the placement is not readable: then the identity must come from the skinning buffer offset instead.
- Cost: CPU only, two mapped reads of 64 and 24 bytes per hair draw at classify; no GPU pass changes.
- Launcher: "Strand hair: stable character slots" (Advanced, experimental, off). Not in the RTX Hair On bundle until
  the gate passes. In game: S8, two characters with the same hair style, camera still: the one-frame holes stop.
- Test: hair_groom_asset_test (same order, swap, both moving and swapped, one leaving, one alone, return after the
  slot expired, a third character drawn first, no history).
- Frame generation, the user's eyewitness ("all heads switch on and off quickly", fast at a still camera): the
  rendered-frame numbers are clean (strands drawn 1438-1440 of 1440, pre-RR flicker strands / cards 1.02), and with
  fg=3 two of three presented frames are generated. What FG gets at strand pixels (layer/streamline.cpp: depth,
  motion vectors, hudless colour, UI):
  - Depth and motion vectors are per-frame binary there. `hair_primary_gbuffer.hlsl` writes the strand depth and
    `hair_primary_velocity.hlsl` the strand motion vector only where this frame's jittered ray hit a strand; the
    other pixels keep what is behind (scalp, background). A strand is about a pixel wide, so the set of strand
    pixels changes with the jitter every rendered frame. RR integrates this for colour; FG gets the stable colour
    with depth and motion that switch between strand and background from one rendered frame to the next.
  - Cycles tied to the frame tag: the tip dither phase `(tag - 1) & 3` (4 frames), `hp_cov_pick(..., tag)`, the
    jitter sequence. None alternates with plain frame parity in the rendered frames; the parity the user may see is
    rendered against generated frames.
  - hs1 also shows that FG presented nothing extra for its first 60 s (`presented per app frame 3` but
    displayed/rendered 1.00 until frame 1621), then 3.00 for the bursts.
  - `fg` is read every present: 3 -> 0 -> 3 works inside one launch when the game starts with fg >= 2 (the Restart
    mark in settings.h is for crossing on/off at startup, when Streamline is not loaded). Precedent: ef1 `fg-off`.
- Staged, not queued: `tools/live_batches_staged/hair_fg` job `hf1-s8-hair-fg` (S8, the user's set, 98 s of phases; 97 s after the fix below).
  Watch windows of 8 s, each followed by 12 presented frames (`fg_snap`, rendered and generated): fg=3, fg=0, fg=3,
  fg=3 with rr_hair_strand_aa=0, fg=3 with cards. The user says in which window the toggling stops; predicted.json
  holds the reading per answer. If it stops at fg=0: stable FG depth and motion on hair (card depth and motion from
  the hold layer at strand pixels) behind a key.
- hf1 run 073932 (main 85711bbe): INVALID, job error, fixed on claude/hair-fg-job (97 s of phases, lint 0 failing).
  - Cause: the snap windows also carried a periodic `[sl] DLSS-G frame` gate, so live_flow made them stats_reset
    frame windows. Such a window starts at its motion-ready record (07:41:30.365); `fg_snap` is applied at the
    settings read (07:41:29.806) and `present capture done` was logged at 07:41:29.932, before the window: 0 events.
  - Fix: snap windows are plain 4 s timed windows with one event gate, the last PNG line
    (`[sl] presented frame 11 -> ... (ok)`, 1.12 s after the request). Each fg=3 watch window has a runner wait
    condition: a fresh `[sl] DLSS-G frame` line with displayed/rendered >= 2.5, 30 s, else
    `pipeline readiness timed out: survive-fg3-...`. Zoom 20 wheel notches instead of 10 (head size not checked
    offline; the captures are half resolution, 1920 x 800).
  - FG timeline of that run: after the load FG went on at 07:40:37.1; the SDK cadence line showed 3.00 from
    07:40:39.7, the layer's present counter (displayed/rendered) stayed 1.00 with 0 batched presents until 07:40:50
    and reached 3.00 at 07:40:53.9. The log names no cause (no cooldown or pacing line; a foreground-change sample
    at 07:40:49.4 is the only coincident line). After the live fg 0 -> 3 at 07:41:19.2 it was 3.00 at 07:41:21.9, so
    window A (07:41:21.8 to 07:41:29.8) had generated frames throughout; the 1.00 lines up to 07:41:17 are the load,
    the warm-up and the warm-fg0 phase.

## 5t. Strands and cards alternating with a still camera (offline 2026-11-10, branch claude/hair-swap-transitions: key rt_hair_swap_latch, off)
- FG hypothesis disproved by the user's fg=0 observation 2026-11-10: in 0.9.6-hairtest1 the hair still toggles between
  strands and cards with a static camera and fg=0, worse for distant characters. Zoomed in, the flicker has a constant
  period: it flickers for a while, is stable for about 0.5 s, and repeats. The fps cap was 58: 28 frames are 0.48 s.
- What the code does (user config: hold 1, frame tag 1, footprint 0, watchdog_draw 0, shared_key 0, slot_stable 0,
  one_key 0). Two loops retry strands without any change in the scene:
  1. Frame watchdog. A swapped frame whose strands sum to less than the bound of the card pixels puts every hair draw
     on cards for `kWatchdogFrames` = 28 frames. Cards are steady during the hold (this is the stable phase, 0.48 s at
     58 fps). Then the prepass drops, the G-buffer swaps 2 to 3 frames later, and the verdict on that first strand
     frame arrives with the count readback 5 to 6 frames after it. If the fault is still there the cycle is 28 + 2..3
     + 5..6 = about 35 to 37 frames, of which 6 to 9 show strands (with holes, since the verdict was a failure).
  2. Coverage gate per key. With the hold a swapping key is judged against `kCoverRatioMin` minus 1 sigma of its count
     noise, so a key whose true ratio sits at that minimum fails about 16% of its measurements by noise alone (the
     smaller the footprint the larger sigma relative to the margin: far characters). A failed key shows cards, its
     masked probe passes the strict 0.891 a few frames later, and it swaps again: card runs of about 3 frames at
     irregular intervals. This is the frame-to-frame flicker of the strand phase.
- Evidence in the user log (lean build, so only trips): trips at frames 2740 (1949 / 3082), 2741 (15 / 2316), 2746,
  2777 (9009 / 12670, bound 0.7993), 2859 (8821 / 12875), 2860 to 2862 (2 to 3 strand pixels of 2660 to 4938).
  2746 -> 2777 is 31 frames: the first strand frame after a hold tripped again, loop 1 seen once. 2777 -> 2859 is 82
  frames: not strictly periodic there. Sums of 0.6 to 0.7 and the near-empty frames are one draw or the whole frame
  without strands (the faults of 5q and 5s, whose keys were off), not count noise. The per-trip line is capped at 8
  per run, so the log cannot show the period: the timing fits loop 1 (stable phase = the hold), it is not proven.
- Tool (always on, lean too, CPU only): one line per 300 frames while hair draws are seen,
  `[hair primary] switches (last 300 frames): N strands->cards (frame watchdog a, key watchdog b, coverage low c,
  coverage high d, coverage stale/none/invalidated e, coverage latched f, header not applied (camera gate, order) g,
  live toggle h, other i), M cards->strands; F frames with hair draws, A with every draw on cards; strand runs n:
  min/median/max frames, card runs n: min/median/max; frame watchdog trips T (their draws under own bound, above;
  frames under a tenth of the card pixels; last: frame, S/C, bound), key watchdog trips K; latched keys L; worst keys
  (prefix: switches, ratio as strands min..max (frames), as cards min..max (frames), card pixels min..max)`.
  A switch is a change of the G-buffer decision of one draw key between frames at most `kSwitchGap` = 2 apart (first
  draw of the key per frame); a G-buffer draw refused only for its card depth takes the cause that refused its
  prepass. `hair_primary::SwapTransitions` in common/hair_primary.h, fed from the existing decision in
  `hair_primary_swap` and the existing count readback. The cap of 8 on the per-trip line stays: this line carries the
  counts of every period.
- Reading the line: card runs at 28 to 31 frames with "frame watchdog" as the cause and strand runs of 6 to 9 frames
  confirm loop 1; "coverage low" with card runs of a few frames confirms loop 2; "ratio as strands" well under "as
  cards" for a key is a draw whose strands are not where its cards are (5q / 5s), not noise; "frames under a tenth"
  counts frame-wide misses; card pixels give the distance dependence.
- Fix behind `rt_hair_swap_latch` (off, live 0/1): a key whose swapped-frame measurement is outside its own bound
  (the bound the gate applies to a swapping key) is latched to cards. The first card-frame measurement after that is
  its reference footprint C0; it is released when a card-frame measurement has C with |C - C0| > sqrt(C) + sqrt(C0),
  1 sigma of each Poisson pixel count: a difference that the counts of an unchanged view do not explain. Until then
  the gate answers "coverage latched" and the probe keeps measuring without showing strands. After a frame watchdog
  hold only the keys that passed their own bound retry, so loop 1 ends as well when the fault belongs to some keys.
- Limits: a key that failed once by noise alone stays on cards in a fully still scene until the footprint moves (a
  steady card instead of a flicker); after a frame-wide miss every counted key latches; draws without a card marker
  are not counted and not latched. The latch does not repair the faults of 5q / 5s, it stops their retries.
- Default path: the latch code is behind `latch` (off); the gate tests of 5b pass unchanged and `test_swap_latch`
  runs the same sequence with the key off and on (off gives the old answers, no latch entry). No shader, no ABI
  change, variants.txt delta 0. Cost: one map lookup per hair draw decision and per counted slot; no GPU work.
- Launcher: "Strand hair: no strand retries" (Advanced, experimental, off).
- Not verified live. A user session (no job needed, the line is in the lean log) must show, key off: switches well
  above 0 per period with the causes and run lengths above; key on in the same scene: switches near 0 after the first
  period, "latched keys" > 0, strand runs no longer ending.

## 5u. Why strands fail the coverage bound (offline 2026-11-10, branch claude/hair-coverage-cause: key rt_hair_coverage_union, off)
User report (release, RTX Hair On, Ultra, still camera, also fg=0): most hair flickers strands/cards, worse far away,
absent in close-ups; zoomed in near a character it is periodic (flicker, ~0.5 s stable, again). Three candidate causes.

**A. The gate measures the wrong quantity; it falls with the pixel footprint. REAL (code + fixture).**
- What the count does (rt/hair_primary_count.hlsl): C = pixels whose card marker (mark.x) holds (tag, slot); S = those
  of them whose accepted word (mark.y) is the same word. S counts strand pixels *inside* the card markers only.
- The card marker is the native dithered alpha test (hp_tips_keep) of the mask sampled with the card's own gradients
  (SampleGrad, mip-filtered). Keep share = the filtered alpha, so C is the card's opacity at any footprint. The strand
  pixel is one ray through the same jittered sample, unbiased for the strands' area at any footprint (5d).
- Near (texels >= pixels) the alpha is sharp and sits where the strands are: S/C ~ strand/card opacity. Far, a pixel
  covers many texels, its alpha is the mean (~0.5) and the dither keeps it independently of where the strands fall:
  E[S]/E[C] -> (strand/card opacity) x (alpha the strands sit on, averaged) -> about half. The bound (kCoverMin 0.891)
  was derived for strand over card opacity (coverage_bound.py: sum of strand opacity / sum of card opacity), i.e.
  U/C with U = every accepted strand pixel. Strand radius, LOD and the trace do not change with distance in a way
  that matters: LOD k keeps share `keep` and widens radii by 1 / keep (area kept); the ray is exact on the capsules.
- Fixture (tools/hair_groom/primary.py footprint_report, CTest hair_groom_test test_coverage_footprint_metric): a
  256-texel card, 30 painted strands 6 texels wide (mean alpha 0.52), groom strands 0.95 as wide on the same lines,
  opacity ratio 0.964, 16 Halton frames. S/C at 0.5/1/2/4/8/16 texels per pixel: 0.934, 0.933, 0.890, 0.808, 0.689,
  0.585; U/C: 0.965, 0.964, 0.965, 0.966, 0.966, 0.967 (= the opacity ratio within 4 sigma everywhere).
- User log fits: `coverage used` minima 0.754-0.83 on several keys with maxima 0.97-1.0; frame watchdog trips at sums
  0.63-0.71 (2740, 2746, 2777, 2859) with bounds 0.79-0.85 (small footprints in the frame). So distant keys sit at
  or below the bound by measurement alone: loop 2 of 5t (coverage low, a few frames of cards), and a frame with
  several distant keys trips the frame watchdog for every key (loop 1, the ~0.5 s period, also the close character).
- Fix `rt_hair_coverage_union` (live, off; launcher "Strand hair: coverage check on every strand pixel", Advanced):
  the count runs `rt/hair_primary_count.hlsl?HP_COUNT_UNION=1` (also counts per tag window and slot every pixel whose
  accepted word is (tag, slot), at kUnionCountOffset 6016; the counts buffer is 7040 bytes always), and the lower
  bound, the latch and both watchdogs judge U instead of S. The upper bound keeps S (CoverageGate::Evidence::inside).
  The count slot records which request ran; a slot counted without the union is judged by S. A define, not a
  specialization constant: it adds a counts region (layout); one request, variants.txt 131 -> 132.
- Protections: U >= S for every slot (S's pixels are a subset), the bound's sigma grows with the count and the upper
  side is unchanged, so the key on can only turn a refusal into a pass, never the reverse (test_coverage_union: 20000
  random measurements x 8 gate modes; healthy keys at 430-9397 card pixels: identical decisions, 0 switches with and
  without the hold). Missing strands still fail (U = 0, or U/C 0.5). Default request byte-identical
  (tools/shader_identity.py vs 2e1f28d0: 226/226), the new one compiles and passes spirv-val.
- Kind of change: a measurement change of the gate, not a reconstruction or shading change. With it distant hair stays
  on strands; mean brightness there changes from the card look to the strand look (the existing strand path).
- Cost: key off none (1 KB more buffer and readback copy). Key on: one groupshared atomic per marked pixel and tag
  window in the existing count dispatch; unmeasured, expected far below the count's own cost.

**B. Draws or frames with ~0 strand pixels. REAL as known mechanisms; the user log alone does not say which.**
- Code paths that write card markers but no strand on a still scene: a two-row key whose slots exchange content
  (5q/5s: `[2 rows: 0+0]` in S8, fix rt_hair_slot_stable, off, untested live), a shared key with a missing row (5i,
  rt_hair_shared_key), a LOD change dropping the groom (5h cause 2, rt_hair_groom_lod_keep in the bundle), and before
  5h the header without the build camera (now unconditional). The user log's near-empty frames 2741 and 2860-2862
  come right after trips, during groom LOD uploads (36576f20, c4938983, a8cd960d), a refused mesh pool growth and UV
  joins: not a still scene in the counted sense. With A, few draws stay swapped, so one bad draw is the frame sum.
- No speculative fix. The frame-wide fallback stays for frame-wide evidence; per-key decisions exist:
  rt_hair_watchdog_draw (every key on its own, 2 bad frames in a row) and rt_hair_watchdog_one_key.

**C. Count noise at the bound. NOT an independent cause.** With S a far key's mean is at or under the bound, so the
hold's noise band decides each frame: that is A. With U the fixture's per-frame U/C spread is 0.0128 at 2142 card
pixels and 0.025 at 536 (the bound's model sigma there is 0.042 and 0.085: stratified jitter, correlated counts):
own-bound fails 0 of 64 frames down to 534 pixels and 1.5-3% at 133 pixels (the strands at 0.964 and at 0.925 opacity ratio). No fix; the session numbers below decide.

**Lean line for the user session (all new keys off), one per 300 frames:** `[hair primary] coverage by footprint
(last 300 frames, judged inside the card markers): card pixels <bin>: strands n, S/C, judged, H/C, U/C (n), own-bound
fails; cards n, S/C, U/C (n) | ... dropouts D (frame-wide F of G judged frames, nothing traced T, traced then rejected
R, keys on two rows W)`. H = the shared pixel trace's hits on the key's own groom rows (existing counts, no shader),
so A is decidable with the keys off. Bins of card pixels 64-255, 256-1023, 1024-4095, 4096-16383, 16384+.
- A confirmed: strands S/C in the 64-255 and 256-1023 bins at least 0.1 under the 4096+ bins and under 0.89, own-bound
  fails mostly in those bins, while H/C there stays within 0.05 of the 4096+ bins (>= 0.9). Refuted: H/C falls with
  S/C (then strands really cover less when small: a rendering fix, minimum strand width by footprint, not built).
  5t switches line: "coverage low" and "frame watchdog" causes, worst keys with card pixels under ~2000.
- B: D > 0. W = D: the 5s exchange (try rt_hair_slot_stable); F > 0 with W = 0: a per-frame header/publish fault;
  T (nothing traced: header, TLAS, rebase, row) against R (traced, then ownership/key map/row UVs) names the stage.
- C: own-bound fails in a bin whose "judged" mean is >= 0.95 (key off: 4096+ bins; predicted 0).
- With rt_hair_coverage_union=1 (next session): judged = U/C flat over the bins (0.9-1.08), fails near 0 above 256
  card pixels, switches near 0 with a still camera; dropouts unchanged in kind (U = 0 is still a dropout).

**Live evidence: hs1 085153 (S8 still camera, main 2e1f28d0, VALID, the user saw the flicker; 30 switches lines of
60 frames).**
- The frame watchdog is not the loop here: trips in 2 of 30 periods (frame 1492, 1075/1867; 5 trips near 2116,
  7773/14059, after the live toggle window, 2 frames under a tenth).
- The steady flicker is per key: in about 15 of 30 periods one key goes strands->cards 1-5 times per 60 frames, card
  runs 1/1/1 (max 4), causes "coverage low" and "other" in about equal numbers.
- "other" is the echo of the same refusal, not a second cause: the period's swap refusals pair it with
  "prepass: strike N" and "G-buffer: card depth not dropped N" (e.g. 2280: coverage low 4, card depth not dropped 6,
  prepass strike 6). Refusal::Strike is SwapGate::allow_prepass, the prepass/G-buffer handshake (the key's G-buffer
  drew strands in the previous publish); after a coverage-low card frame the next prepass keeps its card depth, so the
  G-buffer after it draws cards too ("card depth not dropped", switch cause of the prepass refusal = Other). One
  refusal shows as two one-frame card flashes one strand frame apart.
- be12 (two groom rows, 5650-6200 card pixels, ratio 0.94-0.96, up to 11 switches per period, min 0.000): cause B,
  the 5s slot exchange, not a measurement fault. Every dropout is `be12:~5700/0 rejected after the hit [2 rows: 0+0]`
  (card markers written, no strand accepted) at build frames of `slot content jumps` pairs for be12
  (2204, 2205, 2232, 2245, 2356, 2677, 2822, 2861, 2865, 2866; 2275 and 2359 lie past the 6-entry cap). The draw really drew no strands that frame (a
  one-frame hole); the probe traces the same exchanged rows, hence 0.000 "as cards" too. rt_hair_slot_stable was 0.
  The zero is read 5-6 frames later and refuses a healthy frame: a second artefact.
- ca38 (393-456 card pixels in those periods, as strands 0.815..0.938, as cards 0.882..0.928): S/C straddling the
  bound at a 400-pixel footprint. With S that is A plus noise (5u fixture: S/C 0.81 at 4 texels per pixel); U/C
  decides (the coverage by footprint line). 5d82 (1..1161 card pixels, 0.143..0.967): a changing footprint, the
  small end below kDropoutMinCard-scale evidence.
- Fix `rt_hair_coverage_confirm` (live, off, CPU only; launcher "Strand hair: confirm a failed coverage check"): a held
  swapping key returns to cards only after kCoverConfirm = 2 measurements in a row outside its own swapping bound
  (kWatchdogDrawBad's reason: one header per G-buffer frame, so a one-publish fault makes one bad measurement; noise
  fails twice in a row at p^2). A key on cards keeps the strict start bound. Relaxation only (test_coverage_union
  part 6: be12's sequence, a real two-frame loss still refused, 2000 random sequences: never refuses what the hold
  alone allows). It removes the delayed card flash; the hole frame itself is fixed by rt_hair_slot_stable.
- Next session with the keys on (union, confirm, slot_stable): switches "coverage low" and "other" near 0, be12
  dropouts 0 with "slot exchanges resolved" > 0.

**Keys-on result: hs1 095322 (S8 still, union + confirm + slot_stable = 1, VALID); before = 085153 (keys off).**
- Switches over 30 periods: coverage low 25 -> 0, other 22 -> 0, frame watchdog 15 -> 10 (all in the one period after
  the strands-on toggle), live toggle 11 -> 11, coverage stale 0 -> 2 (key 5364). Periods with per-key switches:
  about 15 -> 0. Judged ratio (U/C) 1.00-1.04 flat over 64..16383 card pixels; own-bound fails 0 in 84 of 84 bins
  outside the toggle period; dropouts 0 in 28 of 29 periods; slot exchanges resolved 104 draws per period.
- be12 46 switches -> gone; ca38 29 -> 0; 5d82 16 -> 0.

**Audit after that run (2026-11-10, branch claude/hair-audit, offline), ranked by visible impact.**
1. Strands-on transient (095322 period ending 09:55:11.313; 085153 at frame 2116; same shape after a load or a new
   groom in the user's log). After the live switch every groom slot is rebuilt (layer/bvhsplit.cpp: a slot built
   without per-point UVs gets a new points range and a full build), votes its LOD again from 0 and re-joins its UVs
   (UV pending 12, LOD switches waiting for upload 15, full builds 11, compacted 11 in that period; new uploads
   16fa158c LOD 0/2, 5dbb53eb, b7349689 LOD 2 at 09:55:09.5-10.1). The losses (G-buffer 2163-2169: 2994/4734, 84/3301,
   119/3502, 170/3719, 4518/7608; 26 dropouts, all "rejected after the hit", one frame-wide) fall inside that
   rebuild. Which stage drops them is not decided: the per-row hit count H uses the judged frame's row keys, and the
   row set changed in that period (12 -> 13 rows, "different grooms 2"), so its "nothing traced 16 / rejected 10"
   split is not reliable there. The live switch skipped the level-load warm-up (it only waited for two publishes).
   Fix `rt_hair_live_warmup` (off, live, CPU only): the strands-on edge opens the level-load quiet window
   (LoadGuard::restart), ended by rt_hair_warmup's settled signal (30 frames without full builds, LOD votes, uploads
   or UV joins), at most 300 frames. Predicted live: no frame watchdog trips or dropouts in the period after a
   toggle; strands back after about 30-80 frames instead of 2. Not covered: a groom that rebuilds mid-play (a new
   character, a LOD upload); that needs the stage found first (live: dropouts of that period with the row keys of
   the applied header, not the record-time ones).
2. Tiny hair (5364: 0..436 card pixels, mostly 1..55, union ratio up to 6.0; 8 switches, "coverage stale"). Under
   64 card pixels one measurement says nothing (cover_sigma(64, 64) = 0.25); the watchdogs and dropouts already ignore
   such draws. A swapping key whose draws stay that small never refreshes its evidence: stale after kHoldFrames (28),
   cards, a tiny probe passes, strands again: a 30-frame cycle. Fix `rt_hair_coverage_min_pixels` (off, live, CPU
   only): measurements under kDropoutMinCard replace nothing, so tiny hair stays on cards steadily until it is drawn
   at 64 pixels or more. Identical answers at >= 64 pixels (5000 random cases). The ratio cannot reach "coverage
   high": the upper bound is judged on S, and S <= C.
3. Upper bound: unenforced in practice. S <= C per slot, so kCoverMax (1.095) never refuses with either count. With
   U, real keys reach 1.07-1.08 at 256+ pixels (50a3 1.075, c09f 1.071) and 1.12-1.34 only under ~230 pixels. No
   flicker can come from it; a groom wider than its cards would show thicker hair, not be refused. Left as is.
4. Steady cards, not flicker: "coverage stale" 684 and "coverage none" 128 G-buffer refusals in 095322 are keys that
   never get fresh evidence (tiny or hidden draws); "count unknown 120" per period are two S8 draws with no groom
   (index counts 3399 and 7086: in no groom file of the 871 in data/assetdb). Both stay cards.
5. Groom catalogue (data/assetdb/hair_grooms: 871 files, all v2; 447 distinct index counts, 141 shared by several
   files and told apart by the UV join). The installed release has 29 grooms (converted on the user's PC). The user
   log's "no groom file" counts: 4578, 4800, 7224, 9384, 14073, 78216 (4 files), 123912 (2 files) exist in the repo
   catalogue but not in the install (conversion pending: cards until the launcher converts them); 1623, 3399, 4077,
   7086 exist nowhere (cards). LOD0 strand radius median 0.143 mm (p5 0.082, p95 0.257); 122 grooms have points at
   the 2 mm radius clamp. Per-groom offline U/C and S/C need the batch cache (captures/hair_groom_batch, gone with
   the 2026-11-07 format): regenerating it is the batch conversion (hours); the fixture says U/C tracks the opacity
   ratio at every footprint, so the bound's 2631-view minimum (0.9105) still holds per groom.
- Bundle (no default changed here): union and confirm only turn refusals into passes (proved by test), slot_stable
  only changes which slot a draw keeps; all three passed 095322. What could regress: with union, strands misplaced
  inside their card's raster (a pose or LOD fault) pass where S would have fallen; with confirm, a fault that alternates
  bad/good every frame is never refused (holes on every other frame); with slot_stable, groups without a readable
  placement fall back to draw order (as today).
- Bundle default (user decision 2026-11-10, branch claude/hair-keys-default): RTX Hair On (common/settings.h
  `exp_hair_groom_primary` "on") now also writes `rt_hair_coverage_union=1`, `rt_hair_coverage_confirm=1`,
  `rt_hair_slot_stable=1` and `rt_hair_live_warmup=1`. Code defaults stay 0, so RTX Hair off is unchanged; the
  Advanced knobs stay for A/B. Warm-up evidence: VALID hs1 103633 (five keys on), 30 periods: frame watchdog trips 0
  (095322 had 5 after the toggle), dropouts 0 in 27 of 27 footprint lines (26 after the toggle in 095322), own-bound
  fails 0 in 82 bins; after the live toggle 11 keys returned to strands after card runs of 80 to 105 frames, no trip.
- Disproved prediction, `rt_hair_coverage_min_pixels` (hs1 103633): predicted 0 "coverage stale" switches on tiny
  hair, observed 5 (key 2aa3 once at 57 to 70 card pixels, key 9b09 four times at 1 to 583; 095322 had 2 on key
  5364). 2aa3 sits across the 64 pixel threshold, which has no hysteresis: the key may add flapping there. Not
  qualified, not in the bundle, not fixed here.

**Per-groom coverage table (2026-11-10, branch claude/hair-groom-coverage, offline; replaces item 5's open point).**
- Source: the native converter on the whole catalogue (`bg3rtx_hairconv --all`, LSLib 1.20.4 granny2.dll from the
  user's folder, never copied): 20 styles in 6 s (220/min), all 1,217 rows in 406 s with --export-hcap (4 jobs x 4
  threads): 898 pass, 282 needs-review, 37 reject. The converter's validation already renders strands and cards in
  three views at 2 mm pixels with the bound's own composited opacities (tools/hairconv/validate.cpp: sum_g, sum_c); it
  now also writes their ratio per view, `coverage_ratio` (it wrote only the difference `coverage_delta`). That is U/C,
  the quantity rt_hair_coverage_union judges, at the near footprint; U/C does not change with the footprint (5u).
- U/C: every written groom (898 pass) lies inside the gate's bounds in all 3 views: 0.907 (BEARD_BUGBEAR_Short_A LOD1)
  to 1.075 (BEARD_HUM_M_Short_C) against [0.8913, 1.0945]; 3 have a view under 0.92, 1 over 1.07. Outside the bounds
  are 72 rows, none written: 37 rejects (headwear braids with an empty view, corpse hair 0.11-0.20, Afro_Short_C LOD1
  0.45, ...) and 44 needs-review (the Watcher headwear hair 0.882-0.888, stuffed bugbear heads 0.882, Minthara LOD0
  0.881-0.894). With --accept-review those 44 would flicker at the lower bound: keep them unwritten.
- S/C by footprint (tools/hair_groom/coverage_audit.py, the test card on each style's real card mesh and mask in
  texture space, 898 grooms, 35 min at 6 jobs, cached): median S/C relative to 1 texel per pixel 0.947 / 0.855 /
  0.763 / 0.694 at 2 / 4 / 8 / 16; with each groom's 3-D ratio, the old count falls under the bound for 15% / 81% /
  99% / 100% of the grooms at 2 / 4 / 8 / 16 texels per pixel (cause A, every style). Texture space has no view term:
  its U/C (median 1.176) overestimates the 3-D ratio (median 0.988), so only the relative S/C fall is taken from it.
  Strand points sit on mask alpha 0.944 (median; p5 0.894): the barycentric placement is right.
- Not in any catalogue row: index counts 3399 and 7086 (the two S8 draws on cards in every hs1 period). They use the
  static CHAR_Hair shader but are no LOD 0/1 CHAR_Hair row with an MSKA mask, so the converter never sees them;
  naming them needs a GR2 index-count scan. 1623 and 4077 are LOD1 beards (BEARD_HUM_M_Short_H/I/J) left as
  needs-review (no file). The user's other unmatched counts (4578, 4800, 7224, 9384, 14073, 78216, 123912) all have a
  passing groom (9384: DWR passes, the HUM variant needs review): only conversion on the user's PC is missing.
- 2 mm radius cap: 124 of 898 grooms have points at it, at most 0.17% of their points (OGRE dreads), median 0.003%,
  medians 0.13-0.23 mm. No visible effect: a few clumps on near-degenerate LOD1 UV triangles are clamped.

**Post-toggle holes with the v3 set (hs1 125217, main a2211fc9, 2026-11-10, branch claude/hair-v3-settle, offline).**
- Not v3 and not the warm-up's settle test. The warm-up ended correctly at frame 2160 (30 settled frames after the
  switch-on at 2130: 12 instances, full builds 0, refits 720, no UV join pending). The losses start at the first
  appearance in the session of three more hair draws (UV join 45252 at 12:54:23.15, 100101 at .47, 7092 at .53;
  their grooms decode in 31-78 ms and upload: 16fa158c LOD 0 at .376 and LOD 1 at .646, 5dbb53eb LOD 0 at .756,
  b7349689 LOD 0 at .866). Trips at G-buffer frames 2171-2176 (249/3702, 0/5160, 30/6015, 63/6737, 91/7655,
  138/8679) line up with those uploads. The keys that lose their strands are the existing ones (931e, 5149, 18db, d3cc
  at 2171; worst 08ab, 13c7, 18db), not the newcomers. 103633 (v2) has the same three joins and uploads at the same
  scene time, but 17-30 frames after its switch-on, inside the warm-up window, so no key was swapping (0 trips; strands
  back after 80-105 card frames). 095322 (v2) shows it 50 frames after the switch (trips 2163-2169). So the toggle
  and v3 only move when the window ends against the characters' arrival. This is the open "groom rebuilt mid-play"
  item: a groom that joins while other keys swap.
- Mechanism (code, likely, not proven offline): a newcomer's rows are keyed after their second update
  (hair_primary.h groom_primary_key, bvhsplit.cpp:3957 instance mask 0xFF), while its draw still shows cards and is
  probed (coverage none until its first count, about 6 frames). The shared pixel trace of every swapped draw skips a
  non-swapping slot's strands by restarting past them at most kHpSkips = 3 times (hair_primary_gbuffer.hlsl:41, :99-116),
  then stores no strand. Every pixel ray with 4 or more of the newcomer's strands in front of its own returns
  nothing. The header line shows it: mask-hole pixels (traces that skipped a card-kept groom) 82744 in that period
  against 660-4760 in the clean periods; 59158 in 095322's transient. Not shown: that the newcomer's strands lie in
  front of every other key along most rays (the loss is frame-wide).
- No fix landed: the fix at the cause is not small. Either the shared trace finds the closest strand of an accepted slot
  without a skip limit (a non-opaque groom query that commits only accepted candidates, as rt_groom_blocks walks
  candidates), or newcomer rows get their own instance mask bit that only their own probe traces. Both change the
  strand G-buffer shader (a specialization constant, restart-applied) and need hair_primary_gpu_test checked on the LSS
  device. Staged: (1) a GPU fixture in hair_primary_gpu_test with a dense non-swapping groom in front of a swapped
  one: count the pixels that come back with no strand at kHpSkips 3 (decides the mechanism offline); (2) the
  candidate-filter trace behind a key, compared with the fixture; (3) one hs1 rerun.
- Predicted for an unchanged rerun of hs1 with the v3 set: the transient appears exactly when the three joins
  (45252/100101/7092) fall after the warm-up's end. If so: 4-7 frame watchdog trips and 40-60 dropouts in that one
  period, mask-hole pixels above 30000 there; every other period clean. If the joins fall inside the window: 0 trips
  and strands back after 80-105 frames, as in 103633.
- GPU fixture (hair_primary_gpu_test nearest_check, RTX 5090, run once, device fine): the synthetic groom with 600
  strands, twice in one TLAS (instance 0 accepted; instance 1 the same groom 3 cm towards the camera, card-kept), 160 x
  160 rays. Of 25478 pixels where instance 0 alone has a strand, the restart loop of the strand G-buffer variant leaves
  2567 without a strand (1860 rays need a 4th restart) and returns a farther strand of the right groom on 11154 more:
  each restart begins 2 x hair_primary_max_radius = 2 cm past the other row's strand and jumps over an accepted strand
  that lies within 2 cm behind it. Mechanism confirmed offline; the second defect (wrong, deeper strand) was not known.
- Fix `rt_hair_groom_nearest` (off; specialization 1047 = kSpecIdHairGroomNearest, compiled into the strand G-buffer
  pipelines when on at their creation, header flag kFlagNearest = 64 switches it off live, as rt_hair_groom_coverage):
  hp_trace takes the closest strand of an accepted row from one non-opaque query that passes over other rows'
  candidates and commits accepted ones (shaders/rt/hair_primary_nearest.hlsli hp_nearest, the rt_groom_blocks walk; the
  traversal reports only candidates closer than the last commit). Found on the device: LSS candidates carry their own
  candidate type (not triangle); they give CandidateTriangleRayT and commit as triangle hits. Same fixture: hp_nearest
  returns the reference's own strand and distance on 25478 of 25478 pixels, 0 holes, 0 hits of the card-kept groom.
  Key off: 223 requests byte-identical, the strand G-buffer request equivalent with 1047 frozen to 0
  (tools/release_shader_equivalence.py --freeze-spec 1047, cascade control PASS); variants.txt delta 0.
- Cheaper alternative not taken: hiding a groom's rows from primary rays (bvhsplit.cpp:3957 mask 0x01) until its draw
  key swaps would hide them from its own masked probe too (the probe traces the same TLAS with mask 0xFE), so a
  newcomer could never be measured and would stay on cards; making it work needs a second mask bit for the probe, a
  shader change anyway, and it leaves the 2 cm restart jump in place.
- Cost: the fixture has no timer. A non-opaque query visits every candidate in front of the closest accepted one
  (opaque traversal stops at the first); read it live from `[hair primary] strand G-buffer draws GPU` (0.133 ms/frame
  in 125217 with the loop).
- hs1 (staged job, regenerated): the key is on for the whole run (it is applied at pipeline creation; the arrival
  happens once per session, so no A/B inside one launch). Predicted, v3 set: in the period with the three arrivals
  0 frame watchdog trips, 0 frames under a tenth, dropouts at most 2, key watchdog trips 0, strand runs not cut;
  every other period as 125217 (coverage low 0, own-bound fails 0); strand G-buffer GPU above 0.133 ms/frame, the
  increase reported (not a gate). Mask-hole pixels still count passed-over candidates (a diagnostic, now harmless).
- Live hs1 133243 (S8 still, main 613a272f, v3 set, rt_hair_groom_nearest=1, VALID): 30 of 30 periods with 0 frame
  watchdog trips, 0 key watchdog trips, 0 dropouts and no "coverage low" switch; 2 "coverage stale" switches (the tiny
  hair item, 5364 class). Strand G-buffer GPU median 0.171 ms/frame (0.118..0.320, 27 lines) against 0.227
  (0.097..0.307, 27 lines) in 125217: no measurable cost. (The 0.133 above was one period of 125217, not its median.)
  Not proven by this run: the three arrivals (UV joins 45252 at 13:34:25.14, 7092 at .45, 100101 at .62) came 0.6-1.1 s
  after the switch-on at 13:34:24.54 (frame 2219), inside the warm-up window (it ended at 2312), as in 103633. The
  failing timing of 125217 was not reproduced: the live shows no regression, the proof of the fix is the GPU case
  (2,567 holes to 0). Passed-over pixels 27,403 and 37,441 in the two back-on periods (103633: 30,036, 35,197).
- In the RTX Hair On bundle since this run (branch claude/hair-nearest-default; code default 0). It is compiled into the
  strand pipelines at their creation: a game started with RTX Hair off keeps the restart loop until a restart.

**Late arrival on demand (branch claude/hair-late-arrival, offline, staged job, 2026-11-10).**
- `rt_hair_debug_late_arrival` (developer only, an index count, 0 off, pinned 0 in data/release.ini, in no preset or
  bundle; CPU only, no shader, no ABI change): after every live strands switch-on the hair draws with that index count
  get no groom for kLateArrivalFrames = kLoadQuietFrames + 1 = 301 frames (bvhsplit: no groom work, no UV join, not
  counted as unsettled by the warm-up, as if the character were not in the scene). 301 is one past the longest
  warm-up window, so the release falls after the warm-up's end whatever its settled signal does (133243: it ended 93
  frames after the switch). Re-armed by every switch-on (hair_primary::LateArrival). Lines: `[hair primary] late
  arrival: groom of the N-index hair draws held from frame F ...` and `... released at frame F, D frames after the
  switch-on (H draw frames held)`. CTest hair_groom_asset_test test_late_arrival.
- Selection: 45252, the draw whose groom (16fa158c, LOD 0 889,639 points) was the first upload of the 125217 transient
  (trip at 2171, one frame after it). One count keeps the key an int; the other two arrivals are not held.
- Job (STAGED, not queued): tools/live_batches_staged/hair_late/jobs/hl1-s8-late-arrival.json (plan in plans/,
  generator make_job.py, predicted.json). One S8 launch, the RTX Hair bundle with rt_hair_groom_nearest=1 at startup,
  static camera lowered 10 notches; warm-loop (nearest 0, live: header flag off), off-a, arrival-a (481 frames, nearest
  0), off-b, arrival-b (481 frames, nearest 1). 481 = 301 + 3 periods of 60. live_flow lint (live and frozen corpus):
  0 failing; 4 readiness queries added to lint_corpus_snapshot.json (extend_readiness, nothing else changed).
- Predicted (predicted.json): both windows hold the release line (301-399 frames after the switch-on). arrival-a: at
  least one period with frame watchdog trips >= 1 or dropouts >= 1 (125217: 6 trips, 55 dropouts); arrival-b: every
  period line with frame watchdog trips 0, key watchdog trips 0 and dropouts 0-2. If A shows no holes, the test does
  not reproduce 125217 with 45252 alone (the fix stays proven by the GPU case only); the period lines then say why.

## 5v. Pixel-footprint strand LOD for camera rays (branch claude/hair-pixel-lod, 2026-11-10: steps 1 and 2 (b) built, key rt_hair_pixel_lod off)
Study: docs/progress/claude-hair-pixel-lod-study.md (eca698b9). Keeping fewer strands about 1 render pixel wide for the
camera rays gives HF like the cards and 1.5-2.4x less coverage noise at heads of 80-150 px (coverage +3 to +6%); it
fails under about 60 px; secondary rays must stay at keep >= 0.25; LOD steps pop about 2x today's.
- Step 1 (built, inert until step 2 uses it): `decode_v3_lod(..., extra_keep)` (common/hair_groom_v3.h) rebuilds keeps
  1/8, 1/16, 1/32 (kV3ExtraKeeps) from the stored LOD 0 with the table's nested per-strand hash, radii x 1 / keep under
  kMaxThinRadius (9 mm); no file changes, the 888 v3 files are read as they are; the table's count check is skipped
  for these (no table entry), an empty result is refused. CTest hair_groom_v3_test: on the fixture all three keeps are
  non-empty, each nested in the coarser level (1/32 in 1/16 in 1/8 in the coarsest table LOD), every radius = LOD 0's
  / keep (capped) to 1e-6; an extra keep with a table index or keep >= 1 is refused.
- Step 2 design (cheapest correct route found, not built):
  - A camera-only BLAS per groom built on the groom's current points (no second points range, no second update
    dispatch): LSS indexing SUCCESSIVE reads segment starts from an index buffer and radii from a radius buffer, so
    a camera BLAS is the same vertex buffer with (a) the kept strands' segment starts (static per LOD/keep pair) and
    (b) a widened radius buffer (static per pair: r_lod x keep_lod / keep_cam, capped 9 mm). Refit every frame with
    the groom's BLAS (the points move).
  - A camera groom TLAS per scene slot (same instance order as the groom TLAS); the header carries its address and
    rows whose segment table (row.z) is the camera index buffer. Points, UVs and previous points are the groom's own,
    so hp_segment, the velocity variant and the count shader read the right points: no shader change, the strand
    shaders stay byte-identical, variants delta 0. Shadow, GI and fibre rays keep the groom TLAS (keep >= 0.25).
  - The cheaper route asked for (camera rays rejecting non-kept strands in hp_nearest against a widened radius)
    does not exist: an LSS intersection uses the radius in the BLAS, so a wider strand needs its own BLAS; a
    candidate filter can only remove strands, not widen them.
  - Pick: per groom, keep_cam = the finest of {lod keep, 1/8, 1/16, 1/32} whose projected median diameter is >= k
    render px (k = rt_hair_pixel_lod_k, 0.7-1.4, default 1.0); px per metre from the build camera's projection and
    the bounds distance; no pixel LOD for heads under 60 render px (the study's failure bound; today's LOD there);
    hysteresis of one quarter octave on the diameter plus the existing 30-frame vote.
  - Cross-fade between two keeps needs a per-candidate keep test in the strand trace (a shader change behind a
    specialization constant); without it the step pops about 2x today's (open).
  - Costs (estimates from the study's groom and today's sizes, to measure): mesh pool + radius buffer 4 B per point of
    every camera-LOD groom (13 grooms at S8, about 4.5M points: about 18 MB) + index buffers 4 B per kept segment
    (under 1 MB); BLAS memory about keep_cam / keep_lod of the groom BLAS (1/2-1/8: about 5-15% more BLAS); build
    time one more refit per groom on 1/2-1/16 of its segments (about 10-30% of today's groom refit ms); one 13-instance
    TLAS build per frame (negligible). The strand G-buffer trace gets cheaper (fewer, fatter segments).
  - Checks to add with it: the coverage gate's upper side is judged on S <= C, so wider strands cannot trip "coverage
    high"; hp_on_card's band uses hair_primary_max_radius (10 mm) >= the 9 mm cap; the tips test is per pixel (no
    radius). A period line: grooms per camera keep, projected diameter range.
- Step 2 (b) built (branch claude/hair-pixel-lod, offline; key rt_hair_pixel_lod, off): layer/bvhsplit.cpp.
  - Camera arrays per (groom LOD, level j): `hair_groom::camera_subset` (common/hair_groom_v3.h) keeps the LOD's strands
    whose LOD 0 index (the n-th strand of a LOD is the n-th index the hash keeps at keep_lod; strands0 = table points -
    segments, `Asset::lod_points`) passes keep_lod / 2^j, radii x 2^j under 9 mm. Uploaded once into the mesh pool
    (radius 4 B per LOD point + index 4 B per kept segment), counted in the groom budget, freed with the LOD (eviction,
    discarded upload, grooms off) and when the key goes off. The LOD's segment starts and radii stay on the host while
    the key is on (8 B per point; decoded again once if the key went on after the upload).
  - Pick per slot: px per radian = render height / 2 x |P[1][1]| of the build's camera UBO (host mapping; 0: level 0),
    head px = 0.25 m (the study's head height, kPixelLodHeadM) / distance x that (fix 2026-11-10: was the bounds
    diagonal, 1.5-3x high), d0 px = 2 x (median LOD radius x keep_lod) / distance x that; the median radius is set at
    every LOD upload whatever the key (fix 2026-11-10: it was read from host radii kept only while the key was on, so a
    LOD uploaded before the key went on gave d0 0 and level 0 for good: 12 of 13 grooms in hl1 180301/180949);
    `pixel_lod_level` (prev = the built level) then the 30-frame `lod_vote`. A level still uploading keeps the built one.
  - Camera BLAS per slot on the groom's points range (LSS successive, kept-segment index + widened radius buffers, not
    compacted), full build on a level or LOD change, refit with the groom BLAS in the same batch; destroyed with
    kGroomFreeLag on a level/LOD change, slot release and key off.
  - Camera groom TLAS per scene slot (`g_cam_tlas`, own scratch): the groom TLAS's instances at the same rows (inactive
    gaps zero), each row's camera BLAS or, at level 0, its groom BLAS. The header publishes its address and row.z = the
    camera index buffer; if it is not built the header falls back to the groom TLAS and the groom's segment table. The
    TLAS check (`hair_primary_groom_tlas_built`) records the header's TLAS. Secondary rays (scene stamp) keep the groom TLAS.
  - Readers of the pixel record's segment: the strand G-buffer, velocity, count, coverage, gi/hair_strand_pixel.hlsli,
    rt/hair_fibre_gi.hlsli and rr_strand_aa.hlsl all resolve it through the header row (hp_segment): consistent.
    InstInfo word 3 (rt_groom_links) and hf_groom only see primitives of their own groom TLAS traces: unaffected.
  - Key off: no allocation, no camera TLAS, header and rows as before. Shaders unchanged: tools/shader_identity.py
    227/227 byte-identical to e5f62b50; variants delta 0. Test: hair_groom_v3_test, on the fixture every table LOD x
    levels 1-5 equals decode_v3_lod's extra keep keep_lod / 2^j strand for strand and radius for radius (14 pairs).
- (d) Period line (detailed logging, with the key on): `[hair groom] pixel LOD (last N frames): groom frames per camera
  level 0/1/2/3/4/5 ...; LOD 0 strand diameter A-B px, head C-D px (P px per radian, n groom frames without a projection);
  camera BLAS full builds, refits; camera arrays MB, camera BLAS MB (k)`. Pool MB, BLAS MB and refit ms are not measured
  offline (no layer BLAS fixture); the live part below reads them (refit cost: the existing `gpu` lines of the build).
- (e) Live, staged not queued: part 2 of tools/live_batches_staged/hair_late (hl1-s8-late-arrival, one S8 launch with the
  arrival part unchanged and rt_hair_row_stable=1): static zoomed-out camera, rt_hair_pixel_lod off / on / off in one run
  (60-frame windows with stats_reset), PNG bursts 99310 (off) and 99311 (on), the period line and the strand G-buffer GPU
  ms per window; predictions in its predicted.json (part2_rt_hair_pixel_lod). live_flow --frozen: 0 failing.
- Not built: (c) the cross-fade between two adjacent levels (spec constant 1048); without it a level change pops.
- User check: none yet (rt_hair_pixel_lod is a dev key, not in the launcher); the hl1 part 2 bursts show it (S8 zoomed out:
  strand hair about 1 px wide, less grain next to the cards). A launcher toggle comes once the live part qualifies it.
## 5w. Stable groom rows (offline 2026-11-10, branch claude/hair-row-stable: key rt_hair_row_stable, off)

Live hl1 164930 (natural arrival, camera wheel zoom): of the 5u candidates A (another build's TLAS traced: 75 submits
checked, 0) and C (full BLAS builds: 0) are disproved, B is confirmed: in the first frame of the zoom 11 of 11 keyed
rows held another key than in the previous publish (row 0: 8553 -> e769), then 7, 4, 4, 0, 7, 3, 5, 7, 4, 7, 6, 7, 0,
7, 9; the frame watchdog tripped at G-buffer frames 2125, 2126, 2128, 2129, 2130 (39 dropouts, 28 "rejected after the
hit"). Before and after, the row order was constant for hundreds of frames with 0 dropouts.

- Cause of the changing order: a groom's row (groom TLAS instance index, header row, InstInfo row) is its position in
  the build's dynamic draw list `dyn`. `layer/bvhsplit.cpp` sorts that list by slot key only inside
  `if (hold_frames && !moving)` (the hold path); with a moving camera (`rt_camera_moving`: the wheel zoom) or
  `rt_dyn_hold 0` it stays in the game's capture draw order, which the game changes from frame to frame (the "slot
  exchanges resolved" line counts it). The rows then follow `groom_work` in that order. Nothing kept a key's row.
- Which consumer pairs a row with another publish: not established. Every layer consumer read (strand G-buffer,
  nearest trace, velocity, count attribution, draw slots, submit upgrade, points update) resolves instance, key map and
  row from the one header applied to its frame, and the TLAS check sees no other build. The evidence (every key
  rejected in the first reordered frame, none while the order is constant) says a TLAS and a header of different
  orders meet on the GPU; the CPU checks cannot see where. Stable rows remove the exposure whatever the pairing: any
  two publishes then name the same groom in every row both hold.
- Fix (`rt_hair_row_stable`, host key, live, off by default because the consumer is not proven):
  `hair_primary::GroomRows` (common/hair_primary.h) gives each groom (dynamic slot key) a row it keeps while it is
  live. A row whose groom left is given to another groom only 8 build frames later (`kSceneSlots`: one groom TLAS per
  scene slot, so no frame in flight still reads the old groom there); a groom that returns earlier takes its own row
  back. Rows between live grooms are inactive TLAS instances (reference 0) with key 0 and zero header and InstInfo
  rows; the row count is the highest live row + 1. More than 64 grooms inside 8 frames: the oldest freed row is reused
  early and a stats line says so. Key off: the position in `groom_work`, as before (no state kept).
- The first-seen diagnostic now counts a row only when it held a key before and holds another one now (a newcomer
  taking an empty row is not a move).
- Test: hair_groom_asset_test replays the arrival (12 grooms, one leaves at the camera move, draw order alternating
  every frame, three newcomers, one late groom): 0 grooms change row, 0 rows name different grooms in consecutive
  publishes, the freed row is reused only after the lag.
- Cost: CPU only, a 64 x 64 compare per build (under 1 us); no GPU cost; up to 64 inactive instances in the groom TLAS.
- Live (staged, not authorized): hl1-s8-late-arrival sets the key and requires `holds another key than before 0` in
  every first-seen line of the "lower" window (expect_every). Predicted: 0 moved rows; if the order was the whole
  cause, 0 frame watchdog trips and 0 "rejected after the hit" dropouts after the zoom starts. If holes remain with 0
  moved rows, the order was not the cause (candidate D) and the next observable is the per-groom points check.
- User check: Advanced, "Strand hair: stable strand rows" On, S8 with several characters in view, zoom with the wheel:
  no holes in the hair at the start of the zoom.

## 5x. Holes at the zoom start: pool reuse disproved (offline 2026-11-10, branch claude/hair-pool-reuse: diagnostic, no key)

Live hl1 180301 and 180949 (rt_hair_row_stable=1, VALID data, 180301 INVALID only for the runner expect below): moved rows
0 in every first-seen line, groom TLAS check 0, full BLAS builds 0 at the trip frames, yet the frame watchdog tripped at
G-buffer frames 2089, 2090, 2093-2095 (180301, down to 0 of 5715 strand px) and 2152, 2153, 2155-2158 (180949, 0 of
5495). So A (164930), C (164930) and B (5w) are not the cause. Dropouts at the first trip frame: every swapped key,
"rejected after the hit" (frame-wide elimination, 5q).

- Candidate D (a newcomer upload, or the free/eviction/growth making room for it, writes pool memory an existing groom
  still uses, or is unordered against its refit/trace) is DISPROVED:
  - Timing: the first trips come before any upload. 180301: trip frame 2089 at 18:04:39.76, first upload (16fa158c LOD
    0) staged in build 2093 at 18:04:40.020; 180949: 2152 at 18:11:18.75, upload in build 2156 at 18:11:18.987; 164930:
    2125 at 16:51:06.78, upload at 16:51:06.995. A copy recorded in build N cannot touch frame N - 4's G-buffer.
  - Control in the same run: 180301's warm-up (static camera, strands swapping in 420 of 460 G-buffer draws) staged five
    LOD 0 uploads (53 MB, frames 1553-1560) and five more LODs (15 MB) with 0 dropouts and 0 watchdog trips.
  - Code: no eviction below the groom budget (hair_groom_budget.h plan_evictions; used + need stayed under 512 MB);
    freed ranges (kGroomFreeLag, bvhsplit.cpp:355) return to the allocator only 6 frames on, with the GPU 2 frames past
    them and older than every live scene slot (bvh_on_present, as_chunk_alloc.h retired); uploads copy into such
    ranges between barriers (bvhsplit.cpp:3736 and 3767) and a LOD is used only once its recording completed
    (groom_lod, readback_state). The pool grew 30-40 s before the holes and never in a hole window; no repack.
- What every hole run shares instead (125217, 164930, 180301, 180949): the trips start in the frame the runner's wheel
  zoom starts (`[bg] dropped real input ... runner mouse passed 2` at frames 2167, 2124, 2088, 2151), and the card
  pixels of the heads double in 6 frames (a fast dolly). The three "arrivals" are characters entering the lowered view:
  a consequence of the zoom, not its cause. The hold runs of 161106 and 133243 had no swapping keys during a zoom. At
  that frame the build itself changes mode (bvhsplit.cpp:2626 `moving`: no dynamic holds :2813, no hold snapshots
  :3459, dynamic list in the game's draw order) and the strand passes trace a publish one frame and one camera step old
  (hp_rebase). Which stage rejects the hits under motion is not decided offline; hk1/hc1 turns showed the same class
  (rt_hair_header_cam_gpu did not remove it).
- Diagnostic (CPU only, no key, no shader, no ABI change, variants delta 0): `hair_primary::CameraSteps`
  (common/hair_primary.h) keeps 32 build frames' cameras; the frame watchdog line ends `build camera step X m at that
  frame, Y m the frame before (-1: not kept)`. CTest hair_groom_asset_test test_camera_steps.
- hl1 regenerated: expect `[wheel-input] phase=2` (warm-off had moved "lower" to index 2; phase=1 made 180301
  INVALID), now derived from the plan; expect_every: every watchdog line in lower/watch carries the camera step
  (min 0). predicted.json: candidate_D_pool_reuse (disproved) and candidate_E_zoom. Lint: 0 failing (live and frozen).
- Predicted for the next hl1: trip lines in "lower" with a camera step >= 0.05 m and none in the static pl-* windows.
  A trip with a step under 0.01 m puts the arrival back as the suspect. Next after that: the rejecting stage (owner,
  card band or tips) under motion, then the fix there.

## 5y. Strand motion vectors (offline 2026-11-10, branch claude/hair-mv: key rt_hair_mv_card, off)
User: strand hair at isometric distance trails / blurs when characters or the camera move (as before).
- Measured on the hs1-s8-static-hair bursts (static camera, idle party; `tools/hair_groom/mv_ratio.py`): 73-86% of
  class-5 pixels carry the camera-only motion vector (within 0.02 px of the static background) with strands, 5-7% with
  cards; hair / 6 px ring median |mv| 0.05-0.23 with strands, 0.52-1.08 with cards. Per head: strands 0-12% of the
  pixels moved while the head next to them moved 0.1-0.7 px; cards 86-100%. RR fetches their history from where the
  hair was (a trail).
- The strand path never wrote these pixels. Its own result is never 1e-4 px: the motion of the axis point at the
  recorded parameter is off the pixel centre by up to the strand's radius even with no previous points (camera-only
  fallback) or previous = current points, while 95% of the strand pixels read 1e-4-3e-4 px, the game's full-screen camera
  pass (background 1e-4). So the previous-points fallback (row.w 0) is not the cause; the velocity fragment
  discards or fails its test.
- Every way a strand pixel keeps the camera value (`shaders/rt/hair_primary_velocity.hlsl`): header tag 0; no grooms;
  no build camera (hp_rebase); pixel record not sealed with the header tag (e.w) or no strand; groom row k out of range;
  accepted marker (word +4) not this tag or its slot not the key-map slot of k; hp_segment out of bounds; the ownership
  band (hp_on_card, 10 mm + half a segment: generous); tips (flag off in the user set); the clone's GREATER_OR_EQUAL
  test of the exported strand depth against the stored one (equal by construction on D32S8, unless the depth image
  changed between the G-buffer and 6b); the velocity draw not swapped (only FS b70cadc5 is replaced; the stats count
  650-720 of 780 draws, and an unswapped card fails its native EQUAL test on the strand depth). The velocity reads the
  G-buffer's record (no re-trace, no jitter or 1047 difference) and runs on the same card triangles, so coverage and
  sub-pixel misses cannot explain whole heads at 0-12%. Which check it is cannot be read off the bursts.
- Built (off by default): `rt_hair_mv_card` (dev key, pinned 0 in data/release.ini). Specialization 1049
  (`kSpecIdHairMvCard`) compiled into the velocity variant when the key is non-zero at pipeline creation; header flags
  switch it live: 1 (kFlagMvCardFallback) the card's own skinned motion (b70cadc5 from location 1) where the row has no
  valid previous points; 2 (kFlagMvCardAll) the card's motion for every fragment of a swapped draw before any strand check,
  with this frame's sealed strand depth (else the card's). Strand points are barycentric on their card triangles.
  Mode 2 bypasses every discard; if it still leaves camera-only pixels, the depth test or the swap is the cause.
- Counter line (detailed logging): `[hair groom] strand motion (last N frames): keyed groom rows R, with previous points
  P, without W (first update, two updates in one build frame, skipped build frames); publishes, without the previous
  camera (previous publish not one frame back, GPU slot); rt_hair_mv_card` (`hair_primary::prev_row_state`, unit test in
  hair_groom_asset_test).
- `pan_mvec=1` (with pan_edge=1, pan_inputs=1): K8 edge bursts also keep RR's motion vectors (half-size, every frame;
  common/pan_epoch.h EdgePanSamples, pan_epoch_test): about 0.1 GB per burst instead of 4.9 GB.
- Key off: 225/227 requests byte-identical to main b29a9d3b; rt/hair_primary_velocity.hlsl (file and manifest entry)
  equivalent with 1049 frozen to 0 (tools/release_shader_equivalence.py); variants delta 0; spirv-val passes.
- Live (staged, not queued): tools/live_batches_staged/hair_mv, hm1-s8-hair-mv: the counter line at 0/1/2 and four K8
  edge bursts (strands at 0, 1, 2; cards) in one S8 run, about 0.4 GB; predictions in predicted.json.
- Live hm1 201105 (INVALID for the bursts): the counter line shows every keyed row with previous points (780 of 780 per
  60 frames, every publish with the previous camera) at the idle S8 camera, so missing previous points are disproved and
  mode 1 (kFlagMvCardFallback) changes nothing; mode 2 is the test. All four edge bursts were refused ("edge capture
  REFUSED") because with pan_inputs=1 the display-size FG inputs (fg_hudless, fg_ui, fg_final, 3840 px) met the
  2560 px extent budget before the edge name filter. Fixed on claude/hair-mv-burst: images an edge burst does not keep
  (`EdgePanSamples::kept`) are skipped before the budget (layer/dump.cpp pan_capture, pan_epoch_test); hm1 unchanged.
- User check (once qualified): S8 or the camp with the party walking; the hair should move with the heads instead of
  leaving a smear behind them. Not in the launcher yet.
## 5z. Holes under a fast camera step: where a camera mismatch could hide (offline 2026-11-10, branch claude/hair-cam-step: diagnostic, no key)

Live hl1 200821 (VALID) confirmed 5x's prediction: 6 frame watchdog trips at G-buffer frames 2064, 2065, 2067-2070, each
with a build camera step of 0.34-1.81 m (1.813 m at 2068: a 130 ms frame while 16fa158c decoded on the render thread;
the wheel dolly runs at about 15 m/s), none in the static windows. Frame 2066 (step about 0.4 m) did not trip.

- Read for a build-camera / render-camera mismatch (one frame of a 0.5 m step moves every strand 0.5 m):
  - Groom points: rt/hair_groom_update.hlsl reads this build's soup, which rt/xfb_to_world.hlsl converted with the
    capture UBO's VP (+128): camera-relative to that VP's origin. Groom TLAS instances are identity (bvhsplit.cpp
    groom TLAS build). The header camera is the same UBO's +208 (rtscene.cpp rt_after_capture), re-read at the build's
    submit with 0 differences (item 11). So points and header camera describe one frame.
  - G-buffer: the header is decided at vkQueueSubmit (hair_primary.cpp hair_primary_submit_begin); in the trip period
    58 of 61 G-buffers applied this frame's publish (org = 0), TLAS check 0, header/BLAS mismatch 0, build after the
    G-buffer 0. Card point, ray direction, hit depth and the card band (hp_on_card) all use the G-buffer's own gCam.vp.
    rt_hair_header_cam_gpu (GPU copy of the camera) did not change the turn trips in hc1.
  - Moving-camera mode (bvhsplit.cpp:2626 `moving`): no holds of absent draws (:2813), no hold snapshots (:3459), the
    dynamic list in draw order (rows fixed by 5w). A held hair draw never updates its groom (soup_update_ok), so an
    absent draw loses its own key only; the trips are frame-wide.
  - Left unproven: the rays start at the VP origin (hp_rebase org; dir from inv(gCam.vp) of the near-plane point), which
    is right only while the eye sits at that origin. If BG3's camera-relative origin lags the eye in motion, every ray
    starts one step off. Also possible: a G-buffer that applied an older publish in exactly the trip frames.
- Diagnostic (CPU only, no shader, no ABI, variants delta 0): `hair_primary::vp_eye` (the projection centre of the
  build UBO's VP, kept per build frame in CameraSteps) and GbFrame::pub_frame (the publish applied at submit). The frame
  watchdog line now ends `applied the publish of build frame B (k frames before), header camera X m from build frame F's;
  VP eye E1 m from the origin at that publish, E2 m at build frame F`. CTest hair_groom_asset_test test_vp_eye.
- hl1 regenerated (every trip line must carry these fields; predicted.json camera_mismatch_5z): eye about one step at
  the trips and 0 when still -> the fix is a ray from the near-plane point along the pixel line (shader, spec constant);
  eye 0 and k > 0 -> a stale header under motion (submit pick); eye 0, k 0 -> no camera mismatch in the header, next the
  per-reason reject counters (owner, card band, tips) of the strand G-buffer.
- hl1 wheel expect: 200821 was VALID (results.md shows no INVALID for it; its job had `phase=2`). The `phase=1` misses are
  180301 and 180949, which ran the job from before 5x.

## 6. Open items
1. Resolved: the native G-buffer FS (captures/alpha_cache/42a95e17ec3adc51.glsl) is ported; its tangent is
   built from the interpolated frame, which is world space (VS 41a168e2 locations 3-5; only location 1, the RT0
   normal, is view space; 5g). Open: the root-to-tip sign of that frame.
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
  **3b-2c (2026-11-08, branch claude/hair-fibre-fix, offline):** review fixes. (1) k scales only the backward
  multiple scattering (`dual.single_part`: f_s, or T_f f_s behind n fibres, is never scaled; before, a fibre behind
  one strand got k 0.7 a_f = up to 2x the f_s of a directly lit one). (2) k is fitted on the path-traced diffuse
  slab (`dual.make_table`, targets = make_volume_table's walks): the normal-incidence fit reflected 1.3-2.2x the
  path-traced diffuse energy (white fibres 1.2-1.6 > 1) and zig-zagged in colour (heavy-tailed walk weights); now the
  diffuse slab equals the walks within 0.03% at every grid entry, CHAR_Hair (held out) 1.001/1.002/0.998, k monotonic
  in roughness; normal incidence reads 0.76/0.83/0.91 of its reference (one k cannot match both incidences;
  a k(mu_i) table would need the volume normal). (3) hair_fibre replays D-LS's own ray (origin, soft sample, extent)
  and strand decision (rt_groom_blocks' draw with the groom's a_f) instead of re-deriving them, and counts n on it.
  (4) Every replaced term and the fibre response take the buffer's AO-on-direct factor (the game's, or D-LS's
  ao_direct give-back for traced lights and the sun). (5) Single scattering to the specular buffer (the game's
  R/TT/TRT are there; RR demodulates diffuse by the albedo), multiple scattering to diffuse. rt_hair_fibre=2
  (constant 1046): the game's terms at the fibre's transmittance model on the D-LS-traced lights; strand pixels must
  equal key off (exact without rt_hair_groom_shadow_t, in the mean with it). Known: behind n >= 1 strands Zinke's
  pi d_b T_f f_back can exceed the direct d_b f_back (integrated over directions, c >= 0.35), and T_f f_s uses f_s
  at the light direction, not the paper's azimuthally averaged f_s^scatter.
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

## 8. Closure plan (2026-11-03, user: "close the implementation properly")
Order = what blocks shipping first, then visible quality per effort. Every step behind its key, off by default,
key-off identity, then the smoke_ultra job with the other qualified keys on. Gates are named per step.
| # | Step | Offline / live | Gate |
|---|---|---|---|
| C1 | Stability: hv4 (GPU-AV) + hv5 reload bisect + hair_crowd, all with df2b9567 | live (user batch) | no device loss, no GPU-AV error, frames in every survival window; GPU ms per pass logged |
| C2 | Strand shadows with transmittance: a shadow ray crossing n fibres passes with a_f^n (forward-scattered fraction of the fibre, tools/hair_groom/dual.py), stochastic per crossing on the existing binary visibility (unbiased mean) | offline, then live | PT reference vs shader on the bench fixture (mean visibility within noise); live: skin/neck under hair not black, energy key-on vs key-off on a static camera |
| C3 | Sub-pixel coverage and tips: dithered tip opacity from the root channel (the game's own dithered alpha test), DLSS RR hair guides (RR hair resolve candidate) | offline, then live | edge-flicker detector on hair pixels lower than cards in one run; no energy change |
| C4 | Indirect light on strands: GI irradiance on strand pixels through the dual-scattering multiple-scattering term instead of the Lambert response | offline, then live | furnace test (white fibre, uniform sky: albedo within 3%); live A/B energy |
| C5 | Style coverage: card reference averaged over the game's dither phases (coverage/radiance gates then meaningful), 2-D curve tracer for curly atlases (HAIR_Afro_B), LOD1 from the LOD0 groom | offline | rerun: pass count up with unchanged gates; 368 reachable left today |
| C6 | Every view: portraits and small passes, instanced hair (0ca5ee93), reflections/water secondary hits shade strands with the fibre BSDF | offline, then live | per-pass strand participation lines; no card-only pass left in the smoke run |
| C7 | Qualify: Ultra preset entry, GUI, cost table, user check notes (what to look at, which save) | live | smoke_ultra clean; cost within the user's budget |
Strand dynamics of their own (collision, wind per strand) stay out: strands follow the game's simulated cards.

### C2 specialization default (2026-11-04)
`rt_hair_groom_shadow_t` is specialization 1026 (`kSpecIdHairGroomShadowT`), default 0.
At 0, rt_visibility uses the original rt_groom_trace first-hit expression. At 1 it uses
rt_groom_blocks and the shared hp_crossing deduplication used by the GPU bench.
GI and native water pipelines carry the value in their specialization cache key; toggles
require driver pipeline creation only. No shader request or ABI change. The former
RT_GROOM_SHADOW_T_REFERENCE text copy is removed. tools/hair_shadow_check.py compares
ordinary requests against main, accepting raw differences only through the shared strict
canonical equivalence with 1026 fixed to zero and every other surviving specialization variable.
The opt-in primary count/G-buffer and fibre programs have separate GPU validation.
Live quality and GPU cost remain unmeasured. Check skin and neck under hair in Test Dialogue1,
then Campsite, with the prepared hair_l2 same-run pairs; no live batch is authorized here.
Offline gates: 113/255 ordinary requests byte-identical to main 9cb1e3a; 142 strict equivalent
at default, zero failures. Request delta 0. Unchanged water_surface_shader_test passes against
009f60f: 16/16 OFF accepted, 16/16 production validated. Full build: 147/147 CTests passed.
GPU shadow bench: 3000/3000 crossing counts match CPU; 174623 transmitted trials vs 175064.4
expected, sigma 353.2 (1.25 sigma error). These are offline gates; live GPU cost remains pending.

### M2.3 AO transmittance (2026-11-07, branch claude/hair-m2-m4-light)
`rt_hair_groom_ao_t` is specialization 1044 (`kSpecIdHairGroomAoT`, knob `exp_hair_groom_ao_t`), default 0.
Before: every AO/GI hemisphere ray that sees the groom TLAS stopped at the closest strand (opaque): the GI trace
(`gi_trace_rt*`, rt_closest / rt_closest_hit), AO at RT hits (`native_hit_ao`, `native_hit_ao_gi`). The game's
SSAO sees the card depth (unchanged). At 1, those rays (marked by `g_rt_groom_ao` around the call) keep the
closest strand hit with probability 1 - d_f lum(prod a_f) over the crossings in front of the scene hit
(`rt_groom_blocks`, the C2 estimator and hp_crossing dedup), else pass on to the scene hit or the miss. A
blocked ray keeps the closest strand's distance for the fade and its screen radiance for the bounce. The layer
packs a_f into groom row word 2 when either key is on (`hair_primary_transmit`; layer change: restart).
GI pipelines carry 1044 in their specialization key; no request or ABI change. Identity (tools/hair_shadow_check.py
--allow-new gi/hair_fibre_spec.hlsl, vs main 2dcf0a88): 160/210 ordinary requests byte-identical, 50 strictly equivalent
with 1026/1044 fixed at 0, 0 failed; hair_fibre.hlsl requests (strand-pixel header moved) 3/3 byte-identical.
GPU bench (tests/hair_primary_gpu_test.py, 3000 rays): crossings 3000/3000 equal to CPU; closest strand t
against the CPU capsule entry max 9.8e-8 m; mean occlusion with a_f = 0.5 1767.65 against 1765.57 expected,
sigma 1.04 (2.0 sigma; the same trials as the shadow line, 174332 lit vs 175064.4, -2.1 sigma). Opaque
strands would give 2278.5: the transmittance removes 22% of the strands' AO on this fixture.
For the user's colours (dual.averages_table at beta 0.41): one crossing passes 41% (CHAR_Hair), 56% (auburn),
58% (blond); four crossings 9% / 29% / 32%.


## 9. Known failure modes and offline proofs

Closed means the named offline regression or reviewer disposition passes, not live qualification.

Not hair: hgn1/hgn2 empty generated-material runs and the hgn share-window row are owned by [TRACK_D_REFLECTIONS](TRACK_D_REFLECTIONS.md) (generated-material publication/producer periods).

| Live run(s) | Cause | Fix commit | Offline proof | Status |
|---|---|---|---|---|
| hv8 091526; earlier hc1/hv5/hv6/hv7 same fault class | Demoted lanes continued past discard into invalid reads | base, hair-demote-fix | hair_groom_test.test_strand_source_safety: every G-buffer/velocity discard immediately returns; hair_primary_gpu_test | closed |
| hv6 045450; hc1 050004, hc2 050124, hv5 045720 | Recursive scene mutex, exit 0xFFFF7001 | base | hair_groom_test.test_publish_lock requires non-locking rt_this_build_camera | closed |
| hv6 through 071137 | Vertically mirrored primary rays | base | hair_groom_test.test_ndc_ray: 50 rotated/projected points round-trip | closed |
| hc2 002952; hc1/hc2 later silent swaps | Adjacent publish tags and camera-relative origin | base (f659b5ee order design) | hair_groom_asset_test pick_publish and camera rebase fixture | closed |
| hv7 094814, 095747 | Count requested newest tag while entries were tag - 2 | base, hair-count-tags | hair_groom_asset_test newest_counted_tag and four-tag marker fixture | closed |
| hv6/hv7 102509 | Different-latency card/strand samples never paired | base, superseded by hair-coverage-accept | hair_groom_asset_test packed card/accepted pair, same tag and slot | closed |
| hv7 092619 (ratio 2.55) | Stacked card/strand occlusion samples | base, hair-coverage-accept | hair_groom_asset_test duplicate fragments and overlapping-key marker fixture | closed |
| hv7 112828 | Rejected pixels counted as accepted strands | base, hair-coverage-accept | hair_groom_asset_test rejection/last-owner/stale-acceptance marker fixture | closed |
| hv7 112828 | Reused applied tag attributed to multiple G-buffer frames | base, hair-coverage-accept | hair_groom_asset_test unique_frame ambiguity and invalidation regression | closed |
| hv7 123740/145421/160354; hv9 180225 | Epoch expiry counted absent/late draws as orphans | working tree | hair_groom_asset_test both recording orders, interleaving, hitch, hv9 180/180/180 fixture | closed |
| hv7/hv9 | Transparent native card pixels inflated denominator | base, hair-orphan-alpha | hair_groom_test.test_strand_source_safety alpha/grazing marker guard; test_tips_dither zero alpha | closed |
| hv7 092619 | Prepass required the wrong present/frame equality (0 prepass vs 187 G-buffer draws) | base, extracted helper | hair_groom_asset_test::test_prepass_drop_decision exercises prepass_drop_allowed called by the layer: applied in [now, now + 1], both recording orders, off-by-one, live key and latch/sibling safety | closed |
| hv9; uncovered sibling risk | Native sibling with neither variant nor fallback | working tree | hair_groom_asset_test uncovered sibling retained 300 frames; true orphan counted once | closed |
| hv9 180225 | Orphan fail-fast aborted before strand PNG | working tree | live_guard_test.test_hv9_orphans_preserve_capture_but_fail_final_verdict; final gate remains mandatory | closed |
| hv9 171448 | Timed return/restore carried invalid frames=0 | base; new regression | hair_validation_test.test_hv9_frame_window_schema; camera_plan_test rejects frames=0 | closed |
| hc2 002952 | G-buffer timestamps depended on publish recording order | base timing_before_gbuffer | Diagnostic; covered by the presence gate `strand pass GPU ms` in hv9 expect_every (c-strands-plus). Regex requires a numeric ms line and nonzero sample count; no timestamp-value threshold | closed |
| hc1 212704; hv5/hv6 readiness timeouts | Pending pipelines and cached readiness acknowledgements | base | camera_plan_test; live_guard_test readiness cutoff; batch_jobs_lint_test frozen preflight | closed |
| hv6 030524..071137 | K8 armed before readiness / missing consecutive frames | base | camera_plan_test readiness before capture; live_guard_test.test_burst_gate_keeps_motion_ready_cutoff | closed |
| hc1/hv5/hv6 05:49..05:54 | Stale rt/hair_primary_probe.hlsl prefetch FAILED | base stale manifest drop; working tree test | hair_prefetch_test checks layer shader_get producers (source of rolling requests.txt), variants.txt membership, source existence and stale-manifest drop path | closed |
| hc2 052540 | Crowd producer demanded outside its producing scene/state | base job changes | Not in the candidate job (hv9 is the only job); hair_crowd re-qualifies it | closed for candidate |
| hc1/hv5 early losses | First-load/reload handle reuse and held soup offsets | base (51956f86 guards) | hair_groom_asset_test LoadGuard first image/reload, soup_update_ok bounds/held draw | closed |
| hv6 072839; hv7 fault attribution | Fault inventory omitted cascade/groom BLAS; TLAS label ambiguous | base, hair-blas-lifetime/hair-tlas-label | Diagnostic names only; covered by the presence gate `strand pass GPU ms` for candidate diagnostics. No image or numeric gate consumes BLAS/TLAS names; this closure does not qualify GPU lifetime safety | closed |
| hv7 112828; close-up low ratios | Coverage bounds exit / delayed old success | base, hair-coverage-accept | hair_groom_asset_test lower/upper bounds and older-success invalidation | closed |
| hv9 retrospective assertion | Aggregates cannot exclude extra native sibling draws | working tree | Cannot be proved from aggregates; closed by construction: exact draw-time layer depth_orphan counter uses DepthLatch::orphan; hair_groom_asset_test frame-latch recording-order/interleaving/hitch/uncovered-sibling/true-orphan fixtures and hv9 180/180/180 fixture; live_guard_test.test_hv9_orphans_preserve_capture_but_fail_final_verdict | closed (by construction) |
| hv9 completion, c-strands-plus, K8 | Six positive patterns lacked historical matches | working tree | hair_patterns_test: check_patterns.source_contract renders actual auto-run.ps1 and dump.cpp emitter strings with sample values; all six regexes match, including K8 select + match predicates | closed |
| F01 review | uint32 soup extent multiplication wrapped (tri 5592405, stride 256: need 280) | working tree | hair_groom_update.hlsl:35-42 checks base, available >= 24, then divides before multiplying; hair_groom_test.test_groom_soup_bounds compares every read byte using Python integers, including exact/short buffers, strides 24/256, last triangle and base past end. Hair-only: layer/bvhsplit.cpp:2393 gates groom_work on rt_hair_groom (set at :1954); dispatch :3092 | closed offline |
| F08 review | Card-depth unprojection can round the same pixel ray differently per layer | working tree | hair_primary_gbuffer.hlsl:101-102 now uses fixed clip depth 1; origin is hp_rebase(rec, gCam.position) (:128), independent of layer. Hit k/seg, v and depth (:147-165) use only that ray, the shared TLAS/points and camera. Card ownership/material/tips are applied after the entry is sealed/stored (:167-168). All readers check hp_seal: G-buffer :142, velocity :78, count :42-58. hair_groom_test.test_pixel_seal_tearing: 100000 identical pairs pass; all 14 word mixes of different entries, 1400000 trials, 0 passes. The 31-bit seal is a probabilistic corruption check, not a collision-free proof | closed offline |
| F09 review | Many fragments store one groom colour sample | unchanged | accepted: any one or a mix of this groom's samples of this frame; the a_f estimate is per groom. G-buffer hair_primary_gbuffer.hlsl:254-255 writes all four colour/roughness words at offset + k * 32 before its tag; count hair_primary_count.hlsl:75-79 requires that tag and finite components. Different grooms have disjoint slots. Count runs after all fragment writes: any current-frame writer overwrites all components, excluding old-frame components from a mix; fragment-to-compute and compute-to-fragment barriers in layer/hair_primary.cpp:315-318,354-358 separate frame writers from count readers. Header replacement waits for fragments (:409-430); pack ends before colour slots (common/hair_primary.h:53,83,158), so remapping cannot mix concurrently writing grooms | closed (accepted limitation) |
| Review | Accepted marker precedes late depth test, as do card markers; both measure depth-free raster, matching the offline bound | unchanged | paired marker fixture | closed (accepted limitation) |
| Review | Overlapping draw keys use last-writer marker attribution | unchanged | duplicate/overlapping-key marker fixture | closed (accepted limitation) |
| Review | Epoch resolution finding is superseded by the frame-based latch | working tree | frame latch ordering fixtures | closed (accepted limitation) |
| Review 1 P0 LOD upload | Stage only; use after copy recording submitted and frame done; discarded upload retries | working tree | hair_groom_asset_test::test_upload_submission | closed offline |
| Review 2 P0 compaction | Separate query_rec; discarded query resets full build and query | working tree | hair_groom_asset_test::test_compaction_submission | closed offline |
| Review 3 P1 UV joins | Track copy recording; discard job without matching stale samples | working tree | hair_groom_asset_test::test_uv_submission | closed offline |
| Review 4 P2 counts | Store count recording; discard slot without coverage evidence | working tree | hair_groom_asset_test::test_count_submission | closed offline |
| Review 5 P1 depth | G-buffer requires this key's prepass drop in [p, p + 2]; otherwise cards/probe | working tree | hair_groom_asset_test::test_gbuffer_depth_latch | closed offline |
| Review 6 P1 velocity | Accepted marker word 1 must match frame tag and groom key-map slot; no new specialization | working tree | hair_groom_test.test_strand_source_safety; accepted marker fixture | closed offline |
| Review 7 P2 wrapped tag | Count skips low-24-bit zero tags, including untouched zero markers | working tree | hair_groom_test.test_strand_source_safety; hair_groom_asset_test four-tag marker fixture | closed offline |
| Review 8 P2 totals | Log card totals and accepted totals separately | working tree | hair_groom_asset_test::test_card_accepted_totals | closed offline |
| Review 9 P2 refusals | Separate feedback failures; reset both counters each period | working tree | hair_groom_asset_test::test_period_failures | closed offline |
| Review 2026-11-08 #1 | Record-time header over in-place refit BLASes/points | hair-layer-fix | hair_groom_asset_test::test_header_version (build before G-buffer, shrinking groom set: ordered_publish, upgrade_plan card slots) | closed offline |
| Review 2026-11-08 #2 | Card-kept keyed grooms hide swapped strands | hair-layer-fix | hair_groom_test.test_strand_source_safety (swap-mask skip, private probe trace, marker flag) | closed offline |
| Review 2026-11-08 #3/#4 | Count colour/traced only for its own tag | hair-layer-fix | four-window count layout asserts; test_frame_tags | closed offline |
| Review 2026-11-08 #13/#14/#17/#19 | Live-off latch, reused depth handle, zero segments, watchdog/orphan | hair-layer-fix | test_load_identity, test_zero_length_segment, test_watchdog | closed offline |

Earlier review receipt: 151/151 CTests passed; the prescribed G-buffer/velocity/count validation passed 3/3.
Closure receipt: 153/153 CTests passed (620.80 s), including hair_prefetch_test and hair_patterns_test. Hv9 lint: 1 job, 0 failing. Pattern audit: 101 checked, 0 missing positives, 0 forbidden hits.
No live qualification. All hair rows are closed; historical aggregate limits remain explicit. See the branch progress fragment for current offline validation.
