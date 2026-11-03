## Pak mesh join and restricted HLOD fallback (branch cov-paks, PARTIAL_OFFLINE, 2026-11-03)
- New tools.assets.mesh_paks reuses the groom LSLib v1.20.4 native GR2 reader.
- Local-only JSON/bin retain v1 content ID, 32 UV samples, full ordered position/UV
  corners, material UUID, texture metadata, colour intent and two-sidedness.
- Cache keys include source, material, tool and decoder bytes; 25 s heartbeat.
- ObjectID suffix is ExportOrder, never group ordinal. Unique native name plus
  one full-range MaterialIndex 0 group only; conflicting assignments fail closed.
- rt_hit_material_paks=0, Experimental GUI; enable with instanced capture at startup.
- Existing recipes win. Exact HLOD_GM recipe, all position bits/UVs, static cull
  state, current captured descriptors/overrides and checked basis gate new rows.
- Pak two-sidedness uses the existing row flag. No ABI/shader/request delta.
- Key off publishes no pak rows; ambiguous, stale or unsupported inputs stay unknown.
- Lava/Tileset vertex-colour recipes and unmapped-input fallback remain incomplete.
- Native ExportOrder/LOD cannot be independently verified by the current GR2 reader.
- Generated 30 GR2 -> 74 rows (40 HLOD / 13 lava / 21 Tileset), 0 failures;
  10 audited HLOD_GM rows. Final local JSON/bin: mesh_paks_cov_paks (76.1 MB bin).
- Tileset repeat: 10/10 cache hits, 2.48 s versus 13.5 s decoding.
- Full host build and 7 focused CTests pass; 9 Python cases pass. Full-match
  mutations beyond the 32 samples, tolerance bounds and malformed v2 reject.
- Live draft tools/live_batches/cov_paks: S6 forge lava, camp, S8; OFF/ON/OFF,
  missing-record/attempted and qualified shares, float scene energy, GI GPU ms.
- Three plans 174 s each; estimate 14.7 min, 570 MB. Fake-runner dryrun passed.
- No game launch, live qualification, measured coverage/energy/GPU cost or promotion.
- No commit, merge or git metadata writes; assetdb additions only, never shipped.
