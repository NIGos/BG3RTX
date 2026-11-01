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
- Left: Flymm step-compose and step-sun flag NONFINITE 32571 > OFF 31743 (info, the verdict still matches).
