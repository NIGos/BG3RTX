## One-ray Ultra analysis (branch gi1-analyze, rejected for promotion, 2026-11-04)

- Reanalyzed `gi1-ultra-1ray.1.174818.log` without launching the game. All 16
  every-window gates pass. No settings, shader requests, ABI or Git metadata changed.
- Fixed two analyzer adapters: keep full-log exposure probes when selecting each
  trace; use queue_gates' reset-aware complete timing periods for short cost windows.
  Float pre-post means already exist in `dumps.json` (5c3fdabc, slot 0).
  No f32 preview or job change is required. Absolute capture paths work;
  `--captures-root` also resolves the float data.
- GPU trace medians, OFF-a / ON-a / OFF-b / ON-b: 6.735 / 4.710 / 9.216 /
  4.480 ms. Mean OFF 7.9755 ms; savings 3.2655 / 3.4955 ms (prediction 2.30 ms).
- Three-sample float luminance means in the same order: 0.192885549 /
  0.168029129 / 0.171767416 / 0.189523346. All exposures 0.000296308;
  normalized means 650.963015 / 567.075910 / 579.692131 / 639.616029.
- ON energy deltas versus mean OFF: -7.841622% / +3.947240%. OFF repeat
  spread 11.582592%; existing Student-t band +/-255.301394%, dof 1.
  Both energy gates pass formally; the large band provides weak evidence.
- Named metric: scripts.qa.detectors.temporal, mean temporal std excess in PQ
  code plus excess area per mille. Static OFF null areas 195.600 / 158.123;
  floor 195.600. ON-a versus OFF-a / OFF-b: excess 0.000636 / -0.000343,
  area 163.034 / 170.116: passes floor rule. ON-b: excess 0.003194 /
  0.002215, area 289.221 / 246.318: FAILS both comparisons (PQ limit 0.000978).
- Overall predicted.json verdict: INVALID, not qualified; static acceptance
  also FAILS. First pan (off-a) fails rigid registration: fewer than four
  consecutive aligned frames. No valid registered pan metric can be reported.
  Partial evidence is retained in the analyzer JSON instead of discarded.
- A future qualification needs all eight pan frames in all four states to pass
  the existing integer-translation registration and cross-window support checks,
  with measured displacement, plus both static ON repeats passing flicker.
  Use a rigid scene/control and compatible motion; unchanged held rotation may
  fail again. Keep three float/stat traces per state and reset-aware timing.
  No missing energy capture requires a rerun. No rerun was prepared or launched.
- Validation: six analyzer unit tests pass (full-log exposures, relocated paths,
  missing-exposure rejection, reset timing and temporal acceptance); diff clean.
- Detailed receipt: queue/logs/gi1-ultra-1ray.1.174818.fixed.gi1.json.
