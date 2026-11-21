## Emission replay packs and PT emission for gi_far_roof_emission (branch claude/em-pack, READY_FOR_LIVE, 2026-11-21)

- Why: the 1054 A/B could not run offline (.scratch/coord/roof_em_pt.md): the GI pack writer refused the emission
  variant, a pack replay bound no emissive scene, the PT had no emission.
- `replay_pack_pass=2` (diagnostic, default 0): one pack of the rt_hit_emission + gi_hit_sun hit GI as pass
  `gi_hit_sun_em` (no SHaRC, no OMM): the live request and specialization values, the sun trust image s3b8, the D-EM
  scene set as pack set 4 (layer records its writes, `replay_note_em_sets`). No ABI change; layer and core rebuilt.
- frame_replay: compiles the pass with `EM_SET=4` (game requests keep 2: the 34 gi_trace_rt requests byte-identical to
  main), rebuilds the emissive TLAS from emInfo + emSoup (`rp::em_geometries`), binds it. `--pt-reference` on such a
  pack sets `PT_EM=1`: mode 8 = first-vertex emission x the GI ray fade at hidden first hits (`pt_emission.pfm`).
- `tools/em_roof_ab.py`: 1054=0 / 1 / no-emission replays against PT mode 8 on the changed pixels; verdict CLOSER /
  FARTHER / NO CHANGED PIXEL / INVALID (raised pixels or giRaw.a changed).
- Tests: replay_pack_test (contract, em_geometries, reference_defines), pt_reference_test 12-16 on the synthetic
  emissive box: mode 8 a = 1, path emission 1 / 1.75, the pass's hidden-hit emission 0.6918 vs PT mode 8 0.6927 per
  pixel, giRaw.a equal to the sun variant, 1054=1 lowers only; em_roof_ab_test; `build.ps1 -Test`.
- Staged (not queued): `tools/live_batches_staged/r2_tools/em_pack` job r2-inn-em-pack: one launch at the inn, Ultra
  with OMM off, ~25 s after load + up to 90 s for the writer, at most 2048 MB; live_flow 0 failing (with and without
  --frozen). Offline steps after it: predicted.json "offline". Key `gi_far_roof_emission` stays 0 until then.
