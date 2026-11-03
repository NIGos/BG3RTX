## Strand header in the G-buffer's command order (branch claude/hair-publish-order, from 51956f86, 2026-11-03)
- Key rt_hair_groom_primary_order (off, live): the groom header is staged at the build (prepass command buffer) and
  applied right before the main G-buffer pass in its own command buffer: the newest staged publish whose build is in
  the same command buffer or already submitted (hair_primary::pick_publish, unit-tested); none: no strand swap that
  frame, the next prepass keeps its card depth. G-buffer and velocity swaps need the draw in the applied groom set.
  The counts pair with the applied tag. Layer only, no shader change. Build, 147/147 CTests. Not run live: use it if
  the confirming run shows "carry an adjacent frame's tag" lines (A/B: toggle the key live, expect those lines to stop
  and strand draws > 0 in every strand window).
