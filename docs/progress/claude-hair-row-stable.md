## Stable groom rows: rt_hair_row_stable (branch claude/hair-row-stable, READY_FOR_LIVE, 2026-11-10)

- Live hl1 164930 decided the 5u candidates: A (another build's TLAS traced) disproved (75 submits checked, 0), C (full
  BLAS builds) disproved (0), B confirmed: 11 of 11 keyed rows held another key in the first frame of the wheel zoom,
  then 7, 4, 4, 0, 7 ...; frame watchdog at G-buffer frames 2125, 2126, 2128, 2129, 2130, 39 dropouts.
- Cause of the order change: a groom's row is its position in the dynamic draw list, which layer/bvhsplit.cpp sorts by
  key only inside `if (hold_frames && !moving)`; with a moving camera it is the game's capture draw order, which
  alternates. Nothing kept a key's row.
- Not established: which consumer pairs a row with another publish. Every layer consumer read resolves instance, key
  map and row from the one header applied to its frame; the TLAS check is 0. Stable rows remove the exposure anyway.
- Fix: `hair_primary::GroomRows` (common/hair_primary.h), used for the groom TLAS instances, header rows and InstInfo
  rows. A groom keeps its row while live; a freed row is reused 8 build frames later (kSceneSlots); gaps are inactive
  instances with key 0. Host key `rt_hair_row_stable`, live, off by default (the consumer is not proven, so key off is
  not provably a bug); launcher: Advanced, "Strand hair: stable strand rows".
- The first-seen diagnostic counts only rows that held a key before and hold another now.
- Test: hair_groom_asset_test replays the arrival (row dropped, newcomers, alternating draw order): 0 moved rows, 0
  rows naming different grooms in consecutive publishes, freed row reused only after the lag.
- Cost: CPU under 1 us per build, no GPU pass, no shader change (variants delta 0, no ABI change).
- Staged, not authorized: hl1-s8-late-arrival sets the key; expect_every `holds another key than before 0` in the
  "lower" window. Predicted (predicted.json): 0 moved rows; 0 watchdog trips and 0 "rejected after the hit" dropouts
  after the zoom starts if the order was the whole cause; otherwise candidate D and a per-groom points check next.
- User check: S8, several characters in view, key On, zoom with the wheel: no holes at the start of the zoom.
