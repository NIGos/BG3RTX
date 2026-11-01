# M4 instanced hit materials (live_batch m4-instanced), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch, FG 2, keys
toggled inside the run, `rt_stats` only in `diag-*` windows, diag-off 20 s `cost-*` windows, one PNG per `shot-*`
phase. Receipt `m4-instanced.json`; the plan goes to test-main `captures\live_batches\m4_instanced\plans`.

1. `m4i-instanced-s14-fg2` (Lower City - 91h, 149 s). `rt_hit_material_instanced` (docs/TRACK_D_REFLECTIONS.md
   14, "Instanced wind and two-sided rows"). S14 is the densest test scene for both families (classifier, placed
   sub-level geometry: wind 9.7%, two-sided alpha-test 4.1%; next S9/S10/S3). Startup `rt_hit_material=1`,
   `rt_hit_material_instanced=1` (the G-buffer VS frame patch), `gi_hit_direct=1` throughout (the consumer that
   counts `hit material`). Phases: warm-up, diag OFF / ON / OFF (15 s each), cost OFF / ON / OFF (20 s), PNGs
   OFF / ON / OFF.
   Validity (expect lines): the ON window logs `[hit material instanced] ... key 1` with wind and 2S prepass > 0
   and G-buffer > 0 (both families in view), the wind inputs line and the `hit material instanced clip` line with
   rejected below 0.04% of all lookups, an OFF window logs `key 0; wind: prepass 0, G-buffer 0`, the
   `hit material` reasons line, `GI hit direct`, `gi: trace pass` and `[geo] transform feedback ... (stride 64)`.

## Deciding lines and gates
- Participation (diag-on, last line of the window): `[hit material instanced]` mapped > 0 for wind and for 2S.
  A zero says where it stops: G-buffer 0 (draws not in view), pipeline (not exact: no frame patch, blend, state),
  bindings (views, sampler, UBO reads), stream (deferred XFB cap), captured > mapped (no unique prepass key, or
  the alpha record's views differ). Key 0 windows: mapped 0.
- Share: `hit material (N frames): attempted A qualified Q ...`: Q/A in diag-on minus the mean of diag-off-a/b,
  larger than |off-b - off-a|. `of base, instance without a material record` / A goes down by about as much.
- Exactness: the reasons `row` (3: the join refused the snapshot vs the alpha record) and `clip` (4) ON minus OFF.
  First run (2026-11-01, log m4i-instanced-s14-fg2.1.074646): clip 0 -> 0.4-1.9M per 120 frames at a static
  camera, qualified + clip constant: the wind rows (TRACK_D_REFLECTIONS 14). Wind rows now compare clip positions
  within a derived fp32 tolerance (2S stays bit-exact). Deciding lines (diag-on): `[hit material instanced] ...
  wind inputs prepass vs G-buffer: equal N, time only N, other differ N, unread N; rows with the derived clip
  tolerance N` (the condition) and `hit material instanced clip (120 frames): wind: checked, bit-exact, within the
  derived tolerance, UV differs, beyond x1-16 / x16-256 / over x256, rigid offset; alpha-test 2S: ...; rejected R
  = P% of all lookups`. Gate (expect line): P < 0.04%, below the spread of the key-off qualified share at this
  static camera in the first run (0.2230-0.2234, 0.046%). Rejects that remain: beyond x256 or rigid offset =
  real mismatches (another instance or vertex), kept fail-closed; `other differ` > 0 = the passes' VS inputs differ.
- Energy: mean PNG luminance shot-on minus shot-off-a against |shot-off-b - shot-off-a|; no darker region.
- Cost: cost-on minus the mean of cost-off-a/b for `[pace] rendered frame time` (median), `gi: trace pass ... GPU
  median` and `rt passes: GPU cost`. Expected: XFB of the instanced G-buffer draws (64 B per vertex, all instances)
  plus the query copies (2x), bounded by the existing caps (`[hit material capacity]`: deferred 128 MB, source 128 MB,
  query 256 MB per frame): about 0.2 to 1 ms on Ultra, not measured.

Duration: 149 s plan, about 4.5 min with the load. Disk: 3 PNG traces at about 60 MB plus logs, about 0.2 GB.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `m4i` and receipt
`m4-instanced.json`.

Offline check: `powershell -File tools\live_batches\m4_instanced\dryrun.ps1 [-Out <scratch dir>]`. Last result
2026-11-01: all checks passed.
