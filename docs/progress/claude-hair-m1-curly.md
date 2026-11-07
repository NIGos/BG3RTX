## Hair groom: 2-D curve tracer for curly atlases (branch claude/hair-m1-curly, READY_FOR_RERUN, 2026-11-07)
- New `tools/hair_groom/curves2d.py`: follows each painted strand along its own direction (profile across the
  strand, recentre on the run). Crossings: keep direction and curvature, follow the strand's R (ID) pixels; a
  duplicate along an already traced strand stops after two widths, as does coasting without own pixels.
  Strands are ordered root to tip by B; points keep the centreline within a quarter width, at most 16 px apart.
- `convert.convert(tracer="auto")` uses it only when the mask's root/row correlation (median over tiles) is below
  0.3, the batch's old reject threshold. The width is taken along the strand normal through the full UV Jacobian.
  The trace is memoised per mask; each card only gets the samples inside its UV box.
- The batch root check for these atlases uses the same correlation along the traced strands (B vs arc length,
  length-weighted median per tile), same 0.3 threshold. Versions: converter 2, batch 5. No gate changed.
- Representative set + all 42 HAIR_Afro_B styles (119 styles, 4 jobs; reports in .scratch/hair-c/base and new):
  50 / 60 / 9 -> 69 / 47 / 3 (pass / review / reject). Sample (80): 50 / 23 / 7 -> 54 / 23 / 3.
  Afro_B: 0 / 38 / 4 -> 17 / 25 / 0 (Afro_Short_B LOD0 IoU 0.95, coverage +0.01). Bugbear beard
  (corr 0.24): reject -> pass.
- Bit identity: the 75 non-curly styles give identical grooms, verdicts and IoU (cached npz compared array by array).
- Cost: 520 s vs 418 s for the 119 styles. The trace takes about 2 min per curly mask per worker process, once.
  Other styles cost the same.
- Review classes in the representative set (the 1217-style report was lost with the reinstall): silhouette IoU on
  sparse stubble (GithyankiCut_Short_D, short beards: coverage within 0.01, noise of the thresholded IoU),
  radiance (beards, mixed signs), helmet and braid cards with roots on the rim. These are gate or reference
  effects, not converter defects. The root channel class was the converter defect, fixed here. Seam stitching and
  tip taper have no failing check on the set: not done.
- Left on Afro_B: B helmets at IoU 0.79-0.85 (the same before), and 17-40 hat-hair cards rooted on the rim.
- Tests: `tests/hair_groom_test.py` test_curves2d (synthetic curl with self-crossing and a crossing strand; radius
  in every direction; auto == rows bit-identical on a row atlas).
- Next: one full rerun (~1 h, 4 jobs) when the other converter or reference changes are in.
