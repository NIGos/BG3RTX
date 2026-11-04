## Strand shadow crossings: shared-joint dedup, RGB transmittance, fibre expectation (branch claude/hair-core-shadow, READY_FOR_LIVE, 2026-11-04)

- Review core1: chained segments merge into one crossing only when they share the joint (segment start table in
  InstInfo word 3, rt_groom_links); the last segment of a strand and the first of the next no longer merge.
- core3: rt_groom_blocks multiplies the RGB transmittance over the crossings and decides once on its luminance.
- core4: with rt_hair_groom_shadow_t and rt_hair_fibre, the fibre pass subtracts D-LS's expected strand transmission
  d_f lum(a_f^n) instead of 0 (exact for the pixel's own groom).
- Identity vs main f7e5ea44: 149/257 byte-identical, 107 equivalent at default (1026 = 0), the last failure was main's
  velocity change (identical after merging main); request delta 0. GPU shadow bench in hair_primary_gpu_test.
