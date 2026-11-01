## Hit-material blockers ranked; wind RGB 2S foliage rows (branch worktree-agent-a87dc09be59810f7c, offline, READY_FOR_LIVE, 2026-11-01)

- New offline ranking: `frame_replay --define HM_BLOCKERS=1` counts each lookup per TLAS instance (raw slots 448+,
  replay only; `rtstats.bin`), `tools/hit_material_blockers.py` joins them with the pack (InstInfo class, alpha
  record FS, geometry). S8 pack (rc1, instanced off, 280660 lookups, 52.8% qualified): pool meshes without a row
  14.9%; soup alpha without a row 25.4%, of it depth FS 0d91554d 13.3%, bfbe4747 (2S, already built) 5.7%,
  e62cdef4 (IP bushes) 2.2%, e2ecf65a (static Base_AlphaTest family) 2.1%; soup without record 5.8% (characters
  5.0%, terrain 0.8%); stale opaque rows 1.0%.
- Asset DB depth variants: 0d91554d = ATM_Wind_Vertex_RGB_AlphaTest_2S templates; at S8 and camp its triangles are
  735d5a19/83831811 (Translucency). Camp and S6 have no pack: camp row 51.6% is soup alpha (by triangles grass
  73849471 -> 105e47e4/35a18400 67%, IP bushes 13%, wind RGB 6.6%); S6 base 65% is soup without record (lava
  bd5dbda2/0b8dfd73, terrain 64e25689, characters, hair) since 94% of its pool instances qualify.
- Built (off): `rt_hit_material_wind_rgb` (startup, GUI "Native hit materials: RGB wind foliage", needs the
  instanced rows): third instanced family, depth 633929ef/0d91554d, G-buffer 735d5a19/83831811. Recipe = 14678345
  with the two-sided N, reflectance 1:1 byte 20 (classifier terms pinned in material_families_test); class 0 front
  lobe only (no leaf transmission). Bit-exact clip check (no tolerance), pairing as wind + 1:1 _m4, counters
  427..434 (`hit material instanced clip wind RGB 2S`, renumbered on counter-renumber after rf_hit_gi 419..426).
- Unchanged with the key off: `instanced::gate` hides the family from every lookup (native_hit_instanced_test);
  shader identity gate PENDING (run the new cached tools/shader_identity.py: every request without RT_HIT_INSTANCED must be byte-identical; HM_BLOCKERS and the family-3 counters are under defines).
- Tests: build + 82 CTests pass (settings schema regenerated); material_families_test 15 (3 new);
  native_hit_instanced_test and native_vt_recipes_test extended.
- Expected qualified share: S8 up to +13 points (minus clip rejects), camp about +3 to +7, S6 0 (absent).
- Live (not authorized): `tools/live_batches/m4_wind_rgb` (S8, 144 s, exposure held in every A/B window,
  expect_every on mapped rows and clip rejects < 0.04%); dry run passes. Cost not measured.
- Next blockers: pool meshes without a row at S8 (14.9%, families not in the pack), camp grass 105e47e4/35a18400
  (VARIANT + distance-dither discard, wind VS), S6 lava and terrain recipes, characters.
