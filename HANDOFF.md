# HANDOFF: D-EM emissive geometry capture (branch `worktree-agent-a2212c228ec492214`)

The design, the data format and both rounds of in-game results are in `docs/TRACK_D_RT.md`
section 12 (12.7 is the latest round). The status row is in `docs/QUALITY.md`
("D-EM data").

The tree builds, `settings_test` passes (120 checks), and the shaders compile with DXC.
Main is merged up to `8f8df1f`.

## State after round 2 (T1-T3 on `74b2338`, run by the coordinator)

- **No device loss** in any scene. The per-submission build works with `rt_bvh_split=0`.
- **Adamantine Forge:** the lava (`1a81f41b` FLUID_Lava_Plane_Flowmap) is captured, with
  98.5% coverage. The fixes for its ×4, for Pulse_FX ×0.05 (`304be1c7`), for Pulse_VT 0
  captured (the soup was full) and for the 87k-light overflow are listed below.
- **Grymforge:** its lava is `56c5e5a0` `ENV_Lava:StaticInstancedForward`, drawn in the
  **forward pass**, not the effects target. Forward emissive surfaces are now captured too.
- **`QuickSave_3784`** no longer opens in the foundry. S1 needs another foundry save.

## Fixes in this round (offline, not yet run)

1. **Lava radiance.**
   - The game's lava is `blackbody(T)·m23·(1, 2.5, 0)`, with T from a heat field that XFB
     cannot reproduce (flow noise, vertex colour, distance).
   - We now read the game's blackbody LUT (set 0 b11) at a constant heat
     (`rt_em_lava_heat=0.5`), which gives the right hue.
   - **On-screen calibration** sets the scale (`rt_em_calib=1`). A statistics-only projected
     check runs every 16 frames, and approximate families are scaled by game/ours (damped).
     Exact families are never scaled.
2. **Pulse:** the `_GM_Pulse` fixup (pulse, texture-colour switch) now applies to every
   Pulse template, so `304be1c7` gets it. Only luminance-emissive ones (`_VT`) get the
   global scale.
3. **Soup sized to demand:** at a build it grows to 4× the frame's demand
   (`rt_em_xfb_max_mb=512`).
4. **Light list without drops:** above `rt_em_max_lights`, k consecutive triangles share
   one light. Area and power are summed, the radiance is area-weighted, and power is
   conserved.
5. **Forward-pass emissive surfaces** (FOR variants with the emissive flag, not VFX, water
   or decals) are captured, flagged forward and approximate. They are not validated or
   calibrated against the effects target, because they are not in it.

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
- `left out by rt_em_max_lights` 0, and `triangles per light` ≥ 2 is expected (152k
  triangles);
- `159a6211` captured = draws (3/3);
- `ours/game per family`:
  - `304be1c7` Pulse_FX and `159a6211` Pulse_VT within **0.8-1.25** (exact families);
  - `1a81f41b` lava within **0.8-1.25 in the last window** (calibrated; its `scale` should
    settle near 1/4);
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

### R3: S8 Water Queens House

`scripts/auto-run.ps1 -Background -Save "Water Queens House" -Traces 5 -Set rt_debug=8,dump_png_width=1280`

Pass:
- no device loss;
- coverage ≥ 70%;
- every exact family (not "approx") with ≥ 500 px within 0.8-1.25;
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
