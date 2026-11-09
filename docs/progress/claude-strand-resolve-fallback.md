## Strand resolve finds its frame's snapshot under a moving present (branch claude/strand-resolve-fallback, offline, READY_FOR_LIVE, 2026-11-09)

- Symptom: user session 2026-11-09 07:59 (0.9.5, Ultra, fg=3, fps_cap=58, rr_hair_strand_aa=2): 145-395 of 600 strand
  resolves per period fell back to the game's dither resolve while 599 of 600 snapshots ran, so analytic coverage and the
  dither alternated frame to frame. Queue lives (fixed camera, no cap) showed 0-3.
- Cause: the snapshot ring was keyed on frame_index() (counts presents). The present thread often advanced it between a
  frame's G-buffer end and its post-chain resolve, so the resolve looked for the next frame's snapshot.
- Fix: common/strand_snapshot.h Ring keys on recorded frames (a frame begins at its first snapshot after the last
  resolve, ends at its resolve); layer/rrpost.cpp record_strand_snapshot / strand_resolve use begin/wrote/take. A frame
  with no snapshot falls back once and never reads an older snapshot. Layer only (game restart), no shader or ABI change.
- Test: hair_groom_asset_test test_strand_snapshot_ring (600 of 600 frames, site merge and decal dedupe, refused
  snapshot, broken chain, one-in-three snapshots gives 400 consistent fallbacks). settings_test passes.
- Live check: any normal play with RTX Hair and exp_hair_strand_aa=coverage: the 600-frame line reads 'N frames, 0 fell
  back' with N equal to the 'strand snapshots' count of the same period (predicted 0 fell back).
