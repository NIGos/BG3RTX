## Strand coverage queries reset and read per pass (branch claude/hair-gb-queries, READY_FOR_LIVE, 2026-11-04)

- hv6/hv5 06:01-06:14: every coverage line 0/0. The G-buffer strand query waited for the prepass recording to be the
  same or already submitted; the game records both before submitting, so no strand query was ever recorded, no pair
  was ever complete and no evidence was observed (the line shows the newest evidence: none).
- Now each pass owns half of the slot's queries (card half reset in the prepass, strand half in the G-buffer, each in
  its own command buffer outside the render pass), its own key list and readback; card and strand sums pair per key and
  frame. No ordering between the two recordings is needed. Live check: hv6 order 1, nonzero card and strand samples.
