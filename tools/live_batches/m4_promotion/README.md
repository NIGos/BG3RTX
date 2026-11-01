# M4 promotion to Ultra (live_batch m4-promotion), NOT authorized

Decides whether the preset branch `m4-promotion` (M4 bundle on in Ultra, docs/PLAN_M4_REFLECTIONS_GI.md
"Promotion") is merged. Lean format of [../lean_regression/README.md](../lean_regression/README.md): one
foreground launch per save, FG 2, every A/B by toggling keys inside the run, `rt_stats` + `rf_stats` only in the
`diag-on` window, diag-off 20 s `cost-*` windows, one PNG trace per `shot-*` phase, static camera. Independent jobs
(no stop-on-failure). Receipt `m4-promotion.json`; plans go to test-main `captures\live_batches\m4_promotion\plans`.

Startup (every job): `preset=ultra` plus the bundle as the branch's Ultra sets it: `rt_hit_material=1`,
`rt_hit_material_instanced=1`, `rt_emissive=1`, `rt_xfb_mb=1024`, `rt_cascade_mb=427`, `rt_cascade_casters=1`,
`gi_rt=1`, `gi_rt_fallback=0`. The test tree is main, so the keys are explicit (the preset is not merged).
- Bundle ON: `gi_hit_direct=1 gi_hit_sun=1 rf_hit_shade=1 rt_hit_material_instanced=1 rt_hit_emission=1 rt_emissive=1`.
- Bundle OFF: those five live keys 0 and `rt_emissive=0` (the Ctrl+Shift+F9 live path). `rt_hit_material` stays 1
  (restart key): its startup capture is in both states and is not measured here.

| Job | Save | Plan | Why this scene |
|---|---|---|---|
| `m4p1-camp-fg2` | Campsite - 27h | 137 s, 7 PNG | S3 outdoor: gi_hit_sun gate (expect: `GI hit sun` traced lit > 0), foliage |
| `m4p2-inn-fg2` | The Blushing Mermaid - 96h 39m | 137 s, 7 PNG | interior, many local lights; no M4 key measured here yet |
| `m4p3-s6-fg2` | Adamantine Forge | 189 s, 9 PNG | emitters: `rt_hit_emission` OFF/ON/OFF (expect: rf tier b emissive > 0); rf bias 1.09 |
| `m4p4-s8-fg2` | Water Queens House | 137 s, 7 PNG | reflections: tier (b) 24% shaded; rf bias 0.93 |

Phases: `warmup-on` 10 s (compiles every variant), `diag-on` 20 s (bundle ON, stats), `cost-off-a` 20 + `shot-off-a`,
cumulative step PNGs `shot-step-gi` (+gi_hit_direct), `shot-step-sun` (+gi_hit_sun), `shot-step-rf` (+rf_hit_shade),
`shot-step-inst` (+instanced), then `cost-on` 20 + `shot-on` (+rt_emissive, +rt_hit_emission), `cost-off-b` 20 +
`shot-off-b`, `restore` 5 s. S6 adds `cost-em-off-a` / `shot-em-off-a` before `cost-on` and `cost-em-off-b` /
`shot-em-off-b` after it (bundle ON with `rt_hit_emission=0`, capture on). Shots: 6 s, trace at 4 s.

Validity: the expect lines (stride-64 XFB, `[geo] RT capture ... of a 1024 MB buffer`, cascade `of 427 MB`, D-EM
capture available, `[hit material instanced] ... key 1`, `hit material`, `GI hit direct`, `GI hit sun`, `rf hit
shade` + `terms`, gi trace and rf pass medians; camp: sun traced lit > 0; S6: `hit emission` rf tier b emissive > 0).

## Deciding numbers and gates (`py -3 -m tools.m4_promotion_gate`, after the runs)
The script finds the latest log of each job in `captures\queue\logs`, times the phases from the runner, and prints:
- Energy: float mean luminance of the scene colour before post (pass 5c3fdabc, slot 0, from each trace's
  `dumps.json`; never PNG previews or the bloom chain: TRACK_D_LOCAL_LIGHTS 9.11). ON and each step vs the OFF-a/OFF-b
  mean, against |OFF-b - OFF-a|. Pass: `within`, or `brighter` with the step that carries it (GI hidden-hit bounce
  and emission only add; tier (b) replaces a probe by the shaded hit). `DARKER` beyond the spread fails the energy
  rule. S6: emission ON vs em-off-a/em-off-b, within or brighter.
- Cost: every GPU line (frame time, whole GI, gi trace, rf pass, ls, rt passes, scene build, cascade capture) ON
  minus the OFF mean, with the OFF spread; only 300/600-frame windows lying wholly inside the phase count. Reported,
  not gated (quality before frame rate on Ultra; the user decides). Known per key: PLAN_M4 "Promotion".
- Participation and bias (diag-on): hit material qualified share, GI hit direct / sun, rf hit shade tier (b) share
  and tier (a) L_b / L_a with mean (L_b - L_a) in standard errors, hit emission, instanced clip rejects (< 0.04%).
- Capacity during the plan: alpha made opaque, opaque dropped, cascade buffer full, all 0 at 1024/427.
Merge `m4-promotion` with merge-green only if all four runs are valid and the energy and capacity gates pass.

Duration: plans 600 s, with 4 loads about 18 min (dry-run estimate). Disk: 30 PNG traces, about 40 MB each
measured (the estimate uses 60) plus logs: 1.2-1.8 GB; delete the trace folders after the script has run.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `m4p` and receipt
`m4-promotion.json`.

Offline check: `powershell -File tools\live_batches\m4_promotion\dryrun.ps1 [-Out <scratch dir>]`. Last result
2026-11-01: all checks passed (Windows PowerShell 5).
