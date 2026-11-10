## Strand holes under a fast camera step: camera mismatch candidates narrowed, deciding diagnostic (branch claude/hair-cam-step, READY_FOR_LIVE diagnostic, 2026-11-10)

- hl1 200821 (VALID) confirmed the zoom trigger: trips at G-buffer frames 2064, 2065, 2067-2070 with build camera
  steps 0.34-1.81 m (1.81 m: a 130 ms frame during a groom decode; dolly about 15 m/s), none in static windows.
- Read (docs/RTX_HAIR_PRIMARY.md 5z): groom points, groom TLAS (identity instances), header camera and the soup's VP
  come from one capture UBO; the G-buffer applies this frame's publish in 58-60 of 61 frames (org 0); card band,
  ray and hit depth all use the G-buffer's own gCam.vp. TLAS check, header/BLAS mismatch, build-after-G-buffer: 0.
  The moving-camera mode (bvhsplit.cpp 2626/2813/3459) changes holds, snapshots and row order only (not frame-wide).
- Not established offline: whether the rays' start (the VP origin, hp_rebase) is the eye while the camera moves, or
  whether the trip frames applied an older publish. No fix, no key (no guessed fix).
- Added (CPU only, no shader, no ABI, variants delta 0): `hair_primary::vp_eye` and the applied publish per G-buffer
  frame; the frame watchdog line now names the applied publish (k frames before), the header camera offset and the VP
  eye offset at that publish and at the frame. CTest hair_groom_asset_test test_vp_eye.
- hl1 regenerated (expect_every: every trip line carries the fields; predicted.json camera_mismatch_5z); lint 0
  failing (live and frozen).
- 200821 was VALID: the `phase=1` INVALIDs were 180301/180949 (job from before the 5x fix); main's job has phase=2.
- Next live decides: eye about one step at trips -> shader ray from the near-plane point; k > 0 -> stale header pick;
  neither -> per-reason reject counters in the strand G-buffer.
