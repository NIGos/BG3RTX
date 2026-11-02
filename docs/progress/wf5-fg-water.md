## Water flicker at display rate: FG water vectors, glint filter, presented-frame scoring; wf5 job (branch wf5-fg-water, READY_FOR_LIVE, 2026-11-02)
- Diagnosis from the kept logs (wf4 143137): every frame with a water pass was composed in every window, static and
  turn (uncomposed 0, on/off flips 0, no scene / no lighting refusals 0). The compose reads the PREVIOUS frame's
  lighting on 108-120 of 120 frames and this frame's on 0-12, in static and turn windows alike (an intermittent
  one-frame source switch, visible only while the camera moves). The water's sun visibility is one sun-disc sample per
  pixel per frame (rt_shadow's jitter): at shadow edges a ~1000 glint switches on / off from frame to frame. The
  wf2-wf4 metric (pan_artifacts) scored only dips of RENDERED frames: a glint switching ON, and anything DLSS-G adds
  in the generated (presented) frames, was invisible to it.
- Tools: pan_artifacts now also reports peaks (`luma_peak`, the dip rule on -luma; water_mask.peak_rate);
  scripts/qa/fg_present_flicker.py scores presented PNG sequences (second difference per present parity, water mask
  vs rest). Layer: `fg_snap_any` (capture presents with FG off too), `fg_snap_delay` (start N frames after the
  request, inside the turn). Unit tests in tests/pan_artifacts_test.py (20 pass).
- Keys (off, live): `rt_water_fg_mv` (DLSS-G gets the water_mv vectors: the water surface's parallax; double-buffered
  by frame parity), `rt_water_glint_aa` (rt/water_glint.hlsl pre-pass + WATER_GLINT_AA compose: each lit sun record's
  sun term replaced by the (1 2 1)^2 tent mean of the valid neighbours', the mean kept). Screen-space, not the FS
  roughness patch (a SPIR-V rewrite of the FS's roughness uses, not attempted here). No ABI change.
- Job wf5-s8-fg2 (foreground, one launch, 271 s, 5 bursts + 6 x 32 presents): off-a / fgoff (fg=0 live) / fgmv /
  glint / noshadow (rt_water_shadows=0) / off-b, wf3's turn protocol; predicted.json first. Dryrun + lints pass.
