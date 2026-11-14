## VRAM right-sizing, first step: capture rise reserve bounded by the seen peak (2026-11-14)

Branch `claude/vram-rightsize` on top of `claude/consol-layer`. Offline only; stopped here at a context boundary.

Done:
- `rt_capture_peak_reserve` (default 0, live; cascade and RT capture). Cause: `CaptureHeadroom` learns the largest
  rise once and adds it at every level. In the user's log the cascade demand alternates between 54-70 MB and
  490-580 MB (rise 535 MB), and the 535 MB went on top of the high level too: 3 x 1088-1120 MB. With the key the
  rise reserve stops at the highest demand of the last 7200-14400 samples + 25%; a rise in progress keeps its own
  reserve; after two quiet windows it decays to the 25% rule.
  Replay of the 130 logged cascade demands (CTest `rt_capture_budget_test`): 1044 -> 769 MB per slot at the high
  level, 3 slots -826 MB; low level unchanged (611 MB); 0 samples that fitted before and not now.
  Risk: a jump from the high level to a level never seen in the window overflows once (casters or draws left out
  for the replacement delay, 3-5 frames), where the old reserve might have held it. Live observable:
  `[cascade] casters ... buffer full 0.0`, RT `left out` 0, `[mem] ours` cascade capture.
- Keys `rt_capture_peak_reserve=0` and `rt_hair_groom_async=0` added to `bg3rtx.ini`, defaults regenerated.

Not done (notes for the next pass):
- Why three cascade buffers: one per scene frame in flight (the ring `g_ccap`, a slot sampled every third build);
  each is sized alone. Not examined further.
- RT capture: its floor is the preset (`elastic_rt_capacity` fixed_capacity = rt_xfb_mb, 1408 MB at the 88-byte
  stride). Starting smaller means dropping that floor: an A/B of its own. The peak reserve applies to its rise.
- BLAS chunks: empty chunks are already freed after `rt_as_chunk_reuse_frames` (120); the gap between reserved
  and live is space inside partly used chunks. `rt_as_cap_occupied` (rv5) is still the staged answer; not run.
- Mesh pool shrink ("deferred: low-demand window / native hair ranges"): not read.
- 88-byte stride for every draw with RTX Hair: about +37% on both captures (1408 vs 1024 MB start); not scoped.
- Pixel-LOD camera BLAS compaction: not started.
- The live job `tools/live_batches_staged/vram_rightsize/` is NOT staged (no predicted.json, no lint).
