## User decisions (branch main, decided, 2026-11-01)

- `rf_hit_shade` bias (S8 -7.5%, S6 +9%): option 2. It stays experimental until its bias is within the
  game's own spread. First the GI bounce at the hit (shared with gi_far) and the box-light fix (`ls_box=2`),
  then re-measure; the threshold is not lowered. The user can try it now: Experimental, Native hit
  materials (restart), then Shaded hidden-surface reflections, at S8 Water Queens House.
- `ls_k` stays off in every preset.
- Disk: keep what a test or comparison still reads; delete superseded packs and analysed raw dumps.
