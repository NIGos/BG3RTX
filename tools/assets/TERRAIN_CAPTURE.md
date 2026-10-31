# Actual terrain blend inputs

`python -m tools.assets.terrain_material` now evaluates the existing FS6650 blend
formulas from a copied schema-1 terrain packet. It decodes the actual 80-byte
uniform and 112-byte brush rows, samples the captured RGBA8 painted mask with the
native normalized linear/clamp-edge sampler, and connects those samples to
`height_score` and `layer_weights`. `--grid X Z` means native FS input `_3.xz`,
not world coordinates. The API does not infer this transform.

All four `--bma-alpha` arguments are explicit native-footprint material samples.
They are never replaced with 1 by the evaluator. Supplying 1 yourself is a
declared input experiment, not actual BMA recovery. Actual material texture
identity, textureGrad derivatives, normal RGB, projection and final G-buffer/BSDF
evaluation remain missing. Global indices 208/98/63/383 are not DDS identities.

The captured R16 LUT is also consumed: `base_weights` implements native gather
order and its separate horizontal interpolation of gather w/z and x/y pairs.
Only then does it call existing `base_curve_weights`, retaining native factor 2.
It does not replace the operation with a bilinear sample. Active two-base inputs
require explicit normalized native normal.y and native `_3.y`; unknown inputs
are rejected. A first base index >=64 has an unsupported checker-material path.

The implementation accepts the actual bounded 49x84 RGBA8 / 256x4 R16 single-mip
profile only. Unknown sampler metadata, changed filtering/addressing/anisotropy,
changed views, extents, mip profiles, malformed bytes and nonfinite inputs fail.
Python binary64 ideal filtering is a CPU reference, not GPU subtexel/FP32 parity.

## R5 acquisition and authored association

Source is coordinator archive `captures/native-hit-material-20261031/r5/20261031-064118/terrain_packet`:
frame 4521, packet 2, recording 113450, pass 0, draw 801, VS64e2568916ede3d6 /
FS6650c75f3d3e4968. The original packet is read only. The compact fixture retains
80 uniform bytes, four brush rows, one 2x2 mask neighborhood and the exact four
constant LUT rows as values, plus original hashes. It is not a whole raw capture.

`resolve_authored` compared all 4 x 4116 RGBA channel bytes against concatenated
patch 0/1 authored layer planes, and all 12 brush scalars plus two RGB gradient
colors against `m2-foundry/terrain-material-inputs/evidence.json`. All match
exactly. The four channels resolve to SharTemple C, SharTemple C Broken A,
Gravel B Grey and Coastal Sand Grey B, in that order. Authored overrides are 0.
This establishes these packet fields and channel order; it does not identify
global texture slots or certify flags/final material arithmetic.

Base indices are 0 and 255. Native FS branches on second index <64, so LUT
mixing is disabled for this actual draw. Its LUT rows are exactly 1/0/1/0, each
constant across 256 columns. Active LUT tests are explicitly separate cases.

Actual-data CLI check (explicit synthetic BMA alpha, no shading-parity claim):

```text
python -m tools.assets.terrain_material --packet <R5 terrain_packet> --grid 8.75 2.75 --bma-alpha 1 1 1 1 --authored-evidence <terrain-material-inputs/evidence.json>
```

Mask is (0, .1973039527793472, .39044107410044093, 0). With those explicit
alphas, painted weights are (0, .9999901057417631, .000009894258236993198, 0),
base 0. No actual BMA alpha or full native terrain shading was measured.

Verification: 10 Python tests cover existing formulas plus real extracted
records/neighborhood, authored channel mismatch, independent sampling and gather
oracles, active/disabled LUT paths, and rejection cases. No shader/runtime/core,
settings, game, saves or GPU tools changed or ran. No new GPU cost exists.

Gather ordering reference: [Khronos GLSL texture gather functions](https://docs.vulkan.org/glsl/latest/chapters/builtinfunctions.html#texture-gather-functions).
The selected texels are returned in i0j1/i1j1/i1j0/i0j0 order.
