## Order-mode prepass swap engages (branch claude/hair-prepass-swap, READY_FOR_LIVE, 2026-11-04)

- hv7 09:31: 0 prepass strand draws next to 187 G-buffer ones. The prepass required g_applied_frame == now; the game
  records the next frame's prepass before the present that advances frame_index, so the last G-buffer apply carries
  this frame's number or the next. Accepted both. New counter on the 300-frame line: prepass card depth dropped
  without its G-buffer swap (the hair would miss there: predicted 0).
