# wsf1 04:31:41, 2026-11-04 — VALID run; Ultra qualification incomplete
- PASS execution: 220 s, 12/12 phases, real Ultra retained, static 0/1/0/1 then E/E/Q/Q; all expect/expect_every/forbid gates pass; restoration completed.
- **Ultra: FAIL (missing evidence, not a demonstrated model defect).** No preset, category or cost-default promotion prepared. Full evidence: RESULT_wsf1_043141.json.
- PASS ownership per complete 120-frame interval: static ON1/ON3 6,999,526 / 6,998,508; E ON 19,172,740–19,183,882; Q ON 18,596,023–18,596,201 weighted pixels.
- PASS coverage: zero/full 0/0; static partial 7,003,878 / 7,002,675; partial=marked=foam texture=edge/flow source in every ON interval; layout/FS-spec/compose-spec mismatch 0. Huge-family native fallback remains.
- PASS partition/invalid: invalid 0 in every ON interval; runtime coefficient residual <= 8 float eps = 9.536743e-7 (maximum residual not logged). Compose invalid 0; max abort share 3.939983e-6 <= 9.312176e-6.
- PASS full-frame post-water float: 8/8 finite, nonfinite 0; exposure 0.0003021 throughout. L/exposure pairs OFF0 546.695/550.109; ON1 563.599/570.167; OFF2 545.962/548.382; ON3 557.426/561.613.
- Phase means 548.402/566.883/547.172/559.519; ON1 +3.486% vs bracketed OFF, ON3 +2.257% vs nearest OFF2 (+2.142% vs pooled OFF); OFF2/OFF0 -0.224%. Model brightness may change; no equal-mean gate.
- FAIL region float evidence: pool rim and open water each unmeasurable; all 8 captures contain 0 .f32 files. dumps.json retains full-frame RGB means only; PNGs cannot supply linear-energy gates.
- FAIL visible persistence proof: both pairs show changed pool highlights, but persistent reflected-column detail on shallow clean water is not resolved against animated wave/glint differences. Crops are review evidence, not a quality pass.
- PASS model scope: native foam c plus clean (1-c)*[E+(1-E)*a+(1-E)*(1-a)] conserves coefficients; interface survives a=0; direct F(L.H) and native rough DFG remain distinct. No exact-LUT/medium claim.
- PASS compose reporting: exclude intervals crossing settings/motion readiness. Static ON delta +0.0255 ms; E +0.0245, Q +0.0235 ms descriptively (successive turn windows cover different views).
| Window / state | Complete intervals | Compose / copy / dispatch median (ms) |
|---|---:|---:|
| static-0 OFF | 1 | 0.434 / 0.022 / 0.411 |
| static-1 ON | 1 | 0.459 / 0.022 / 0.436 |
| static-2 OFF | 1 | 0.433 / 0.022 / 0.411 |
| static-3 ON | 1 | 0.459 / 0.022 / 0.436 |
| turn-0 E OFF | 2 | 0.574 / 0.023 / 0.551 |
| turn-1 E ON | 2 | 0.5985 / 0.023 / 0.5755 |
| turn-2 Q OFF | 3 | 0.591 / 0.023 / 0.568 |
| turn-3 Q ON | 2 | 0.6145 / 0.023 / 0.5915 |
- FAIL FS/whole-water cost evidence: 0 forward-FS timer samples; compose excludes dual FS evaluation, so total cost congruity is unknown. This timer limit is allowed by predicted.json, but cannot answer total-cost qualification.
- Crops: review-main/captures/offline/wsf1/{pool_rim_off_on,pool_rim_repeat_off_on,open_water_off_on}.png; same fixed PNG mapping, OFF left/ON right, frame IDs labelled; no game or Git metadata writes.
- Cleanup: deleted 0; no raw float captures exist. Keep dumps.json/frame.json/PNGs used by existing gates and review. Reproducer: captures/offline/wsf1/analyze.py; original main captures/offline parent is absent and sandbox creation was denied, so artifacts use this worktree.
