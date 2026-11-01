# M4 wind RGB foliage hit materials (live_batch m4-wind-rgb), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch, FG 2, keys
toggled inside the run, `rt_stats` only in `diag-*` windows, diag-off 20 s `cost-*` windows, one PNG per `shot-*`
phase, the exposure held (`qa_exposure_hold=1`) in every A/B window. Receipt `m4-wind-rgb.json`; the plan goes to
test-main `captures\live_batches\m4_wind_rgb\plans`.

1. `m4w-wind-rgb-s8-fg2` (Water Queens House, 144 s). `rt_hit_material_wind_rgb` (docs/TRACK_D_REFLECTIONS.md 14,
   "Third family: wind RGB 2S"): the instanced ATM_Wind_Vertex_RGB_AlphaTest_2S_Translucency_VT draws (depth
   633929ef/0d91554d, G-buffer 735d5a19/83831811), the largest single blocker of the S8 replay pack: 13.3% of all
   hit-material lookups (`tools/hit_material_blockers.py`, 17 soup alpha instances, row reason). Startup
   `rt_hit_material=1`, `rt_hit_material_instanced=1`, `rt_hit_material_wind_rgb=1` (the G-buffer VS frame patch),
   `gi_hit_direct=1` throughout (the consumer that counts `hit material`). Live A/B: `rt_hit_material_instanced`
   0/1 (it carries all three instanced families; the third family has its own lines). Phases: warm-up ON, diag
   OFF / ON / OFF (15 s), cost OFF / ON / OFF (20 s), PNGs OFF / ON / OFF, restore.

## Deciding lines and gates
- Participation (`expect_every`, diag-on): `[hit material instanced] frame N wind RGB 2S: prepass P, G-buffer G
  (pipeline, bindings, stream) captured C, mapped M rows T triangles` with C, M > 0. A zero says where it stops:
  G-buffer 0 (not in view or the frame patch missing), pipeline (not exact), bindings (views, sampler, UBO reads),
  captured > mapped (pairing: the `wind RGB 2S since the last line: pairs ...` line names the cause).
- Exactness (`expect_every`, diag-on): `hit material instanced clip wind RGB 2S (120 frames): checked X, bit-exact
  Y, ...; rejected R = P% of all lookups` with X > 0 and P < 0.04% (the wind/2S gate of m4i). No tolerance is
  derived for the flutter term: every hit is bit-exact or rejected (fail closed). Expected X = Y: the RG wind rows
  were 100% bit-exact live (m4i 2026-11-01, 101357 of 101357) and the two RGB VSs have the same arithmetic text.
- The other families (`expect_every`, diag-on): the wind/2S clip line keeps rejected < 0.04%; key-off windows log
  `key 0; wind: prepass 0, G-buffer 0` (nothing instanced maps).
- Share: `hit material (N frames): attempted A qualified Q ...`: Q/A diag-on minus the mean of diag-off-a/b, larger
  than |off-b - off-a|. Predicted from the replay pack: up to +13.3 points from this family (row reason down by as
  much; S8 replay key-off 52.8% qualified), plus the wind and 2S rows' own gain (bfbe4747 2S 5.7%).
- Energy: float mean of the pre-post scene colour (`dumps.json`), shot-on minus shot-off-a against |shot-off-b -
  shot-off-a|; no darker region. The hit gets the class 0 front lobe only (the leaf transmission is missing):
  bounce can only be added where the old fallback added none, so ON >= OFF within noise.
- Cost: cost-on minus the mean of cost-off-a/b for `gi: trace pass ... GPU median` and `rt passes: GPU cost`.
  Expected: the XFB of the instanced G-buffer draws of this family (64 B per vertex, all instances; S8 about
  38k triangles) and the query copies, inside the existing caps (`[hit material capacity]`). Not measured.

Duration: 144 s plan, about 4.4 min with the load. Disk: 3 PNG traces at about 60 MB plus logs, about 0.2 GB.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `m4w` and receipt
`m4-wind-rgb.json`. Restart required to undo (startup key): set `rt_hit_material_wind_rgb=0`.

Offline check: `powershell -File tools\live_batches\m4_wind_rgb\dryrun.ps1 [-Out <scratch dir>]`. Last result
2026-11-01: all checks passed.
