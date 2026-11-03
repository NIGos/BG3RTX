## Strand header in the G-buffer's command order (branch claude/hair-publish-order, from 51956f86, 2026-11-03)
- Key rt_hair_groom_primary_order (off, live): the groom header is staged at the build (prepass command buffer) and
  applied right before the main G-buffer pass in its own command buffer: the newest staged publish whose build is in
  the same command buffer or already submitted (hair_primary::pick_publish, unit-tested); none: no strand swap that
  frame, the next prepass keeps its card depth. G-buffer and velocity swaps need the draw in the applied groom set.
  The counts pair with the applied tag. Layer only, no shader change. Build, 147/147 CTests. Not run live: use it if
  the confirming run shows "carry an adjacent frame's tag" lines (A/B: toggle the key live, expect those lines to stop
  and strand draws > 0 in every strand window).
- Knob exp_hair_primary_order (Developer, dev_only, Live, off in every preset). The 300-frame counted-frame line now
  reports the counted frames with adjacent-tag pixels and the header order. hv5 gains the header-order A/B after its
  stages: order-prepass-1/2 (key 0: adjacent-tag frames observed, not gated) and order-gbuffer-1/2 (key 1: no
  adjacent-tag frame in -2, gated), strand draws gated in all four; predicted.json has the decision. live_flow 5 jobs 0
  failing; 147/147 CTests.
