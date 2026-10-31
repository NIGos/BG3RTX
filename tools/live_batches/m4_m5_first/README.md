# M4/M5 first live batch (live_batch m4m5-first), NOT authorized

Drafts for the first increment of [PLAN_M4](../../../docs/PLAN_M4_REFLECTIONS_GI.md) and [PLAN_M5](../../../docs/PLAN_M5_WATER.md), in the lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch per job (the game takes focus: the user must be away), FG 2, same-run A/B, diagnostics only in `diag-*` windows, 20 s diag-off `cost-*` windows, one PNG trace per 8 s `shot-*` phase, no float bursts. Run only after the regression batch passes and the user gives an explicit go. Receipt `m4m5-first.json`; plans go to test-main `captures\live_batches\m4_m5_first\plans`.
1. `mm1-gi-hit-direct-s1-fg2` (QuickSave_3784, 199 s): M4.1, the existing `gi_hit_direct` with startup `rt_hit_material=1`. 20 s warm-up ON with rt_stats (participation), then OFF/ON/OFF: each a 20 s static cost window, an 8 s PNG shot, a 15 s Q turn and a 15 s E turn back. Runs on current main.
   Pass: valid run; `GI hit direct` candidates/qualified/supported/nonzero > 0; in the PNG shots no GI region darker than OFF beyond the OFF-a/OFF-b spread, added bounce visible in hidden-hit crops (review). Cost: `gi: trace pass ... GPU median` ON minus OFF, static and turning.
   Dropped from the burst draft: the turn transient gate (`pan_artifacts` needs K16 bursts, about 2.3 GB each, over the 2 GB budget). If the static gate passes, a separate job with one OFF and one ON turn burst answers it.
2. `mm2-water-geometry-s1-fg2` (QuickSave_3784, 154 s): M5.1, `rt_water_geometry` OFF/ON/OFF with PNG shots, coverage stats in ON (15 s static and a 15 s Q turn with rt_stats: lifetime). Needs the M5.1 implementation: its `[water] capture`, `[water] coverage` and `[water] build GPU` expect lines are the log contract. Without them the run is INVALID, never a pass.
   Pass: supported draws > 0 (a view without water is INVALID); behind-opaque 0 beyond `depth_tol`; final image OFF/ON/OFF equal within the OFF-repeat spread (PNG shots); build ms reported. `requires_success_of` mm1: both run on the same build.

Duration: plans 199 + 154 s; with 2 launches about 10 min (at most 16 min of timeouts). Disk: 6 PNG traces plus logs, about 0.4 GB.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `mm` and receipt `m4m5-first.json`.

Offline check: `powershell -File tools\live_batches\m4_m5_first\dryrun.ps1 [-Out <scratch dir>]`. Last result 2026-10-31: all checks passed.
