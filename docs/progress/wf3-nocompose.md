## Water flicker attribution job wf3-s8-fg2 (branch wf3-nocompose, READY_FOR_LIVE, 2026-11-02)
- wf3-s8-fg2 (foreground: wf2 ran unfocused at 30 fps): calibration burst, then rot-off-a / rot-nocompose / rot-off-b,
  same direction from the same pose, every water image key (compose, sun / local shadows, refraction, game shadow)
  on / off / on, toggled 9 s before each burst; rt_debug 21 masks static and in the turn; energy and cost shots.
  161 s, 4 bursts (about 9.2 GB, deleted after the analysis). Dryrun + lints pass. wf2's job removed (analysed).
- predicted.json (committed before the run): shot-nocompose energy +12.6% (water-compose measure: the Ultra water
  keys remove -0.0217 in frame at this pose, wf2), compose GPU -0.41 ms. Flicker: nocompose water-masked rate within
  the OFF spread = the game's own water flickers; clearly below = ours (compose switches, shadow ray noise).
- rt_water_rf_edge marked REJECTED and rt_water_rr_hitdist NULL in bg3rtx.ini (both stay 0, no GUI entry).
