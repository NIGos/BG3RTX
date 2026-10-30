# Solar angular size and outdoor penumbra reconciliation

Read-only source reconciliation at a676bf5. No game, GPU, frozen trial access,
new captures, settings changes or renderer edits. This addresses exploration
outside dialogue; closeup windows are not substituted for the outdoor case.

## Physical angular size is not too narrow

`core/gi.cpp` uploads `rt_sun_radius` directly, with default 0.00459 radians.
`shaders/gi/rt_shadow.hlsl` normalizes `cam.v0.xyz`, samples a disc with
`sqrt(u1) * tan(radius)`, and normalizes the perturbed direction. There is no
extra degree conversion, half-angle division, camera-distance scaling or light
intensity multiplier in this angular construction. The resulting half-angle is
0.262987628 degrees, diameter 0.525975256 degrees. Native sun specular uses
sin(theta)=0.0045900648 (`shaders/rt/bg3_sun.hlsli`); asin gives
0.004590080918 radians. The RT default differs by only -0.001763 percent.
This establishes agreement with the native apparent specular disc, not equality
with a particular live native PCSS UBO angle.

For a planar blocker and receiver perpendicular to the central sun direction,
full geometric transition width is `2 * separation * tan(radius)`. Separation
is blocker-to-receiver distance along that direction, not distance to the camera.
Oblique receiving planes stretch the footprint; general geometry is not a
constant screen-space blur.

| Separation | Full transition width at default radius |
| --- | --- |
| 1 cm | 0.0918 mm |
| 10 cm | 0.918 mm |
| 1 m | 9.180 mm |
| 10 m | 91.801 mm |
| 50 m | 459.003 mm |

The metre labels assume the existing lighting-P metric-space contract; they do
not certify every retained cascade transform. Contact hair/leaves may therefore
have physically sharp solar edges. A distant caster separated by many metres
should produce a wider transition in scene units. Camera distance only changes
its projected pixel width. Ray tracing alone does not promise a wide penumbra.

The sampler is uniform in the tangent-plane disc, a small-angle approximation to
solid-angle source integration. At this radius its measure variation is only
about 3.2e-5 across the disc; this cannot explain conspicuous uniformly hard
shadows. `pixel_seed` hashes pixel coordinates and `pc.frame * 4 + salt`, and
`pc.frame` is uploaded from the host frame index. Seeds are not intentionally
frozen between frames. One ray still returns binary visibility per selected
layer: a single raw frame is not an analytically resolved penumbra.

## Actual outdoor evidence supports preserved native edges

Retained evidence is `sun-penumbra-review-20261030.json`, not a new measurement.
Capture 162008 is overhead exploration on the ruined bridge; 163004/163010 are
overhead camp trees/rocks. The configuration records replace=1, strength=1,
offscreen=1, ramp=0.75, radius=0.00459, cascade casters=1, cascade roof trust=0.
The radius is a retained configuration value, not a GPU radius readback.

| 600-frame window ending | Traced/all pixels | Native-dark and RT-lit/traced | Fully replaced/disagreement | Kept offscreen/disagreement | Offscreen ramp/traced |
| --- | --- | --- | --- | --- | --- |
| 16:20:06.894 | 86.1% | 8.99% | 27.1% | 56.1% | 69.6% |
| 16:20:17.747 | 78.8% | 7.01% | 34.7% | 45.1% | 59.0% |
| 16:30:07.580 | 92.4% | 39.33% | 26.5% | 55.9% | 63.7% |

These temporal windows surround retained world views; they are not ROI-only or
proof that every frame/pixel has that exact pose. Their correct denominators
matter: offscreen keep is a percentage of the native-dark/RT-lit disagreement,
not all pixels. The six earlier dialogue windows are not the primary evidence.

The actual fold is multiplication, despite old comments calling it `min`:
at strength=1, untrusted visibility is `M * V`. Resolve at trust T writes
`(1-T) * M * V + T * V`. Thus wherever M=0 and T=0, a lit RT sample cannot
restore direct-sun visibility on that side of the native edge. A nonzero native
filtered mask also attenuates the expected RT visibility. This is a demonstrated
mechanism in exploration as well as closeups, not a universal attribution of all
visible shadow edges. It is the reason to qualify current native caster coverage
before replacing the native mask, rather than widening the sun or lifting trust.

Sun radiance/colour, atmosphere, clouds and the native scale multiply direct
lighting later (`bg3_sun.hlsli`); they do not shrink the ray cone. They can change
perceived contrast. A zero direct-sun component does not imply a black final
pixel, because indirect/local lighting remains. RR sees the composed noisy
lighting; it cannot recover the missing unbiased direct-sun visibility merely
from a mask that suppressed those samples. There is no separate Sun-mask temporal
accumulator or implemented SIGMA pass in this dispatch chain. The older design's
RR-off SIGMA/penumbra bilateral item is still a design, not a completed filter.

## Native PCSS is not a physical angular-size reference by itself

The exact native receiver audit is `native-csm-receiver-20261030.md`.
Its PCF radius is proportional to
`min(0.21, tan(theta) * (meanBlockerDepth-qz) * D) / W`, with a pixel-size floor,
then selected/next cascade blending. The engine's actual theta bytes, metric
conversion and live sampler cannot be inferred from the decompiled equation.
A finite 0.21 cap and pixel floor can themselves make widths less distance-varying
than an unbounded physical disc. If theta equalled our default and the units were
metres, the cap would activate at roughly 45.75 m axial separation; those two
conditions are not proven for these captures, so this is conditional algebra,
not a claim about the game's measured cutoff. Do not tune RT to match that cap.

## One proven small sampler defect is already corrected in the candidate

The ordinary path uses tangent `(1,0,0)` when `abs(L.y)>=0.99`. If L.x is nonzero,
that vector is not perpendicular to L. Its companion cross product is not unit
length, so the source is slightly anisotropic. In this branch L.x squared is
below 0.0199: perpendicular radius compression is at most about 1%, plus the
small normalization skew from an axial component. This cannot substantiate the
large general outdoor complaint, and the retained evidence has no actual L
readback establishing that this branch was used.

The default-off `RT_SUN_CURRENT` candidate already uses a normalized cross with
an alternative nonparallel axis for this case. No additional key, radius increase,
bias, blur or speculative renderer correction is warranted by this audit. Ordinary
key-off shaders are intentionally untouched. The substantive pending correction
is current same-frame native-domain coverage and replacement, already implemented
behind `rt_sun_cascade_current=0`, with actual GPU/visual qualification still open.

Next coordinated check should distinguish raw visibility/native mask/trust from
final denoised lighting at the same outdoor pose and actual caster separation.
No new tool or live request is created here. Existing historical aggregate RT
cost is 1.130-2.521 ms in the earlier closeup evidence, not a new Sun-only cost or
an outdoor quality measurement. This documentation change has no runtime cost.
