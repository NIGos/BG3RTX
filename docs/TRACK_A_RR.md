# Track A: DLSS Ray Reconstruction in place of DLSS SR

Status: **working in game.** With `rr=1` (the default in `bg3rtx.ini`) BG3's DLSS
Super Resolution evaluate is replaced every frame by a DLSS Ray Reconstruction
(DLSS-D, `nvngx_dlssd.dll` 310.7.129) evaluate. It runs on the same command
buffer, with the same colour, depth, MV and output images, plus four guides
built from the G-buffer. `rr=0` gives back the game's own SR. The switch is
live, without a restart.

Files:
- `layer/ngx.cpp`: the hooks, contract logging, RR creation and evaluation,
  guide dispatch, GPU timing and trace dumps.
- `shaders/rr_guides.hlsl` and `shaders/common.hlsli`: the G-buffer to guide
  conversion, hot-reloaded.
- `bg3rtx.ini`: the `rr*` and `ngx_hook` keys.

## How it hooks NGX

- **Where the calls go.** BG3 links the NGX client statically: bg3.exe exports
  `NVSDK_NGX_*`. That client calls the driver's loader,
  `DriverStore\...\_nvngx.dll`.
- **What is hooked.** The host layer patches the loader's four Vulkan entry
  points the moment the DLL is mapped: `CreateFeature`, `CreateFeature1`,
  `EvaluateFeature` and `ReleaseFeature`. It finds the mapping through
  `LdrRegisterDllNotification`.
- **Patch and trampoline.** Each patch is a 14-byte absolute jump. The original
  15-byte prologue is relocated into a trampoline; it is the usual
  `mov [rsp+x],reg` / `push` / `sub rsp` sequence, with no RIP-relative operands.
  This is the same approach dlss5-bridge used for the "loader layer". Neither
  the game executable nor the feature snippets are touched.
- **Recursion.** A thread-local nesting counter forwards NGX's internal calls
  untouched.
- **The layer DLL is pinned** (`GET_MODULE_HANDLE_EX_FLAG_PIN`). BG3 first
  creates and destroys a throw-away `XessVulkanExtensionManager` VkInstance, and
  the Vulkan loader unloads the layer with it. That leaves the DLL notification
  callback pointing at unmapped memory. Without the pin, the game died
  silently (no WER report) about 1 s after start, on the next DLL load.

## SR contract (logged once per run, `[ngx]` lines)

See the table in [FRAME_MAP.md](FRAME_MAP.md#dlss-sr-contract-hooked-in-_nvngxdll-layerngxcpp).
In short:
- **Create:** feature 1, 2560x1067 → 3840x1600, PerfQuality 2 (Quality), flags
  `IsHDR | MVLowRes | DepthInverted | AutoExposure` (plus the deprecated
  sharpening bit), output subrects enabled.
- **Colour:** R11G11B10F, the step-15 target.
- **Depth:** D24S8, reversed Z.
- **MV:** R16G16F, the step-5 velocity, MV scale −1, −1.
- **Output:** R11G11B10F 3840x1600.
- **Per-frame values:** jitter in render pixels, pre-exposure 1, no exposure
  texture.

All of this matches FRAME_MAP steps 5, 15 and 16. The colour, MV and output are
exactly the images the frame map names.

## Guides

The guides are built by `rr_guides.hlsl:main`, one 8x8-thread dispatch at
render resolution. They are stored in our own images, which stay in `GENERAL`:

| Guide | Format | Formula | Sky |
|---|---|---|---|
| Diffuse albedo | RGBA8 | `RT1.rgb · (1 − RT2.z)` | 0.5 |
| Specular albedo | RGBA16F | `EnvBRDFApprox2(F0, RT2.x², N·V)` with `F0 = mix(0.16·RT2.y², RT1.rgb, RT2.z)` (the RR guide's appendix formula and BG3's own F0) | 0 |
| Normals + roughness | RGBA16F | xyz = decoded RT0 (world-aligned, unit length, NaN-guarded), w = RT2.x perceptual roughness; `DLSS.Roughness.Mode = Packed` | 0 |

Sky is detected from depth (reversed Z, depth = 0).

- **G-buffer decode.** Confirmed against the G-buffer fragment shader
  `aa4efa4d` (it writes `rg = n.xy/(1−n.z)·0.28125 + 0.5`, base colour,
  `(roughness, specular, metal, flags)`) and against the tiled lighting shader
  `f469db78`. See FRAME_MAP.
- **N·V is a constant** (`rr_nov=0.5`). The normals are world-space and the
  guide pass does not know the camera yet. This is the one approximation left.
  The fix is to read the per-camera UBO (also needed by Track B). With
  `rr_nov<=0` the shader uses `|n.z|`, which is only correct for view-space
  normals.

**The guides have to be built at the end of the G-buffer pass, not at the
evaluate.** This was the key finding:
- **First attempt.** The guide dispatch ran at evaluate time and gave a normal
  guide of noise, with a block of foreign content and 267 non-finite texels.
- **Cause.** Dumping RT0 at the evaluate (`rr_dump_gbuffer=1`) showed that its
  memory has been reused by then: values up to ±65504 and 86 NaN/Inf texels.
  The same image at the G-buffer pass end is clean.
- **Fix.** The host now records the guide dispatch inside the game's command
  buffer right after every matching G-buffer/decal pass. It moves RT0–RT2 from
  `COLOR_ATTACHMENT_OPTIMAL` to `SHADER_READ_ONLY`, and depth from
  `DEPTH_STENCIL_ATTACHMENT` to `DEPTH_STENCIL_READ_ONLY`, then back afterwards.
  It also restores the game's compute bindings.
- **Ordering.** The evaluate only adds a memory barrier: queue submission order
  plus barriers order the two command buffers.
- **Freshness.** RR refuses a frame (and SR runs instead) if no guides were
  recorded in the last 2 frames. This happens on the one frame around a level
  load.

## RR feature

- **Creation.** Created lazily on the first evaluate, in that evaluate's
  command buffer, with `CreateFeature1(device, cmd, NVSDK_NGX_Feature_RayReconstruction, ...)`
  on a parameter block from the loader's `AllocateParameters`.
- **Create parameters:**
  - the SR feature's own sizes, PerfQuality and flags, minus `AutoExposure`,
    which RR does not support;
  - `DLSS.Denoise.Mode = DLUnified`;
  - `Roughness.Mode = Packed`;
  - `Use.HW.Depth = 1`;
  - `RayReconstruction.Hint.Render.Preset.* = rr_preset` (default 0 = the
    snippet's default).
- **Evaluation.** The game's own parameter block is passed to `EvaluateFeature`
  with the RR handle. It carries colour, depth, MV, output, jitter, MV scale,
  reset and subrects. The guide keys are added: `DLSS.Input.DiffuseAlbedo`,
  `DLSS.Input.SpecularAlbedo`, `GBuffer.Normals`, `GBuffer.Roughness` and their
  subrect bases.
- **Release.** The RR feature is released together with the game's SR feature
  in `ReleaseFeature`.
- **Switching.** `rr` is read on every evaluate. The first frame after a switch,
  in either direction, carries `Reset = 1`, and the game's own value is restored
  afterwards.

## Verification

### Test conditions

- **Scene.** Steel Watch Foundry, static camera, loaded from the latest save
  with `scripts/auto-run.ps1` (headless presentation, from main `e002608`).
- **Output.** SDR: the display was in standby, so BG3 fell back from HDR10 to
  B8G8R8A8.
- **Pair.** RR `captures/20261026-012711`, SR `captures/20261026-013939`, both
  with `dump_png_width=3840`.

### Images

- **`docs/img/sr_full.png` / `docs/img/rr_full.png`.** Tonemapped output
  (the tonemap pass that reads the DLSS output), 3840x1600 scaled to 1920x800.
- **`docs/img/sr_vs_rr_crops.png`.** Three 640x400 crops at 1:1, SR on top
  and RR below.
- **`docs/img/sr_vs_rr_diff_x4.png`.** Absolute difference ×4.
- **The output is correct:** same exposure, colour and geometry.
  - **Size of the difference.** The mean absolute difference of the tonemapped
    8-bit images is 0.71/255 (p99 5.3/255).
  - **Where it differs.** Almost all of the difference is the animated
    character (a different idle frame) and small light sources. RR's HDR
    output keeps brighter highlights (max 23.8 vs SR's 9.8 in the linear
    output), while the means agree to 1.5%.
  - **Aspects that look the same.** Edges, thin geometry (chains, railings,
    cables) and texture detail are reconstructed as well as with SR. There is
    no ghosting or smearing in the static shot.
- **Guides, at full resolution in the RR capture:**
  - `d162` diffuse albedo: metals dark, cloth and wood coloured, as expected;
  - `d163` specular albedo: metals bright, dielectrics about 0.04;
  - `d164` normals + roughness: world-aligned, the floor +Y.

### Non-finite values and stability

- **Non-finite values: none.** Every dump of both captures has nonfinite = 0,
  including the guides and the RR output (`d165`).
- **The one exception was the evaluate-time guide attempt.** Before the
  guides moved to the G-buffer pass, the normal guide had 267 non-finite
  texels, because RT0 is aliased at the evaluate.
- **Crashes: none.** Four RR sessions ran, each including at least one level
  load. RR creation succeeded every time.
- **One SR evaluate during a load.** About 20 s in, the game renders one
  frame without a G-buffer pass (a loading transition). RR declines that frame
  (`RR skipped this frame`), the game's SR runs, and the next RR frame resets
  its history.
- **The live toggle** (`rr` 1→0→1→0→1 in the running game) is covered
  below.

### Frame cost

Measured with GPU timestamps around the evaluate (`[ngx] gpu` lines) in the
same scene and run conditions:

| | Guides | Evaluate | Total |
|---|---|---|---|
| DLSS SR (preset default, 2560x1067 → 3840x1600) | — | 0.68–0.91 ms | ≈ 0.8 ms |
| DLSS RR (preset default) | 0.03 ms (one compute pass at render resolution, measured when it ran at the evaluate) | 2.48–2.84 ms | ≈ 2.6 ms |

- **Cost.** RR costs about **+1.8 ms of GPU time per frame** on the RTX 5090
  at this resolution. That is in line with NVIDIA's table for RR presets D/E
  in Quality mode (about 1.5–2 ms at a 4K-class output).
- **Throughput.** The evaluate count per 5 s, a proxy for gameplay frame rate
  with headless presentation, was about 520 with RR against about 600 with SR
  in the same scene.
- **Why `[watch]` is not used.** Its `presents` count mixes in the loading
  screen's presents, so it is not a clean figure for the frame-rate
  comparison here. Repeat with the display on to get real fps.

### Live switch

Run `captures/20261026-0213xx`, four traces, with `bg3rtx_toggle.log` in
`-021355`. `rr` in the live ini was flipped 1→0→1→0→1, 1.5 s apart, during
gameplay. The log shows:

```
02:13:44.988  [ngx] upscaler for feature 000001E913D62730: DLSS SR
02:13:48.061  [ngx] upscaler for feature 000001E913D62730: Ray Reconstruction
02:13:48.994  [ngx] gpu SR: ... evaluate 0.653 ms (avg of 320)
02:13:48.994  [ngx] gpu RR: ... evaluate 2.592 ms (avg of 376)
```

- **Coalesced flips.** Two of the four flips landed while the game was
  writing a trace (no presents, so no ini poll) and were folded into the
  next read.
- **No restart and no re-creation.** The RR feature stays alive while SR
  runs, and each switch resets the history of the feature that takes over.
- **The trace at `-021346` ran on SR.** It was taken in the middle of the
  session, between the two switches.
- **Non-finite values: none.** All four traces have nonfinite = 0.
- **A config fix found by this test.** `cfg_poll` used to accept an ini read
  while a script was rewriting it (0 keys), which silently reset every setting
  to its default. It now keeps the previous settings and reads again on the
  next poll (`layer/util.cpp`).

## Debug switches

- **`rr_show=N`** replaces the upscaled frame with one guide (nearest
  upsampling), so the tonemapped dump shows exactly what RR gets:
  1 diffuse, 2 specular, 3 normals, 4 roughness, 5 raw RT0, 6 depth×20.
- **`rr_guides_at=0`** builds the guides at the evaluate. It is kept only to
  reproduce the aliasing finding.
- **`rr_dump_gbuffer=1`** adds RT0–RT2, as they are at the evaluate, to the
  trace. In a trace, the guides and the DLSS output always appear as an extra
  pass unit right after the input pass, so the analyser dumps them with stats.
- **`ngx_hook=0`** leaves NGX completely alone. It is read at instance creation.
- **`[ngx] gpu RR/SR`** lines every 5 s give the GPU time of guides and
  evaluate, measured with timestamp queries around the evaluate in the game's
  command buffer.

## Known limits and next steps

- **N·V for the specular albedo is a constant.** Read the camera UBO (View at
  0x00 of PerCamera) to get the true view vector. The same work unblocks
  `pInWorldToViewMatrix`/`pInViewToClipMatrix` for RR's own specular motion
  estimate.
- **No specular hit distance or specular MVs are given.** BG3 has no RT, so the
  noisy input is raster lighting plus SSR. Once Track C produces GI, it should
  feed `DLSSD.DiffuseHitDistance`/`SpecularHitDistance`.
- **Transparents, fog and particles** are in the colour but not in the guides,
  so RR sees background albedo under them. The RR guide suggests blending the
  guides towards transparent surfaces; BG3's forward pass would need a hook for
  that.
- **The DLSS settings in `graphicSettings.lsx`** (`DLSS2Mode`,
  `ResolutionUpscaleType`) disappeared several times during this session. Runs
  on this branch had a working SR/RR feature in every session where the keys
  were present. The rewrites coincided with other agents' sessions and with
  the display being in standby, and no NGX call through these hooks ever
  failed. Run `scripts/ensure-settings.py` (main) before launching.
