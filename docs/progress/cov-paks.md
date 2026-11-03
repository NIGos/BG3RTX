## Pak material coverage (branch cov-paks2, COMPLETE_OFFLINE_CATALOGUE / JOINS_UNPROVED, 2026-11-03)
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
- Inventory: 45,356 candidates; 3,541 conflicting source/name assignments rejected.
  All 41,815 eligible rows in 21,712 GR2s processed; unprocessed 0.
  Decoded/material candidates 40,983; native names absent 832; source decode failures 0.
  Proven runtime joins 0; this is not a measured 0% coverage or quality result.
- Original decoder blocker resolved by the user-approved local LSLib release.
  Full additive catalogue generation is complete; old 74-row table is preserved.
- Host/core builds pass; 11 focused CTests and 12 Python cases pass. Python v3
  fixture and existing 74-row v2 table both pass the C++ reader.
- 236/236 shader requests byte-identical to babbb778; 0 DXC compiles; ABI/request delta 0.
- cov_paks predictions updated; fake-runner dryrun passed, no watched queue used.
- S6/camp/S8 draft checks same-run shares, float scene energy and GI GPU ms;
  no measured gain/cost. Pak Tileset layout skips unsupported RT water variants.
- Before live: select bounded scene tables and verify current resources/COLOR joins;
  whole-catalogue loading above 2 GB and Ultra water parity remain unresolved.

## Catalogue continuation (2026-11-03, cov-paks2)
- Branch checked before all other work: cov-paks2. Git metadata stays read-only.
- Native reader: official Norbyte LSLib release v1.20.4, Packed/LSLib.dll;
  GR2P v2 exporter and B3PAKS1 v3 tables. Divine.exe is available but this run
  uses LSLib's native GR2Reader directly, without a glTF conversion.
- LSLib licence: [MIT, copyright 2015 Norbyte](https://github.com/Norbyte/lslib/blob/v1.20.4/LICENSE).
  LSLib.dll reports file/product version 0.0.0.0; SHA-256
  `5dfe246b93cbc95531e011dbf18478c665a9de4f922f8b4a2b73da5a865c76d0`.
- granny2.dll: RAD Game Tools, file version 2.11.8.0; proprietary, never
  redistributed. SHA-256 `93e29135af0f8cb08a9cf95bad69f7b6a13acee82d689d1086312fe1135f2af6`.
  Its existing location is Packed/, alongside ConverterApp, not Packed/Tools/.
- Existing portable PowerShell 7.6.9 (MIT) is read from the other worktree's
  build/cov-paks-pwsh directory; no files are written there. Python 3.11.16,
  NumPy 2.4.3. No downloaded binaries or derived data are redistributed.
- Generation uses BelowNormal priority, 128-source immutable batches, per-GR2
  cache/checkpoints and 25 s progress. Output prefix is the new shared addition
  `mesh_paks_cov_paks2_full_20261103`; old tables remain intact. Lava sources
  precede Tileset/HLOD sources, following the high-impact emission/coverage gaps.
- This prefix completed all 21,712 GR2 sources in 1,425.9 s (23.8 min):
  41,815 eligible rows processed, 40,983 decoded candidates (98.01%), 832 row
  rejections, 0 failed-source rows and 0 unprocessed. Earlier metadata rejections
  remain separate: 3,541 of the original 45,356 catalogue candidates.
- All 832 failures are catalogue ObjectID mesh names absent from the decoded
  GR2 (including Dirty/Wallpaper Tileset variants). No fallback name, group
  ordinal or default colour was substituted. There were 0 source decode failures.
  6,091 candidate rows retain explicit native COLOR_0 RGBA.
- Published 170 complete batch manifests and 176 v3 shards: 33,386,148,008 bytes
  (31.09 GiB), largest 502,737,004 bytes, below 480 MiB. Independent validation
  passed header/version, row totals, file sizes, manifests, common batch identity
  and conservation: 40,983 + 832 = 41,815. Existing shared tables were preserved.
- Proven joined rows: 0. Generation is complete; qualification remains
  PARTIAL_OFFLINE. The full set exceeds the runtime 2 GiB aggregate cap and is
  deliberately not published as `mesh_paks_cov_paks2.manifest`.
- Read-only replay audit: exact ordered pool position/UV matches are 0/1,014 and
  0/1,015 in S6 forge packs 013032-1 and 035919-1; 0/471 in camp 013545-2;
  0/602 in S8 142819-1. These are instance counts, not hit-weighted coverage.
  Joined-row coverage estimate is unavailable: these packs lack native texture
  UUID-to-view mapping and per-draw COLOR/cull/basis proof. Camera-relative soup
  is excluded. No measured visual gain or GPU cost is claimed.
- Evidence in this tree: `captures/cov-paks2/catalogue_validation_20261103.json`,
  `replay_inventory_20261103.json` and `replay_geometry_20261103.json` in that
  directory. Shared summary: main checkout's
  `data/assetdb/mesh_paks_cov_paks2_full_20261103.json`.
- Focused checks: 14 generator tests and 8 replay-audit tests pass. No game,
  saves, queue jobs, presets, shader compilation or git metadata writes.
- Concurrent work in this tree also added a separate `full_v1` generation and
  provenance section below. Those edits/outputs are preserved; the counts above
  belong specifically to `full_20261103`, not a combined or deduplicated set.

### Full local generation, 2026-11-03
- Branch checked: cov-paks2. Git metadata stays read-only; no merge or commit.
- Reusable native reader and 100-source immutable batches avoid one PowerShell
  startup per GR2. Original/C# readers agree on all 40 probe candidates' ordered
  geometry, UV, COLOR (25 rows) and content IDs. No conversion or vertex reordering.
- BelowNormal priority is inherited by Python, PowerShell and native validation.
  Per-GR2 checkpoints and 25 s progress remain active. Completed batch manifests
  resume without decoding again; source caches also survive a new output prefix.
- Outputs: shared additions mesh_paks_cov_paks2_full_v1_s*.json/.manifest/*bin;
  mesh_paks_cov_paks2_full_v1.json is an offline summary, not a runtime manifest.
  Whole-catalogue publication above the runtime 2 GB aggregate cap is excluded.
- Reader: LSLib ExportTool v1.20.4, LSLib.dll file version 0.0.0.0,
  SHA-256 5dfe246b93cbc95531e011dbf18478c665a9de4f922f8b4a2b73da5a865c76d0.
  [LSLib LICENSE at v1.20.4](https://github.com/Norbyte/lslib/blob/v1.20.4/LICENSE)
  is MIT, copyright Norbyte 2015. Divine.exe and ConverterApp are available;
  generation directly calls LSLib's GR2Reader rather than a conversion export.
- granny2.dll: RAD Game Tools, Inc., version 2.11.8.0,
  SHA-256 93e29135af0f8cb08a9cf95bad69f7b6a13acee82d689d1086312fe1135f2af6.
  Proprietary local dependency supplied in the approved release; never redistributed.
- Host: PowerShell 7.6.9 (MIT; local LICENSE.txt), bundled .NET 10.0.14;
  CPython 3.11.16 (PSF), NumPy 2.4.3 (distribution licence expression:
  BSD-3-Clause AND 0BSD AND MIT AND Zlib AND CC0-1.0).
  Pak decompression: python-lz4 4.4.5 (BSD-3-Clause; installed LICENSE),
  zstandard 0.25.0 (BSD-3-Clause; installed metadata).
  Detailed tool paths, archive and executable hashes: build/cov-paks2-tool-provenance.json.
- No game launch, live measurement, shader/ABI change or preset promotion.

### Completed results
- 24 min 45 s, BelowNormal throughout. Candidates: lava 45, HLOD 35,506,
  Tileset 5,432; 6,091 rows retain explicit native COLOR. All 832 eligible-row
  rejections are absent native names in Tilesets. No source decode failures.
- Representative mismatch: VisualBank CastleTower_Foundation_Dirty name versus
  native CastleTower_Foundation. Both readers produce byte-identical GR2P dumps.
  No heuristic name stripping or alias publication was introduced.
- 218 immutable shards, 33,386,148,680 bytes total; largest 494,532,784 bytes.
  All 218 pass the existing C++ parser. No whole-set runtime manifest published.
- Replay pools examined: S6 forge 1,015, camp 471, S8 602 instances. Strict full
  position/UV-bit matches 0 in each; joined-row coverage estimate unavailable.
  UV-bit equality is stricter than runtime's 1e-3 tolerance: zero strict matches
  does not prove zero runtime coverage. Packs lack native texture identity and
  per-draw COLOR/cull/basis proof. Original replay row-table hashes are unchanged.
- Checks: 14 catalogue tests, 8 replay-audit tests and 218 native shard parses pass.
  Native reference parity includes the original 40-row probe and one rejected
  Tileset source. Shader request delta 0; no GPU cost or visual gain measured.
- Structured report: [cov-paks2-report.json](cov-paks2-report.json). Detailed local
  logs, provenance and checkpoints: build/cov-paks2-*. Keep build/cov-paks2-cache
  while the JSON provenance still references its geometry/COLOR streams.
