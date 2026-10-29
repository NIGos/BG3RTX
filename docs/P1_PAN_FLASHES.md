# P1 transient shadow flashes: positive control first

## Latest FG-on verdict — 2026-10-29

FG 2 is decisive; FG 0 is reference only. Latest authorized mode is visible,
non-activating native-windowed real presentation without USER_AWAY, blocked by
USER_PLAYING or an unowned game. Never force focus. ABI is now 16.
Static parity PASS (13 phases, 30 traces), primary ~99.52%, 18.7-18.8 fps,
FG ratio 2.00. Five PNG sheets: captures/p1-fg/static-effects-sheets/.
GI debug black in both modes is only parity evidence, not proof of visible GI.
Motion qualification INVALID: 13 complete of 56 phases, then partial phase 13;
posted rotation fidelity and sustained baseline FG generation failed. First
negative FG 2 primary baseline/rebase/destroy-only 21.299/85.984/20.5465%,
static floor 99.501%. RT active in every sampled window. This does not certify
transient final-image flashes or exact TLAS frame lag. Both fixes default-off;
no merge/readiness claim. Repair relative background rotation before renderer
work. Reports/logs and limitations: docs/evidence/p1-fg-live-20261029.json.
The older evidence below is historical and does not supersede this FG-on gate.


The user reports new, brief dark wall/shadow regions during camera motion,
clearing when the camera stops. The current task supersedes the older P2 slot.
Do not change renderer timing until the hands-off test reproduces the symptom.
Only ask the user for the final in-game confirmation after the gate passes.

## Runtime and capture

Use scripts/test-queue.ps1, current branch codex/p1-pan-flashes in the main
workspace, paired ABI 15. Save QuickSave_3784 comes from the preserved user log:
captures/p1-pan-flashes/source-logs/user-0929-0934.log. No user save writes.
Use real presentation and FG off first. Latest permission allows a visible
second-plane window while the user uses the PC, without USER_AWAY. Never steal
focus, run with USER_PLAYING or launch alongside a user-owned game. Existing
auto-run display helpers restore the user's settings when the job exits.

pan_capture=0 and pan_prepare=0 are defaults. Preparation prewarms diagnostic
pipelines. Every burst retains 16 consecutive record indices, all channel data,
GPU camera headers and record/submit/scene-build indices. It writes only after
the entire burst; no disk write or queue-idle wait inside the measured window.
Capture dimensions come from actual resources, independent of desktop size.

Required channels: depth, normal, albedo, material, primary-ray depth residual,
sun visibility, local-light visibility, GI, native final colour, scene pose.
Final colour is the native hudless tonemap output (the existing tracer and
Streamline signature), not an assumed swapchain render attachment. Packed HDR
Inf/NaN remains nonfinite. Missing/unsupported/nonfinite image data is INVALID.
The native scene UBO footer contains unused metadata; validate the actual camera
matrix and xyz origin, not that unused word as an image/pose component.

## Analysis and positive control

Run python -m scripts.qa.pan_artifacts on the burst paths in that run's queue
log. The report and contact sheets are produced without user actions. Final
colour is an encoded-display luma proxy; masks and GI remain separate signals.
GPU frustum/origin data drives reprojection. Do not register unrelated images.

Stationary textures need sampling uncertainty, not exact RGB equality. Bounds
use both half-texel footprints plus UNORM8 conversion intervals. Material flags
remain exact; depth and normal checks exclude incompatible surfaces. Static
camera classification requires an identical native origin and at most the
sqrt(2)-pixel two-raster-jitter support. No angle is fitted to the result.

Calibrate per-world-surface noise from the first static burst. Reproject its
local repeat bound into each later anchor, requiring matching G-buffer surfaces.
A noisy point must not disable the detector everywhere. Native storage precision
sets each signal's code step. Unseen/uncalibrated surfaces stay unscored.
The second static burst is independent evaluation, not reused calibration.
Transient valleys last 1-3 frames and must recover; persistent shadows and
incompatible surfaces are excluded. Report events, area, attribution and strips.

The positive-control status requires final-colour transient excess AND primary
mismatch versus independent static evaluation at comparable capture cadence
(user baseline approximately 18+ fps). This is distinct from a renderer-fix gate.
Record timestamps estimate burst cadence; native real-presentation/pace logs are
retained separately. Record/submit age is not camera-pose lag. Exact conversion-
camera origin matches constrain offsets, but do not prove instance-transform timing.

## Current evidence

Initial a0bb416 run: queue execution valid, quality INVALID because final colour
was absent. Static/slow/fast record cadence 18.82 / 16.84 / 18.82 fps; primary
match windows 99.534% / 88.044% / 30.922%. Fast GPU camera origins consistently
match offset +1 in the ten tested centres. No lag fix follows from that alone.

08a9c39 transport validation: 176 samples / 16 frames, all required image signals
finite. The native unused footer is retained raw. No copy-image VVL tag appears;
other VVL errors remain, including an unattributed source-buffer usage error.
Validation capture cadence is 11.86 fps, so this is not a positive control.
Evidence: docs/evidence/p1-pan-final-transport.json. Full VVL is preserved at
captures/p1-pan-flashes/08a9c39-final-validation.log.

The analyzer now passes 13 synthetic tests, including local noisy-point isolation,
jitter versus translation, format precision, missing final, packed HDR Inf/NaN,
metadata fields, 1-3-frame flashes, persistent shadows and disocclusion. Host/core
build and all three CTests pass. Renderer synchronization remains uncorrected.
Next run tests/fixtures/p1_pan_positive_job.json: static calibration, independent
static evaluation, slow pan and faster opposite pan in ONE run. No renderer fix.
Only after reproduction, confirm the actual geometry/camera epoch mismatch,
add the smallest default-off correction, and compare with the same tool. The
fix must reach static flash/mismatch levels before default promotion.
FG toggling/invalid inputs and the roughly 18 fps base rate follow P1.
