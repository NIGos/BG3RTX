## RTX Hair warm-up after a level load (branch claude/hair-warmup, READY_FOR_LIVE, 2026-11-09)
- Cause (he1/sh2 logs, code): the LoadGuard quiet window pauses every strand swap 300 frames after each level load.
  Grooms are joined (UV readback 1-3 frames), uploaded (2-3 frames) and keyed (+2) within ~15 frames of the first hair
  draw; then they wait ~250-290 frames. First strands came ~+300..+330 frames after the load (~10 s unfocused).
  Second cause: a new groom slot starts at LOD 0 (no bounds yet) and reaches its screen-size LOD only after a 30-frame
  vote (a second upload and full build 30-50 frames in; sh2 LOD 1/2 uploads 0.9 s after LOD 0).
- Fix, key `rt_hair_warmup` (live, off; knob exp_hair_warmup "RTX Hair fast start", developer): the quiet window ends
  after 30 settled builds in a row (no join pending, no LOD upload/vote, no full build, every groom keyed), 300 stays
  the upper bound; a slot's first screen-size measurement picks its LOD at once. All boundary resets and swap gates stay.
- Rejected: persistent join cache (the readback is the key; handles reused across loads), groom prefetch, all-LOD
  upload (lod_keep already hides LOD waits; 1.5-2x pool), see docs/RTX_HAIR_PRIMARY.md 5o.
- Lines (both key values): `[hair primary] warm-up: level load at frame B: load quiet ended ... first strand G-buffer
  swap at frame F, m frames after the load`, per-period first-swap and groom first-key waits.
- Expected: load to strands ~+300..+330 -> ~+55..+170 frames. Cost: CPU only; no shader/request/ABI change.
- Tests: hair_groom_asset_test (LoadGuard::settle, lod_vote). Live job tools/live_batches/hair_warmup hw1-dialogue
  (first load on, F8 reload off, F8 reload on; predicted.json), not queued.
- In game: knob on, load Test Dialogue1, watch the party's hair: strands within 1-2 s of the scene appearing.
