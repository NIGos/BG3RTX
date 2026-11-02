## Water flicker attribution job wf3-s8-fg2 (branch wf3-nocompose, READY_FOR_LIVE, 2026-11-02)
- wf3-s8-fg2 (foreground: wf2 ran unfocused at 30 fps): calibration burst, then rot-off-a / rot-nocompose / rot-off-b,
  same direction from the same pose, every water image key (compose, sun / local shadows, refraction, game shadow)
  on / off / on, toggled 9 s before each burst; rt_debug 21 masks static and in the turn; energy and cost shots.
  161 s, 4 bursts (about 9.2 GB, deleted after the analysis). Dryrun + lints pass. wf2's job removed (analysed).
- predicted.json (committed before the run): shot-nocompose energy +12.6% (water-compose measure: the Ultra water
  keys remove -0.0217 in frame at this pose, wf2), compose GPU -0.41 ms. Flicker: nocompose water-masked rate within
  the OFF spread = the game's own water flickers; clearly below = ours (compose switches, shadow ray noise).
- Result 141227 (VALID, foreground, bursts 48-50 fps): water-masked transient rate rot-off-a 3.38%, rot-nocompose
  3.53%, rot-off-b 3.61%; whole frame 2.92 / 3.17 / 2.66%. Every water image key off (compose not running during the
  burst: no compose GPU line 14:15:45-59) leaves the flicker unchanged: it is not the RT water compose. Energy
  (water-compose) nocompose +11.13% vs predicted +12.6% (band 2.12%): as predicted. Compose GPU 0.41 ms (0.52-0.57
  in the turn), 0 when off. Bursts deleted (9.5 GB); report: scratchpad wf3_141227/report.json.
- Next (not the compose: its switches, temporal stabilisation and tier 2/3 hit shading would leave this rate
  unchanged): RR's inputs on water (the water writes no depth and no motion: RR reprojects the surface with the
  bed's), A/B rr on/off or a water motion/depth input; else the game's own water (flow-mapped normals, probe).
- rt_water_rf_edge marked REJECTED and rt_water_rr_hitdist NULL in bg3rtx.ini (both stay 0, no GUI entry).
