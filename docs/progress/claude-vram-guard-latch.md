## VRAM guard no longer latches LOW after one budget drop (branch claude/vram-guard-latch, READY_FOR_LIVE, 2026-11-14)

- Bug confirmed in the user's `bg3rtx.log` of 2026-11-13: budget 31419 MB at 13:41:10, 15477 MB at 13:41:15
  (used 13256 MB), 13599 and 13522 MB at 13:41:41. The guard kept the largest drop of the session (15942 MB)
  as slack: margin 16454, then 17352 and 17672 MB, more than the budget. Leaving LOW needed
  used + 2 x margin <= budget (32908 MB of margin against a 31419 MB card): impossible for the rest of
  the session, and `allow()` refused every growth above 31419 - 15942 = 15477 MB.
- While LOW the layer asked the yielders at every sample (16 frames): empty BLAS chunks freed, the
  hit-material recycle pool cleared, LRU meshes evicted, new chunks refused (draws left out of the RT scene).
  The log ends 27 s after the drop (52 Streamline "Frame rate over 100 ms" lines, DLSS-G eWarnOutOfVRAM), so it
  has no evictions per minute and no budget recovery: the rate comes from the code (one ask per 16 frames).
- Fix (`common/vram_guard.h`, `layer/layer.cpp` vram_guard_tick), always on with `rt_vram_guard=1`:
  - The observed drop is the largest single-sample drop of this and the last stats window (`rt_timing_frames`,
    300 frames each; the same two-window memory as the largest grow step), and it is void at once when the
    budget is back at the level before the drop.
  - Slack floor: 2.5% of the budget (unchanged, the 789 of 31419 MB evidence). Slack cap: what the yielders can
    give back at that budget = caps of BLAS chunks + mesh pool + recycled pool (6783 MB = 21.6% on the 32 GB
    reference, 3340 MB at 15477 MB): a reserve beyond what the guard could free protects nothing.
  - One trim ask per LOW episode for the whole need; a new ask only when the need is worse than the one
    acted on by one BLAS chunk (64 MB, the smallest amount a yielder returns). An unmet ask is kept until its
    consumer takes it. bvhsplit's demand-driven cache share eviction is unchanged.
  - New always-on lines, one per transition: `[vram] OS VRAM BUDGET TOO LOW: ... grants this process X MB, it
    uses Y MB (budget peak ..., heaps ...)` when used > budget, and `[vram] OS VRAM budget covers this process
    again` when LOW is left.
- Replay of the logged sequence (unit test): margin at 15477 MB is 3852 MB (was 16454); LOW with one ask,
  a second ask at 13599 MB (used 14747, starved), none at 13522 MB; back to "growth allowed" at the first
  sample with 31419 MB; no refusal at full budget from 14 to 28 GB used; still LOW when used + margin
  exceeds the budget. Steady 13522 MB budget with 14.7 GB wanted, 4000 samples: 1 ask, 1 log transition
  (before: 4000 asks). With the budget staying low the drop leaves the margin after two windows.
- Tests: `vram_guard_test` (new checks for the replay, decay, cap, no-thrash); `.\scripts\build.ps1 -Test`.
- Cost: none (CPU policy on the 16-frame sample). No new key, no shader or ABI change, variants delta 0.
- Not changed: required RT capture growth (`allow_capture`, real budget), the per-consumer caps.
- Open question for the user: the budget halved while the game ran (31419 -> 15477 MB). Windows does that to a
  process whose window is not in the foreground, or another program took VRAM; the new log line names it.
- Live check (any save, normal play): alt-tab away and back, or start a VRAM-heavy program. Expect one
  `headroom LOW` line with a margin of a few GB, at most one `evicted ... LRU meshes` line per episode,
  `OS VRAM BUDGET TOO LOW` only if used > budget, and `headroom back, growth allowed` within one sample
  (16 frames) of the budget returning; no `refused` lines at 31419 MB with used below about 28 GB.
