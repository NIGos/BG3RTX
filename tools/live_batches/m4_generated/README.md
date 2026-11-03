# Generated hit-material rows (live_batch m4-generated), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch per save, FG 2,
the key toggled inside the run, `rt_stats` only in `diag-*` windows, `cost-*` windows without it, one PNG per
`shot-*` phase, the exposure held in every A/B window. Receipt `m4-generated.json`; the plans go to test-main
`captures\live_batches\m4_generated\plans`.

1. `hgn1-generated-camp-fg2` (Campsite - 27h, 164 s) and 2. `hgn2-generated-inn-fg2` (Blushing Mermaid, 164 s).
   `rt_hit_material_generated` (docs/GAME_DATA_AUDIT.md action 2; TRACK_D_REFLECTIONS.md 14 "Generated rows"):
   8 rows of `common/native_hit_generated.h`, made by `py -3 -m tools.material_families.recipe_rows` from the asset DB
   offsets, the FS classifier and the VS capture path. Startup Ultra + `rt_hit_material_generated=1` (patches the
   generated G-buffer VSs), `rt_hit_material_ip_grass=0`; then OFF / ON / OFF live. Phases: warm-up ON, diag 20/25/20 s,
   cost 20 s x 3, PNGs OFF / ON / OFF, restore.

## Deciding lines and gates
- Participation (`expect_every`, events): `[hit material generated] frame F key 1: instanced G-buffer S (...) captured C,
  mapped M rows T triangles` with C, M, T > 0 in diag-on; key 0 windows: captured 0, mapped 0. Pool rows: the
  `[hit material pool pairs]` entries tagged `(gen)` (records / ready UV-basis), read, not gated (the inn has a few).
- Clip check: the audited families' `hit material instanced clip` lines keep rejected < 0.04% with the rows on (the
  generated instanced rows count in their family's numbers: 2S, wind RGB, IP grass).
- Share: `hit material (N frames): attempted A qualified Q ...`, Q/A diag-on minus the mean of diag-off-a/b, larger than
  |off-b - off-a|. predicted.json: camp +12 points (3-25), inn +1.5 (0.3-4).
- Energy: float mean of the pre-post scene colour, shot-on vs shot-off-a/b; predicted null.
- Cost: `gi: trace pass ... GPU median`, cost-on minus the cost-off mean; predicted camp +1.0 ms (0.2-2.5).
- Capacity: `[hit material capacity]` overflow 0 in every diag window, `[geo] RT capture` opaque dropped 0.00 and
  `[rt] scene wait` 0 timeouts in every cost window. Risk: the IP_Base_VT G-buffer draws share the pair 0de1b186/641822a7
  with IP_AlphaTest but have no alpha depth record, so they are captured and never mapped (captured > mapped, stream use).

Duration: 2 x 164 s plans, about 10 min with the loads. Disk: 6 PNG traces plus logs, about 0.4 GB.
Undo: `rt_hit_material_generated=0` (live); restart without it to drop the VS patch.

Offline check: `powershell -File tools\live_batches\m4_generated\dryrun.ps1 [-Out <scratch dir>]`.
