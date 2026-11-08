## Layer hair capture writer and groom re-index (branch claude/hair-capture-writer, offline, 2026-11-08)
- Key rt_hair_capture (startup; On with "Strand hair on screen"). layer/hair_capture.cpp writes .hcap v2
  (common/hair_capture.h) for each hair style drawn without a groom: index range, soup, mask mip 0 as raw BC3; full-mask
  rule (no 128x128 placeholders, re-capture on upgrade), card stretch stat, one capture per frame at most, worker
  thread, 512 MB folder cap, per-capture log and a 300-frame summary. The tracker adds TRANSFER_SRC to BC3 images.
- Cost once per style: GPU copy ~4.3 MB (typical) to ~11 MB; render thread 0.1-1 ms; worker 10-30 ms; none per frame.
- Re-index at a level load: new groom files only (hair_groom::rescan).
- bg3rtx_hairconv reads .hcap v2 (BC3 decode). CTest hair_capture_cpu_test (rule, stretch, round trip, BC3).
- READY_FOR_LIVE: tools/live_batches/hair_capture hcap2-capture then hcap2-strands (two launches: startup key, no
  in-run reload); lint 0 failing; not queued.
- Open: "Strand hair on screen" still requires local groom files, which a fresh install does not have.
