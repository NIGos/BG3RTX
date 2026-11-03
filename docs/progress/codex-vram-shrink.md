## VRAM shrink starvation (branch codex-vram-shrink, READY_FOR_LIVE, 2026-11-02)

- No commit: Git metadata read-only. No game or shader tools run. ABI unchanged;
  shaders/variants.txt request delta 0. Existing keys and defaults unchanged.
- Evidence: vg2-s8-low-long-view-fg2.1.212432.log is INVALID. At lines 12573,
  12600-12602, demand targets existed: RT 544 MB (later 592), cascade 240 MB
  per slot. Line 12957 still has RT 1024, cascade 1632, pool 2047 / 296 MB.
- Capture cause: singleton RT refreshes record_frame each frame, cascade every
  three, while elastic_retired requires GPU completion plus two/four frames.
  Continuous capture starves replacement even after 24 low samples.
- Replaces the strict retirement design in codex-capture-reserves.md: detach at
  the next producer boundary after submission/drop is known. Keep one old buffer
  per producer until the fence covers detachment + 4 frames, then defer destruction.
  New captures cannot reset that clock. Pending/unknown producers still defer.
  Allocation failure preserves the old capture; a second replacement waits for
  the first old allocation. Key off still drains retained allocations safely.
  Memory totals include retained buffers. Current-sun private captures unchanged.
- Mesh cause: 296 / 2047 MB already passes the quarter-full/254-frame policy.
  The code vetoes all dynamic hold ranges, which a static view keeps live, and
  later applies a growth-budget refusal to the replacement needed for shrinking.
  The old log does not identify which earlier safety gate blocked each attempt.
- Extends codex-mesh-pool.md: include hold snapshots in copy accounting and rebase
  their offsets on confirmed publication; pause hold writes during the copy.
  Removed/replaced snapshots are not resurrected. Existing scenes retain old pool
  handles and offsets. Native hair, pending work/ranges and uncertain recordings
  still block. A target at most half the old reserve may allocate despite LOW;
  allocation failure rolls back. Threshold and low-demand window stay unchanged.
- Deferred reason lines: once per 254-frame pool window, once per 24 RT / 72
  cascade-frame window per producer. Includes outstanding work, retirement,
  unknown recordings and allocation failure.
- Verified: C++ layer build and 5/5 targeted CTests: capture budget, elastic,
  compact, guard, settings. Elastic 430 CPU checks; compact 85, including mixed
  mesh/hold allocation accounting, removal during copy and old-pool preservation.
- Predicted at unchanged vg2 demand after retirement: pool about 592 MB,
  RT 544-592 MB, cascade 3 x 240 MB; about 2.7 GiB less active reserve combined.
  Temporary allocation overlap can fail under pressure; no measured savings claim.
- Live confirmation still needed: same S8 low-budget continuous view, then real
  Ultra turns/key toggles. Expect capacity/repack lines or named deferred reasons,
  falling memory after retirement, unchanged coverage and no Vulkan errors.
  GPU copy cost, energy and visual invariance remain unmeasured. Keys stay off.
