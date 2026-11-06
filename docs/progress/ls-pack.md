## Production Ultra LS replay (branch ls-pack, READY_FOR_LIVE, 2026-11-05)

- Added `local_shadows`: production box exact-k request, recorded specialization
  constants and push bytes, no shader change; variants.txt request delta 0.
- `replay_pack=1` snapshots immediately around LS after its light-slot join.
  Captures shadow records/atlas, projector records/2D cookies, initial packed
  diffuse/specular, folded AO, sun mask, auxiliary state and output references.
- Replay restores mutable colour/auxiliary/counters before every dispatch,
  outside timing; existing `--pipeline-stats`, `--ab-spec` and raw comparisons apply.
- ABI 26 -> 27 is required for pre/post capture, extra descriptor sets and specs.
  Both host and core must be rebuilt together. No Git writes were performed.
- Pack budget: bounded lossless compression preserves required textures;
  compressed disk cap 2048 MiB. Actual inn size is pending capture.
- Prepared `tools/live_batches/ls_pack/jobs/ls-pack-inn.json`: inn save
  `The Blushing Mermaid - 96h 39m`, Ultra, one static LS frame, 45 s plan,
  visible_unfocused_fg. NOT authorized, staged or queued. Frozen/non-frozen lint pass.
- CPU tooling: live_flow (74), merge_scope (9), batch_jobs_lint (12) and
  queue_gates tests pass. Native pack/codec and ABI CTests pass.
- Synthetic LS CTest passes (12.74 s): guards 0, real cookie/atlas controls change
  output; paired outputs/counters 100% bit-equal and synthetic references 0 diff.
- `--pipeline-stats` paired replay: 168 registers, 7168 shared-memory bytes;
  2 control pairs 100% equal. No optimisation or production GPU cost claim.
- Synthetic referenced payload: 1,527,712 decoded -> 1,332,032 disk bytes,
  plus 1,412-byte manifest. Real inn size remains pending live capture.
- BelowNormal `scripts/build.ps1 -Test` PASS 195/195 (164.93 s CTest).
  Final log: `.scratch/ls-pack-final-build.log`. Both DLLs rebuilt for ABI 27.
- Fixture corrections: screen-sized froxel and an oblique projector make the
  cookie/atlas controls observable; invalid and zero-delta setups were rejected.
- No live result, real-pack size or production GPU cost claimed. Capture is diagnostic overhead;
  replay/live TLAS differences must be reported separately from exact offline A/B.
- Synthetic CTest checks packed initial restoration/repeat/A-B equality and format.
  Check in game only through the prepared job after explicit batch authorization.
