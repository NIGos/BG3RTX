# Gate audit 2026-11-01 (section of META_AUDIT_20261101, main 84e9ac1)

Every live gate in use, checked against today's queue logs. No game, no build; scripts and job JSON only.
Sources: the `expect`, `expect_every`, `forbid`, `expect_runner` lines of the 32 jobs in
`tools/live_batches/*/jobs` (525 expect, 285 forbid, 203 expect_runner, 5 expect_every lines before this
branch), the gate scripts (`tools/m4_promotion_gate.py`, `scripts/queue_gates.py`, `scripts/test-queue.ps1`,
`scripts/qa/*.py`), and the gate statements in PLAN_M4 "Promotion", PLAN_M5_WATER and the TRACK docs.

Method: today's job gates were replayed on the kept logs (`captures/queue/logs`, runner and plan included)
and `m4_promotion_gate` on every lean run with shot phases (42 logs). "Can fail" means a known-bad log
fails it; "cannot fail" means it passes a known-bad log or passes by construction.

Columns: (a) claim; (b) threshold source; (c) can fail, with the evidence; (d) reads the right line/buffer;
(e) one window or every window; (f) one-sided energy; (g) single scene. Class = before this branch;
"now" = after it.

## 1. Results in one paragraph

70 gates (families counted once): **30 OK, 28 weak, 12 broken**. This branch fixes 9 of the 12 broken
and 4 weak ones (after it: 43 OK, 24 weak, 3 broken; the 3 left need a user decision or a derivation). The three
worst were in the energy gate: it read a plan file edited after the run (m4p3 / m4p4 labels one phase
off, so every comparison was INCOMPLETE), its tolerance was a one-sample range, and it was one-sided.
Read correctly, **m4p3 S6 shows the M4 bundle steps darker by 1.05%, 1.82% and 3.10%** (in-run OFF spread
0.07%, derived band 1.73%), and **m5w4 shows +2.20% (141148) and +2.75% (121950) for a key that only
removes light** (OFF spread 0.02% / 0.18%, band 1.17%). Both passed or hid behind the old rules.
**Exposure recheck (section 7):** no run today held the eye adaptation. At a fixed exposure m5w4 is
-0.12% / -0.55% (the "brightening" was the adaptation: s1b13[0] +2.9% / +2.8% in the ON shot), gf1 -0.14%
(was +3.97%), gf2 -0.32% (was +2.87%). The S6 darkening is real: the S6 exposure did not move (ratio 1.0000).

## 2. Top 10 issues

1. **Energy gate tolerance is a one-sample range** (S4). |OFF-b - OFF-a| flags 46% of pure noise
   (P(|N(0, 1.5)| > |N(0, 2)|) = 0.456, simulated in the test). lsk1 071324's own OFF repeats fail it:
   off-c +0.61% "brighter", off-d -0.28% "DARKER". Fixed: band t(1 - 0.05 / 2m, dof) x sigma x sqrt(3/2),
   sigma pooled from the OFF-OFF pairs of every lean run on the save (S8: 0.458% from 20 pairs; S6 0.390%
   from 7; inn 0.502% from 5; S14 0.716% from 3; camp 0.156% from 2).
2. **Energy gate is one-sided** (S4, D1). "brighter: explain" passed gf1 +3.97%, gf2 +2.87% (predicted
   -2%), m5w4 +2.2 / +2.75%; "DARKER fails" would fail a correct shadow key (bx1 shot-on-b -2.31%).
   Fixed: two-sided around a `predicted` delta committed before the run (`<batch>/predicted.json`);
   none = no change; a prediction committed after the run start is "post hoc" and does not pass.
3. **Plan drift** (S6, S3). The gate read today's plan; 0a0e672 (13:45) inserted `cost-ao0` and
   `hit_ao_rays=2` after the 11:2x m4p3 / m4p4 runs, so every label after phase 9 was shifted and all
   energy rows read INCOMPLETE ("17 of 18 phases"). Fixed: the runner's `set=` is compared with the plan;
   the matching committed plan (eeca1fd) is used, else PLAN DRIFT fails. Same check in `queue_gates`.
4. **S6 bundle darker, unexplained** (finding from the fixed gate, m4p3 112657 with plan eeca1fd):
   shot-step-gi -1.05%, +sun -1.82%, +inst -3.10% (all `rt_emissive=0`), then shot-on -0.45% and
   shot-em-off-a -0.21% (`rt_emissive=1`). `gi_hit_direct` should only add light. Hypothesis to check
   first: with the D-EM capture off, hidden hits on lava replace a non-zero fallback that carried the glow.
   Not the eye adaptation: s1b13[0] is 0.00136865 in every S6 trace (exposure ratio 1.0000, section 7).
5. **m5w4 brighter for a light-removing key** (finding; **resolved by section 7: eye adaptation**, at a fixed
   exposure -0.12% (121950) / -0.55% (141148), darker as a shadow must be): scene colour +2.20% / +2.75% with sun shadows
   on the water, while `[water] shadows` removes 46.7% of mean C over owned pixels. Either the 5c3fdabc
   dump is not the image the key changes (the water FS `c4e92ccd` and `423e0548` write the same image in
   the trace) or the key changes something outside the water. The M5 owner must explain or predict it.
6. **Non-finite count from the wrong buffer** (S5): summed over every dump of the trace; m4p3's 2917 came
   from a depth dump (`76433aef`, far plane), the scene colour had 0. Fixed: only the scene-colour dump.
7. **The gate script always exited 0** (S7): energy, capacity and drift were printed only. Fixed: exit 1
   on any failure. Capacity now fails m4p2 112258 (`alpha draws made opaque` 0.03 per frame).
8. **Missing traces pass validity** (J5): `[trace] frame N written` is any-line; m4p1 lost shot-step-sun
   and shot-step-rf, m4p2-4 lost shot-step-rf (all four runs). The runner never requested them: phase 6
   (rf_hit_shade on) lasted 11.2 s instead of 6 with a > 100 ms frame (pipeline compile), and the trace
   request was skipped. Fixed for m4p1-4: an `expect_every` event gate (new `"event": true` in queue_gates)
   needs one written trace in every shot window; replayed, it fails m4p1 (2 windows) and m4p2 (1).
9. **Documented gates not encoded** (Q12, Q26): m5w5's sum check (PLAN_M5 M5.4b: within 2^-10 on >= 99%,
   ratio 1 within 0.1%) accepted any share; pl1's `[bvh] audits ... \d+ failed` accepted failures. Fixed:
   m5w5 `expect_every` on both, pl1 `forbid` on `[1-9]\d* failed`. Both pass their good logs (122327: 100.00%,
   0.99999-1.00000; 203538: 0 failed).
10. **Gates that cannot fail by construction or were redefined after the run** (D4, Q16, S9): GI R3
    "0 negative px" (the addition replaces only zero fallbacks: negative is impossible); M5.1 "behind opaque"
    renamed "occluded water" after the run; the M4.2 z gate replaced by "bias within 9%" after measuring
    -7.4% / +9.1%. Not fixed here: replace R3 by the M4.1 per-pixel k sigma test (QA derived); M5.1 needs a
    depth-tolerance derivation; the 9% is the user's decision (top of PROGRESS).

Also found: m5w5 122327 12:25:25 `clamped 46` in one precheck window, which is the PLAN_M5 M5.4b trigger
("do it if m5w5 shows clamped above ~0") for the Q = C - F L record.

## 3. Inventory

### 3.1 Job JSON: validity families (32 jobs)

| # | Gate | (a) claim | (b) threshold | (c) can fail | (d)/(e) | Class | Now |
|---|---|---|---|---|---|---|---|
| J1 | common expects: scene ready, core loaded, RR create, DLSS-G 2.00, TLAS > 0, pace 600, cfg read | the run tested FG 2 + RR + RT | presence | yes (any missing line: INVALID) | right lines; one line enough (validity) | OK | OK |
| J2 | common forbids: crash, device lost, core rejected / faulted, compile failed, RR failed | nothing broke | presence | yes (every line) | every line | OK | OK |
| J3 | expect_runner: phase count, every trace phase requested, `Run ended: done` | the plan ran | presence | yes: m4p1 111854 has no `[motion-trace]` for shot-step-sun / -rf | runner | OK | OK |
| J4 | expect_runner `pointer centred in owned foreground game` | foreground focus | presence | wrongly: m5w4 141148, m5w5, m5w6, m4i 111403 lack it (visible_unfocused copies, which AGENTS.md now prescribes when the user is at the PC); 111403 was marked INVALID for it | the queued copy's gates differ from the repo's | weak | weak |
| J5 | `[trace] frame \d+ written` (24 jobs) | each shot has its trace | presence | no: one trace passes N shots (m4p1 lost 2 of 7) | one line | weak | weak (fixed for m4p1-4: every shot window, event) |
| J6 | feature-on lines (box active, FS patched, exact-k active, gi_far on, key 1 rows, capacity 1024 / 427, D-EM available) | the key really took effect | presence | yes | right lines | OK | OK |
| J7 | per-burst `transport ok` (cb3, cb4, cb7) | each burst complete | presence per burst id | yes | every burst | OK | OK |

### 3.2 Job JSON: quantitative gates

| # | Job / gate | (a) claim | (b) threshold | (c) can fail (evidence) | (d)/(e)/(g) | Class | Now |
|---|---|---|---|---|---|---|---|
| Q1 | m4i clip rejected < 0.04%, every diag-on window | instanced rows reproduce the game's clip | 0.04%: no derivation written | yes: 093913 3.14% (complete window) fails; 074646 0 lines fails | every window (diag-on: the only stats window); S14 only | weak (threshold) | weak |
| Q2 | m4i pairs / instance-order lines | the pairing diagnostics exist | presence | yes (074646, 093913 lack them) | one line | OK | OK |
| Q3 | m4p1 `GI hit sun` traced lit > 0 | sun reaches hidden hits outdoors | > 0 | yes | one line; camp only (S3 gate of M4.3 still not run as specified) | OK | OK |
| Q4 | m4p3 hit emission rf tier b emissive > 0 | emission at hidden hits participates | > 0 | yes | one line; S6 only | OK | OK |
| Q5 | m4p capacity (gate script): opaque dropped, alpha made opaque, cascade full = 0 | no geometry lost at 1024 / 427 MB | 0 (physics: any loss is wrong) | printed only, exit 0 | right lines, whole run | broken (S7) | OK: fails m4p2 0.03 |
| Q6 | m5w3 composed (1[01]\d\|120) of 120, every compose-on window | compose runs every frame | 100 of 120: not derived | yes: 070927 (0-3 composed) fails | every window | weak (threshold) | weak |
| Q7 | m5w3 compose GPU < 0.5 ms, every window | the cost bug stays fixed | budget (0.5 ms; not cited) | yes: 083416 7.9 ms fails | every window | OK | OK |
| Q8 | m5w3 compose energy / ratio lines | report the darkening | presence | no: -0.35..-0.65% and ratio 0.31 accepted on explanation | in-frame delta exists but is not compared with the scene colour | weak | weak (predicted.json now possible) |
| Q9 | m5w4 sun records traced > 0, every window | the shadow rays run | > 0 | yes | every window | OK | OK |
| Q10 | m5w4 clamped < 120 per window | the R = C - D fix removed the specks | "below one per frame": not derived (test 4j predicts 0 up to half rounding) | yes: 121950 fails | every window | weak (threshold) | weak |
| Q11 | m5w4 / m5w5 / m5w6 compose ran | compose on most frames | as Q6 | was one-window `expect` | one window | weak | OK (every window, added) |
| Q12 | m5w5 local-light sum check | our light sum equals the FS's | doc: >= 99% within 2^-10, ratio within 0.1% (not derived; offline 100%) | no: any share matched | every value accepted | broken | OK (every window), threshold weak |
| Q13 | m5w5 clamped | no specks from F L | none | not gated | 46 in one window (122327) | weak | weak (trigger met) |
| Q14 | m5w6 refraction transmissive > 0, no usable copy 0 | refraction ran with the copy | 0 / > 0 | one-window | one window | weak | OK (every window) |
| Q15 | m5w2 water px / in front > 0 | water in view | > 0 | yes | one line | OK | OK |
| Q16 | mm2 coverage "behind opaque" (M5.1 violations) | water depth matches the bed | "derived depth tolerance": redefined after the run | no: behind opaque became "occluded water" | | broken | broken (needs the tolerance) |
| Q17 | mm1 GI hit direct nonzero > 0 | participation | > 0 | yes | the M4.1 per-pixel test is not in the job | OK | OK |
| Q18 | lr1 / lr2 / lr3 / m4r1-inn / pl1 `primary-ray check match` | RT primary hits = raster | presence only (98.97% at lr2 not bounded) | no | | weak | weak |
| Q19 | lr3 `ls check` | D-LS output = reference | presence (9.3% of px differ > 1e-5) | no | | weak | weak |
| Q20 | cb4-6 `ls check` bit-exact (expect + forbid) | same arithmetic, same bits | 0 px (derived: identical code path) | yes: cb5 185553 29.4M px differ, INVALID | every line (forbid) | OK | OK |
| Q21 | bx / cb box stats evaluated, traced > 0 | box lights participate | > 0 | yes (cb4 INVALID 10-31) | one line | OK | OK |
| Q22 | box `projector validation` | projector accuracy | none: 0 samples reads like 0 error | no | | weak | weak |
| Q23 | lsk1 over-cap > 0, variant lines | exact-k ran | > 0 | yes | one line; S7 only | OK | OK |
| Q24 | gf1 / gf2 `GI far` line | gi_far ran | presence | yes | one line | OK | OK |
| Q25 | pl1 `[bvh] audits ... \d+ failed` | cached matrices match raster in motion | any failures accepted | no | | broken | OK (forbid `[1-9]\d* failed`) |
| Q26 | pl1 epoch split, fade class | diagnostics exist | presence | yes | | OK | OK |
| Q27 | rc1 replay pack (>= 27 copies, 19 of 19 slots, n / n per binding) | the pack is complete | completeness counts | yes | every line | OK | OK |
| Q28 | ef1 exclusion `0.00 per build`, check cost lines | the pulse family is excluded; both schedules run | 0 / presence | yes | | OK | OK |
| Q29 | m4r1 hidden share > 0, rf debug 0 / 8 | tier (b) domain exists | > 0 | yes | | OK | OK |

### 3.3 Gate scripts

| # | Gate | (a) | (b) | (c) | (d)/(e)/(f) | Class | Now |
|---|---|---|---|---|---|---|---|
| S1 | test-queue `expect` | the run tested its config | presence | yes | any line: one good window hides bad ones (m4i 093913, by design) | weak | weak (use expect_every for stats) |
| S2 | test-queue `forbid` | nothing broke | presence | yes | every line | OK | OK |
| S3 | queue_gates `expect_every` | every complete ON window holds | straddle rule from the log cadence (derived) | yes: m4i 093913, m5w3 070927 / 083416, m5w4 121950 | judged shifted windows after a plan edit; no one-off events | weak | OK (plan drift, `event`) |
| S4 | m4_promotion_gate energy | equal energy, or the predicted change | one-sample OFF range | wrongly both ways (lsk1 OFF repeats; 46% of noise) | right buffer (5c3fdabc float); one-sided | broken | OK (two-sided, predicted, derived band) |
| S5 | m4_promotion_gate non-finite | the scene colour is finite | 0 | wrong buffer: depth far plane (m4p3 2917) | summed all dumps | broken | OK |
| S6 | m4_promotion_gate plan | phases are labelled right | | m4p3 / m4p4 shifted after 0a0e672 | today's plan, not the run's | broken | OK (history / PLAN DRIFT) |
| S7 | m4_promotion_gate exit code | a failed gate fails | | always 0 | | broken | OK |
| S8 | m4_promotion_gate cost | cost reported | OFF range (1 dof) | report, not a gate (user: quality first) | windows wholly inside phases (right) | OK | OK |
| S9 | rf_hit_shade bias (z, then "within 9%") | tier (b) unbiased | z x SE (derived); 9% set after measuring -7.4 / +9.1% | z fails both scenes; 9% cannot fail them | | broken (tuned) | broken (user decision) |
| S10 | em_gate ours / game per family | D-EM matches the game | temporal + counting noise, Bonferroni, t dof (derived) | yes: ef1 304be1c7 0.841 FAIL | not phase-aware (pools ef1's fx_pulse / check phases); particles band 0.10-10.0 (s_w 0.82) | OK | OK (note) |
| S11 | qa detectors: k sigma, dark_fail_area, hard_dark floor, flicker, sun_check_darker | no artifact beyond noise | derived (thresholds.REGISTRY) | yes (qa_detectors_test) | image harness; not replayed today | OK | OK |
| S12 | qa review-only (bright_ratio, dark_*, edge_thr) | rank crops | review | not gates | registry says so | OK | OK |
| S13 | coverage.py | coverage report | none | not a gate; HANDLING table stale (META 1) | | weak | weak |
| S14 | pan_artifacts | ON transients <= the two OFF bursts' bound | measured OFF bursts | yes (pan_artifacts_test) | same run | OK | OK |
| S15 | fg_motion_gate / fg_static_effects / fg_log_audit | FG P1 / P2 | FG 2 decides, retained traces | not exercised by today's logs | TRACK_B: the full quality gate is not passed | weak | weak |
| S16 | rotation_preflight | the exit path works | presence | yes | | OK | OK |
| S17 | lean_dryrun | jobs are lean and valid | structure | yes | did not check `expect_every` windows (now in queue_gates_test for all 9 jobs) | OK | OK |

`rebase_gate` is retired with cb2 (baf1c8e) and not counted.

### 3.4 Gate statements in the plans and TRACK docs

| # | Statement | (a) | (b) | (c) | (e)/(f)/(g) | Class | Now |
|---|---|---|---|---|---|---|---|
| D1 | PLAN_M4 Promotion pass: within the spread or brighter with the step named | bundle energy | OFF range | no (any explained brightening) | one-sided | broken | OK (text: two-sided, predicted) |
| D2 | rt_hit_material qualified share | hit material coverage | none (no floor) | no | S8 / S6 / S14 | weak | weak |
| D3 | gi_hit_direct "0 negative px" (R3) | GI never darker | 0 | no: negative is impossible by construction | static only | broken | broken (use the M4.1 k sigma test) |
| D4 | M4.1 per-pixel GI below OFF beyond k sigma, in motion | no darkening in motion | derived (QA k, OFF-repeat sigma) | yes | never run | weak | weak |
| D5 | M4.3 gi_hit_sun at S3 | sun at hidden hits outdoors | lit > 0 | yes | single scene; partly via m4p1 | weak | weak |
| D6 | M4.4 emission: GI never darker per pixel; rt_debug 16-18 unchanged | emission only adds | derived | yes | not run; m4p3 shows S6 darker steps | weak | weak |
| D7 | PLAN_M5 M5.2 image ON = OFF within the OFF spread | reflections key alone changes no colour | OFF range | wrongly (46% noise) | | weak | OK (derived band; m5w2 140754 +0.01% within 1.17%) |
| D8 | M5.3 P6 Composition / Physics / Motion, one moving run | compose correct in motion | P6 | not run (motion) | single view S8 | weak | weak |
| D9 | M5.5 step 1 ratio of the means reported | refraction is a geometry correction | reported by design | | | OK | OK |
| D10 | TRACK_D_LOCAL_LIGHTS 9.11 ls_k: scene mean within the OFF spread per alternation | exact-k unbiased | OFF range | its own OFF repeats fail it | S7 only | broken | OK (k1 -0.38 / -0.34 / +0.12% within 2.33%) |
| D11 | TRACK_D_EMISSIVE 13.15 coverage >= 95% | D-EM coverage | review only | | | weak | weak |
| D12 | TRACK_D_EMISSIVE ef1 check-sliced cost within the check-off spread | sliced check costs as planned | OFF range | | 1 dof | weak | weak |
| D13 | TRACK_D_REFLECTIONS D-EN4 replication < 1% | replicated lighting = game | 1%: not derived | yes | | weak | weak |
| D14 | TRACK_C_GI gi_far go / no-go "> ~30% far field" | worth promoting | not derived | | | weak | weak |
| D15 | TRACK_D_SUN "zero certified pixels is INVALID" | the sun gate saw data | fail-closed | yes | | OK | OK |
| D16 | TRACK_D_SCENE_BUILD hidden quads "0 / 1 twice in debug 8", rt_alpha_stream speed gate | | | pending, not run | | weak | weak |
| D17 | TRACK_D_LOCAL_LIGHTS box: 3,329 unshadowed box lights, projector validation | box shadows correct | see Q22 | | S6 only | weak | weak |

Counts (rows J, Q, S, D = 70): before 30 OK / 28 weak / 12 broken; after 43 OK / 24 weak / 3 broken.

## 4. Energy gate replay (scene colour 5c3fdabc, new gate; old verdict in brackets)

Raw scene colour (before the exposure correction). Section 7 repeats it at a fixed exposure.

| Run | Comparison | d | Band | Old | New |
|---|---|---|---|---|---|
| m4p3 112657 (S6, plan eeca1fd) | step-gi / +sun / +inst / on | -1.05 / -1.82 / -3.10 / -0.45% | 1.73% | INCOMPLETE (drift) | within / FAIL / FAIL / within; step-rf INCOMPLETE (no trace) |
| m4p4 113146 (S8, plan eeca1fd) | step-gi / +sun / +inst / on | +0.06 / +0.00 / +0.26 / +0.15% | 1.59% | INCOMPLETE | within; step-rf INCOMPLETE |
| m4p1 111854 (camp) | step-gi / +inst / on | -0.04 / -0.00 / -0.12% | 1.89% | within | within; sun, rf INCOMPLETE |
| m4p2 112258 (inn) | gi / +sun / +inst / on | +0.71 / +1.01 / +0.61 / -0.04% | 2.47% | within | within; rf INCOMPLETE; capacity FAIL |
| m4i 074646 / 093913 / 111403 | on | +0.27 / +0.10 / +0.76% | 2.77% | brighter / within / within | within |
| gf1 141602 | on | +3.97% | 0.82% | brighter: explain | FAIL (no prediction) |
| gf2 141905 | on, predicted -2% | +2.87% | 1.17% | brighter: explain | FAIL (brighter than predicted; prediction post hoc) |
| m5w4 121950 / 141148 | on | +2.75 / +2.20% | 1.17% | brighter: explain | FAIL (no prediction) |
| m5w2 140754, m5w3 093518 / 121606, m5w5, m5w6 | on | +0.01, -0.45 / +0.03, -0.04, +0.10% | 1.17% | within / brighter | within |
| bx1 151641 (box shadows) | on-a / on-b | -1.05 / -2.31% | 1.36% | DARKER / DARKER | within / FAIL until predicted |
| lsk1 071324 | k1, k2, k1-b, k1-c | -0.38, +0.04, -0.34, +0.12% | 2.33% | DARKER, within, (OFF repeats flagged) | within |

The band is wide where few OFF pairs exist (camp dof 2, S14 dof 3, inn dof 5): to resolve 0.3% effects a run
needs about 3 OFF shots (OFF / ON / OFF / ON / OFF), not 2. S8's sigma (0.458%) is dominated by a bimodal OFF-b
(about +1% in m4r1 and three m5w runs): a scene state that changes during the run, worth finding.

## 5. What changed on this branch

- `tools/m4_promotion_gate.py`: two-sided energy with `predicted.json` and the post-hoc check; sigma pooled from
  OFF-OFF pairs per save, t band with Bonferroni; nearest-OFF comparisons (lsk1 alternations, bx one-sided);
  scene-colour-only non-finite count; plan drift with git-history recovery; step increments printed; exit 1
  on failure; capacity fails above 0.
- `scripts/queue_gates.py`: `"event": true` gates; plan drift fails windowed gates.
- Jobs: m4p1-4 trace in every shot window; m5w4 / m5w5 / m5w6 compose every window; m5w5 sum check and ratio;
  m5w6 refraction every window; pl1 forbid on failed audits. `tools/live_batches/gi_far/predicted.json` (gf2
  -2%, post hoc), `tools/live_batches/m4_promotion/predicted.json` (empty, to fill before queuing).
- Tests: `tests/m4_promotion_gate_test.py` (now in CTest), `tests/queue_gates_test.py` (event, drift, all jobs'
  windows). Dry runs: m4_promotion, m5_water, pool_lag all checks passed.

## 6. Still open (owners)

- Explain or predict the S6 bundle darkening (M4) and the m5w4 brightening (M5) before any promotion.
- Derive Q1 (0.04%), Q6 (100 of 120), Q10 (120 per window), Q12 (99%, 0.1%), Q18 / Q19 floors; replace D3 by D4;
  derive Q16's depth tolerance; S9 stays the user's call.
- J4: let `ConvertTo-QueueWindowJob` swap the foreground pointer line for the unfocused one, so the repo's
  gates are the ones that run (ps1 change, not done here).
- `scripts/auto-run.ps1` skips a phase's `trace_at` when a compile stall carries the loop past the phase
  (all four m4p runs lost shot-step-rf): take the trace late rather than never, or warm the variant first.

## 7. Energy recheck at a fixed exposure (branch worktree-agent-a80bee797afaacedd)

The lighting is pre-exposed by s1b13[0] = 0.8333 / 2^EV, so the eye adaptation scales the scene colour.
**No run today held it** (0 `exposure held` lines in all 28 logs). `tools/m4_promotion_gate.py` now divides
each trace's scene colour by the `[rrpost] exposure probe` s1b13[0] of its frame (found for every trace
today), then applies the two-sided band of section 2 (OFF-OFF noise pairs normalised the same way).
Exposure = s1b13[0] ON / mean OFF. Band: t x sigma x sqrt(3/2), sigma pooled per save. "Before" is the
verdict written today (docs, or the old OFF-pair rule); "raw gate" is section 4.

| Run | Comparison | Raw | Exposure | Fixed exposure | In-run OFF spread | Band | Held | Before | Raw gate | After |
|---|---|---|---|---|---|---|---|---|---|---|
| gf1 141602 camp | on | +3.97% | 1.0411 | **-0.14%** | 0.11% | 0.75% | no | brighter: explain | FAIL | **within** |
| gf2 141905 S8 | on, predicted -2.0% | +2.87% | 1.0320 | **-0.32%** | 0.21% | 1.56% | no | brighter, unexplained (keep off) | FAIL | FAIL: 1.68% above the prediction (post hoc) |
| m5w4 121950 S8 | on (D record) | +2.75% | 1.0288 | **-0.12%** | 0.13% | 1.56% | no | brighter: explain | FAIL | **within** |
| m5w4 141148 S8 | on (R = C - D) | +2.20% | 1.0276 | **-0.55%** | 0.01% | 1.56% | no | brighter: explain | FAIL | **within, darker** (needs a prediction) |
| m4p3 112657 S6 | step-gi / +sun / +inst / on / on vs em-off | -1.05 / -1.82 / -3.10 / -0.45 / +0.16% | 1.0000 (all) | same | 0.07% | 1.73% | no | INCOMPLETE (drift) | within / FAIL / FAIL / within / within | unchanged: **the S6 darkening is real** |
| m4p4 113146 S8 | step-gi / +sun / +inst / on | +0.06 / +0.00 / +0.26 / +0.15% | 1.0004 / 0.9997 / 1.0038 / 1.0030 | +0.02 / +0.03 / -0.12 / -0.15% | 0.14% | 2.13% | no | INCOMPLETE | within | within (+inst and on flip sign) |
| m4p1 111854 camp | step-gi / +inst / on | -0.04 / -0.00 / -0.12% | 0.9987 / 1.0006 / 0.9963 | +0.09 / -0.06 / +0.25% | 0.26% | 1.73% | no | within | within | within |
| m4p2 112258 inn | gi / +sun / +inst / on | +0.71 / +1.01 / +0.61 / -0.04% | 1.0000 | same | 1.15% | 2.47% | no | within | within | within (capacity FAIL stays) |
| bx1 151641 S6 | on-a / on-b | -1.05 / -2.31% | 1.0000 | same | 0.01% | 1.36% | no | DARKER | within / FAIL | unchanged (shadows: needs a prediction) |
| bx2 152015 S7 | on-a / on-b | +0.13 / -0.19% | 1.0000 | same | 0.33% | 1.94% | no | within | within | within |
| lsk1 071324 S7 | k1 / k2 / k1-b / k1-c | -0.38 / +0.04 / -0.34 / +0.12% | 1.0000 | same | 0.20-0.89% | 2.33% | no | equal energy | within | within |
| m4i 074646 / 093913 / 111403 S14 | on | +0.27 / +0.10 / +0.76% | 1.0000 | same | 0.04 / 0.15 / 1.75% | 2.77% | no | brighter, explained / within | within | within |
| m5w3 070927 / 083416 / 093518 / 121606 S8 | on | -0.21 / -0.36 / -0.45 / +0.03% | 1.0005 / 1.0033 / 1.0031 / 1.0030 | -0.25 / -0.68 / -0.76 / -0.26% | 0.37 / 1.23 / 1.57 / 0.34% | 1.56% | no | within (083416 INVALID) | within | within (all darker: the probe leak, as derived) |
| m5w5 122327 S8 | on | -0.04% | 1.0008 | -0.12% | 0.13% | 1.56% | no | passes (ratio 1.0000) | within | within |
| m5w6 131651 S8 | on | +0.10% | 0.9998 | +0.11% | 0.02% | 1.56% | no | +0.07%, within noise | within | within |
| lr1 083902 / 142244 camp | RT on vs off (1 OFF) | +3.83 / +3.67% | 1.0140 / 1.0088 | +2.39 / +2.77% | - | - | no | no energy gate | - | no energy gate (RT changes the lighting) |
| lr2 084249 inn / lr3 084746 S6 | RT on vs off / box on vs off (1 OFF) | -0.05 / +0.38% | 1.0000 | same | - | - | no | no energy gate | - | no energy gate |

lr1 / lr2 / lr3 02:2x and lsk1 062607: trace folders deleted, not recomputable. m4p1-4 shot-step-rf (and m4p1
shot-step-sun): no trace (issue 8).

**Verdicts that change with the exposure correction:**
- gf1 camp: FAIL ("brighter") -> within. gi_far adds no measurable energy at the camp (-0.14%).
- gf2 S8: the sign flips (+2.87% -> -0.32%, the sign of the offline -2.0%). Still FAIL against -2.0%, 0.12%
  beyond the band; "live energy unexplained" becomes "live darkening about 6x smaller than offline".
- m5w4 121950 / 141148: FAIL (brighter for a shadow key) -> within; the image gets darker, as a shadow must.
  Finding 5 was the eye adaptation, not a wrong buffer.
- Old OFF-pair rule only (reference now): m4p4 shot-on within -> DARKER; gf1 and m5w4 brighter -> DARKER.

**Not the eye adaptation:** S6 (m4p3, bx1), S7 / inn (bx2, lsk1, m4p2) and S14 (m4i) kept one s1b13[0]
in every trace of a run (exposure ratio exactly 1.0000), so those verdicts stand: the S6 M4 bundle darkening
(-1.82 / -3.10%) and the bx1 box-shadow -2.31% are real changes of the lit image.

**Pool noise.** With normalised pairs the S8 sigma is 0.612% (0.458% raw). The bimodal OFF-b is not the
adaptation: in m4r1 (10-31, four runs) s1b13[0] fell 0.4% while the normalised OFF-b rose 1.3-1.7%. Without
the m4r1 and m5w3 083416 / 093518 pairs (14 left) sigma is 0.26% and the band 0.68%: m5w4 141148 (-0.55%)
is still within, gf2 still fails. The scene state that changes OFF-b at S8 is still to be found.

**Changes.** All 17 energy-gated lean plans (box_lights, ls_exact_k, m4_instanced, m4_m5_first, m4_promotion,
m4_round1, m5_water; gi_far already did) set `qa_exposure_hold=1` from the first cost/shot phase to the
last phase before `restore` (0 before it and in `restore`); their jobs start with `qa_exposure_hold=0`, keep
the `[rrpost] exposure` lines and expect `exposure held` and `hold released`. Lint:
`py -3 scripts/queue_gates.py --lint-exposure-hold` (also in `queue_gates_test`) fails an energy-gated plan
(an ON shot between two OFF shots, as the gate tool compares them) that does not hold from its first to its
last compared shot, holds in phase 0, or lacks the `exposure held` expect line. Dry runs of the nine
batches passed (Windows PowerShell 5). With the hold, the exposure ratio must read 1.0000 (a check of the hold).

## 8. Silent degradations: fixed limits and key combinations (branch worktree-agent-a29080dd15a8a2223)

Inventory of fixed capacities, skips and fallbacks in core/, layer/, shaders/, checked against the user's
run (bg3rtx.log 17:26-17:35, CTY_Main_A, Ultra) and the 56 queue logs of 2026-11-01. "Gated" = a
smoke_ultra or lean-job gate fails when it is hit.

| Limit | Where | Logged | Gated | Hit 2026-11-01 | Status |
|---|---|---|---|---|---|
| Roof volumes `kSunMaxVolumes` 256 | core/sun_trust.h, 3 shader loops | WARNING + `sun stats` | now (forbid) | yes: 359 within 1980 m, 103 dropped on 306/600 frames | **fixed**: 512 > 398 (largest level), `roof_cap_test` |
| Set-1 range guard (InstInfo / vertex / alpha record reads) | rt_common.hlsli:509,842,1325 | `[core] guard:` (cumulative) | now (forbid nonzero) | yes: 0 at frame 574, 19.2e9 by frame 16216 (~1.5e6 per frame) | **fixed (pending live)**: nonzero only in runs whose mesh pool reached 2048 MB (2^31); the pool now stays at 2047 MB or less |
| Water reflections under `rt_cascade_casters` | core/water.cpp | `variant N` only | expect_every | yes: variant 300/300 in all windows | fixed on main by 64c0f5d (18:15, after the run) |
| Water under rt_omm / tileset / scalp / hair / hair origin | core/water.cpp, `kWaterNoVariant` | `skipped: no variant for` | now (forbid) | no (keys off) | by design, matrix-tested; 24 variant requests compile |
| Cascade casters buffer (rt_cascade_mb 427) | layer/geocapture.cpp | `buffer full N` | expect_every (CAPACITY_GATES) | yes: 1.3/frame, 427.0 of 427 MB | **fixed**: grows on overflow to demand + 25%, logged, `demand max` printed |
| RT capture alpha region / opaque buffer | layer/geocapture.cpp | `region full`, `buffer full` | opaque: CAPACITY_GATES | alpha 1.15/frame (load), opaque 0.03/frame | open (capture owner) |
| Emissive soup | layer/emissive.cpp | `soup full` | no | 0.10/build in 13 windows | open, small |
| Hit-material basis pool 256 MB | common/native_hit_pool.h | now `over kBasisCapBytes` | now (forbid) | 240.1 of 256 MB (94%) | logged + gated; cap kept until the over-cap demand is measured |
| Alpha records 8192 / textures 4095 / caches 15 | layer/alphatest.cpp | `records full` etc. | no | no (522 draws, 29 textures max) | ok |
| Emissive records 1024 / tex 256 / inst 16384 | layer/emissive.cpp | yes | no | no | ok |
| Water draws 256, hit-material records 16384 and tables | layer/water.cpp, native_hit_pool.h | `overflow N` | no | no | ok |
| `kMaxCandidates` 4096, loop caps, light-list OOB | rt_common.hlsli | `[core] guard:` | no | no | ok |
| D-LS sun set ring 32 | core/gi.cpp | once | pattern | no | ok |
| Runtime lights 1024, froxel lights 1024, lsSlots 4095, cluster bit fields | ls_lights.h, bg3_lights.hlsli, rt_local_shadows | **silent** | no | max 393 runtime lights | ok today; no log |
| Scene wait: `skipped` | layer/rtscene.cpp:1325 | yes | SCENE_WAIT_GATE (timeouts) | 73 and 168 | by design: after 8 timeouts in a row with no scene in the last 2 frames (menu, loading) the wait probes every 64 frames; both windows end before the level was known (17:27:35), every in-game window has 0 |
| Sun stats "0 roof volumes, scene top -96.8" | core/sun_trust.cpp | now with the reason | no | first window (menu) | by design: fail-closed (replace 0) until the asset DB knows the level; the line now says "level unknown: nothing trusted" |

**Combination matrix** (`variant_matrix_test`, CTest): every pair of the raw keys of the Experimental
knobs plus the water, hit-material, box, D-LS and GI keys. Each gi/ pass gets its request from
`shader_variant::request` (the flags refresh() derives from the keys), each water pass from
`water_request`; every request must be in `shaders/variants.txt`. The only skips are the water passes
under the five `kWaterNoVariant` keys; the test fails if that table changes, if any pair lacks its
variant, or if a preset turns on a skip key. The water requests those variants would make
(`variant_matrix_test <root> --requests`) all compile with `tools/shader_identity.py` (24 of 24), so
routing them is the follow-up. Key-combination fallbacks that are logged once and not variants:
`gi_far` without `gi_hit_sun`, `rf_hit_shade` without `rt_hit_material`, `gi_hit_sun` without a sun set.
Convention: a pass that skips for a missing variant logs `[<area>] <pass> skipped: no variant for <keys>`;
smoke_ultra forbids it (the m5 jobs' old "no water variant" forbids now use the same text).
