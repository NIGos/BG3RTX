## VRAM right-sizing: five policies behind keys, replayed on the user's logs, one live job staged (2026-11-14)

Branches `claude/vram-rightsize` and `claude/vram-rightsize-2`. Offline only. No shader or ABI change. All keys default 0, live.
Evidence: the user's play logs of 22:12-22:27 (ours 10.2-12.6 GB of 23-27.5 GB used) and hq-s8-ultra / hq-inn-fix.

| Pool | Cause | Key | Replayed saving |
| --- | --- | --- | --- |
| BLAS chunks | A BLAS over 64 MB gets a chunk of its exact size. The cascade casters' BLAS is rebuilt at a new size when its draw list changes; a larger one fits no retained empty chunk: 18 new chunks, 448 -> 1770 MB for 356 MB live (S8 21:54:48). | `rt_as_chunk_fit`: that chunk is sized + 1/8 in 16 MB steps; the smallest retained chunk that fits is taken. | 1316 -> 160 MB for that event; the user's log had 99-1214 MB of empty chunks in 15 of 19 samples. |
| RT capture | The preset floor (1408 MB at 88 bytes), and the demand counted the 40% alpha region the buffer holds for itself: 1632 MB kept for 130 MB of soup. | `rt_capture_demand_size`: need without the held region, floor one quantum; the first buffer stays the preset. | Mean 1787 -> 1544 MB, low level 1408 -> 832 MB; with the two keys below: peak 2047 -> 1808, mean 1485 MB. |
| Capture presets | Presets follow the 88-byte stride although the wide split keeps every non-strand draw at 64 bytes (0.0 wide casters per frame). | `rt_capture_split_mb`: presets by the base stride (RT 1024, cascade slot 427, alpha floor 171 MB). | First buffers -384 MB (RT) and 3 x -160 MB (cascade); reserve -40 MB per cascade slot after that. |
| Cascade capture | The rise reserve stacked on the high level (first pass). | `rt_capture_peak_reserve` | 1044 -> 769 MB per slot, 3 slots -826 MB. |
| Mesh pool | The repack moves meshes and holds only, so with RTX Hair it never runs (36 "native hair ranges" lines): 1947 MB for 400-1420 MB used. | `rt_mesh_pool_trim`: a shorter pool with the same offsets when its end is free and a quarter is saved, after the same quiet window. | 1947 -> 1353 MB (-594) at 22:24 if no mesh lies above the last groom (1162 MB); no trim at 22:19 (a groom at 1585 MB). |

Expected together: about 1.5 to 2.5 GB less reserved in the user's session (cascade 0.8, RT capture 0.3-0.6, BLAS
0.3-1.1 in motion, pool 0-0.6), from replays, not measured live.

Answers without a change:
- Three cascade buffers: slot F % 3 records frame F while the builds read F-1 and F-2, and each holds one frame's whole
  caster soup. Each is sized from its own samples, which see the same demand. Two slots would need the build to wait
  for the recording: not a sizing change.
- The 88-byte stride is already limited to strand-frame shaders (`rt_capture_wide_split=1`); only the presets were left.
- `rt_as_cap_occupied` (rv5, never run): it lets oversize chunks pass the cap up to + 25%. It lowers refusals, not the
  reserve. With `rt_as_chunk_fit` the oversize requests reuse chunks; keep it at 0 until a live shows refusals remain.

Cost bounds (stated, not measured): a capture demand jumping past its reserve leaves draws out for the replacement
delay (5 frames RT, 3 cascade), then one replacement of at least 1/8, old + new allocated 4 frames. A trim is one
copy of the kept bytes, at most once per 254 frames. A fitted chunk wastes at most size / 8 + 16 MB.
Hysteresis: shrink after 120 low samples with 25% to spare, trim only for a quarter saved (replay: 9 against 8 growths).

`[mem] pools reserved/used MB` (detailed_logging, one line per window): cascade capture, RT capture, BLAS chunks (live,
pending, empty), mesh pool, hit-material recycled, OMM, hair in the pool and BLAS, VRAM used and budget, the keys.

CTests: `as_chunk_alloc_test`, `rt_capture_elastic_test`, `rt_capture_budget_test`, `vram_compact_test`.
READY_FOR_LIVE: `tools/live_batches_staged/vram_rightsize/` (vr-s8 293 s, vr-inn 514 s after load; predicted.json first;
lint live and frozen 0 failing). Not queued, not authorized.

Risks: the trim switches the pool inside the build like a growth (a dropped command buffer would leave the new pool
without its copy, as for a growth today); the capture policies can drop draws for a few frames at a cut.
Not done: pixel-LOD camera BLAS compaction (about 16 MB per groom before compaction; needs a second query and copy per
groom in the groom build path, no offline test); moving groom ranges in a repack; the ms of one replacement.
