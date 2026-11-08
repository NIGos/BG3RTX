## VRAM: capture reserve from the observed rise, BLAS chunk reuse (branch claude/opt-vram, READY_FOR_LIVE, 2026-11-08)

- Evidence (user log 2026-11-08, 4 min, RTX 5090): cascade capture 3536 MB (3 x 1232 MiB) for a
  370 MiB demand peak; RT capture 2047 MiB for about 840 MiB; 1531 BLAS chunk allocations.
- `rt_capture_rise_reserve` (new, default 0): the elastic movement reserve is the largest demand
  rise over the replacement delay, not the largest single step x 3 (cascade) / x 5 (RT); the RT rise
  is learned without the buffer's own 40% alpha region (its growth fed back as steps: 1024 -> 2047).
  Expected: cascade about -1.7 GB, RT about -0.4 to -1.0 GB. Off until a live shows buffer full 0.
- `rt_as_chunk_reuse_frames=120` (new, default on): empty BLAS chunks stay 120 frames and are reused
  by any arena instead of free + reallocate. Placement only, no rendering change. Churn model
  599 -> 6 chunk creations, same peak. VRAM guard asks still free them at once.
- Tests: rt_capture_elastic_test (ramp unchanged, cut 832 -> 480 MiB, 2000-sample replay covered,
  RT self-feedback gone), as_chunk_alloc_test (cross-arena reuse, window, yield, churn).
- Not changed (candidates): water compose records 560 MiB for about 292 needed at 2560x1067
  (capacity baked into patched shaders: needs a resolution-change plan); GI history ping-pong with
  RR at full res (62 MiB, core GI area); mesh pool 1024 MiB for 673 peak (shrink blocked by native
  hair ranges). The untracked 11.3 GB is mostly the game: core GI images are about 172 MiB.
- Check in game (lives paused): one run, toggle rt_capture_rise_reserve 0 / 1 / 0 at S8, read
  `[mem] ours` cascade / RT capture MB and `buffer full` in every window.
