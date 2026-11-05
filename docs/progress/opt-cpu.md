## CPU scene scratch candidate (branch opt-cpu, READY_FOR_LIVE, 2026-11-05)

- Watch CPU submit is ms per reporting interval, not per frame: 58-185 / 120 =
  0.48-1.54 ms/frame, including driver submit/queue wait. Inn Ultra static/pan
  hook medians are 12.623/15.395 ms/frame, nested scene work included. See
  `tools/live_batches/opt_r3/cpu/README.md` for units, scoped costs and limitations.
- Added default-off `cpu_opt1`: retain four scene vectors and two membership
  tables; cache the per-draw pool-trim setting with its correct default. No GPU
  work, draw order, duplicate winner, lifetime contract, shader or ABI changes.
- Exact membership fixtures pass. Synthetic 5000-key lifecycle benchmark:
  0.3183 -> 0.0517 ms/build; no measured game saving claimed.
- One inn Ultra OFF/ON/OFF/ON job prepared outside the queue, quiet readiness
  only, 600 frames/window. Lint passes; READY_FOR_LIVE, readiness unproven.
- `scripts/build.ps1 -Test`: PASS 180/180, 273.19 s CTest (277 s total), using
  a writable checkout-local shader cache. Earlier shared-cache attempt discarded.
  Final analyzer fixtures, settings-default check and diff check also pass.
- No game, queue staging or commit. No changed/new Git candidate exceeds 20 MB.
