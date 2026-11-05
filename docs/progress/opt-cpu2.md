## CPU hook optimization 2 (branch opt-cpu2, READY_FOR_LIVE, 2026-11-05)

- Default-off `cpu_opt2`, layer-only, registered as NO_PIPELINE. ABI unchanged;
  shader request delta 0. No commit, game launch or queue staging.
- Requested 005111 Ultra static/pan: pre-draw 11.467/12.820, pass-after
  4.400/7.827, nested G-buffer material 2.777/3.838 ms/frame. Latest valid
  CPU1 OFF repeats: 8.696/8.624, 2.550/2.636, 2.064/2.032 respectively.
  Watch totals use rendered presents; nested timers must not be summed.
- Replace repeated alpha slot scans with capture-local indices; stage mesh
  eviction ranges and snapshot lifetime handles on the stack. Preserve first
  layout, descriptor order, extraction ranges and duplicate lifetime calls.
- Reuse G-buffer qualification within a call when runtime policy is unchanged;
  index material source records once per query and stage table additions without
  heap allocation. Reuse each TLAS slot's identical size receipt (one query/build).
- Detailed QPC scopes split pipeline, cascade, BVH classification, alpha records,
  material and driver-excluded dispatch tracking. Existing logs lacked these splits.
- Exact fixtures cover slots, records, full tables, toggles, layouts, extraction,
  qualification, rollback and lifetime order. Synthetic timings are not game savings.
- Prepared one inn Ultra OFF/ON/OFF/ON job, 600 frames/window, outside the queue.
  Ready phases require only a fresh quiet witness; every window requires CPU
  receipts. Frozen lint passes; 303 old readiness entries preserved, 3 appended.
- Synthetic alpha slots: 0.7530 -> 0.0196 ms/frame (5000 draws/1500 views);
  mesh ranges: 0.3860 -> 0.0508 (5000 draws/5 extracts); material records:
  1.6976 -> 0.0066 ms/query at 4096 rows, 0.0001 -> 0.0001 at 32 rows.
- Final `scripts/build.ps1 -Test`: PASS 180/180, 148.25 s CTest, 153 s total.
  Build began after the requested 15-minute wait; checkout-local shader cache.
  Defaults and diff checks pass. No changed/new Git candidate exceeds 20 MB.
- Full breakdown, measurement limits, cost gates and in-game check:
  `tools/live_batches/opt_r3/cpu2/README.md`. No live CPU/GPU saving is claimed.
