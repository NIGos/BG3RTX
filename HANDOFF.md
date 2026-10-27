# HANDOFF: D-EM emissive geometry capture (branch `worktree-agent-a2212c228ec492214`)

The design, the data format and both rounds of in-game results are in `docs/TRACK_D_RT.md`
section 12 (12.7 is the latest round). The status row is in `docs/QUALITY.md`
("D-EM data").

The tree builds, `settings_test` passes (120 checks), and the shaders compile with DXC.
Main is merged up to `8f8df1f`.

## State after round 5 (`db5808a`, run by the coordinator)

- No device loss.
- With decals, Water Queen's House coverage is 99.6%.
- Still open:
  - `159a6211` and `f298bf4d` read 0.000;
  - `222986b8` is 1.92 in Adamantine Forge and 1.03 in Water Queen's House;
  - `a201d825` reads 1.41 and `a7caa456` 1.42;
  - the lava calibration moved the wrong way;
  - Water Queen's House had no `rt_emissive=0` window.

## Fixes in this round (offline, not yet run; TRACK_D 12.10)

1. **Decals are captured by default** (`rt_em_decal=1`). They are approximate and
   calibrated.
2. **Check sums**: 64-bit at 1/4096 per pixel. The 32-bit sums at 1/16 could wrap, which is
   the likely cause of the 0.000 values and of `222986b8`'s scene dependence.
3. **New log line, `[em] luminance scale copies`**:
   - copies dispatched and copies owed but not recorded;
   - per family: the slot, the value written on the GPU (min/max), and the records without a
     slot.
4. **Pulse**: the time-average factor when the time is not host-readable (`a201d825`). It is
   counted as `pulse time not host-readable`.
5. **VertCut templates (`a7caa456`)** are flagged approximate, since their vertex-colour mask
   is not captured.
6. **Calibration**: an exponential average in log space of the ratio (weight 1/4), not a full
   step.
7. **Families under 200 px** are marked "not gated".
8. **Log windows** are `rt_timing_frames` presents, not builds. A capture-off window now logs
   `no builds in the last N presents ... ms without capture`.

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

The calibration needs a few windows to settle, so judge the **last** window of each run. Use
`auto_interval=1200` and `-Traces 6` on every run. Each live phase, `rt_emissive=0` included,
then spans at least one 300-present window. Check that the live_phases log reaches the
`rt_emissive=1` step before the run ends.

On every run, also report the `[em] luminance scale copies` line:
- a value of 0 for `159a6211` / `f298bf4d` while their game pixels are bright means the copy
  reads the wrong buffer or offset;
- `owed but not recorded` must be 0.

Families marked "under 200 px: not gated" are not pass criteria.

### R1: S6 Adamantine Forge

`scripts/auto-run.ps1 -Background -Save "Adamantine Forge" -Traces 6 -Set rt_debug=8,dump_png_width=1280,auto_interval=1200`

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
  - `222986b8` within 0.8-1.25 (1.92 last time; expected fixed by the 64-bit sums);
- coverage ≥ 95%;
- 0 non-finite power sums;
- build median ≤ 0.5 ms (152k triangles).

Report the lava's `scale` value.

### R2: S5 Grymforge

`scripts/auto-run.ps1 -Background -Save "Grymforge - 32h 59m" -Traces 6 -Set rt_debug=8,dump_png_width=1280,auto_interval=1200`

Pass:
- no device loss;
- `56c5e5a0` ENV_Lava appears in `emissive families` with captured = draws, and
  `forward-pass emissive draws` > 0;
- `lights` > 0 and power > 0.

Its `ours/game` is not a criterion: forward, not in the effects target, so expect
"forward", a large ratio, scale 1. The `rt_debug=6` dump should show the lava lit by our
data.

### R3: S8 Water Queens House (run first)

`scripts/auto-run.ps1 -Background -Save "Water Queens House" -Traces 6 -Set rt_debug=8,dump_png_width=1280,auto_interval=1200`

Pass:
- no device loss;
- coverage ≥ 70%, and report the "by an emitter in front of the depth" share;
- `159a6211` and `a201d825` within 0.8-1.25 (they read 0.000 before);
- decals are now on by default, so coverage ≥ 95% is expected;
- the `rt_emissive=0` window logs a non-zero "without capture" time;
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
