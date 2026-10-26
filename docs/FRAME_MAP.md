# BG3 Vulkan frame map

Built from automatic captures `captures/20261025-234821` and `-235042`: Steel Watch Foundry, static camera, HDR10 output, DLSS Quality.

- **Resolution.** The game renders at 2560x1067 and outputs 3840x1600.
- **Per frame.** About 166 units (passes, dispatches, copies), 54 render passes and about 3100 draws.
- **Image ids (`iNNNN`) change between runs.** Identify passes by fragment-shader hash, format and size instead (see ARCHITECTURE.md).
- **Unit numbers shift by a few between runs.** The order below is stable.

## Frame structure

| Order | What | Targets (render res unless noted) | Evidence |
|---|---|---|---|
| 1 | GPU culling/skinning compute (about 70 dispatches, one shader) | buffers | `cs:718d8329` |
| 2 | Depth prepass | D24S8 (reversed-Z-ish range 0.0014–0.029) | |
| 3 | **G-buffer** | 5 colour + depth, see below | 3D geometry, about 3000 draws |
| 4 | Linear depth | R32F | range 0.0023–0.046 |
| 5 | RG16F copy of G-buffer RT0 | fs `f90209c7` copies RT0 (stereographic normals) for pixels flagged in RT2.a bit 3; in `-235042` its stats equal RT0's | Track B found this; cross-check which RG16F image DLSS SR actually receives (step 6b's MV image vs this one) with Track A's evaluate hook |
| 6 | Decal / G-buffer second pass | the same 4 colour targets + depth | |
| 6b | **Motion vectors** | RG16F + D24S8 (render res). First draw is a full-screen camera pass (fs `788dfdf60d8ed4cd`): world position from linear depth, projected with the previous view-projection; then dynamic objects. Values in pixels, current − previous, jitter removed | used by DLSS SR and DLSS-G (TRACK_B_FG.md) |
| 7 | **Deferred lighting** (several passes, additive) | R11G11B10F HDR accumulation | max 2–6, mostly dark (point lights) |
| 8 | Half-res AO / blur chain | R16F, RGBA16F 1280x534, R8 | |
| 9 | SSR / AO composite | RGB10A2 ping-pong | |
| 10 | Shadow cascades | 4–5 × D16 2048² | |
| 11 | Screen-space shadow mask | R8 | reads the cascades |
| 12 | Volumetrics / clustered lighting (1/9-res 285x119 compute, then 15 × `dispatch_indirect` into two R11G11B10F) | | |
| 13 | **Compose + fog + transparents** | R11G11B10F scene colour; a copy for refraction, then a forward pass | max ≈ 34 |
| 14 | Post at render res: DOF / TAA prep, exposure (64² R16F), bloom pyramid down to 80x34 and back up | | |
| 15 | **DLSS input** | R11G11B10F 2560x1067, linear HDR, no UI | `d157` |
| — | **DLSS SR evaluate** | output R11G11B10F 3840x1600 | Runs through NGX CUDA kernels (`VK_NVX_binary_import`), so the tracer shows no dispatch |
| 16 | **Tonemap** (output res) | RGB10A2 3840x1600 (HDR10/PQ encoded), reads the DLSS output plus a 64² LUT | `d158` |
| 17 | UI: widgets, blur chain for panel backgrounds (1920→992→512), final UI | RGBA8 3840x1600 with alpha | `d151` (alpha is a clean UI mask) |
| 18 | **UI composite → swapchain** | reads tonemapped scene + UI RGBA8 | last pass; matches RenoDX's `ui_0x36496C32` (t0 scene, t1 UI) |

## G-buffer (one render pass, 5 colour + depth)

| RT | Format | Content | How we know |
|---|---|---|---|
| 0 | RG16F | **Normals, stereographic projection** (see below). Not velocity: the velocity target is a separate RG16F written right after linear depth | G-buffer fs `aa4efa4d` writes `rg = n.xy/(1-n.z) * 0.28125 + 0.5`; tiled lighting cs `f469db78` decodes it |
| 1 | RGBA8 | **Base colour** (rgb, clamped 0–1), a ≈ 0.95 | fs `aa4efa4d` location 1; lighting uses it as baseColor |
| 2 | RGBA8 | x **perceptual roughness**, y **specular reflectance** (F0 = 0.16·y² for dielectrics, a per-material constant), z **metalness**, w bit flags (bits 4–6: light-channel mask) | fs `aa4efa4d` location 2; lighting clamps roughness to ≥ 0.09 and computes `F0 = mix(0.16·y², base, metal)`, `diffuse = base·(1-metal)` |
| 3 | RGBA8 | Almost all zero: emissive or sparse material flags | mean ≈ 0.0005 |
| 4 | RGBA8 | Per-object constant colour: object ID / material ID | preview `d97` |

**Normal decode** (identical to the game's): `e = rg·3.5554 − 1.7777`, `t = e·e + 1`; if `t > 5.4e10` then `n = (0,0,1)` else `n = (2e/t, 1 − 2/t)`. The decoded normals are **world-aligned, Y up**: floors decode to +Y, walls to ±X/±Z, independent of the camera pitch (`captures/20261026-011007`, guide dump `d167`). The lighting shader multiplies them by `mat3(cb._m6)` into its own lighting space; its view vector comes from the frustum-corner rays `cb._m7[0..2]` × linear depth.

**The G-buffer memory is reused before DLSS.** At the DLSS evaluate, RT0's image holds unrelated data (values up to ±65504, 86 non-finite texels, a 1280x128 block of foreign content in the top-left; `captures/20261026-011007` dump `d169` vs the same image at the G-buffer pass end, `d116` of `-005736`). Anything that needs the G-buffer late in the frame must copy or convert it at the end of the G-buffer/decal pass. Track A does exactly that (`layer/ngx.cpp`, `record_guides_at_gbuffer`).

G-buffer pass signature for code: colour formats `[R16G16_SFLOAT, R8G8B8A8_UNORM ×3(+1)]`, depth `D24_UNORM_S8_UINT`, render resolution; both the 5-target G-buffer pass and the 4-target decal pass match, and each leaves RT0–RT2 in `COLOR_ATTACHMENT_OPTIMAL` and depth in `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`. The G-buffer images are recreated on every level load (new image ids).

## DLSS SR contract (hooked in `_nvngx.dll`, `layer/ngx.cpp`)

BG3 links the NGX client statically (bg3.exe exports `NVSDK_NGX_*`) and calls the driver's loader `_nvngx.dll`; one SR feature (feature 1) is created about 15 s into loading, through `NVSDK_NGX_VULKAN_CreateFeature` (not `CreateFeature1`).

| Create | Value |
|---|---|
| Width × Height → OutWidth × OutHeight | 2560x1067 → 3840x1600 |
| PerfQualityValue | 2 (MaxQuality) |
| DLSS.Feature.Create.Flags | 107 = IsHDR · MVLowRes · DepthInverted · DoSharpening (deprecated) · AutoExposure; **not** MVJittered |
| DLSS.Enable.Output.Subrects | 1 |
| Render presets | not set (default) |

| Evaluate | Value |
|---|---|
| Color | R11G11B10F 2560x1067 (step 15's target), usage 0x17 |
| Depth | D24S8 2560x1067, view aspect DEPTH\|STENCIL, reversed Z (far = 0) |
| MotionVectors | R16G16F 2560x1067 (the velocity target of step 5) |
| Output | R11G11B10F 3840x1600, ReadWrite, usage 0x1f (has STORAGE) |
| ExposureTexture, bias mask, transparency mask | not set |
| Jitter | per frame, render-pixel units, a Halton-like sequence in ±0.5 (e.g. +0.219 −0.426, −0.000 −0.167, +0.250 +0.167 …) |
| MV.Scale | −1, −1 |
| DLSS.Pre.Exposure / Exposure.Scale | 1 / 1 |
| Sharpness | 0.35 (the in-game slider; ignored by current DLSS) |
| Reset | 1 on the first frame |
| Render subrect | 2560x1067 at 0,0; input and output subrect bases 0,0 |

The parameter block is shared between create and evaluate (as the dlss5-bridge found), so the size keys in an evaluate block are whatever the last create wrote; use the create-time values per handle.

## Inputs for each track

- **Ray Reconstruction (Track A)** — done, see [TRACK_A_RR.md](TRACK_A_RR.md).
  - Inputs from the scene: the game's own SR evaluate block (colour, depth, MV, output, jitter, MV scale).
  - Guides, built at the G-buffer pass end: diffuse albedo `RT1·(1−RT2.z)`, specular albedo `EnvBRDFApprox2(F0, RT2.x², N·V)`, normals decoded from RT0 with roughness RT2.x packed in w.
- **Frame generation (Track B).**
  - Hudless colour = the tonemapped image from step 16, and UI = the RGBA8 target from step 17. Both are inputs of the final composite pass.
  - Depth and MV as above.
  - Camera matrices: the per-camera UBO, set 0 binding 2. Layout: std140, row-major, used as `v * M`:
    - `m0`: world view;
    - `m1`: jittered reversed-Z infinite projection (near 0.1, jitter in row 2);
    - `m2`: camera-relative jittered view-projection;
    - `m3`, `m4`: near and far (0.1, 40000);
    - `m6`: camera world position.

    Set 0 binding 5 holds the previous frame's values: `m2` is the previous view-projection, `m3` the previous camera position, and `m5`/`m6` the current and previous jitter in pixels. Both are read on the CPU by `layer/streamline.cpp`.
- **GI (Track C).**
  - Inject between steps 7 and 13, after deferred lighting and before compose/fog/transparents.
  - Inputs: G-buffer albedo and normals, linear depth (R32F), and the lighting buffer.

## Present behaviour (affects all tracks)

- **BG3 keeps rendering but stops calling `vkQueuePresentKHR` while its window isn't in the foreground.** `auto-run.ps1` keeps the window focused.
- **NVIDIA Smooth Motion is enabled for BG3 in the NVIDIA App.** Its layer (`VK_LAYER_NV_present`) inserts extra presents carrying `VK_NV_present_metering`. `-Clean` disables it with `DISABLE_LAYER_NV_PRESENT_1`; it has to stay off once DLSS-G is ours.
- **The swapchain is HDR10:** A2B10G10R10, `VK_COLOR_SPACE_HDR10_ST2084_EXT`. Usage is 0x17 without Smooth Motion and 0x2 with it.
