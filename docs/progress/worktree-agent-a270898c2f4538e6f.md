## Persistent runtime shader cache, background prefetch, A/B toggle cost (branch worktree-agent-a270898c2f4538e6f, READY_FOR_LIVE, 2026-11-01)

- Cause of the startup greybox: every launch compiled each runtime request with DXC on the render
  thread (44 in the 20:35 log). Offline, the same 35 files (50 entries) take 47 s of serial DXC; the
  biggest are 6 s (gi_trace_rt_hit_sun_em) and 10 s (rt_reflections_hit_shade_em).
- Cause of the slow Ctrl+Shift+F9: the toggle only overrides cfg keys; pipelines are kept per variant.
  But after ON the emissive scene is a few frames stale, so the non-emission fallback variants
  (gi_trace_rt_hit_sun, rt_reflections_hit_shade) were requested for the first time and compiled
  synchronously: +6 s and +10 s after the 20:36:32 toggle.
- Fix (layer only, no ABI change, no new ini key): SPIR-V cache in `%LOCALAPPDATA%\BG3RTX\shader_cache`
  (`BG3RTX_SHADER_CACHE` -> `<dir>\runtime`, `none` = off), keyed exactly as tools/shader_identity.py
  (C++ twin in layer/shader_cache.cpp; on the real tree all 44 keys equal Python's). Checksummed entries,
  corrupt = miss + recompile, LRU prune above 512 MB. Hot reload rebuilds only entries whose key changed
  (common/ is now watched too). A background thread prefetches the last 5 sessions' requests
  (requests.txt) at the first request, so the fallback twins are ready before any toggle.
- Expected: warm launch = tree 25-55 ms + keys 5-27 ms + 44 reads ~50 ms (measured offline) instead
  of ~45 s of DXC; toggle stall from 16 s to a pipeline creation.
- Logs: `[shaders] cache: N hits, M compiled, F failed, K ms building (render thread waited W ms)`,
  `[shaders] cache: prefetched ...`, per compile `compiled in X ms`, `[ab] toggle cost: longest gap
  between presents X ms`.
- Tests: shader_cache_test (golden keys, disk tree = memory tree, hit/miss/corrupt/prune in %TEMP%),
  shader_cache_key_test.py (Python side of the golden file).
- Live check (one launch twice, any save): second launch shows `N hits, 0 compiled` and a shorter
  greybox; press Ctrl+Shift+F9 twice and read the `[ab] toggle cost` line.
