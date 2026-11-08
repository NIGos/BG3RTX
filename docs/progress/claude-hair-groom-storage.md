## Strand groom storage, needed set and launcher Preparation card (branch claude/hair-groom-storage, offline study, 2026-11-08)
- Design: docs/RTX_HAIR_ROADMAP.md section 2b. Prototypes: tools/hair_groom/storage.py (scan, HGRZ codec, error
  study), tools/hair_groom/prepare.py (needed-first order, launcher status JSON), CTest hair_storage_test.
- Size: 871 files, 20.97 GB: barycentrics 39%, triangle 19%, radius 19%, segment list 17%, root 5%. Thinned LODs
  (keep 1/2, 1/4) are 43% and bit-exact `convert.thin(LOD 0)`; the segment list is derivable from strand lengths.
  7.1 GB (312 files) are not written by the last batch (older, wider runs). The batch cache adds 16.5 GB.
- HGRZ (LOD 0 only, strand lengths, deltas, zstd 19), 24-file sample: lossless 6.1x; per-file barycentric bits
  (16-20, chosen from the card mesh) 11.1x with max position error 0.35 um under the derived 0.38 um (one 8-bit
  level at a 4K 0.6 m close-up); 16-bit 11.5x but 1.2 um p99. zstd on the raw file: 2.2x.

## Grooms v3, seen list, launcher Preparation (branch claude/hair-groom-storage, implemented offline, 2026-11-08)
- User decisions applied: per-file bits (format v3), needed first with Convert all off, the player picks when to
  convert (default: launcher open and game closed), compile shaders before Play, batch cache deleted per style.
- Format v3: common/hair_groom_v3.h (layer + launcher read v1-v3), writer asset.write (v3 only). Codec LZMS through the
  Windows Compression API: no zstd source on this PC and vendoring needs a download the user must approve.
- 5-style dry run from cached grooms (paks on D:\ not read): 176.7 MB v2 -> 16.9 MB v3 (10.4x), max position error
  0.358 um. C++ decode equals the Python reference (CTest hair_groom_v3_test + --dir); all 3 LODs 98-110 ms per
  0.35-0.40 M-point style, 757 ms for the largest (3.3 M points), LZMS 80%; the layer decodes one LOD per upload.
- Layer: hair_seen.txt next to the assetdb (`note_seen` on the existing miss paths, `flush_seen` at a level load and
  device destroy). No shader or request change (shaders/precompile.txt is a list, not HLSL); export
  `bg3rtx_precompile` in layer.def.
- Batch: --seen / --all / --keep-cache / --no-reach, tiers from reach.json, index ids per row, index_map.json,
  preparation.json every 30 s, cache deletion.
- Launcher: tools/launcher/preparation.{h,cpp}; first card on Help & support with live lines, Pause/Resume, Convert all,
  When to convert; children in kill-on-close job objects at BelowNormal; settings in bg3rtx_launcher.ini.
  `BG3RTX.exe --precompile-shaders` compiled 2 test requests in 1.2 s, then 0 in 0.07 s (cached).
- Tests: hair_groom_v3_test, launcher_preparation_test, web bridge prep checks, renderer prep checks,
  hair_storage_test (v3 fixture, seen list, categories), hair_groom_test (v3 asset, cache deletion).
- Left before a release: the release has no Python, converter, LSLib or catalogue (the card says "Not available");
  the full conversion on the user's PC; a live check of the upload decode cost and of hair_seen.txt.
