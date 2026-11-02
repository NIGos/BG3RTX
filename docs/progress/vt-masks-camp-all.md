## Offline VT alpha masks for every camp VT alpha shader (branch vt-masks-camp-all, offline, 2026-11-02)
- 16 more VT alpha shaders at camp (same recipe as the grass): 65 masks, 79.2 MB, ADDED to data/assetdb/vt_alpha
  (index/manifest `_camp2`); the layer now reads every vt_alpha_index*.txt. Two sub-tile textures (13,092
  triangles) cannot be extracted and keep the page cache.
- Agreement on the camp pack: alpha test 99.05% triangle-weighted (0.94% differ on coarser pages).
- Masked share: 99.4% of camp VT triangles; estimated ~97% of shadow and ~98% of visibility alpha candidates
  (was 18% / 37% with the grass only). va1/om3 expects accept any mask count.
