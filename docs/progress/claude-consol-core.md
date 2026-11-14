## Consolidation, core and shaders: GI images no longer reallocated twice per level load; two dead shaders removed (branch claude/consol-core, offline verified, READY_FOR_LIVE, 2026-11-14)

- Evidence first: the play logs of the installed hairtest17 (`bg3rtx.log`, `bg3rtx.prev.log`) and the 12 newest queue logs.
- Fixed: every level load allocated the GI images twice. `rr_active` is true while RR evaluated in the last 3 presents; a
  loading screen is longer, so the first lit frame saw "inactive", the 13 GI images (about 180 MB at 2560x1067) were
  rebuilt at half resolution and again at full resolution on the next frame, and the history was cleared twice.
  12 of 12 queue sessions and both play sessions show the pair (`gi: resources ... (GI at 1280x534)` then `(GI at 2560x1067)`,
  60 to 150 ms apart) at startup and again at each load.
- Fix: `core/rr_mode_latch.h`. The GI keeps its RR mode while RR is inactive for less than 30 presents, counted from the
  first frame the GI sees it inactive. RR does denoise that first frame, so RR-mode input is the right one for it.
  A real fallback (SR, FSR, TAA) switches after the hold. `gi_rr` 0/1 still overrides at once. CTest `rr_mode_latch_test`.
- Image: steady state unchanged. The first frame after a load now gets the same GI as the frames after it (full
  resolution, `gi_rr_rays`), not half-resolution accumulation over an empty history. No shader change, no new key.
- Cost: CPU only. Saves 13 image allocations, 13 deferred frees and one history clear per level load. Not measurable offline.
- Live check (one observable, any save): after loading a save from the menu the log has no new `gi: resources` line
  (before: two). The startup pair stays (RR does not exist yet on the first frame).
- Removed: `shaders/probe.hlsl` (its pipeline was removed in 4dda0c72) and `shaders/rr_hair.hlsl` (rr_hair_resolve
  retired in 2d072e42). Nothing requests or includes them. `shaders/variants.txt` delta 0; merge-green compiles 2
  standalone files less. No other request changes, so no identity run was needed.

Looked at and found clean (no change):
- Pipelines: no rebuild without a cause. Per session: about 30 builds at startup (first frames, 16 to 190 ms bursts), one
  `_em` to plain hit-GI switch at a level load (104 ms present interval inside the load), 22 builds in 66 ms when the user
  pressed the RTX Hair hotkey (old pipelines kept meanwhile). Layout changes destroy their variants.
- Memory: every core image and buffer is allocated once or on a size change and freed through `defer_destroy`. No growth.
  All GI images are allocated whatever is on (about 180 MB on Ultra at 2560x1067); skipping some needs dummy bindings.
- Shaders: stores to history, previous radiance, hit distance and RR guides are guarded. Visibility rays end at the
  first hit; the groom trace is behind constant 1020 and the stamp's instance count; the origin pass skips sky.
- Small passes cost 0.02 to 0.04 ms each (temporal, composite, AO fold): nothing to gain there.

Not fixed (reasons):
- `gi_trace_rt.hlsl:746` maps a NaN to 65504 (`select(x <= max, x, max)`). Not reachable today (every term is guarded).
  A finite-only guard changes every hit-GI module: do it with the next change of that file.
- Hidden-hit emission beyond the roof bound is kept (`gi_trace_rt.hlsl:531`) while the screen branch drops it (:460).
  Both are commented as intended; a PT-reference question, not a bug fix.
- `refresh()` runs about 25 times per frame with about 15 `cfg_int` and 6 `persist` lookups each (mutex, hash, string
  copy). Estimated 0.1 ms CPU per frame, unmeasured. Needs a settings generation from the host (ABI) to cache safely.
- Dead branches kept because offline tools still define them: `RT_GI_FAR`, `RT_CASCADE_MASKS`, `RT_HIT_INSTANCED`.
  Defined nowhere: `RT_SCENE_REBASE`, `RT_REBASE_LIGHT_CAMERA`, `GI_DEBUG_SWIZZLE`, `GI_FAR_M` (removal is
  byte-identical by construction; left for a pass that already recompiles `rt_common.hlsli`).
