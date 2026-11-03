## Strand shadow transmittance, C2 (branch claude/hair-shadow-transmittance, 2026-11-03)
- C2a, GPU check (tools/hair_shadow_gpu_check.hlsl, CTest hair_primary_gpu_test): strand crossings per ray on the
  layer's LSS geometry vs the CPU count. Chained joints are reported by both segments: raw candidates and successive
  closest hits over-count 35% of rays (2.33 / 2.29 vs 1.90 per ray). hp_crossing (adjacent segments of a strand once)
  matches the CPU on 100% of 3000 rays with one non-opaque query.
- Fix: gi/hair_fibre.hlsl hf_strands (dual scattering n) now uses that one query: the over-count made the fibre-lit
  hair too dark (a_f^n with n too large); one trace instead of up to 8. Key-on only (rt_hair_fibre): identity 258/262.
  Live: expect hair under rt_hair_fibre slightly brighter (energy check pending).
- C2b, strand shadows with transmittance (key rt_hair_groom_shadow_t, read by the layer; not yet in common/settings.h,
  added after the GUI review lands): the strand G-buffer variant stores one colour sample (albedo, fibre roughness)
  per groom and 8 x 8 block; the count pass turns the counted frame's samples into a_f (hair_dual.hlsli table), read
  back with the strand counts; an exponential mean per draw key goes into the groom InstInfo row word 2. Shadow rays
  (rt_visibility) pass the strands with d_f * prod(a_f) (luminance), drawn per crossing in one non-opaque query
  (rt_groom_blocks); row word 0 keeps the old first-hit blocking. Needs rt_hair_groom_primary (the colour samples).
  GPU check: mean visibility 174623 vs d_f a_f^n 175064 over 3000 rays x 256 trials (1.3 sigma). Not run live.
- Identity: the runtime requests that include rt_common differ (109 of 262: the new strand-shadow code sits behind
  spec constant 1020 like the rest of the groom path); their RT_GROOM_SHADOW_T_REFERENCE=0 form (the text before C2)
  is byte-identical to the baseline on all 163 ordinary requests, and every runtime module passes spirv-val.
  water_surface_shader_test compares runtime water modules with the merge-base, so it fails on this branch by
  construction and passes once merged (its baseline is then the merge).
