## Late groom arrival on demand: rt_hair_debug_late_arrival and the hl1 job (branch claude/hair-late-arrival, offline, staged, 2026-11-10)

- `rt_hair_debug_late_arrival` (developer only, index count, 0 off, pinned 0 in release.ini): after every live strands
  switch-on the groom of the hair draws with that index count is held for 301 frames (kLoadQuietFrames + 1, one past
  the longest warm-up), not counted by the warm-up, then released. Logs the hold and the release. CPU only.
- Staged job tools/live_batches_staged/hair_late/jobs/hl1-s8-late-arrival.json (S8, one launch, ~61 s of phases,
  45252 held): arrival-a with rt_hair_groom_nearest live off must show holes (frame watchdog trips or dropouts),
  arrival-b with it on none (0 trips, dropouts <= 2 in every period). predicted.json precedes it; live_flow 0 failing.
- Gates: hair_groom_asset_test test_late_arrival; build.ps1 -Test. Details: docs/RTX_HAIR_PRIMARY.md 5u.
