# Native class 0 parameter bridge

`common/native_opaque_transport.h` is shared C++/HLSL parameter arithmetic. It
includes the existing transport types/math; it adds no BRDF, sampler, PDF or
reference renderer. It has no runtime consumer, host ABI or user setting.

## Verified source equations

Native lighting module `f469db7865d7c524`, catalog identity
`Shaders/Vulkan/ClusteredLighting/ClusteredLighting_0.bshd` in EngineShaders.pak.
Read-only retained decompile:
`captures/20261027-050634/shaders/f469db7865d7c524.glsl` in the main checkout,
SHA-256 `6e7440f71d8355800098a0d5460268428956641028f3d980af89eb7fadd3327b`.
Lines 338–357 contain the parameter conversion and native quad AA:

- `_433` is material: x roughness, y reflectance, z metalness. `_431.xyz` is
  already interpreted linear G-buffer albedo; there is no extra sRGB conversion.
- `_477 = albedo * (1 - metalness)`.
- `_482 = mix(0.16 * (reflectance * reflectance), albedo, metalness)`.
- `_483 = max(0.09, roughness)`.
- `_515 = sqrt(clamp(fma(_483,_483,min(dot(dh,dh)+dot(dv,dv),0.18)),0,1))`.

Native specular D later squares `_515` twice. Thus the matching GGX **slope
width alpha** is `_515²`, not `_515` or raw perceptual roughness. The bridge
preserves the sqrt-then-square sequence for qualified native quad AA. Without
that AA, `base_alpha = max(0.09,roughness)²` is explicitly the intrinsic parameter,
not a fabricated offscreen equivalent of the raster footprint.

Existing equivalents: `shaders/gi/rt_local_shadows.hlsl:200` and
`shaders/gi/rt_reflections.hlsl:103`; consumers' `Bg3Surface` and native lobe math
are in `shaders/rt/bg3_lights.hlsli` and `bg3_materials.hlsli`.

## Exact supported contract

Input is verified decoded class 0 only, with caller evidence of current samples,
linear color interpretation, known opaque coverage and known absence of additional
lobes. Unknown sampling/view interpretation/coverage, transmission, anisotropy,
extra lobes, unknown flags, other classes and nonfinite/out-of-unit-range fields
return explicit non-success statuses. Cleared result values are never a black
or white fallback; a consumer must test status before using them.

The separate AA operation requires actual known native view-normal quad
differences. It declines absent footprint and nonfinite derivatives. Native
raster NaN-vector sanitization is outside this finite-input helper's contract;
it does not guess zero derivatives or reinterpret ray cones as raster quads.

C++ `nt_from_native_surface` accepts the existing VT/multi-texture decoder's
Surface and successful decode status. It does not invent sampling or opaque
coverage evidence from a shader hash. The actual AlphaTest_VT foundry asset's
coverage is still unknown and is rejected unless a real provider establishes it.

## Distinct from a transport material model

`NtParameters` retains linear albedo, reflectance, metalness, native diffuse/F0
coefficients and roughness parameters. It intentionally contains no lobe weights
and cannot silently become `TbMaterial`.

Original `TbMaterial` is a convex Lambert + GGX mixture with weights summing at
most 1. Native class 0 uses empirical Burley diffuse and different visibility
math; preserving its parameter coefficients does not make that mixture its BSDF,
a coupled dielectric or a proven energy-conserving native transport model.
There is no automatic metalness-to-weights prescription here.

`nt_to_fresnel_blend` now offers an explicit separate physical-model policy:
native diffuse_color -> FresnelBlend Rd, native F0 -> Rs, base_alpha -> GGX alpha.
It propagates non-success native status and does not claim native-BRDF equivalence.
Sampling probabilities do not become material weights. Model tests and limits
are in `tests/transport_bsdf_sources.md`.

Remaining runtime gap: the shared reference must obtain a qualified hit-material
tuple (including current sampling, coverage and frame) and wire the declared
model's evaluate/sample/PDF. Native AA requires its
own footprint evidence. Emission, normal basis/correction and unsupported classes
remain separate provider/consumer work. No scene, renderer, energy or GPU cost
qualification follows from this helper.

## Validation

The CPU test uses independent binary64 native equations over every pair of 256
UNORM8 roughness/metalness codes, varying reflectance, endpoint identities and
finite native-quad AA cases. Its comparison envelope is derived from a 12-operation
FP32 gamma bound over unit-range terms, not tuned to fixture results. It also
tests explicit unsupported/unknown statuses, valid black versus unknown and sixteen
actual decoded normal/physical point-code fixtures through the existing decoder.
Fixture albedo is explicitly synthetic linear input; no native sampled-view sRGB
proof is inferred from DDS codes.

Dynamic HLSL compile fixture preserves parameter outputs/statuses and AA. DXC
cs_6_6 targeting Vulkan 1.3 compiles and spirv-val passes; AA retains SPIR-V Fma and
Sqrt. These are compile checks only, not GPU numerical parity or performance.
