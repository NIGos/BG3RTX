## Cascade dialogue allocation fix (branch fix-cascade-dialogue2, READY_FOR_LIVE, 2026-11-03)
- No commit or git metadata writes. No game launch, save access or jobs queued.
- Previous fix did not fire: 0.0 mesh-offset duplicates in three saves;
  Dialogue1 unchanged at 335 seen / 60 duplicates / 113 added.
- Reproduced per-draw constants/material allocations, different skin-output
  regions and an unlisted camera VS. All three could defeat the old identity.
- Removed material allocation from geometry identity; retained exact vertex
  source/range, index range, proven model/placement and pulled-instance data.
- No stable skinning-source lineage exists in capture tracking; different
  output regions cannot safely match. Distinct placed props remain distinct.
- Use existing rt_cascade_static_only=1 fallback: audited skin/wind, storage
  outputs and unknown paths are excluded; camera-pass current pose remains.
  Default stays 0; unknown off-camera static roofs can also be omitted.
- Reject opposite-policy F-1/F-2 soups after toggles; no stale unfiltered copy.
- Added [cascade] cascade casters with key 0 per frame before admission filters.
- ds1-ds3 remove every-window camera-log gate; analysis requires zero integer
  registration against both OFF neighbours and within/across motion controls.
- Prepared static_only 0/1 shots, fix-off/fix-on motion pairs and restoration.
- Predictions: port character motion <=0.002 block luminance; nocascade
  character gap falls to camp level. No live quality or GPU cost claim.
- Build passed (40 s); 119/119 CTests passed; QA detector regressions passed.
- Batch dryrun passed: 4 synthetic jobs, 29.0 min / 1990 MB; nothing queued.
- ABI unchanged; shader edits 0; shaders/variants.txt request delta 0.
