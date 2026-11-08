## Strand groom storage, needed set and launcher Preparation card (branch claude/hair-groom-storage, offline study, 2026-11-08)
- Design: docs/RTX_HAIR_ROADMAP.md section 2b. Prototypes: tools/hair_groom/storage.py (scan, HGRZ codec, error
  study), tools/hair_groom/prepare.py (needed-first order, launcher status JSON), CTest hair_storage_test.
  No layer, format or game key change.
- Size: 871 files, 20.97 GB: barycentrics 39%, triangle 19%, radius 19%, segment list 17%, root 5%. Thinned LODs
  (keep 1/2, 1/4) are 43% and bit-exact `convert.thin(LOD 0)`; the segment list is derivable from strand lengths.
  7.1 GB (312 files) are not written by the last batch (older, wider runs). The batch cache adds 16.5 GB.
- HGRZ (LOD 0 only, strand lengths, deltas, zstd 19), 24-file sample: lossless 6.1x; per-file barycentric bits
  (16-20, chosen from the card mesh) 11.1x with max position error 0.35 um under the derived 0.38 um (one 8-bit
  level at a 4K 0.6 m close-up); 16-bit 11.5x but 1.2 um p99. zstd on the raw file: 2.2x. ~20.97 GB -> ~1.9 GB;
  last batch 13.9 -> ~1.25 GB. numpy decode 133 ms per M points (median style ~70 ms).
- Needed set: recommended companions first, then reachable styles by tier in the background, plus a runtime
  "seen" list from the layer's existing miss path. Index map from the GR2 decode alone: 1212 meshes, 1180 ids,
  1.1 s, 86 KB.
- Launcher: read-only Preparation card on Help & support (`state.preparation`), sample preview and renderer
  test; the bridge does not send it yet.
- User decisions open: encoding (bits), needed-set strategy, Convert all opt-in, when the launcher converts,
  shader seed vs prewarm, dropping the batch cache.
