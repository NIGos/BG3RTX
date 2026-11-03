## Hair batch (branch claude/hair-batch-lod, 2026-11-03, after bundle8)
- Game use (tools.hair_groom.reach): of the 698 unconverted styles 130 are unreachable, 568 reachable (150 LOD1 of a
  passed LOD0). Fixes: zero radii from collapsed card triangles, mirrored UVs reviewed against the coverage bound;
  the 200 affected reachable styles rerun: all pass. Now 719 pass, 372 review, 126 reject (368 reachable still
  unconverted). Table and per-class causes in docs/RTX_HAIR_PRIMARY.md 5a.
- Companions (2026-11-03): every companion head hair passes; Halsin passes after the orientation test counts only
  radial strands (26 styles more). Gale's beard and the short beards stay in review (systematic, see RTX_HAIR_PRIMARY
  5a). 745 pass, 346 review, 126 reject.
