## Ultra scene build audit (branch scene-ana, analysis complete, 2026-11-05)

- Added the measured split and ranked conditional candidates to TRACK_D_SCENE_BUILD.
- Static Ultra receipts: S8 convert/BLAS/TLAS 0.595/0.375/0.203 ms, total 1.175;
  inn 1.219/0.773/0.324 ms, total 2.504. Component medians are not additive.
- Static/dynamic/cascade/hair BLAS GPU times cannot be separated by existing logs.
- Original Ultra already packed through hair trim; High on the same wide-stride
  device had disabled it. This explains why independent packing helped High.
- Static cache, held BLAS reuse, dynamic refits and transform-only TLAS updates
  already exist. Cascade immutability remains unproved and reuse stays rejected.
- No candidate has the requested existing-fixture proof; stopped at analysis.
  No implementation, raw key or specialization allocated; shader request delta 0.
- Read-only source/log review; no GPU, build, game, queue or Git mutation.
  Documentation checked with git diff --check; no runtime qualification claimed.
