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
| B, DLSS-G | **Done, merged.** Streamline DLSS-G 2x/3x/4x inside the host layer; shares the game's NGX instance. Verified with RR + GI on (10 min stable, no API errors). See `TRACK_B_FG.md`. Verified with the display on: 2x is about 103 rendered / 206 displayed fps, 4x about 66 / 266. Pan motion and UI are clean, output is HDR10. On by default (`fg=2`). |
| C, GI | **v1 in main.** Ray-traced visibility folded into the game's AO, one-bounce GI, MV reprojection, sun contact shadows. About 0.3 ms GPU, validation-clean. See `TRACK_C_GI.md`. |
| D, hardware RT | **Phase 2 done, off by default (`rt=1`).** AS + ray query on the game's device (instance API 1.3, the game's EXT BDA kept next to the core feature); per-frame BLAS refit 0.39 ms / rebuild 2.3 ms + TLAS from the XFB capture; ray-query sun shadows folded into the game's sun mask (0.1 ms) and ray-traced AO (0.13 ms); about 1.5 ms in total with a static camera. TLAS matches the depth buffer on 98.1% of pixels; validation-clean. Next: alpha test, cascade-pass casters, static/dynamic BLAS split. See `TRACK_D_RT.md` section 6. |
| D, phase 3 | **Merged, off by default** (`rt_alpha=1`, `gi_rt=1`). Alpha-tested BVH geometry (the primary-ray match goes from 98.1% to 99.1%) and hybrid ray-traced GI (trace 0.175 ms). Not yet verified in motion or outdoors. See `TRACK_D_RT.md` section 7. |
| Effect catalogue and quality audit | **Analysis done (2026-10-26).** The feasibility of the next RT effects, with enablers and a value/effort ranking, is in `TRACK_D_RT.md` section 8. The audit of the existing effects and the rules for new ones are in `QUALITY.md`. The prioritised list is below. |

## Next, prioritised (2026-10-26)

Order by value/effort. IDs refer to `QUALITY.md` (Qn) and `TRACK_D_RT.md` section 8 (D-…).

1. **Quality P1 fixes.** These are cheap correctness fixes.
   - [ ] Q1: reproject `prevRadiance` with the MVs, plus a depth check.
   - [ ] Q2: RT visibility *replaces* the game's SSAO instead of multiplying it; `gi_ao=1`.
   - [ ] Q11: RR normal guide in world space, true N·V, view/projection matrices passed to RR.
2. **The RR path for the GI** (Q4 + Q5).
   - With `rr=1`: full resolution, 1 ray per pixel, no checkerboard and no own temporal/upsample, a pcg3d/xxhash seed. This is cost-neutral.
   - With `rr=0`: blue noise and the improved accumulator (Q6, Q7).
3. **Soft sun shadows** (planned, refined in TRACK_D 8.5).
   - Cone of 0.00459 rad, the game's own sun disc. RR or SIGMA denoises.
   - Geometric-normal bias (Q10).
4. **D-EN1 light-list snapshot.**
   - Bind set 1 b13/b23/b24/b25/b29/b39/b40/b41.
   - Write `bg3_lights.hlsli` with an exact re-evaluation, validated against b10/b11.
   - Log `b0._m0.y` (Q3).
5. **D-LS ray-traced local-light shadows.**
   - v1: deterministic rays for shadowed lights.
   - Then unshadowed lights (to stop leaks).
   - v2: RIS 1-2 rays per pixel + RR.
6. **D-EM (a): emissive/FX pre-lighting target into the bounce source** (with Q8: diffuse-only bounce).
7. **D-RF ray-traced reflections.** Needs Q11 and D-EN4 (the exact specular-ambient delta).
   - Tiers (a) on screen + (c) probe fallback first.
   - Then tier (b) off screen with D-EN2 hit materials.
8. **Cascade-caster capture** (planned, 6.7 item 2) **+ D-EN3 retention.** This closes interiors, adds off-screen casters, and is the prerequisite for D-GI2 and D-VOL.
9. **D-GI2: screen-seeded SHaRC multi-bounce GI.** Replaces `gi_rt_fallback`; radius from 1 m to room scale.
10. **Later:**
    - D-DI (ReSTIR DI; fetch RTXDI);
    - D-TR (thickness translucency);
    - D-VOL (RT froxel sun visibility);
    - D-EN5 (lighting-shader patch, as an optimisation of the deltas);
    - Q6/Q7/Q9/Q12-Q15 polish.
11. **Not recommended:**
    - D-RFR refraction;
    - RT transparents/particles;
    - a full path-tracing replacement. D-PT is kept only as a reference/debug mode.

Every item follows the `QUALITY.md` rules R1-R10: sampling, a single denoiser path, composition without double counting, validation, A/B images, and the cost budget of ≤ 2.5 ms for all Track D effects.

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
- Effect catalogue (2026-10-26, `TRACK_D_RT.md` section 8):
  - local-light shadows (D-LS), reflections (D-RF), screen-seeded SHaRC GI (D-GI2), emissive bounce (D-EM), ReSTIR DI (D-DI);
  - translucency, volumetrics and refraction are low priority;
  - full path tracing is a reference mode only.

## Quality (cross-track, `QUALITY.md`)
- An audit of RR inputs, sampling, temporal filtering, upsampling, composition/energy, the sun penumbra and alpha handling (Q1-Q15).
- The binding rules R1-R10 for all new effects.

## Dependencies to fetch
| What | For | From |
|---|---|---|
| DLSS SDK (NGX headers, RR guide) | Track A | github.com/NVIDIA/DLSS |
| Streamline SDK | Track B | github.com/NVIDIA-RTX/Streamline |
| NRD, SHaRC | Track C | github.com/NVIDIA-RTX/NRD, /SHARC (both already in `external/`) |
| RTXDI | D-DI (later) | github.com/NVIDIA-RTX/RTXDI (not in `external/` yet) |
| Spatiotemporal blue noise (STBN) textures | Q5, the `rr=0` path | NVIDIAGameWorks/SpatiotemporalBlueNoiseSDK |
| RenderDoc | Phase 0 (optional) | renderdoc.org |
