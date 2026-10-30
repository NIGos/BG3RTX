# Optional vegetation transient support A/B

DRAFT ONLY. Not queued or authorized by this file. User has not tested this
comparison. It is optional supporting evidence, not a release requirement for
the independently demonstrated capture-capacity fix.

Hypothesis: cascade truncation at 160 MB contributes to transient dark flashes;
587 MB removes truncation. Camera capture stays 1408 MB at startup throughout.
This does not test or prove a camera/TLAS frame-lag mechanism.

`motion.plan.json` drives the existing posted mouse-rotation input in real
foreground presentation. There are four K=16 bursts: static 160, static 587,
fast positive rotation 160, fast positive rotation 587. Each rotation is followed
by an equal-duration negative return without a burst. No resolution change.
The first 160 MB motion burst is a reproduction control, not an assumed success.
No attempt is made to force darkness or alter shader coverage.

Nine phases take 265 seconds after load. Job timeout is 600 seconds, one run.
Dedicated 15-second static phases settle each live cascade allocation change;
bursts start 18 seconds into the test window, three seconds after rotation starts.
The diagnostic pass is prewarmed. `pan_inputs=0` is required: setting it to 1
changes the producer to 17 frames and adds FG/RR inputs this detector does not need.
Primary difference, G-buffer, sun visibility, local visibility, GI and hudless
native-tonemap final are captured by the existing path. Extra diffuse is retained.
These are app-rendered frames, not generated FG frames or the final UI composite.

## Resource estimate and release preparation

Actual R4 dimensions: render 2560 x 1067; GI 1280 x 534; output 3840 x 1600.
Conservative typical raw estimate per frame is `46*renderPixels + 4*outputPixels
+ 224` bytes: depth4, normal4, albedo4, material4, primary8, sun4, local8,
GI about2 per full-size pixel, extra diffuse8, final4 per output pixel and pose224.
Four bursts x 16 frames are approximately 8.96 GiB. Reserve 12 GiB for this output
and require at least 40 GiB free before release; retain raw evidence. Manifest
formats/dimensions provide the exact actual size, which can differ from this estimate.
The producer already caps each individual burst at 6 GiB and invalidates overflow.

Run `check-draft.ps1` offline before freezing a candidate. Coordinator must preserve
the effective R4 renderer/FG options, adjust unrelated draft settings if necessary,
and freeze the updated job/plan/build in a NEW finite receipt. This draft has no
`live_batch`. Never copy it into a watched inbox before authorized release. Existing
queue cleanup restores display/settings. No user INI or save edits are requested.

## Analysis and interpretation

The post command uses the existing analyzer and exact completed-burst paths from
the retained run log:

`py -3 -m scripts.qa.pan_artifacts --log <retained-run-log> --out captures/cascade-support`

Output is `captures/cascade-support/report.json`; flagged-frame strips are
`captures/cascade-support/burst-5800N-strip-M.png`. Each burst reports frame IDs,
calibrated stable pixels, flash events/pixel-frames/area by effect and contact sheets.
The new optional log annotation reports SDK state for every captured frame, missing
or duplicate rows, actual SDK present-end cadence and interval distribution.
`fg_state_every=1` is necessary. This is distinct from the existing G-buffer
recording cadence and from disk-dump completion. It is not GPU scanout timing.

Read these concrete supporting numbers for each serial:

- Raw transport complete and exactly 16 consecutive frames; all required RT views.
- SDK status 0, at least two presented frames per app frame, no API errors for
  every captured frame; reported present and record cadence at least 18 fps.
  A slowdown or missing FG evidence makes artifact reproduction inconclusive.
- Primary match distribution and active/missing RT counts from the existing log.
  Missing RT must never be described as an improvement.
- `[cascade] casters` buffer-full counts and max MB during settled 160/587 phases.
  The expected demonstrated fix is zero overflow at 587; report any remaining overflow.
- Final-luminance transient rate/area and sun/local/GI overlap, against independently
  evaluated static noise. A lower count on fewer/different calibrated surfaces is
  not enough to claim a quality improvement; retain analyzed-area denominators.

The existing positive-control classifier requires both transient excess and primary
mismatch. Vegetation caster flashes can occur without a primary mismatch. Therefore
`NOT_REPRODUCED`/`INCOMPLETE` remains a scoped observation, not rejection of the proven
zero-overflow fix or permission to tune thresholds. No new fix-PASS gate was added.
The analyzer's pose-offset output remains explicitly unproven when origins do not
uniquely identify rotation. Report evidence, not a full P1 or global 99.5% claim.

Stop on crash/device loss, unexpected user game/USER_PLAYING, authorization revocation
or existing queue stop rules. No raw cleanup, repeated batches or automatic scope
expansion. Supporting captures can be deferred if their readback changes the timing.
