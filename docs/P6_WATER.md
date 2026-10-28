# P6 water: design for coverage gap 1

Design only, 2026-10-28. No water rendering code, new runtime key or default
change is included. P1 remains the runtime priority. This meets the requested
minimum P6 design deliverable; it does not close the water coverage gap.

## Problem and deciding scene

[COVERAGE.md](COVERAGE.md), gap 1, reports that the S1 docks' forward water draw
(`FLUID_Water_Plane_flowmap_huge_PixelLighting`) changes 89.2% of the image.
The deferred G-buffer is empty over much of it. The current G-buffer capture in
`layer/geocapture.cpp` therefore supplies neither a water receiver nor a water
interface to the ray-tracing scene. Existing opaque GI, AO and reflections can
hit the bed or nothing while the visible water retains its forward lighting.

S1 `QuickSave_3784` is the deciding docks save. Its old table label says foundry;
the measured captures in COVERAGE establish the docks view. Verify the loaded
view and water shader before accepting a run. S8 Water Queens House tests water
near columns and reflective stone. S3/S4 provide mixed foliage, shore and foam.
S5/S6 lava is an exclusion control, not a water reference.

## Inputs: available and still unproven

| Input | Evidence / intended use | Remaining proof |
|---|---|---|
| Forward draw identity | AUDIT_ENGINE_DATA 2.9, material DB pass `FOR`, water class / `BG3_MAT_F_WATER` | Match the runtime shader variant, material and descriptor tuple; reject ambiguous aliases |
| Deformed surface positions | Existing XFB patch records final `gl_Position`; recover world positions with the draw's actual camera | Validate forward camera/layout conventions and indexed, instanced and displaced variants |
| Surface shading normal | FLUID shader's normal/flow/wave evaluation | XFB positions alone do not capture normal maps; extract the actual forward shader evaluation and texture transforms |
| Material parameters | AUDIT_ENGINE_DATA A8 names ColorShallow/Deep, blend distances, distortion, Roughness_Base, Reflectance_Base, Flowmap, WaveStrength | Generic `Bg3MaterialInfo.slot` does not expose all of these roles; preserve named bindings and prove runtime offsets per family |
| Before-water colour | AUDIT_ENGINE_DATA R5: compose colour copy used for refraction, forward set 2 binding 4 in the audited shader | Discover by pass/binding relationship, not recorded image handles or unit numbers |
| Native water lighting terms | Forward shader and its retained lighting descriptors | Identify exact reflection, transmission, sun and local terms before replacing any of them |
| Previous water motion | Previous object/deformation and flow/wave state | Not established by opaque camera MVs; changing wave normals need their own history validity |

The source evidence is local: [AUDIT_ENGINE_DATA.md](AUDIT_ENGINE_DATA.md)
sections 2.9, A8 and R5, [ASSET_DB.md](ASSET_DB.md),
`common/assetdb_api.h` and `tools/assets/extract/materials.py`.
The audit's feasibility assessment is not evidence that forward XFB or its
material decoding is already implemented.

## Rendering contract

Water is an interface between media, not an opaque occluder. Add a separate
water geometry class and ray policy. Do not insert it into the existing opaque
mask and turn all GI/shadow queries into black water-shaped blockers. Keep VFX,
foam billboards, glass and lava outside this class unless separately modelled.

For a supported interface, separate reflected and transmitted radiance using
the material's Fresnel model and roughness. Use its validated reflectance or
IOR mapping; do not infer physical absorption from an authored colour ramp
without a units/path-length derivation. A supported absorbing medium uses
`T(lambda, distance) = exp(-sigma_a(lambda) * distance)` along the actual
refracted segment. Reflection and transmission weights must conserve energy,
including total internal reflection and the refractive radiance convention.
Missing medium/thickness or material mapping keeps the native transmission
term; it is not replaced with an invented deep-water colour or sky.

Reflected rays need scene radiance at the hit, including offscreen geometry.
Use a proven hit-material provider when available. Until EN2 and the relevant
lighting consumers pass, missing hit material retains the native reflection
contribution. A TLAS miss in an incomplete scene is not proof of open sky.
Roof/cut-away coverage and the established environment/room policy still apply.

Water shadow rays transmit through a supported medium/interface, rather than
using opaque alpha testing as a substitute for refraction or absorption. For
an unsupported crossing, retain the native shadow term. Do not claim caustics
from a single refracted visibility ray; focused transport is a later integrator
requirement. Report that limitation instead of creating unaccounted energy.

## Capture, lifetime and pass placement

1. Identify supported forward water draws from shader/material data. Extend
   XFB capture separately from opaque capture, preserving draw order, per-draw
   transforms, dynamic offsets, triangle counts and overflow checks. Unknown
   shader/material variants remain entirely on the native path.
2. Build a separate water BLAS after its captured writes complete. Supply a
   distinct instance mask or an equivalent explicit hit classification. Current
   forward capture cannot retroactively enter ray queries submitted earlier in
   the frame. Initially consume it only in later forward-water work; any
   previous-frame geometry reuse needs a proven motion/deformation contract.
3. Obtain depth, material identity, geometric normal and the shader's displaced
   shading normal for the visible water receiver. Preserve transparent-layer
   ordering and coverage. A first-surface buffer cannot silently replace a
   multilayer water/glass/foam blend. Unsupported overlap uses native shading.
4. Replace a validated native term at that term's composition point. Prefer a
   shader variant replacing the reflection/shadow term inside the forward
   shader. A later delta pass is allowed only when the exact old term and blend
   equation are available. Never add complete RT water lighting to an already
   lit forward result, or subtract the full image difference as "water".
5. Keep the pre-transparency colour copy immutable for its native consumers.
   Respect SSS, fog, exposure and the RR-before-post order. New water denoising
   must have consistent water depth, normals, roughness, motion and hit distance;
   opaque bed guides are invalid for the visible interface. Preserve the
   existing transparency path until that signal/guide contract is verified.

Pin captured buffers, images, descriptor sets and pipelines from recording
through actual submission completion. Barriers must cover XFB -> conversion ->
BLAS build -> ray query -> composite. Resize, cancellation, missing POST and
out-of-order recordings must not publish an unexecuted water history. A CPU
frame number or a resource name is not a GPU pairing certificate. No dependency
on desktop dimensions: allocate and dispatch from the actual attachments.

## Incremental implementation and rejection rules

Proposed future keys (not implemented): `rt_water_geometry=0` for capture and
intersection validation; `rt_water_reflections=0` for a proven reflection-term
replacement; `rt_water_shadows=0` for sun/local visibility; transmission follows
only after medium and material mapping are established. Every key defaults off.
No key implies that the other components are correct or available.

First implement geometry and a same-frame coverage check, without modifying
the native colour. Next validate exact native material/term recomputation with
the replacement disabled. Then test reflection and shadow replacements
separately in the same live session. Refraction/absorption follows those proofs.
Use existing traces and queue logs; add a counter only to answer an unresolved
input, coverage or lifetime question. Keep recording-only diagnostics out of
production timings.

| Gate | Evidence required before enabling a component |
|---|---|
| Identity and coverage | Actual supported water draws and nonzero surface intersections at the S1 docks; count excluded/unknown variants and visible uncovered water separately |
| Geometry | Same-frame raster/intersection depth agreement under bounds derived from depth precision, raster footprint, displacement and camera arithmetic; shore and thin geometry cannot be hidden by an arbitrary percentage allowance |
| Native inputs | Recompute the original supported term with the same resources; assess format-rounding/derivative differences explicitly, including wave normals, roughness, flow time and texture LOD |
| Composition | Replacement disabled gives the original result; pure-component tests prove no double reflection, no opaque-water shadow, and no accidental removal of foam or other transparency |
| Physics | Finite nonnegative supported transport, reflection/transmission energy balance, attenuation monotonic with physical path length; no light through existing roof/shore casters |
| Motion | One moving run, alternating the key or using exact surface reprojection; no cross-run image subtraction. Water animation must be distinguished from static docks and walls |
| Fallback | Unknown material, missing bindings, unproven geometry, unsupported overlap or unavailable guides preserves the corresponding native term |
| Lifetime | Resize/reload/failed capture and submission-order checks; inspect deciding validation messages, driver events and dumps |

Do not invent a fixed acceptable error percentage. Derive numerical limits from
the actual formats and operations; derive stochastic quality limits from the
same-run signal variance. Missing coverage or an unproven mapping is incomplete,
even when the frame-wide mean looks good. Optimize expensive work after quality
is established on Ultra; do not reduce that target to meet a timing threshold.

## User check and completion

When implemented, log which water components actually activate. At the S1 docks,
pan along the pier: reflected posts and shore buildings should stay attached to
their surfaces, shoreline contact should remain stable, and the bed should not
become a black opaque shadow. At S8 check columns, water/stone boundaries and
overlapping transparency. Toggle each water key in one session, then use
Ctrl+Shift+F9 for the all-RT reference. Brightening alone is not a failure.

This design is complete as a planning deliverable. Runtime water support remains
unimplemented and gap 1 remains open until the applicable gates above pass.
