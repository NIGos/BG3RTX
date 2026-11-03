# S8 native foam coverage, offline 2026-11-03

**Prediction: exactly 0% of finite S8 pool pixels have zero native foam weight.**
The game's `max(abs(signal), 1e-4)` before `pow` proves this without a diagnostic
live or a UV/time histogram. Do not use the template's edge-foam default as the
pool-instance setting: the installed Waterqueenhouse materials override it.

Reproduce at BelowNormal with
`python -m tools.assets.water_foam_coverage --out build/water-foam.json`.
This uses the existing read-only priority-resolved package reader and LSF parser;
no game, saves, GPU or git metadata is used. Decoder fixtures:
`python -m unittest tools.assets.tests.test_water_foam_coverage` (3 passed).

## Authored material chain

`data/assetdb/mesh_materials_cov_mesh_material_v2.json` first identified the resource
UUIDs. The tool independently rereads their current resource nodes from
`Public/SharedDev/Content/Assets/Nature/[PAK]_Water/_merged.lsf`, owned by Shared.pak,
and resolves enabled overrides over the Materials.pak template
`Public/Shared/Assets/Materials/Fluids/FLUID_Water_Plane_flowmap_NoCaustics_Waves.lsf`.
Template SHA-256: `61a7921d92e5be564f9363ed2406dbb90e810a71e9df11d9738b1bb20a94bb4a`.
The runtime FS is c4e92ccdb08dd777 (StaticInstancedForward, material CB 224 B).

| Resource | UUID | Exponent | Multiplier | Edge distance / weight | Lower bound c |
|---|---|---:|---:|---:|---:|
| NAT_RVR_Waterqueenhouse_BridgePools | 45e698fc-b82e-aac1-d651-f41066319f95 | 1.600000024 | .200000003 | .100000001 m / .5 | 7.96214e-8 |
| NAT_RVR_Waterqueenhouse_Fountain | 8a7a0b51-9f4d-8f0b-00dc-5ca1ebc9e4f0 | 1.600000024 | .300000012 | .100000001 m / .5 | 1.19432e-7 |

Both override high-velocity mask min/max/multiplier to 3/-1/1 and low-velocity
to .1/1.5/.2; foam world tiling is (.2,.3). Their foam flow speeds are .05/.3.
These are authored assets, not a captured runtime CB/material UUID witness. An
unknown runtime override, NaN or changed material is not covered by this proof.
Both relevant authored water materials nevertheless give the same zero-share
prediction; the exact scene-weighted coverage distribution needs runtime inputs.

## Textures and all mips

Both use inherited foam UUID 78c4bac0-421c-0585-d63b-8c4c4e07d9e4,
`Generated/Public/Shared/Assets/Nature/Water/ENV_Tileable_Noise_Waterfall_NM.DDS`,
Textures.pak, linear DXT5/BC3, 2048 square, 12 mips. SHA-256:
`63fa454022c6d3bde88fcfba847ad0ed870bb77939be5d59b2c68e3aed30e61e`.
The foam signal samples red (.x), not normal channels wzy. Decoded red minima at
levels 0..11 are {0,0,0,0,0,.031373,.031373,.094118,.129412,.129412,.181699,.213072}.
Red means are about .205 across the chain. Fine-level exact black texels exist
(at most .008% per mip); these are texel statistics, not pool-pixel percentages.
Levels 5..11 have no black texels. Linear/anisotropic phase-blended sampling cannot
undo the final shader floor even when both contributing texels are exactly black.

Actual flow UUIDs are 82d39159-2404-35b3-d008-7d032af23da5 (BridgePools) and
2fad42b0-8136-8c05-014b-d12148a1f9d7 (Fountain), not the template's default cloth
texture. Paths end in `BGH_Waterqueenhouse/Resources/NAT_RVR_Waterqueenhouse_` +
`BridgePools_FM.DDS` / `Fountain_FM.DDS`, under Generated/Public/SharedDev/Assets/Nature/Water/Act3/.
Both are Textures.pak linear BC5U. All 7 BridgePools mips (64..1) and 8 Fountain
mips (128..1) were decoded. At level 0, pool RG ranges are [.207843,.956863] /
[.066667,1], final mip (.576471,.584314); fountain [0,1] / [.019608,1], final
mip (.545098,.537255). Non-neutral flow therefore exists; no assumption of an
identically-zero flow mask is justified. JSON retains per-level/channel statistics
and texture hashes. Decoder interpolation is ideal UNORM; zero/nonzero and the
post-pow floor proof do not depend on hardware's last-bit interpolation rounding.

## FS path and implication

For colour SSA 751, F0 SSA 814 and roughness SSA 878, the native path is
`c_i=clamp(pow(max(abs(phaseBlend(foam.x)*edgeFlow),float32(1e-4)),exponent)*multiplier,0,1)`.
The three independent phase/flow evaluations need not give identical weights.
`edgeFlow` adds depth-edge foam to two flow-speed smoothstep terms. Even if all
three terms vanish, the positive FP32 post-pow bounds in the table survive; these
are far above FP32 underflow. Texture or flow zeros cannot produce c=0. Thus the
exact-zero clean-only design has no participating pool pixels for these assets.
Colour SSA 751 is the scattering coverage c; preserve distinct native F0 and
roughness authoring on the native foam layer rather than treating either as a
clean-water IOR or coverage. Apply the interface/column model to weight (1-c),
retain native foam at weight c, and check both endpoints plus all intermediate c.
