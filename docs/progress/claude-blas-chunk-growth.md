## BLAS chunks capped for unguarded callers; retirement-stall report (branch claude/blas-chunk-growth, READY_FOR_LIVE, 2026-11-09)

- Evidence: player log 2026-11-09 (0.9.5, RTX 5090, Ultra, RTX Hair on). After about 10 Ctrl+Shift+F9 toggles,
  BLAS chunks went 553 MB / 6 chunks (19 MB live) to 8196 MB / 91 chunks (15 MB live, 0 empty) at 08:02:05.
  The guard was Low and refused 64640 guarded requests. VRAM 32056 of 30392 MB, then a DLSS-G resource
  recreation failed and sl.common crashed (0xc0000005).
- Cause 1, the growth path: `as_create` (layer/bvhsplit.cpp) checks Low and the chunks' cap only for callers
  that pass `guard_what`. The cascade casters' BLAS and the mesh and groom compaction copies pass nullptr and
  could add chunks of max(64 MB, size) without limit. The cap at that budget is about 3.9 GB, so the 4.3 GB
  above it came only from these callers.
- Cause 2, why nothing came back: used minus live was about 8 GB. Freed ranges retire only when
  `frame < rt_scene_oldest_live()` (as_chunks::retired). A scene slot that never retires (or no new scene
  commit) pins that value, so every rebuild leaves a range pending and the cascade BLAS needs a new chunk
  each time. The log does not show which slot pinned it. 534 MB were already stuck from 08:01:14 to 08:01:39.
- Fix: `as_chunks::unguarded_chunk_ok` (common/as_chunk_alloc.h). An unguarded caller may add a chunk only
  while the reserve stays within the cap. Otherwise it is refused and logged in the guard window: the cascade
  casters are left out that frame, and compaction keeps the original. Retained empty chunks are freed before
  the cap check for every caller. Guard off: no change.
- Diagnostic: `BvhVram.as_pending`. Once per session, when pending passes 1408 MB (the chunks' floor, the
  largest live AS of the qualified long session), the layer logs `[vram] BLAS ranges awaiting retirement`
  with `rt_log_state` and `bvh_log_state` (slot states, oldest live slot).
- Test: as_chunk_alloc_test replays the stall: a 90 MB cascade BLAS rebuilt every frame for 120 frames. The
  reserve is 10800 MB uncapped and 3870 MB capped (77 rebuilds refused). Edge cases are covered too.
- Left: the root cause of the stalled retirement. Live check, same toggling in a busy scene: `[mem] ours`
  reserved stays at or below the cap (about 3.9 GB at a 30.4 GB budget), never 8 GB. If the stall happens
  again, the one-shot retirement-stall block names the slot that never retired.
