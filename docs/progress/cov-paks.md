## Pak material coverage (cov-paks2, READY_FOR_LIVE, 2026-11-03)
- No game, saves, git metadata writes, commit, source deletion or promotion.
- Compact B3PAKS1 v4: 40,983 rows, 6,091 COLOR layouts, 85,630 texture refs.
- Binary 41,956,440 bytes (40.01 MiB), versus 33,386,148,008 bytes (31.09 GiB).
  Full source digest validation/conservation passed; C++ parsed every compact row.
- Additive shared prefix: mesh_paks_cov_paks2_compact_20261103 .bin/.manifest/.json.
  Runtime prefers its complete manifest; 512 MiB/file and 2 GiB/set caps unchanged.
- Retains native content ID/canonical samples, UV signature, counts, UUID, flags,
  texture parameter/UUID refs and explicit canonical COLOR_0 RGBA layout.
  Full ordered geometry/COLOR SHA-256 witnesses replace geometry/color payloads.
- Native content ID reads bound float3 input before VS instance transforms,
  not XFB. Pak IDs can agree with unchanged native upload bytes/index order.
- Expanded XFB has changed positions, implicit corner indices and count/span;
  pool inverse transforms restore approximate positions, not original float bits.
- Candidate join now uses count + ordered UV0 (byte 16 in 64/88-byte XFB).
  Ambiguous contracts reject. Native ID equality/difference remains diagnostic.
- Read-only replay audit: S6 unique 0/1,015 (36 missing counts, 979 UV mismatches);
  camp 58 unique + 4 ambiguous/471; S8 52 unique/602. Full witnesses 0 everywhere.
  Matching-UV examples differ in position by 2.83e-6..6.68e-6 after reconstruction.
- Publication still requires full ordered SHA-256, current resources, shader pair,
  cull and basis. Tileset also requires all native RGBA. No UV-only promotion.
  Device-local UV candidates lack a full witness and remain unqualified.
- Loader/attempt/match/rejection logs distinguish missing, ambiguous, invalid,
  witness, recipe/cull and pending/unsupported inputs. Texture-view bridge absent.
- Five focused CTests pass (including compact conservation/corruption tests and
  synthetic transformed/expanded 64/88-byte layouts); host build passes.
- Camp diagnostic: 75 s + load, ON/OFF/ON + restore, visible_unfocused_fg,
  both requested keys on at startup. Predicted JSON and fake-runner dryrun pass.
  Prepared outside watched inboxes; no live run, visual gain or GPU cost measured.
- ABI and shader request delta 0. Lava dynamic recipes/Ultra parity unqualified.
- Coordinator may delete the 517 full_20261103 files only after accepting compact:
  exact additive inventory in compact JSON; 33,602,532,633 bytes including metadata.
  Other prefixes/caches are excluded. No compact file depends on full files.
- Design/diagnosis: tools/assets/mesh_content_id.md. Full structured evidence:
  docs/progress/cov-paks-compact-report.json; previous provenance reports preserved.
