# HANDOFF: D-EM emissive geometry capture (branch `worktree-agent-a2212c228ec492214`)

The design, the data format and both rounds of in-game results are in `docs/TRACK_D_RT.md`
section 12 (12.7 is the latest round). The status row is in `docs/QUALITY.md`
("D-EM data").

The tree builds, `settings_test` passes (120 checks), and the shaders compile with DXC.
Main is merged up to `8f8df1f`.

## State after em8 and the merge with main (TRACK_D 13.13)

- **Pass:**
  - capture-off cost: +0.044 ms (WQ, 0.021 without, 0.065 with);
  - GF load with a soup resize, 2 of 2 runs;
  - `304be1c7`: 0.84 (AF) and 1.04 (GF);
  - AF lava 1.005.
- **`159a6211`** is dark in the game too: its intensity is 0 in AF and GF, and its glow map
  is black at our hits in WQ. That is no model error, and it is now ungated (0 bright px).
- **`222986b8`:** AF is dark (1.5e-4 per hit). WQ moves with the view (1.02-1.28), a mask
  filtering residual.
- **Fixed:**
  - `f298bf4d`: its intensity is now read from CB +16;
  - first calibration after 4 checks (decals were 35× in the first window after a load);
  - EMA weight 1/2 (GF lava 0.75-0.78 while converging);
  - gate on bright hits.
- **Main merged** (`eb54bca`). The emissive scene now has its own acceleration structures
  (`em_as_build`: one BLAS with one geometry per draw, one TLAS, per-slot reuse).
  `bvhsplit.cpp` is main's, unchanged. ABI 9.

## Serial tests to rerun (em9 jobs in main's `captures/queue/pending/`)

- **`em9-gf-check`** (session, the first job of a GF session: a fresh load). Pass:
  - no fault;
  - `1a81f41b` within 0.8-1.25 in the last window;
  - decals and other approximate families calibrated within about 64 frames (no window
    above 3× after the first);
  - build median ≤ 0.6 ms.
- **`em9-wq-check` / `em9-wq-on`** (session; `wq-check` is the session's first job). Pass:
  - coverage ≥ 95%;
  - total ours/game within 0.8-1.25 in every window after the first ~64 frames. The decal
    family had been 35× in the first window of `em8-wq-on`;
  - every exact family with ≥ 200 bright px within 0.8-1.25.
- **`em9-af-check`** (session). Pass:
  - lava and `f298bf4d` within 0.8-1.25 (`f298bf4d` is now non-zero);
  - coverage ≥ 95%.
- **`em9-af-validation`** (non-session, `-Validation`, 2 traces). Pass: no validation IDs
  beyond main's baseline, in particular none on our AS builds (`em_as_build`).

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
