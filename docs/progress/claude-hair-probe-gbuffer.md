## Strand passes trace the previous publish rebased to this camera (branch claude/hair-probe-gbuffer, READY_FOR_LIVE, 2026-11-04)

- Cause (hv6/hv5 04:09-04:41): the scene build, so the groom TLAS, is recorded after the G-buffer pass. The prepass
  and the G-buffer only ever see the previous frame's publish ('older publish 300' per 300 frames, every order-mode
  G-buffer pass refused). Before the same-frame rule the strands drew from that camera-relative TLAS without
  correction: a one-frame camera offset (the misaligned, flickering strands).
- Fix: each publish carries its build camera (rt_build_camera, header kCameraOffset, flag bit 1); the G-buffer,
  velocity and probe shaders start their rays at camera_now - camera_build (shaders/rt/hair_primary_rebase.hlsli);
  no camera: no strand (fail closed). The probe and the G-buffer accept the publish of this or the previous frame.
  Groom frees keep the 3-frame lag (2 frames of header age covered).
- Velocity: camera motion only (the strand's own animation of the last frame is not in the header).
- Live check: hv6 (predicted.json rebase_20261104). Layer and strand shader change: game restart.
