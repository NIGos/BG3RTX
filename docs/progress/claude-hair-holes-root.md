## Strand holes under the wheel zoom: header camera read on the CPU, points converted on the GPU (branch claude/hair-holes-root, READY_FOR_LIVE, 2026-11-10)

- Cause (existing logs, no new live): hp_rebase starts the rays at gCam.position minus the header camera. The header
  camera is a CPU read of the mapped camera UBO at the build's recording; the groom points are converted on the GPU with
  the block the G-buffer's draws read. Under motion the CPU read can be one camera step behind: rays start one step from
  the eye against points that are already this frame's. A dolly puts the whole error along the view: hits are one step
  off the card, hp_on_card rejects them, 1-3% survive, the frame watchdog trips.
- Evidence: hc1 234025 (turns): header camera copied on the GPU -> shader-sampled origin exactly 0 in 18 of 18 samples,
  strand share 0.93-0.98 in 15; CPU camera -> origin 1-12 cm in 13 samples, share 0.13-0.94. hf1 073932 zoom frame 2455:
  origin 0.496 m, share 0.029. hf2 233301: strand px rise (64 -> 231) as the step shrinks (0.524 -> 0.342 m).
- Not established: why the CPU read lags, and not in every moving frame. Not yet shown in the zoom with the GPU camera.
- Fix: the existing key `rt_hair_header_cam_gpu` (off): the build copies the camera on the GPU, so the header camera is
  the origin of the points by construction. hc1 tested it on turns only, confounded by the shared-key dropouts. The key
  stays off until the live passes. Cost: one 16-thread dispatch, two 12-byte copies, three barriers per publish.
- Added (CPU only, no shader, no ABI, no key, variants delta 0): `hair_primary::rebase_sample` / `RebaseStats`; the frame
  watchdog line ends with the GPU-sampled rebase origin, card and hit distance of that frame; period line
  `[hair primary] rebase origin (last N frames)`. CTest hair_groom_asset_test test_rebase_sample.
- hl1 regenerated: rt_hair_header_cam_gpu=1, rt_hair_stale_publish=0; expect_every in lower and watch: watchdog trips 0
  with header cameras copied on the GPU, 0 sampled frames with a non-zero origin. predicted.json first (root_cause_6c,
  fix_6c_header_cam_gpu with the signature of each other outcome). Lint 0 failing (live and frozen).
- Next live shows: origin 0 and 0 trips -> cause confirmed, add the key to the On bundle. Origin 0 and trips remain ->
  each trip line carries card and hit distance (miss / near the card / far from it) and names the next candidate.
- Wrong or misleading, corrected in docs/RTX_HAIR_PRIMARY.md 6c: item 11 "DISPROVED" (its check re-reads the same mapped
  memory); rt_hair_stale_publish compares two CPU reads (hf2 233301 "same camera" in the trip window; it cannot act); the
  watchdog line's camera steps are CPU cameras paired by frame label; "rejected after the hit" is one frame-wide count;
  "header/BLAS mismatch" is always 0 in the bundle; gaps between trip frames are uncounted frames.
- Left: GbFrame::pub_frame is not reset per frame (stale by 16 frames when no upgrade is taken); `python -m
  tools.live_batches.lint_snapshot` on this PC shrinks the checked-in snapshot from 79 to 26 logs (needs a merge mode);
  the same CPU camera feeds the cascade caster rebase (rt_cascade_origin=0), the groom LOD bounds and rt_camera_moving.
- User check (once qualified): S8, wheel zoom in fast: the hair keeps its strands, no pop to cards.
