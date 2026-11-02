## rt_occ_stats occluder-kind diagnostic + character-mask float energy (branch worktree-agent-a0c9be1ab336eae1a, READY_FOR_LIVE, 2026-11-02)

- Key `rt_occ_stats` (0, live, diag windows only): "occ stats (N frames)" every 60 frames. Per source (RT sun,
  D-LS point/spot, D-LS box, RT AO) on character px (class skin/hair): darkened %, mean darkening %, occluded rays by
  kind (cascade, static, moving, cutout-static, cutout-moving = wind foliage, scalp, hair, held) with the near
  (<= 0.5 m) share and the dark share, occluder distance histogram, on-screen hair/skin share. Design and limits:
  docs/DIALOGUE_SHADOWS.md "Occluder-kind diagnostic".
- Sun/AO: own pass gi/rt_occ_stats.hlsl after the timed RT region (darkening from rtVis / giRaw, kinds from replica
  rays; sun replica mismatches counted). Local/box: RT_OCC_STATS twin of the every-mode box pass. Held: InstInfo
  meta bit 31 (layer, restart). Counters device-local (set 4 b1), group-reduced, host ring after D-LS.
- Key off: 2961/2961 requests byte-identical to 41bd576 (tools/shader_identity.py --baseline, 133 min uncached);
  the 36 new requests compile. Build + 98 CTests pass (new: occ_stats_test, char_energy_test; shader_identity_test
  failed once while the identity run held every DXC slot, passed on rerun).
- dump_preview_f32=1: the 5c3fdabc scene colour as float32 at the preview grid (~10 MB/trace);
  char_energy.py gives the character-mask float mean per shot vs its OFF neighbours.
- dialogue_shadows jobs: diag-occ-base/-nohold/-nocascade/-hair windows, shot-nogi-on, ds0 warms the occ variants;
  predicted.json "occ" per save (ds1/ds2 camp WLD_Main_A, ds3 port CTY_Main_A) committed before any run.
- Found: the box D-LS variants are not cascade entries, so with rt_cascade_casters=1 local/box rays hit the F-1/F-2
  cascade copy (traced 0xFF). Not fixed here; the 'cascade' kind under local/box measures it.
- Not measured: GPU ms of the occ pass (diag only). Live batch not run (needs user steering).
