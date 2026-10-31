# Physical Hair secondary origin candidate

Default-off key: `rt_hair_physical_origin=0`. This is a candidate correction, not
confirmation of the user's darker/dithered Hair attribution. No game/GPU check
was performed by this worker. Native sampling and other Hair-family gaps remain.

Native Hair camera depth moves the apparent surface behind its base card. Camera
primary queries reproduce that virtual surface. Physical secondary queries use
the thin base ribbon and native Shadow coverage. Previously `rtOrigin` therefore
launched secondary rays from the virtual surface: a camera-facing light could
intersect the same card in front of that origin. The ordinary ULP offset removes
floating-point self-intersection; it cannot remove a real material displacement.

The candidate leaves `rtOrigin` unchanged for primary checks and history matching.
Only after the existing virtual primary match succeeds does it reconstruct the
physical point/normal from the **same selected procedural instance/primitive**.
Auxiliary eligibility additionally requires raster material class 5. Audited
native deferred FS `42a95e17ec3adc51` (Hair static), `0ca5ee93d0f33b8c`
(Hair instanced), and `7362bcdec4c0415c` (Pubes static) always write
`(5 | ((channel & 7) << 4)) / 255` to RT2.a. Depth-near skin/other
receivers retain the original origin; class 5 does not prove same-card identity.
Depth-only FS do not write the material class. The accepted Hair mathematics
and ordinary virtual `rtOrigin` remain unchanged.

The triangle test starts at camera zero, with the same scene rebase, avoiding
cancellation from a virtual search window far behind the physical card. Ambiguous
triangle edges and invalid source state retain the original path and are counted.

The new full-render-resolution RGBA32F image stores a unit physical normal and
positive physical radial distance. Inactive pixels have distance zero. Direct
distance avoids half-precision loss and offset-ratio cancellation near -1.
Reconstruction normalizes the raster direction using its largest component;
its arithmetic error scales with the physical distance. A gamma16 FP32 envelope
checks encoding/normalization in CPU cases. This is not a full native/GPU error
certificate. The existing ULP offset and terminator fallback remain unchanged,
but both are anchored at the physical point. No bias value or guard is weakened.

Sun, GI, AO, reflections and local visibility use this launch data. Finite-light
segments use the physical receiver when choosing their direction/soft-light
sample, then preserve the actual center (including hard mode with nonzero
source radius) or sampled near-sphere target after the
origin offset. Box-light exit is already computed from the final origin. The
shared CPU/HLSL segment helper rejects an unresolved sphere intersection; it
does not invent a hit or a miss. Native shading parameters and the RT cone
footprint policy are unchanged. Frozen local-light arithmetic is unchanged;
its infrastructure receives the corrected receiver geometry under this key.
Its pre-existing procedural caster traversal limitation is unchanged.

The image costs exactly `16 * renderWidth * renderHeight` payload bytes. At the
observed 2560x1067 render resolution: **43,704,320 bytes / 41.6796875 MiB**.
Allocation padding and GPU pass cost remain unmeasured. Output resolution is not
used for this allocation. There is one image, not an eight-slot image ring.

Resource/layout capability is latched at first resource initialization and stored
in host persistence across core reload. Starting this candidate with the key off
cannot hot-enable it; restart with this key and `rt_native_hair=1`. Starting on
reserves the image/layout and allows runtime `1 -> 0 -> 1` A/B in one session.
The GUI marks it Experimental in every preset, with conditional startup guidance.
It requires `rt_origin=1`; no host ABI change or shader-loader change is needed.

Startup off leaves descriptor layouts/push writes unchanged. Startup on adds set 0
binding 50; RF omits proven-unused binding 12, retaining its 32-descriptor limit.
OMM bindings 10/11 remain in set 1; Sun/native/reflection/SHaRC layouts retain their
existing set identities. Thin wrappers use the existing compiler mechanism and
preserve first-line 16-bit options. The frozen normal entry remains the key-off
path; no mutable shader-global feature state is used.

Verification is `native_hair_origin_test`, `settings_test`, the existing native
scalp shader check, and `tests/hair_origin_shader_test.py`. The latter compares
old/key-off SPIR-V byte-for-byte, validates feature modules and reflects every RF
variant to prove binding 12 is unused. CMake registers the new CPU test. Actual
GPU fixture and in-game A/B remain pending; no rendered-quality claim follows.
The current offline gate passes 100 origin checks, 272 settings checks, a private
full host/core/launcher build and 43/43 CTests after merging main 5f3e93b,
112 ordinary byte-identical SPIR-V modules, 112 validated
feature modules, and binding-12 exclusion in all 16 RF variants. The independent
existing scalp gate also passes 95 ordinary identity/95 capable validation cases.
Independent review corrected hard nonzero-radius light targeting; all 48 affected
local feature modules compile/validate again with frozen inputs.

Later automated same-run checks need both mode markers, RT active, nonzero
matched-Hair/available-origin counts, and recorded unresolved counts. Counters
188/189/190 report those pixels through existing `rt_stats`. Primary agreement
must be compared to this scene's actual off/off repeat baseline, not an assumed
99.5%. Compare Hair-local visibility/final energy and unchanged non-Hair pixels;
report existing `rt passes` GPU cost. Missing RT or absent affected Hair cannot
pass. The user has not tested this feature; automated work can continue.
