## Tooling fixes from META_AUDIT_20261101 (branch worktree-agent-ad53b913f6f20e0c2, offline, 2026-11-01)

- Every-window gates: job field `expect_every` (scripts/queue_gates.py, run by test-queue.ps1). A stats
  line counts for a window only if the previous line of its kind was printed after the window's
  settings were applied. On the real logs: m4i 093913 now INVALID (diag-on 3.14% clip line), m5w3
  083416 INVALID (compose GPU 7.9 ms), m5w3 093518 passes. Used in m4i (clip) and m5w3 (composed, GPU).
- CTest `host_atomics_lint_test`: 18 shader atomic targets mapped to their allocations; 13 host-visible
  ones are the 2026-11-01 baseline (rt_stats, D-EM stats/header, split/hair/basis status, OMM epochs,
  scene/Sun stamps). New ones fail; fixed ones must leave the baseline.
- cb2 retired (job and plan deleted; it tested the removed rt_scene_wait=2 with the rebase gate).
- Coverage HANDLING dated 2026-11-01: water sun/refl "key" (M5.1-M5.4), D-EM "key", keys listed per family.
- Progress fragments: docs/progress/<branch>.md, folded by scripts/fold_progress.py (this file is one).
