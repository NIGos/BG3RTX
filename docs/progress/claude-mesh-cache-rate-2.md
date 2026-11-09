## Mesh cache headroom for the frame's other users; no build fails on hair bounds (branch claude/mesh-cache-rate-2, READY_FOR_LIVE, 2026-11-09)

- rv3 (183745, 191231): with `rt_mesh_budget_own=1` the cache filled (433 meshes, 3.6M triangles) and took the pool
  (2034 / 2047 MB) and the BLAS chunks (guard cap 4013 MB). The hair bounds pool request then failed `bvh_build`
  outright; freed ranges retire only behind a newer built scene, so no scene was built for the rest of the run
  (118 of 120 frames skipped, RT passes off).
- Fix 1 (bug fix, key-independent): a hair draw whose bounds do not fit is left out of that build (counted), never
  the whole build. Same output whenever the old path succeeded.
- Fix 2 (`rt_mesh_budget_own=1` only): the cache keeps out of the mandatory other users' peak demand + 25% in the pool
  (bounds, grooms, OMM indices) and in the BLAS chunk guard cap (per-draw, cascade, sun, groom BLAS); refused requests
  count as demand; a share pass evicts meshes unused for 8+ frames when the cache is over a share or declined a mesh;
  hold snapshots keep out of the reserve. Key off: identical.
- Tests: rt_capture_budget_test starvation model with the guard cap (old own rule: 300 of 300 builds fail, pool stuck;
  headroom: 0 fail, 99.9% visible cached, pool peak 1947 / 2047 MB), share/peak/hold-limit unit checks.
- Live: rel095_verify rv3 again (same plan); new gates in every window: 120 split builds per 120 frames, 0 skipped
  frames; ON windows: 0 hair draws left out. Report the cache size and captured triangles in the ON turns (smaller
  than the first rv3 fill, by the reserve) and the share/peak numbers of the mesh cache line.
