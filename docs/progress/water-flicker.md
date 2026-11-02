## S8 water reflection flicker in rotation: causes, rt_water_rr_hitdist (off), live job (branch water-flicker, READY_FOR_LIVE, 2026-11-02)
- Causes found in the code (the compose's frame-level stability is fine: 0 on/off flips):
  1. RR's specular hit distance on water pixels is D-RF's at the G-buffer surface = the bed (core/gi.cpp:3117
     set_spec_hit_distance(g_rf_hit); shaders/gi/rt_reflections.hlsl:484; the water writes no depth,
     water_reflect.hlsl:6-14), often kNoHit (rough bed): RR reprojects the mirror image with the wrong virtual depth
     when the orbit camera translates. Most likely; fix under test.
  2. RR's normal/roughness guides are the bed's G-buffer (layer/ngx.cpp:12-16 rr_guides), not the mirror water.
  3. The water pixels' motion vectors are the bed's (no water depth/velocity): surface parallax of the water plane
     against the bed below.
  4. The compose resolves per pixel (tier a, water_compose.hlsl header): with the FS's flow-mapped normals the
     resolved/unresolved mask moves as reflected samples leave the screen in rotation (C' = C + A L - B vs C).
  5. water_reflect's water TLAS is one build old (age 1.07 frames, shifted by translation only): its normal and
     hit distance lag the animated waves by a frame (compose uses the FS's own N, t).
- Fix behind `rt_water_rr_hitdist` (default 0, live): request rt/water_reflect.hlsl?WATER_RR_HIT=1 writes the mirror
  ray's hit distance (tier 1) or no hit into g_rf_hit on water pixels (binding 30 in the water push set, a stand-in
  when off). Key-off SPIR-V byte-identical (shader_identity vs main 566db3d); variant compiles; variants.txt,
  shader_variant_test, variant_matrix_test, settings_test pass.
- Live job tools/live_batches/water_flicker wf1-s8-fg2 (unfocused, 163 s, 3 K16 bursts about 7 GB): energy null,
  cost 0, flicker via pan_artifacts on rotation bursts ON vs both OFF. lean_dryrun gained -Unfocused and -MaxBursts.
