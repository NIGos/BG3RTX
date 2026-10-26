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
| Normals + roughness | RGBA16F | xyz = decoded RT0 rotated to **world space** (unit length, NaN-guarded), w = RT2.x perceptual roughness; `DLSS.Roughness.Mode = Packed` | 0 |

Sky is detected from depth (reversed Z, depth = 0).

### Guides v2 (`rr_guides_v2=1`, the default)

RT0 holds **view-space** normals (Track D proved it; see FRAME_MAP). The v1
guides passed them as they were, with a constant N·V and no matrices. v2 fixes
all three from one source: **the camera UBO of the G-buffer pass itself**,
set 0 binding 2 of the G-buffer vertex shaders.
- **How it is read.** When the G-buffer/decal pass ends,
  `read_guide_camera` (`layer/ngx.cpp`) resolves the binding with
  `query_binding` (dynamic offset applied) and copies its 224 bytes from the
  game's mapped memory. This is the same mechanism and layout that
  `layer/streamline.cpp` uses at the velocity pass: std140, row-major,
  `v * M`, `_m0` world-to-view, `_m1` the jittered reversed-Z infinite
  projection. It is the current frame's camera, read where the guides are
  recorded, so no frame lag.
- **Checks.** `_m1` must look like a perspective projection (`P23 = 1`,
  `P33 = 0`, no shear) and `_m0` like an affine view (last column `0,0,0,1`,
  non-singular 3x3). Otherwise the frame falls back to v1 and the reason is
  logged once.
- **World normals.** The game's relation is `n_view = n_world * R`, with
  `R = mat3(_m0)` (the same check Track D used, |dot| median 0.998). The host
  inverts R on the CPU (adjugate) and pushes its rows. The shader computes
  `n_world = normalize(n_view * inverse(R))`. In game, R is orthonormal
  (rows of unit length; for example world up maps to view `(0, 0.758, -0.652)`
  in the foundry shot), so this is the transpose.
- **Per-pixel N·V.** The view direction of the pixel centre is
  `d = ((ndc.x − P20)/P00, (ndc.y − P21)/P11, 1)`. It uses the jittered `_m1`,
  because the G-buffer was rendered jittered. The NDC is D3D-style
  (`ndc.y = 1 − 2·uv.y`), because the game's vertex shaders negate clip y
  for Vulkan (TRACK_D). Then `N·V = dot(n_view, −normalize(d))`, and
  `EnvBRDFApprox2` gets `max(|N·V|, 1e-3)`.
- **Matrices for RR.** `WorldToViewMatrix` = `_m0`, and `ViewToClipMatrix` =
  `_m1` with the jitter (`P[2][0..1]`) zeroed. Both stay in the game's
  row-major, row-vector form, which is what the RR guide (3.4.9) asks for:
  "Row Major Order and use left multiplication". `_m0` is the absolute
  world-to-view: the camera position (UBO floats 52–54) times `_m0` gives the
  origin to < 1e-3. They are set on every RR evaluate whose guides are at most
  2 frames old. Otherwise they are set to null, because the game reuses its
  parameter block.
- **The lighting shader's `_m6`** (camera UBO set 0/b1 of the lighting pass,
  Track C) is a different UBO, bound only after the G-buffer pass. It is not
  needed: `_m0` of the G-buffer pass is the matrix the G-buffer normals were
  written with.
- **Not given: specular hit distance and specular motion vectors.** BG3's
  specular is raster plus SSR, and the guides have no per-pixel specular ray.
  So `DLSSD.SpecularHitDistance` stays unset. The matrices are what RR needs
  together with a hit distance once one exists (Track D reflections, D-RF).
- **Cost.** Unmeasurable: same dispatch, 96 bytes of push constants instead of
  32, and a 224-byte memcpy on the CPU per G-buffer pass.

- **G-buffer decode.** Confirmed against the G-buffer fragment shader
  `aa4efa4d` (it writes `rg = n.xy/(1−n.z)·0.28125 + 0.5`, base colour,
  `(roughness, specular, metal, flags)`) and against the tiled lighting shader
  `f469db78`. See FRAME_MAP.
- **N·V** is per pixel with v2 (above). With `rr_guides_v2=0` it is the
  constant `rr_nov` (default 0.5), or `|n.z|` with `rr_nov<=0`.

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
  - `d164` normals + roughness. This capture predates v2, so these were
    view-space normals. That camera was nearly view-aligned with world Y,
    which is why they were taken for world-aligned (see "Guides v2").

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

### Guides v2 (2026-10-26)

Runs on this branch, `scripts/auto-run.ps1` (headless), Steel Watch Foundry
from the latest save. The pairs differ only in `rr_guides_v2`.

| Run | Capture |
|---|---|
| v2, static | `captures/20261026-083016` |
| v1, static | `captures/20261026-084630` |
| v2, `rr_show=7` (N·V) | `captures/20261026-084817` |
| v2, `-Move`, 4 traces | `captures/20261026-090804` … `-090849` |
| v1, `-Move`, 4 traces | `captures/20261026-091145` … `-091229` (the pan key did not take, static) |
| v1, `-Move`, 1 trace | `captures/20261026-085747` (panned through the foundry) |
| `-Validation`, v2 / v1 | `captures/20261026-085212` / `-085333` |

- **Normals: floors decode to +Y world.**
  - [rr_v2_normal_guide_world.png](img/rr_v2_normal_guide_world.png) is the
    v2 guide (`d164`): floors green, walls red (+X) or blue (+Z).
  - Decoded from the dump preview, a 140x90 patch of the walkway has a median
    normal of (0.000, 0.998, 0.037). Four other floor spots give 0.967–0.998
    in Y.
  - The v1 guide of the same view
    ([rr_v1_normal_guide_view.png](img/rr_v1_normal_guide_view.png)) has the
    walkway at view (0.03, 0.73, −0.69), which is exactly world up times
    this camera's R.
  - During the pan out to the docks (v2 trace 4,
    [rr_v2_moving_docks_normals.png](img/rr_v2_moving_docks_normals.png)), the
    quay and the water plane are still +Y with a different camera, so the
    rotation follows the camera.
- **N·V is per pixel and has the right sign.**
  - [rr_v2_nov_show7.png](img/rr_v2_nov_show7.png) is `rr_show=7`
    (tonemapped): silhouettes are dark (grazing), and only 0.8% of pixels are
    negative (red), at normal-mapped pipe edges.
  - On the flat walkway N·V rises from 0.42 (top of the screen) through 0.57,
    0.72 and 0.84 to 0.92 (bottom).
  - The prediction from this camera's floor normal is 0.69 at the centre and
    0.91 at row 500. A flipped NDC y would give 0.38 at the bottom. This
    confirms the D3D-style NDC.
- **Specular albedo.** The `d163` mean goes from (0.130, 0.106, 0.086) with
  v1 to (0.128, 0.104, 0.083) with v2. The spread is larger: higher at grazing
  angles and lower facing the camera.
- **RR output, static** (`d166`, tonemapped): mean absolute difference
  v1 → v2 0.20/255 (p99 1/255). The mean is 61.11 against 61.14.
  [rr_v1_vs_v2_static_diff_x4.png](img/rr_v1_vs_v2_static_diff_x4.png)
  shows the difference ×4: only the animated character, animated hazard
  stripes and a flickering sign differ. In a static frame RR's output is
  dominated by the colour input, so this was expected.
- **RR output, moving.**
  - The v2 pan's traces 3 and 4 were taken while the camera moved: the `_m0`
    translation changes between consecutive traces.
  - [rr_v2_moving_docks.png](img/rr_v2_moving_docks.png): clean, with no
    ghosting, trails or smearing on the masts, rigging or water glints at
    1280-wide preview resolution.
  - The v1 references are the same scene and quality, but at a different
    point of the pan. There is no exact like-for-like moving pair (the key
    injection is not deterministic).
- **Non-finite values: none.** Every dump of all 21 traces of this work has
  nonfinite = 0, including the guides and the RR output. No `RR evaluate
  failed`. Each run has the usual single `RR skipped` (the load frame).
- **Validation.** The `-Validation` runs with v2 and v1 report exactly the
  same message IDs, with the same counts:
  - `0xfa0c48b`, `0x46582f7b`, `0x3a9b375b`, `0x1cecb534` and `0x13365b2`,
    three each (duplicate limit);
  - `0xa5625282` and `0x779fc816`, once each.

  v2 adds no validation messages.

## Debug switches

- **`rr_show=N`** replaces the upscaled frame with one guide (nearest
  upsampling), so the tonemapped dump shows exactly what RR gets:
  1 diffuse, 2 specular, 3 normals, 4 roughness, 5 raw RT0, 6 depth×20,
  7 the guides' N·V (grey, red where negative; carried in the specular
  guide's alpha only while `rr_show=7`).
- **`rr_guides_v2=0`** gives back the v1 guides (view-space normals, constant
  N·V, no matrices) for A/B tests. It switches live. **`rr_log_camera=1`**
  logs the guide camera every 600 frames (it is always logged for the first
  two frames and on every trace frame).
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

- **Done (v2):** world-space normals, per-pixel N·V and the
  WorldToView/ViewToClip matrices. See "Guides v2".
- **No specular hit distance or specular MVs are given.** BG3 has no RT
  reflections, so the noisy input is raster lighting plus SSR. The matrices
  are in place; a Track D reflection pass (D-RF) should add
  `DLSSD.SpecularHitDistance` (65504 for sky), and the GI
  `DLSSD.DiffuseHitDistance`.
- **Not checked: what RR does with the matrices without a hit distance.** It
  gets them every frame, and they caused no visible change in static or
  moving frames and no validation or NGX errors. Their effect on reflections
  can only be judged once a hit distance is given.
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
