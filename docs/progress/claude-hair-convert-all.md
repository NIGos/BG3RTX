## Convert all hair styles from the player's own files (branch claude/hair-convert-all, offline, 2026-11-09)
- `bg3rtx_hairconv --all --game <BG3> --granny <granny2.dll> --out <dir> --report-dir <dir>`: native LSPK v18 / LSF / LSX
  reader (LZ4 only; zstd refused, none in BG3's paks), the catalogue (1,217 hair and beard rows, cached by package
  fingerprint), reach categories and prepare tiers, Pillow-exact DXT5 masks, GR2 via the player's granny2.dll used only
  for section decompression (stock Granny 2.11 cannot open BG3's GR2 itself). No Python, PowerShell 7 or LSLib.
  Without the DLL: exit 3 and preparation.json state "unavailable" (capture model stays). Roadmap 2f has CLI and fields.
- Equivalence vs the 2026-11-07 Python batch, 28 styles: catalogue 1,217/1,217 rows, GR2 dumps 28/28 byte-identical,
  masks 28/28 identical, index id and verdict 28/28. Strands differ by median 0.4% (max 1.2%) from that run because
  strands2d.trace uses numpy's non-stable argsort on tied distances; with a stable argsort the Python reference equals
  the native records exactly on 28/28. Fix proposal for the reference: kind="stable" (not done).
- Full catalogue on this PC (4 jobs x 4 threads): 1,217 rows in 532 s; 898 pass, 282 review, 37 reject; 2.72 GB of v3
  grooms (scratch, deleted). Against the 737 batch rows: 736 same verdicts (one review-threshold flip from the tie order).
- CTest hairconv_all_test (synthetic pak, LSF frames, patch override, catalogue, synthetic GR2 without the DLL,
  granny-less path). LSLib MIT text still to collect before distribution (THIRD_PARTY.md).
