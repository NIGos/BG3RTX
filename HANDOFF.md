# HANDOFF: D-EM emissive geometry capture (branch `worktree-agent-a2212c228ec492214`)

The design, the data format and both rounds of in-game results are in `docs/TRACK_D_RT.md`
section 12 (12.7 is the latest round). The status row is in `docs/QUALITY.md`
("D-EM data").

The tree builds, `settings_test` passes (120 checks), and the shaders compile with DXC.
Main is merged up to `8f8df1f`.

## State after em9 and the merge with main `253e437` (TRACK_D 13.14)

- **Pass:**
  - validation (no IDs beyond main's baseline);
  - WQ (coverage 98.5-99.8%, totals 0.80-1.05 from a fresh load, `222986b8` 1.02);
  - GF lava 1.035 and `304be1c7` 1.04;
  - AF lava 1.02-1.21;
  - no faults in 5 runs.
- **Open:**
  - AF `222986b8` 1.93 and `304be1c7` 0.64: a few hundred bright px inside dark families,
    with other emitters' glow on the same pixels. The gate is now the bright-hit ratio
    (logged as `bright`) and needs one AF run;
  - GF build median 0.616 ms (criterion 0.6).
- **Main merged with SHaRC:** `BG3RTX_HOST_HAS_RT_EMISSIVE` is defined, `sharc_em` is
  compiled in, and the `sharc_*` shaders compile.

## Gates (derived, QUALITY.md R0; TRACK_D 13.15)

**ours/game per family**, evaluated with `python -m scripts.qa.em_gate <run logs>`:
- **Ratio:** `r_w` is the family's ratio in window `w`: the `bright` ratio when it is
  logged, else the all-hit one.
- **Measured temporal noise:** `s_w` is the std of ln r_w over the windows after the first,
  at the run's fixed view.
- **Counting noise:** N is the bright px per frame. It is an upper bound on the independent
  ones, which errs toward failing.
- **Exact families:** σ² = s_w²/W + 1/N on the mean of ln r_w.
- **Approximate (calibrated) families:** σ² = s_w² + 1/N on the last window.
- **Pass:** |ln r| ≤ z·σ, with z two-sided at ALPHA_FW / families (Bonferroni,
  `scripts/qa/thresholds.py`).
- **Replaced:** the old 0.8-1.25 band and the 200 px cut were not derived. Coverage (≥ 95%) is
  review-only until it is derived.

**Build cost:**
- SETTINGS_DESIGN has no per-pass split entry for D-EM, so the 0.6 ms criterion is
  review-only.
- Proposed entry for §5.4 (Ultra):
  - "D-EM data: effects-pass capture + emissive BLAS/TLAS + light list".
  - Measured: capture +0.044 ms, build 0.44-0.62 ms median, so ≈ 0.7 ms.
  - Taken from the static headroom (≈ 5 ms) and the pan headroom (≈ 2.5 ms). It is the input
    of emissive lighting (sampling the game's emitters), which has no other source.
- The allocation is the user's call. Independently, the build can be cut: the draw list and
  triangle counts are stable between frames, and our own command buffers are never dropped,
  so the emissive BLAS can be refit instead of rebuilt. That is planned after em10.

**Applied to em9:**

| Run | Family | Result | Derived verdict |
|---|---|---|---|
| WQ | all 5 gated families | | Pass |
| AF | lava | 1.21 (band 0.68-1.47) | Pass |
| AF | `4d3f7dab`, `8bcd0aff` | | Pass |
| AF | `222986b8` (all-hit) | 1.93 (band 0.87-1.15, N 380) | Fail |
| AF | `304be1c7` (all-hit) | 0.635 (band 0.84-1.19) | Fail |
| AF | particles `10086377` | 1.73 (band 0.65-1.55, N 42) | Fail |
| GF | lava | 1.035 | Pass |
| GF | particles | 1.60 (s_w 0.37) | Pass |
| GF | `304be1c7` | 1.038 (band 0.979-1.021, N 12.8k) | Fail |

The GF `304be1c7` failure is a real +3.8% bias that the counting noise cannot explain at that
N. em10 logs the bright ratios.

## Serial tests to rerun (em10 jobs, already queued)

- **`em10-af-check`:** every gated family passes `scripts.qa.em_gate`, using the bright
  ratio. Coverage is review-only.
- **`em10-af-sharc`:** the log has `sharc: D-EM effects target injected`, and there is no
  fault.

### R4 (when a foundry save exists): S1

Same command and criteria as R3. The foundry families seen in round 1: `20864dc6` exact,
`7085eee7` exact (VT albedo), `aab75fed` approximate (vertex colour), `159a6211` exact.

### R5: cost, validation and motion (as before, on S6)

- **Cost:** from R1's `rt_debug=0` and `rt_emissive=0` windows. Build ≤ 0.5 ms. Effects
  passes with capture minus without ≤ 0.1 ms. The calibration's check (every 16 frames)
  must not show in the frame pacing.
- **Validation:** `auto-run.ps1 -Background -Validation -Save "Adamantine Forge" -Traces 2
  -Set rt_debug=18`. Pass: no IDs beyond main's baseline. In particular, none naming our
  queue submission or acceleration-structure builds, and no layout IDs on the effects
  target.
- **Motion:** `auto-run.ps1 -Background -Move -Save "Adamantine Forge" -Traces 3`. Pass: no
  device loss, and 0 non-finite values in the dumps.

## em10 filtering A/B (queued, TRACK_D 13.16)

The jobs:
- `14-em10-af-filt1` and `14-em10-af-filt2`: in the AF session after `em10-af-check`
  (filter 0).
- `15-em10-gf-filt0`, `15-em10-gf-filt1` and `15-em10-gf-filt2`: a GF session.

What to read: `python -m scripts.qa.em_gate` on each job's log. The mode under which
`222986b8` and `304be1c7` pass the derived gate in both scenes becomes the default. If no
mode does, the filtering hypothesis is rejected.

**Budget:** SETTINGS_DESIGN §5.4 row #8, 0.7 ms from the pan headroom. It is an optimisation target, not a gate. Only the derived ours/game gate decides the merge. The ledger leaves
0.7 ms after SHaRC (0.6) and D-EM.

## em11 (queued): refit, overlap, longer windows (TRACK_D 13.17)

| Job | Mode | Warmup |
|---|---|---|
| `16-em11-af-check` | session | 240 s |
| `16-em11-gf-check` | session | 150 s |
| `16-em11-wq-check` | session | 150 s |
| `16-em11-af-validation` | non-session, `-Validation` | |

Pass criteria:
- **Gate:** every gated family passes `python -m scripts.qa.em_gate` (exact families on the
  bright ratio without the overlapped pixels; calibrated families on the energy ratio).
- **Refit:** `[em] BLAS` shows refits, and there are no faults.
- **Validation:** no IDs beyond main's baseline. In particular, none for acceleration-structure
  updates.

The build median falls toward the 0.7 ms target. That is not a gate.

## em11 result (TRACK_D 13.18)

- **Gate:** every family passes except AF `304be1c7`, at 0.36 over 236 bright px. Its
  overlap is 0 and its pulse phase matches. The suspected cause is the captured UV output
  (the lowest-location vec2) differing from the fragment shader's location-0 UV in AF's
  vertex shader.
- **Validation:** pass.
- **Refit:** WQ build median 0.095 ms. The AF and GF keys changed every frame, because they
  included ring offsets; that is fixed.

## em12 (queued)

- `17-em12-af-check` (session, 240 s) and `17-em12-gf-check` (session, 150 s): the refit share
  and build median with the new key, and the gate again.

## Known gaps

- **Lava texture detail:** the calibration matches the mean per family only. The lava's
  heat field (bright cracks, dark crust) is not in the data; the light list's power is right
  on average.
- **Grymforge's forward lava is not validated:** the reference would be the forward-lit
  target before and after the lava's draw.
- **`aab75fed`** vertex colour (approximate, now calibrated) and the HLOD family.
- **Not captured:** candles and fire (forward particles), and the unit-58 VFX planes.
- **One frame of latency.** Consumers apply `em_shift()`.
