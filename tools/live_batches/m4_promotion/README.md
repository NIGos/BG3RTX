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
| `m4p3-s6-fg2` | Adamantine Forge | 306 s, 10 PNG | emitters: `rt_hit_emission` OFF/ON/OFF (expect: rf tier b emissive > 0); rf bias 1.09 |
| `m4p4-s8-fg2` | Water Queens House | 254 s, 8 PNG | reflections: tier (b) 24% shaded; rf bias 0.93 |

Phases: `warmup-on` 10 s (compiles every variant), `diag-on` 20 s (bundle ON, stats), `cost-off-a` 20 + `shot-off-a`,
cumulative step PNGs `shot-step-gi` (+gi_hit_direct), `shot-step-sun` (+gi_hit_sun), `shot-step-rf` (+rf_hit_shade),
`shot-step-inst` (+instanced), then `cost-on` 20 + `shot-on` (+rt_emissive, +rt_hit_emission), `cost-off-b` 20 +
`shot-off-b`, `restore` 5 s. S6 and S8 add `cost-ao0` after `cost-on` (bundle ON with `hit_ao_rays=0`: the cost
of the AO rays at hits; every other phase sets `hit_ao_rays=2`); with a core that has it, diag-on also logs
`rf hit shade boxes` (L_b's projector part). S6 adds `cost-em-off-a` / `shot-em-off-a` before `cost-on` and `cost-em-off-b` /
`shot-em-off-b` after it (bundle ON with `rt_hit_emission=0`, capture on). Shots: 6 s, trace at 4 s.
S6 and S8 also measure `rf_hit_gi` (docs/TRACK_D_REFLECTIONS.md 14 "Indirect diffuse at hits"; needs a core with it,
every other phase sets `rf_hit_gi=0 ls_box=0`): `warmup-gi` 6 s after warmup-on (compiles the RF_HIT_GI variant and
the ls_box=2 pass), then after diag-on `diag-gi1` 25 s (bundle ON + `rf_hit_gi=1`), `diag-gi2` 20 s (`rf_hit_gi=2`:
the tier (a) check from the screen's own GI) and `diag-gi1-box2` 20 s (`rf_hit_gi=1 ls_box=2`), each with `rf_stats=1`
only; after cost-ao0 `cost-gi1` 20 + `shot-gi1` (bundle ON + `rf_hit_gi=1`; prediction in predicted.json). Deciding:
the `rf hit gi` ratio L_b / L_a of each diag-gi window against the user's gate (tier (a) bias within the game's own
spread); expected S8 0.925 -> 1.0-1.03, S6 1.089 -> 1.15-1.25 (S6's excess is in the direct term).

Validity: the expect lines (stride-64 XFB, `[geo] RT capture ... of a 1024 MB buffer`, cascade `of 427 MB`, D-EM
capture available, `[hit material instanced] ... key 1`, `hit material`, `GI hit direct`, `GI hit sun`, `rf hit
shade` + `terms`, gi trace and rf pass medians; camp: sun traced lit > 0; S6: `hit emission` rf tier b emissive > 0).

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
- Participation and bias (diag-on): hit material qualified share, GI hit direct / sun, rf hit shade tier (b) share
  and tier (a) L_b / L_a with mean (L_b - L_a) in standard errors, hit emission, instanced clip rejects (< 0.04%).
- Capacity during the plan: alpha made opaque, opaque dropped, cascade buffer full, all 0 at 1024/427.
Merge `m4-promotion` with merge-green only if all four runs are valid and the energy and capacity gates pass.

Duration: plans 834 s, with 4 loads about 22 min (dry-run estimate; the dry run allows 23 min). Disk: 32 PNG traces, about 40 MB each
measured (the estimate uses 60) plus logs: 1.2-1.8 GB; delete the trace folders after the script has run.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `m4p` and receipt
`m4-promotion.json`.

Offline check: `powershell -File tools\live_batches\m4_promotion\dryrun.ps1 [-Out <scratch dir>]`. Last result
2026-11-01 (with the rf_hit_gi windows): all checks passed (Windows PowerShell 5), 21.9 min, 1960 MB.
