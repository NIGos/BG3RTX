# Roadmap

Target: BG3 Vulkan. Final per-frame order:
1. Hybrid GI (and later hardware RT) injected after the lighting pass, before fog.
2. DLSS **Ray Reconstruction** replacing SR. It denoises our GI and upscales.
3. DLSS 5 NR (optional, through dlss5-bridge).
4. DLSS-G MFG at present, fed by the hudless frame plus the UI from the UI composite pass.

Background research: [research.md](research.md) and the agent findings
summarised in the sections below.

## Status (2026-10-26)
| Track | State |
|---|---|
| Phase 0, frame mapping | **Done.** See `FRAME_MAP.md`: pass order, G-buffer decode, the DLSS contract, the UI composite. Unattended loop: `auto-run.ps1` (headless, `-Move`, `-Validation`). |
| A, Ray Reconstruction | **Done, merged.** RR replaces SR with G-buffer guides, about +1.8 ms GPU, live `rr` switch. See `TRACK_A_RR.md`. |
| B, DLSS-G | **In progress** on its own branch. FG is reported working in game; merge pending verification. |
| C, GI | **v1 in main.** Ray-traced visibility folded into the game's AO, one-bounce GI, MV reprojection, sun contact shadows. About 0.3 ms GPU, validation-clean. See `TRACK_C_GI.md`. |
| D, hardware RT | **Phase 1 done** on its branch: G-buffer geometry captured every frame by transform feedback (all 835 draws / 36 VS, 3.16M triangles, 97.8% depth match, 0% missing), +0.45 ms; BLAS refit 0.55 ms, rebuild 2.5–3.4 ms. See `TRACK_D_RT.md`. |

## Phase 0 — map the frame (blocks everything)
- [ ] Trace one frame in an outdoor area and one in a dungeon. Run `analyze_trace.py --disasm`.
- [ ] Find these passes by signature:
  - G-buffer: normals are octahedral in `.xy`. Check for albedo/roughness.
  - Lighting: Lambert and Emission targets (only with SSS on).
  - Fog.
  - Transparents.
  - DLSS evaluate: Color R11G11B10F at render resolution, MV R16G16F, depth D24S8, output at display resolution.
  - Post and tonemap (LUT).
  - UI composite: `t0` is the scene, `t1` is the UI with alpha.
- [ ] Find the per-camera UBO. On DX11 it maps to bg3se `rf::sb::PerCamera`: View at 0x00, JitteredProj at 0x40, Near at 0xC0, Far at 0xC4, WorldTranslate at 0xD0. Confirm the same layout on Vulkan.
- [ ] Host feature: **image readback**. Dump the attachments of a chosen pass signature to `.dds`/`.exr` so G-buffer contents can be inspected without RenderDoc.
- [ ] Optional: RenderDoc (Vulkan) for single-capture deep dives.

## Track A — DLSS Ray Reconstruction (layer: `ngx.cpp`)
- Hook the game's NGX Vulkan create and evaluate. The bridge already does this; reuse what it learned: the hooks sit in the driver loader and snippet layers, not in `bg3.exe`.
- Create a DLSS-D (RR) feature in place of SR. Feed it these guides from the G-buffer: diffuse albedo, specular albedo, normals, roughness, depth, MV.
- BG3 has no pure albedo target. Candidates: derive it from Lambert lighting, or capture the material pass.
- Needs the DLSS SDK headers (NGX + RR programming guide).

## Track B — real frame generation (layer: `streamline.cpp`)
- Streamline manual hooking on Vulkan:
  - `slGetFeatureRequirements` → add extensions and queues at `vkCreateDevice`, including a dedicated optical-flow queue family.
  - Route swapchain create/acquire/present through Streamline; call `slSetVulkanInfo`.
- Tag depth (depth-only view), MV and backbuffer. Then add hudless (the UI composite's `t0`) and the UI with alpha (its `t1`).
- `sl::Constants` come from the per-camera UBO (Phase 0). Matrices are row-major and unjittered.
- Needs the Streamline SDK.

## Track C — hybrid GI (core + shaders, hot-reloaded)
- Screen-space hierarchical depth tracing at render resolution, using G-buffer normals.
- A world-space hash-grid radiance cache (SHaRC-style) catches rays that leave the screen. It's fed from the previous frame's lit pixels.
- Screen-space ReSTIR GI resampling.
- Denoising: RR (Track A) once it exists; NRD as a fallback.
- Modulate by diffuse albedo; inject at the pre-fog pass.

## Track D — hardware RT (long-term research; the game is frozen, so this is viable)
- Enable `VK_KHR_acceleration_structure` + `ray_query` at `vkCreateDevice`.
- Capture world-space geometry by patching vertex shaders to write positions into a buffer. This covers skinning and instancing.
- Build and refit BLAS/TLAS per frame; use any-hit for alpha-tested foliage.
- Ray-query shadows, AO, GI and reflections. Shade hits from the screen or the radiance cache.

## Dependencies to fetch
| What | For | From |
|---|---|---|
| DLSS SDK (NGX headers, RR guide) | Track A | github.com/NVIDIA/DLSS |
| Streamline SDK | Track B | github.com/NVIDIA-RTX/Streamline |
| NRD, SHaRC | Track C | github.com/NVIDIA-RTX/NRD, /SHARC |
| RenderDoc | Phase 0 (optional) | renderdoc.org |
