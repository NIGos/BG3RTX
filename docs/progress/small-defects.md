## Small defects: inn capture split, A/B toggle latency, roof-cut leak estimate (branch small-defects, 2026-11-02)
- Inn "opaque draws dropped (buffer full) 0.02/frame" (lr2 044237, 0.01 at 023108): both windows at the inn rotation,
  alpha region 547-613 MB (alpha demand 418 MB) + opaque peak 427-440 MB in 1024 MB. alpha_region kept alpha's 1.5x
  reserve while opaque had 1.1x (about 373 MB last frame): the opaque jump overflowed with 195 MB of alpha slack.
  Fix (common/rt_capture_budget.h): when both reserves do not fit, split the buffer by the two demands (same relative
  headroom), never below the alpha floor: 541 / 483 MB there. Unit test in rt_capture_budget_test. Layer (relaunch).
- A/B latency: `[ab] toggle latency: X ms from the toggle (frame F) to the present of frame F+2, the first recorded
  entirely with ray tracing OFF|ON` (layer/util.cpp; includes a shader compile on the render thread). New key
  `ab_off` (default 0) toggles the same A/B from the settings, since the queue cannot press Ctrl+Shift+F9.
  smoke_ultra su1: ab-off / ab-on phases (8 s each) and three expects on the `[ab]` lines.
- gi_far roof-cut leak (estimate, no replay: a live batch was running): screen hits beyond 1 m are 4.9% of S8 rays
  (31.9M of 655.6M); the roof rule blocks 85% of the qualified sun-lit far hits there (24.2M of 28.4M), so most
  far rays from roofed pixels do cross a cut roof. A screen hit through it needs on-screen geometry above the room
  within 6 m (neighbouring walls and roofs). Upper bound: the whole far bounce gain, +0.88% of S8 screen radiance
  offline. Measurable on the packs with one rt_stats counter (visible, t > R0, !gi_far_roof_clear) on S8 142819-1
  and inn 013914-3 (casters in the pack) with --sun-trust.
