## CPU pre-draw optimization 3 (branch opt-cpu3, READY_FOR_LIVE, 2026-11-05)

- Qualified `cpu_opt2` now defaults to 1, retaining its A/B key. User-provided
  inn Ultra receipt: hook bodies -0.30 ms/frame, OFF spread 0.088 ms/frame;
  `captures/queue/logs/opt-cpu2-inn.1.052927.log.cpu.md`. `cpu_opt1` stays 1.
- Candidate `cpu_opt3=0` is layer only and NO_PIPELINE. ABI unchanged;
  shader request delta 0. No commit, game launch or queue staging.
- Detailed logging plus CPU timing adds pre-draw attribution for prepass
  observation, cascade membership, draw planning, alpha lookup, opaque/alpha material,
  BVH record publication, XFB commands, cascade draws and emissive draws.
  Nested scopes overlap; no new game timing result is claimed.
- Cache pipeline/table generation lookups, pipeline eligibility, model-position
  derivation from exact fresh bytes, complete VT alpha templates and BVH row
  address/stride preparation. Buffer lifecycle/binding/mapping epochs invalidate
  resource receipts, including recycled handles. Material bytes still read fresh;
  frame-local slots, triangle ranges, audits and lifetime uses still run each draw.
- Reduce hit-material mutex scope over mapped-input staging; shared publication
  and failure counters remain locked. Rejected mesh-arithmetic/finite-only caches
  avoided speculative overhead. No descriptor-result cache is used.
- Production-helper fixtures pass 240-frame row/model/material sequences with
  handle reuse, rebind, epochs, unmap/range failures and host writes; alpha checks
  preserve the original strict finite bounds. Mutex/staging fixtures pass 192 frames.
  Synthetic 5000-draw/1500-stream row preparation: 0.4202 -> 0.2408 ms/frame;
  registry reads 3,600,000 -> 1,060,860 over 120 frames. Not a game saving.
- One inn Ultra OFF/ON/OFF/ON session is prepared outside the queue at
  `tools/live_batches/opt_r4/cpu3/`: quiet readiness, 600 rendered frames/window,
  CPU1/CPU2 enabled. Every window requires complete CPU attribution receipts.
  Both ON windows must beat both OFF medians beyond their observed repeat spread
  for pre-draw and driver-excluded hook bodies. No fixed saving threshold.
- Frozen lint passes with three appended readiness queries and all old snapshot
  receipts preserved. Live CPU/GPU costs remain unmeasured.
- Final `scripts/build.ps1 -Test`: PASS 183/183, 184.67 s CTest, 3m59s total.
  Full build started after 10 minutes; checkout-local shader cache. Defaults and
  diff checks pass. All 31 changed/new Git candidates are below 20 MB.
- The pre-check merge-from-main attempt was blocked by sandbox permissions on
  shared Git metadata; HEAD and main both point to `2f209380`.
