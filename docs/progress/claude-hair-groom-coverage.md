## Per-groom coverage table from the native converter (branch claude/hair-groom-coverage, offline, 2026-11-10)

- bg3rtx_hairconv --all over the whole catalogue (user's LSLib granny2.dll, not copied): 20 styles in 6 s, 1,217 rows
  in 406 s with --export-hcap; 898 pass, 282 needs-review, 37 reject.
- The converter's 3-view validation now writes `coverage_ratio` (strand / card composited opacity per view, the
  bound's own quantity) next to `coverage_delta`. hairconv tests pass.
- Every written groom (898) is inside the gate's bounds: U/C 0.907..1.075 against [0.8913, 1.0945]. The 72 rows
  outside are all unwritten (37 rejects, 44 needs-review: Watcher headwear 0.882-0.888, Minthara LOD0 0.881).
- tools/hair_groom/coverage_audit.py (texture-space test card on each real card and mask, cached, progress, 35 min for
  898): the old S/C count falls under the bound for 15% / 81% / 99% / 100% of grooms at 2 / 4 / 8 / 16 texels per
  pixel (cause A for every style). Its U/C overestimates the 3-D ratio (1.18 vs 0.99): relative S/C only.
- 3399 / 7086 (S8 cards every frame) are in no catalogue row; 1623 / 4077 are needs-review LOD1 beards; the user's
  other unmatched counts all have passing grooms (conversion on the PC pending). 2 mm radius cap: at most 0.17% of a
  groom's points, no visible effect. Details: docs/RTX_HAIR_PRIMARY.md 5u.
