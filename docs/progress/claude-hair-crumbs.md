## Strand probe gate and breadcrumbs (branch claude/hair-crumbs, READY_FOR_LIVE, 2026-11-04)

- hv7 08:10: device lost about 2 s after order 1 with rt_hair_primary_passes 0: the coverage probe (the masked strand
  G-buffer variant, now hitting) was still drawn. Bit 8 of rt_hair_primary_passes gates the probe (default 15).
- Breadcrumbs: the strand G-buffer variant checks its explicit buffer accesses (pixel entry, points after hp_segment,
  point UVs) against their ranges, records a violation (site, address) in a host-visible buffer of the layer and skips
  the access; it also notes the last TLAS, pool and pixel addresses. Logged every 300 frames and after a device loss
  ("hair primary breadcrumbs: ..."). No violation with a device loss: the fault is in the TLAS traversal or a
  descriptor access, not in an explicit read.
- The access violation after hv7's device loss is the game's (bg3.exe write at 0); the layer frame is our exception
  logger (streamline.cpp veh).
