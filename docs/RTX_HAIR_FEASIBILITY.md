# RTX Hair feasibility for BG3RTX

2026-10-30. Offline source audit. No SDK installation, GPU test or game session.

**Decision: feasible bounded prototype, not a drop-in replacement.** RTX Hair can
render actual strand geometry on Vulkan. It cannot recover missing BG3 grooms or
automatically fix the current darker/dithered hair. Keep the current regression
fix lane first. Treat faithful card coverage and true strand replacement as
separate deliverables. Runtime quality and GPU cost are unmeasured.

## Current NVIDIA implementation

The public [RTXCR releases](https://github.com/NVIDIA-RTX/RTXCR/releases) list
v1.4.1 as latest. Vulkan LSS support arrived in v1.2.0. v1.4.0 improved far-field
sampling, PDF consistency, energy conservation and denoiser guidance. Pin a
release rather than copying an early SDK example with known corrected behavior.
Read-only source revisions inspected: RTXCR `9db7dd36cb322f67f0a4ceb1cb30ba503fd0bd86`,
Geometry Library `d6886b9fe37d51dd6529a8a921aa115eab177e06`, Material Library
`eb184f9637f81a58a620d0e3cb4356acbc45cbe6`.

The [RTXCR README](https://github.com/NVIDIA-RTX/RTXCR) describes Blackwell LSS
hardware intersection and DOTS triangle fallback. It lists RTX 20+, Radeon RX
7000+ and Intel Arc for the sample, driver >= 580.88 and Vulkan SDK 1.4.313.2.
That sample compatibility is not a promise of LSS acceleration on all GPUs.
RTX 5090 belongs to the intended LSS hardware class; the actual game-created
device's extension/features have not been qualified by this audit.

Vulkan requires explicit negotiation of
[`VK_NV_ray_tracing_linear_swept_spheres`](https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_ray_tracing_linear_swept_spheres.html),
`VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV.linearSweptSpheres`,
the relevant format capabilities, AS geometry type and extension dependencies.
This is an NV extension, not a core/KHR feature implied by existing ray queries.
Our local Vulkan SDK 1.4.357.0 has its declarations; BG3RTX does not enable it.

Do not claim inline ray queries are intrinsically incompatible: the current
[`SPV_NV_linear_swept_spheres`](https://github.khronos.org/SPIRV-Registry/extensions/NV/SPV_NV_linear_swept_spheres.html)
explicitly defines ray-query LSS hit detection, positions, radii and hit parameter
instructions. The local SPIR-V header also contains them. Our actual DXC,
validator and four production query consumers still need a small compile and
intersection gate; existing triangle/AABB handling cannot simply be left unchanged.
An RT-pipeline implementation instead needs its corresponding pipeline capability.

The SDK/source repositories are publicly readable. The
[RTXCR license](https://github.com/NVIDIA-RTX/RTXCR/blob/main/License.txt) and
separate [geometry](https://github.com/NVIDIA-RTX/RTXCR-Geometry-Library/blob/main/License.txt)
and [material](https://github.com/NVIDIA-RTX/RTXCR-Material-Library/blob/main/License.txt)
licenses are NVIDIA RTX SDK licenses, not blanket MIT. They govern modification,
application redistribution and notices; third-party portions can have separate
terms. Pin and retain the applicable notices before incorporating code. Source
inspection does not install or accept dependencies on the user's behalf.

## Geometry and shading inputs versus actual BG3 evidence

The actual [geometry API](https://github.com/NVIDIA-RTX/RTXCR-Geometry-Library/blob/main/include/CurveTessellation.h)
takes connected line segments with two endpoint positions, endpoint radii and UVs.
LSS conversion packs these into position/radius arrays. DOTS also starts from
those segments: it is not a hair-card-to-groom converter. Its implementation
emits four triangles per segment. The library's radius floor is expressed in
source units; do not import its numerical scale as a BG3 physical hair thickness.
The [sample conversion](https://github.com/NVIDIA-RTX/RTXCR/blob/main/samples/pathtracer/src/Curve/CurveTessellation.cpp)
operates on existing curve/line meshes, not arbitrary alpha-tested triangles.

Our [native coverage implementation](NATIVE_HAIR.md), `common/native_hair.h`,
`layer/geocapture.cpp` and `shaders/rt/rt_common.hlsli` expose deformed triangle
positions, UV, N/T/B, masks/noise and camera fade/virtual depth. The captured
88-byte stream has **no groom segment connectivity or physical endpoint radii**.
Its “strand frame” is a shading frame interpolated over a card, not a strand.
Procedural AABBs currently contain those same card triangles/virtual primary
surfaces; they do not turn them into curves.

Read-only priority-resolved Materials.pak templates inspected:

| Template | Actual source evidence | Consequence |
|---|---|---|
| CHAR_Hair | ID_Depth_Root_Alpha_MSKA, noise, depth offsets, color/roughness/scatter/backlight | Card material parameters, not a complete groom |
| CHAR_Pubes | Mask/noise and root coverage | Separate recipe; no proven curve provider |
| CHAR_Fur_IDmsk | IDMsk, Fur_normalmap, body color, dither/opacity parameters | Cannot infer fiber radii or borrow Hair capture semantics |
| CHAR_Scalp | Scalp coverage/color/roughness/scatter | Underlying scalp surface; preserve it during strand replacement |

Template SHA-256: Hair `09717bee7da73c48ed6e3532cce136a358033bedef1f1c2a6e7483b43e179887`;
Pubes `0fa7cd73de54720f8c9632f7cb3bba87b7f2fdb1dd14e0bbe31860419cd36896`;
Scalp `ce4212b0793a6298240f4043101a179a7f43b3ed6e650ad40e7d8f854159d2cd`;
Fur `667ac56d2b79248546cb2a74491264f786b3458970eb1ad59d50b600123384ff`.
Recovered native module hashes and supported interfaces are documented in
NATIVE_HAIR.md and the existing native-module-provenance capture.

Shared.pak's actual `[PAK]_Hair/_merged.lsf` contains SkeletonBank, PhysicsBank
and VisualBank. Example resource `1a8791b8-f766-5adb-ff77-224c162e0e4a` points to
`HAIR_HUM_MS_GnomeCut_Long_C_Spring_Base.GR2`. This is not evidence of an LSS
groom: skeleton/animation curves are not necessarily hair centerlines. No groom
provider is established for the observed styles. This bounded audit did not
decode every GR2 or prove that no usable strand data exists anywhere in BG3.

The [hair shading guide](https://github.com/NVIDIA-RTX/RTXCR/blob/main/docs/RtxcrHairGuide.md)
requires a hair scattering model and local fiber frame, color/absorption, two
roughness directions, IOR and cuticle angle. Far-field scattering reduces
azimuthal detail/noise; near-field retains it. Neither substitutes for geometry.
The guide also requires current/previous strand positions for reliable motion
guidance. Native Roughness, Scatter and HairBacklit are not an established
one-to-one mapping to these inputs. Hair color is not a measured melanin value.

## Staged plan and stopping conditions

1. **Fix and qualify existing cards first.** Preserve native appearance while
   resolving the reported darkening/dither. Compare actual source coverage,
   native light response and RT contributions. Do not compensate with exposure,
   alpha erosion or hair exclusion. RTX Hair research must not hold this fix.
2. **One actual hairstyle provider.** Decode one identified GR2/material tuple,
   verify topology, skinning/root attachment, LOD and animated runtime mapping.
   If it contains only cards, report that result. A replacement then needs an
   explicitly authored groom or an explicitly approximate conversion with artist
   validation. Recovering a unique groom from masks is not promised.
3. **One bounded curve prototype.** Start with a licensed/authored tiny groom,
   explicit unit/radius scale and static endpoint data. Validate LSS against DOTS
   or a reference representation, then add captured deformation. Cover grazing
   rays, endpoints, overlaps and unsupported hardware fallback. Synthetic success
   is a backend gate only, not replacement of any BG3 asset.
4. **One-style replacement integration.** Selectively replace that style's
   native primary hair, depth and shadow contribution while retaining scalp and
   accessories. Supply matching depth/normal/material/coverage, current/previous
   motion and RR guidance. Handle secondary visibility/transmission consistently;
   opaque binary strand shadows plus unrelated card primaries can worsen darkening.
   A BCSDF experiment on existing cards is a distinct hybrid approximation, not
   “RTX strand replacement”. Unmapped styles retain native rendering.
5. **Authorized same-run quality/cost gate.** Compare silhouette/hairstyle,
   backlighting, face shadows, colored transmission, animation and disocclusion
   against an accumulated reference and native baseline. FG/RR cases remain
   explicit. Measure BLAS update, tracing/shading, denoising, VRAM and total GPU ms.
   No arbitrary brightening or accepted loss of strands to lower noise. Unsupported
   geometry/material/motion must fall back explicitly, not silently pass.

**Next dependency:** establish one actual hairstyle's geometric provider and
finish the current regression before installing SDK code or attempting primary
replacement. No new feature key is justified by a documentation-only milestone.
Cost is UNKNOWN: NVIDIA's LSS-vs-PolyTube figures do not predict BG3 card-vs-strand
cost. No frozen user trial, main runtime, settings, ABI or live session was changed.

## One actual authored provider checked - 2026-10-31

Decoded the ordinary Shadowheart style, not an inferred groom. Shared Hair bank
visual `12b9f717-05cc-71de-49ba-99650936e13c` names
`HAIR_HUM_F_Shadowheart_Spring.GR2` in Models.pak (1,089,048 bytes, SHA-256
`8b71daa700a7f07cf8dd4433b3346f1c7d90bc355317e2e2095fb18fc79cf2be`).
Material `d2a17b69-bb3d-4940-6c6f-64220bd3d4c6` resolves to CHAR_Hair;
accessories use separate `02a7555e-9bd7-413c-41df-2807fe17b075` Base_AlphaTest_VT.
This is the supported recipe, but the runtime trial has no proven visual-UUID
join selecting this exact style variant.

Existing pinned LSLib v1.20.4 decoded the native GR2 and exported all four objects:

| Object | Vertices | Triangles |
|---|---:|---:|
| Hair LOD 0 | 33,513 | 40,090 |
| Hair LOD 1 | 3,198 | 2,003 |
| Accessories LOD 0 | 1,935 | 2,394 |
| Accessories LOD 1 | 141 | 117 |

Every primitive is a triangle list. Hair has POSITION/NORMAL/TANGENT, COLOR_0,
two UV sets and JOINTS_0/WEIGHTS_0. The native model has a 28-bone skeleton,
18 hair bone bindings, zero morph targets, zero track groups/animations and no
root ExtendedData. Decoded mesh metadata contains LOD/rigid/spring/cloth flags,
not a separate centerline/radius array. Independent accessor checks validated
finite values, matching attribute counts and every triangle index. These checks
do not certify arbitrary unknown GR2 extensions or native animation behavior.

HairPreset `84d7e901-e40a-73e3-4daf-010bd3391eff` resolves to scalar material
overrides (including root depth, roughness, wind and scatter), not a curve asset.
Skeleton `28f00611-d8c1-7e7d-4204-7d53d6b59ade` links SpringResource
`ef5dba38-df97-b9a1-454c-717a30536ee8`: an 11,410-byte SEBD binary, SHA-256
`e6d2ce594a9f78f29cb028a81523ad3ab8c020fcad504f3c6284c1825f973756`.
Its retained strings identify Hair0_M..Hair6_M bone constraints. It has not been
decoded into a physical schema; those constraints are not evidence of fiber
centerlines or endpoint radii.

Scope: the existing resolver indexed 566,751 paths across 27 installed packages;
1,507 relevant character/material/engine LSF resources were checked for the
selected linked IDs. No candidate curve-format filename established a provider.
This is not proof that all game/mod resources lack grooms. Exact missing inputs
remain authored connected fiber endpoints, physical radii and attachment/current
and previous deformation correspondence. No faithful RTX strand provider can be
implemented from this selected decoded tuple. Retain the supported native card
path; do not reinterpret tangents, masks, UV edges or bone chains as fibers.
Private reproducible decode, resource records, fingerprints and topology checks:
`m2-foundry-mesh/BG3RTX/captures/rtx-hair-provider/`. No SDK download, game, GPU,
shader, ABI, runtime setting or installed resource was changed.
