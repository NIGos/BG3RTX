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

## Wind RGB, mask and long-session reads; capacity line per window, snapshot gates as events (branch worktree-agent-a2c38a4884e982692, 2026-11-02)
- m4w-wind-rgb-s8-fg2 08:32 VALID, every gate holds: wind RGB clip checked 7.9M, bit-exact all, rejected 0.0000%;
  qualified 45.0% -> 63.5% (+18.4 points, all three instanced families: the A/B toggles rt_hit_material_instanced);
  energy -0.00% (band 1.30%); cost +0.67 ms (gi trace pass 2.41 -> 3.08 ms). Kept on in Ultra (no change).
- hmm1-mask-s8-fg2 VALID: qualified +0.65 points (predicted 1.1, range 0.3-3.0), energy +0.04% (band 1.30%), cost +0.006 ms.
  hmm2-mask-inn-fg2 INVALID for the gate only: the `[hit material capacity]` line printed only on a frame % 120 query, and
  2040/2280/2400/2760 were rejected (source epoch), 1 line per diag window, while the gate wanted 2. Now the first query of
  each 120-frame window logs it (instanced::stats_window_due), and every snapshot gate (`hit material capacity`,
  `hit material instanced ... prepass`) is `"event": true` (m4_hit_mask, m4_wind_rgb, m4_pool_alpha). Re-gated, hmm2 passes:
  qualified 18.0% -> 30.1% (+12.1 points, above the 0.5-6 range), energy -0.80% (band 1.20%), cost +0.92 ms trace pass.
- ls1-long-session-camp-fg2 08:45: no unbounded leak. AS is 0.25 GB static and 0.8-1.4 GB in pan/rotate windows
  (transient BLAS). The 64 MB chunks are never freed: 58 -> 66 chunks (+512 MB) until cycle 14, then flat for 8 cycles,
  and VRAM is flat from cycle 13 (18.2-18.7 GB). VRAM trend now compares cycles 12-14 with 20-22 (passes on this log).
  The pool records line needs 120 frames at ~10 fps: the diag-pool windows are 20 s (plan 41 min). Records stay 411-417.
- Open (separate VRAM item): 66 x 64 MB chunks reserve 4.2 GB for 0.25-1.4 GB of AS (fragmentation, never freed).

## OMM upper bound windows in the hig1 camp rerun (branch worktree-agent-a2c38a4884e982692, READY_FOR_LIVE, 2026-11-02)
- Existing hot key `rt_alpha_test=0`: alpha-tested camera TLAS instances get FORCE_OPAQUE (bvhsplit.cpp 1536/1598, rtscene.cpp 1211), geometry kept;
  cascade casters already skip cutouts. No code change. Plan: omm-ub-on / -opaque / -on2, 25 s each before restore (300-frame gpu lines,
  10 s cadence at 30 fps: 2 per window). Gates per window: rt passes GPU cost, gi trace pass median, scene wait 0 timeouts.
  Predicted: unknown; >= ~3 ms saved pursue VT OMM, < 1 ms close it. Plan 239 s, about 6 min with the load.
