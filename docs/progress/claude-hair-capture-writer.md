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
- "Strand hair on screen" (and "in ray-traced lighting") need hardware only now: local groom files no longer required.

## Live e2e 2026-11-08 22:40 (hcap3, Test Dialogue1, scratch assetdb without groom 8b32681bb9ecfc30): both VALID
- Capture: 8b32681bb9ecfc30 captured once its full mask arrived (121368 indices, 36112 vertices, 2048x2048 BC3, 12 mips,
  5.2 MB); 2-3 other styles stayed "waiting for a full mask" (placeholders). nv153 0 in both launches.
- bg3rtx_hairconv on the real capture: pass, IoU 0.966 / 0.939 / 0.926, coverage -0.7 / -1.6 / -1.2 points; 1.8 s
  (trace 0.08, dedupe 0.68, validation 0.21, write 0.84).
- Against the removed original (Python, GR2 bind pose): same index id, strands 153473 vs 154531 (-0.7%), points -0.2%,
  median radius ratio 0.998, 62% identical strand starts (the rest re-traced from the posed capture; quality gates pass).
- Second launch: the v3 groom decoded at level load (LOD 0 140 ms, LOD 1 141 ms) and drawn as strands (900 G-buffer
  strand draws per 300 frames). Scratch copy removed; the real assetdb still has 871 grooms.
