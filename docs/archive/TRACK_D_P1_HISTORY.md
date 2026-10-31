# Track D P1 history (2026-10-29): FG-on gate, rotation, rebase, flashes

Archived from TRACK_D_RT.md on 2026-10-31. Superseded; kept for history. Current P1 state:
`rt_scene_wait` in [../TRACK_D_SCENE_BUILD.md](../TRACK_D_SCENE_BUILD.md), and
[P1_PAN_FLASHES.md](P1_PAN_FLASHES.md).

## Live FG-on primary gate — 2026-10-29, NOT passed

Static parity PASS at primary 99.5195-99.5215%, 18.7-18.8 fps and actual FG 2.00.
Motion matrix deliberately stopped after 13 complete phases plus partial phase 13:
posted 60 px/s input produces stationary periods and up to 98.58 deg/frame jumps;
baseline generated-presentation ratio falls to 1.00. It is INVALID qualification.
First negative FG 2 rotation primary median baseline/rebase/destroy-only:
21.299 / 85.984 / 20.5465%, versus static floor 99.501%. All sampled RT 120/120,
none 0. No visible-flash certification; different paths/views confound attribution.
Both keys remain off; no merge. See docs/evidence/p1-fg-live-20261029.json.

Translation rebasing alone is insufficient. Residual pool/soup mismatch and
scene/lighting pose separation support investigating older instance/dynamic
geometry or camera-buffer epochs, after qualifying no-focus relative rotation.
They do not prove a native matrix frame index or a Reflex cause. No new selector.

Cost evidence: in the same FG-2 static debug phase, disabling full per-ray stats
raises 15.5 to 19-20 fps, GI ~6.8 -> 0.379 ms and LS ~5.25 -> 0.530 ms. Source
p1-fg-static-effects.1.145628.log, 15:02:53-15:03:18. RT 2.872 ms is a mixed
300-frame interval. Historical 15.040 vs 4.269 ms remain confounded by settings,
scene/presentation; RR on in both. No isolated rebase performance regression.
Lightweight primary/epoch producer now counts with full stats off, with no
lighting change: 21 ordinary SPIR-V byte identities, 20 variants validate.

## P1 deciding configuration is FG ON — 2026-10-29

Latest steering supersedes the FG-off-only gate below: the visible symptom is
reported only with FG on. Paired fg=0/2 jobs use real foreground presentation;
FG 2 decides, FG 0 is reference. No runtime matrix has run. rt_scene_rebase and
fg_current_destroy_only remain default-off; no merge or renderer promotion.

The static job compares one GPU-verified view in both configurations: sun,
local shadows, GI, reflections and RR per-pass counters plus three repeats of
each debug view. Noise/preview/jitter-derived differences block the motion job.
The motion matrix tests rotation at three speeds/both directions and slow/fast
pans, with baseline/rebase-only/destroy-only in both FG configurations. Actual
generation, active effects/RT, GPU motion and >=18 rendered fps are required
with FG on; candidate primary match must reach the FG-on static level ~99.2%.
The destroy candidate additionally needs fewer interior FG toggles. See
tests/fixtures/p1_fg_{static,motion}_job.json and PROGRESS_CODEX.md.

The diagnostics record primary/FG-toggle association and sampled lighting pose.
Code audit finds app g_frame incremented once; generated presents go directly
to the next layer and update displayed counters. ReflexSleep/new token runs
after app present, including the initialized FG-off reference. This does not
prove causation or native instance-row epoch. No speculative Reflex change.
Seven CTests and 19 new matrix/static/fixture tests pass; no HLSL/ABI changes.

## P1 rotation qualification and epoch diagnostics — 2026-10-29, pending

Rotation is now the main primary-match gate, approximately static 99.2% with
active RT. Default-off `rt_scene_rebase=0` remains unchanged. New default-off
`rt_primary_epoch=0` selects a diagnostic primary variant: it uses the same
single closest ray, reports nearest-hit pool versus dynamic-soup cohorts, and
keeps misses in the global denominator. Ordinary shaders remain byte-identical.
GPU scene snapshot is enabled for this diagnostic even with rebasing off.

rtStats 136..143 hold cohort counts; 144..150 store source/lighting translation,
rotation separation and lighting forward; 151 validates the stamped snapshot,
152 marks diagnostic presence, and 155..157 hold native absolute camera origin.
CPU aggregation measures actual angular speed/signed yaw and static translation
with FP32 precision bounds. It does not infer motion from requested inputs.
These source-pose observations do not establish the frame of every native
instance matrix. If pool mismatch persists after origin correction, compare
actual same-draw GPU native rows to TLAS rows before asserting an older epoch.

Prepared foreground real-present FG-off QuickSave_3784 jobs: four static
90-second qualification windows, then sixteen 90-second static/rotation
off/on windows at 60/120/240 px/s, both directions. Strict USER_AWAY required.
No runtime result yet. Failed qualification blocks the A/B on the same build.
Expected reports: captures/p1-pan-flashes/rotation-{preflight,gate}-result.json.
Missing RT, ignored input, insufficient cadence or early exit invalidate it.

Old exit at 242 s was not the 750 s deadline or cleanup. Cause remains unknown.
FG off skipped VEH installation: fixed, with optional crash_dump and retained
process status. No crash fix claim before runtime qualification.
Old RT interval 15.592/15.040 ms off/on includes primary/GI/AO/sun; historical
4.269 ms had different scene/settings and missing RT. Residual remains flagged;
preflight isolates diagnostic overhead and static rebase delta in one run.
Six CTests, 14 Python gates, 18 input/policy checks, 21 ordinary shader identity
checks and 20 variant validations pass. No promotion or merge.

## P1 lighting-to-scene translation — 2026-10-29, gate incomplete

`rt_scene_rebase` (its gi/rebase and gi/cascade_rebase wrappers), `rt_scene_camera_match` and
the host's `rt_scene_camera` (ABI 16): removed 2026-10-31, see CONSOLIDATION.md C1 (ABI 22).
The `RT_SCENE_REBASE` blocks of rt_common.hlsli and hair_receiver.hlsli stay for
tools/native_hair_gpu_probe's rebase cases; no runtime variant defines it.

314840d: one real-present foreground FG-off job exited before slow/fast windows.
Late static primary match off 99.164-99.177%, on 99.157-99.177%, RT active and no
missing scene in interior windows. Median cadence 17.60 / 17.50 fps. Initial
static camera drift and early exit invalidate the quality gate. Moving result
is unknown. No merge; key stays off. On RT-query cost 15.040 ms; off 15.592 ms
includes camera drift and cannot establish a cost delta. Evidence and next epoch
hypothesis: docs/evidence/p1-origin-rebase-314840d.json. No further job this turn.

Key: `rt_scene_rebase=0` by default; selector `rt_scene_camera_match=0` stays off.
All four query producers in rt_common translate ray origins by `C_lighting -
C_scene`. Their directions and t values are invariant. Closest-hit object
transforms have that delta removed before returning to lighting-space callers.
Alpha candidates remain entirely in scene space, including ray cone footprints.
The frozen local-light reference applies the same infrastructure translation.

The instance analyzer proves native `W=M_instance*object` reaches clip space only
through the camera-relative `_m2`. Dynamic XFB inverts that same `_m2`; the existing
mesh extraction and audit compare `M*object` with converted XFB. Both paths thus
use the source G-buffer camera origin and world axes, not a camera-view rotation.
This establishes the coordinate convention, not the epoch of the native rows.
Native instance rows are still read at submit. GPU camera snapshots are read from
the actual conversion UBO in the build command buffer, into offset 256 of that
scene slot's expanded stamp buffer. No CPU pose is used to compute the shader
translation. A matching pose generation is required when rebasing is active.

The layouts reuse native camera binding 48 (RT) or 29 (D-LS/D-RF), preserving the
32-descriptor reflection limit. Small wrappers compile the experiment separately.
21 ordinary SPIR-V binaries are byte-identical; 19 variants validate. Runtime
cost, moving primary-match gate and in-game instructions follow the single A/B.


## P1 transient flashes — 2026-10-29, experimental

`pan_capture` is a positive request serial, default 0. Each new serial records
16 consecutive lighting frames. Named raw images retain recording and submit
indices. Readbacks remain in memory until the burst ends; no disk/PNG work or
queue idle wait occurs between those frames. ABI 15 exposes the core's private
images to this bounded host readback path. Both DLLs must match.

`pan_primary.hlsl` writes a separate primary-hit residual normalized by the
existing geometric depth bound. Its header preserves exact GPU camera float
bits in FP16 byte lanes. The ordinary sun shader and final image remain intact.
`pan_local.hlsl` exports ls_debug=3 visibility without replacing the diffuse.
It requires the non-k full path for valid masks. All normal variants are unchanged.
The analyzer must reject missing channels, gaps and invalid transport. Pose
offset remains unproven until scene-camera evidence is connected; a one-present
recording age must never be treated as a demonstrated lag. No rendering fix
is promoted. Runtime tests and contact sheets are pending.
