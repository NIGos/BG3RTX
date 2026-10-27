# HANDOFF: D-EM emissive geometry capture (branch `worktree-agent-a2212c228ec492214`)

The design, the data format and both rounds of in-game results are in `docs/TRACK_D_RT.md`
section 12 (12.7 is the latest round). The status row is in `docs/QUALITY.md`
("D-EM data").

The tree builds, `settings_test` passes (120 checks), and the shaders compile with DXC.
Main is merged up to `8f8df1f`.

## State after round 4 (`502aa29`, run by the coordinator)

No device loss in any scene.
- **S8 Water Queens:** CHAR_BASE_GM 1.02 and Base_GM_VT `9228b9cb` 1.03 pass. `222986b8` is
  at 1.27, the Pulse_VT and two character families read 0.000, and coverage is 63.5%.
- **S6 Adamantine:** coverage 99.8%, but the lava pool (`f298bf4d` ENV_Base_MutipleUV) and
  Pulse_VT read 0.000. The calibration of `1a81f41b` only reached 0.68.
- **S5 Grymforge:** the forward lava passes (1.04), with coverage 100%.

## Fixes in this round (offline, not yet run; TRACK_D 12.9)

1. **Luminance families read 0** (Pulse_VT `159a6211`, lava pool `f298bf4d`). The game
   rewrites its luminance-scale buffer later in the same batch.
   - The value is now copied on the GPU right after each effects pass
     (`shaders/rt/em_copy.hlsl`) into our own slot buffer. Records refer to the slot.
2. **Calibration**: the full correction per window (clamped to ×4), with ≥ 200 px.
   - `f298bf4d`: its glow map reads the second UV, which isn't captured. It is now a
     constant × the noise mean × global, flagged approximate, so it gets calibrated.
3. **Character 0.000 families**: their constants are host-readable (counter 0). The cause
   is moving characters (one frame of latency) and, for VertCut, a vertex-colour mask.
   Under 0.01% of the screen; reported, not fixed.
4. **Coverage measurement**: `rt_em_decal=1` (new) and `rt_em_backdrop=1` capture the
   emissive decals and the backdrop, for measurement only.
5. **Merge:** my `bvhsplit` barrier change is reverted (`dd70778`). Use the split agent's
   `c9297e3`.

## Serial tests to rerun

The phases file is the same as before. Start it with `scripts/live_phases.py` together with
the run:

```
{"start": "\\[trace\\] frame \\d+ written", "timeout": 5400, "after_trace": [
  {"set": {"rt_debug": "6"}}, {"set": {"rt_debug": "7"}}, {"set": {"rt_debug": "0"}},
  {"set": {"rt_emissive": "0"}}, {"set": {"rt_emissive": "1"}}]}
```

Read the results in the `[em]` lines of `bg3rtx.log` (300-build windows):
- `last N builds`: census, left out;
- `soup ... MB, ... dropped by a soup resize; forward-pass emissive draws; triangles per light`;
- `per build`;
- `emissive families`;
- `projected check`;
- `ours/game per family` (ratio, px, approx/forward, current calibration scale).

The calibration needs a few windows to settle, so judge the **last** window of each run. The
first `rt_debug=8` phase is about 1-2 windows; to have more, raise the first
`after_trace` interval, or add `auto_interval=1200` to `-Set`.

### R1: S6 Adamantine Forge

`scripts/auto-run.ps1 -Background -Save "Adamantine Forge" -Traces 5 -Set rt_debug=8,dump_png_width=1280,auto_interval=1200`

Pass:
- no device loss or stall;
- `soup full` 0.00 after the first window, with one "[em] emissive soup grown" line at
  most;
- `invalid soup triangles collapsed by the sanitize pass`: report the value. A non-zero
  value is handled, not a failure, but tell me the families;
- `left out by rt_em_max_lights` 0, and `triangles per light` ≥ 2 is expected (152k
  triangles);
- `159a6211` captured = draws (3/3);
- `ours/game per family`:
  - `304be1c7` Pulse_FX and `159a6211` Pulse_VT within **0.8-1.25** (exact families);
  - `1a81f41b` lava within **0.8-1.25 in the last window** (calibrated);
  - `f298bf4d` ENV_Base_MutipleUV (the lava pool, 1.7M px) within 0.8-1.25 in the last
    window (calibrated; it read 0.000 before);
  - `222986b8`: report the ratio (1.92 last time, not yet explained);
- coverage ≥ 95%;
- 0 non-finite power sums;
- build median ≤ 0.5 ms (152k triangles).

Report the lava's `scale` value.

### R2: S5 Grymforge

`scripts/auto-run.ps1 -Background -Save "Grymforge - 32h 59m" -Traces 5 -Set rt_debug=8,dump_png_width=1280`

Pass:
- no device loss;
- `56c5e5a0` ENV_Lava appears in `emissive families` with captured = draws, and
  `forward-pass emissive draws` > 0;
- `lights` > 0 and power > 0.

Its `ours/game` is not a criterion: forward, not in the effects target, so expect
"forward", a large ratio, scale 1. The `rt_debug=6` dump should show the lava lit by our
data.

### R3: S8 Water Queens House (run first)

`scripts/auto-run.ps1 -Background -Save "Water Queens House" -Traces 5 -Set rt_debug=8,dump_png_width=1280`

Pass:
- no device loss;
- coverage ≥ 70%, and report the "by an emitter in front of the depth" share;
- `159a6211` and `a201d825` within 0.8-1.25 (they read 0.000 before);
- then two short reruns with `-Set rt_debug=8,rt_em_decal=1` and `-Set rt_debug=8,rt_em_backdrop=1`:
  the coverage gain of each is that source's share (report both);
- every exact family (not "approx") with ≥ 500 px within 0.8-1.25;
- report `material constants not host-readable` (> 0 explains `38e91cf3` / `5d837ba1`);
- approximate families within 0.8-1.25 in the last window;
- left out 0.

Report the family list.

### R4 (when a foundry save exists): S1

Same command and criteria as R3. The foundry families seen in round 1: `20864dc6` exact,
`7085eee7` exact (VT albedo), `aab75fed` approximate (vertex colour), `159a6211` exact.

### R5: cost, validation and motion (as before, on S6)

- **Cost:** from R1's `rt_debug=0` and `rt_emissive=0` windows. Build ≤ 0.5 ms. Effects
  passes with capture minus without ≤ 0.1 ms. The calibration's check (every 16 frames)
  must not show in the frame pacing.
- **Validation:** `auto-run.ps1 -Background -Validation -Save "Adamantine Forge" -Traces 2
  -Set rt_debug=8`. Pass: no IDs beyond main's baseline. In particular, none naming our
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
