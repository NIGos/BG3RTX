# Track D: D-RF ray-traced reflections

Part of Track D (hardware ray tracing); index and old section map: [TRACK_D_RT.md](TRACK_D_RT.md).
Section 10 and the first section 13 (unknown queries, shared reflection origin) keep their original numbers.

## 10. D-RF: ray-traced reflections (implemented, tiers (a) and (c))

Status (2026-10-26): **implemented, on by default** (`rt_reflections=1`, live; needs `rt=1`
and `gi=1`). Code: `shaders/gi/rt_reflections.hlsl` (the pass), `shaders/rt/bg3_ambient.hlsli`
(the game's specular ambient, D-EN4), `core/gi.cpp` (bindings, pass, timing, statistics),
`layer/ngx.cpp` (the RR hit distance), `layer/cmd.cpp` (`query_set`, host ABI 5).
Measured in the Steel Watch Foundry (the auto-run save) and at the docks (end of the `-Move`
pan), RTX 5090, 2560x1067.

### 10.1 The game's specular ambient (D-EN4)

BG3 has no SSR: its only reflections are cube-map probes, applied inside the tiled lighting
`f469db78` (FRAME_MAP "Specular ambient"). The term in b11 is `S · k · SO`:
- `S` (`_1684`): the global probe b35 (and b37) at `R = reflect(−V, N)`, mip `a·8 + _m30 − 8`,
  times the split-sum env BRDF `E = F0·DFG.x + DFG.y` (LUT b38 at `(max(N·V, 1/512), a)`); then
  every local probe listed in the pixel's froxel (b23 `y >> 24`, records b33, cubes in the
  bindless array b45: box/sphere parallax, edge fade, its own roughness and DFG) lerped over it;
- `k = b13[0] · 100`;
- `SO`: Lagarde's specular occlusion from the AO target;
- then the aerial-perspective transmittance (1 indoors; not replicated).

**How the pass sees the probes.** The local probes need the bindless cube array, which
cannot be pushed (push descriptors are capped at 32 and D-LS already uses 30). The host now
exposes `query_set` (ABI 5): the descriptor set the lighting dispatch has bound at set 1, its
`VkDescriptorSetLayout` and its dynamic offsets (2 here). D-RF binds **that set itself** as its
set 2, through a pipeline layout built on the game's own set layout (rebuilt if the layout
changes; it never did). Every probe resource is the game's exact descriptor. The froxel walk
and the lighting constants come from the D-LS push bindings 21-29; the hit distance image is
push binding 30 (31 push descriptors).

**Validation** (`rf_validate=1`, `rf_stats=1`): the D-LS prep pass sets the AO to 1 (so
`SO = 1`) and the sun mask to 0 before the lighting, and D-LS is skipped. b11 then holds only
the local lights (recomputed with the game's own atlas shadows, as D-LS does), projector
lights and `S·k`. Over the pixels without projector lights:

| Foundry, gameplay (same result in two runs) | Value |
|---|---|
| pixels compared | 1.34M per frame |
| game specular (luminance) | 0.01813 |
| local lights + probe term recomputed | 0.01825 |
| **mean \|residual\|** | **0.00011 = 0.58% of the game's** |
| recompute above the game | 0.58% (almost all of the residual) |

The replication meets the < 1% gate of D-EN4. The remaining +0.7% is an excess of the
recompute; its cause (LUT/cube filtering, the transmittance) is not identified. The uber
permutation `34a007cc` handles shading models 2-7 (`RT2.w & 7`) differently (two-sided
foliage blends a back-face probe, model 7 takes F0 from RT3); D-RF only touches models 0 and 1
(`rf_models=3`): 99.8% of the foundry's pixels, 98.5% at the docks. (Validation numbers logged
on loading-screen frames are meaningless: that G-buffer is not the scene's.)

### 10.2 What the pass does

Full resolution, after the lighting and D-LS, **before** the GI composite (which stores this
frame's lit radiance, reflections included, as next frame's `prevRadiance`), SSS and fog.

1. The surface exactly as `f469db78` sets it up (shared with D-LS): `P`, `N` (not
   renormalised), `V`, F0, roughness `a` with the game's specular AA, the shading model.
2. `Tp = S·k·SO` (10.1) and the pixel's `E`.
3. One ray (`rt_closest`, alpha-tested TLAS) from `P + N·dist·rt_bias`, up to `rf_tmax` (100):
   - with RR (`rf_rr=-1` follows `rr`): GGX **bounded VNDF** (Eto & Tokuyoshi 2023), α = a²,
     white noise from `pcg3d(pixel, frame)` on its own stream; a mirror ray for
     `a < rf_mirror_rough` (0.1);
   - without RR: mirror rays only, up to `rf_max_rough_norr` (0.3). There is no denoiser for
     GGX noise there, so the fallback is deterministic and nothing of ours filters (R2).
4. Tiers:
   - **(a)** the hit projects on screen, the depth there is the hit itself (`rf_depth_k` × the derived depth_tol; was `rf_depth_tol`
     3%) and that surface faces the ray: `L = prev_radiance_at(hit pixel)` (last frame's lit
     diffuse + specular, reprojected with the MVs, QUALITY Q1), weight `w = 1`, faded within
     `rf_edge` of the screen border and between 0.75 and 1 × `rf_max_rough` (0.6);
   - **(c)** a miss, a hit hidden on screen or off screen, or a sample below the horizon:
     `w = 0`, the game's own probe term stays exactly as it was. A TLAS miss is never sky
     (QUALITY R3; roofs are cut away).
   - Tier (b), shading off-screen hits, needs the hit material (D-EN2) and is not done.
5. The exact delta `b11 += w·(L·E − Tp)`, clamped at 0 only against the transmittance it does
   not replicate. The game's `SO` is not applied to traced hits: the ray found the
   occluder and returns its radiance; applying `SO` as well would double-occlude. (`rf_so`:
   removed 2026-10-31, see CONSOLIDATION.md C1.)
6. RR's `DLSSD.SpecularHitDistance` (R16F): the hit distance for tier (a), 65504 elsewhere
   (the probe behaves as an environment at infinity). Non-finite guard on every write (R5).

Per pixel the estimate is stochastic between (a) and (c); in expectation it is
`P(a)·L·E + (1 − P(a))·Tp`, and RR denoises it (R2: no history or filter of ours).

### 10.3 RR guides: the specular hit distance

`layer/ngx.cpp` sets `DLSSD.SpecularHitDistance` (and its subrect base) on every RR evaluate
while the image is fresh (written this frame or the previous one) and at the render
resolution; otherwise the key is cleared, since the game reuses its parameter block. It goes
with the v2 world normals and camera matrices (TRACK_A "Guides v2"). The log line
`[ngx] RR gets the specular hit distance (D-RF)` confirms it, and RR reported no evaluate
error in any run. `rr_spec_hit_distance=0` withholds it (an A/B of its effect was not made).

### 10.4 Results

Foundry, one run, live toggle (`auto-run.ps1 -Traces 4` with `scripts/live_phases.py`,
`dump_png_width=3840`; `scripts/ab_compare.py`):

- [rf_foundry_off.png](img/rf_foundry_off.png) / [rf_foundry_on.png](img/rf_foundry_on.png) /
  [rf_foundry_diff.png](img/rf_foundry_diff.png) (×4): the presented frames, downscaled.
- [rf_foundry_crops.png](img/rf_foundry_crops.png): three 1:1 crops of the 3840x1600 output,
  off | on | diff ×4. Top: the metal pipes along the walkway. With the probe they and the wall
  behind them carry a uniform sheen; with D-RF they reflect the dark wall and walkway they
  face, and the sheen is gone. Middle and bottom: the chimney pipes and the curved beam,
  rough; almost no change (the moving NPC is most of the diff).
- [rf_foundry_probe_term.png](img/rf_foundry_probe_term.png) (`rf_debug=3`): the game's probe
  term as recomputed, i.e. everything D-RF can replace.
- [rf_foundry_tiers.png](img/rf_foundry_tiers.png) (`rf_debug=1`): green tier (a), red hidden,
  orange off screen, blue miss (the walkway's rays leave through the cut-away roof), grey not
  traced. [rf_docks_tiers.png](img/rf_docks_tiers.png): the same at the docks.

| Statistics (`rf_stats=1`, 120-frame windows) | Foundry | Docks (end of the `-Move` pan) |
|---|---|---|
| traced (models 0/1, `a` below the fade) | 54.1% of px (1.48M rays) | 23.3% (0.64M) |
| of the traced: tier (a) on-screen hit | 42.0% | 4.8% |
| hidden on screen / off screen / miss | 20.9 / 6.1 / 28.4% | 1.5 / 3.0 / 5.7% |
| below the horizon | 2.7% | 85.0% (G-buffer under the forward-rendered water) |
| local probes per pixel | 1.29 | 0.01 |
| mean luminance: game specular / probe term / probe term after D-RF | 0.01425 / 0.01093 / 0.01098 | 0.03700 / 0.01714 / 0.01717 |
| presented frame, mean luminance | −0.78%; 10.5% of pixels change by > 2/255 | water animation dominates the diff |
| non-finite values (every dump of every trace, all runs) | **0** | **0** |

The probe term is 77% of the foundry's specular, yet its mean moves by +0.5% only: the
foundry is mostly rough painted metal and stone, and the change is local (concave areas lose
the probe's sheen, faces opposite lit surfaces take their colour). The docks view the pan
ends at is mostly water, which is forward-rendered over the G-buffer; there D-RF changes only
the ship's hull and the quay. **No scene with strongly reflective G-buffer surfaces (wet stone,
polished floors) was reached by the automated runs**, so that A/B (R6) is still owed.

Moving camera (`-Move`, RR on): the presented frame during the pan
([rf_docks_moving.png](img/rf_docks_moving.png)) shows no noise, smearing or black holes on
the reflecting surfaces; 0 non-finite values. The dark dissolve pattern at its left edge looks
like the game's see-through cut-away of the wall in front of the camera, but no D-RF-off frame
of that exact view was captured to prove it. Not measured: a temporal-stability metric and
FLIP against a reference.

### 10.5 Cost (GPU timestamps around the pass, medians of 300 frames)

| Configuration | Foundry | Docks |
|---|---|---|
| **Default (RR, GGX VNDF, 1 ray per px below a = 0.6)** | **0.33 ms** (p10 0.31, p90 0.36) | 0.13 ms |
| `-Validation` | 0.31 ms | — |
| `rf_stats=1` (wave-reduced atomics; measurement only) | 4.1-4.4 ms | — |

`rr=0` (mirror rays up to a = 0.3, 2% of pixels traced) was only timed with `rf_stats=1` on,
so it has no clean number; the probe recompute runs on every pixel either way. The default is
within the 0.5-1.0 ms budget (QUALITY R7). Track D in the foundry: sun + RT GI 0.26-0.28 ms,
D-LS 0.35 ms, D-RF 0.33 ms, about 0.95 ms of the 2.5 ms.

### 10.6 Validation layer and robustness (R5, R8)

- `auto-run.ps1 -Validation`, `rt_reflections=0` against `1` with every mode toggled live in
  the lit foundry (`rf_stats`, `rf_validate` with debug 5, debug 1, 2, 3, 4, 6, `rr=0` (the
  mirror path), off, on): **identical message IDs**, all the game's own (`09600`, `10788`,
  `StorageImage-FormatMismatch`, `OutputNotWritten`, `CopyBuffer-00118`, `08737`, the headless
  `non-acquired-swapchain-image-used`). Binding the game's own descriptor set into our
  pipeline raised nothing.
- `fg=2` (`-RealPresent`, DLSS-G 2x) with D-RF on and RR getting the hit distance: DLSS-G
  status 0x0 on every logged frame (26 reports), 2 presents per rendered frame, 0 Streamline
  API errors; the pass costs the same (0.313 ms).
- Missing inputs (no TLAS or a stale one, a missing light-list binding, the lighting's set 1
  being a push set, `gi=0`): the pass does not run and logs why once; the game's probe term
  is untouched, and RR stops getting the hit distance after 2 frames.

### 10.7 What does not work / next steps

- **Tier (b)**, off-screen hits shaded with the hit material (D-EN2 + EN1 lights): 6% of the
  foundry's rays (and the 21% hidden on screen) fall back to the probe.
- **Off-screen and culled geometry** is missing from the TLAS (D-EN3), so hits behind the
  camera are probe-shaded, and misses through cut-away roofs keep the probe (by design).
- **A reflective-scene A/B** (wet stone, polished floors) and the temporal-stability metric.
- **Shading models 2-7** are left to the game (`rf_models`).
- **The transmittance** is not replicated (delta clamped at 0), as for D-LS.
- **`rr=0`**: mirror rays only, up to `a = 0.3`; NRD REBLUR is not integrated.
- **Bounce source.** `prevRadiance` holds view-dependent specular (QUALITY Q8). For reflections
  that is the usual SSR approximation (the hit's highlight as the camera sees it).
- `stats_begin` adds 3.8-4 ms when `rf_stats=1`, as for D-LS: measurement only.

Settings (`bg3rtx.ini`, all live): `rt_reflections`, `rf_max_rough`, `rf_max_rough_norr`,
`rf_mirror_rough`, `rf_rr`, `rf_tmax`, `rf_depth_k` (was `rf_depth_tol`), `rf_edge`, `rf_strength`,
`rf_models`, `rf_debug` (1 tiers, 2 change ×4, 3 probe term, 4 probe term after D-RF,
5 validation residual, 6 hit distance), `rf_stats`, `rf_validate`, `rr_spec_hit_distance`.
The log reports `rf: first frame ...` (the lighting's set and its layout),
`rf: reflection pass GPU median ...`, `[ngx] RR gets the specular hit distance` and, with
`rf_stats=1`, `rf stats` and `rf validation`.

Tooling added with D-RF: `auto-run.ps1 -MoveSec N` (release the pan key after N s), and the
`trace.request` file trigger now works (its path had a `\t`, a tab, so it never fired).

## 13. Unknown ray queries, shared reflection origin, QA local-light knob (2026-10-27)

**Candidate cap = unknown, never lit.** A ray query that passes `kMaxCandidates` (4096)
alpha-tested candidates is aborted (counted in guard 121, as before). Its answer is now
*unknown*: `rt_visibility()` (rt_common.hlsli) returns `kRtLit` / `kRtOccluded` /
`kRtUnknown` (a hit committed before the abort is still `kRtOccluded`); `rt_occluded()` keeps
its signature and returns `rt_visibility() != kRtLit`, so unknown counts as occluded wherever a
caller has nothing better. `RtHit.aborted`, `RtDebugHit.aborted` and
`rt_closest(o, d, tmax, cone, out bool aborted)` say the closest hit is unknown (its fields are
then unreliable). Invalid rays and an unbuilt scene are unchanged (`kRtLit`, guards 120 / scene
stamp). Per caller:

| pass | on unknown |
|---|---|
| sun (rt_shadow.hlsl) | the game's mask value for the pixel (one ray per pixel: per ray = per pixel); `rtVis.a = 8/255` marks it, so the fold and rt_sun_resolve keep the mask exactly; `rt_debug=6` shows it yellow |
| D-LS (rt_local_shadows*.hlsl, ls_ref) | `ls_debug=10` yellow; `t = s` for that pixel and light: the atlas PCF for a light with a game shadow map, 1 for one without (the game never shadows those: its own visibility is unshadowed, this is the game's term, not "lit by default"). Same in every path (default, LS_FULL, RIS, exact-k) and in the frozen reference (hand edit), so ls_check stays comparable |
| D-RF (rt_reflections*.hlsl) | tier 6: w = 0, the game's probe stays, SHaRC is not asked; `rf_debug=1` yellow |
| RT AO (rt_ao.hlsl) | per ray, an occluder at distance 0 (weight 1): conservative; an abort means > cap alpha candidates within `rt_ao_radius`, i.e. dense foliage |
| GI (gi_trace_rt*.hlsl) | not changed here (separate change); counted as "GI" if it sets `g_rt_pass = kRtPassGI`, else "other" |

Unknown queries are counted per pass (rtStats 128..133, always on, like the guards) and logged
in the `[core] guard:` lines: `... unknown (candidate cap) queries ...: other N, sun N, D-LS N,
D-RF N, AO N, GI N`. **QA knob** `rt_max_candidates` (live, Push `rt_max_cand`, 0 = 4096, never
above): 1-4 forces aborts wherever a ray crosses more alpha-tested candidates (foliage).

**Reflections use the shared origin.** rt_reflections.hlsl starts its ray at
`rt_ray_origin(fp, P, Nn, foot, dir)` (rt_origin=1: TLAS surface point, geometric normal,
Wachter-Binder offset to the side `dir` leaves on; rt_origin=0: the old rt_bias offset) instead
of `P + Nn * dist * rt_bias`. The D-RF push set now also binds rtOrigin (40): 32 push
descriptors, the RTX 5090's limit (core/gi.cpp kExtras).

**QA ambient floor, `qa_zero` bit 1** (rt_local_shadows*.hlsl, ls_ref): every point/spot light
the tiled lighting evaluated at the pixel gets t = 0 and its unshadowed transmission ct is
removed too, so D-LS subtracts exactly what the game added for it (no rays; exact-k / RIS
bypassed). It includes the lights D-LS normally leaves alone (ls_shadowed=0, below
ls_min_radius, ls_owners / ls_noshadow). It cannot remove: shadow-mapped lights on hair pixels
(the game's hair self-shadow is not modelled), projector lights, anything outside the froxel
light list (forward draws, fog in-scatter, emissives), and nothing when D-LS does not run
(rt_local_shadows=0, no TLAS, sun-set ring full) or in ls_debug=4-7. No cleaner single point:
the light buffer is the game's (read-only to us), and the prep pass runs without the light list.

**To verify in game** (S3 Campsite, S8, S14 Lower City; `-Background`):
1. Baseline: `rt_max_candidates=0`; the `[core] guard:` heartbeat must show unknown 0 everywhere
   (or the real count: any non-zero here is a scene that needed the fallback).
2. `rt_max_candidates=1`, `rt_debug=6`: yellow pixels in foliage = aborted sun rays; with
   `rt_debug=2` their blue (written) must equal red (game mask). `rf_debug=1`: yellow
   reflection pixels keep the probe (`rf_debug=2` black there). `ls_debug=10`: yellow
   pixels had a D-LS ray aborted (that light kept the game's term; `ls_debug=2` removes
   nothing there that the game's own shadow did not). The
   300-frame `[core] guard:` line must list non-zero sun / D-LS / D-RF / AO counts.
   No pixel may get brighter than with `rt_max_candidates=0` where the game's term is dark.
3. `qa_zero=2`: the local lights vanish (torches, braziers in S3; street lamps in S14); what
   stays lit is sun, ambient, projector lights, fog and emissives. `ls_debug=1` counts the
   lights per pixel for comparison. `ls_check=1` with `qa_zero=2`: rtStats 81 (differing px)
   must stay at its usual floor.

## 14. Tier (b): `rf_hit_shade` (M4.2, 2026-10-31; offline done, READY_FOR_LIVE, not qualified)

Key `rf_hit_shade=0` (live; GUI Experimental "Shaded hidden-surface reflections",
`exp_rf_hit_shade`, needs `exp_hit_material` = startup `rt_hit_material=1`). Plan:
PLAN_M4_REFLECTIONS_GI.md M4.2.

- **What.** A hidden (tier 2) or off-screen (tier 3) hit inside the frustum on a qualified native
  material takes L_b instead of the probe: `native_hit_direct` (point/spot, RT-shadowed) + the
  sun (replicated `bg3_sun_colour()` x clouds x b13[0], one RT visibility ray, same FresnelBlend
  BSDF as the point/spot term) + the game's ambient at the hit with AO 1 (sky irradiance
  b34/b36 x diffuse colour x DFG.z, and `bg3_spec_ambient` at the hit with its local probes)
  x b13[0]*100. w = rough_w x rf_strength (no edge fade). Unknown material, projector light in
  the hit's froxel, hit outside the frustum, unknown visibility, backface: the probe stays.
- **Code.** `shaders/rt/native_hit_shade.hlsli` (`native_hit_lit` = gi_hit_direct's
  `rt_hit_material` + `native_hit_direct`; gi_trace_rt.hlsl keeps its inline calls so its SPIR-V
  stays byte-identical while M4.1 qualifies it, M4.3 moves it onto the include),
  `gi/rt_reflections_hit_shade.hlsl` and `gi/rt_reflections_sharc_hit_shade.hlsl` (define
  `RF_HIT_SHADE`; with SHaRC a hit the material cannot shade still asks the cache),
  `core/gi.cpp` `rf_hit_dispatch` / `ensure_rf_hit_layout`, contract
  `common/native_hit_rf_bindings.h` + `tests/native_hit_rf_bindings_test.cpp`.
- **Layout.** No new push descriptor, no ABI change: set 0 the D-RF push set (32), set 1 the
  material AlphaDSL from `rt_hit_material_use` (same layout as the host alpha set), set 2 the
  game's lighting set 1, set 3 the D-LS sun set (`BG3_SUN_SET 3`; this frame's D-LS set, else
  a fresh ring slot), set 4 SHaRC. Any missing input runs the ordinary D-RF unchanged.
- **Same-frame check.** With `rf_stats=1` the tier (a) hits also compute L_b; their output is
  unchanged (energy). Every 300 frames:
  `rf hit shade (300 frames): tier b hidden hits N, shaded N (P%), unknown material N,
  unsupported lighting N, shaded with local probes listed N, shaded mean luminance X; tier a
  check: N hits (N unsupported), mean L_b X, mean L_a X, mean (L_b - L_a) per frame X, standard
  error X (N frames), mean |L_b - L_a| X; L_b terms direct X, sun X, ambient X; clipped N`.
  `rf_debug=8`: tier (a) red L_a, green L_b (yellow = equal), tier (b) blue L_b; `rf_debug=1`
  shows shaded tier (b) white.
- **Expected bias of L_b vs L_a** (the gate measures it): L_a carries the GI/RT AO darkening,
  the game's native sun mask and D-RF's own reflections at the hit; L_b has AO 1 and the
  probe. Emission and local diffuse probes are missing from L_b (counted). Emission: L_a lacks it too
  (prevRadiance is before compose); `rt_hit_emission` adds it to shaded tier (b) hits, and with `=2` to
  tier (a) hits after this check (TRACK_D_EMISSIVE 13.23). The sun ray traces
  mask 0x01 in the cascade variants (no off-camera cascade casters).
- **Offline gates.** `py -3 tests/rf_hit_shade_shader_test.py`: every key-off request of the
  merge-base compiles byte-identical (rt_reflections, its SHaRC variant, D-LS, gi_hit_direct: 644/644);
  every rf_hit_shade request compiles, passes spirv-val and its descriptor contract.
- **Cost.** Expected +0.2 to 0.4 ms on `rf: reflection pass GPU median` (one material fetch,
  the froxel's shadow rays and one sun ray per tier (b) hit, 27% of the foundry's traced rays);
  `rf_stats=1` adds the tier (a) checks (measurement only). Not measured.
- **Hit lighting fixes (2026-10-31, branch hit-lighting; offline, not live).** From live S8/S6
  (main 1e90cd0): terms direct 0, sun 0, ambient 0.111; tier (a) L_b 0.111 vs L_a 0.0019.
  - L_a 0.0019 was the `rf_debug=8` window: the check read last frame's debug image from
    prevRadiance. Valid window (rf_debug 0): L_b 0.0774 vs L_a 0.0468. The check is no longer
    counted under any rf_debug.
  - The counters were luminance x 128: b13[0]-scaled direct/sun terms (~1e-3) rounded to 0.
    Now x 8192, clamped at 8 (clip counted above 8).
  - Ambient lacked the game's local DIFFUSE probes (f469db78:994-1178): indoors they replace the
    outdoor sky irradiance; every shaded S8 hit lists local probes. Now replicated
    (`native_hit_diffuse_probes`, m5 bit 1, cube pair m5 bits 3-16 / 17-30, mip 0).
  - The hit point is the triangle point (barycentrics), not o + d t (`native_hit_position`).
  - New lines: `rf hit shade lights` (rays traced/lit/occluded, first hit within 1 cm, own
    triangle, empty froxel, channel/range/cone zero, N.L <= 0, unsupported by reason) and
    `rf hit shade sun` (zero colour x clouds x b13, N.L <= 0, traced lit/occluded).
  - Known risk: the sun ray at hits has no roof-volume test (the TLAS lacks cut-away roofs; S8
    has 95% of pixels under roofs). If the sun line shows lit rays indoors, tier (b) must treat
    roofed hits as unsupported before it can pass.
- **Hit lights: space check and projector lights (2026-10-31, branch hit-light-space; offline).**
  Live S8 (main c3f406f): light rays at hits lit 0 of ~4.4k (RF) / ~12k (GI), N.L <= 0 91% (RF)
  vs 25% (GI), zero before the cosine 0; S6 every hit unsupported by projector or source.
  - No space mismatch: hit points (`o + d t`, `native_hit_position`), `rt_hit_normal` and the
    froxel lookup are in the camera-relative lighting space of `ray_at` (gi_common.hlsli:132),
    the same space D-LS uses for P (rt_local_shadows.hlsl:186) with the same b25 positions
    (`load_light`) and TLAS; `native_hit_cluster` is `bg3_cluster` on the projected point.
    The GI N.L <= 0 share (25%) shows the RF 91% is the reflection hits' geometry, not a frame error.
  - The difference left from D-LS is the visibility rule: D-LS skips the darkvision pair by
    art intent (`ls_noshadow=1`: 86% of S8's light evaluations, r = 18, TRACK_D_LOCAL_LIGHTS)
    and clears max(R, ls_clear) with the per-light radius R; the hit path traces every light
    with ls_clear only (lsSlots is not bound in D-RF / GI). New counters tell which: occluded
    with the blocker within 1 m of the light, rays to lights of radius >= 10 m.
  - Projector (box) lights: rf_hit_shade evaluates them (`NATIVE_HIT_BOXES`, set 1 b29/b45/b16
    as set 2; channel, box falloff, cookie at mip 0, _m9, D-LS ls_box visibility: traced to the
    box exit only with a game shadow map). gi_hit_direct has no box binding: it skips the
    projector term (`NATIVE_HIT_PROJECTOR_SKIP`, counted; bias: darker, never brighter) instead
    of the whole hit. The `lights` line adds `projector entries N, evaluated N, skipped N`.
  - Offline: 708/708 compile; 612/708 key-off requests byte-identical to c3f406f, the 96 others
    are exactly the gi_hit_direct and rf_hit_shade variants; 48/48 rf_hit_shade pass spirv-val
    and the descriptor contract (set 2 b29 added); build + 67 CTests pass.
- **Hit lighting as the screen (2026-10-31, branch hit-bias; offline, READY_FOR_LIVE).** Live
  tier (a) at main 0ad3fac: S8 L_b 0.148 vs L_a 0.107 (x 1.39; terms direct 0.0024, sun 0.036,
  ambient ~0.110), S6 0.0356 vs 0.0185 (x 1.9; direct 0.0248, sun 0). 96% of light rays go to
  lights of radius >= 10 m (the darkvision pair). Matched to the screen term by term, both
  `gi_hit_direct` and `rf_hit_shade` (`native_hit_direct.hlsli` `NATIVE_HIT_DLS_POLICY`):
  - `hit_ls_policy=1` (Push ls_flags bit 24): D-LS's own light policy. A light without a game
    shadow map that D-LS leaves to the game (ls_min_radius, ls_owners, ls_noshadow: the
    darkvision pair) is unshadowed, no ray, as on screen; traced lights stop max(R, ls_clear)
    short, R from lsSlots (else ls_source_radius); shadow-mapped lights are traced (no atlas at
    a hit; D-LS replaces it by default). lsSlots: GI push set b46 (room: 20/21 of 32); D-RF sun
    set b7 (the push set is full; D-LS ignores b7). A slot is used only while its key (absolute
    position within 0.25 m, radius) is the light's: the GI runs before this frame's join.
  - `hit_ao_rays=2`: the screen's AO estimator at the hit (the hybrid GI's: cosine rays,
    gi_rt_radius, weight 1 - (t/R)^2, aborted 1 - u^2/3), applied as the lighting applies b6:
    ambient diffuse x A, specular x Lagarde SO(A), direct light x mix(1, A, m0.y) where the
    screen keeps it (lights at the game's visibility; the sun unless ao_direct=1 with the RT
    sun; traced lights only with ao_direct != 1). Chosen over a fixed bias: the screen's AO is
    a per-point visibility (indoor corners vs open floor), a constant cannot match both.
    Residual bias: the screen's occluded ambient is partly replaced by the GI bounce, which the
    hit lacks (darker, never brighter). GI computes A only where a factor applies.
  - Log: `rf hit shade terms`: L_b direct (of it lights at the game's visibility), sun, ambient
    diffuse / specular, L_b at AO 1 (the old L_b, same frames), A vs the screen's AO at the
    hit's pixel, L_a diffuse / specular (prevRadiance .a), ratio L_b / L_a (and at AO 1);
    `GI hit direct policy` / `rf hit shade policy`: lights at the game's visibility, traced with
    a per-light R, AO estimates and mean A.
  - Expected: S8 ambient x ~0.6-0.7 (indoor A) -> L_b ~0.10-0.11 (ratio ~1.0); darkvision
    unshadowed adds at most ~+0.002. S6 is direct-dominated: unshadowing raises direct; the AO
    factor removes <= m0.y x (1 - A) of it; if the ratio stays > 1 the new terms line says
    whether L_b's direct or L_a's specular share carries it (outgoing = -ray, not the camera).
  - Cost (not measured): 2 rays <= 1 m per shaded hit (+ fewer light rays: the darkvision pair
    is no longer traced); `rf: reflection pass GPU median` and the GI trace median tell.
  - A/B in one run: `hit_ao_rays` 2 / 0 and `hit_ls_policy` 1 / 0 (both live).
- **Sun roof rule (2026-11-01, branch gi-hit-sun; offline, READY_FOR_LIVE).** Live S8 at f98179f:
  tier (a) L_b sun 0.037 + direct 0.005 + ambient diffuse 0.069 vs L_a diffuse 0.085 (x 1.26),
  S8 ~95% under roofs: the hit's sun ray went through roofs the camera cut away. Now
  `rt/native_hit_sun.hlsli` (`NATIVE_HIT_SUN_ROOF`, shared with `gi_hit_sun`): without the
  cascade casters (no `RT_CASCADE_MASKS`) a hit whose sun ray passes a roof volume (the sun
  pass's test, `gi/sun_roof.hlsli`; volumes from the sun set b8), or any hit while the volumes
  are unknown (Push ls_flags bit 25 / header flag 2), has an unsupported sun: tier (b) keeps the
  probe, the tier (a) check skips it (unsupported). With the casters the sun ray (only:
  `RT_CASCADE_SUN_HIT`) traces mask 0xFF with the cascade filter, as rt_shadow.hlsl. The
  `rf hit shade sun` line adds `roof-unsupported N, unknown visibility N` (counters 329-330).
  Live check: S8 tier (a) mean(L_b - L_a) with `rt_cascade_casters` 0 and 1.
- **Instanced wind and two-sided rows (2026-11-01; offline, READY_FOR_LIVE, not qualified).** Key
  `rt_hit_material_instanced=0` (GUI Experimental "Native hit materials: wind and two-sided foliage",
  `exp_hit_material_instanced`; needs `rt_hit_material=1`; the startup value patches the G-buffer VS, then 0/1
  is live). Covers ATM_Wind_Vertex_RG_VT 50fbfceb/8e49cde8 (3.9% combined) and Base_AlphaTest_2s_VT
  1e9145e4/923131c7 (2.4%): classifier built share 48.2% -> 54.4% (same 139 traces).
  - Why they had no record: neither VS is class A (wind displaces W; 2S adds the per-draw 1:0 @32 offset to the
    instance rows), so the meshes are never pooled; their BVH geometry is the per-frame prepass XFB of all
    instances, and the per-draw path took only instances == 1 and the 3812bc98 pair. Both depth passes are
    alpha-tested (depth VS 1719e9d0 + FS 4bed5db2 `vt p0=0 idx=32`; 106a0550 + a VT alpha FS).
  - Path (`common/native_hit_instanced.h`, `layer/hit_material.cpp`): the depth draw (by VS) and the G-buffer draw
    (exact pair) share a key of the draw words + index range + family (no vertex-buffer words, no first
    instance). Rows hold triangles x instances in the XFB's instance-major order; both XFBs are the VSs' clip
    positions per instance, so the per-instance transform is in the captured vertices (as the BVH soup). The
    G-buffer VS gets the 24/36/48 N/B/T patch (2d2ac3c3's interface). The STI engine bytes and the 1:1 reflectance
    (8e49cde8: byte 16) are staged in the gather's 8995 layout (512 B per row, as the pool rows); the join then
    checks p0/p1 and the tile-set matrices against the depth draw's alpha record, and each hit checks clip + UV
    per vertex (fail closed). Alpha cut: these hits come from the traversal's alpha test of that same record.
  - Two-sided (923131c7): row word 220 bit 0; the per-draw consumer scales the N column by
    `nv_two_sided_sign(ng, N, RtHit.dir)`, both only in the `RT_HIT_INSTANCED=1` variants of the 15 consumer files
    (`common/shader_variant.h` kHitInstanced, core slot bit 64). The core requests them when the layer's startup
    capability `native-hit-instanced-v1` is set; the live key only gates the rows (word 220 is 0 on every other row).
    Key off: all 872 requests of 2bf305e byte-identical; the 296 new variants compile and pass spirv-val.
  - Offline: `native_hit_instanced_test` (54 checks: families, key, geometry, staging, flags), recipe test, GPU
    fixture 9a/9b (12 rays, both sides), 73 CTests. Log (`rt_stats`): `[hit material instanced] frame F key K; wind:
    prepass N, G-buffer N (pipeline N, bindings N, stream N) captured N, mapped N rows N triangles; alpha-test 2S: ...`.
  - Risks: the wind VSs' non-`precise` terms may round differently (reason `clip` rises); two draws with one key in
    a pass are declined. Cost (not measured): XFB of the instanced G-buffer draws (64 B per vertex) plus 2x query
    copies, inside the existing caps; expected 0.2 to 1 ms on Ultra.
  - Live: `tools/live_batches/m4_instanced` (S14 Lower City: wind 9.7% + 2S 4.1% of the placed geometry), OFF/ON/OFF.
  - First live run (2026-11-01, log m4i-instanced-s14-fg2.1.074646): qualified 22% -> 36-40%, +0.14 ms gi trace,
    +0.1 ms rt passes; mapped wind 5 rows 31841 tri, 2S 12 rows 90407 tri. Reason `clip` 0 -> 0.4-1.9M per 120
    frames (up to 6% of lookups).
- **Wind clip tolerance (2026-11-01; offline, READY_FOR_LIVE).** Root cause: the wind rows. The camera was static,
  clip varied 0.59M-1.86M per window while qualified + clip stayed 13.1-13.3M: the same hits pass or fail over time.
  2S (106a0550 / 1e9145e4) is `precise` end to end on static inputs, so it cannot vary; the wind VSs (1719e9d0 /
  50fbfceb) evaluate the displacement o = (normalize(Wvec + d) |d| - d) fade outside `precise`, so the driver may
  fuse a mul + add in one module and not the other. The phase works in absolute world xz (u x 500 m) and the gust
  changes |o|, so the share of flipped clip bits varies with the wind. Only UV, N/B/T are read from the G-buffer XFB
  and none of them depends on o.
  - Fix (`common/native_hit_instanced.h` `wind_clip_tolerance`, RT_HIT_INSTANCED only): the layer reads the wind VS
    inputs at both draws (frame 0:0 time, scene 0:1 wind, camera 0:2 _m2/_m6, material 1:1 _m0.._m3). If they
    are bitwise equal, or only the time differs (the time term evaluated in each pass), the row gets word 220 bit 1
    and tau_c = a_c + b_c |W|inf (words 224/240): the first-order bound of every fusable site (phase, wind vector,
    normalize/length, n L - d, fade), through the precise _22 + o and the 4-term clip dot, rounded up. UV stays
    bit-exact, 2S and any other input difference stay bit-exact (fail closed). Word 220 bits 8..9 tag the family.
  - Unit test (`native_hit_instanced_test`, 71 checks): fp32 simulation of both VSs with every fusable site fused
    or not: 20000 vertices, 13% with differing clip bits, all within tau (worst 0.004 tau; tau ~1.5 mm in world at
    |cam| 500 m, loose by the L <= 2 sqrt3 R term); with the time 1/60 s apart all within (worst 0.12 tau); the same
    vertex of another instance 5 cm away is always outside tau.
  - Log: `[hit material instanced] ... wind inputs prepass vs G-buffer: equal, time only (max |dt|), other differ,
    unread; rows with the derived clip tolerance` and `hit material instanced clip (120 frames)` per family:
    checked, bit-exact, within tolerance, UV differs, beyond x1-16 / x16-256 / over x256, rigid offset (counters
    356..371); rejected = P% of all lookups. Key off: every request without RT_HIT_INSTANCED byte-identical.
  - Expected live: wind inputs equal (time only at most), clip rejects < 0.04% of lookups (job expect line), and
    the qualified share up by about the old clip share (+1.9 to +6 points). Cost: none measurable (3 more loads and a
    compare per instanced hit; 8 mapped UBO reads per wind draw).
  - Live result (m4i 09:39): not the cause. Wind "within the derived tolerance 0, over x256", 2S "differs", UV
    differs 0, rigid 0, wind inputs equal on the logged frames.
- **Pairing and instance order (2026-11-01; offline, READY_FOR_LIVE).** The rejects of that run: 2.6%, 3.1%, 0%,
  0.4% of all lookups per window at a static camera; per family the same share of its checked hits in each window
  (wind/2S 24/27%, 10/16%, 2.2/2.4%); 0 clip rejects on the static per-draw rows in the same frames. So whole frames
  held rows whose two XFBs place the same mesh (UV bit-exact) at other instance positions.
  - Cause 1, frames: the instanced key (draw words + index range) is the same every frame, and the slot <-> packet
    epoch is `frame_index()`, which counts presents and moves mid-frame (same log: `query reject source epoch
    current 4320 source 4319`). When the current G-buffer packet is recorded after the query, the previous frame's
    packet carries the source's index and joins. Fix: both draws read their VS inputs at record (frame time 0:0
    word 0, camera 0:2 _m2/_m6; 2S the per-draw translation 1:0 @32; wind the scene/material words) and
    `pair_check` maps the row only when all are equal (time differs = another game frame). The wind tolerance now
    only covers equal inputs.
  - Cause 2, order: the two passes upload their instance streams separately; the rows are instance-major, so
    every instance in another order fails at the hit. Fix: `rt/hit_material_instanced_remap.hlsl` after the join,
    per instanced row: mode 0 maps depth instance i to the first G-buffer instance (from i, wrapping) whose first
    triangle has i's clip + UV bit for bit; mode 1 copies that instance's block into the row's deferred copy. An
    instance without a match keeps its own block, so the hit check rejects it (fail closed). CPU reference:
    `match_instance` / `remap_instances`. The per-hit check is unchanged.
  - Log (per window, all queries): `[hit material instanced] frame F pairs since the last line (wind/2S): same
    frame, other frame (time), camera, placement, wind inputs, unread; wind rows with the derived clip tolerance`
    and `... instance order since the last line (wind/2S, retired queries): rows, instances in the same order,
    permuted, unmatched` (the remap shader's counters, read when the query retires). Deciding: clip rejects < 0.04%
    in every ON window; `other frame` > 0 or `permuted` > 0 says which cause was live.
  - Offline: `native_hit_instanced_test` 128 checks (2S VS in fp32: 9 instances, 3 reordered -> UV equal, clip
    differs, rigid 0 as live; after the remap all bit-exact; a replaced instance stays rejected; pair_check per
    input). No existing shader changed. Cost (not measured): mode 0 is O(instances) in the same order, at most
    instances^2 first-vertex compares; mode 1 one 4 B copy per word of the reordered instances.
- **Third family: wind RGB 2S (2026-11-01; offline, READY_FOR_LIVE, not qualified).** Startup key
  `rt_hit_material_wind_rgb=0` (GUI Experimental "Native hit materials: RGB wind foliage", needs the instanced rows).
  Why: the S8 replay pack ranked by `tools/hit_material_blockers.py` (frame_replay `--define HM_BLOCKERS=1`, the
  outcome per TLAS instance): soup alpha instances of depth FS 0d91554d block 13.3% of all lookups, the largest
  single item. Asset DB: 0d91554d is the StaticInstancedDepth FS of four ATM_Wind_Vertex_RGB_AlphaTest_2S templates;
  in the logs its triangles match 735d5a19/83831811 (camp 126.9k of 128.3k), which holds 2.6% of traced G-buffer
  draws against 0.05% for the plain 2S one. Pair: depth VS 633929ef + FS 0d91554d, G-buffer 735d5a19/83831811.
  - Recipe (classifier vs 14678345, "same sampling, 10 statements differ, outputs out0,out2,out3", pinned in
    `tests/material_families_test.py`): out0 the two-sided N column (as 923131c7), RT2.y = clamp(1:1 _m5) (byte 20),
    RT2.w = class 4 (thin translucency) with the channel bits, out3 the translucency inputs. Engine UBO as 14678345.
    The hit gets the exact class 0 front lobe; the transmission lobe is missing (as the Iridescent pool rows).
  - Capture: VS 735d5a19 has 50fbfceb's interface and the depth VS 633929ef 1719e9d0's; the position arithmetic is
    textually equal in both (RG wind plus a flutter term: 1:1 _m3/_m4, vertex colour b). No clip tolerance is
    derived for the flutter term: the rows are checked bit for bit (the RG wind rows were 101357 of 101357
    bit-exact live), fail closed. Pairing as wind plus 1:1 _m4. Family 3 in word 220 bits 8..9; clip counters
    427..434, logged as `hit material instanced clip wind RGB 2S`; layer lines `[hit material instanced] frame F
    wind RGB 2S: ...` and `... wind RGB 2S since the last line: pairs ...; instance order ...`.
  - Off (default): `instanced::gate` makes the family None for every lookup, so wind and 2S are unchanged;
    only the `audited recipe, no capture path` census counts its draws. Shaders: every request without
    RT_HIT_INSTANCED should stay byte-identical (gate PENDING: tools/shader_identity.py); the instanced requests change
    only by the third family's counters.
  - Live: batch `m4_wind_rgb` (S8, 144 s). Expected: up to +13 points of qualified share at S8, about +3 to +7 at
    camp (0d91554d is 6.6% of camp's unsupported alpha triangles; hits per triangle are higher for these trees than
    for grass), 0 at S6 (not in that scene).
- **Fourth family: IP grass (2026-11-02; offline, READY_FOR_LIVE, not qualified).** Key `rt_hit_material_ip_grass=0`
  (startup frame patch and RT_HIT_IP_GRASS variants, then live rows; needs the instanced rows). IP_Wind_GrassFlowers
  _AlphaTest_2S_VT / _PBR_VT: depth 05d39f75/73849471, G-buffer 105e47e4/35a18400, camp's largest blocker (row reason
  60% of lookups, m4p1 031746). FS: the Base_VT decode with its VT layers at 1:4..1:7 (`LayoutVt4`; the VS samples its
  wind noise at 1:3), engine UBO as the 2S family, reflectance 1:1 byte 16, a camera-distance dither discard (coverage).
  VS 105e47e4: W at 0, UV at 1, N/B/T at 2..4 (frame patch N location 2). Both VSs: the same wind arithmetic, so the
  rows are checked bit for bit. Family 4 needs word 220 bit 10: without the define it reads 0 and the row gets the plain
  bit-exact check; with it the 3-bit field and counters 435..442. Pair check: time, camera, 2S translation, wind words.
  Live: `tools/live_batches/m4_ip_grass` (camp). Predicted +30 points qualified (10-40), brightness null.
- **Pool family: Base_MSK_VT (2026-11-01; offline, READY_FOR_LIVE, not qualified).** Live key
  `rt_hit_material_mask=0` (ini only: the GUI knob "Hidden-surface shading" is at its 8-key limit).
  - Why "pool without row" (14.9% of S8 replay lookups, 133 instances): every one is a class A G-buffer pair with no
    recipe (layer log `without record: other pair 67/172`; basis ready 99/100, no slot shortage). The pack has no
    VS/FS per pool instance, so the split is from the S8 traces (class A pairs without a recipe, draws per frame):
    54df894a/7a07134e ENV_DetailMap_HeightGradient_VT 17.9, 6044c8cf/02807a68 ENV_Tileset_MultipleTextures 13
    (TileConstruction; the house has 170 tile objects), 2aff0bdc/d54ed410 9, 66602480/fc32acdc 8, 2d2ac3c3/a5f44c48
    Base_MSK_VT 5, f474891e/cebfa2f6 5, 2d2ac3c3/fa18b37c 4.
  - Built: the smallest, Base_MSK_VT (`LayoutMask`, native_vt_surface.h). Classifier vs 14678345 (pinned in
    `tests/material_families_test.py` BaseMask): only RT1 differs (albedo x colour mask 1:6 at the UV with sampler 0:7
    and the 1:1 colours at 0/16/32: `nv_mask_albedo`), RT2.y reads 1:1 byte 12, physical moves to 1:7. Row: word 224
    = 3 | mask sampler slot << 8, 228 mask view, 232..252 c0/c1, c2 in p1.yzw (68..79; the VT layers read only p1.x).
  - Not built: Tileset 02807a68 needs per-vertex COLOR (the blend weights) in the pool basis (44 B/vertex, UV + N/B/T)
    and a 6044c8cf XFB map (COLOR at location 1 shifts N/B/T); HeightGradient 7a07134e needs the absolute world
    position (camera _m6) at the hit, 3 more 2D views and 20 floats: more than the 256 B row holds.
  - Offline: key off, the S8 replay is bit-identical (giRaw fnv1a a54c4dfd1cc9acb9, all 512 x 64 counters equal with
    HM_BLOCKERS) with the new pool SPIR-V; the qualified share after it cannot be replayed (the pack holds only the
    rows and views captured live). Live: batch `m4_hit_mask` (S8 + inn). Predicted S8 +1.1 points (0.3-3).
- **Generated rows (2026-11-02; offline, READY_FOR_LIVE, not qualified; docs/GAME_DATA_AUDIT.md action 2).** Key
  `rt_hit_material_generated=0` (startup VS frame patch, then live; ini only). `py -3 -m tools.material_families.recipe_rows`
  writes `common/native_hit_generated.h` (`--check` in CTest `hit_material_generated_test`, about 8 s) from three sources
  that must agree: the asset DB (StaticInstancedDeferred VS/FS, StaticInstancedDepth VS, engine UBO offsets of the UV
  transform / tile-set index / light channel, the 1:1 Reflectance offset, the VT layer bindings), the FS classifier
  against the audited recipes (identical, fields only, reflectance member moved, RT2.y = 0.5, or the RT2.w flag bit 3
  dropped; the DB offsets must equal the classifier's field map) and the capture path (pool: VS 2d2ac3c3; instanced: an
  audited bit-exact family whose depth VS it shares with an equal G-buffer gl_Position slice, or whose bindings it has
  with its own depth and G-buffer slices equal). 8 rows of 830 pairs (rejects listed with `--rejected`):
  IP_AlphaTest(_2S)_VT 0de1b186/641822a7 (= 35a18400, refl @8, LayoutBase per the DB, 2S family, N location 2),
  wind bushes PBR translucency 27ef2b49/2e34c667 (= 83831811 fields only, LayoutVt4, IP grass family certificate),
  wind RGB 2S 9cc7094f/4464f442 (= 923131c7) and its GM 9cc7094f/4f3ec0c7 (= 14678345, refl @20), BASE_AlphaTest_FX
  1e9145e4/9d925bfd (= 14678345), GM_Pulse_AlphaTest 1e9145e4/e4ade329 (= 3da860be, constant: `stage` writes 0.5),
  Base_GM/BGM 2d2ac3c3/651863a6 and HLOD_GM 2d2ac3c3/b0ecc292 pool (= 3df678c0). Generated instanced rows borrow their
  family's key, pairing certificate and remap and are checked bit for bit at the hit; they are not gated by the wind
  RGB / IP grass keys. Key off: every audited lookup unchanged (`native_hit_generated_test`), no shader changed
  (2961/2961 requests byte-identical). Packs cannot show the share after (rows are captured live); camp pack 013545-2
  (instanced off) blocks 20.2% of lookups on the bushes' depth FS f9ab595d and 5.4% on IP_AlphaTest's 2f1b0ffd. Live:
  `tools/live_batches/m4_generated` (camp + inn), predicted camp +12 points (3-25), inn +1.5 (0.3-4), energy null.
  Rejected and worth a recipe next: 3b346b42 HLOD two-sided (pool two-sided flip), 6561beef/ce81ffaa (DB channel @60
  disagrees with the classifier's map), the ColorVar / SSS / translucency variants.
  Live (m4_generated 2026-11-02, hgn1 camp / hgn2 inn): qualified +2.3 / +0.12 points (predicted 12 / 1.5: below),
  generated 2S rows bit-exact, energy -0.12% / -0.10% (null), GI trace +0.58 / +0.03 ms, no overflow; the wind bushes
  rows captured 9 and mapped 0 (pairing to diagnose), 83 IP_Base_VT G-buffer draws captured and never mapped. Off.
  Two-sided pool rows (2026-11-02): RT_HIT_POOL_2S (variant bit kHitPool2s, only instanced + wind RGB without IP grass /
  OMM / tileset / scalp / hair origin, +45 requests) flips the pool row's N for h2.w = 5; other consumers reject such
  rows. The generator also accepts an RT2-only difference equal to an audited recipe's RT2 (members erased, temporaries
  renamed, RT2.w folded): 11 rows (+ Base_2S_VT, Base_AlphaTest_2s_Translucency, ATM_Wind_Vertex_R_AlphaTest). HLOD
  two-sided 3b346b42 stays rejected (another page decode in its normal-mix sample).

  HLOD audit (2026-11-02, code only, unverified: build pending):
  `db_templates` / `db_fields` identify `Engine_Default_HLOD_TwoSided_VT` as pool VS
  `2d2ac3c38992b7f0`, FS `3b346b4241d44152`, engine transform/index/channel offsets
  0/32/36, table/normal/albedo/physical at 1:3/4/5/6 (LayoutBase), material CB 16 B.
  There is no parameter with the `reflectance` role. The one-sided HLOD FS
  `8165785442a20e57` has the same bindings, offsets and missing role; those facts
  alone cannot prove equal reflectance sampling. ASSET_DB section (b) records named
  bindings and offsets, drops unrecognized roles and samplers, and stores shader
  hashes, not shader instructions or VT page-decode expressions.

  Existing row fields cannot express the documented differing page decode of the
  two-sided FS's reflectance-mix input. `ReflectanceNormalMix` evaluates
  `clamp(0.5 + (nl.x - 0.5) * ubo)` with the SAME `nl` used for the shading normal:
  `rt_hit_material_pool` calls `hit_material_layer(..., col=1)` once for both.
  That sampler fixes `e=page[col]`, scale `exp2(e & 15)`, page x/y bits 14/4,
  layer bits 24..30 and the tile-set matrix column `col`. `Recipe` can select a
  binding layout, UBO offsets, reflectance arithmetic and N flip, but has no
  independent reflectance sample, table column or page-decode selector. Changing
  the normal binding/layout would also change the shading normal; changing the
  reflectance offset changes only the mix weight. Neither preserves the material.
  A row `{3b346b42, 2d2ac3c3, 0, 32, 36, ReflectanceNormalMix, ..., CapturePool,
  LayoutBase, true}` would therefore silently use the wrong sample and is rejected.
  A new sampler/row contract is required before adding it. The exact alternative
  page expression and material-CB weight offset were not re-established here:
  shader dumps are outside this task's allowed capture reads. No generated header
  change; no fabricated offset or page selector. The native generated-row test
  pins rejection. This is a schema limitation audit, not a new qualified recipe.
- **Cost and bias breakdown (2026-11-01; offline, READY_FOR_LIVE).** Live (m4r1 030749/031149, m4p 11xxxx,
  diagnostics off): rf pass +1.5-1.6 ms at S6 (0.42 -> 1.94-2.0), +2.0 at S8 (0.57 -> 2.55), budget 0.2-0.4.
  - Where it goes (counts per frame from the stats windows, which include the tier (a) checks): S6 has
    1.23M tier (b) candidates (hidden 11%, off screen 46% of 2.15M rays), S8 0.46M (13% + 12%). The material
    lookup qualifies 26% (S6) / 55% (S8); about 60% of the qualified S6 decodes then failed `native_hit_cluster`
    (off screen: "source" 176k/frame), after paying 3 VT layers. Per hit reaching the light loop: S6 ~5 point/spot
    evaluations and ~2.3 long shadow rays (90% to lights of radius >= 10 m), S8 ~0.6 rays but ~2 projector entries
    (739k box evaluations/frame); AO 2 closest-hit rays per hit reaching the end (`hit_ao_rays=2`: S8 ~234k,
    S6 ~70k rays/frame); one sun ray (S8 ~1/3 of hits). The rest is divergence: 25% of S8's traced lanes take
    the shading branch, so nearly every warp waits for its slowest shaded lane. Register count not measured
    (`tools/pipeline_stats.py` needs the GPU; not run while the live batch runs).
  - Exact cuts (image unchanged, no key): `native_hit_shade` tests the froxel (`native_hit_cluster` at
    `native_hit_position`) before the material decode when `rf_stats=0` (the reason counters stay as before
    with stats on); the box loop tests channel and inside-box from 4 words of the record before loading the rest
    and the cookie (`native_hit_direct.hlsli`, same arithmetic as `bg3_box_reaches`). Expected (not measured):
    S6 -0.2 to -0.5 ms (most off-screen decodes gone), S8 -0.1 to -0.3 ms. Default and gi_hit_direct SPIR-V
    byte-identical (rf_hit_shade_shader_test). Next, if more is needed: compact the tier (b) lanes per group
    (groupshared job list) to remove the divergence; a 3-phase direct loop (domain checks before rays) saves the
    S6 rays of hits that fail later ("other" 24k hits/frame).
  - New counters 383-384, log `rf hit shade boxes`: L_b's projector (box) part at tier (a) checks and shaded
    tier (b) hits. The m4_promotion S6/S8 plans add `cost-ao0` (bundle ON, `hit_ao_rays=0`).
  - Bias (tier (a), L_b / L_a). S8 0.925: diffuse-like L_b (direct 0.0019 + sun 0.0038 + ambient diffuse 0.0954)
    = 0.101 vs L_a diffuse 0.115 (-0.014): the screen's diffuse includes the hybrid GI bounce that replaces part
    of the AO-occluded ambient (the hit has ambient x A only; A 0.773 vs screen 0.779 is within 1%). Specular
    L_b 0.0173 vs L_a 0.0133 (+0.004): view-dependent (outgoing = -ray, more grazing than the camera) and the
    screen's own D-RF (probe term -9% after D-RF on screen, none at the hit). Emission: neither side. S6 1.089:
    direct-dominated (0.066 of 0.076; lights at the game's visibility 0.005), L_b direct + ambient diffuse 0.073
    vs L_a diffuse 0.059 (+0.014), specular 0.003 vs 0.011 (-0.008). Candidates for the excess: the box lights
    (30% of S6's light evaluations; the box AO fix alone moved 1.48 -> 1.09; the new boxes line measures their
    share), the lights' specular lobe toward the reflection ray (hd_scatter returns diffuse + specular), and the
    GI bounce working the other way (L_a has it). The AO estimator is not the cause (0.804 vs 0.794).
- **Indirect diffuse at hits: `rf_hit_gi` (2026-11-01; offline, READY_FOR_LIVE, off).** The S8 diffuse gap
  (L_b 0.101 vs L_a 0.115) is the GI composite's bounce: the screen's diffuse is ambient x b6 (b6 = the traced
  visibility, `gi_ao_apply`) + history.rgb x diffuse colour x gi_strength (`gi_composite`); the hit had only the
  first. Key `rf_hit_gi=0` (live; needs `rf_hit_shade=1`; GUI Experimental "Indirect light in hidden-surface
  reflections", `exp_rf_hit_gi`). Variant define `RF_HIT_GI=1` on the four hit-shade files
  (`common/shader_variant.h` kRfHitGi, own Pipes `g_rf_shade_gi`); key off, every existing request byte-identical.
  - Composition: `common/hit_gi_compose.h` `hg_compose` (direct, sun, ambient x A, specular x SO(A), + bounce x
    diffuse colour x gi_strength; strength 0 when the GI composite does not run). The AO-fold rule is the
    screen's: A replaces SSAO for diffuse and specular (gi_ao=1; with gi_ao < 1 the screen mixes SSAO, a hit has none).
  - Sources. `rf_hit_gi=1`: the screen's estimator at every hit. The `hit_ao_rays` rays (same rays and weights
    as `native_hit_ao`, so A is unchanged) also apply the GI trace's bounce rule (gi_rt_fallback 0): a ray hit the
    screen shows adds `prev_bounce_at` x w; with SHaRC a hidden ray hit found in the cache adds its radiance x w.
    `rf_hit_gi=2`: as 1, but the tier (a) check takes the screen's own b6 and GI history at its pixel (exact
    screen terms: the check then measures only the material and lighting replication).
  - Not reproduced (lower bound, darker never brighter): the GI's own hidden-hit shading (gi_hit_direct /
    gi_hit_sun / gi_far) and SHaRC's radius split; 2 rays per hit instead of the screen's temporal history (noisier).
  - Box lights (the S6 question). With `ls_box` 0/1 the screen keeps the game's box term for boxes without a
    shadow map (unshadowed) and the game's atlas (ls_box=0) or a ray to the box exit (ls_box=1) for shadow-mapped
    ones; the hit traces shadow-mapped boxes to the box exit and leaves the others unshadowed: the same rule,
    except atlas vs ray at ls_box=0. With `ls_box=2` the screen also traces the unshadowed boxes (to the end of
    their light-side fade-in) and ends shadow-mapped ones at the shadow view's near plane; the hit did not.
    `RF_HIT_GI` applies that rule at hits when the screen's box pass ran in mode 2 (Push::ls_flags bits 26 and 27,
    `native_hit_direct.hlsli` NATIVE_HIT_BOX_LS2). The intensity is the same math (`common/box_light.h`, checked
    against the game's loop by `box_light_test`); left: the cookie at mip 0 (same mean) and the hit's outgoing
    direction (the reflection ray, more grazing: its specular lobe differs from the camera's by design).
  - Log (`rf_stats=1`, device-local counters 419-426, set 6 b0): `rf hit gi (300 frames, N tier a hits, N of them
    from the screen's GI): tier a GI term X vs the screen's GI term there X; ratio L_b / L_a X (without the GI term
    X); tier b shaded GI term X; AO-ray hits N, screen source N (P%), cache source N (P%)`.
  - Unit test `hit_gi_compose_test` (CTest): a hit identical to a screen pixel (mode 2) composes exactly the
    transcribed screen chain (gi_ao_apply, the lighting's b6 rules, gi_composite) for 16800 random cases over every
    m0.x / m0.y / gi_ao / ao_direct combination; the per-ray weight is gi_trace_rt's; Monte Carlo under a ceiling
    matches the closed form. Shaders: `rf_hit_shade_shader_test` (key-off byte identity, every RF_HIT_GI request
    compiles, validates and matches the contract with `--gi`).
  - Expected (tier (a), the user's gate): S8 L_b / L_a 0.925 -> about 1.0-1.03 with mode 2 (the +0.014 diffuse
    gap closes; +0.004 specular stays), 0.98-1.03 with mode 1 (the estimator misses the GI's hidden-hit shading).
    S6 1.089 -> about 1.15-1.25: the GI term adds to a hit whose direct is already too bright (L_b direct + ambient
    0.073 vs L_a diffuse 0.059, which itself includes the bounce): S6's excess is in the direct term, not the GI.
    `diag-gi1-box2` (ls_box=2) gives its box part (`rf hit shade boxes`) with the screen's mode-2 visibility.
  - Cost (not measured): per AO ray one projection, two loads and the depth test (S8 ~234k, S6 ~70k rays/frame):
    expected +0.02 to 0.1 ms on `rf: reflection pass`; with ls_box=2 the unshadowed boxes' rays (S8 up to ~0.7M
    short rays/frame) up to +0.3 ms. Live: m4_promotion m4p3/m4p4 `diag-gi1`, `diag-gi2`, `diag-gi1-box2`,
    `cost-gi1`, `shot-gi1`.
  - **The S6 direct excess is the BRDF, not the boxes (2026-11-01, branch rf-hit-box; offline).** Live m4p3 161852
    (`rf hit shade boxes`): the box part of tier (a) L_b is 0.00508 of 0.0764 (6.6%), all of it already at the game's
    visibility with the AO factor; point/spot give 0.0606 of the 0.0657 direct. Box record math, channel, falloff,
    cookie, shadow strength and AO factors match f469db78 (box_light_test). What differs for every light is the lobe:
    the hit used the declared FresnelBlend, the screen the game's Burley x mix(1, 0.6622, a) + GGX/Smith-joint/Schlick
    (f469db78 _1597/_1598, the same for point, spot and box). `RF_HIT_GI` now also defines NATIVE_HIT_GAME_BRDF:
    `HD_SCATTER` -> `hd_game_scatter` (common/native_hit_direct.h) for point/spot, box and sun at hits, same domain
    checks and status codes (the supported set is unchanged). Not reproduced: the screen-quad AA roughness term
    (a hit has no derivatives; a = max(0.09, r)). Test: `box_light_test` section 8, hit == the game's box loop on
    random records and surfaces (2^-14 relative), and the FresnelBlend / game energy ratio over the same cases.
- **Live gate (pending).** Same run OFF/ON/OFF at S8 and S1, `rf_stats=1` hold >= 300 frames:
  mean (L_b - L_a) within z x its standard error, else tier (b) is biased and stays off; then
  hard_dark on tier (b) pixels and crops.

### Pak mesh materials: identity audit and remaining runtime work (cov-mesh-material, 2026-11-03)

Status: offline catalogue plus sampled host identity; runtime fallback is **not implemented or READY_FOR_LIVE**.
`rt_hit_material_paks` is the proposed off-by-default key, not a registered working feature.
No qualified row, descriptor, shader or ABI changes accompany this work.

Step 1 runtime increment (mesh-identity, 2026-11-03): `rt_mesh_content_id=0` gates
a host-only v1 candidate fingerprint on class-A mesh captures and audits. It reads
existing mapped raw float3 positions and uint8/16/32 indices, never XFB positions
or extra GPU readbacks. It hashes the referenced vertex span, index count and 32
ordered index/position samples; indexed draws scan indices to derive the span.
Handles, offsets, padding, index width and instance transforms are excluded.
Missing mappings/layouts stay unknown. Key off computes no IDs. The host stores
the candidate on captured pool meshes and logs `[mesh id] content ids: N meshes,
M collisions, K stable across 2 captures`; collisions here mean observed differing
samples with the same hash. Unsampled mutations can alias: this ID never qualifies
a row. Full canonical equality in prerequisite 1 below remains unimplemented.
LSLib is absent and the catalogue cannot decode GR2 buffers; the byte contract and
exact decoder prerequisite are recorded in `tools/assets/mesh_content_id.md`.
Unit tests and CPU cost are recorded in `docs/progress/cov-mesh-material.md`.

The existing mesh key is not a resource ID or vertex/index content hash. `common/native_hit_pool.h`
`Geometry`, `geometry_key` and `position_key` hash Vulkan buffer handles, draw ranges and offsets.
`layer/hit_material.cpp` `pool_keys` builds those keys; `layer/bvhsplit.cpp` `bvh_classify` similarly
keys the BLAS cache by handles, layout and draw arguments. These identities work within the running
process but cannot join a pak resource. The tracker retains mapped-memory addresses, not a device-local
VB/IB byte shadow (`layer/tracker.cpp`, `BindBufferMemory` / `MapMemory`). `layer/cmd.cpp`
`CmdCopyBuffer`, `CmdCopyBuffer2`, `CmdUpdateBuffer` and `CmdFillBuffer` invalidate BVH ranges only.
No reliable runtime resource identity may be inferred from a shader family, triangle count or bounds.

Three prerequisites are needed before a pak row can qualify:

1. **Exact geometry join.** Export each GR2 ObjectID / LOD / submesh with its indexed object-space
   positions, UV and native tangent-frame inputs. Define one versioned canonical byte stream and
   compute it identically offline and from the runtime draw's raw indexed attributes. Preserve index
   order, vertex offset, winding, attribute decode and float bits; reject unsupported formats,
   non-finite inputs, unknown transforms and collisions or multiple material candidates. Hashes select
   candidates; full canonical-byte equality must decide the match. Use mapped bytes when available;
   otherwise a bounded GPU readback with submission retirement and buffer-write invalidation is
   required. Existing handle keys may cache the verified join only for that buffer generation.
2. **UV / basis without a G-buffer draw.** `PoolMesh` currently stores the pool position range, and
   `pool_job` obtains UV / N / B / T from G-buffer XFB. A never-drawn mesh cannot run that job.
   Decode the audited raw quaternion / UV attributes, or import an independently verified offline
   basis, into the existing `nh_pool_basis_vertex` format. Match the pool's triangle order and mesh
   generation and retain the basis header / position check in `rt_hit_material_pool`; do not mark
   `Basis::Ready` merely because an ObjectID or fingerprint matched.
3. **Textures and parameters.** The descriptor/cache path accepts current `VkImageView` / image
   generations and sampler metadata (`ResourceBudget`, `capture`, query-owned pool rows). It does not
   resolve asset texture UUIDs to those views. VT indirection, cache views, tile-set matrices, engine
   UV transform, channel and exact material overrides must all resolve before constructing the same
   256 B row. Build a verified UUID-to-current-view bridge, or owned pak texture uploads with equivalent
   sampling semantics; reuse the existing budget, sampler clone, publication and retirement path.
   Missing, stale or conflicting resources remain unknown.

Integration must try pak rows only after the existing captured path fails, and must reserve captured
rows and descriptors first. Offline gates: canonical fingerprint equality across buffer handles and
index widths; reject position / UV / basis mutations, ambiguous materials, unsupported layouts and
stale writes; exact 256 B row equality for matched captured fixtures; identical qualified-row resources
and pre-post colour on replay with the key off and for previously qualified hits with the key on.
Existing packs lack raw mesh resource identity, so they can freeze the old-row baseline but cannot
establish a new pak join or predict its qualified-share gain. No GPU cost or energy improvement is claimed.

### Recipe-row branch progress details (verbatim)

```markdown
## Generated hit-material rows from the asset DB (branch hm-recipe-rows, READY_FOR_LIVE, 2026-11-02)
- New `tools/material_families/recipe_rows.py` -> `common/native_hit_generated.h`: rows only where the asset DB (offsets,
  VT bindings, depth VS), the FS classifier (identical / fields only / reflectance moved / constant 0.5 / flag dropped)
  and the VS capture path (pool 2d2ac3c3, or a bit-exact instanced family by depth VS + gl_Position slice) agree.
  8 of 830 pairs: IP_AlphaTest(_2S), wind bushes PBR translucency, wind RGB 2S plain/GM, BASE_AlphaTest_FX,
  GM_Pulse_AlphaTest (constant), Base_GM/BGM and HLOD_GM (pool, = 3df678c0). TRACK_D_REFLECTIONS 14 "Generated rows".
- Key `rt_hit_material_generated=0` (startup VS frame patch, then live). Key off: audited lookups unchanged
  (native_hit_generated_test), 2961/2961 shader requests byte-identical (no shader change), 100/100 CTests.
- Offline share: packs cannot replay new rows (captured live). Camp pack 013545-2 (instanced off) blocks 20.2% of
  lookups on the bushes' depth FS and 5.4% on IP_AlphaTest's; the inn pack predates pool alpha (1.0% replayed).
- Live job ready, not queued: `tools/live_batches/m4_generated` (camp + inn, 164 s each, ~10 min, 0.4 GB), predicted
  camp +12 points (3-25), inn +1.5 (0.3-4), energy null, camp GI trace +1.0 ms (0.2-2.5). Dryrun static checks pass
  (its fake-queue runs fail as on main).
- Also on this branch (from the audit branch): `tools/assets/locate.py`, no hard-coded LSLib or game path in the VT
  mask extractor (BG3RTX_LSLIB / external/lslib*, BG3_DATA / Steam libraries); vt-masks-catalogue still hard-codes it
  until it merges with this.

## m4_generated live result: correct, small gain, stays off (branch hm-recipe-rows, measured, 2026-11-02)
- hgn1 camp 145058 (INVALID only for the share gate's window length) / hgn2 inn 145500 (VALID), GPU ms only (two
  identity runs compiled during the batch). Qualified share OFF -> ON: camp 26.9% -> 29.2% (+2.3 points; predicted +12,
  range 3-25: below), inn 24.97% -> 25.09% (+0.12; predicted 1.5, range 0.3-4: below). Clip: every generated 2S row
  bit-exact (866k-1.49M checked, 0 rejected). Energy: camp -0.12%, inn -0.10% (bands 1.85% / 1.23%): null as predicted.
  GI trace: camp +0.58 ms (predicted 1.0, 0.2-2.5), inn +0.03 ms. Capacity: overflow 0 everywhere.
- Why below: camp's bulk was the wind bushes (20.2% of pack lookups): 9 draws captured, 0 mapped (pairing not logged:
  the IP grass family lines printed only with rt_hit_material_ip_grass; now also with the generated key). The 2S family
  gained 42 mapped rows (IP_AlphaTest) but 83 more G-buffer draws were captured and never mapped (the opaque IP_Base_VT
  risk, confirmed): deferred stream 57 -> 93 MB, no overflow. Inn: pool rows 651863a6 9/7 and b0ecc292 6/3 records/ready.
- Verdict: not into Ultra (+2.3 points for +0.58 ms at camp, no measurable image change); keep off until the bushes pair.
- Job fix: at camp's ~8.5 fps with rt_stats, 120 frames = 14 s, so a 20-25 s diag window never held two stats lines.
  The plans now have 5 s gap-* phases (rt_stats=0 resets the core's 120-frame window) before diag-on and diag-off-b,
  and the share and clip gates are events (settle 1 s). Dryrun static checks pass; 13 phases, 9.8 min.

## Two-sided pool rows (RT_HIT_POOL_2S) and RT2 matching (branch hm-recipe-rows, READY_FOR_LIVE, 2026-11-02)
- Pool consumer (shaders/rt/native_hit_material.hlsli rt_hit_material_pool): with RT_HIT_POOL_2S + RT_HIT_INSTANCED, rows
  with h2.w = 5 flip the N column by nv_two_sided_sign (world triangle, interpolated N, ray), as the per-draw 2S rows.
  Every other consumer rejects h2.w 5 (fail closed, reason row). Variant bit kHitPool2s (common/shader_variant.h): only
  with instanced + wind RGB and without IP grass / OMM / tileset / scalp / hair origin: +45 requests (variants.txt 2922).
  The layer publishes `native-hit-pool-2s-v1` when the generated key starts on and a two-sided pool row exists.
- Generator: two-sided pool rows allowed; an RT2-only difference is accepted when the FS's RT2 equals an audited recipe's
  (UBO members erased, temporaries renamed, the RT2.w class/flag word folded) and takes its reflectance mode. 11 rows:
  + Base_2S_VT 2d815e2a and Base_AlphaTest_2s_Translucency 80d6fb8f (pool, two-sided), + ATM_Wind_Vertex_R_AlphaTest
  0289a872/55592de4 (wind RGB family). HLOD_TwoSided 3b346b42 (the inn's 19 draws) stays rejected: its RT2 normal-mix
  sample uses another page decode than 81657854's, so it needs a hand audit, not the flip.
- Job m4_generated updated for one rerun (window fix, IP grass pairing lines, `pairs \d+`); predicted.json rerun 2:
  camp +2.5 (1.5-4), inn +0.3 (0-1), bushes pairing diagnostic only.
- Remaining plan (for a fresh agent): (1) rerun m4_generated (needs the user's go); read the IP grass pairing line for
  the bushes (unread / wind inputs / placement / no prepass) and fix that certificate or the depth-family match;
  (2) skip capturing G-buffer draws of a generated pair that never pair (IP_Base_VT, 83 per camp frame): e.g. capture
  only when a depth draw of the same key was noted this frame; (3) audit 3b346b42 by hand (RT2 decode) as a new
  recipe; (4) promote only if the share gain is worth its GI ms (run 1: +2.3 points for +0.58 ms at camp).

## Item 3: skip unpairable IP draws (branch hm-recipe-rows, unverified: build pending, 2026-11-02)
- `deferred_begin` now requires an already noted, unique alpha source key for generated instanced and IP grass
  draws before bindings, staging and XFB allocation. It searches prepared slots and pending prepasses in the
  current frame; pending recordings must still be current. Opaque sources cannot qualify. The final pairing
  certificate and GPU checks remain unchanged. Deferred-first recording conservatively skips these rows.
- Target: the 83 unmapped camp IP_Base_VT draws (deferred stream 57 -> 93 MB in hgn1); expected to remove their
  stream cost, not a measured result. Generated / IP grass off: other audited capture decisions are unchanged.
- Extended `native_hit_generated_test`: missing, unassigned, unrelated, duplicate and reset source keys,
  a valid alpha key, IP grass gating and the unchanged audited wind / 2S policy. Not compiled or run.
- Later, after the compiler is free, from this checkout in PowerShell:
  `.\scripts\build.ps1 -Test`
  `ctest --test-dir build -R '^native_hit_generated_test$' --output-on-failure --no-tests=error`
- No build, CMake, CTest, DXC, shader tool or live run executed. A future authorized camp A/B must confirm
  fewer captured unmapped draws / deferred bytes without losing mapped alpha rows; GPU cost is unmeasured.

## Item 4: HLOD two-sided audit (branch hm-recipe-rows, unverified: build pending, 2026-11-02)
- Read-only DB audit: `Engine_Default_HLOD_TwoSided_VT` = 2d2ac3c38992b7f0 / 3b346b4241d44152;
  engine offsets 0/32/36, LayoutBase bindings 1:3/4/5/6, material CB 16 B, no Reflectance role.
- Cannot express its documented separate reflectance page decode with existing fields. NormalMix consumes
  shading normal `nl.x`; no independent mix-sample page selector exists. TRACK_D_REFLECTIONS section 14
  records the exact schema limitation and evidence limits. Exact alternative page bits / weight offset
  require a future shader-source audit; shader dumps were not read under this task's capture restriction.
- Corrected the misleading generator comment; native test pins rejection of this pair. Header unchanged.
  No feature added, no GPU cost measured. The 19 inn draws remain unsupported.
- Later verification (after compiler availability and permission to read shader dumps):
  `python -m tools.material_families.recipe_rows --check`
  `python tests/hit_material_generated_test.py`
  `.\scripts\build.ps1 -Test`
- Commit creation blocked: Windows ACL denies writes to this worktree's Git metadata, including index.lock.
  Both requested commit subjects must include `(unverified: build pending)` and the final trailer
  `Co-Authored-By: Codex <noreply@openai.com>`. No commit, push, game launch or capture write occurred.
```
