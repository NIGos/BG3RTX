# HANDOFF: D-EM emissive geometry capture (branch `worktree-agent-a2212c228ec492214`)

The design, the data format and both rounds of in-game results are in `docs/TRACK_D_RT.md`
section 12 (12.7 is the latest round). The status row is in `docs/QUALITY.md`
("D-EM data").

The tree builds, `settings_test` passes (120 checks), and the shaders compile with DXC.
Main is merged up to `8f8df1f`.

## State after em6-WQ (`7f3f89a`, Water Queens House, run by the coordinator)

- Coverage 99.6%; total ours/game 1.073; build median 0.399 ms; effects passes 0.067 ms.
- `159a6211` Pulse_VT is still 0.000 over 394 px. Its copy runs: slot value 0.000406901,
  constant over 2700 reads.
- `222986b8` 1.258. `7123ab1d` Base_Fresnel_GM_VT reads about 10, over 10 px.
- The `rt_emissive=0` window never closed.

## Fixes in this round (offline, not yet run; TRACK_D 13.11)

1. **`159a6211`: the shader takes the copied word as is.** The decompiled FS writes
   `mix(c·glow.r, glow.rgb, floor(m4)) · m5 · pulse · _7._m0[0u]`, with set 2 b0 a
   `float[]` std430 SSBO. There is no `exp2`, no ×1000 and no unit change, and our model is
   the same expression.
   - The copy reads word 0 at the binding's offset, with the dynamic offset applied
     (`query_binding`). That is the word the draw reads, unless the binding we query is not
     the one the draw uses.
   - A ratio under 0.0005 needs the game's effective factor to be about 1 / 4e-4 (2458)
     times ours. That points to the copied word being the wrong one, or to the game's pixels
     there coming from another emitter (a decal over the same surface).
   - New diagnostics in the `[em] luminance scale copies` line:
     - the latest source's offset and range;
     - its first 4 raw words;
     - per family, the mean luminance per checked pixel, ours and the game's, and ours
       before the scale.

     If ours before the scale ≈ the game's, the word is wrong (a word nearer 1 should appear
     among words 0-3, or the offset is off). If ours after the scale ≈ the game's elsewhere,
     the pixels belong to another emitter.
2. **`7123ab1d` Fresnel templates** are flagged approximate: a view-angle term is not
   modelled. It is under 200 px, so not gated.
3. **Main merged** (`99d7a02`, the session-aware queue).
   - Main's sun replace took TRACK_D section 12, so D-EM is now **section 13**.
   - Main uses `rt_debug` 6/7 for the sun's trust and check, so the **D-EM views moved to
     `rt_debug` 16/17/18** (ours / the game's alone / ratio).
   - The statistics-only check still runs every 16 frames whatever `rt_debug` is, so queued
     jobs with `rt_debug=8` still log the `[em]` lines.
4. **The `rt_emissive=0` cost:** measured in a dedicated session job (below). Since
   `7f3f89a`, a window without builds logs `[em] no builds in the last N presents ... ms
   without capture`.

## Serial tests to rerun (session jobs, enqueued in main's `captures/queue/pending/`)

Jobs named `em7-*` share one game per save (`"session": true`): first the check, then
capture off, then capture on. Read the matched lines in `results.md`.

**WQ** (`em7-wq-check`, `em7-wq-off`, `em7-wq-on`). Pass:
- no device loss;
- coverage ≥ 95%;
- `159a6211` within 0.8-1.25. If not, report the `luminance scale copies` line in full
  (offset, range, words 0-3, per-family means): it decides between "wrong word" and
  "another emitter";
- `222986b8` within 0.8-1.25;
- every exact family ≥ 200 px within 0.8-1.25;
- `em7-wq-off` logs `[em] no builds ... ms without capture (N frames)` with N > 0;
- `em7-wq-on`'s effects passes "with capture" minus `em7-wq-off`'s "without" ≤ 0.1 ms;
- build median ≤ 0.5 ms.

**AF** (`em7-af-check` with 90 s of warmup for the calibration, `em7-af-off`, `em7-af-on`).
Pass:
- `304be1c7` and `159a6211` within 0.8-1.25 (exact);
- `1a81f41b` lava and `f298bf4d` lava pool within 0.8-1.25 in the last window (calibrated).
  Report the lava's scale;
- `222986b8` within 0.8-1.25;
- coverage ≥ 95%; soup full 0; left out 0; 0 non-finite;
- report the copies line.

**GF** (`em7-gf-check`). Pass: `56c5e5a0` ENV_Lava captured = draws; forward-pass draws > 0;
lights > 0.

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
