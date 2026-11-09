## Scene handout design (branch claude/scene-handout-design, design only, 2026-11-09)
- docs/SCENE_HANDOUT_DESIGN.md: how to remove the `[rt] scene wait` (3.3 ms static, 5-7 ms motion at S8).
- Design: set 1 update-after-bind, TLAS read by address from the slot's scene stamp (as the groom TLAS already is),
  fallback to the previous slot by one descriptor-set copy at vkQueueSubmit. All four review blockers answered.
- Smaller steps (notify before AS recording, scratch reuse) remove at most ~1 ms; the rest is the game's recording.
- Next: run the prepared sh1 job (rt_scene_wait 1/0/1, uncapped) to get the frame-time ceiling; build only at >= 1 ms.
