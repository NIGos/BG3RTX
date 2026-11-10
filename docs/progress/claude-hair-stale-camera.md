## rt_hair_stale_publish carded every frame: unknown camera now traces, gCam looked up at any hair draw (branch claude/hair-stale-camera, READY_FOR_LIVE, 2026-11-10)

- hf2 225504 (key on): "camera not known 60, drawn as cards 60" in every period, every G-buffer applied build frame N - 1
  by label, still camera included: strands were cards in every frame (its 0 trips mean nothing).
- k = 1 is the normal label pairing (G-buffer label frame_index + 1 at recording, build label frame_index at the build:
  the build labelled N is never noted at G-buffer N's submit). 6a's claim that k = 1 caused the 210357 trips is withdrawn.
- Not known because the gCam UBO was looked up only after a swapped draw, matched by command buffer: the first frame
  drew no strand, then carded frames gave no coverage evidence and nothing swapped again (lock-in).
- Fix: lookup at any main-view G-buffer hair draw, matched by G-buffer frame, at most 4 tries; the label fallback no
  longer decides; only a measured move over 1 cm cards a frame, an unknown camera traces as before. Period line names
  why a camera was not measured; the first 4 checks are logged whatever the verdict.
- rt_hair_mv_card, rt_hair_pixel_lod, rt_hair_row_stable: no effect on submit order (velocity specialization / build
  content only).
- CTest test_stale_publish updated (unknown policy, label indexing, reasons). Key stays off until a live shows the
  G-buffer camera measured and equal to the publish camera on a still camera.
