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

## Guide descriptor pool retirement - 2026-10-30 (`066b38b`, not merged)

The guide cache used DeviceWaitIdle + ResetDescriptorPool on cache pressure or
pool exhaustion, and reset pools directly on resize. With FG 2, exhaustion during
real rotation raced the SDK pacer's vkQueueSubmit and stalled the test process.
Pools now retire through the existing GPU-completion deferred-destruction path;
new allocations use a fresh 64-set pool. In-flight sets keep their old pool alive.
Non-pool allocation errors return failure instead of attempting an idle/reset.
The repaired runs completed 64 pool rollovers without that stall. This adds no GPU
dispatch and changes no guide shader or ABI. See `docs/evidence/fg-followup-20261030.json`.
The FG/UI quality gates remain unqualified; no main merge or feature-default change.

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

## RR before post (`rr_post=1`, the default; AUDIT_SOTA #1, 2026-10-26)

### Hair candidate (`rr-hair-guide`, 2026-11-03, unqualified)

ef1 Dialogue3 presented hair-box residual was 2.87 / 2.40 (repeat spread 0.47),
sparkle 1.59% / 0.83%. Late RR (`rr_post=0`) reached 1.25 / 0.35%; SR reached
1.51 / 0.47%. CBT off worsened it to 3.58 / 2.23%; armour control was about 0.9.
Mean brightness was unchanged. This identifies a pre-post-path quality gap.

The local DLSS RR Integration Guide section 3.4.10 requires a separate,
premultiplied transparency layer removed from the input colour. It is upscaled,
not denoised. `nvsdk_ngx_helpers_dlssd_vk.h` exposes layer, opacity and motion
inputs; its generic transparency mask is reserved. These are not reactive masks.
The G-buffer retains class-5 surviving samples, not discarded fragment alpha or
separate hair radiance/background. No unsupported coverage guide is submitted.
The cached native deferred shader `captures/alpha_cache/42a95e17ec3adc51.glsl`
overwrites RT1 alpha with 1; RT2 alpha contains material-class bits.

`rr_hair_resolve` retired 2026-11-04: user verdict "ruins the hair" (2026-11-03); GUI, INI and runtime path removed.

Historical implementation (standalone shader retained only as an offline compile fixture): when on,
an independent G-buffer dispatch snapshots class-5 occupancy before aliasing.
A separate pre-RR dispatch averages sharp RGB over mixed, aligned 4x4 cells.
This preserves each cell's sharp-input mean before store quantization, not the
native geometric resolve's mean or the spatially varying DoF-composed mean.
It is a coverage proxy, not a recovered native dither pattern; silhouettes can
soften by up to 3 render pixels. Legacy guide/post shaders remain untouched.

Hair keeps the game's velocity-pass motion vectors; no verified defect warrants
rewriting them. With default `rf_models=3`, class 5 has the fresh 65504 no-hit
distance, not zero. RR accepts distance guides up to two frames old; spatial
staleness remains a possible confound requiring captures, not a proven cause.

Prepared ef2 lives in `tools/live_batches/rr_hair_guide`, outside pending inboxes.
It compares baseline / late RR / resolve / baseline with separate P16 bursts,
ungraded Xr/Z float means, grading and clean GPU timing. The quality target is
late RR's residual/sparkle within new repeat noise, without lost energy/detail.
GPU cost is unmeasured; occupancy capture is outside existing evaluate timing.
No game was launched. No quality pass or default promotion is claimed.

### ef2 observations and ef3 decision (rr-hair2, 2026-11-03)

Historical ef2 analysis saw hair edge gradient -24%, high-frequency detail -48%,
RR output mean -0.6%, and no flicker gain above the static baseline. Dialogue
camera drift of 10-18 px confounded ef2. The later user rejection supersedes
the earlier candidate verdict and unexecuted ef3 resolve plan; do not queue it.

**What late RR gives up.** `rr_post=0` keeps the game's DoF, bloom and grading,
but places all of them before RR at render resolution. It gives up denoising
linear sharp colour before DoF and applying the game's grade after RR at output
resolution. Bloom and DoF are therefore part of the signal RR filters; they are
not disabled. Earlier static foundry A/B above measured new/old delta 1.79 PQ
codes against 1.57 for new/new, 0.71% vs 0.74% above 16 codes, mean -0.79% vs
-0.03%, high-frequency energy 15.8 vs 15.6. The early path removed a noise-linked
grade bias: -0.49/-0.67/-1.01/-1.54% by input-noise quantile, versus repeat
+0.02/-0.05/-0.10/-0.06%. Late RR gives that bias removal up; it saves about
0.2 ms of chain/grade overhead (RR evaluate stayed 2.5-2.7 ms). No historical
measurement proves a temporal, DoF or highlight benefit of early RR: those
were expected, unmeasured benefits. ef3 must weigh hair/detail against whole
frame RGB histograms, mean, highlights and trace DoF/bloom/grade review.
PQ means are appearance, not energy: use same-stage float dumps.json means,
pre-post compose normalization and repeat bands. Never equate ungraded Xr/Z
with late graded RR output. Future live GPU cost remains unmeasured.

**The problem.** BG3 hands DLSS its *graded* image. Between the scene colour
and the evaluate it runs, at render resolution: god rays (`a7466e4e`), depth of
field (12 passes, composite `96bb806d`), a stencil-tagged dither resolve
(`7401d785`), exposure (`2cdd9f08`), bloom (`0463a527`, `+0.2·bloom`) and a
PQ-space 3D-LUT grade with an HSV adjust (`0592680b`). RR therefore denoised a
non-linear, DoF-blurred signal. The RR guide 3.1 wants RR "as close to the start
of post-processing as possible" and DoF after it.

**What runs now** (`layer/rrpost.cpp`, `shaders/rr_post.hlsl`):
- **RR's colour** is the scene colour after compose, transparents and
  `c1fb2881`, before god rays and DoF (`X`), with the game's own dither resolve
  applied to it (a line-by-line port of `7401d785`, same stencil view, same
  16x16 tiling and border quirk). RR writes an RGBA16F image of ours.
- **The post chain is carried to output resolution as a per-pixel affine map**
  `chain(X) = A·X + C` (render resolution, RGBA16F, rgb C and a A). Each step is
  recorded in the game's command buffer right where the game's own step is:
  `start` (first post step: snapshot X, A = 1, C = 0), `dof` (the composite's own
  far/near blend weights from its CoC images and constants: `A *= (1−w_far)(1−w_near)`),
  `resolve` (before `7401d785`: RR's colour, and `C = U − A·X` with `U` the
  game's own chain value there, so god rays and the blurred DoF layers enter
  exactly), `exposure` (`k = b0[1]·b1[0]`, the game's buffers bound live),
  `bloom` (`B = 0.2·bloom` at the game's uv). k and B live in a second
  RGBA16F image so the stores can be replayed in order.
- **After RR**, one output-resolution pass writes the game's DLSS output image:
  `grade(((A·RR + C)·k) + B)`, A, C, k and B bilinear from render resolution,
  the game's grade UBO (4 floats, read per frame, so per-area grades follow)
  and its 32³ LUT. Each intermediate is truncated to R11G11B10 where the game
  stores one (`rr_post_quantise`, see below). The tonemap and UI read that
  image as before.
- **Guides added:** colour before transparency (the compose output, copied at
  the compose pass end, same format as the colour; `rr_cbt`) and the
  screen-space SSS guide (`rr_sss`): `lum(after − before·albedo)` of the SSS
  blur (`6f535075`/`d1669194`; the skin pixels are lit with diffuse colour 1 and
  the blur multiplies the albedo back in), 0 where the stencil-culled blur did
  not touch the pixel. The DoF guide is not needed: DoF now runs after RR
  (guide 3.4.13, "can be omitted").
- **Fallback.** Anything the chain does not model breaks it for that frame and
  RR gets the game's graded colour, as before (`[rrpost] post chain not
  followed`, then `[ngx] upscaler ...`; a switch resets RR's history once). The
  game's other post passes inside the chain are whitelisted by hash; any other
  pass or dispatch between the first step and the grade is "unmodelled".
  Seen so far: a level-load transition (1-2 frames, `cs 4681b95e`), and one
  Goblin Camp cinematic state with chromatic aberration `b9f5a8ab`, a tint
  vignette `3b556695` (mask `abfdaa7e`) and a 3D overlay `a1e30a20`.

**Replication check.** `rr_post_check=1` (and every trace) also evaluates the
same chain on RR's own input `Xr` at render resolution, i.e. our reconstruction
of the game's DLSS input, and `scripts/rr_post_compare.py` compares it with the
game's grade output of the same frame (`dump_raw=1`):

| Scene | Frame | k | Identical texels | Within 1 R11G11B10 ULP |
|---|---|---|---|---|
| Foundry (god-ray pass, no DoF) | `captures/20261026-162517`, `-162536` | 1.0048 | **100.00%** | 100.00% |
| Goblin Camp (no DoF in the traced frame) | `captures/20261026-162210` | 0.997 | **100.00%** | 100.00% |

The store order matters. The GPU truncates R11G11B10 stores (checked against
round-to-nearest), and the game stores after every step: with k slightly below
1 (Goblin Camp's steady state) the exposure store alone drops a whole ULP, so
the game's image is 0.65% (red, green) and 1.4% (blue) darker than an fp16
chain. Without the replayed stores the foundry was 97.4% identical, Goblin Camp
74%, and a DoF dialogue frame (earlier build) was within 1 ULP on 87-98% of the
texels with the same uniform offset. **Not verified with the final build: a DoF
frame** (the traced Goblin Camp frame had no DoF; the DoF weights were checked
only on the earlier build, where the residual error was the uniform store
offset, independent of the DoF weight).

**S-F05 boundary correction (rrpost-r11, 2026-11-06, offline).** Mantissa masking
alone retained FP32's exponent range. `trunc_r11g11b10` now uses fixed subnormal
steps (R/G 2^-20, B 2^-19), saturates finite overflow at 65024/65024/64512,
preserves positive infinity and reduced NaN payloads, and zeros negative finite
values/infinity. Normal inputs retain the original path. No new key or request.
`tests/rr_r11_test.py` runs `tools/rr_r11_fixture.cpp` on an isolated Vulkan device:
real R11 image stores and subsequent loads versus production HLSL and an independent
numeric reference, bit for bit. It checks every finite positive FP16 value, R11/R10
boundaries and their FP32 neighbours, negatives and NaN payload boundaries.
The fixture refuses GPU work while BG3, Stalker2 or Cyberpunk runs and requires
Khronos validation. RTX 5090 verified; other devices must pass their own oracle.
77933 mixed RGB inputs / 233799 channel comparisons pass exactly, with zero
numeric-reference mismatches, normal changes or Vulkan validation errors.
Release equivalence uses an explicit normal-input assumption (the exact range
guard folded true), plus a separate quantisation-OFF proof. It covers both RR
requests; unrelated requests remain byte-identical. Full-range equality would
be incorrect: boundary outputs intentionally change. In-game cost was not measured
(offline-only authorization); normal store rounding is unchanged. To inspect later,
use the existing `rr_post_check` replication comparison; no live batch was queued.

**A/B against the old path** (foundry, static, one run toggled live with
`scripts/live_phases.py`: new, old, new; tonemapped HDR10 frames compared in PQ
code values):

| Pair | mean abs delta (10-bit PQ codes) | px > 4 codes | px > 16 codes | mean luminance |
|---|---|---|---|---|
| new vs old (`-162536` vs `-162527`) | 1.79 | 11.4% | 0.71% | −0.79% |
| new vs new, 19 s apart (`-162517` vs `-162536`) | 1.57 | 9.5% | 0.74% | −0.03% |

- Almost all of the difference is the animated character, a flickering sign and
  small lights (`img/rrpost_foundry_diff_x4.png`); the 1:1 crops
  (`img/rrpost_foundry_crops.png`, top new, bottom old) show no visible change
  in static content.
- **The luminance shift is the expected bias removal, not a replication error**
  (the replication is bit-exact). Sorting the static pixels (|delta| < 16 codes)
  by the local noise of RR's input (|x − 3x3 mean| / mean):

  | input noise quantile | 0-50% | 50-80% | 80-95% | 95-100% |
  |---|---|---|---|---|
  | new vs old | −0.49% | −0.67% | −1.01% | −1.54% |
  | new vs new | +0.02% | −0.05% | −0.10% | −0.06% |

  The old path was brighter where RR's input was noisier: RR averaged graded
  values, and the mean of a convex-ish grade of a noisy signal is above the
  grade of its mean (Jensen). The new path no longer has that bias. Part of the
  −0.5% in the cleanest pixels is the same effect at lower noise (the metric
  also counts texture detail as "noise").
- Apart from that, the new-vs-old difference is barely above the frame-to-frame
  floor of the same mode: in a static, converged frame RR's temporal
  accumulation hides most of the rest. The gains expected in motion, in dark
  1-spp areas and in DoF scenes (where the old path fed RR the blur) are **not
  measured** (no motion metric yet).
- High-frequency energy (mean |Laplacian| in PQ codes) is 15.8 new vs 15.6 old:
  no loss of detail.

**Pre-exposure (`s1b13[0]`) confirmed.** The exposure probe (logged by
`[rrpost] exposure probe`) binds the game's buffers in our own compute:
- the lighting's set 1 b13 is **the same buffer** as `2cdd9f08`'s b0, and the
  adaptation `bd9db92d` writes the other one (b1): two buffers that swap each
  frame;
- their layout is `[0] = 0.8333/2^EV`, `[1] = 1.2·2^EV`, `[2] = EV` (e.g. EV 14.18,
  `[0] = 4.50e-5` in the foundry);
- so the lighting is pre-exposed by `0.8333/2^EV(previous)`, and `2cdd9f08`
  multiplies by `b0[1]·b1[0] = 2^(EV_prev − EV_new)`, a re-exposure: 1.0048 in
  the foundry, 0.997 in Goblin Camp, in steady state.

RR's input (both the old graded colour and the new `X`) is therefore already
eye-adapted. The RR guide 3.7 says the exposure parameters (DLSS guide 3.9,
which includes the pre-exposure factor) are not supported by RR, so
`DLSS.Pre.Exposure` stays the game's 1. The `rr_pre_exposure` A/B key was never run:
removed 2026-10-31, see CONSOLIDATION.md C1.

**Cost** (RTX 5090, 2560x1067 → 3840x1600, GPU timestamps, `[rrpost] gpu`,
foundry): render-resolution steps (snapshot, DoF weights, resolve, exposure,
bloom, SSS before/guide) 0.14-0.17 ms per frame, the output-resolution grade
0.06 ms, the CBT copy unmeasured (one 11 MB image copy). About **+0.2 ms** in
total (0.15 ms before the SSS guide and the store replay were added). The RR
evaluate itself is unchanged (2.5-2.7 ms).

### Robustness checks (merged with main `a5603a9`, BVH split)

- **Non-finite values: 0** in every dump of every trace with `rr_post=1`
  (foundry, Goblin Camp; Xr, AC, EB, Z, the check image, CBT, the SSS guide,
  the DLSS output and the tonemapped frame).
- **`-Validation`** against the baseline taken first on the unmodified branch
  (`captures/validation_baseline.log`: 7 IDs) and a merged-build run with
  `rr_post=0` (the same 7): `rr_post=1` adds **one** ID, once per run,
  `VUID-vkDestroyImage-image-01000` (`0xf2d29b5a`) at level-load
  transitions. The explanation first given here (the game destroying images
  of a still-pending frame) was wrong: the cause was stale descriptors in our
  own descriptor ring. It is **fixed**; see "Lifetime of the game's images"
  below.
- **Fallback test** (`rr_test_fail=1`): RR creation refused, the game's SR
  runs, `rr_active()` stays false, and the core switches to its non-RR modes:
  `gi: resources ... (GI at 1280x534)`, "half res, own accumulation", and
  reflections "mirror rays, no denoiser, max roughness 0.30". Before this
  change they would have stayed in their 1-spp RR modes with no denoiser.
- **Level loads**: RR pauses for a few frames, so `rr_active()` drops and the GI
  reallocates to half resolution and back (two `gi: resources` lines per load).
- **Unmodelled steps seen in motion**: three colour-LUT bakes (`304fe7e0`,
  `dd9110b2`, `66cc9cb8`, compute writing a 3D image) run inside the post
  command buffer when the camera enters another atmosphere area; they are
  whitelisted, since they only rebuild the LUTs read later (`66cc9cb8` was
  added after the last `-Move` run, so that run still fell back on it, once).
- **`-Move`** (foundry, pan then hold, 480 s of gameplay with `rr_post=1`,
  BVH split on): no fault, no `RR evaluate failed`, the traced frame after the
  pan has 0 non-finite values in 90 dumps (`captures/20261026-174211`). The
  chain fell back only at the three load transitions and on the LUT bake above.
  (Two earlier `-Move` attempts ended early: one game exit at the loading
  screen before RR or our post steps had run, and one exit after 108 s with no
  fault logged; neither is explained, and the 480 s rerun was clean.)
- **`-Move` after the lifetime fix** (2026-10-27, merged with main):
  - 5 runs, foundry: four of 8 minutes of pan then hold, and one `-Save`
    run that traced during the pan. No exits, no `RR evaluate failed`, no
    faults.
  - The traced frame (`captures/20261027-014238`): **0 non-finite values in
    91 dumps**.
  - There are no BG3 crash events in the Windows Application log for the
    earlier exits either. The `nvlddmkm` event 153 pairs in the System log
    coincide with each game launch.

### Lifetime of the game's images (2026-10-26)

**Root cause of `VUID-vkDestroyImage-image-01000`.** The game's lifetime is
correct, and our GPU work never read a freed image. The message came from our
descriptor bookkeeping:
- `rrpost.cpp` records each step from a ring of 256 descriptor sets, and each
  step wrote only the bindings it reads. A reused set kept whatever an earlier
  step had written in the other bindings: our own views of game images (the
  `sss2` diffuse light, the `7401d785` input U, bloom, god-ray input, the DLSS
  output).
- At a level-load transition, the game destroys those images 4 frames after
  their last use. Instrumented runs (`life_log=1`) show this every time: our
  present fence for that frame had already signalled at the destroy.
- By then, though, a later step (exposure, resolve, ...) of the frame still in
  flight had bound a recycled set that still held the stale view in a binding
  it does not read. The validation layer counts every descriptor of a bound
  set, so it reported the image as used by that pending command buffer.
- The GPU never read the stale descriptor, because the binding is not
  statically used by that step. But by the spec, destroying an image that a
  bound set references invalidates the command buffer. So this was a real
  correctness bug of ours, not only a validation-layer artefact.
- The earlier explanation ("the game reads them through push descriptors in
  the pending buffer") did not hold: the game's last use was 4 frames earlier
  and complete.

**Fix.**
1. `rrpost.cpp` `run()` writes **every** binding of the set on each use. Those
   the step does not read get fillers of ours: a 1x1 RGBA16F 2D image, a 1x1x1
   3D image for the LUT slot, and a 256-byte buffer. These go into the GENERAL
   layout once per recording. A ring set can therefore no longer hold a game
   object beyond the step that wrote it.
2. **Safety net for real in-flight use** (`tracker.cpp`, `life_*`):
   - Every game image, view and buffer that our recorded work references is
     noted on the command buffer's recording. The notes come from
     `query_binding` results (the GI/RT passes, RR before post, the alpha
     records), every pass's attachments (the RR guides, the CBT copy) and the
     DLSS output that `rrpost_final` writes.
   - At submit, each note becomes "last submitted in frame F on queue Q".
   - Every present appends an empty submit carrying a fence of ours to the
     present queue, while it is still inside the game's present call.
   - `vkDestroyImage`, `vkDestroyImageView` and `vkDestroyBuffer` look the
     handle up first. If the fence of present F+1 has not signalled, they wait
     for it (bounded at 2 s, logged) before anything is released. This
     includes `ngx_on_destroy_image`, which destroys its views at once.
   - A game `vkDeviceWaitIdle` retires everything submitted before it.
   - A destroy of an object submitted in the current frame (no present since)
     cannot be fenced and is logged as "no fence covers it yet".
3. **DLSS-G:** `sl_note_view_destroyed`. The game destroys a view before its
   image, and the tag hands DLSS-G the view, so the existing wait on DLSS-G's
   inputs fence now also runs when a current tag's view is destroyed.

**Stall cost.** Measured over 3 validation runs (foundry, Water Queens House,
Grymforge) and 5 `-Move` runs (4 of 8 min each and 1 traced run; foundry, panning):
- **Level-load transitions:** no waits. Every destroy of a noted object found
  its frame's fence already signalled, since BG3 defers these destroys by
  about 4 frames.
- **During gameplay:** the safety net fired three times, in two of the five
  `-Move` runs.
  - Two image views and one buffer were destroyed one frame after our work
    last used them, with that frame's fence still pending.
  - The waits were 6.754 ms and 0.135 ms (views) and 0.609 ms (buffer), all
    "complete" (`[life] the game destroys ...`).
  - Without the wait, those destroys would have raced our recorded work on the
    GPU. This is the use-after-free class the brief suspected, and it is real
    though rare.
- **No destroy came within the frame of its submission**, which would have
  been logged as "no fence covers it yet".
- **Bookkeeping:** one empty submit per present, plus the merge of the notes
  at submit. In gameplay that is 375 to 700 references and 0.007 to 0.016 ms
  of CPU per frame (`[life] bookkeeping`, every 60 s).

**Validation, before and after.** Same build except for the fix, `-Validation
-Background -Save`, one run per scene, each through a main-menu-to-level load:

| Scene | Baseline IDs | With the fix |
|---|---|---|
| Foundry (`QuickSave_3784`) | 8, including `0xf2d29b5a` (2x) | 7: the same minus `0xf2d29b5a` |
| Water Queens House | 8, including `0xf2d29b5a` | 7: the same minus `0xf2d29b5a` |
| Grymforge | 7, including `0xf2d29b5a` | 6: the same minus `0xf2d29b5a` |

Grymforge has no `vkCmdCopyBuffer-srcBuffer-00118` in either run. No new IDs.

**Same class elsewhere (checked):**
- GI/RT, reflections and local shadows (`core/gi.cpp`): they use push
  descriptors, rewritten on every dispatch. The sun-scattering ring writes all
  7 bindings on every use. Their in-flight references are covered by the
  safety net (via `query_binding`).
- RR guides (`ngx.cpp`): sets are cached per image tuple, fully written, and
  dropped from the cache when one of their images is destroyed.
- Alpha records (`alphatest.cpp`): every slot a set ever held is rewritten
  (dummies after this frame's views).
- BVH split: reads game vertex and instance buffers only on the CPU, and
  evicts on destroy. The GPU sees them only inside the game's own draws.
- **Not fixed, small:** the core's RT passes accept a scene up to one frame
  old (`built + 1 >= now`). They may therefore bind the previous frame's alpha
  set, whose texture views the game could have destroyed in between. BG3's
  roughly 4-frame destroy deferral makes this unlikely, but `alphatest.cpp`
  (owned by another track) should drop views of destroyed images.

### Preset and snippet

The loaded snippet is now logged at RR creation (`[ngx] RR snippet loaded`):
- `fg=0` (headless test runs): the game's `bin\nvngx_dlssd.dll`, **310.7.129**;
- `fg=2`: Streamline's `bin\sl\nvngx_dlssd.dll`, **310.9.16**.

A/B on 310.9.16 (foundry, static, separate runs `-153811` preset 0 and
`-153033` preset 6): evaluate 2.50-2.56 ms vs 2.50-2.55 ms, and the frames differ
by 1.15 PQ codes on average (median 0), below the same-mode floor above. The
default on 310.9.16 is F, as its header says ("Default model RR2"). `rr_preset=6`
is pinned so an OTA snippet cannot change the model silently. 310.7.129 (the
game's snippet, `fg=0`) also accepts preset 6 (creation Success, Goblin Camp
run, evaluate 2.6-2.7 ms); whether that older snippet really runs RR2 or falls
back to its default is **not known** (NGX does not report the model). Note that
with `fg=0` NGX loads the game's snippet, so the headless test runs and the
player's `fg=2` setup can run different RR versions.

## Known limits and next steps

- **RR before post, not verified:** a DoF frame on the final build (the DoF
  weights were checked only on an earlier build); a motion/temporal quality
  metric (the expected gains are in motion, dark 1-spp areas and DoF scenes);
  whether 310.7.129 runs RR2 for preset F; the
  SSS guide's effect on skin (it is set, non-finite-free, but not A/B'd).
- **RR before post, fallbacks:** effects the chain does not model (the Goblin
  Camp cinematic's chromatic aberration, tint vignette and overlay, and the
  load transition) switch the frame back to the graded input, with one RR
  history reset at each switch. Modelling them (the tint is per-channel
  affine; the aberration needs an output-resolution intermediate) would remove
  those switches.
- **Fixed:** the once-per-run `VUID-vkDestroyImage-image-01000` at level-load
  transitions (stale descriptors in our set ring); see "Lifetime of the game's
  images".

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

## Game upscaler guard (`game_dlss_guard`, 2026-11-05)

**Incident.** After the RenoDX DLSS5 ReShade addon was installed, two player
sessions logged `no DLSS feature has been created through _nvngx.dll`. The
game's `graphicSettings.lsx` had lost `ResolutionUpscaleType` and `DLSS2Mode`
(the 08:28 backup had 3 and 2). BG3 rewrites the file from defaults after a
start where DLSS failed; the addon's first launches exited after 2-5 s. Every
later start then runs with no upscaler, so the game never creates DLSS and RR
never runs. Test runs were already protected by `scripts/ensure-settings.py`;
player starts were not.

**The game-folder snippets are not involved with fg>=2.** The game's NGX Init and
Shutdown are stubbed (`fg_game_ngx=2`, `layer/streamline.cpp`), so DLSS SR and RR
run on Streamline's NGX, which loads `bin\sl\nvngx_dlss.dll` and
`nvngx_dlssd.dll`. The addon's own log confirmed those paths. Only with `fg=0`
does the game's own Init pick the game folder's snippets. A hook that rewrites the
game's feature path list was not built. The driver's Init exports use the
static client's undocumented arguments (the public `FeatureCommonInfo` overload
does not exist there), so the hook could not be checked offline.

**Guard.** With `game_dlss_guard=1` and `rr=1`, BG3RTX.exe sets
`ResolutionUpscaleType=3` before every game start, and sets `DLSS2Mode=2` only when
the entry is missing, so the player's own DLSS mode is kept. It backs the file up
to `graphicSettings.lsx.bg3rtx-bak` first (`guard_game_upscaler`,
`tools/launcher/launch.cpp`; tested in `launcher_launch_test`). Default 1 (2026-11-05, user go): the same rule as `scripts/ensure-settings.py`, which runs before every queue live. The first
player start through BG3RTX.exe is the confirmation. To check: put the reset file in place, start the game
from BG3RTX.exe and look for `[ngx] upscaler for feature ...: Ray Reconstruction`
in `bg3rtx.log`. The launcher log should name the repair.

## Temporal replayed post (`rr_post_temporal`, Ultra, 2026-11-04)

The ef1 12:10 evidence isolates the replayed jittered CoC coverage/residual and
bloom: hair-over-DoF residual 7.6/13.1 versus late RR 3.1 x 1000 PQ; brightest
contours 7.3/6.1 versus 1.5. Stable face residual is only 0.2-0.35.
`rr_post_temporal=1` adds one render-resolution pass after bloom and the RR evaluate,
before the output grade. It reprojects A/C and bloom with RR's scaled motion plus
previous-minus-current jitter, clamps each accepted history to the current 3x3
range, then accumulates at alpha 1/16 (one Halton-x cycle). Exposure stays fresh.
Four bilinear history taps are tested independently against the current depth
footprint plus two D24 roundings and an MV one-pixel footprint plus two fp16
roundings. Failed taps are excluded; zero valid weight selects the fresh value.
Camera/NGX reset, RR mode switch, skipped frames, key toggle and input/output resize
invalidate history. Final samples AC/EB at uv + jitter/render size.
Six device-local RGBA32F images (~250 MiB at 2560 x 1067) avoid fp16 EMA stagnation.
Original rr_post shader entries remain unchanged; one auto-discovered standalone
request adds two entries (manifest delta 0). No host-visible atomics or ABI change.
CPU reference gates constant energy, alternating/iid coverage variance, affine
linearity, clamps, invalid history and depth/MV rejection. The live jobs under
`tools/live_batches/post_temporal` compare 0/1/0/1 on Dialogue3 and static S8,
including real Ultra. Targets: hair and bright-contour residual reach the supplied
late-RR level, linear far-background energy and stable face remain within OFF
repeat noise. Dialogue3 VALID 032335 measured 0.152-0.153 ms; paired sign tests were inconclusive
(p 0.17-0.83). On 2026-11-04 the user promoted it on visual judgement: DoF much more
stable. Ultra enables it; Low/Medium/High keep it off. Lighting matches the RR category;
DLSS, RR and rr_post=1 remain required. Check fine white hair over DoF and sun-lit rims
in Test Dialogue3. This is a visual decision, not a statistical quality pass.

Dialogue3 drift repair (pt-d3-analysis, 2026-11-03): 12 counterbalanced adjacent OFF/ON pairs; freeze masks from all OFF windows and use a Bonferroni exact paired sign gate (12 observables, minimum 8 pairs). See post_temporal/predicted.json for the derived 338.9 s plan, 818.9 s load/readiness bound and offline fixtures.
The earlier four-window receipt was insufficient (2 pairs). Later VALID 032335 had 10 pairs; all 12 primary sign tests missed significance, float energy +0.1492% failed, and background registration failed. S8 remains unqualified; the Ultra promotion follows the explicit visual verdict above.


### L11/L12/L17 submission redo (layer-submit2, 2026-11-06, READY_FOR_LIVE)

The reverted `ae625cef` used one pending temporal/guide producer and refused later
recordings until its exact ID was published. Recording and submission are separate:
`BeginCommandBuffer` assigns a new ID; `CmdExecuteCommands` copies secondary IDs;
only primaries enter `QueueSubmit(2)`. A secondary re-begin invalidates its old
recording. Submitting a different primary cannot publish that ID. The global guide
gate also refused RR before a separately recorded G-buffer producer was submitted.
The descriptor change pinned executable recordings in 256 slots; completion alone
cannot release those sets, so later post steps could fail even when the GPU was idle.
These are code-confirmed failure paths; no new game trace was taken offline.

The redo retains immutable descriptor leases but grows pool pages at the retained
recording high water mark. Reset releases ownership; the lifetime tracker still
proves GPU completion before reuse. Every full-overwrite guide writer carries its
own UNDEFINED-to-GENERAL barrier, ordered after prior accesses. Its initialization
is published only by a submitted ID, without gating RR on one global pending ID.
Each temporal slot retains producer provenance and metadata independently. Only a
submitted previous-frame producer can be sampled. A missing/pending/discarded
producer uses the fresh sample and still runs temporal plus output-resolution grade.
A newer recording cannot erase an older slot's pending publication. An older
resubmitted writer invalidates metadata for a newer writer to that same slot.
Cold dispatches alias unused history descriptors to the initialized output, avoiding
an UNDEFINED transition that would discard the other slot's pending contents.

Existing keys/presets are unchanged; variant request delta is 0. Shared shader_identity
proves 218/218 requests byte-identical to HEAD 5256bec2d25a by identical input keys.
Offline tests cover N/N+1 before submit, secondary ID snapshots, discard/re-begin,
steady-state submitted history, older resubmission and 600 retained descriptor sets.
Final `scripts/build.ps1 -Test`: PASS 196/196 (20 s build, 180 s tests).
Live gate for the coordinator: `rel1-smoke-ultra-s8` must contain `[rrpost] gpu temporal`
lines and output-resolution grade > 0 (rejected attempt: 0 lines and 0.000 ms;
qualified baseline: 32 lines and 0.079 ms). GPU cost is pending that authorized smoke.
