# Windowless analytic hit-shading fixture

CTest `native_hit_gpu_fixture` (built by default, part of `.\scripts\build.ps1 -Test`).
It runs the real hidden-hit shading functions on the GPU against closed-form references:
`native_hit_direct` (point, spot and box lights, the screen's light policy),
`native_hit_sun` (disc sampling), `native_hit_ao` and `hd_scatter`.
The include chain and defines are rf_hit_shade's (`NATIVE_HIT_BOXES`, `NATIVE_HIT_DLS_POLICY`)
plus gi_hit_sun's `NATIVE_HIT_SUN_DISC`. Not covered: the roof-volume rule
(`NATIVE_HIT_SUN_ROOF`, its trust slots), the ambient (`NATIVE_HIT_SHADE_FULL`),
`rt_hit_material` (the material is built with the real `nt_class0_parameters`), alpha-tested
geometry, and per-light slots (`ls_flags` bit 0).

The build compiles `tools/native_hit_gpu_fixture.hlsl` with DXC
(cs_6_6, SPIR-V 1.3, HLSL 2021, O3) into `build/native_hit_gpu_fixture.spv`.
The test skips (exit 77) without a Vulkan 1.3 device with ray queries, or without the
Khronos validation layer. It never creates a window or a swapchain. Implicit layers
(the game layer included) are disabled. It runs at below-normal priority, with queue priority 0.
Any validation error fails the run.

## Scenes

The scenes are opaque triangles in one real TLAS. There are eleven 30 m floors, 100 m apart.
Each dispatch has one froxel listing that dispatch's lights.

1. Point lights (inverse square; the m5 < 0 model with b13[0]) and a spot light, checked against
   bg3_light_eval's falloff and the declared FresnelBlend. Range, cone and channel give an exact 0.
   A light inside a closed 0.2 m lamp cube lights the floor (stop-short `ls_clear` 0.2).
   With no stop-short it is occluded (control).
2. Occluder between: occluded. Ceiling behind the light: lit. Blocker within `ls_clear`: lit
   (the D-LS rule). A light below `ls_min_radius` is left at the game's visibility:
   unshadowed, and `.native` equals its radiance.
3. Sun: an open floor is lit with N.L; a floor under a roof slab is exactly 0. Disc sampling:
   a half-plane at 20 m with its edge on the axis, at rho/2, and outside. The visible fraction
   must equal the circular-segment value. The sample cone must reach `sun_radius`.
4. Box light: inside or outside the box, z ramp, edges, smoothstep and smootherstep curves,
   direction _m9, b13[0]. Without _m8 bit 0, blockers are ignored (the game look). With it,
   a blocker inside the box occludes and one beyond the box exit does not.
   AO-on-direct share (4c): with the screen's box pass off (Push::ls_flags bit 26 clear) every box
   term is also in `.native`, bit for bit, so the caller applies mix(1, AO, m0.y) to it, as the game
   does on screen; with bit 26 set `.native` is 0 and the radiance is unchanged.
5. AO: an open floor gives exactly 1. A wall at 0.1 m and at 0.5 m (R = 1) is checked against
   the closed form of the hybrid GI weight 1 - (t/R)^2 under cosine rays:
   E[w] = (1/pi) int g(h / (R cos phi)) dphi, with g(a) = (1 - a^2 + 2 a^2 ln a) / 2.
   The AO-on-direct factor is checked as mix(1, A, m0.y).
6. Energy: direct + sun on a white floor equals the sum of the two references (the zero fallback
   adds nothing). `hd_scatter` matches the FresnelBlend formula on 368640 directions.
   White furnace: the albedo is at most 1 (worst 0.968), for roughness 0.3/0.6/1 and wo 0/60/85 deg.

## Tolerance

All consumers store these radiances in FP16 targets (rgba16f), so the bound is half an FP16 ULP:
|gpu - ref| <= 2^-11 |ref| + 2^-24. The FP32 shader error is about 1e-6 relative (SPIR-V
precision of pow, exp2 and rsqrt), far below that bound. The worst observed error is 6e-6.
An expected zero must be exactly 0. Monte Carlo cases use 5 standard deviations of the
estimator, taken from its analytic variance.

## Sun-disc frame near the zenith (fixed 2026-11-01)

`3g` checks the disc sample's tangent frame with the sun within 8 degrees of the zenith
(|L.y| >= 0.99, Y up in lighting space). It used to fall back to t = (1,0,0), which is not
orthogonal to L: the disc shrank to 0.995 of `sun_radius` at L.y 0.993 (printed as FINDING).
`native_hit_sun.hlsli`, `gi/rt_shadow.hlsl`, `gi/rt_local_shadows.hlsl` and the frozen
`gi/ls_ref/ls_ref_body.hlsli` (a marked hand edit, so `ls_check` stays bit-exact) now use
`normalize(cross((0,0,1), L))`, as the `RT_SUN_CURRENT` path. `3g` is now a PASS line.

Runtime on an RTX 5090: about 2 s wall in total. The dispatches and readbacks take 0.03 s.
