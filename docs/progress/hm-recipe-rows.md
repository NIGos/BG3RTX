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

## Two-sided pool rows (RT_HIT_POOL_2S) and RT2 matching (branch hm-recipe-rows, READY_FOR_LIVE, 2026-11-02)
- Pool consumer (shaders/rt/native_hit_material.hlsli rt_hit_material_pool): with RT_HIT_POOL_2S + RT_HIT_INSTANCED, rows
  with h2.w = 5 flip the N column by nv_two_sided_sign (world triangle, interpolated N, ray), as the per-draw 2S rows.
  Every other consumer rejects h2.w 5 (fail closed, reason row). Variant bit kHitPool2s (common/shader_variant.h): only
  with instanced + wind RGB and without IP grass / OMM / tileset / scalp / hair origin: +45 requests (variants.txt 2922).
  The layer publishes `native-hit-pool-2s-v1` when the generated key starts on and a two-sided pool row exists.
- Generator: two-sided pool rows allowed; an RT2-only difference is accepted when the FS's RT2 equals an audited recipe's
  (UBO members erased, temporaries renamed, the RT2.w class/flag word folded) and takes its reflectance mode. 11 rows:
  + Base_2S_VT 2d815e2a and Base_AlphaTest_2s_Translucency 80d6fb8f (pool, two-sided), + ATM_Wind_Vertex_R_AlphaTest
  0289a872/55592de4 (wind RGB family). HLOD_TwoSided 3b346b42 (the inn's 19 draws) stays rejected: its RT2 normal-mix
  sample uses another page decode than 81657854's, so it needs a hand audit, not the flip.
- Job m4_generated updated for one rerun (window fix, IP grass pairing lines, `pairs \d+`); predicted.json rerun 2:
  camp +2.5 (1.5-4), inn +0.3 (0-1), bushes pairing diagnostic only.
- Remaining plan (for a fresh agent): (1) rerun m4_generated (needs the user's go); read the IP grass pairing line for
  the bushes (unread / wind inputs / placement / no prepass) and fix that certificate or the depth-family match;
  (2) skip capturing G-buffer draws of a generated pair that never pair (IP_Base_VT, 83 per camp frame): e.g. capture
  only when a depth draw of the same key was noted this frame; (3) audit 3b346b42 by hand (RT2 decode) as a new
  recipe; (4) promote only if the share gain is worth its GI ms (run 1: +2.3 points for +0.58 ms at camp).
