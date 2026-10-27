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

## Serial tests to rerun (em10 jobs in main's `captures/queue/pending/`)

- **`em10-af-check`** (session, 90 s warmup). Pass:
  - `222986b8` and `304be1c7` with `bright` within 0.8-1.25 (the gate);
  - lava within 0.8-1.25;
  - coverage ≥ 95%.
- **`em10-af-sharc`** (session, `sharc=1,sharc_em=1`). Pass:
  - the log has `sharc: D-EM effects target injected`;
  - no fault.

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

## Known gaps

- **Lava texture detail:** the calibration matches the mean per family only. The lava's
  heat field (bright cracks, dark crust) is not in the data; the light list's power is right
  on average.
- **Grymforge's forward lava is not validated:** the reference would be the forward-lit
  target before and after the lava's draw.
- **`aab75fed`** vertex colour (approximate, now calibrated) and the HLOD family.
- **Not captured:** candles and fire (forward particles), and the unit-58 VFX planes.
- **One frame of latency.** Consumers apply `em_shift()`.
