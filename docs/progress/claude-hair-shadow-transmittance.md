## Strand shadow transmittance, C2 (branch claude/hair-shadow-transmittance, 2026-11-03)
- C2a, GPU check (tools/hair_shadow_gpu_check.hlsl, CTest hair_primary_gpu_test): strand crossings per ray on the
  layer's LSS geometry vs the CPU count. Chained joints are reported by both segments: raw candidates and successive
  closest hits over-count 35% of rays (2.33 / 2.29 vs 1.90 per ray). hp_crossing (adjacent segments of a strand once)
  matches the CPU on 100% of 3000 rays with one non-opaque query.
- Fix: gi/hair_fibre.hlsl hf_strands (dual scattering n) now uses that one query: the over-count made the fibre-lit
  hair too dark (a_f^n with n too large); one trace instead of up to 8. Key-on only (rt_hair_fibre): identity 258/262.
  Live: expect hair under rt_hair_fibre slightly brighter (energy check pending).
