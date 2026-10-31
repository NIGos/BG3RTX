# Native terrain surface arithmetic contract

`common/native_terrain_surface.h` shares CPU/HLSL post-sample formulas from native
fragment module `6650c75f3d3e4968`. Decompiled local GLSL SHA256:
`5db81ef285601670a1bb9d928e24f5adfbec28061ea8e85a34dbd0cd4fd8e13f`.
VS `64e2568916ede3d6` SHA256:
`f37739bad02fd33eacc68dc65f322ec5dc8be2da7f9da38f7cc3cc857e414753`.
Existing local cache: `captures/alpha_cache/<hash>.glsl` in main checkout.
Resource provenance: private `captures/m2-foundry/terrain-material-inputs/evidence.json`.

## Implemented subset

- `ntt_project`: FS `_219.._298`, given the already sampled RGB normal, grid X/Z
  and local height already computed by native fma(normalizedHeight,scale,bias).
  Preserves normalization, signed axis weighting, native threshold/rebalancing,
  nested FMA UVs and normal XY orientation flag. It does not fetch a normal or
  infer derivative/LOD/material coordinates from package positions alone.
- `ntt_height_score`: `_341.._349` repeated for four painted layers. Actual
  post-sample mask, alpha and native members m5/m3/m4/m6 are explicit arguments.
  Keeps strict mask/scale/height branches; offset cannot revive rejected values.
- `ntt_blend`: `_486.._546`, competitive height blend, native epsilon and base
  remainder. A valid zero/negative score is distinct from unknown input data.
- `ntt_base_curves`: `_1131.._1152` AFTER gather/interpolation. Caller supplies
  both interpolated slope/height pairs. Preserves native factor2 and all-zero
  first-curve rule; no curve upload, gather order or filtering is invented.

CPU uses float and `std::fma`; HLSL uses float and mad (compiled fixture has eight
GLSL.std.450 Fma instructions). Pow(0,0), nonfinite inputs/results and zero native
normal/denominator domains return explicit statuses. No roughness, BSDF weights,
normal-basis, final G-buffer encoding or opacity conversion is added. The packet
structs describe native field order only; they are not bound directly as buffers.

## Missing input contract

`NttInputEvidence` stays UNKNOWN unless the caller separately proves all packet,
mask mapping, sampler and derivative facts. Numeric tests never establish these.
Native s4b0 std140 layout is 80 bytes: vec4 m0/m1/m2 at0/16/32, vec2 m3/m4 at48/56,
base uint indices64/68, blend width72, m7 at76. Native s1b4 std430 stride112:
three vec4 at0/16/32; twelve floats48..92; four uint96..108. CPU sizes/offsets
have compile-time checks. Authored field names do not certify runtime upload.

Required future same-draw acquisition: s4b0 bytes80, s1b4 descriptor offset/range
and records0..3 plus selected base indices<64, s1b2 i8442 RGBA8_UNORM49x84 pixels
(16464 tight bytes), s1b3 i8443 R16_UNORM256x4 LUT pixels (2048 tight bytes), view
formats/swizzles/row pitch and native s4b4/s1b1/s0b7 sampler states. The prior S1
frame retains metadata, not these buffer/pixel values. Native raster derivatives
are still separate. No shader or renderer integration occurs in this increment.

## Verification

Standalone C++17 `/O2 /fp:strict /W4 /WX` test uses independent analytic axis
planes/signs, actual authored contrast1 tuples as explicit scalar cases, exact
symmetric blend results, base factor2, and invalid/undefined cases. Numeric
comparison bounds use gamma(n) with binary32 u=2^-24 and absolute operation sums;
pow is checked at actual contrast1, not claimed generally correctly rounded.
HLSL cs_6_6 fixture takes dynamic samples and passes Vulkan1.2 SPIR-V validation.
No GPU dispatch, renderer quality, native-packet availability or live gate follows.
Root owns CMake registration; compile fixture is deliberately outside runtime shaders.
