## VRAM motion iteration 4 (branch codex-vram-motion, READY_FOR_LIVE, 2026-11-02)

- vg2 reserved/live/pending 4077.7/867.3/588.3 MiB leaves 2622.1 MiB
  free inside pinned chunks. Existing suballocation and retirement already work;
  mixing persistent cached meshes with rebuild allocations fragments reuse.
- With rt_vram_compact on, cached meshes, per-draw BLAS and cascade/sun BLAS
  use separate chunk arenas. Cached relocation respects those arenas. Existing
  mixed chunks drain normally. No size, fence or in-place BUILD change.
- CPU motion reproduction: reserve 1344 -> 448 MiB for identical occupied
  354.8 MiB and live 24 MiB. This demonstrates the mechanism, not a game result.
- Required RT capture checks full replacement plus outstanding grants against
  the real driver budget, bypassing LOW hysteresis and simulated/cache shares.
  Retains layout limits and refused-attempt count/peak logs.
- vg3: one S8 low-view launch, identical right/left OFF-a / ON / OFF-b turns,
  real budget, guard+compact+elastic toggled together. Mandatory post gate checks
  direction-wide mean/peak drops against OFF-OFF spread plus rounding, static
  float energy within +/-0.92%, and ON memory snapshots with consumer accounting.
- Offline: host DLL build, 8 targeted CTests and vg3 fake-queue dryrun pass.
  Allocator/guard/compaction report 438/149/99 CPU checks. No shader tools or game
  launched; git metadata unchanged. Keys remain off by default; ABI and shader
  request delta unchanged (0). Runtime reserve, motion quality and GPU ms remain
  unmeasured; vg3 is prepared outside watched inboxes, not queued or authorized.
