## Far-GI screen-hit roof leak (branch roof-leak, offline fix, 2026-11-04)

- Confirmed: the screen-hit path accepted previous radiance without the hidden-hit roof segment test.
- Reuse `gi_far_roof_clear` for matched screen hits beyond the base GI radius. Blocked or unknown roof
  segments keep native ambient and stop before screen, emission, SHaRC or fallback radiance can supply the ray.
- No new key: existing `gi_far` specialization 1007 gates the correction. Near hits remain unchanged.
  ABI unchanged; `shaders/variants.txt` request delta 0; no preset changes.
- Offline regression: 2 tests, 12 segment fixtures pass; the original HEAD shader fails both tests.
  A 4 m canopy-crossing fixture with radiance 12 and radius 6 m changes bounce 6.666667 to 0;
  clear segments retain their original contribution. Covers rotated roofs, camera offset and unknown data.
- Full build passed; GI/roof/variant CTests 8/8 passed. One serial shader identity batch: 26/38 requests
  byte-identical; the 12 changed far-capable modules validate and all 12 match the baseline program with
  specialization 1007 frozen to 0 (other specializations preserved). No raw identity claim for those 12.
  Receipts: `build/roof-leak-{identity,off-proof,ctest,build}.log`, `build/roof-leak-off-proof.json`.
  Identity baseline cd534813d381; OFF baseline 3a60e2c0448a: shader/common trees are identical between them.
- No game launch. GPU cost and in-game image delta unmeasured; no live quality qualification claimed.
  Future check: S8 roofed interior with exterior ground on screen; inspect reduced leaked bounce and
  unchanged open-air bounce. This task does not authorize that live check.
- Kept as an uncommitted correctness fix for review. Git metadata untouched.
