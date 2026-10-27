# HANDOFF: D-EM emissive geometry capture (branch `worktree-agent-a2212c228ec492214`)

Design, data format and the analysis of the first gameplay data: `docs/TRACK_D_RT.md`
section 12. Status row: `docs/QUALITY.md` ("D-EM data").

Main is merged in up to `8f8df1f`, which turns `rt_bvh_split=0` on by default. Build state:
- the tree builds;
- `settings_test` passes (120 checks);
- `em_lights.hlsl` and `em_debug.hlsl` compile with DXC.

**Nothing in this version has run in the game yet.**

## What changed in this round (offline)

**1. Build per game submission** (`em_after_submit`, `layer/layer.cpp` after the game's
`vkQueueSubmit(2)`):
- Draws are kept per command buffer.
- When the game submits the batch holding a frame's effects passes, the layer records its
  own command buffer: one full BLAS per draw, the emissive TLAS and the light list. It
  submits that buffer on the same queue right after the game's batch, fenced, in a ring of 8.
- This groups exactly one frame's draws. The previous designs either mixed frames (the
  DEVICE_LOST) or split each frame across two builds (most of the 10.6% coverage).
- Consumers get the previous frame's scene. The header holds the build camera, and
  `em_shift()` moves the current frame's points into the scene.

**2. Works with `rt_bvh_split=0`.**
- There are no cached meshes, refits or compaction in the emissive scene.
- `em_wanted()` needs only the split's machinery (`bvh_available()`), not `rt_bvh_split`.

**3. Radiance fixes** (TRACK_D 12.2 has the table):
- **`159a6211` Base_GM_Pulse_VT (×3,318).** The luminance global scale (set 2 b0 [0]) is in
  device memory. The CPU read failed and left the factor at 1. It is now bound as a buffer
  and applied on the GPU, for every luminance-emissive material.
- **`7085eee7` Base_BGM_MaskedShadows_VT (×2.0).** Its shader multiplies the glow by the
  virtual-texture albedo (mean ≈ 0.5). That albedo is now reproduced: the record holds the
  VT constants plus the indirection and cache views, and `em_vt_albedo()` does the Granite
  lookup.
- **`aab75fed` ENV_Tileset_BGM (×5.9).** Its colour is a blend of three base maps by vertex
  colour, × glow × m3 × (1 − max(vc)). It is now modelled as base map b2 at uv·m0 × glow ×
  m3, i.e. with vc = 0, the upper bound, flagged approximate. The vertex colour is not
  captured.
- **General rule:** "BGM" templates get the base colour (VT albedo for `_VT`, else the base
  colour map).

**4. Coverage analysis** (TRACK_D 12.5).
- The main cause was the split build. Of the rest:
  - the unit-58 VFX (`e0e3b441`, `f081b803`, `87ea6f1c`: alpha-blended planes, not in the
    DB) and the unit-59 decal added nothing measurable in the foundry capture;
  - the Skydome backdrop (`f12c2004`, excluded) can cover pixels with depth > 0.
- `rt_em_backdrop=1` measures the backdrop's share.

## Serial tests to run (the gate)

Use main's `auto-run.ps1` for every step.

**Live toggles.** Drive them with `scripts/live_phases.py <phases.json>`, started together
with the run:

```
{"start": "\\[trace\\] frame \\d+ written", "timeout": 5400, "after_trace": [
  {"set": {"rt_debug": "6"}}, {"set": {"rt_debug": "7"}}, {"set": {"rt_debug": "0"}},
  {"set": {"rt_emissive": "0"}}, {"set": {"rt_emissive": "1"}}]}
```

**Settings for every run:** `rt=1`, `rt_bvh_split=0` (main's default). Repeat T1 once with
`rt_bvh_split=1` after the split's fix.

**Where to read results.** `bg3rtx.log`, the `[em]` lines, in windows of 300 builds:
- `[em] last N builds`: the census;
- `[em] per build`: triangles, lights, power, GPU;
- `[em] emissive families`;
- `[em] projected check` (only while `rt_debug` is 6-8): coverage and the overall ratio;
- `[em] ours/game per family`.

### T1 to T3: per scene

**T1, S1 Steel Watch Foundry:**
`scripts/auto-run.ps1 -Background -Save "QuickSave_3784" -Traces 5 -Set rt_debug=8,dump_png_width=1280`,
with the phases above.

**T2, S5 Grymforge:** the same with `-Save "Grymforge - 32h 59m"`. Lava.

**T3, S6 Adamantine Forge:** the same with `-Save "Adamantine Forge"`. Lava.

Pass, for each of T1-T3:
1. The level loads and runs: no DEVICE_LOST dialog and no stall.
2. `[em] last N builds`: `left out` all 0.00, `builds skipped (busy)` 0, `global scale
   conflicts` 0, `VT albedo missing` 0.
3. `[em] per build`: `0 non-finite power sums`; `lights` > 0 wherever `emissive` > 0.
4. `[em] ours/game per family`: **0.8-1.25** for every family with ≥ 500 px. This applies
   to `20864dc6`, `159a6211`, `7085eee7`, `222986b8`, `48332370`, and to any new
   non-approximate family. `aab75fed` passes at 0.8-1.6 (vertex colour not modelled; the
   upper bound is expected).
5. `[em] projected check`:
   - **coverage ≥ 70%** in S1 (it was 10.6% with the split build);
   - hits where the game shows < 2% of ours < 5%.
6. **Lava (S5, S6):**
   - the lava's fragment shader appears in `[em] emissive families` with captured = draws;
   - coverage ≥ 70% while the lava is in view.

   If it does not, report the `[em] last N builds` census (engine / unknown / decal /
   other). Lava drawn as forward or VFX is outside this capture and needs a design change,
   not a fix.
7. Optional: rerun with `rt_em_backdrop=1` added to `-Set`. The coverage gain is the
   backdrop's share.

Images: the traces dump the frame with `rt_debug=8` (green: within 25%; red/blue: too
bright/dark; yellow: ours only; magenta: the game's only), `6` (ours) and `7` (the game's
alone).

### T4: cost (S1)

Read it from T1's later windows: `rt_debug=0`, then `rt_emissive=0`.

- `[em] per build ... GPU: build median`. **Pass: ≤ 0.35 ms.**
- `effects passes per frame median ... with capture` against `... without` (the
  `rt_emissive=0` window). **Pass: difference ≤ 0.05 ms.**
- Only valid when `captures/watchdog.log` shows a single game.

### T5: validation (S1)

`auto-run.ps1 -Background -Validation -Save "QuickSave_3784" -Traces 2 -Set rt_debug=8`

**Pass:** `captures/validation.log` has no ID beyond the baseline (unmodified main):
- `UNASSIGNED-non-acquired-swapchain-image-used`
- `VkShaderModuleCreateInfo-pCode-08737`
- `vkCmdCopyBuffer-srcBuffer-00118`
- `vkCmdDraw-None-09600`
- `vkCmdWaitEvents2-pEvents-10788`
- `vkDestroyImage-image-01000` (that last one may be gone in current main)

In particular, none of:
- `VkImageMemoryBarrier-oldLayout-01197` or `vkCmdDispatch-imageLayout-00344` (the old
  effects-target mistake);
- an ID naming our command buffer or queue submission (`vkQueueSubmit-*`,
  `vkCmdBuildAccelerationStructures*`).

### T6: motion and load robustness (S1, then S8)

1. `auto-run.ps1 -Background -Move -Save "QuickSave_3784" -Traces 3`
2. `auto-run.ps1 -Background -Save "Water Queens House" -Traces 3 -Set rt_debug=8`

**Pass:**
- no device loss or stall;
- 0 non-finite values in the dumps (`analyze_trace` table);
- `[em] per build` non-finite 0;
- S8: report the families and coverage. The ratio criteria are those of T1.

## Known gaps

- The vertex colour of `aab75fed` and the HLOD family `b86501b6` (three VT layer channels)
  are approximate.
- Candles and fire are forward-pass particles, and unit-58 VFX planes are alpha-blended,
  not surfaces. Neither is captured.
- One frame of latency: consumers must apply `em_shift()`. Emitters that move get one frame
  of lag.
- The census's per-pass counter relies on a thread-local flag (cosmetic).
