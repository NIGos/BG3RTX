# Regression process: why misses passed, and liveness (2026-11-23)

Evidence: `.scratch/coord/hunt_*.md` and the 32 queue logs of 2026-11-21 in `captures/queue/logs`.

## 1. Why each miss passed every gate

| Miss | How long | Why no gate saw it |
|---|---|---|
| OMM did no work in High and Ultra | about a week, to 2026-11-21 05:00 | `expect` patterns check that `[rt] scene handout: \d+ frames, reserved` exists. The line printed `OMM accelerated commits 0` every second. The `[omm] rejected ... retaining until idle` latch printed once and nothing forbade it. The cost A/B gates compared OMM-off against OMM-off. |
| The asset DB never learned the level | 2+ weeks (since Patch 8 saves) | `sun stats (600 frames, replace 0, 0 roof volumes (level unknown: nothing trusted)` is in all 171 logs. Older jobs had expected `replace 1`, but newer jobs dropped that pattern. Sun replace, far-GI roofs and per-light radii were dead while their keys were on. |
| 17 of 71 VT masks rejected at every launch | since 2026-11-02 | `[vt alpha] ... invalid: skipped` is a load-time line. No job forbade it. `masks loaded` passed with 54. |
| Fresh installs ran High without OMM or masks | since 2026-11-14 | Every test and live run starts from a user file with `preset=`. No test covered a first run with no user file. |
| Release smoke gated water in High | several reruns | The gates were written against the Ultra profile. In High `rt_water_compose=0`, so the smoke failed for test reasons, while the real defects above passed. |

## 2. Root causes

1. **Existence, not value.** `expect` and `expect_every` check that a line exists or matches a fixed regex. A counter stuck at 0 still matches (OMM commits 0; replace 0).
2. **Gates judge A/B deltas, not absolute health.** A/B analyzers check ON against OFF in one run. If the feature is dead in both, the delta is 0 and the gate passes (OMM cost runs; sun replace).
3. **No liveness per preset.** Nothing maps "this key is on in High" to "this line must show work in High". So gates were written against the wrong preset (water in High), and a feature dead in only one preset hides behind the other.
4. **One-off load lines are never judged.** Rejections, latches and fallbacks print once (`invalid: skipped`, `retaining until idle`, `level unknown`). Window gates only look at periodic stats lines.
5. **Gates are written per batch and drift.** Each batch writes its own patterns. `replace 1` was expected in the October jobs and dropped later. Nothing carries a feature's proof from one batch to the next.
6. **No first-run or low-preset path.** Every live starts from a prepared `auto.ini` with a preset. A user with no file, or on Low or Medium, ran a path that no test covered.
7. **No comparison between releases.** Nobody diffed log signatures between builds. A line that changed from `replace 1` to `replace 0`, or a new `invalid: skipped` line, would have shown up at once.

## 3. The fix: one liveness table, checked on every run

- **Spec:** `tools/live_batches/liveness.json`. One row per feature: `when` (the keys, judged per `[cfg] settings read`
  segment), `presets` (where it is on by default; a test checks this against the schema), `kind`:
  - `positive`: the regex value must be > 0, or >= `min`.
  - `appear`: the line must appear.
  - `absent`: the line must not appear (`invalid: skipped`, `retaining until idle`), with a `proof` line that the path ran.
    `level_known` was `absent` of `level unknown` until 2026-11-24; it is now `appear` + `once` of a game level line,
    since the level is legitimately unknown during loads.
  - `only_if` sets a scene precondition (water rows need water passes). `once` marks launch-time lines.
  - 31 rows today, covering 41 keys.
- **Checker:** `tools/live_batches/liveness.py`.
  - It resolves each segment's settings: preset from the read line, then the `knobs` line, then the truncated `keys` line. The rest comes from `bg3rtx.ini` + `release.ini` + the schema.
  - It judges each row per preset group: ALIVE, DEAD or UNPROVEN.
  - Output: one summary line per log, plus a row table for directories. It prints progress every 10 logs or 30 s and caches results in `build/liveness_cache.json`.
  - Modes: `--gate` exits 1 on DEAD. `--rules` checks the AGENTS.md rule.
- **Wired:**
  - (a) `scripts/test-queue.ps1` runs the checker after every run. Its lines and record field `liveness` go to `results.md`/`results.jsonl`, and `<log>.liveness.json` sits next to the kept log. A job with `"liveness": "gate"` is INVALID on a DEAD row, and `"off"` skips the check.
  - (b) The staged release smoke (`tools/live_batches_staged/release_final`) sets `"liveness": "gate"` on both jobs.
  - (c) Offline: `py -3 tools/live_batches/liveness.py captures/queue/logs`.
- **Rule (AGENTS.md, CTest `liveness_test`, run by merge-green):**
  - Every key with a non-zero player value in a preset or by default needs a row (`when` or `covers`) or an `exempt` reason.
  - The 173 keys that were non-zero on 2026-11-23 without a row are listed in `baseline_no_row`. That list may only shrink.

### Result over the 32 logs of 2026-11-21 (no game run)

| Row | ALIVE | DEAD | UNPROVEN | Note |
|---|---|---|---|---|
| omm_handout_commits | 11 | 16 | 3 | DEAD = every run before the 05:00 OMM fix |
| omm_session_latch | 12 | 18 | 0 | the latch line; catches the 2 logs where commits were > 0 before the latch |
| omm_dyn_scenes | 9 | 18 | 3 | no `[omm dyn]` line at all under the latch |
| vt_masks_valid | 0 | 29 | 3 | 17 rejected masks per launch (fixed on main by claude/fix-vt-masks) |
| level_known | 0 | 29 | 3 | `level unknown: nothing trusted` in every log |
| sun_replace_active, roof_volumes | 0 | 25 | 7 | the consequence: replace 0, 0 roof volumes |
| ls_per_light_data | 0 | 17 | 15 | no `ls: per-light data:` line (needs the level) |
| GI, RF, LS, RR, RR post, FG, handout, hit material, cascade, alpha, hair, water x4, emissive, core, asset DB (20 rows) | 18-32 | 0 | 0-3 | no false DEAD |

The three `rel*-smoke` logs (01:31, about 30 s each, ended early) have no DEAD row, only UNPROVEN rows.

Every known miss of the month shows up as DEAD, and no working feature shows as DEAD. On main the level rows stay
DEAD until `assetdb_level_vote` is on, so the release smoke now fails on the defect instead of passing it.

## 4. Recommended next (not implemented)

1. **First-run smoke.** A launcher test that sends `play` from the package `bg3rtx.ini` with no user file and checks the merged map (hunt_tools finding 1 has the harness). A queue job variant with an empty `auto.ini` is the live equivalent. Liveness would then show OMM DEAD for that run.
2. **Low/Medium smoke.** One 2-minute S8 job per preset at release time. The liveness rows already say what Medium must prove (sun, GI, RF, LS, RR).
3. **Log signature diff between releases.** Normalise each log the way `uniq` does (numbers become N, then count line shapes) and diff the release candidate against the last release. New lines (`invalid: skipped`), lost lines (`[omm dyn]`), and lines whose zero/non-zero state flipped are listed for review. It can reuse `liveness.segments`.
4. **Shrink `baseline_no_row`.** Add rows for the next on-by-default features first: `rt_hit_emission`, `rt_native_scalp`, the hair bundle, `rt_scene_compact_holds`, `rt_mesh_pool_trim`.
5. **Fold liveness into the QA harness report** (`python -m scripts.qa`), so a user log from a bug report gets the same table.
