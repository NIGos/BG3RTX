# Path tracing feasibility and rendering ceiling

Offline study, 2026-10-30. Proposal only. No runtime feature is implemented here.
2026-11-01: a measured offline PT reference on a replay pack and the real-time decision (next section).

## Measured reference and real-time decision (2026-11-01, M6 offline)

**Tool.** `frame_replay --pack <dir> --pt-reference` runs `tools/pt_reference.hlsl` (tools/frame_replay.md
"PT reference"): raster G-buffer primary, cosine paths, up to 8 vertices with Russian roulette, NEE at
every vertex (froxel point/spot lights through `native_hit_direct` + the sun, RT shadows), the declared
FresnelBlend BSDF (diffuse + GGX), native hit materials where they qualify, else a declared fallback
(albedo 0.5, roughness 0.5). It estimates giRaw.rgb's quantity (mean incident radiance from surfaces),
split over the same paths by first hit, distance and light. CTest `pt_reference_test`: furnace exact
(1, 1.75), Russian roulette unbiased (5.00 within 4 SE), FresnelBlend energy bounded, dark control 0.

**Pack.** S8 Water Queen's House, frame 4268, 2560x1067, GI full res, 2 rays of 1 m (`gi_rt_radius`),
`gi_hit_direct` on. The 2 GB VT hit cache is not in it: 0% of PT hits qualify, every hit uses the
fallback (the live run had them: about 17% at S8). 256 paths per pixel and mode, 11.6 s GPU.
Not in the pack, so 0 in the PT: sky/environment (64.2% of first rays miss), emission, projector
lights, clouds (off this frame), the transmittance LUT (estimated: Kasten-Young air mass, T = 0.73 /
0.63 / 0.45). Calibration k = game radiance / PT radiance on the visible first hits of the same paths:
0.49 (albedo 0.5), 0.87 (albedo 0.3). Shares below are scale-free; they move by at most 1 point between
the two albedos, except multi-bounce.

| Of the PT's traced bounce energy (excludes sky) | albedo 0.5 | albedo 0.3 |
|---|---:|---|
| Sun / local point+spot lights | 98.9 / 1.2% | 98.9 / 1.1% |
| Multi-bounce (vertex >= 2) | 20.9% | 13.9% |
| First hit visible on screen (near 1 m / beyond) | 22.2% (7.5 / 14.7) | 21.2% |
| First hit on screen but hidden (near / beyond) | 48.3% (6.7 / 41.5) | 48.7% |
| First hit off screen | 29.5% | 30.1% |
| Live domain: first hit within 1 m, with its fade | 11.6% | 11.2% |
| Of the live domain: hidden or off screen (fallback0 adds 0) | 44.5% | 45.4% |

**What the real-time path gets right and misses.**
- In its own domain the live GI matches the calibrated reference: live / (k x PT within 1 m) = 0.97
  (0.96 at albedo 0.3). The screen tier is 0.0070, and the in-game `gi_hit_direct` adds 0.0014
  (17% of live), which is what the 44.5% hidden share needs.
- The rest is outside the 1 m ray: 56-86% of the traced bounce energy comes from first hits beyond
  1 m (56% from on-screen hits beyond 1 m, plus up to the 29.5% off screen; the off-screen part is
  not split by distance). Live giRaw is 11% of k x PT total. In game that part is the native
  probe/SH ambient x visibility (live a = 0.89), not in the pack, so this run cannot say whether
  the native term is too bright or too dark. That is the next missing observable.
- Per region (16 x 9 tiles, PT x k): live / PT median 1.17, p10 0.22, p90 13.5, mean |error| 84%.
  The one global k cannot fit per-material albedo: region numbers need the pack with the VT cache.

**Cost, measured on the RTX 5090 on this pack** (`pt cost` lines, 1 spp, sun + local NEE at each
vertex, median of 9 dispatches; a first run under other agents' GPU tests gave 4.2 / 15.4 ms for
the 300 m rows, so treat these as +-2x):

| Kernel | ms per Mpx | 4K DLSS Performance (2.07 Mpx) | Quality (3.69 Mpx) |
|---|---:|---:|---:|
| 1 vertex, rays 300 m | 1.36 | 2.8 | 5.0 |
| 2 vertices, rays 300 m | 2.29 | 4.8 | 8.5 |
| 1 vertex, rays 8 m | 0.88 | 1.8 | 3.2 |
| 2 vertices, rays 8 m | 0.97 | 2.0 | 3.6 |

Today: gi 1.7-2.6 ms (2 rays of 1 m, full res for RR), rt passes 1-3 ms each; SHaRC queries at 4 m
cost +0.33 ms at Campsite. A ReSTIR PT / 1-2 bounce path tracer at 1 spp = the 2-vertex row plus
reuse (temporal + 1-2 spatial reconnections, about one extra visibility ray each: +60-100% of the
1-vertex row) plus history/denoise in RR: about 6.5-9 ms at 4K Performance, 11-15 ms at Quality with
300 m rays; 3.5-5 ms (Performance) with 8 m rays and a cache tail. That is +2 to +7 ms over today's GI.

**Decision.** No real-time PT now. Close the largest measured gap with increments on the hybrid
path, each off by default and gated against this reference in one run:
1. Far-field sun bounce (largest: 56-86% of traced bounce energy, 99% of it sun in this scene):
   lengthen GI rays to 4-8 m and give hidden/off-screen hits the sun (`gi_hit_sun`, merged, off;
   roof rule kept), removing the native ambient only for rays that hit. Cost about 1 vertex at 8 m:
   ~1.8 ms at 4K Performance, minus today's 1 m pass. Prerequisite: a hit material at those hits
   (live qualified ~17% at S8): either more recipes (M3a) or a declared fallback material, checked
   with this tool's k on the VT-cache pack.
2. Multi-bounce (14-21%): traced SHaRC updates (v2) from those hits, not longer paths per pixel.
3. Local lights (1% here): decide on an interior or night pack (S3/S6) before investing.
4. Full ReSTIR PT only if 1-2 leave a measured gap worth +2 to +7 ms, and after world closure,
   sky/ambient replacement and roofs have inputs (sections below).

**Roof correction (2026-11-01, audit).** The pack has `rt_cascade_casters=0`, so the roofs the camera cut away are
not in its TLAS, and S8 is mostly under them. `--define PT_ROOFS=1` (roof volumes from `tools/pt_roofs.py`;
sun rays through a padded volume occluded, path rays end at the roof plane): PT total 0.152 -> 0.0111, sun
98.9 -> 84.4%, on-screen first hits beyond 1 m 56.2 -> 32.7% (off screen 29.5 -> 59.8%, not split by distance),
first-ray miss 64.2 -> 28.2% (+ 37.5% roof), k 0.49 -> 10.3. The numbers above are upper bounds of the far
field; step 1 (`gi_far`, docs/TRACK_C_GI.md) is built energy-safe and waits for a casters-on, VT-cache,
native-ambient pack before promotion.

**Full S8 pack (2026-11-01 evening).** `replay-20261101-142819-1`: casters on (but the caster instance carries no
geometry in the pack, so `PT_ROOFS=1` is still needed), VT cache, lighting inputs 19 of 19. With roofs, real
materials, env and the sun LUT: far field 53.6% of the traced bounce on screen beyond 1 m (visible 15.2, hidden
38.3), off screen 35.8% (not split), live domain 8.4%; sun 70.3 / local 29.7%; k = 15.2. Native ambient vs the
reference: E x AO / (k x PT far + env) 1.44 (median tile 1.75), so the native term is brighter than the traced far
field + sky, not darker (the 0.66 / 0.70 without roofs was sun through cut roofs). The far field passes the 30%
share bar, but its traced part is small next to the native ambient (k x PT far 0.081 vs E x AO 0.443).
Step 1 (`gi_far`): emulated exactly offline (the live `GI far` line within 2%); it darkens S8 by 2% toward the
reference, but live S8 brightened 2.9%: unexplained downstream of giRaw, so it stays off (docs/TRACK_C_GI.md
"Go / no-go on the full pack").

Next measurements: one interior night pack (S3/S6) for the local-light share, and the live stage check above.
The writer now captures those inputs (sky cubes, probes, DFG, transmittance LUT) and
`frame_replay --native-ambient` makes the comparison (tools/frame_replay.md "Native ambient"); the S8
number needs one new pack.

## Decision

No inspected evidence establishes that path-traced lighting is fundamentally
impossible in BG3. Archive data provides a credible route beyond camera-submitted
geometry. This does not establish complete scene reconstruction or playable cost.

Recommended: extend the hybrid renderer, then qualify path-traced opaque lighting
in a bounded scene with raster primary visibility. Treat a complete replacement
covering every level, dynamic object, material and medium as a separate, high-risk
renderer project. Do not promise that result or switch the current release to it.

There ARE reasons to reject a shortcut: camera captures plus retention plus a
screen-fed cache cannot supply an unseen world, current dynamic state, complete
secondary materials or missing light state. Adding bounces cannot fix those inputs.

## Scope and evidence strength

Read the current handoff/progress, architecture, coverage, Track D, SOTA design,
engine-data audit, asset DB, SE bridge and P6 water design, and inspected the actual
capture, material, reflection and cache code. Older roadmap/status tables are
historical; implemented, merged, enabled and visually qualified are different states.

Fresh evidence: [probe JSON](evidence/pt-feasibility-probe-20261030.json), produced
by [read-only probe](evidence/pt-feasibility-probe-20261030.py). It reads installed
Data packages only. It records package fingerprints, Git revision and source hashes.
It writes only its evidence JSON. No saves, game process, input, live settings,
shaders, DLLs or builds were accessed or changed by this study.

Primary upstream source was checked for format support and exposed interfaces.
That is documentation verification, not running the game or proving compatibility
with this install. This study did not export a GR2 mesh, decode a VT tile, reconstruct
a room, execute a GPU pass, measure performance or establish visual quality.

## 1. Fresh package findings

The installed archive index contains 27 packages and 566,751 effective paths.
It contains 154,273 GR2 paths, including 19,037 HLOD paths, and 3,819 terrain
patch paths. These are file counts, not unique meshes, placed instances or pixels.
Differences from earlier audit totals reflect counting scope and must not be
interpreted as newly recovered geometry.

For the foundry sub-level BGH_SteelWatchFoundry_B:

| Category inspected | Records | Result |
|---|---:|---|
| Scenery | 2,315 | All resolve through templates to a resource ID |
| Ordinary VisualBank resources | 209 distinct | All referenced mesh paths exist; 185 distinct GR2 paths |
| Material references from those visuals | 79 distinct | All exist in inspected MaterialBank records |
| Remaining resource IDs | 3 distinct / 40 placements | All found in EffectBank: 35 steam, 4 sparks, 1 dust |
| Tile constructions | 627 | Every record has construction lines/points/spline/fillings and tiles |
| Items | 144 | Inventory and use/state-related data also present; not resolved as static scene geometry here |
| Terrain | 1 | Dedicated visual/layer data, not a simple ordinary scenery join |
| Instance files | 9 | Additional LSF paths; contents not reconstructed by this probe |

The three references initially absent from VisualBank are classified effects,
not evidence of missing mesh archives. The other 2,275 scenery placements resolve
to ordinary visual records. This is reference availability, NOT 98% scene or pixel
coverage. The probe has no draw-to-placement association or rendered comparison.

All 3,183 inspected content-bank LSF files parsed with zero read errors. Four
sampled GR2 bodies (three ordinary resources and one HLOD) have version 7 headers
and in-bounds section storage; their sections include compression type 4. This
is not decompression or topology validation. Upstream LSLib has a GR2 reader with
section decompression and GR2/DAE/GLTF/GLB export paths. A tested local export is
still a required milestone. Sources: [reader][gr2], [exporter][export].

Virtual textures: 16 GTS files, 1,318,060,032 logical bytes; 12,974 GTP files,
82,948,653,056 logical bytes. The sampled Albedo_Normal_Physical_0 GTS is GRPG v5,
three layers, nine mip levels. LSLib implements page lookup, lazy page-file load
and BC3 texture extraction. [Source][vt]

This refutes a claim that offscreen texture data necessarily does not exist.
It does not prove correct material association, sampling or streaming. Those
83 GB of GTP storage are not a VRAM requirement: a useful implementation must
stream bounded working sets. No working-set size was measured here.

Limitations: the probe uses the repository's package resolver and lexical last
match for duplicate bank IDs. It is not an independent proof of engine override
precedence. Installed loose overrides, separate Mods packages and arbitrary mod
content are outside its completeness claim.

## 2. Structural obstacles and their consequences

### Scene closure: feasible inputs, incomplete reconstruction

The ordinary scenery join is encouraging. A general level is more than placed
GR2 meshes: constructions have tile selection and possible deformation, terrain
has its own patches and splat materials, and grouped instances need decoding.
Those 627 construction records are direct evidence against assuming a universal
placement-times-mesh baker. Their presence does not prove they are unrecoverable;
explicit tile children may provide much of the required information.

The existing tools/assets/geom.py Transform stores uniform scale and averages a
vector scale if encountered (line 59). Reusing it unchanged for arbitrary mesh
placements would lose nonuniform scale. No affected foundry object is claimed;
this is a demonstrated limitation of the proposed helper, not a runtime bug fix.
A world baker needs affine transforms, correct normal transforms, mirrored
winding, nested placements, material overrides and supported deformation rules.

HLOD is a promising far-field proxy, not an exact replacement for nearby walls,
thin geometry, openings or mirror detail. Inspect whether proxy materials contain
baked illumination before using their colour as reflectance for further bounces.
Collision meshes likewise cannot certify visual occlusion or shading.

Current capture precedes clipping but follows CPU draw selection. Capture from
more shadow passes extends coverage; it cannot guarantee objects never submitted.
The current rt_dyn_hold path in layer/bvhsplit.cpp is bounded to 0-120 frames
(default 4), is used only while the camera is stationary, and invalidates on
motion. It is not the general persistent world described by the old EN3 proposal.
Cached BLAS geometry also does not imply that a valid instance remains in TLAS.

### Dynamic state: a serious blocker for whole-game correctness

Archive placements describe authored state. They cannot by themselves tell us
that a door is open, an object was moved/destroyed, a sub-level is inactive, or a
character changed pose/equipment. A stale object can block light indefinitely.
Duplicate runtime and offline geometry can double surfaces or leave ghost blockers.

The bridge currently exports lights and environment metadata, not complete object
state. Its client interval is 250,000 microseconds (4 Hz) over 60 m around the
controlled character; placed-light state is refreshed in quarters, at about 1 Hz.
Those existing policies cannot certify render-frame animation or fast light
changes. GPU light records remain preferable for exact current visible state;
the bridge/database supplement missing world state with explicit age and coverage.

Upstream SE exposes useful transforms, models and applied materials, but scene
internals, GPU buffers and pose-related structures include hidden members. Schema
availability is not a verified, frame-synchronous export for culled objects.
[Source][render] The currently installed v33 mappings need separate validation.

Required: stable instance identity, spawn/despawn and streaming state, per-frame
authoritative transforms/deformation where needed, epoch invalidation and GPU
completion lifetime. Descriptor handles and position-only matching are insufficient
identities. Widening CPU culling or native hooks may be necessary for unsupported
dynamic classes; neither is proven safe or cheap by this offline study.

### Material evaluation is not yet a complete BSDF system

Current rt_common.hlsli supplies alpha/native-scalp coverage and geometric hits.
Current rt_reflections.hlsl explicitly retains probes for hidden/offscreen hits
without the SHaRC variant; full hit-material tier (b) is not implemented there.
The old EN2 plan allows average colour/roughness and geometric normals. That is
a useful hybrid fallback, but insufficient for material-faithful path tracing.

The generic Bg3MaterialInfo API keeps the first parameter of each classified role.
Multi-layer, gradient, vertex-colour and material-specific formulas require more
than that table. Native fragment programs exist, but reconstructing their inputs
requires UV sets, tangent frames, vertex colours, deformation, runtime overrides,
filtering and VT residency. Ray footprints do not automatically reproduce raster
quad derivatives. Shader transplant is a large compiler/integration effort,
not a ready universal solution.

bg3_materials.hlsli evaluates native light contributions. It does not constitute
a complete sample/evaluate/PDF interface for multi-bounce transport. Skin uses a
primary screen-space SSS convention; hair uses specialized light evaluation.
Reproducing native direct lighting does not prove energy conservation, reciprocity
or correct secondary transport. Test integrated reflectance and multi-bounce
stability before reusing empirical lobes recursively. No divergence is measured
or asserted here.

### Lighting and artistic intent cannot be inferred from radiometry alone

The asset DB and bridge provide a credible offscreen-light route, but do not yet
certify all loaded lights, their timing, cookies, channels or environment blending.
The 4,096-entry GPU array is not a global authoritative light list: the audit
already found unlisted slots from other places/frames.

Many box/fill/character lights are artistic rigs or stand-ins for indirect light.
Baked local probes may already contain the lighting being explicitly traced.
Emissive fixtures may also have paired analytic lights. Blindly summing all three
can double energy; shadowing every fill can spoil dialogue readability. Point
light emitter radii are currently fixture-name estimates, not authored physical
emitter geometry (SE_BRIDGE.md, Source radius).

Consequently, a physically coherent relighting can be beautiful while differing
from the authored image. Preserve a native-compatible mode first. Any physical
mode needs explicit emitter/rig/probe policy, including which sources illuminate
secondary hits. Exact native appearance and removal of all nonphysical authoring
choices cannot both be acceptance requirements for the same mode.

### Cut-away roofs impose a visibility policy

Restoring a hidden roof for transport can correctly close an interior, but hiding
that same roof from a mirror is an authored visibility choice. Treat transport,
primary visibility and reflection visibility separately and state the compromise.
Do not call a ray miss open sky until relevant coverage is established. Room
volumes or the shadow atlas are useful guards, not substitute surface radiance.

### Composition, special effects and temporal reconstruction

The split diffuse/specular lighting and existing pre-SSS/fog hooks support replacing
opaque lighting without changing gameplay or primary raster visibility. Preserve
the skin white-diffuse convention if native SSS remains. Remove native probe/AO
terms only where their replacement is accounted for. Native atmosphere, fog,
emission and forward effects must be applied exactly once.

Water and glass are missing interfaces, not merely more opaque geometry. P6_WATER
already specifies reflection/transmission, masks, medium data and history gaps.
Billboard VFX may depend on view direction, scene colour and native soft depth;
literal secondary-hit execution can be undefined or visually wrong. Their presence
is a reason to keep a declared hybrid scope initially, not evidence against opaque PT.

SHaRC v1 injects gDiffuse at selected visible pixels (sharc_inject.hlsl:39), including
previous-cache GI feedback. It is not an independently traced world solution.
A diffuse radiance cache cannot supply detailed directional mirror radiance.
Current code already restricts its glossy use by roughness/cone footprint.

RR cannot recover missing surfaces, resolve double counting or prove energy
preservation. Primary motion vectors do not fully describe secondary reflection,
refraction and deformation motion. Full primary-ray PT would additionally need
consistent depth/MV/material guides. Raster primary visibility avoids that extra
burden initially; it does not eliminate secondary temporal validation.

Current capped queries expose aborted/unknown results. In an incomplete hybrid
scene, a native fallback is defensible. A path tracer cannot silently treat every
unknown as black/occluded and claim an unbiased result. Reference mode needs
complete traversal or explicit invalid-sample reporting; real-time approximations
must be compared with that reference.

## 3. How far to go, and what is actually beyond the plan

| Target | Relationship to current plan | Recommendation |
|---|---|---|
| Correct hair/cascade coverage and sun replacement | Current pending correctness work | Finish first; known visible defects |
| Offscreen shaded reflections, emissive bounce, traced SHaRC updates | Already planned, not fully qualified | Strong next quality targets after inputs pass |
| Water reflections/lighting, cloud-aware sun/misses, volume shadows | Already designed or catalogued | Strong scene-dependent gains; stage separately |
| Persistent, streamed static scene plus authoritative dynamic reconciliation | Promotes old distant backlog into a first-class scene provider | Best substantial extension; benefits every RT effect |
| Shared material evaluator and world-light provider for all ray hits | Stronger contract than approximate EN2 and view-cluster EN1 | Necessary extension for faithful long-range transport |
| Path-traced opaque lighting with raster first hit | D-PT becomes an optional renderer rather than debug only | Conditional target after one-room proof |
| Whole-game surface PT including dynamic unseen characters and transparent interfaces | Beyond the qualified plan | High risk; do not promise before coverage/state proof |
| General volumetric PT, complex caustics and exact transport through all effects | Beyond existing volume-shadow/refraction plans | Poor initial value/effort; defer |

The practical modern-rendering ceiling worth pursuing is consistent direct/indirect
light, emissive colour bleeding, stable reflections beyond the screen, correct
contact/penumbra behaviour, and water integrated into that light transport. These
can provide major visible improvement while keeping native SSS, selected effects,
volumes and primary visibility. Full primary-ray replacement is not required to
obtain those benefits or to path trace opaque surface lighting.

## 4. Smallest evidence-driven extension

Do not interrupt current concrete coverage fixes. Afterwards use short, default-off
steps, with a reviewable result at each gate. This is a proposed order, not a new
implementation commitment or permission to run the game.

1. **Offline asset gate.** Export one ordinary GR2 and one HLOD, checking indices,
   UVs, tangent frames, LODs and materials. Reconstruct a construction record and
   terrain patch. Decode a selected VT tile and its mapping to the material.
   Discover the native transform precision before choosing geometric tolerances.
   A successful mesh export alone does not pass the room gate.
2. **One-room scene gate.** Bake a bounded room, roof, opening and surrounding
   blockers; compare against retained XFB evidence where available. Establish
   world placement and no duplicate surfaces. Include geometry never visible
   to the test camera and texture residency independent of camera requests.
   Existing captures may lack the object identities needed for this comparison;
   if so, record that missing evidence instead of inferring a match.
3. **State gate, later user-coordinated live test.** A moving/open door and a
   removed object must update visibility and transport without ghosts. Test
   streaming, camera rotation, teleports, cut-away transitions and load reset.
   If current interfaces cannot provide adequate dynamic state, limit scope to
   certified static transport; do not conceal stale objects with retention.
4. **Reference-lighting gate.** Use a simple unfiltered, cache-free integrator
   over the verified scene/material/light subset. Start from G-buffer primary
   hits; provide BSDF evaluation/sampling/PDFs, explicit light sampling, emission,
   MIS and energy-correct path termination. [Algorithm reference][pbrt]
   Increasing sample count must converge; missing/unknown paths are reported.
5. **Real-time gate.** Compare RR and traced-cache variants against that same
   reference in one run. Check unchanged-model mean energy within uncertainty,
   motion/disocclusion, bright small emitters, reflections and dark interiors.
   A real lighting-model change may legitimately change brightness versus BG3;
   denoising, rejection, reuse and optimizations must preserve their estimator.
6. **Expansion gate.** Qualify foliage, skin/hair, decals, water, glass, VFX and
   additional levels individually. Unsupported categories remain explicitly native.
   Derive limits from repeat noise/encoding/footprints, not visual tuning.

The same scene gate benefits EN2, reflections, sun and GI even if PT stops there.
That is why it is a better first investment than replacing the entire renderer.

## 5. Stop conditions and fallback value

| Evidence found at a gate | Decision |
|---|---|
| Static opaque room, mesh/material mapping and transform agreement pass | Proceed to bounded opaque PT reference |
| Constructions/terrain remain approximate | Keep proxies for justified distant effects; no exact closure claim |
| Dynamic offscreen state cannot be synchronized | Reject general full-scene replacement; retain hybrid lighting and verified static scope |
| Material/light providers change with camera orientation | Fix provider first; a cache or denoiser is not a pass |
| Baked/rig energy cannot be separated reliably | Retain native contributions by term; label relighting approximation |
| Real-time error or measured cost is unacceptable | Keep reference offline/static and deliver improved hybrid effects |

There is no demonstrated hard impossibility in the inspected asset formats.
There is demonstrated insufficiency in the current scene/state/material pipeline
for whole-game PT. The largest risk is correctness of world reconstruction and
dynamic synchronization, followed by material/lighting semantics, then cost.

The historical 5-8 ms PT estimate and Track D's old 2.5 ms budget do not qualify
this expanded renderer. Current short-ray timings cannot predict long paths,
more alpha candidates, material sampling, emissive sampling, streaming and extra
BLAS work. Measure capture/build, transport, cache, reconstruction, CPU time,
peak memory and pacing separately, with heavy statistics disabled for timings.
No FPS, VRAM or delivery-date claim follows from this offline study.

## Validation performed for this report

The probe completed with zero content-bank read errors; missing VisualBank IDs
were resolved to their EffectBank types. Header sections were checked for file
bounds, not decoded. The evidence records its limitations and sources. No runtime
tests or shader/CTest builds were appropriate for this documentation-only study.
The current renderer, feature defaults and active agents' implementation files
were not changed.

[gr2]: https://github.com/Norbyte/lslib/blob/master/LSLib/Granny/GR2/Reader.cs
[export]: https://github.com/Norbyte/lslib/blob/master/LSLib/Granny/Model/Exporter.cs
[vt]: https://github.com/Norbyte/lslib/blob/master/LSLib/VirtualTextures/VirtualTexture.cs
[render]: https://github.com/Norbyte/bg3se/blob/main/BG3Extender/GameDefinitions/Render.h
[pbrt]: https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer
