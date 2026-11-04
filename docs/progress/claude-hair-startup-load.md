## Strand hair fail-safe for the first level load (branch claude/hair-startup-load, READY_FOR_LIVE, 2026-11-04)

- Strand hair fail-safe for the first level load (hair_crowd hc1 2026-11-04: device lost 10 frames after the first
  strand publish, rt_hair_groom_primary latched at startup). `hair_primary::LoadGuard` now opens its 300-frame quiet
  window on the first G-buffer image too (before: only on a change, so the first load had none). bvhsplit publishes
  a groom draw key only from the second update of its points range. Layer change (game restart). Cause of the fault
  not confirmed: the live check is hc1 again (no device lost; "level load (first or new G-buffer images)" logged once
  at the first publish; strand draws only after the window).
