## CPU pre-draw optimization 4 (branch opt-cpu4, READY_FOR_LIVE, 2026-11-05)

- Qualified `cpu_opt3` now defaults to 1, retaining the A/B key. User-provided
  inn Ultra receipt: capture total -0.24 ms/frame vs OFF spread 0.054 ms/frame;
  `captures/queue/logs/opt-cpu3-inn.1.055551.log.cpu.md`. CPU1/CPU2 remain 1.
- Candidate `cpu_opt4=0` is layer only and NO_PIPELINE. ABI unchanged;
  shader request delta 0. No commit, game launch or queue staging.
- Exact descriptor receipts key set incarnation/write generation, full dynamic
  offsets and buffer/image/view/sampler epochs. Push, flagged and unsupported
  layouts fall back; lifetime-note order and uncached failure side effects remain.
  Fresh cascade and non-stream alpha bytes use cached mapped addresses;
  cascade usage/size facts
  invalidate on resource epochs. With existing `perf_hook_state=1`, persistent
  submit matrix slots refresh every submit, retaining each source's first sample
  with collision overflow.
- Fixtures exercise 320-frame descriptor sequences and 240-frame mapped/facts/
  matrix sequences, writes/copy/template, set reuse, offsets, failures, host writes,
  unmap/remap, rebind, recycled handles, invalid matrices and hash collisions.
  Actual OMM descriptor registry fixture also passes 144-frame invalidation sequences.
  Collect, frame-local publication and resource lifetime ownership are unchanged.
- Synthetic descriptor fixture: 42 loads and 5,078 cache hits. Matrix lookup
  benchmark: 0.1313 -> 0.1017 ms/frame versus the prior `perf_hook_state=1` map;
  applies only to that existing opt-in. These are not measured game savings.
- One inn Ultra OFF/ON/OFF/ON session is prepared outside the queue at
  `tools/live_batches/opt_r5/cpu4/`; 600 rendered frames/window, CPU1/CPU2/CPU3 on.
  It retains real `perf_hook_state=0`, `rt_alpha_stream=1`; matrix slot and
  non-stream alpha address savings are not predicted for this job.
  Every window requires complete CPU attribution receipts. Both ON windows must
  beat both OFF medians beyond observed OFF-repeat spread for pre-draw and
  driver-excluded hook bodies. No positive live saving or quality pass is claimed.
- Frozen lint passes; snapshot change appends three readiness queries while
  preserving every historical receipt. No raw corpus cache is read or rewritten.
- Defaults check and CPU3/CPU4 job fixtures pass (8 Python tests).
- HEAD and main are both `ac6d0428`; merge-from-main attempted before gates but
  shared Git metadata denied `ORIG_HEAD.lock` in the sandbox. No merge was needed.
- Final `scripts/build.ps1 -Test`: PASS 186/186; CTest 183.44 s, final
  incremental build 3 s, total 3m06s. Full build first started at 06:10:54,
  after the first 10 minutes. Compiler environment and missing OMM test stub
  were corrected before the full passing rerun. READY_FOR_LIVE is offline only;
  live CPU/GPU costs and quality/energy equivalence remain unmeasured.
