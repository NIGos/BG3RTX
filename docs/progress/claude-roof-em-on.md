## gi_far_roof_emission on by default (branch claude/roof-em-on, applied 2026-11-21, confirm or revert)

- `gi_far_roof_emission=1` (specialization 1054) after the PT A/B on the inn emission pack (claude-em-pack-result-2):
  1128 px changed, emission |E - PT| 0.406 -> 0.039, 55/55 tiles closer, 0 raised pixels, occlusion unchanged.
- Live r2-inn-roof-em: energy +0.25% (within noise), GI GPU ms equal. No new shader request (a specialization value).
- settings_test lists it with the applied-overnight keys; build.ps1 -Test passes. Check in game: the inn's interior with
  the roof cut away should lose no light except bright speckles from street emitters behind the roof.
