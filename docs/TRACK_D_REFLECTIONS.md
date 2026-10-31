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
