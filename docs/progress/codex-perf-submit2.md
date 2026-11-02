## CPU descriptor and submit batching (branch codex-perf-submit2, READY_FOR_LIVE, 2026-11-02)
- Implements ranks 6 and 7 of `codex-perf-cpu:docs/PERF_AUDIT_CPU.md` behind one key:
  `perf_submit_batch=0`; live developer GUI choice, off in all presets. ABI unchanged.
- Game descriptor driver calls and write/copy order remain unchanged. Tracker decodes
  writes outside its mutex and merges them in order under one write lock; OMM callback
  and sequential copy boundary remain intact. Reuses decode storage per thread.
- Hit clones: copy only the complement of appended texture slots, so Vulkan's
  writes-before-copies yields the old copy-then-overlay contents: 2 calls -> 1.
- Sun clones retain copy first; sampler, buffer and image writes become one later
  call: 2..5 calls -> 2. Owned image arrays survive until driver return. Slots stay owned.
- Submit wrappers reuse leased thread scratch; nested calls use independent storage.
  Empty command lists skip BVH, pan and Streamline consumers. Lifetime sequence,
  terrain/material/OMM bookkeeping and failure handling still run.
- Eligible graphics-queue OMM ticket batches append a separate private timeline signal
  entry to the same driver call: 1 auxiliary driver call -> 0. Game entries, waits,
  signals and fences are preserved; the app fence also covers the private signal.
  Serial lock spans driver acceptance; failed attached signal retains resources until idle.
  Extension/protected/foreign/stale cases keep the original path. Emissive/water fences stay separate.
- Existing `[watch]` lines now include interval submit-wrapper CPU, descriptor-tracker
  CPU and alpha-clone descriptor-driver CPU (ms), clone call count and coalesced signals.
  Timings need diagnostics + `cpu_timing=1`; normalize by presents, exclude mixed intervals.
- Offline: explicit C++ host/test targets built successfully; 5/5 selected CTests pass.
  `submit_batch_test`: 2,365 checks (copy interval equivalence, owned descriptors,
  nested scratch, append eligibility and source submit preservation). Settings: 1,001 checks.
  Settings defaults/schema regenerated and validated. No shader tools run; request delta 0.
- A/B plan and predictions: [predicted.json](codex-perf-submit2/predicted.json).
  One launch per save (S8, camp); OFF/ON/OFF/ON static/pan/rotation with real Ultra,
  at least 600 frames and 90 s per window; no builds/compiles in cost windows.
  Require patterns/expect and every ON window's ownership, coverage, wait and capacity gates.
  Separate reference/energy captures from cost; noise comes from matching OFF repeats.
- Predicted energy/coverage delta 0. CPU saving is unmeasured (zero is credible);
  GPU cost is unmeasured. OMM coalescing needs actual eligible tickets to qualify.
- Kept as off-by-default candidate only. No game launched, no queue job created,
  no Git metadata modified and no commit made. Live validation remains pending.
