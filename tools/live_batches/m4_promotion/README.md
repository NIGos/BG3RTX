# M4 promotion to Ultra (live_batch m4-promotion), NOT authorized

Decides whether the M4 preset commit (branch rf-hit-box: Ultra's "Hidden-surface shading" at "+ Bounce" and
"Emission at ray hits" at "Hidden hits", other presets Off; docs/PLAN_M4_REFLECTIONS_GI.md "Promotion") is merged.
`rf_hit_shade` (the "+ Reflections" level) is NOT in the bundle: it stays experimental. Lean format of
[../lean_regression/README.md](../lean_regression/README.md): one foreground launch per save, FG 2, every A/B by
toggling keys inside the run, `rt_stats` only in the `diag-on` window, diag-off 20 s `cost-*` windows, one PNG trace
per `shot-*` phase, static camera, the exposure held (`qa_exposure_hold=1`) in every diag, cost and shot window.
Independent jobs (no stop-on-failure). Receipt `m4-promotion.json`; plans go to test-main
`captures\live_batches\m4_promotion\plans`.

Startup (every job): `preset=ultra` (the real preset: RT water on as in main's Ultra) plus the bundle as that level
writes it, explicit: `rt_hit_material=1`, `rt_hit_material_instanced=1`, `rt_hit_material_wind_rgb=1`, `gi_hit_direct=1`,
`gi_hit_sun=1`, `rt_hit_emission=1`, `rt_emissive=1`, `rf_hit_shade=0`, `rt_xfb_mb=1024`, `rt_cascade_mb=427`,
`rt_cascade_casters=1`, `gi_rt=1`, `gi_rt_fallback=0`.
- Bundle ON: `gi_hit_direct=1 gi_hit_sun=1 rt_hit_material_instanced=1 rt_hit_emission=1 rt_emissive=1`.
- Bundle OFF: those live keys 0 (the Ctrl+Shift+F9 live path). `rt_hit_material` and the wind RGB row stay 1
  (restart keys): their startup capture is in both states and is not measured here.

| Job | Save | Plan | Why this scene |
|---|---|---|---|
| `m4p1-camp-fg2` | Campsite - 27h | 149 s, 6 PNG | S3 outdoor: gi_hit_sun gate (expect: `GI hit sun` traced lit > 0), foliage |
| `m4p2-inn-fg2` | The Blushing Mermaid - 96h 39m | 149 s, 6 PNG | interior, many local lights |
| `m4p3-s6-fg2` | Adamantine Forge | 227 s, 8 PNG | emitters: `rt_hit_emission` OFF/ON/OFF (expect: GI hidden emissive > 0) |
| `m4p4-s8-fg2` | Water Queens House | 169 s, 6 PNG | interior, water, instanced foliage |

Phases: `warmup-on` 10 s (compiles the all-on variant), then one 6 s warm-up per step variant (`warmup-step-gi`,
`-step-sun`, `-step-inst`, S6 `-em-off-a`: no step shot follows a DXC compile, the flaw of m4p3 112657), `diag-on`
20 s (bundle ON, stats), `cost-off-a` 20 + `shot-off-a`, cumulative step PNGs `shot-step-gi` (+gi_hit_direct),
`shot-step-sun` (+gi_hit_sun), `shot-step-inst` (+instanced rows), then `cost-on` 20 + `shot-on` (+rt_emissive,
+rt_hit_emission), `cost-off-b` 20 + `shot-off-b`, `restore` 5 s. S6 and S8 add `cost-ao0` after `cost-on` (bundle ON
with `hit_ao_rays=0`: the cost of the AO rays at hits). S6 adds `cost-em-off-a` / `shot-em-off-a` before `cost-on`
and `cost-em-off-b` / `shot-em-off-b` after it (bundle ON with `rt_hit_emission=0`, capture on). Shots: 6 s, trace
at 4 s. Predictions: predicted.json (written from the m4p runs before this batch: every step within its band, null).

Validity: the expect lines (stride-64 XFB, `[geo] RT capture ... of a 1024 MB buffer`, cascade `of 427 MB` (no closing parenthesis: the line now goes on with the demand), D-EM
capture available, `[hit material instanced] ... key 1`, `hit material`, `GI hit direct`, `GI hit sun`, gi trace
median; camp: sun traced lit > 0; every job: `hit emission` GI hidden emissive > 0) and `expect_every`: one trace in
every shot window, scene wait 0 timeouts, RT capture opaque dropped 0.00 and cascade buffer full 0.0 in every cost
window. The S6 diagnostic m4p5 ([s6_diag](s6_diag/README.md)) keeps its replay-pack base variant (no instanced rows:
the pack contract has no defines) and now holds the exposure in its shots, with the scene-wait and capture gates in its 8 s+ windows (a 6 s shot
can end before a complete 120-frame line: m4p5 012922).

## Deciding numbers and gates (`py -3 -m tools.m4_promotion_gate`, after the runs)
The script finds the latest log of each job in `captures\queue\logs`, times the phases from the runner, and prints:
- Energy: float mean luminance of the scene colour before post (pass 5c3fdabc, slot 0, from each trace's
  `dumps.json`; never PNG previews or the bloom chain: TRACK_D_LOCAL_LIGHTS 9.11). ON and each step vs the mean of
  its nearest OFF shots, two-sided against the delta in `predicted.json` (fill it and commit BEFORE queuing; empty =
  no change): pass when |d - predicted| <= t x sigma x sqrt(3/2), sigma from the OFF-OFF pairs of every lean run on
  the save (docs/GATE_AUDIT_20261101.md S4). S6: shot-on also vs em-off-a/em-off-b (key `shot-on@em-off`).
  The script exits 1 on any failed gate (energy, plan drift, capacity). Validity also needs one trace in every
  shot window (`expect_every` event gate; m4p1 111854 lost shot-step-sun and shot-step-rf).
- Cost: every GPU line (frame time, whole GI, gi trace, rf pass, ls, rt passes, scene build, cascade capture) ON
  minus the OFF mean, with the OFF spread; only 300/600-frame windows lying wholly inside the phase count. Reported,
  not gated (quality before frame rate on Ultra; the user decides). Known per key: PLAN_M4 "Promotion".
- Participation (diag-on): hit material qualified share, GI hit direct / sun, hit emission, instanced clip rejects
  (< 0.04%).
- Capacity during the plan: alpha made opaque, opaque dropped, cascade buffer full, all 0 at 1024/427.
Merge the preset commit with merge-green only if all four runs are valid and the energy and capacity gates pass.

Duration: plans 694 s, with 4 loads about 19.6 min (dry-run estimate; the dry run allows 23 min). Disk: 26 PNG traces, about 40 MB each
measured (the estimate uses 60) plus logs: 1.0-1.6 GB; delete the trace folders after the script has run.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `m4p` and receipt
`m4-promotion.json`.

Known flaw of the step shots (m4p3 112657, 2026-11-01): `warmup-on` compiles only the all-on variant, so each
`shot-step-*` toggles to a variant not yet built; the DXC compile (about 4 s, 1-2 fps) ends just before the trace at
4 s, and the trace measures the first 1-2 frames of the new state. At S6 the scene also moves by about 1% per shot
(animated lava, 54.5% of the frame's luminance), so two OFF shots cannot set the band there. Before a rerun: one
warmup phase per step variant (done: the `warmup-step-*` phases), and at S6 more OFF repeats or the lava excluded. S6 diagnostic: [s6_diag](s6_diag/README.md).

Offline check: `powershell -File tools\live_batches\m4_promotion\dryrun.ps1 [-Out <scratch dir>]`. Last result
2026-11-01 (bundle without rf_hit_shade): all checks passed, 19.6 min, 1600 MB; s6_diag 4.5 min, 1030 MB.
