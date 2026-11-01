## One cached, monitored offline shader compiler (branch worktree-agent-a6e1ec4eea42cb3bc, offline, done, 2026-11-01)

- New `tools/shader_identity.py` (importable + CLI). It compiles requests exactly as `layer/shaders.cpp`
  does, in parallel at BelowNormal priority (`tools/low_priority.py`). It prints progress every 15 s
  (done/total, cached, rate, ETA, failures) and caches each entry's SPIR-V in the main checkout's
  `build/shader_cache`, which every worktree shares.
- Cache key: the DXC binaries and version, the argument template, the request, the entry, the profile,
  and the git blob ids of the source and everything it can include (transitively, conservatively). An
  include edit recompiles only the requests that reach it. A DXC change recompiles everything. A baseline
  commit is read from git blobs. Requests with equal input keys count as identical without compiling.
  Every cache hit re-hashes its blob. Failed compiles are never cached.
- Ported onto it: `tools/compile_shaders.py` (merge-green) and the 7 `tests/*_shader_test.py` gates
  (same CLI, assertions and result files; `--no-cache` forces a full compile). `tests/shader_baseline.py`
  removed. `shader_variant_test` never compiled (it checks `variants.txt` only), so it is unchanged.
- All gates now use the runtime flags (-O3, SHaRC). Before, scalp, hair-origin and bounce compiled
  without -O3.
- New CTest `shader_identity_test` (30 checks, no DXC): include edit -> exactly its dependents;
  DXC change -> all; a new file at a missing include candidate; a macro include depends on the whole tree;
  warm run compiles nothing; a corrupt blob is a miss; failures are not cached; identity states.
- Timings on this PC (8 jobs, BelowNormal, 1374 requests / 1392 entries). Cold `compile_shaders`:
  1607 s. Warm: 1.6 to 10 s. After a one-line edit to `rt/native_hit_shade.hlsli` (240 dependents),
  `rf_hit_shade_shader_test`: 723 s. It compiled only the 240 current-side requests; the baseline
  side came from the cache. `compile_shaders` after that: 1.6 s. Warm `rf_hit_shade_shader_test`
  against an unchanged baseline: 31 s (26 s of that is the first spirv-val of 96 modules, which is
  now cached). The old gates compiled the whole tree twice, about 2 x 1374 requests, every run.
