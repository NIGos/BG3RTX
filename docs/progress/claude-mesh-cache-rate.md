## Mesh cache budget charged with its own BLAS (branch claude/mesh-cache-rate, READY_FOR_LIVE, 2026-11-09)

- Cause (rv2 14:57-14:59, S8 zoomed out and turning, main 0b45b0c8): `rt_mesh_budget_mb` (2048) charged the whole
  pool plus EVERY BLAS chunk byte (`as_bytes()`: per-draw, cascade, sun, groom BLAS and pending ranges). In the turns
  pool + all BLAS was 2.5-3.8 GB (e.g. 14:58:21: pool 844 + AS 1677 = 2521 MB). Then every new mesh was declined
  (`mesh_cache_admit`, counted as "deferred by the per-build budget": 30k-49k per line = every non-pattern captured
  draw, about 250 per build) and the LRU evicted every mesh not drawn in the last 8 frames, each frame, because its
  90% target is unreachable while non-cache bytes alone exceed it: 28-60 meshes left, 300-700 LRU per line.
  The declined draws stay captured with uncompacted per-draw BLAS, which keeps the total over: a loop.
- Not the cause: the per-build budget (`rt_mesh_new_tris` 4M, 1024 meshes), verification throughput (241-646 verified
  per line, 0 mismatches), the pool cap (pool 2047 MB, 528-1463 used). The "pattern" rule (VS not class A: skinned,
  wind, UBO-offset variants) is by design: 359 of 610 captured draws per build. In steady windows captured draws equal
  pattern draws (381 vs 385), so about half of the 4.0M captured triangles (2.0M; 4.0-6.2M in the turns) is static,
  instanceable and was only kept out by the budget accounting.
- Second defect: admission refuses AT the budget, the LRU ran only ABOVE it: a cache filled to the budget kept stale
  meshes (rv2 15:00:39-51: 2044-2047 of 2048 MB, about 15 draws declined per build).
- Fix, `rt_mesh_budget_own` (live, default 0, developer key): 1 charges the whole pool (shared, hard-capped, pending
  included: the 2026-11-03 load-admission rule unchanged) plus only the cached meshes' own BLAS; a decline at the
  budget triggers the LRU pass (still only meshes unused for 8+ frames, oldest first, down to 90%). Other BLAS stay
  under the VRAM guard's chunk cap and its mesh LRU ask. Key off: identical code paths and values (the LRU plan helper
  replays the old loop exactly). Host only: no shader, ABI or variant change; NO_PIPELINE.
- `[bvh] mesh cache` line appends `budget (rt_mesh_budget_own=K): charged X / 2048 MB (pool, cached-mesh BLAS, other
  BLAS), N draws declined`.
- Tests: rt_capture_budget_test (charged bytes, rv2 admission and LRU cases, LRU plan = old loop over 1300 cases, a
  turning-view model: visible meshes cached off 0.00 / on 1.00, 0 re-admissions on a ring that fits, charged <= budget).
- Expected (predicted.json): turns with the key on keep 300-600 meshes, captured triangles 4.0-6.2M -> 1.7-3.6M
  (the pattern draws), BLAS GPU median in motion 2.4-4.3 -> 1.0-2.0 ms (p99 7-13 -> 3-6), scene wait 5.6-7.5 -> 3-5 ms,
  no BLAS chunk refusals by the VRAM guard (rv2: 42-146 per window, cascade casters' BLAS left out).
- Live (prepared, not run): rel095_verify rv3-s8-mesh-cache-budget, the rv1 view and turns with release defaults,
  key 0 / 1 (two turn pairs) / 0 in one run; every window: capture, cascade, stride split, scene wait, `rejected N/0/0`.
  In game: S8 zoomed out, turn the camera; RT shadows/GI of buildings and props as before, smoother turns.
