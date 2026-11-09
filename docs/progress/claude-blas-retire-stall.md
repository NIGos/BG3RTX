## Scene slot material records pinned against pruning (branch claude/blas-retire-stall, READY_FOR_LIVE, 2026-11-09)

- Evidence: player log 2026-11-09. From 08:01:07 to 08:01:39 no scene was committed ("0 got this frame's
  scene"), and the mesh pool was full (1022 / 1024 MB) with its growth refused. 534 MB of freed BLAS ranges
  stayed pending. After builds resumed the pending bytes grew to 8 GB (15 MB live) by 08:02:05.
- Cause: the current scene slot is never re-picked while builds fail (pick_scene_slot skips g_scene_cur).
  Its material record ages past 4096 newer records, and bvh_rec_register prunes it. slot_retired
  (rtscene.cpp ~181) then reads state -1 and returns false forever. So the slot is never reused, and
  update_oldest_live (~194) keeps its old built frame. as_chunks::retired requires frame < oldest_live,
  so no freed range retires again. The other 7 slots keep rotating, so no "every scene slot" line appears.
  A likely trigger for the failed builds is the native-hair bounds pool_alloc (bvhsplit.cpp ~2328): it
  aborts the whole build when the pool is full. The F9 toggles coincided with this.
- Fix: the registry prune moved to vram_compact::prune_records and skips pinned records. rtscene pins a
  slot's material record (bvh_rec_pin) from assignment until the slot is built again. With at most 8
  pins, the registry stays bounded.
- Test: vram_compact_test replays 10000 records, the slot's record, then 30 s x 60 fps x 4 records. The
  record is lost unpinned and kept pinned, and is pruned normally after release. Limit and keep edges
  are covered.
- Live check (with claude/blas-chunk-growth): after a stretch with no commit, `[mem] ours` used minus live
  drops back within seconds of builds resuming. No `[vram] BLAS ranges awaiting retirement` line.
