## Launcher Play on a fresh install, Experimental notice, RTX Hair restart key (branch claude/fix-launcher-play, ready to merge, 2026-11-23)
- Regression hunt (settings, launcher, package) against 0.9.5-beta cb2d03f2; report in .scratch/coord/hunt_tools.md.
- Play on a fresh install (no bg3rtx_user.ini) now writes the shown preset. Since instant apply (b657b2ba, 2026-11-14),
  Play wrote nothing, so the game ran bg3rtx.ini without `preset=`. The release policy then kept opacity micromaps and
  cutout masks off on High/Ultra (about 1 ms at camp). Before: 0 saves, rt_omm=0, rt_vt_alpha_masks=0. After: 1 save with
  `preset=high`, rt_omm=1, rt_vt_alpha_masks=1, and a second Play writes nothing (web_bridge_test).
- "Show experimental options" no longer lists "Custom value -> Off/Standard" lines or the false "(after a game restart)"
  for the four hidden opt-in options (web_bridge.cpp, the `also` loop skips -1 opt-in values).
- rt_hair_groom_nearest 0->1 needs a restart (key_change_needs_restart): it is a pipeline-creation specialization in the
  RTX Hair On bundle since 050328b5. The RTX Hair note and details now say that the nearest-strand trace waits for a restart
  when RTX Hair is turned on while playing.
- Not changed: Hair shadows and Detailed logging restart-banner gaps (same in 0.9.5; finding 4 of the report).
- How to check: delete bg3rtx_user.ini in a portable copy, open BG3RTX.exe, press Play: the file appears with `preset=high`.
  Turn on "Show experimental options": the notice has no extra lines.
