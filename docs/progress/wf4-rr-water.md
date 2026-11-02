## rt_water_rr_mv: RR's motion vectors on water; wf4 job (branch wf4-rr-water, READY_FOR_LIVE, 2026-11-02)
- What RR gets on water today: the game's depth (D24S8) and motion vectors (its RG16F velocity pass), both of the BED
  (or the sky over an empty G-buffer pixel): the water writes neither. The guides (rr_guides.hlsl) are built at the
  G-buffer pass, also the bed's. `rr` is live (0: the game's DLSS SR, history reset; ngx.cpp).
- Fix (off): `rt_water_rr_mv` (live). shaders/rt/water_mv.hlsl, after water_primary: our RG16F copy of the game's
  vectors, on water pixels mv + prev_px(P_water) - prev_px(P_bed) with the previous frame's lighting camera (stored by
  the pass; same pixel and one previous camera for both points, so both frames' jitter cancel). ABI 26
  `set_rr_motion_vectors`: the layer gives it to RR instead of the game's (same size and format, while fresh) and puts
  the game's back after the evaluate; DLSS-G keeps the game's. Depth and normal/roughness guides unchanged (next, if
  the motion helps). ABI 25 -> 26: rebuild both DLLs, restart the game.
- Identity: no existing shader changed (rt/water 33/33 identical to main by inputs); water_mv compiles, passes
  spirv-val, its bindings match the layout. Build + 97 CTests pass. No unit test: the logic is the shader's.
- Job wf4-s8-fg2 (foreground, 173 s, 5 bursts ~11.5 GB): rot-off-a / rot-rrwater / rot-rroff (rr=0) / rot-off-b,
  wf3's turn protocol and masks. predicted.json: energy null, cost +0.02 ms; flicker below both OFFs = RR's water
  reprojection. wf3's job removed (analysed). Dryrun + lints pass.
