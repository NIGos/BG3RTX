## Hair: every style converted, crowd budget, cost per character (branch claude/hair-batch-lod, 2026-11-03)
- Batch over the mesh_materials v2 catalogue (1217 LOD0/LOD1 CHAR_Hair objects, 4 jobs, BelowNormal, cached per
  style in captures/hair_groom_batch/state.json), 1 h 52 min: 519 pass (written, 13.3 GB), 553 needs review (not
  written: mirrored UVs, coverage, radiance), 145 reject (silhouette IoU, root channel, 22 zero radii in a thinned
  LOD). Pass min-view IoU median 0.94. Details in docs/RTX_HAIR_PRIMARY.md 5a. The 22 radius rejects left 296 B
  header-only stubs in data/assetdb/hair_grooms (asset.write fixed and tested; the stubs await a decision).
- Crowd budget `rt_hair_groom_crowd_ksegments` (off, live; GUI Performance): largest characters first within a
  per-frame segment budget, coarser LODs or cards beyond (`hair_groom::plan_crowd`, unit tests); LOD segment
  counts read from the file headers at index time.
- Cost model `tools.hair_groom.cost` (bench constants, header-only reads: 0.6 s for 549 files): 527 styles, LOD 0
  median 541k segments, 57 MB per instance, 0.06 ms refit; 14-character scenario 0.80 ms, 343 MB.
- Includes main up to bundle7. Live job tools/live_batches/hair_crowd (2 launches, visible_unfocused_fg, 12.6 min,
  1.8 GB): live_flow 0 failing, dryrun pass, not run.
