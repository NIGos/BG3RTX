## Native strand groom converter bg3rtx_hairconv (branch claude/hair-converter-native, offline, 2026-11-08)
- C++ port of the Python converter (tracing, 2-D tracer, dedupe, analysis, validation renderer, verdicts, v3 writer):
  tools/hairconv, `bin\bg3rtx_hairconv.exe` (static CRT). Input .hcap (tools/hair_groom/hcap.py), output .hgrm v3.
- Equivalence: 24 cached styles (synthetic masks, 2 curly) identical to the Python reference: verdicts, IoU /
  coverage / radiance, strand counts, and decoded grooms bit-identical. CTest hairconv_test on a synthetic fixture.
- Speed: see docs/RTX_HAIR_ROADMAP.md 2d (C++ 4 threads: 0.51 s median, 1.78 s max per style).
- Launcher Preparation runs bg3rtx_hairconv over data/assetdb/hair_capture (no Python); Convert all removed (only
  captured styles exist). Release ships the exe; verify_rc requires and allows it.
- 2c updated with the hcap1-dialogue live result: BC3 masks, 128x128 placeholders (capture waits for full mips),
  every hair draw indexed, unmatched draws are all "count unknown", card stretch stat added to the capture spec.
- Left: the layer capture (.hcap writer), groom re-index at a level load, one live capture -> strands run.
