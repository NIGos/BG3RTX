## sh1 SHaRC + gi_rr_rays, lean regression on the new Ultra (branch rays-flicker-rerun, analysis + sh2 job, 2026-11-02)
- SHaRC on Ultra + gi_far (sh1 043322, S8): **fail**. Scene colour -0.86% against the committed +1.5% (band 1.47%):
  darker, not brighter (the carried-over prediction is disproved: cache hits now override qualified hidden hits,
  whose shading the cache's screen radiance undercuts). Cache found 23.8% of the queried non-screen hits; leak test
  10.1% of cache hits from occluded cells (5.4% of its contribution, rejected). Cost: GI trace +0.49 ms, reflections
  +0.17 ms, resolve 0.03 + inject 0.06 ms: about +0.75 ms. Stays off.
- gi_rr_rays 1 vs 2 (same run): energy +0.11% (band 1.47%, predicted null): pass. GI trace 4.32 -> 2.02 ms (-2.30,
  predicted -2.6), whole GI -2.04 ms, frame time 15.8 -> 12.9 ms. Flicker (rays_flicker.py, 3 traces per state):
  void, the OFF-b vs OFF-a null failed (excess regions 3.4 / 21 per mille in its two directions; 1 ray vs OFF-a / OFF-b
  9.3 / 31.7 per mille in the same places); mean temporal std 1 ray 0.00456 between the OFFs' 0.00426 and 0.00471.
  Test fixed, not the theory: job sh2-s8-fg2 (rays only, 6 traces per state, 132 s, about 1.1 GB), predicted.json
  written before it. Not into Ultra until its null passes and the 1-ray set shows no excess.
- Lean regression on main aeb248b (new Ultra) vs the 02:27 runs: no correctness regression. Primary match camp 94.05%
  (94.05), S6 99.86% (99.77), inn 84.8% (73.8, includes the rotations); scene wait 0 timeouts everywhere (inn had 157 of
  600); capacity: inn opaque dropped 0.02/frame (0.01 before: pre-existing, fails the 0 gate in both); ls_check any-bit
  30.5M px (26.9M). Cost is the new Ultra: GI trace camp 1.22 -> 7.65 ms (frame 16.7 -> 22.0), inn 0.45 -> 5.05,
  S6 0.32 -> 3.56 (frame 9.9 -> 12.7), from the hidden-surface bundle and gi_far's 2-ray 6 m pass.
- Deleted the 53 analysed trace folders of today's gf/bx/ef/sh1/lr runs (2.2 GB); packs kept.
