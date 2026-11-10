## Why strands fail the coverage bound: the gate counted strand pixels inside the card markers only (branch claude/hair-coverage-cause, offline, not live-verified, 2026-11-10)

- Cause A, real: the count's S is the strand pixels whose card marker (the dithered, mip-filtered native alpha test)
  is set too. Far away a pixel's card alpha is the texel mean and the dither keeps it independently of the strands, so
  S/C falls with the footprint; the bound 0.891 was derived for strand over card opacity (every strand pixel, U/C).
  Fixture (primary.footprint_report, 0.5 to 16 texels per pixel): S/C 0.934 -> 0.585, U/C 0.965 -> 0.967 (flat).
  Distant keys therefore fail by measurement (5t loop 2), and frames with several of them trip the frame watchdog
  for every key (5t loop 1, the ~0.5 s period, also on the close character).
- Fix `rt_hair_coverage_union` (off, live; launcher Advanced "Strand hair: coverage check on every strand pixel"): the
  count runs rt/hair_primary_count.hlsl?HP_COUNT_UNION=1 and the lower bound, the latch and both watchdogs judge U;
  the upper bound keeps S. Relaxation only: U >= S per slot, so a key that passes today passes with it; missing
  strands still fail. Measurement change of the gate, no shading or reconstruction change.
- Cause B (draws/frames with ~0 strands): real as known mechanisms (5s two-row exchange, 5i shared key, 5h LOD drop);
  the user log's near-empty frames sit in groom LOD uploads and a refused pool growth. No speculative fix.
- Cause C (count noise): not independent; with U the per-frame spread is 3 to 4 times under the bound's sigma, fails
  0 down to 534 card pixels in the fixture.
- Lean line, always on, CPU only: `[hair primary] coverage by footprint` (S/C, judged, H/C from the existing shared
  trace counts, U/C, own-bound fails per card-pixel bin; dropouts frame-wide / nothing traced / rejected / two-row).
- Gates: build.ps1 -Test all pass; hair_groom_asset_test test_coverage_union, hair_groom_test
  test_coverage_footprint_metric; default count request byte-identical (shader_identity vs 2e1f28d0, 226/226), the
  new request compiles and passes spirv-val; variants.txt 131 -> 132 (+1). No ABI change.
- User session (all keys off, 1 min, still camera, characters far and near): A is confirmed if the small bins' S/C is
  >= 0.1 under the 4096+ bins while H/C stays within 0.05. Then rt_hair_coverage_union=1: judged flat, switches near 0.
- Details: docs/RTX_HAIR_PRIMARY.md 5u.
