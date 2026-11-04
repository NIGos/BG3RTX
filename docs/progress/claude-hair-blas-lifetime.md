## Fault dump lists the cascade and groom BLAS as live; strand pass bisect key (branch claude/hair-blas-lifetime, READY_FOR_LIVE, 2026-11-04)

- hv6 07:30 lost the device 2.5 s after the first real strand swaps. The dump's "#552 (RELEASED range)" was the current
  cascade casters BLAS (the last instance with rt_cascade_casters): the dump's live set left out g_casc_as and the groom
  BLAS. Fixed; BLAS lifetimes were not at fault. The invalid read (0x25be68a000) is in no buffer of ours: unresolved.
- rt_hair_primary_passes (Developer, live, default 7: 1 G-buffer, 2 velocity, 4 prepass clone) bisects which strand
  pass faults once draws swap. Live check: hv6 order-gbuffer windows with 1, then 3, then 7.
