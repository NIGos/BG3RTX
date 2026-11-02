# S8 harbour water reflection flicker (live_batch water-flicker), NOT authorized

User report 2026-11-02 (Ultra on main): the water reflections at the port of Water Queens House flicker during camera
rotation. Two keys under test, both default 0 and live, toggled inside one run:
- `rt_water_rr_hitdist`: water_reflect writes RR's specular hit distance on water pixels (its mirror hit distance where
  it resolves, else no hit) instead of D-RF's value of the bed below (the water writes no depth).
- `rt_water_rf_edge`: the compose (`shaders/rt/water_compose.hlsl`, WATER_RF_EDGE) replaces the probe term by the
  traced one with D-RF's screen-edge fade w (rf_edge of the border): C' = C + w (A L - B), instead of all or nothing.
  Without it a water pixel switches from the traced image to the probe the frame its hit leaves the screen, and
  under a turn that border moves every frame.

| Job | Save | Plan | Mode |
|---|---|---|---|
| `wf2-s8-fg2` | Water Queens House | 199 s, 6 PNG, 5 K16 bursts | `visible_unfocused_fg` (the user is at the PC) |

Phases: `warmup` 20 s; `cost-off-a` / `cost-rr` / `cost-edge` / `cost-off-b` 20 s, each followed by a 6 s `shot-*`
(one trace, exposure held); `diag-view-mask` (rt_debug 21 trace at the static pose) and `diag-view-mask-rot` (the same
3 s into the same turn as the bursts), `ret-mask` back; `cal-static` (the static K16 calibration burst);
`rot-off-a`, `rot-rr`, `rot-edge`, `rot-off-b`: 6 s turns, all direction +1 from the same pose, burst at 3 s; between
them `ret-*` turns back (direction -1, same length) that set the next state. A plan step's `set` is applied at its
`capture_at` (scripts/auto-run.ps1), so wf1's rot-on burst started at its own toggle; here every key changes at the
start of the ret-* phase, 9 s before the burst.

## Gates
- Validity: every expect line, among them the `WATER_RR_HIT=1 ... water_reflect: built` and
  `WATER_RF_EDGE=1 ... water_compose: built` lines and the five `[pan] burst 9210x complete` lines.
- Energy: `py -3 -m tools.m4_promotion_gate <log>`: shot-rr and shot-edge within the band of the OFF shots (predicted
  null), read with the job's `energy_fs` "water-compose": 5c3fdabc (before the water) plus, in the same frame, the
  scene image after each water pass's compose minus the water pass's own dump (tools/m4_promotion_gate.py
  WATER_COMPOSE). The plain after-water mean swings 2.5% with the water FS's sun glints (PLAN_M5_WATER M5.3c).
- Cost: `gi: GPU cost` / `[water] compose GPU` ON against the OFF mean: no change expected.
- Flicker: `py -3 -m scripts.qa.pan_artifacts <the 5 burst dirs> --out <dir> --mask 92100=<diag-view-mask PNG>
  --mask 92101=<diag-view-mask-rot PNG> ...` (the scene-colour PNG of each mask trace, after the water pass):
  `water_mask.luma_rate` (transient pixel-frames per calibrated pixel-frame inside the traced water) and the whole-frame
  rate of each rot-* ON burst against both rot-off bursts; |rot-off-a - rot-off-b| is the noise. Judge GPU ms and
  energy only, not frame time (unfocused mode). Disk: about 11.5 GB of bursts (5 x 2.3 GB; over the 2 GB lean budget:
  a rotation flicker needs them); delete them after the analysis.
- Look: the user's own check, Ultra with the key on, rotating over the harbour.

Offline check: `powershell -File tools\live_batches\water_flicker\dryrun.ps1 [-Out <scratch dir>]`.
