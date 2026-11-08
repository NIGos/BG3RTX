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
  the groom gate, pixel record and feedback reset and strand swaps pause 300 frames. bvhsplit drops the groom join
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
    10 rejected for the main view (one header per frame, applied in the first G-buffer command buffer). 11 not changed
    (CPU-read UBO camera vs GPU soup; no evidence of a mismatch).
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
