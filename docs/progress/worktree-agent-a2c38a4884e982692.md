## IP grass camp job INVALID: layer stats lines once per window, snapshot gates as events (branch worktree-agent-a2c38a4884e982692, READY_FOR_LIVE, 2026-11-02)
- Cause (hig1-ip-grass-camp-fg2 08:26): the layer's `[hit material instanced]` lines printed only from a query on
  frame % 120 == 0. Frames 1800 and 1920 were rejected (`source epoch current 1800 source 1799`), so 1 line in 3 diag
  windows. That line (frame 2040, IP grass prepass/G-buffer/mapped 0) sat in diag-off-b with the key off: correct, not a
  family-4 counter bug. Its "pairs same frame 31506" are since the run start (no earlier window line).
- The live toggle works: the layer shows 0 IP grass rows in diag-off-b; core rows need `h1.w == pc.frame` (no stale
  rows). The core clip "checked 44952" in diag-off-a is about 5 ON frames after the diag-on cfg read (29.012, line
  29.247: 0.057% of 78M lookups vs 1.4% in the ON line). The gate's straddle rule already skips such lines.
- Fix: `instanced::stats_window_due` (common/native_hit_instanced.h): the first query that reaches the log in each
  120-frame window prints both lines. Unit test: native_hit_instanced_test section 7.
- Gates: the per-family line is one query's snapshot, not a summary, so its two expect_every gates are now
  `"event": true`. At about 10 frames/s with rt_stats=1 the 120-frame cadence is 10-13 s, so the diag windows are now
  20/25/20 s (25 s is the lint's maximum; diag-on needs 2 core clip lines). The plan is 164 s.
- Re-gating the old log with the new gates: diag-off-b passes; diag-on and diag-off-a still have no line (the code fix
  is needed). Build + 96/96 CTests pass. No shader change.
- The dryrun's static checks pass. Its fake queue runs fail on main too (`-Force`): foreign expired jobs get recorded in the
  fake root.
- Rerun: rebuild the layer (game restart), rerun hig1-ip-grass-camp-fg2 as is.
