## Strand motion vectors: the velocity variant does not write most strand pixels; rt_hair_mv_card and pan_mvec built (branch claude/hair-mv, offline, key off, 2026-11-10)

- User: strand hair at isometric distance trails / blurs when characters or the camera move.
- Measured on the hs1-s8-static-hair bursts on disk (static camera, idle party) with the new
  `tools/hair_groom/mv_ratio.py`:
  - camera-only motion vectors (within 0.02 px of the static background): 73-86% of strand hair pixels, 5-7% with cards;
  - hair / 6 px ring median |mv|: strands 0.05-0.23, cards 0.52-1.08;
  - per head: strands 0-12% of the pixels moved (next to heads moving 0.1-0.7 px), cards 86-100%.
  RR then takes the history from where the hair was: a trail.
- Cause, narrowed offline: the strand path itself never writes these pixels. 95% of them read 1e-4-3e-4 px, the game's
  camera pass value. The strand formula, even without previous points, gives the axis point's offset from the pixel
  centre instead (up to a strand radius), so the previous-points fallback (row.w 0) is not it.
- Left open: one of the velocity discards (seal/tag, accepted marker slot, segment bounds) or the depth test (an
  unswapped draw, or the depth changed after the G-buffer). It reads the G-buffer's record on the same card triangles,
  so neither re-trace nor coverage can explain whole heads. The full list is in docs/RTX_HAIR_PRIMARY.md 5y.
- Built, off by default: `rt_hair_mv_card` (dev key, pinned 0 in data/release.ini), specialization 1049 in the velocity
  variant, with live header flags:
  - 1 = the card's skinned motion where the row lacks previous points;
  - 2 = the card's motion for every fragment of a swapped draw, before any strand check, with the sealed strand depth.
  Mode 2 bypasses every discard, so the live separates "discard" (mode 2 fixes it) from "depth test or swap" (it does not).
- Counter line: `[hair groom] strand motion` (rows with and without previous points and why; publishes without the
  previous camera). `hair_primary::prev_row_state` is unit-tested.
- `pan_mvec=1`: K8 edge bursts keep RR's motion vectors (half-size, every frame; EdgePanSamples and pan_epoch_test).
  A burst is now about 0.1 GB instead of 4.9 GB.
- Key off: 225/227 shader requests byte-identical to main b29a9d3b. The velocity shader is equivalent with 1049 frozen to
  0, and spirv-val passes. Variants delta 0.
- Staged, not queued: `tools/live_batches_staged/hair_mv` (hm1-s8-hair-mv: counter line at 0/1/2, four K8 edge bursts:
  strands 0/1/2 and cards, about 0.4 GB; predicted.json first; live_flow --frozen 0 failing).
- Status: READY_FOR_LIVE (needs the user's go). Cost: no extra trace; mode 2 skips the strand loads.
- Also on this branch: `docs/progress/claude-hair-pixel-lod-live1.md` (the pixel LOD live analysis).
