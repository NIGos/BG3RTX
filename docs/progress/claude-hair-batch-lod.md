## Hair: every style converted, crowd budget, cost per character (branch claude/hair-batch-lod, 2026-11-03)
- Batch over the mesh_materials v2 catalogue (1217 LOD0/LOD1 CHAR_Hair objects, 4 jobs, BelowNormal, cached per
  style in captures/hair_groom_batch/state.json): running; coverage report to follow.
- Crowd budget `rt_hair_groom_crowd_ksegments` (off, live; GUI Performance): largest characters first within a
  per-frame segment budget, coarser LODs or cards beyond (`hair_groom::plan_crowd`, unit tests); LOD segment
  counts read from the file headers at index time.
- Cost model `tools.hair_groom.cost` (bench constants): 53 styles, LOD 0 median 504k segments, 53 MB per instance,
  0.06 ms refit; 14-character scenario 0.77 ms, 320 MB. CTest hair_groom_test (test_cost).
- Includes the device-loss / bald fix (e43731cc). Live job tools/live_batches/hair_crowd (2 launches, 12.2 min,
  1.8 GB): dryrun pass, not run.
