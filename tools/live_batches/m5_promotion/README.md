# M5 water promotion to Ultra (live_batch m5-promotion), NOT authorized

Decides whether the preset commit "Ultra preset: M5 water chain on" (branch of this batch, docs/PLAN_M5_WATER.md
"Promotion") is merged. Lean format of [../lean_regression/README.md](../lean_regression/README.md) and of
[../m4_promotion/README.md](../m4_promotion/README.md): one foreground launch per water save, FG 2, every A/B by
toggling keys inside the run, `rt_stats` only in `diag-stats-on`, diag-off 20 s `cost-*` windows, PNG traces in
`shot-*`, `diag-view-*` and the first pan of each pair. Independent jobs. Receipt `m5-promotion.json`; plans go to
test-main `captures\live_batches\m5_promotion\plans`.

Startup (both jobs): `preset=ultra` (main's Ultra, so roof casters on) plus the whole water chain as the preset
commit sets it: `rt_water_geometry=1 rt_water_reflections=1 rt_water_compose=1 rt_water_shadows=1
rt_water_ls_shadows=1 rt_water_refraction=1 sharc_device=1`. The four restart keys patch the audited water FS with the
104-B record at pipeline creation; afterwards each key is a live switch. The test tree is main: the keys are explicit.
- Chain ON: all six 1. Chain OFF: all six 0 (geometry and measured reflections included: their cost is part of the chain).
- `qa_exposure_hold=1` from `cost-off-a` to `shot-off-b`: one pre-exposure for every compared frame (docs/QA.md
  "Exposure hold"). Without it the darker water raises the eye adaptation: m5w4 read "+2.2% brighter" before the water.

| Job | Save | Plan | Why |
|---|---|---|---|
| `m5p1-s8-fg2` | Water Queens House | 215 s, 18 traces | the only water scene measured so far (pool c4e92ccd, sea 7b44a567) |
| `m5p2-flymm-fg2` | Flymm Cargos Basement - 101h 05m | 215 s, 18 traces | second scene: flooded basement, open water over most of the save thumbnail, light shafts and lanterns; never captured |

S1 docks (`QuickSave_3784`) now loads the Steel Watch Foundry. Other candidates checked (read-only, `scripts/save_index.py`
thumbnails and 218 captures scanned for the audited water FS): S14 Lower City and S7 inn draw 7b44a567 at about 0% of the
screen; S3 camp draws 06e66a6f (not mapped for the compose); Wyrms Crossing / Rivington need the missing mods;
`QuickSave_3781` shows the same basement as Flymm (a quick save can be overwritten: the named save is used).

Phases (both): `warmup-on` 10 s, `diag-stats-on` 12 s (rt_stats=1, chain on), `cost-off-a` 20 s, `shot-off-a` 10 s
(traces at 2, 5, 8 s), cumulative steps of 8 s with one trace each: `shot-step-rf` (+geometry, +measured reflections:
no image change), `shot-step-compose` (+reflections composed), `shot-step-sun` (+sun shadows), `shot-step-ls` (+local
shadows); `cost-on` 20 s (+refraction = the chain), `shot-on` 10 s (3 traces), debug views 6 s each (rt_debug 25
compose delta, 26 sun shadow, then 28 refraction at S8 or 27 local shadows at Flymm), `cost-off-b` 20 s, `shot-off-b`
10 s (3 traces), then the moving camera: `move-on-q` (pan Q, chain on, a trace at 6 s), `move-on-e` (E back),
`move-off-q` (Q, chain off, a trace at 6 s), `move-off-e`; `restore` 5 s (all 0, hold off).

## Deciding numbers and gates (`py -3 -m tools.m5_promotion_gate`, after the runs)
- Valid run: every `expect` line (participation of each key, the 104-B record, patched FS, exposure held) and every
  `expect_every` gate, per complete window of `cost-on`, `move-on-q`, `move-on-e`: capture supported > 0 with overflow 0
  and failed 0; compose 110-120 of 120 frames composed with owned > 0 (static and moving); compose GPU median < 0.5 ms
  (cost-on); sun, local and refraction `clamped` below 120 per window (one record per frame); S8 only: sun occluded > 0,
  local sum check within 0.098% for >= 99% of the checked records, refraction screen hits > 0. A Flymm run whose water
  draws are not on a variant (an unmapped water family) is INVALID: the decision then waits for that family's map.
- Energy, two-sided (the gate script). Measured: the RR colour input (the trace's `00000000` slot-0 dump: the frame
  after the water and every forward pass) divided by the exposure probe at the trace frame. The m4 gate's 5c3fdabc
  is the lighting BEFORE the water: here it is the control (it must not move: water is in no TLAS the lighting
  traces). Predicted: the compose's own per-window accounting (reflection delta, sun and local removed, refraction
  delta, times owned pixels over the render size). Pass: |measured - predicted| <= 2 standard errors from the six
  OFF frames + |control| + the prediction's window spread, for `shot-on` and every step, at both scenes, and no
  non-finite pixel beyond the OFF frames'. The noise is real: the water's sun glints (up to ~1000 against a mean of
  0.18) move one frame's mean by about 0.5% (SD) at S8; the 11-01 single-frame runs showed up to 2.6%.
- Predicted at S8 before the run (11-01 logs, the same view; the in-run accounting decides): step-rf 0.00%,
  +compose -0.07%, +sun -9.46%, +local -9.46%, chain ON -9.44% of the image mean. Tolerance at about 0.55% SD: about
  +-1.2% for a one-frame step, +-0.8% for shot-on. Flymm: no prior (a basement lit by a sun the game does not shadow on
  water: a large sun removal is expected and physical).
- Participation (every complete window, min..max): capture, reflections (in front, tiers), compose (composed frames,
  owned, ratio bins), sun (occluded, removed, clamped), local (sum check, ratio, clamped), refraction (screen hits,
  shift, ratio of the means, clamped). Motion: the move-on windows compose >= 110 of 120 frames, clamped < 120.
- Cost (reported, not gated: quality first on Ultra): cost-on minus the OFF mean for gi, rt passes, scene build, and
  the water lines (build, reflections, compose); "water chain GPU" = their sum. Expected about 0.75 ms at S8 (build
  0.28 + reflections 0.12 + compose 0.24-0.35). Frame time only when the game had focus (unfocused runs cap at 30 fps).
- Visual (review): shot-on vs shot-off-a only on water pixels; the diag views (25 green/red only on water, 26 red
  where columns or walls block the sun, 28 green on the clear pool); the two pan traces (move-on-q vs move-off-q at
  the same time into the same pan) show no lagging or smeared reflections.
Merge the preset commit with merge-green only if both runs are valid and every gate above passes.

Duration: plans 430 s, 2 launches, about 11.2 min (dry-run estimate; at most 16 min of timeouts). Disk: 36 PNG traces
at about 43 MB (measured at S8, 1280-wide previews) plus logs: about 1.6 GB; delete the trace folders after the script
has run and the summary is in the progress file. Start, results and rollback: as in ../lean_regression/README.md with
this folder, prefix `m5p` and receipt `m5-promotion.json`.

Offline check: `powershell -File tools\live_batches\m5_promotion\dryrun.ps1 [-Out <scratch dir>]`. Last result
2026-11-01: all checks passed (Windows PowerShell 5).
