## Layer round 1: groom BLAS full build into compacted storage, mesh pool trim with moves, status ring UB (branch claude/r3-layer, offline verified, 2026-11-21)

- Pure fix, groom BLAS (`layer/bvhsplit.cpp` groom work, `hair_groom::groom_storage` in `common/hair_groom_budget.h`).
  After compaction the storage is the compacted size, but a full build (no refit: the previous build record dropped or
  unsubmitted around loads, `rt_dyn_refit=0`, or the compaction-copy frame itself) was written into it. That breaks
  the "destination at least accelerationStructureSize" rule: writes past the AS into the neighbouring chunk ranges.
  Now: undo the copy in its own frame, or re-create the full size (`g.compact = 0`, compacted again later). New
  counter at the end of the `[hair groom] frame N` line: "full builds into re-grown compacted storage N".
  Expected 0 except after a dropped build record. Unit test in `tests/hair_groom_asset_test.cpp`.
- `rt_mesh_pool_trim_move` (default 0, live, READY_FOR_LIVE). With native hair a single range at the pool's end blocked
  the trim. Now cached meshes (`kReady`) and hold snapshots past the cut move below it in the trim's in-command copy.
  Groom, hair bounds, OMM and pending ranges stay pinned. Meshes move only while `native_hit::pool_compact_freeze`
  holds (as for the repack); otherwise only holds move. The planner is `vram_compact::pool_trim_move_plan`.
  hx1 S8 001645 00:19:54 (1479 MB for 648 used, target 988, "no trim"): the planned cut is 988 MB (-491 MB) when no
  pinned range lies above 1109 MB (75% of 1479). With a pinned groom range at 1303 MB, still no trim (the quarter
  rule): the earlier estimate of -167 MB in that case was wrong. 8 layouts in `tests/vram_compact_test.cpp`.
  The key-off path, its log line included, is unchanged.
- Pure fix: the `status_slots` alias detector read and incremented `aliased` unsequenced (undefined behaviour).
- Not done: pixel-LOD camera BLAS compaction (coordinator: skip).
- Live to confirm (hx1 S8 hair job, `rt_mesh_pool_trim_move=1` from the start):
  - a `trimmed ... ranges moved below the cut` line where the key-off run logs "no trim";
  - `[mem] ours` mesh pool MB down by the trimmed amount;
  - 0 held-draw rejections (`why 2`) and no new hit-material or validation rejects;
  - energy within noise;
  - the re-grown counter is reported (any value is valid; above 0 only after dropped records).
- Cost: one copy of the kept bytes (as a growth) at most once per quiet window (254 frames). GPU ms not measured offline.
