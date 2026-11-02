## OMM option B: fine-LOD key, mip-0 coverage, om3 job (branch omm-option-b, READY_FOR_LIVE, 2026-11-02)
- Key `rt_alpha_fine_lod` (live, off): masked VT records (rt_vt_alpha_masks) bind a mip-0-only view, so the RT
  alpha test reads mip 0 with the hard cut for every ray. No shader change (key-off SPIR-V untouched).
- Derived energy: grass alpha coverage 0.262 -> 0.295 (x1.125, darker); measured by omm_qual om3 (camp, 123 s).
- Micromap coverage at mip 0 (camp pack): level 3 34% known (19% opaque, 15% transparent), level 4 49%,
  level 5 62% of micro-triangles.
- Not done: the bake. Camp grass is 179 per-draw BLAS (0 pool meshes); plan in docs/P4_OMM.md 11.2c (about 2 days).
