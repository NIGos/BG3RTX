## Strand motion vectors: camera-only on most strand pixels, rt_hair_mv_card built (branch claude/hair-mv, offline, key off, 2026-11-10)

- User: strand hair at isometric distance trails / blurs when characters or the camera move.
- Measured on the hs1-s8-static-hair bursts already on disk (static camera, idle party), with the new
  `tools/hair_groom/mv_ratio.py`:
  - camera-only motion vectors (within 0.02 px of the static background): 73-86% of strand hair pixels, 5-7% with cards;
  - hair / 6 px ring median |mv|: strands 0.05-0.23, cards 0.52-1.08.
  Most strand pixels read 1e-4 px while the head next to them moves 0.1-0.4 px, so RR takes their history from
  where the hair was: a trail. Depth and fibre normal also change strand to strand (about half the pixels per frame).
- Two code paths give camera-only motion (docs/RTX_HAIR_PRIMARY.md 5x):
  - (a) the groom row has no previous points (`hair_primary_velocity.hlsl` fallback; row.w 0 unless
    `prev_ok && prev_frames > 1`, `layer/bvhsplit.cpp` hp_rows) or the header has no previous camera;
  - (b) the velocity variant discards and the game's full-screen camera value stays.
  The logs favour (b): one publish per frame, "recovered 0". The bursts cannot separate (a) from (b).
- Built, off by default: `rt_hair_mv_card` (dev key, pinned 0 in data/release.ini). Specialization 1049 in the velocity
  variant (when the key is non-zero at pipeline creation), live header flags. 1 = the card's own skinned motion only where
  the row lacks previous points; 2 = for every strand pixel. Strand points are barycentric on their card triangles.
- Counter line (detailed logging): `[hair groom] strand motion (last N frames)`: keyed rows with and without previous
  points (first update, two updates in one build frame, skipped build frames), and publishes without the previous camera
  (not one frame back, GPU slot). `hair_primary::prev_row_state` is unit-tested in hair_groom_asset_test.
- Key off: 225/227 shader requests byte-identical to main b29a9d3b. The velocity shader (file and manifest entry) is
  equivalent with 1049 frozen to 0, and spirv-val passes. Variants delta 0. CPU cost: one counter per groom row.
- Staged, not queued: `tools/live_batches_staged/hair_mv` (hm1-s8-hair-mv, one S8 launch, about 76 s of phases; predicted.json
  first; live_flow --frozen 0 failing; lint snapshot extended). It logs the counter line at keys 0, 1 and 2 and takes two
  RR-input bursts (keys 0 and 2), analysed with mv_ratio.py and temporal_burst.py. Disk: about 9.8 GB transient (2 x 4.9 GB,
  no smaller RR-input capture exists), deleted after analysis.
- Decision rule: key 2 brings the camera-only share near the cards' value -> qualify it with the energy check. If it stays
  high, the cause is (b), and the next step is a velocity discard counter.
- Status: READY_FOR_LIVE (needs the user's go). Cost of the fix: no extra trace; the card path skips two point loads.
- Also on this branch: `docs/progress/claude-hair-pixel-lod-live1.md` (the pixel LOD live analysis and the trail section).
