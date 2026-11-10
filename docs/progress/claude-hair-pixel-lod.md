## Pixel-footprint strand LOD: decode-time keeps built, camera BLAS designed (branch claude/hair-pixel-lod, offline, 2026-11-10)

- Step 1 built: decode_v3_lod(..., extra_keep) rebuilds keeps 1/8, 1/16, 1/32 from LOD 0 with the table's nested
  hash, radii x 1/keep under 9 mm, without changing the 888 files. hair_groom_v3_test: all three non-empty on the
  fixture, nested, radii exact to 1e-6. No layer use yet: behaviour unchanged, no key.
- Step 2 not built (larger than a prototype): it needs a camera-only BLAS per groom and a camera groom TLAS in
  bvhsplit. Cheapest correct route: reuse the groom's points (no second update), a static widened radius buffer and
  a kept-segment index buffer per LOD/keep pair, refit every frame; header with the camera TLAS and row.z = the camera
  index buffer, so the strand shaders stay byte-identical (variants 0). The hp_nearest candidate-filter route cannot
  widen strands (LSS radius lives in the BLAS). Estimated cost: ~18 MB mesh pool (radius buffers, S8), ~5-15% more
  BLAS memory, ~10-30% more groom refit ms, a cheaper strand trace. Cross-fade needs a shader change (open).
- Details and the pick rule (k = 1.0 px, no pixel LOD under 60 px, quarter-octave hysteresis + vote):
  docs/RTX_HAIR_PRIMARY.md 5v.
- (a) built and pushed (2a43c06b): keys rt_hair_pixel_lod (dev, 0, pinned in release.ini) and rt_hair_pixel_lod_k
  (1.0); pixel_lod_level (common/hair_groom_budget.h): finest lod_keep / 2^j reaching k px, 1/32 floor, level 0 under
  60 px, quarter-octave hysteresis; unit tests. Correction to the design: the camera subset needs no decode-time keep;
  it is the groom's current LOD strands whose nested hash passes keep_lod / 2^j (original strand index recovered by
  enumerating the hash over LOD 0's strand count = table points - segments), radii x 2^j.
- (b)-(e) not started in this session (agent context exhausted). Plan with pointers for the next agent:
  GroomSlot (layer/bvhsplit.cpp:524) gains the camera level, camera radius / index pool offsets (static per LOD and
  level, computed on the CPU from the slot's LOD arrays, uploaded through the groom_lod staging path, freed in
  groom_release and on a LOD switch with kGroomFreeLag) and a camera AsAlloc (LSS SUCCESSIVE on pos_off with the camera
  radius and index buffers; full build when the level or LOD changes, refit with the groom BLAS after the points
  update, bvhsplit.cpp ~3480); g_cam_tlas[kSceneSlots] / g_cam_inst beside g_groom_tlas (bvhsplit.cpp:995), built at
  ~3930 in the same instance order; hair_primary_publish gets the camera TLAS address and rows with row.z = the camera
  index offset; hair_primary_groom_tlas_built is called for the camera TLAS too (the 7f10d62b check). Cross-fade: a
  per-strand stable hash test between levels j and j+1 in hp_nearest's candidate loop behind a new kSpecId (1048);
  it needs both levels' segments in the camera BLAS: design it on the GPU fixture first.
- 2026-11-10, later: claude/hair-row-stable (a2646548, rt_hair_row_stable) merged into this branch. (b)-(e) still not
  started: the agent stopped at this part boundary (context). Two additions to the plan above: the camera TLAS must
  write its instances at the same rows as the groom TLAS (`row_of[]` in the groom instance loop of
  layer/bvhsplit.cpp, including the inactive gap instances with rt_hair_row_stable); and with row.z = the camera
  index buffer the pixel record's segment is a camera BLAS primitive, so before (b) check every reader of that
  segment outside the header rows (InstInfo word 3 `w.l->seg` in rt_groom_links, core fibre shading): a reader that
  resolves it through the groom's own segment table would name another strand.
- (b) built: camera arrays (hair_groom::camera_subset), camera BLAS per slot, camera groom TLAS per scene slot, header
  points the strand shaders at it (row.z = camera index buffer); details 5v "Step 2 (b) built". Segment readers checked:
  all resolve through the header row. Key off: no allocation. Shader identity 227/227 vs e5f62b50, variants 0.
  hair_groom_v3_test: 14 LOD x level pairs equal to the decode-time keeps. Not measured: pool MB, BLAS MB, refit ms (live).
- (d) built (3b501d8e): the pixel LOD period line (levels, diameter and head px, px per radian, camera builds, arrays and
  BLAS MB). No offline BLAS fixture: MB and refit ms come from the live run.
- (e) per the coordinator: no separate job; hl1-s8-late-arrival (hair_late) gains part 2 after its arrival part: pl-off,
  pl-on, pl-off2 frame windows (60 frames after tightening), PNG bursts 99310/99311, the period line and the strand
  G-buffer GPU ms per window; warm-up runs the key on once (warm-up lint), off again before "lower". Predictions in
  predicted.json part2_rt_hair_pixel_lod. live_flow --frozen 0 failing (readiness entries and never-seen patterns frozen
  in lint_corpus_snapshot.json). Not queued.
- (c) not started (cross-fade, spec constant 1048): next agent; design it on the GPU hair fixture first. Without it a level
  change pops (about 2x a LOD step, study).
