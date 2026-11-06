## Default Ultra RF replay (branch rf-replay, READY_FOR_LIVE, 2026-11-06)

- Added `pass reflections` for the actual default `gi/rt_reflections.hlsl`
  request; captured defines/specs/push bytes. No hit shading, SHaRC or OMM.
- `replay_pack_pass=1,replay_pack=N` selects one RF frame per positive serial;
  both keys default 0. Selector 0 preserves GI/LS capture. ABI stays 27.
- Reused LS pre/post capture, raw BCn and bounded lossless codec (2048 MiB
  compressed disk cap). Includes RF-time lighting/probes/AO/DFG, alpha/TLAS,
  previous radiance, G-buffer/velocity/origins and shadow inputs.
- Restores native packed diffuse/specular, R16 hit distance and raw counter
  bins before every dispatch, outside timing. R16 diagnostics now use 2 bytes.
- Preserves the soup range when both RF ray masks exclude cascade geometry,
  reusing the LS fix for the NVIDIA 2 GiB GetDimensions limit.
- Synthetic CTest PASS 2.72 s: screen hits, changed history/probe controls,
  finite outputs, zero guards, repeat and paired output/counter byte identity.
- Explicit --ab-spec/--pipeline-stats control: 2 pairs, outputs/counters 100%
  equal; all 3 synthetic references differ by 0 bytes. Synthetic executable
  128 registers, shared 0 bytes; anomalous local-memory value not interpreted.
  Receipt: .scratch/rf-replay-pipeline.log. No production cost/saving claim.
- BelowNormal build.ps1 -Test PASS 200/200 (176.31 s CTest).
  Log: .scratch/rf-replay-final-build.log. Shader identity 218/218 versus HEAD;
  variants.txt request delta 0. Receipt: .scratch/rf-replay-shader-identity.log.
- Job tools/live_batches/rf_pack/jobs/rf-pack-inn.json: inn Ultra, stock hair,
  visible_unfocused_fg, static 45 s plan. Frozen/non-frozen lint: 0 failures.
  New RF expect has no historical receipt; capture remains unqualified.
- Offline only: no game launch, queue mutation or Git writes. Coordinator
  captures the pack, then compares live output/counter bytes before looking
  for exact savings. Actual inn size and capture GPU cost remain unmeasured.
