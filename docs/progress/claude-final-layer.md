## Final consolidation, host layer (branch claude/final-layer, offline, 2026-11-20)
- User log 16:45-16:50 (0.9.6-hairtest18, Ultra, FG 2x, RTX Hair toggled live): no device or swapchain error, no
  `VK_ERROR`, guard quiet except 16:47:15-20 (LOW at 26.4 GB used). Peak used 27.1 GB (16:48:13; 26.5 GB on 14 Nov),
  ours 8.7-15.3 GB tracked. 46 Streamline "Frame rate over 100 ms" warnings, in bursts at loads and live hair toggles.
  Defects found: (1) two groom LODs decoded 9 times each (1.07 s) under five 100 ms hitches; (2) 162 BLAS chunk refusals at
  the chunks' cap, 137 in windows led by the cascade casters (left out those frames; 4.0 GB reserved, 1.2 GB live); (3) 17.4 s of strand-variant pipeline
  creation at launch (19.6 s in the 14 Nov log).
- Groom decode (fix, on): the pool range is taken from the index's counts before the decode; a refused pool growth
  no longer throws a 30-190 ms decode away each build. Check: no LOD id with several "decoded" lines before its
  "uploaded" line.
- Tracker: freed sets and the sets of reset or destroyed pools leave `sets`, `dynamic_counts`, `set_layouts`
  (descriptor_epoch_test). `detailed_logging` adds `[tracker] descriptor sets N ...`: flat over a long session.
- `[mem] ours` now splits the BLAS chunk reserve: pending, free in partly used chunks, per arena, oversize chunks.
  The cause of item (2) is not settled offline: in vr-inn 23:38:05 (still) 2157 MB in 28 chunks held 554 live,
  2 pending, 360 empty, so 1241 MB were holes in partly used chunks. A Python model of the arenas (grooms 72-136 MB
  compacted to about 30%, cascade 90-155 MB, 600 per-draw slots) gave no gain for "empty chunks only for ranges of half
  their size" or a separate groom arena (-5% to +5%): no key shipped. The next user log names the split.
- `rt_hold_pack_base` (default 0, live): the 24-byte hold pack also for the 64-byte layout. Predicted: 91.5 -> 34 MB
  written per static-camera build at S8, 57 MB less pool, about 0.05 ms GPU. READY_FOR_LIVE.
- `rt_hair_header_early` (default 0, startup): the strand header is made at device creation. Its address is a
  specialization constant of 519 variants; a new address each launch missed the driver cache: 14.7-48.5 s in 18 of 25
  queue launches, 190-501 ms in the 7 whose address repeated. READY_FOR_LIVE: two launches with the key, same
  `header at 0x...` both times and `strand variants: 519 pipelines in` under 1 s in the second.
- Small: `log_stats` gate cached per cfg_poll; cascade draws read two keys through CfgHot; `rt_hair_dense_sample` maps
  pruned. Kept: `log()` flush per line (9 lines/s in release, the runner reads the live log, crash safety).
- Left, with reasons: mesh pool repack of grooms (the groom BLAS, the camera BLAS and header rows hold point addresses:
  a move is a rebuild, not small); pixel-LOD camera BLAS compaction (key off in every preset, about 16 MB per groom);
  refit skip for unchanged per-draw BLAS (needs a GPU content stamp); deferred strand-variant creation (the create
  infos die with the call: a deep copy of the chain); dead-code sweep not started.
- Stream C: add `rt_hold_pack_base=0` and `rt_hair_header_early=0` to bg3rtx.ini / settings.h (Advanced).
- Live check (staged in words only): S8, static 2 x 120 frames with `rt_hold_pack_base` 0/1/0: `hold_pack_bytes`
  up, `hold_copy_bytes` down, energy within 0.5%, 0 held-draw rejections `why 2`; plus the two-launch header check.
