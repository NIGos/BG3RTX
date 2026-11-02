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

## m4_generated live result: correct, small gain, stays off (branch hm-recipe-rows, measured, 2026-11-02)
- hgn1 camp 145058 (INVALID only for the share gate's window length) / hgn2 inn 145500 (VALID), GPU ms only (two
  identity runs compiled during the batch). Qualified share OFF -> ON: camp 26.9% -> 29.2% (+2.3 points; predicted +12,
  range 3-25: below), inn 24.97% -> 25.09% (+0.12; predicted 1.5, range 0.3-4: below). Clip: every generated 2S row
  bit-exact (866k-1.49M checked, 0 rejected). Energy: camp -0.12%, inn -0.10% (bands 1.85% / 1.23%): null as predicted.
  GI trace: camp +0.58 ms (predicted 1.0, 0.2-2.5), inn +0.03 ms. Capacity: overflow 0 everywhere.
- Why below: camp's bulk was the wind bushes (20.2% of pack lookups): 9 draws captured, 0 mapped (pairing not logged:
  the IP grass family lines printed only with rt_hit_material_ip_grass; now also with the generated key). The 2S family
  gained 42 mapped rows (IP_AlphaTest) but 83 more G-buffer draws were captured and never mapped (the opaque IP_Base_VT
  risk, confirmed): deferred stream 57 -> 93 MB, no overflow. Inn: pool rows 651863a6 9/7 and b0ecc292 6/3 records/ready.
- Verdict: not into Ultra (+2.3 points for +0.58 ms at camp, no measurable image change); keep off until the bushes pair.
- Job fix: at camp's ~8.5 fps with rt_stats, 120 frames = 14 s, so a 20-25 s diag window never held two stats lines.
  The plans now have 5 s gap-* phases (rt_stats=0 resets the core's 120-frame window) before diag-on and diag-off-b,
  and the share and clip gates are events (settle 1 s). Dryrun static checks pass; 13 phases, 9.8 min.
