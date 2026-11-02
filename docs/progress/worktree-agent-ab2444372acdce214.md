## VRAM budget guard rt_vram_guard (branch worktree-agent-ab2444372acdce214, READY_FOR_LIVE, off, 2026-11-02)

- Evidence: S8 port, custom Ultra, low camera long view 20:04: 21992 of 31419 MB, then 31668 of 30630 MB and over
  budget until 20:07; 95 Streamline >100 ms lines and 12 fence timeouts from then on. Ours in those 23 s: BLAS
  chunks 81 -> 124 (5.2 -> 7.9 GB reserved for 253 -> 1059 MB live), cascade 427 -> 544 MB x 3.
- Policy `common/vram_guard.h` (unit test `vram_guard_test`, 107 checks). Margin = largest grow step (this or
  the last window, at least one 64 MB chunk) + slack (2.5% of the budget = the 789 MB drop of the evidence, or
  the largest drop seen). Low when used + margin > budget, back when used + 2 margins <= budget. Low: every grow
  refused (logged once per window and consumer); yielders asked in order: empty BLAS chunks, hit-material
  recycled buffers, meshes unused for 8 frames (LRU). Live need is never asked.
- Caps per card: each grower's 32 GB size scaled by budget / 31419 MB, never below its start size: BLAS chunks
  4224 MB (12 GB card: 1576), mesh pool 2047 (764), cascade buffer 2047 (764), recycled pool 512 (191).
  Empty chunks above the BLAS cap are freed at once.
- Wired in layer: bvhsplit (new chunk for new meshes and per-draw BLAS, pool grow, yields; compaction and
  scene BLAS never refused), geocapture (cascade grow, 3 x new size), hit_material (recycled pool cap, clear on ask).
- `[mem] ours (...)` line every rt_timing window (key on or off), on a state change and after a refusal: BLAS
  reserved / live / empty, mesh pool, RT capture (alpha region), cascade, recycled, em and water soups,
  tracked vs process usage, rest (game, core GI/RR images, untracked), within/OVER budget.
- GUI: Experimental "VRAM budget guard" (`exp_vram_guard`, live). Runner: plan field `wheel` (mouse-wheel
  notches, `[wheel-input]`) for the low camera.
- Verified offline: build, 93 non-shader CTests (settings_test with the schema), vram_guard_test, dryrun of
  `tools/live_batches/vram_guard` (all checks passed). No shader change. Cost: CPU only (one budget query per
  16 frames); GPU ms to be read in the live run.
- Pending live (not queued): `vg1-s8-low-long-view-fg2`, gates and predictions in its README / predicted.json.
- Known limit: chunks that are mostly empty but not wholly empty are not compacted (docs/VRAM_AUDIT.md rank 1
  relocation); the mesh pool and the cascade reserve never shrink (ranks 2 and 3). If the live run shows
  refusals while within budget, the BLAS cap costs detail: then relocate, or apply the cap only when low.
