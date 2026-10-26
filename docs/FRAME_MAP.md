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
| 5 | Normals copy (not velocity) | RG16F: G-buffer RT0 (stereographic normals) copied for flagged pixels (RT2.a bit 3) | fs `f90209c7` |
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
| 0 | RG16F | **Normals**, stereographic projection: `xy/(1-z) * 0.5625 * 0.5 + 0.5` (fs `cad41eff`, output 0) | G-buffer fragment shader |
| 1 | RGBA8 | **Base colour / albedo**, unlit, plus alpha ≈ 0.95 | preview `d94` is clean albedo |
| 2 | RGBA8 | **Encoded normals** (+ a material value in A, 0–0.33) | false-colour preview `d95` |
| 3 | RGBA8 | Almost all zero: emissive or sparse material flags | mean ≈ 0.0005 |
| 4 | RGBA8 | Per-object constant colour: object ID / material ID | preview `d97` |

Still to decode, from the G-buffer fragment shaders' outputs:
- the exact normal encoding in RT2;
- where roughness and metalness live;
- (RT0 is decoded: stereographic normals, see above.)

## Inputs for each track

- **Ray Reconstruction (Track A).**
  - Inputs from the scene: colour (step 15), depth (D24S8), motion vectors (step 5), and the DLSS SR create/evaluate parameters (to be hooked).
  - Guides from the G-buffer: diffuse albedo (RT1), normals (RT2, once decoded), roughness (TBD).
  - Specular albedo has to be derived.
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
