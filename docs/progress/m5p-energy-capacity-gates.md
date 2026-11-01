## m5_promotion gates: energy prediction double count, conditional cascade gate (branch m5p-energy-capacity-gates, done, 2026-11-01)
- S8 energy "BRIGHTER than predicted" was the tool's prediction, not the image. The compose energy line's
  "mean delta over owned" is lum(C') - lum(C) of the composed pixels with every term of the variant
  (water_compose.hlsl d). With the sun shadows on it held their sun removal on the composed (glint) pixels:
  -0.137 of the -0.343 per owned pixel, which the shadows line counts again.
- tools/m5_promotion_gate.py now takes the reflection's own term, (A L - B) per composed pixel x composed
  share (-0.0014 per owned pixel at S8). Re-run on the 21:38 logs: S8 step-sun -11.17% vs -11.29%, step-ls
  -10.32% vs -11.27%, shot-on -10.78% vs -11.21% (tolerance 2.2%): all match. Flymm and camp unchanged (match).
- Cascade capacity gate: new expect_every option "when_present" (scripts/queue_gates.py): a window without
  the selected line passes (the pass did not run), present lines are judged as before. The canonical
  CAPACITY_GATES cascade entry has it, the capacity lint requires it, 19 jobs updated. Flymm printed 4
  casters lines during the load and none in a cost window (an interior has no sun cascade pass).
- Gates re-run on the existing logs with the updated jobs: m5p1 ok (13), m5p2 ok (10, was INVALID), m5p3 ok (11).
- Tests: m5_promotion_gate_test (sun-inclusive mean delta not counted twice), queue_gates_test (when_present
  absent/present, lint flags a cascade gate without it).

## Flymm "+828 non-finite": depth texels, not water; compose fail-closed (branch m5p-energy-capacity-gates, READY_FOR_LIVE, 2026-11-01)
- Per-dump counts of all 20 m5p2 traces: every RR colour input has 0 non-finite pixels, every colour image
  0. All the counted ones are in one 2048x2048 D32F depth image, dumped 9 times per frame, with
  3328..3527 infinite texels per frame with every key off (OFF-a alone: 29952..31743 totals). The gate's
  sum over every dump made frame-to-frame depth variation look like water NaN.
- tools/m5_promotion_gate.py counts colour images only (formats not D*); Flymm rows now flag nothing.
- Compose fail-closed anyway (shaders/rt/water_compose.hlsl, every variant): C' = max(Cu, 0) only where
  Cu is finite, else the game's C, counted in word 56 (common/water_compose.h kCsNonFinite, compose_writes);
  the base/sun/GSH variants reduce 57 words. Logged as "non-finite terms N" in the compose stability line.
  water_compose_test reproduces 0 x Inf, Inf radiance, a half-packed Inf and Inf - Inf. All 14 compose
  variants compile and validate; the other 2019 requests are byte-identical to main.
- Gate: expect_every "non-finite terms 0" in every compose-on window of m5w3..m5w6, m5p1..m5p3, su1
  (the old logs predate the word: the next run judges it). sw3-motion: cascade gate when_present.
