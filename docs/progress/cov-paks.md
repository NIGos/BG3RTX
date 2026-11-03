## Pak material coverage (branch cov-paks2, PARTIAL_OFFLINE, 2026-11-03)
- Continues babbb778; no git metadata writes, commit, game launch or live promotion.
- B3PAKS1 v3 retains full ordered COLOR_0 RGBA beside position/UV corners.
- Native descriptor must declare exactly one Float4/Byte4 colour map; absent,
  mixed, multiple, non-finite and unknown layouts fail closed, never default white.
- Audited Tileset reads R/G. Paks alone enables its native XFB/9-texture path;
  full geometry, UV, COLOR, cull, resource and basis checks gate its rows.
- Existing native-Tileset opt-in stays independent; paks-only rows stop on key off.
- S6 FLUID lava uses B/A plus flow noise, rock mask and view distance. ENV lava
  Flowmap_Bounce VS uses G/B for displacement/phase; FS uses animated VT flow UV.
  Channels are retained offline; these missing dynamic recipes stay unqualified.
- Device-local HLOD inputs get one-time 32-corner UV readback after submit retirement.
  Unique UV candidates still need full geometry proof before material publication.
- Reset recordings retry; destroyed buffers invalidate candidates; jobs are bounded.
- Batch covers all matching lava/Tileset/HLOD sources, with 25 s progress,
  per-GR2 cache/checkpoints, resume offsets and immutable streamed <=480 MB shards.
- Complete manifests load transactionally, <=512 MB/file and <=2 GB aggregate.
- Inventory: 45,356 candidates, 3,541 metadata rejected, 41,815 eligible rows
  in 21,712 GR2s. Fresh decoded/joined 0; unprocessed 41,815, not rejected.
- Blocked: LSLib v1.20.4 native DLLs unavailable; restricted-network download failed.
  Only shared addition: mesh_paks_cov_paks2_inventory.json. Old 74-row table preserved.
- Host/core builds pass; 11 focused CTests and 12 Python cases pass. Python v3
  fixture and existing 74-row v2 table both pass the C++ reader.
- 236/236 shader requests byte-identical to babbb778; 0 DXC compiles; ABI/request delta 0.
- cov_paks predictions updated; fake-runner dryrun passed, no watched queue used.
- S6/camp/S8 draft checks same-run shares, float scene energy and GI GPU ms;
  no measured gain/cost. Pak Tileset layout skips unsupported RT water variants.
- Before live: supply decoder, generate bounded real tables, verify COLOR joins;
  whole-catalogue loading above 2 GB and Ultra water parity remain unresolved.
