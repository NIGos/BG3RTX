## BLAS chunk refusals, tracker handle reuse, hair variants (branch claude/r2-layer, offline, READY_FOR_LIVE, 2026-11-20)
- Cause of the 162 refusals (user log 16:47-16:48, 137 cascade-led): at 16:48:11 the chunks held 4038 MB in 26 chunks
  (mean 155 MB, so nearly all larger than a 64 MB base chunk) for 1273 MB live, 0 empty. Small per-draw and cached
  ranges settle in the chunks made for the cascade casters (99-189 MB) and grooms (72-136 MB): fullest-first placement
  takes their tails, and a retained empty chunk joins any arena. One surviving small range pins 100-190 MB that no range
  over 64 MB can use, so the next cascade rebuild needs a new chunk past the cap and the casters are left out.
- `rt_as_size_class` (default 0, live): ranges up to a base chunk only in base chunks, larger ones only in larger chunks
  (`as_chunks::same_class`). Replay of the dolly in `as_chunk_alloc_test` (traffic scaled to the log, 8 seeds x 1380
  frames, cap 4045 MB): 53 refusals (all cascade, every seed) -> 2; reserve mean 3812 -> 3713 MB for 1459 live; the
  colonised large chunks (19-22 of them, 3.4-3.9 GB holding 0.9-1.7 GB) are gone. `rt_as_chunk_fit` made it worse
  (33 alone, 2 with the class key): unchanged at 0.
- `rt_cascade_keep_refused` (default 0, live): the new cascade BLAS is created before the old one is released; if
  refused, the previous BLAS is traced, translated from its own origin (`cascade::rebase`), instead of no casters. Its
  signature, triangle counts and update scratch stay the kept BLAS's, so no refit runs over another caster list.
- Always on: `[bvh] BLAS refusals in N frames: cached, per-draw/groom, cascade (previous BLAS kept, left out)` once per
  300 frames, only when there were refusals. `[mem] ours` adds the free space inside chunks over 64 MB and both keys.
- Review fix (tracker.cpp): `DestroyBuffer`, `DestroyImage`, `DestroyImageView` erased their records after the driver's
  destroy. A `CreateBuffer` + `BindBufferMemory` on another thread that got the freed handle in between lost its
  `buffer_mem` record for good (camera, ring and constant reads of that buffer fail). Records now go first.
- Review, detector only: the 16384-slot validation status ring can wrap onto the done word of a batch still waiting (up to
  240 frames, a recorded command buffer not yet submitted); counted in `[bvh] status ring wrapped onto ...`. Not seen.
- Hair variants: 519 = per G-buffer hair pipeline 1 fallback + 16 strand + 16 probe slots (15 x 33) + 24 prepass,
  velocity and cascade clones. In the user's log the first 132 took 17.2 s and the other 387 0.24 s (same-session
  driver cache hits): lazy creation of the duplicates saves nothing; the cost is the unique compiles that
  `rt_hair_header_early` turns into cross-launch cache hits. Skipping them with RTX Hair off needs a deep copy of the
  create info and the game's shader modules (destroyed after creation): not a release-size change. Left.
- Live check staged (not queued): `tools/live_batches_staged/r2_layer` (r2-inn, inn dolly, both keys 0/1/0, 8.6 min).
  Decisive: ON "free in partly used chunks (N in chunks over a base chunk)" N at most 30 MB against 300-2000 MB OFF;
  refusals ON <= OFF; energy within 0.5%; GPU ms equal. At this machine's 4224 MB cap refusals may be 0 in both states:
  then `rt_cascade_keep_refused` is not exercised there.
- Stream C: `pipeline_readiness.py` gained the two keys (host only, no pipeline).
