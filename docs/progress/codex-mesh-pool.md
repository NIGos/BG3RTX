## Mesh pool shrink and cached BLAS evacuation (branch codex-mesh-pool, offline candidate, 2026-11-02)

- `rt_vram_compact` off by default and in all presets; Experimental GUI entry.
  Layer restart required for the DLL change. No ABI or shader changes; request delta 0.
- Pool repacks live vertices/basis when used <= reserved / 4 for 254 frames
  (240-frame result watchdog + 8 scene slots + 6-frame retirement), to start size
  or twice used. Supplied vg1 S8: 467 / 2047 MB predicts 934 MB, saving 1113 MB
  active reserve after retirement. Native hair and existing dynamic pool ranges
  defer repacking. New cached allocations/basis jobs pause during the GPU copy;
  uncached geometry still uses per-draw BLAS. Existing cached geometry is preserved.
- Protected completion word, generation checks, retained scene-specific material
  offsets, explicit fence/oldest-scene retirement, and recording-drop rollback.
  No timeout publishes an incomplete copy. Off cancels pending publication safely.
  Timed-out pool users block repacking until command reset; other commands'
  registered unsubmitted work also blocks copying ahead of old pool writers.
- Evacuates the emptiest eligible cached BLAS chunk into fuller existing chunks
  using CLONE and the existing confirmed swap path. No new chunk; source cannot
  refill, and evictions cannot recycle a pending copy source. Complete evacuation
  fits the existing per-frame compaction count. Dynamic/cascade/sun/OMM BLAS and
  diagnostic-pinned chunks are excluded. Exact BLAS savings depend on ownership.
- Existing `[mem] ours` and mesh-cache capacity lines report used/reserved,
  copy/retirement overlap, repacks, cached relocations and released chunk bytes.
- Validation: explicit C++ build passed; 94 / 94 permitted CTests passed (21.44 s).
  Final layer rebuild and 5 / 5 targeted CTests passed after the lifetime safeguards.
  CPU compaction policy/planner test: 68 checks, including copy/drop/stall and
  shared-record retirement.
  No game, live job, shader identity/compile/spec check or git mutation performed.
- Live status: unqualified. GPU ms, energy and visual invariance unmeasured.
  Separately authorized S8 port long-view test should show pool/chunk reservation
  decreasing after retirement with unchanged draw coverage and no Vulkan errors;
  repeat in the real Ultra preset, including turns and key toggles.
