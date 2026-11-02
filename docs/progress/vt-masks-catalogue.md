## VT alpha: va1 result, game-wide catalogue, miss log, first-install design (branch vt-masks-catalogue, 2026-11-02)
- va1 (camp, VALID): masks alone are image- and cost-neutral (energy +0.24%, band 1.95%; rt passes and GI ms
  unchanged). Disproved: the coarser-page share did not drop (camp grass reads its resident mip 2).
- Catalogue (all paks): 1,677 alpha-tested VT materials, 125 templates, 1,174 textures; BC4 1,713 MB (levels 0..2),
  1,305 MB (level 0). Masked so far 65. data/assetdb/vt_alpha/vt_alpha_catalogue.json added.
- Key `rt_vt_alpha_miss_log` (live, off): ranked captures/vt_alpha_misses.tsv across sessions;
  vt_alpha_masks.py --misses extracts them.
- First-install generation design in docs/P4_OMM.md 11.2h (about 10 s per texture, 30-60 min for all in parallel,
  1.3-1.7 GB output, LSLib needs .NET 8).
