# Shared construction material: native recipe

Material `a1a20440-b6f8-0446-bc9d-8b92545b637d` is
`WALL_GEN_Glasshouse_Bronze_A_Multi_A`, not a VT material. Its actual Shared.pak
MaterialBank node selects `ENV_Tileset_MultipleTextures_AlphaTest.lsf`.
The priority-resolved Materials.pak template SHA-256 is
`6456680878a31ab98f7d9e9a55f2ad6748cd738df331e7b157f74e1f227c92cf`.
Full material node, nine enabled texture resources, DDS hashes, decoded point
codes and native module hashes are in `tests/native_tileset_surface_reference.json`.

Native Deferred ST pairs VS `af1d30a11685c516` with FS `7621aebd092c3889`;
STI pairs VS `a618ddae98c2d1cf` with FS `cad41eff979970e4`.
Both vertex programs pass color and UV to fragment locations 0 and 1.
The fragment selects Green when interpolated color.y > 0, otherwise Red when
color.x > 0, otherwise Base. It does not blend these three materials.
Normal textures occupy set 1 bindings 2/3/4, albedo 5/6/7, physical 8/9/10.
All use sampler set 0 binding 7. Selected UV is TEXCOORD0 times the material
tiling float at byte 0/4/8. Reflectance is byte 12. Material overrides for all
three tilings and Reflectance are disabled: authored defaults are 1 and 0.5.
There is no enabled virtual texture. The unnamed fallback Texture2D is disabled.

Native normal unpack is normalize(2*A-1, 2*B-1, 1-2*G), then native frame
columns (T,N,B). Physical R is metalness, G roughness, and B becomes the albedo
target alpha. Reflectance, roughness, metalness and albedo RGB are clamped.
Both Deferred fragments contain no discard. Their names do not prove depth
coverage, which remains unknown here.

Actual floor exports have constant COLOR0=(0,1,0,1), selecting Green at every
interior point. Wall exports contain Green, Red and a few Base vertices; their
interior selection must use interpolated color, not object names or vertex votes.
The helper supports the exact strict-positive boundary and rejects missing color,
wrong shader pairs and nonfinite selected UV/tiling. It reuses the already tested
post-sampling field arithmetic without inventing a VT validity marker.

All nine DDS files were extracted and decoded locally. Authored texture records
mark three albedos SRGB=true and all normals/physical maps false. This is not
proof of the actual native sampled view, sampler, filtering, mip selection or
residency. The numeric point fixtures test field arithmetic only; they perform
no guessed sRGB conversion. Native world normal basis equivalence and runtime
shader/material association are also unqualified.

Validation: 230 new CPU checks, including exact zero/nextafter branch boundaries,
Green precedence, selected-only tiling validation, both shader pairs, missing
view/sample/scalar refusal and three actual normal/physical point-code fixtures
against double arithmetic. Existing VT test still passes all 164,188 checks.
No CMake, runtime integration, shader, ABI or settings change; no GPU/live work.

## Retained S1 runtime recipe - 2026-10-31

The actual R8 frame `20261031-064118` contains STI Deferred VS/FS
`a618ddae98c2d1cf/cad41eff979970e4` in two resource groups (48 and 57 draws).
Its prepass is `689836dece58e778/781cd8d1d600117d` (49 and 61).
Prepass Green albedo `s1b1` equals Deferred `s1b5` exactly:
`i12664` and `i12460`. Earlier frame `20261031-055814` independently repeats
the relationship (`i12712`, `i12480`). These four identity checks establish the
live family/resource relationship, not a unique geometry/material-UUID join.
The aggregated frame omits per-draw vertex bindings, scalar bytes, view ranges
and sampler state. Unequal group counts forbid ordinal pairing.

| Native vertex path | Position | Color / UV / quaternion frame |
|---|---|---|
| ST Deferred af1d30a1 | loc0 or cached `s0b13`, then engine `s1b0` matrix | loc1 / loc2 / loc3 |
| ST Depth 4235ddab | same cached/native position and matrix | loc1 / loc2 / absent |
| STI Deferred a618ddae | loc0 times instance rows loc1..3 | loc4 / loc5 / loc6 |
| STI Depth 689836de | same position and instance rows loc1..3 | loc4 / loc5 / absent |

ST cache index is engine uint byte64; positive index selects
`(gl_VertexIndex + index - 1) * 20` bytes, float3 position followed by two packed
SNORM16 quaternion words. UV/color remain ordinary streams. Deferred outputs
COLOR/UV/N/B/T at loc0/1/2/3/4, with native normalized quaternion/instance frame;
capture these actual outputs rather than reconstructing tangents from triangles.
Native Vulkan input formats/strides must come from the actual pipeline, not the
GLSL vector types or package accessor formats.

Crucial coverage difference: neither Deferred FS discards, but native Depth FS
does. Green (`COLOR.y > 0`) samples Green albedo alpha at UV times Green tiling;
other branches use alpha1. Cut is 0.5. ST Depth uses texture `s1b2`, UBO `s1b1`;
STI Depth uses texture `s1b1`, UBO `s1b0`. Their UBO byte12 is **SeeThrough**, not
Deferred Reflectance. If it equals1, `s0b6` capsule parameters, camera origin and
a separate noise texture enter coverage. Neither depth hash is in alpha_fs.txt.
Do not mark the whole family opaque because its Deferred shader has no discard.

Smallest next runtime slice: STI first, actual SeeThrough !=1 only; snapshot
depth COLOR/UV and its Green alpha/tiling, plus Deferred COLOR/UV/N/B/T, three
tilings/Reflectance and all nine actual Texture2D views with `s0b7` sampler.
Retain original view formats/swizzles/subresources and clone the actual sampler;
image fmt137/138 etc in frame.json is image metadata, not sampled-view proof.
Extend existing recording-owned unique draw association to per-instance geometry,
then require same post-VS clip/UV/color at publication. The current VT-only
position/UV join, single-instance restriction and loc0 UV/frame offsets cannot be
reused unchanged. COLOR needs explicit storage; no host ABI bump is implied by
this layer-private packet change. Use the already derived strict branch/field
helper and declared ray footprint; raster implicit derivatives are not available.
ST additionally needs cached-P/frame and pass-local-engine normalization; defer
that distinct path until its actual inputs are captured. SeeThrough remains
unsupported until its complete native inputs/policy are represented, not opaque.

Read-only proof artifacts: isolated `captures/tileset-runtime-recipe/` contains
15 priority-resolved native module pairs, eight cached source fingerprints and
the four retained image-identity checks. No renderer/source integration, settings,
ABI, game or GPU work was performed for this recipe audit.
