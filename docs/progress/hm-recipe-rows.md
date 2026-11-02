## Generated hit-material rows from the asset DB (branch hm-recipe-rows, READY_FOR_LIVE, 2026-11-02)
- New `tools/material_families/recipe_rows.py` -> `common/native_hit_generated.h`: rows only where the asset DB (offsets,
  VT bindings, depth VS), the FS classifier (identical / fields only / reflectance moved / constant 0.5 / flag dropped)
  and the VS capture path (pool 2d2ac3c3, or a bit-exact instanced family by depth VS + gl_Position slice) agree.
  8 of 830 pairs: IP_AlphaTest(_2S), wind bushes PBR translucency, wind RGB 2S plain/GM, BASE_AlphaTest_FX,
  GM_Pulse_AlphaTest (constant), Base_GM/BGM and HLOD_GM (pool, = 3df678c0). TRACK_D_REFLECTIONS 14 "Generated rows".
- Key `rt_hit_material_generated=0` (startup VS frame patch, then live). Key off: audited lookups unchanged
  (native_hit_generated_test), 2961/2961 shader requests byte-identical (no shader change), 100/100 CTests.
- Offline share: packs cannot replay new rows (captured live). Camp pack 013545-2 (instanced off) blocks 20.2% of
  lookups on the bushes' depth FS and 5.4% on IP_AlphaTest's; the inn pack predates pool alpha (1.0% replayed).
- Live job ready, not queued: `tools/live_batches/m4_generated` (camp + inn, 164 s each, ~10 min, 0.4 GB), predicted
  camp +12 points (3-25), inn +1.5 (0.3-4), energy null, camp GI trace +1.0 ms (0.2-2.5). Dryrun static checks pass
  (its fake-queue runs fail as on main).
- Also on this branch (from the audit branch): `tools/assets/locate.py`, no hard-coded LSLib or game path in the VT
  mask extractor (BG3RTX_LSLIB / external/lslib*, BG3_DATA / Steam libraries); vt-masks-catalogue still hard-codes it
  until it merges with this.
