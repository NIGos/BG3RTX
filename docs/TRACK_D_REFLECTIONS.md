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
    fixture 8a/8b (12 rays, both sides), 73 CTests. Log (`rt_stats`): `[hit material instanced] frame F key K; wind:
    prepass N, G-buffer N (pipeline N, bindings N, stream N) captured N, mapped N rows N triangles; alpha-test 2S: ...`.
  - Risks: the wind VSs' non-`precise` terms may round differently (reason `clip` rises); two draws with one key in
    a pass are declined. Cost (not measured): XFB of the instanced G-buffer draws (64 B per vertex) plus 2x query
    copies, inside the existing caps; expected 0.2 to 1 ms on Ultra.
  - Live: `tools/live_batches/m4_instanced` (S14 Lower City: wind 9.7% + 2S 4.1% of the placed geometry), OFF/ON/OFF.
- **Live gate (pending).** Same run OFF/ON/OFF at S8 and S1, `rf_stats=1` hold >= 300 frames:
  mean (L_b - L_a) within z x its standard error, else tier (b) is biased and stays off; then
  hard_dark on tier (b) pixels and crops.
