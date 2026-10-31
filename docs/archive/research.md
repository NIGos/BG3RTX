# BG3 — ray tracing lighting and real frame generation: feasibility and plan

Date: 2026-10-25 · Machine: RTX 5090, driver 32.0.16.2101 · Game: `D:\SteamLibrary\steamapps\common\Baldurs Gate 3\bin`

## State on disk
- `bg3.exe` (Vulkan) and `bg3_dx11.exe` (D3D11), both calling NGX DLSS SR natively.
- DLSS SR/RR 310.7.129, DLSS NR (DLSS 5) 310.8.0.0, XeSS 2.0.1.
- ReShade 6.8.0 (`dxgi.dll` for DX11; global Vulkan layer in `C:\ProgramData\ReShade`).
- Installed in `bin`: REST only. `dlss5-bridge.addon64`, `renodx-dlss5.addon64`, `renodx-baldursgate3.addon64` are in `bin\bkp`.
- dlss5-bridge log confirms, on Vulkan: Color R11G11B10F 2560x1067, Depth D24S8 reversed, MV R16G16F low-res (scale -1,-1), jitter, Output 3840x1600 — every frame.

## What we know about BG3's renderer
- Deferred. Octahedral normals in `.xy`. Separate Lambert (diffuse) and Emission lighting targets (only with in-game SSS on). MVs in pixels.
- Order: shadows → G-buffer → lighting → **[pre-fog injection point]** → fog/water/transparents → DLSS → post (bloom/DoF…) → tonemap (3D LUT) → grading → **UI composite** (one fullscreen pass: `t0` scene, `t1` UI with alpha; RenoDX hash `ui_0x36496C32` on DX11).
- Per-camera cbuffer (DX11 PS slot 11) matches bg3se `rf::sb::PerCamera`: View @0x00, JitteredProjection @0x40, Near @0xC0, Far @0xC4, WorldTranslate @0xD0. bg3se also exposes the camera and sun direction.
- DLSS input is pre-tonemap linear HDR; UI is drawn after upscaling at display resolution.

## Frame generation
- **Smooth Motion** works on the final presented image only: no game MVs, no depth, no UI separation. Expect UI and text artifacts and roughly 10 ms more latency than DLSS-G.
- **Real DLSS-G/MFG is feasible.** PureDark shipped a paid one for BG3 in 2023, on both D3D11 and Vulkan, requiring in-game DLSS.
- **Vulkan route:** Streamline manual hooking.
  - Hook `vkCreateInstance`/`vkCreateDevice` to add the extensions and queues reported by `slGetFeatureRequirements`, including a dedicated optical-flow queue family.
  - Route the swapchain and present calls through Streamline.
  - Tag depth (depth-only view), MVs and backbuffer; add hudless (scene input of the UI composite) and UI color+alpha (`t1`).
  - Fill `sl::Constants` from the per-camera cbuffer or bg3se.
  - Hardest part: coexisting with ReShade's Vulkan layer.
- **D3D11 route:** Streamline has no D3D11 DLSS-G, so presentation would have to move to a D3D12 swapchain via interop. Harder.
- OptiScaler OptiFG, DLSS Enabler and dlssg-to-fsr3 do not help here (DX12-only, or they need an existing DLSS-G).

## Ray-traced lighting — approaches ranked
1. **Custom hybrid GI** (best quality/effort):
   - Screen-space hierarchical depth tracing, using G-buffer normals.
   - On a miss, fall back to a world-space hash-grid radiance cache (SHaRC-style, fed from previous frames).
   - Screen-space ReSTIR GI resampling, then NRD denoising.
   - Injected at the **pre-fog point** via a draw hook, at render resolution, modulated by the diffuse/Lambert target; DLSS then upscales it.
   - About 2–5 ms (estimate). Weeks of work.
2. **iMMERSE Pro RTGI / NiceGuy Complete RT** placed pre-DLSS via REST. Low effort; screen-space artifacts; tints fog and transparents if placed on the DLSS input instead of pre-fog.
3. **Option 1 plus Ray Reconstruction** replacing SR (feed albedo, normals, roughness). Potentially the best denoising; unverified on BG3.
4. **Real hardware RT** (Vulkan only): enable `VK_KHR_acceleration_structure`/`ray_query` in the game's device.
   - Capture world-space vertices by patching vertex shaders, build or refit the BLAS each frame.
   - Use ray queries for shadows, AO, GI and off-screen reflections; shade hits from the screen or the radiance cache.
   - Months of work; fragile across game patches.
5. **D3D11 + D3D12 DXR interop:** same as 4, plus cross-API sync.
6. **RTX Remix:** still DX8/DX9 only (1.6, Oct 2026). The DX11 community fork is unproven and replaces the whole renderer. Not viable.

## DLSS 5 interplay
- DLSS 5 reinterprets the frame it is given; it does not add your GI a second time.
- More physically correct input lighting gives it a better result, so GI helps it.
- Risk: its contact shadows and relighting stack with your AO, causing over-darkening. Lower AO strength and A/B test.
- Cost is about 50–60% fps, so FG on a 5090 is what pays for DLSS 5 + GI.

## Recommended target stack (Vulkan, `bg3.exe`)
- **Pre-fog:** hybrid GI (approach 1), with NRD or RR.
- **Upscale:** DLSS SR (or RR).
- **Output:** DLSS 5 NR via the bridge.
- **Before present:** DLSS-G MFG via Streamline, fed hudless + UI from the UI composite pass.

Vulkan is the platform where RT and FG both live in one device.

## Plan
0. Today: Smooth Motion in the NVIDIA App as a baseline, plus the DLSS preset override.
1. Mapping (1–2 days):
   - RenderDoc or Nsight captures on both renderers with DLSS and SSS on.
   - Confirm G-buffer formats, the end of the lighting pass, where the UI composite sits, the cbuffer layout, and the matrix at 0x40.
   - Find the SPIR-V equivalents of the DX11 hashes.
2. Frame generation (weeks): Vulkan layer with Streamline manual hooking, prototyped without hudless first, then with hudless + UI.
3. GI (weeks): hybrid SS + SHaRC + ReSTIR + NRD at pre-fog.
4. Optional research: ray query with captured geometry for shadows and reflections.

## Sources
- Streamline DLSS-G: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md
- Streamline manual hooking: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideManualHooking.md
- PureDark BG3 FG: https://www.dsogaming.com/pc-performance-analyses/baldurs-gate-3-gets-a-dlss-3-mod-that-doubles-its-performance-in-cpu-bound-scenes/
- OptiScaler: https://github.com/optiscaler/OptiScaler
- bg3se Camera.h / RenderFramework.h: https://github.com/Norbyte/bg3se
- RenoDX BG3: https://github.com/clshortfuse/renodx/tree/main/src/games/baldursgate3
- REST BG3: https://github.com/4lex4nder/ReshadeEffectShaderToggler-BG3
- RTX Remix release notes: https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/changelog/remix-releasenotes.html
- Raysterizer: https://maknee.github.io/blog/2022/Raysterizer/
- doaxvb-re DXR GI: https://github.com/NoRain211/doaxvb-re/pull/131
- NRD: https://github.com/NVIDIA-RTX/NRD · SHaRC: https://github.com/NVIDIA-RTX/SHARC · RTXDI: https://github.com/NVIDIA-RTX/RTXDI
- Pascal Gilcher world-space PT Q&A: https://wccftech.com/path-tracing-reshade-qa-with-pascal-gilcher-aka-marty-mcfly/amp/
- DLSS 5: https://nvidianews.nvidia.com/news/nvidia-dlss-5-delivers-ai-powered-breakthrough-in-visual-fidelity-for-games · https://guru3d.com/review/nvidia-dlss-5-technical-preview-review
