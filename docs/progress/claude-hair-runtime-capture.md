## Runtime capture as the groom converter's source (branch claude/hair-runtime-capture, offline study, 2026-11-08)
- Why: granny2.dll cannot ship, so the release converter cannot read GR2. Design: docs/RTX_HAIR_ROADMAP.md 2c.
- Feasible: UV0 (soup +16), native triangle order and the index range, and the bound CHAR_Hair mask (plain 2D view)
  are all reachable; positions are posed (radius follows the pose's stretch), skeleton, names, LOD rows and
  catalogue categories are lost (index id, mask-image link and the seen order replace them).
- Offline equivalence (tools/hair_groom/capture.py, CTest hair_capture_test): with the index range, a rigid pose
  gives bit-identical strands, triangles and barycentrics, the same index id and UV samples; a 2% bend moves radii by
  1.5-6.6%; dedupe flips 3 of 125,678 strands on the largest style. Welding without indices breaks cards and the id.
- Stage times (12 cached styles, one thread, synthetic mask): tracing 4.6 s median, dedupe 0.7, validation 1.8,
  LODs + write 1.6; total 8.6 s median, 15.3 p90, 45 max. Minimal path without validation ~6.9 s typical.
- Recommendation: capture in game, convert in the launcher's existing child (C++ port needed for the release).
- Layer probe: `[hair capture]` mask format / size / mips / layout once per mask image (rt_native_hair only).
- READY_FOR_LIVE: tools/live_batches/hair_capture (hcap1-dialogue, Test Dialogue1), lint 0 failing, not queued.
