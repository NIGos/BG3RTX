# Base_MSK_VT pool hit materials (live_batch m4-hit-mask), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch per save, FG 2,
the key toggled inside the run, `rt_stats` only in `diag-*` windows, diag-off 20 s `cost-*` windows, one PNG per
`shot-*` phase, the exposure held (`qa_exposure_hold=1`) in every A/B window. Receipt `m4-hit-mask.json`; the plans
go to test-main `captures\live_batches\m4_hit_mask\plans`.

1. `hmm1-mask-s8-fg2` (Water Queens House, 144 s) and 2. `hmm2-mask-inn-fg2` (Blushing Mermaid, 144 s).
   `rt_hit_material_mask` (live; docs/TRACK_D_REFLECTIONS.md 14 "Pool family: Base_MSK_VT"): pool material rows for
   the static instanced Base_MSK_VT pair (VS 2d2ac3c3 / FS a5f44c48). Startup `rt_hit_material=1`, `gi_hit_direct=1`
   throughout (the consumer that counts `hit material`). Phases: warm-up ON, diag OFF / ON / OFF (15 s), cost OFF /
   ON / OFF (20 s), PNGs OFF / ON / OFF, restore. The inn replaces camp: the traces show 15.5 Base_MSK_VT draws per
   frame at the inn, 5 at S8 and none traced at camp.

## Deciding lines and gates
- Participation (`expect_every`, diag-on): `[hit material pool pairs] ... 2d2ac3c3/a5f44c48 R/B` with R, B > 0
  (records / ready UV-basis). Key off (diag-off-a/b): the pair is not listed (its draws stay "other pair").
- Capacity (`expect_every`): `[hit material capacity]` overflow 0 in every diag window; `[geo] RT capture` opaque
  dropped 0.00 and `[rt] scene wait` 0 timeouts in every cost window.
- Share: `hit material (N frames): attempted A qualified Q ...`: Q/A diag-on minus the mean of diag-off-a/b, larger
  than |off-b - off-a|; base reason down by as much. Prediction in predicted.json (S8 +1.1 points, range 0.3-3).
- Energy: float mean of the pre-post scene colour, shot-on vs shot-off-a/b; ON >= OFF within noise (bounce only
  where the fallback added none). Predicted null (below noise).
- Cost: cost-on minus the mean of cost-off-a/b for `gi: trace pass ... GPU median`; expected below the noise (one
  2D sample more per qualified mask hit). Not measured.

Duration: 2 x 144 s plans, about 9 min with the loads. Disk: 6 PNG traces at about 60 MB plus logs, about 0.4 GB.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `hmm` and receipt
`m4-hit-mask.json`. Undo: `rt_hit_material_mask=0` (live).

Offline check: `powershell -File tools\live_batches\m4_hit_mask\dryrun.ps1 [-Out <scratch dir>]`.
