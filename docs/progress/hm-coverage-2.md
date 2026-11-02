## Hit-material coverage: alpha-tested pool meshes get pool rows (branch hm-coverage-2, READY_FOR_LIVE, 2026-11-02)
- Ranking (replay packs, `tools/hit_material_blockers.py`; live `hit material` lines of m4p 01:13-01:28):
  S6 021627: pool alpha without a row 39.0% (depth FS 84d5df19 Base_AlphaTest_VT 27.5%, 5afd54a9 11.5%), pool
  without a row 46.2% (inst 96/97, 30-37 m discs: FLUID_Lava_Plane_Flowmap bd5dbda2/0b8dfd73, animated flowmap with
  time, vertex colour and world position: a new recipe, not started). Camp: row reason 60% (soup alpha foliage,
  IP_Wind_GrassFlowers: per-draw rows like wind RGB). Inn: base 68%. S8: 68% qualified now.
- Cause of the S6 alpha share: Base_AlphaTest_VT STI uses the audited G-buffer pair 2d2ac3c3/14678345 (asset DB);
  its pool records were qualified (live m4p3 012015: 66 records) but only 21 had a UV/basis, because bvhsplit
  gave alpha pool meshes neither a basis range nor a row (rec holds the alpha record).
- Fix, live key `rt_hit_material_pool_alpha=0`: basis ranges and rows for alpha pool meshes too; the row slot + 1 in
  InstInfo meta bits 16..31 (`nh_pool_alpha_slot_shift`), rec keeps the alpha record. `rt_hit_material_pool` reads it.
- Offline: 95/95 CTests (pool test pins the meta encoding); key off, both packs bit-identical with the new SPIR-V (S8 142819 giRaw fnv1a a54c4dfd1cc9acb9, S6 021627 5cf77062bdef5c21, as before). The gain needs live rows (the packs hold none).
- Live job `tools/live_batches/m4_pool_alpha` (S6, 4.4 min, 0.2 GB), dry run passed. Predicted: qualified +23 points
  (10-39), shot-on +0.5%.
