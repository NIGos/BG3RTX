# Reflections: visible off-screen objects with conserved energy

Branch `rt-reflections-offscreen`, 2026-11-03. Design and candidate implementation; no live
qualification or preset promotion. This plan supersedes older future-tense water paragraphs
in TRACK_D_REFLECTIONS and PLAN_M5_WATER. Source line numbers identify the implementation
read for this branch; named functions remain the stable anchors.

## What is composed today

The audited BG3 deferred lighting and water shaders contain probe reflections, not a separate
SSR term. See [TRACK_D_REFLECTIONS.md](TRACK_D_REFLECTIONS.md), section 1, and
`shaders/gi/rt_reflections.hlsl:6`. The user's visible limitation is real: our RT ray can hit
geometry outside the image, but ordinary radiance resolution still needs a screen pixel.
Calling that limitation SSR does not mean an additional game SSR buffer must be subtracted.
This audit does not claim that every unaudited game permutation has been examined.

| Stage | Source and composition | Code anchor |
|---|---|---|
| Native opaque specular | Global cube pair, then parallax-corrected local probes; game's DFG and specular occlusion | `shaders/rt/bg3_ambient.hlsli:41` (`bg3_spec_ambient`), `:116` (`bg3_spec_occlusion`) |
| D-RF opaque RT | Reconstruct probe term `Tp = S * (b13[0] * 100 * SO)`, then `specular = max(specular + w * (E * Lrt - Tp), 0)` | `shaders/gi/rt_reflections.hlsl:216`, `:427` |
| D-RF screen hit | Reprojected previous lit radiance; edge fade, roughness fade and `rf_strength` set replacement weight | `shaders/gi/rt_reflections.hlsl:302` |
| D-RF hidden hit | Experimental `rf_hit_shade` calls `native_hit_shade`; optional `rf_hit_gi` adds hit AO/GI composition and the game BRDF | `shaders/gi/rt_reflections.hlsl:315`, `shaders/rt/native_hit_shade.hlsli:214` |
| Water diagnostic | Separate water primary TLAS, geometric wave normal, mirror ray; `wOut = (L, Schlick E)` for diagnostic views | `shaders/rt/water_reflect.hlsl:199` (`water_reflect`), `core/water.cpp:1436` (`water_rf`) |
| Water image | Forward FS records its actual shading normal and `A = G E`, `B = G k Ks`, `G = opacity * fog T * atmosphere X`; compose traces again and writes `max(C + A L - B, 0)` | `shaders/rt/water_compose.hlsl:1`, `:354` (`water_compose`), `common/water_fs_patch.h` |
| Water scheduling | Copy final opaque radiance/depth/camera at lighting; consume recorded forward fragments at pass end | `core/gi.cpp:3220`, `core/water.cpp:942` (`compose_at_lighting`), `:1075` (`compose_at_pass_end`) |

The water image does **not** read the diagnostic's Schlick approximation as its reflection
weight. Its own FS supplies the DFG weight and flow-mapped normal. Enabling only the diagnostic
would not add off-screen objects to the final water image; both traces need the hit path.
Water can cover an opaque receiver whose deferred D-RF was already changed. That deferred
reflection is the bed's reflection, not a second reflection term on the forward water.

## What a reflection pixel shows

| Hit class | Opaque ordinary RT | Opaque Experimental hit shading | Water before this candidate | Water candidate |
|---|---|---|---|---|
| Eligible screen hit, tier 1 / (a) | `E * Lscreen` replaces the weighted probe, blended at screen/roughness edges | Same output; optional tier (a) check computes hit shading without replacing the screen source | Current-frame copied opaque radiance replaces the FS probe | Same |
| Hidden on screen, tier 2 | Native probe | Qualified material and complete supported lighting: `E * Lhit`; otherwise probe | Native FS probe | Same hit shader as RF on a qualified, fully supported row; otherwise probe |
| Off screen, tier 3 | Native probe | Hit shading only if its material point still passes the light-grid domain; otherwise probe | Native FS probe | Mode 1: native grid. Mode 2: bounded current-frame world query (C); unsupported hits retain probe |
| TLAS miss, tier 4 | Native probe | Native probe | Native FS probe | Native FS probe; directional sky is a later stage |
| Aborted / invalid | Native probe | Native probe | Native FS probe | Native FS probe |
| Ineligible receiver, rough water, below-surface ray, invalid/unowned forward record | Native game lighting | Native game lighting | Original forward colour | Original forward colour |

Tier numbers are not identical between every shader: D-RF uses tier 5 for below-horizon and
6 for aborted; the water histogram uses 5 aborted and 6 invalid. Preserve the geometric tier
when shading succeeds and count success separately. Otherwise the tier histogram hides the
actual off-screen workload.

A qualified material alone is insufficient. `native_hit_direct.hlsli:140`
(`native_hit_cluster`) explicitly requires projection inside `[0,1)` and a valid froxel slice.
`native_hit_shade.hlsli:223` performs that check before costly VT decoding. A genuinely
out-of-frustum hit therefore keeps the probe even after step A. Step A (mode 1) chiefly resolves hidden
on-screen objects. Mode 2 adds the bounded world query in C; its current-frame
light union can illuminate off-grid hits but is not a full active-world provider.
The current TLAS also contains captured draws, not an arbitrary persistent world. An object
never captured or culled from every contributing pass cannot appear in a reflection.

## Staged implementation

### A. Water material hits, disabled candidate

Use `rt_water_hit_shade=0`, off in every preset. Reuse `rt_closest_hit` and `native_hit_shade`
from RF for tiers 2/3 in both water consumers. Take radiance only on `kHitShadeOk`; unpublished
rows, missing material resources, unsupported lighting, sun/roof uncertainty and non-finite
results keep the original probe. Do not turn unknown radiance into black. Preserve the
geometric histogram and add attempted/shaded/material/lighting failure counts to the existing
device-local water statistics, with group reduction and readback outside timed regions.

The runtime switch is Vulkan SpecId 1016 in `common/shader_variant.h`, after compose shift
1015. Cascade 1000, native hair 1008/1010, edge 1014, shift 1015 and hit shading 1016
share one pipeline specialization map and cache key. `WATER_HIT_SHADE_LAYOUT=1` adds
material bindings 21-29, borrowed game set 2 and sun set 3, so it remains a layout define.
This capability adds 9 requests to the bundle's 147 (156 total), not an on/off cross product.
Ordinary requests remain unchanged with the key off. Mode 2 adds device-local
world membership binding 31 within this capability and one preparation request (157 total). The compose reflection ray starts at
`compose_scene_origin(o)`; hit shading receives `compose_scene_origin(H)` in that same TLAS
camera space. Screen projection still uses the current-space H. The capability histogram
uses eager pure predicates so specialization matches the fixed-key reference without changing
the tier counts. Compose flag 64 controls
hit shading separately from shift flag 32. Borrowed descriptors still require exact frame/TLAS
identity: earlier-lighting passes retain ordinary composition, never stale lighting sets.

Keep water's `C' = C + A L - B` equation. Never add `A L` without removing `B`, never apply E
or exposure twice, and never rescale L to force the image mean. If `A L == B`, replacement is
an identity within target/record encoding precision. Unsupported hits are exact identity.
Energy-preserving algebra does not prove the radiance estimator unbiased: this candidate
inherits the unresolved RF bias and remains Experimental until measured.

Offline: compile only affected water requests through `tools/shader_identity.py`; validate
descriptor use and key-off identity. Inspect the S8 replay manifest before executing it:
opaque GI resources do not establish that it contains a water TLAS, forward records, post-water
target and the matching lighting/material sets. A pack missing them cannot qualify water.
Do not manufacture water rays or claim an opaque GI replay is a water result. Inspection of
`replay-20261101-142819-1` reports **NOT_COVERED**: its GI-direct manifest has no water TLAS
binding 64 or forward-record binding 66. It cannot run or qualify this water candidate.

The live draft is in `tools/live_batches/water_offscreen`, outside watched inboxes.
Dependent implementation/identity checks must pass before marking it READY_FOR_LIVE.
Prepare, without queueing, one S8 port launch with static OFF/ON/OFF/ON repeats and matched
turns with the key toggled inside that launch. Write `predicted.json` first. Measure the
post-water RR colour input in floating point with its exposure scale, and check the in-frame
water compose delta. The pre-water lighting dump and bloom PNGs are not energy observables.
Derive the allowed mean band from OFF repeats and encoding uncertainty; require every ON
window to pass. Use existing GPU water reflection/compose ms, not unfocused frame time. The
job must include the real Ultra combination as well as isolated windows, with no concurrent
build or shader compile in cost windows. No game launch is authorized by this plan.

### B. Directional sky/environment on proven outdoor misses

Read the game's global specular cube pair (`bg3_ambient.hlsli:25`, b35/b37), sampler b16 and
its scales/blend (`lConsts.c[31].zw`, `c[32].x`). Add a helper that accepts the **exact reflected
world direction R**, rather than deriving a new reflection from a different normal. Use the
cube's audited sharp mip convention (the existing `a * 8 + mipOffset - 8` expression), then
match the ray's lobe/footprint for rough reflection. Multiply the resulting radiance by the
same `b13[0] * 100` lighting scale. Apply the receiver's DFG once during composition. Do not
sample diffuse irradiance cubes b34/b36 for a mirror reflection. Cube radiance is the game's
environment estimate; matching its direction does not reconstruct moving clouds or a sun disc
that were never captured in that cube. Those need the game's atmosphere/sky shader inputs.

A scene-TLAS miss is not proof of open sky: cut-away roofs, range limits and missing geometry
also miss. First test the available cascade-caster geometry/roof trust contract along R;
untrusted indoor or roofed rays retain their local/native probe. The S8 pack's cascade
entries have zero triangles (GAME_DATA_AUDIT, action 3), so it cannot validate that guard.
Gate outdoor sky direction and scale against the same-frame game environment source, including
cube blend states, and prove enclosed rooms do not acquire sky. Keep local-probe replacement
separate: a miss may represent intentional interior probe lighting.

### C. Bounded world query candidate (mode 2)

`rt_water_hit_shade=2` extends SpecId 1016; modes 0 and 1 keep their contracts.
The Experimental GUI exposes this mode; every preset remains off. No ABI change.
`water_light_prepare.hlsl` builds a 4096-slot current-frame point/spot membership
and type union once at lighting, before the water traces, in device-local memory.
It scans only the current grid dimensions, validates list bounds and rejects an
inconsistent type. Transfer clear -> compute build -> compute readers are ordered.
The preparation is included in reflection GPU timing. Borrowed frame/TLAS and
forward/opaque light-buffer equality checks still apply to compose.

**Provider limit:** the premise that every b25 slot is a current active light is
false for this renderer. ASSET_DB.md measured 1,182 valid-looking slots versus 39
listed in Grymforge; unlisted slots contain other frames/places. This candidate
uses all currently listed point/spot lights as a world-space source, independent
of the hit's projection. Such lights can reach off-screen hits. It does not revive
unlisted slots, infer spot type from cone values, or substitute story-unsafe DB
lights. A verified full active-light provider remains necessary for lights absent
from every current froxel. World projector and local-probe queries remain gaps.

At an out-of-grid qualified hit, `water_world_lights.h` tests channel and authored
bounding spheres, then selects nearest centres with stable slot-id tie breaks.
K = 32 bounds local visibility rays to one subgroup's lane count, 32 times below
the existing 1024-light froxel cap. This is a work budget, not an accuracy cutoff:
if more than K spheres overlap, keep the original probe and count truncation.
No partial nearest sum is silently accepted as full energy. Both consumers use
the existing wave -> group -> device-local statistics and asynchronous readback:
reflection word 31 / compose word 55, logged as `world truncated`.
Malformed lists, unknown visibility, material and roof failures also keep probe.

Local terms reuse authored light amplitudes, BRDF, visibility and D-LS policy.
Sun reuses `native_hit_sun`, including its roof/cascade contract. Sky specular
samples the global cube pair along the exact reflected world direction at the
hit, `reflect(R, material.normal)` for outgoing `-R`; sharp/rough mip, blend and
lighting scale follow the audited native convention. Diffuse uses irradiance at
N, with native DFG/AO factors. Receiver DFG remains applied only in water compose.
This is a global environment approximation, not a world local-probe replacement.
No TLAS miss acquires sky in this step. Missing captured geometry remains a gap.

Offline culling/selection tests cover empty, boundary, invalid/channel cases,
nearest sorting, ties, truncation and shuffled 4096-slot populations. The live
draft toggles 0/2/0/2 and predicts offscreen shaded > 0 in every ON window, equal
static mean within OFF noise/encoding uncertainty, and added median/p90 GPU cost
<= 1.0 ms in matched diagnostic-free windows. Cost is unmeasured. No launch or
queue submission is authorized; this is not a quality pass or preset promotion.

For opaque receivers continue subtracting only the audited `Tp` term, weighted by w. At w=1
the RT estimate replaces the native probe; at 0<w<1 it blends two estimates of the same term;
at w=0 the probe stays. For water replace only recorded B with A L. There is no audited SSR
term to add or subtract in these paths. If a future game permutation exposes an actual SSR
term, identify its composition and confidence mask first, then subtract/blend exactly that
term; stacking SSR and full-weight RT would double-count reflected energy.

### D. Fix RF bias before promotion

The user's gate is unchanged: tier (a) `L_b / L_a` must lie within the game's own repeat
spread. Existing failed ratios are S8 0.925, S6 1.089 and camp 1.097. Do not lower the threshold
or apply a global gain. S8 has missing indirect diffuse; S6 previously used a different direct
BRDF. `rf_hit_gi` now shares the screen composition/game BRDF, but it stays off: aggregate
historical explanations do not prove the remaining instanced-row gap is fixed.

1. Obtain an offline pack with the live instanced material rows and matching AO/GI inputs.
   The current handoff says the reproduced non-instanced hit AO matches the screen; the
   unresolved gap is in instanced hits. Replaying a pack without those rows cannot refute it.
2. Compare the same tier (a) point with mode 2 screen AO/GI, then mode 1 estimated AO/GI.
   Split by pool/instanced family and record validity; compare barycentric position, geometric
   and shading normal orientation, two-sided flip, transform/instance remap, footprint and AO
   ray origin. This tests the instanced-row AO gap without compensating its energy elsewhere.
3. Use existing direct/sun/ambient diffuse/specular/GI term counters and RF boxes line. Separate
   expected outgoing-direction specular changes from a diffuse/AO mismatch. Screen derivative
   roughness and emission/recursive-reflection differences must be compared on like sources.
4. Treat material coverage as a separate provider gate: roughly 25-63% in the supplied scenes,
   with the latest S8 wind-RGB figure 63.5%. Missing rows retain the probe. Rank unsupported rows
   with existing blocker tools; add only audited recipes and verified per-instance joins.
   Never count more attempted rays as more qualified off-screen objects.
5. Re-run S8, S6 and camp in one launch per save with OFF repeats, all real-preset keys and every
   ON window gated. Require equal mean energy within the derived noise band, visible hidden
   objects following the reflection geometry, and no systematic tier (a) bias.

## Cost and preset decision

The supplied RF hit-shading cost is about 2.3-2.7 ms in its measured context; older TRACK entries
quote different isolated pass/frame deltas and must not be mixed into a water prediction.
The current qualified water chain is 0.53-0.67 ms total. S8 water's supplied histogram is
16.8% screen, about 18% hidden, 24% off screen, 41% miss. Step A can only shade a subset of
that 42% hidden/off-screen population, bounded by row coverage and the froxel restriction.
The incremental water cost is **unmeasured**, not 2.3-2.7 ms scaled by those percentages:
divergence, VT reads and shadow/AO rays prevent that inference. Report reflection and compose
GPU median/p90 separately, and total added GPU ms from matched diagnostic-free windows.

Ultra is the first possible shipping preset, after energy, visibility, stability and combined
smoke gates pass. Until then the key stays off in every preset and the feature is Experimental.
No lower preset promotion, live quality claim or GPU-cost claim follows from an offline compile.

