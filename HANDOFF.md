# HANDOFF: D-EM emissive geometry capture (branch `worktree-agent-a2212c228ec492214`)

Design and data format: `docs/TRACK_D_RT.md` section 12. Status row: `docs/QUALITY.md`
("D-EM data"). Main (`51a2db9`) is merged in; the tree builds, `settings_test` passes
(120 checks), and the new shaders compile with DXC.

## Implemented

- **Identification** (`layer/emissive.cpp`). An effects pass is 1× B10G11R11 + D24S8 at
  render resolution. An emissive draw is one whose FS is an asset-DB EMI material variant
  (`pass == 2`). Decals, skydome backdrops, engine shaders and unknown shaders (the VFX
  `e0e3b441` / `f081b803` / `87ea6f1c`) are counted, not captured. The effects target image
  is taken from the EMI draws.
- **Records**, one per draw: colour × intensity from the DB roles (`GlowColour`,
  `GlowMultiplier`, read from the mapped CB) and the glow map view. Luminance-emissive
  materials are multiplied by the global scale at set 2 b0 [0]. `222986b8` and `159a6211`
  have a family fixup (texture-colour switch, pulse).
- **Geometry.** The effects-pass draws are XFB-captured (`layer/geocapture.cpp`
  `em_pre_draw`) into an emissive soup and converted per pass. The BVH split gains emissive
  scenes 1 and 2 (`layer/bvhsplit.cpp`: `use_scene` over 3 scenes, tokens carry the scene
  and generation, fixups keyed by command buffer and scene). Cached meshes plus per-draw
  BLAS go into a **separate emissive TLAS**, built at the first tiled-lighting dispatch
  from the previous frame index's draws.
- **Light list** (`shaders/rt/em_lights.hlsl`): one `EmLight` per triangle (world corners,
  area, mean radiance, power π·A·lum), capped at `rt_em_max_lights`, with per-group power
  sums. The format and `em_lookup()` are in `shaders/rt/em_common.hlsli` (set 3), and the
  host API is ABI 8 `rt_emissive`.
- **Debug and logs.** `rt_debug=6` shows our emission, `7` the game's effects target alone,
  `8` a ratio heatmap (`core/emissive.cpp`, `shaders/rt/em_debug.hlsl`; one line added to
  `core/gi.cpp`). Log lines: `[em] last N builds ...`, `[em] per build ...` (draws,
  triangles, lights, total power, GPU cost), `[em] emissive families ...`,
  `[em] projected check ...`, `[em] ours/game per family ...`. `rt_em_log=1` adds a line
  per build and per frame.
- **Shader-side changes outside new files:** one constant `kInfoEmissive` in
  `rt_common.hlsli` (outside the alpha functions).

## Verified (numbers)

- **DEVICE_LOST at the level load, found and fixed.** Seen 00:45, 01:58, 02:39, 03:19 (dialog
  text read: "DEVICE_LOST"), 04:12 and 05:09. These are not the shared render stall. Bisected:
  - `rt_em_build=0` loads;
  - per-draw BLAS + TLAS alone, with refits, still loses the device.

  Fix: no refits and no compaction in emissive builds (the game drops lighting command
  buffers around loads), plus build-only-previous-index generations. Afterwards the foundry
  save loaded and ran (run 05:27-05:30).
- **One gameplay window** (foundry load, 05:28), per build:
  - 10.8 emissive draws (8.5 cached → 21 instances, 2.3 per-draw BLAS), 0 left out;
  - 36k triangles, 35.5k lights (429 over the cap), power 1,586, 0 non-finite;
  - build 0.31 ms median (p99 0.34); effects passes with capture 0.011 ms/frame.
- **Projected check** (ours/game):
  - CHAR_BASE_GM 1.21;
  - Base_BGM_MaskedShadows_VT 2.0;
  - ENV_Tileset_BGM 5.9;
  - **Base_GM_Pulse_VT 3,318 (bug)**;
  - coverage of the bright effects-target pixels: 10.6%.
- **Main menu**, several runs: 1 draw, build 0.023 ms, stable.
- **Validation baseline** (unmodified main, menu only):
  - IDs: `non-acquired-swapchain-image-used`, `08737`, `CopyBuffer-00118`, `Draw-09600`,
    `WaitEvents2-10788`, `DestroyImage-01000`.
  - My run (rt_debug=8) added `ImageMemoryBarrier-oldLayout-01197` and
    `Dispatch-imageLayout-00344`. Both came from the effects-target misidentification,
    fixed since but not re-run.

Every timing above may include other agents' games on the GPU, except the 05:27-05:30 run;
check `captures/watchdog.log`.

## In-game tests still needed (the gate)

Run each one alone with main's `auto-run.ps1`. Use live toggles with `scripts/live_phases.py`
and a phases file like the one below (after-trace phases: `rt_debug` 6 → 7 → 0, then
`rt_emissive` 0 → 1):

```
{"start": "\\[trace\\] frame \\d+ written", "timeout": 5400, "after_trace": [
  {"set": {"rt_debug": "6"}}, {"set": {"rt_debug": "7"}}, {"set": {"rt_debug": "0"}},
  {"set": {"rt_emissive": "0"}}, {"set": {"rt_emissive": "1"}}]}
```

1. **Per scene, coverage and the projected check.** Command:
   `scripts/auto-run.ps1 -Background -Save "<save>" -Traces 5 -Set rt_debug=8,dump_png_width=1280`,
   with the phases above.
   - Saves: `QuickSave_3784` (foundry), `Grymforge - 32h 59m`, `Adamantine Forge`,
     `Water Queens House`.
   - Read `[em] last ...` for the draws against the captured ones, and `[em] emissive
     families` and `[em] ours/game per family` for the families.
   - Pass:
     - no DEVICE_LOST and no stall;
     - "left out" 0;
     - 0 non-finite power sums;
     - ours/game within 0.8-1.25 for the families not flagged approximate.
   - Expected fail until fixed: Base_GM_Pulse (×3,318).
   - **Lava:** in Grymforge and the Adamantine Forge, check whether the lava's FS appears in
     `[em] emissive families`. If it doesn't, check the effects-pass census (unknown or
     other) and the forward pass.
2. **Cost.** Use the same run, rt_debug=0, and compare the windows with capture on and off.
   - Read `[em] per build ... GPU: build median` and `effects passes per frame ... with
     capture / without`.
   - Pass: build ≤ 0.35 ms and capture overhead ≤ 0.05 ms in the foundry.
3. **Validation.** Command:
   `auto-run.ps1 -Background -Validation -Save QuickSave_3784 -Traces 2 -Set rt_debug=8`.
   - Pass: the IDs equal the baseline list above. In particular, no `01197` or `00344`.
4. **Motion.** Command: `auto-run.ps1 -Background -Move -Save QuickSave_3784 -Traces 3`.
   - Pass: no device loss and 0 non-finite values in the dumps (analyze_trace).
5. **Level-load robustness.** Repeat test 1 twice in the foundry, the scene that crashed
   before the fix. Pass: both runs load.

## Known issues / next code steps

- Base_GM_Pulse radiance (the global scale read at set 2 b0, or the pulse); colour-map and
  vertex-colour families (`aab75fed`, HLOD).
- The census's "effects passes" counter uses a thread-local flag. It reads 0-2 where the
  frame has 1-3 passes: cosmetic.
- Emissive builds rebuild every BLAS and the TLAS each frame. That is cheap, and refits are
  unsafe in the lighting command buffer.
- The scratchpad scripts that drove my runs: `em_scene.ps1` and `em_phases.json` in the
  session scratchpad. They are not needed: use the commands above.
