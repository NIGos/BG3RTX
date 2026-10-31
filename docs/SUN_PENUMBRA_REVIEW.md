# Solar penumbra review - 2026-10-30

Offline source and retained-log audit. No game, settings, shaders or binaries
changed. Save files were not accessed. This is a diagnosis of a current coverage
limitation, not a measured physical sun-shadow gate or a proposed blur setting.

## Finding

The finite solar source is implemented correctly, but the final direct-sun mask
often remains constrained by BG3's native mask. In the dialogue windows around
the retained F10s, the conservative off-screen guard prevents almost all
native-dark/RT-lit disagreements from being replaced. This can preserve an abrupt
native edge or suppress the bright side of an RT penumbra. The user's observation
is therefore plausible; a nonzero solar radius alone does not settle it.

These close-up windows do not establish the cause of every outdoor shadow. They
also do not attribute the separate hairstyle-specific face-shadow defect.

## Source trace

- `core/gi.cpp:1243` passes `rt_sun_radius` unchanged, default 0.00459 radians.
  `rt_shadow.hlsl:103` normalizes `cam.v0.xyz`; lines 122-129 sample a uniform disc
  using `sqrt(u1) * tan(pc.sun_radius)` in an orthonormal tangent frame, then
  normalize the resulting direction. No degrees/radians or missing-normalization
  defect was found. The disc is flat uniform radiance, without solar limb darkening.
- `gi_common.hlsli:187` seeds by pixel and `pc.frame * 4 + salt`;
  `core/gi.cpp:1232` uses the host frame index. The sun is not using one frozen
  direction across the image or a static per-pixel seed.
- One ray per pixel returns binary visibility. There is no dedicated sun-mask
  temporal accumulation, blocker-distance filter or sun-mask denoising pass in
  this dispatch chain. RR can reconstruct the resulting lit image when active;
  RR off still leaves stochastic sun sampling enabled. Lack of a sun-specific
  denoiser is a separate limitation, not proof that RR causes these hard edges.
- `rt_shadow.hlsl:205` writes `fold = mask * lerp(1, vis, rt_strength)` for known
  rays. Unknown rays keep the native mask. The misleading nearby comment says
  min(game, traced); the implemented operation is multiplication.
- `rt_sun_resolve.hlsl:45` uses geometric trust to interpolate between that fold
  and RT visibility. At strength 1, outside roof modes, the sun-component mask is
  `V * (t + (1-t)*M)`, where M is the native mask, V the RT sample and t trust.
  Full trust gives V. At t=0, M=0 prevents RT from providing direct sun even when
  V=1. If M already approximates the same soft visibility, multiplication can
  also count occlusion twice; the exact expectation depends on signal correlation.
  This statement concerns direct sun, not total image luminance including GI.
- `sun_offscreen_trust` (`rt_shadow.hlsl:64`) tests the central sun ray against
  the camera frustum until `rt_sun_rise` or scene top is reached. Capturing a sun
  cascade does not remove this guard. `rt_cascade_roofs_trust` changes roof-volume
  handling, not this frustum test. Close-up camera frusta can make this especially
  restrictive even when the visible receiver is entirely on screen.
- Normalized rays use `rt_tmax` (300 m configured). Ray-origin offsets handle
  self-intersection, not source softness; increasing their bias would not repair
  this mask-composition limitation.

## Retained evidence and effective configuration

Source log: `captures/dialogue-shadow-review-20261030/user-session.log`.
F10s at 16:18:07 (57830), 16:18:18 (58438), 16:18:45 (60034).
The log records base `bg3rtx.ini` plus user preset ultra, RR on, RT sun on,
cascade casters 1 and FG 2. Actual sun stats below confirm replace 1 and cascade
roof trust 0. Current unchanged base/settings specify radius .00459, offscreen 1,
rise 6 m, ramp .75, roofs 1, roof mode 0, strength 1. The retained user ini has no
sun overrides and no preset override for these values was found. Radius itself
is not logged per frame, so this is configuration-source evidence, not a GPU
push-constant readback certificate.

Each row covers 600 frames, not just its adjacent F10. Percentages have different
denominators: ramp is a fraction of traced pixels; kept/fully replaced are
fractions of native-dark and RT-lit disagreements among traced pixels.

| Window end | Traced pixels | Off-screen ramp | Disagreements kept off screen | Fully replaced disagreements |
| --- | ---: | ---: | ---: | ---: |
| 16:18:05.934 | 87.2% of image | 99.6% | 98.1% | 0.3% |
| 16:18:17.233 | 85.8% of image | 98.6% | 96.2% | 0.9% |
| 16:18:32.665 | 83.6% of image | 96.2% | 94.6% | 1.3% |
| 16:18:39.826 | 84.8% of image | 97.7% | 96.1% | 0.8% |
| 16:18:50.644 | 84.9% of image | 99.5% | 96.6% | 0.7% |
| 16:18:57.337 | 85.8% of image | 99.5% | 97.6% | 0.9% |

Roof-kept disagreements are 0.0% in all these windows; the measured limiter is
off-screen coverage. RR GPU evaluations are present near the captures, so simply
missing RR is not an adequate explanation. RT scope logs use current TLAS 300/300.
Existing logged RT scope cost in 16:18 is 1.130-2.521 ms/frame; that scope includes
other RT work and does not isolate the solar pass. No new GPU cost was measured.

The retained F10 has the native R8 sun mask (`161818/d401`) before our injection,
and final diffuse, but no independently identified raw rtVis/trust dump. There
is no same-pose radius/trust A/B or caster-distance reference. Native-mask PNG
previews are downscaled and are not a precise edge-width measurement.

## Physical expectation and next correction

### Exploration is the user's primary case

New user clarification: the sharp/uniform shadows are especially visible outside
dialogues. The close-up percentages above are mechanism evidence, not primary-case
attribution. Retained final PNG inspection identifies 162008 as an overhead world
view of the ruined bridge, and 163004/163010 as overhead world views of camp trees
and rocks. 161807 is a wide Shadowheart DIALOGUE view, and 162506 is an Astarion
DIALOGUE; those two must not be counted as exploration controls.

Relevant 600-frame windows have less extreme but still substantial fallback:

| Window end / nearby world capture | Traced pixels | Off-screen ramp | Disagreements kept off screen | Fully replaced disagreements |
| --- | ---: | ---: | ---: | ---: |
| 16:20:06.894 / 162008 | 86.1% of image | 69.6% | 56.1% | 27.1% |
| 16:20:17.747 / 162008 | 78.8% of image | 59.0% | 45.1% | 34.7% |
| 16:30:07.580 / 163004, 163010 | 92.4% of image | 63.7% | 55.9% | 26.5% |

These are aggregate windows around capture times, not region-matched sun-mask
readbacks. The same composition limitation therefore remains relevant in retained
exploration, but the extreme dialogue percentages cannot be transferred to it.
The two camp frames are six seconds apart, not a frozen-scene A/B; animated foliage
or changed geometry can also alter individual shadows. No specific tree edge has
yet been proven dominated by fallback rather than trusted RT reconstruction.

Radius .00459 means .526 degrees full diameter. For a straight edge and suitable
receiver geometry, full penumbra width is approximately `2*d*tan(radius)`:
0.184 mm at d=.02 m, .918 mm at .1 m, 9.18 mm at 1 m, 91.8 mm at 10 m.
Orientation and projection change the apparent width. Hair near skin can be
physically sharp; distant caster shadows should not all retain the same edge
width in world space. A single binary sample is not a resolved penumbra profile.

Do not disable the off-screen guard or increase radius/bias to conceal incomplete
caster coverage. The smallest defensible rendering correction first needs a
verified sun-caster coverage volume, including alpha/depth correctness and
retained geometry, so resolve can trust rays outside the camera frustum when
that caster volume actually covers them. A complete cascade flag alone is not
such a certificate. Without it, lifting trust risks light leaks from omitted roofs
or cutouts. No renderer change is justified by this audit alone.

Later user-coordinated check: same stationary view, `rt_debug=6` for the trust map
and `rt_debug=2` for native/RT/final sun masks, with one near-contact shadow and a
known distant caster shadow. Read existing sun stats over the same window. Verify
that full-trust penumbra profiles grow with caster separation and preserve mean
energy before considering an accumulated sun-mask implementation. No new live
job was launched or queued here.
