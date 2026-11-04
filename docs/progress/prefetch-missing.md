## Stale shader prefetch requests (branch prefetch-missing, kept, 2026-11-04)
- Prefetch drops and rewrites removed sources/entry points once; real failures and renderer demand retain FAILED reporting. ABI and shader variant requests unchanged (delta 0).
- Layer build and shader_prefetch_test/shader_cache_test pass (2/2); covers persistence, duplicate logging and renderer-demand veto. No game launch.
