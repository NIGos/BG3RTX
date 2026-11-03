# S8 camera input probe (prepared offline)

No live authorization. One Water Queens House launch, `visible_unfocused_fg`,
110 s of phases plus loading (allow up to 4 min including the conservative 120 s load estimate).
Three mode groups: legacy 1 px / 4 renders, hold_drag at 1 and 2 px/render,
centre_hold at offsets 1 and 2 px. These are five 18 s measurement subwindows.
Prediction was written before the job. No winner has been measured.

## Exact input protocol

All coordinates are client coordinates; y = floor(H/2). The plan loop polls every
10 ms, so messages are **not** guaranteed to arrive on the next rendered frame.
The `fg_state_every=1` DLSS-G marker supplies the latest observed render number;
skipped markers never cause catch-up posts. It logs frames, not camera yaw.

- At plan start: unfocused mode posts WM_MOUSEMOVE (0x200, flags 0) at
  (round(W/2), round(H/2)); it never calls SetCursorPos. Foreground mode instead
  calls ClientToScreen + SetCursorPos once at that centre.
- Legacy reset: WM_MOUSEMOVE (0x200, 0). Wait >= 2/18 s, then
  WM_MBUTTONDOWN (0x207, MK_MBUTTON=0x10) at the reset position. No same-poll drag.
- wf8/time-based x = floor(W/2) + direction * (floor(t * pixels_per_second)
  - floor(travel/2)), where travel=max(8,floor(W/4)) and t is time within the
  current drag segment. The segment lasts travel/speed, then a 2/18 s reset interval.
- wf9/frame-based x = floor(W/2) - floor(W/8) + direction * offset.
  offset starts at 0, increases 1 px once after >= 4 newly observed render frames,
  and wraps to 0 at max(8,floor(W/4)). All intermediate polls/frames send nothing.
  For W=3840, reset x=1440, y=H/2; first drags x=1441,1442,... (direction +1).
- Changed x/y: exactly one WM_MOUSEMOVE (0x200, 0x10). No press/release per pixel.
  Unchanged coordinates: no message. Segment change: WM_MBUTTONUP (0x208, 0)
  at the last posted point, wait >= 2/18 s, reset at the new sample, wait again,
  press. Even a stall skipping the reset interval follows that sequence.
- Motion end, phase boundary or normal runner shutdown releases at last x/y once.
  Every phase boundary releases, including adjacent turn windows. No periodic
  centre reset or physical cursor movement occurs during an unfocused turn.
- hold_drag: reset/press at (floor(W/2),floor(H/2)), then x += direction *
  frame_delta_pixels once per observed render while pressed. No wrap/recentre.
- centre_hold: same reset/press, then one move to centre + direction * 1 or 2 px;
  no further move until release. Tests the game's possible held-offset integration.

Use a client width >= 2560 and the job's 30 fps render cap: 18*30*2 = 1080 px
fits within the half-width. Edge exhaustion aborts rather than wrapping. Speeds
are input pixels; the desired angular rates 15/30 deg/s are hypotheses to calibrate.

## Evidence and decision gates

There is **no existing per-frame yaw line** in camera/uniform logging. `rt epoch`
reports aggregate mean/max/signed yaw only and needs expensive primary diagnostics;
do not enable those here. No new render instrumentation or shader variants.
The job requires moving scene-build samples and a completed delayed 16-present
FG sequence in **every** turn subwindow; no raw burst, trace or image dumps.
Each sequence starts 6 s after input starts. All 80 presents together are bounded
at 503 MB (display <=3840x2160; half-size RGB plus 1% allowance), plus 50 MB logs.

Read the existing scene-build GPU `camera moving` and `static` sample counts for
each interior window. Report moving/(moving+static) and GPU ms; counts alone
cannot prove small or steady yaw. Use the minimap orientation in each 16-present
sequence to unwrap yaw and check jump/hold versus continuous movement. FG inserts
interpolated presents. Existing logs give render/present rates, but no reliable
per-image render ID; do not call presented yaw samples rendered yaw samples.
For each candidate/speed, require median abs yaw delta > 0, max <= 3*median,
and median consistent with desired rate / measured render fps (15/30 deg/s means
0.5/1 deg at 30 render fps). Report rate and all deltas; a full rotation, unresolved
render/present correspondence, absent moving samples or a zero median is INVALID,
never a pass. If the minimap cannot resolve that delta, yaw qualification remains
pending. The fallback can reject jumps and support a visual input choice; it cannot
certify a numeric rendered-yaw gate without render correspondence. The 2 px
candidate should turn about twice as fast as 1 px; measure it.
Select the slowest passing mode; no wf9 mode becomes active until then.

Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/camera_probe/dryrun.ps1`.
This uses the sibling fake-queue harness in TEMP, tests missing evidence rejection
and stripped structural success, and grants no permission to a real queue.
