## RTX Hair strand/card flicker while turning: header camera from the GPU (branch claude/hair-header-cam, READY_FOR_LIVE, 2026-11-09)
- Cause (item 11 of the 5i review, reopened): the header camera was the CPU's record-time read of the game's camera UBO;
  the groom points come from the GPU's read at execute. rv3-s8 220145 turns: sampled origin 0 -> 94-98.5% strand
  share (8/8 frames), origin 2.6-15.5 cm -> 18-80% (7/7); 0-23 frame-watchdog trips per 300 frames, each holding
  every hair draw on cards for 28 frames (up to 2045 watchdog refusals per line).
- Proof counter (always on, CPU only): header safety line "build camera re-read at submit (item 11): N publishes
  checked, M differ (max X m), K header cameras copied on the GPU"; sampled-ray line adds the header camera and the
  evidence strand share.
- `rt_hair_header_cam_gpu` (live, off, Advanced knob "Strand hair: GPU build camera"): the build copies the UBO camera
  into the header on the GPU (geo_copy_camera + two 12-byte copies; previous camera from a device slot). No shader
  change, variants delta 0. Cost: one tiny dispatch per publish (not measured live).
- `rt_hair_watchdog_draw` (live, off, Advanced knob "Strand hair: per-draw watchdog"): per draw key, 2 bad judged
  frames in a row (one header per G-buffer frame), hold kWatchdogFrames (28) for that key only. Unit test in
  hair_groom_asset_test.
- "with a strand" counter: correct but misnamed (shared trace of swapped slots only; probes trace privately). Relabelled.
- Gates: build -Test, settings test, no shader request changed. Live job staged (not queued):
  tools/live_batches_staged/hair_cam/jobs/hc1-s8-header-cam.json, predicted.json; live_flow lint passes.
- User check: S8 or Test Dialogue1, turn the camera with both keys on: hair stays strands, no flash to cards.
