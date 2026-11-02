## Native hair Ultra blockers: water hair variants, background pipeline builds (branch worktree-agent-a78f48005e3b350bd, READY_FOR_LIVE, 2026-11-02)
- Water: `water_request(req, cascade, scalp)` adds RT_NATIVE_SCALP;RT_NATIVE_HAIR to water_reflect/compose when
  the layer's scalp capability is set (same rule as gi/ refresh). The hair origin needs nothing on water (launch
  image of gi/ receivers). rt_native_scalp/hair/hair_physical_origin left `kWaterNoVariant`; 32 new requests in
  variants.txt, all compile and validate (tools/shader_identity.py, 12.8 s); key-off requests unchanged, no HLSL
  changed (32/32 identical inputs vs main). variant_matrix: only rt_omm / rt_native_tileset still skip water.
- Freeze: new pipeline variants (DXC up to 10.4 s + driver creation up to 7 s each on the render thread, ds1 log
  10:03:51-10:04:52, about 60 s) now build on 2 below-normal workers (common/pipeline_async.h, core/gi.cpp refresh,
  core/water.cpp make_pipe). A new request skips its pass until ready (never another request's pipeline); a hot
  reload keeps the current one. Pipeline layouts a build reads are destroyed only after it (park); unload joins.
  Failed builds retry after 240 polls. Unit test pipeline_async_test (293 checks); 98/98 CTests.
- Live check (not run): enable native hair in game; expect no freeze, `building in the background` lines, passes
  appearing within about a minute, no `skipped: no variant for` with hair on, water reflections still drawn.
  Layer stat "render thread waited" now also counts worker waits (layer unchanged).

- Qualification batch tools/live_batches/hair_qual (READY_FOR_LIVE, not authorized): S8 water + Test Dialogue1-3,
  smoke_ultra keys + hair latches, hair toggled live, every checklist item below gated per window; new core lines
  `gi: background builds (N frames)` / `... done: ... longest present interval L ms` gate "no pass skipped while
  measuring" and the startup (first enable) burst (L < 1000 ms, done by settle-cam). Needs a0c9be1 (char_energy.py).

### What native hair still needs for Ultra (NATIVE_HAIR.md, DIALOGUE_SHADOWS.md)
- [ ] Participation: `[alpha hair] native coverage records` > 0 and `[alpha scalp]` > 0 in every hair window, all three dialogue saves.
- [ ] Invalid/missing native state 0, retired bounds-invalid 0, alpha made opaque 0.00 per window (10-31 lost 28-31 cutouts/frame).
- [ ] Scalp coverage: scalp camera dither / hard-cut records match native (scalp gate 95/95 offline only); Astarion hairline band (class 5 shared Hair/Pubes/Scalp).
- [ ] Hair origin: matched / available / unresolved (rt_stats 188-190) in diag-rt-hairon; primary-ray match vs the same run's off/off repeat.
- [ ] Energy: character-ROI ON >= OFF - spread; non-Hair pixels unchanged.
- [ ] Cost: cost-hair minus mean(cost-off-a, cost-off-b) with spread (prior 10-31: RT 3.44 / 3.41 ms).
- [ ] smoke_ultra with hair on: water drawn, no `skipped: no variant for`, no scene-wait timeouts.
- [ ] Other families still opaque: lashes, beards, fur, ExtraUV; all 15 CHAR_Hair FS absent from alpha_fs.txt (opaque path, geocapture.cpp:1956).
- [ ] Startup cost: rt_xfb_mb / rt_cascade_mb scaled for the 88-byte vertices; then the GUI default (Experimental until all saves pass).
