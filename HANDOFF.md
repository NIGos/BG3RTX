# HANDOFF: D-EM emissive geometry capture (branch `worktree-agent-a2212c228ec492214`)

The design, the data format and both rounds of in-game results are in `docs/TRACK_D_RT.md`
section 12 (12.7 is the latest round). The status row is in `docs/QUALITY.md`
("D-EM data").

The tree builds, `settings_test` passes (120 checks), and the shaders compile with DXC.
Main is merged up to `8f8df1f`.

## State after em7 (`61006ba`), with fixes (offline, TRACK_D 13.12)

- **Pass:**
  - decals by default: WQ coverage 97.8-99.0%;
  - AF lava 1.047 calibrated;
  - build 0.39 / 0.43 ms;
  - effects passes with capture 0.066 / 0.096 ms.
- **`159a6211`:** the copied word is right. It is the game's pre-exposure: words s, 1/s,
  log2(1/s), -8. The game's per-pixel value is 0 (WQ) / 0.0066 (AF), ours 0 in both. The
  fault is in our level, map or pulse. A new `[em] record dump` line logs radiance, mode,
  texture, flags, slot, the share of zero hits, ours without the maps, the constant-buffer
  words, the time and the pulse.
- **Fail, now in the record dump:** `222986b8` AF 1.93 / WQ 1.26 (the 64-bit sums did not
  change it) and `304be1c7` AF 0.63.
- **Capture-off timing:** the pass was never marked as an effects pass when capture was off.
  Fixed.
- **GF device loss** right after "soup grown" at the load. The old soup was freed while sets
  and passes still bound it. It is now retired for 16 builds, and conversions use the
  pass's own soup.
- Log lists are split into parts under the 2 KB line limit.

## Serial tests to rerun (em8 jobs in main's `captures/queue/pending/`)

- **`em8-gf-load`** (non-session, 2 runs, `stop_on_dump`). Pass:
  - nv153 = 0 and no stall in both;
  - at most one `soup grown` line per run;
  - `56c5e5a0` captured = draws.
- **`em8-wq-check` / `em8-wq-off` / `em8-wq-on`** (session). Pass:
  - `em8-wq-off` logs `[em] no builds ... without capture (N frames)` with N > 0;
  - with capture minus without ≤ 0.1 ms;
  - coverage ≥ 95%.

  Report the `[em] record dump` lines.
- **`em8-af-check`** (session, 90 s warmup). Pass:
  - lava within 0.8-1.25;
  - coverage ≥ 95%.

  Report the `[em] record dump` lines (`159a6211`, `222986b8`, `304be1c7`). They decide the
  next fix.

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
