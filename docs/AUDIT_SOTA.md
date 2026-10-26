# State-of-the-art audit: is each technique the best quality per millisecond? (2026-10-26)

**Question.** For each technique BG3RTX implements or plans, is it the current state of the
art for quality per millisecond on Blackwell and Ada, given that we inject into a closed
engine? Where it is not, what should replace it, at what cost and with what risk?

**Scope and method.**
- Read: the track docs, `QUALITY.md`, `AUDIT_RT.md`, `FRAME_MAP.md`, `research.md`, and the
  code listed in the brief.
- `AUDIT_RT.md` covers data correctness. Its findings are not repeated here; they are only
  referenced where they change a state-of-the-art verdict.
- Compared against shipped SDKs (DLSS 310.9.16, Streamline 2.14.16, NRD 4.18 and SHaRC 1.8.3,
  all already in `external/`), NVIDIA and Khronos documentation, and published research.
- Hardware facts for this machine come from `vulkaninfo` on the RTX 5090 with driver 621.01
  (`32.0.16.2101`). That run is the only command executed for this audit. The game was not run,
  and no code was changed.

**Evidence labels used throughout.**
- **[SDK]** a shipped NVIDIA or Khronos SDK, or a ratified extension.
- **[shipped]** used in a released game, with published numbers.
- **[NV-ext]** a vendor extension that this driver exposes, not ratified.
- **[research]** a paper or experimental library only.

**Costs.** All costs are GPU ms on the RTX 5090 at the 2560x1067 render resolution (3840x1600
output).
- "Measured" means a number from this repo's docs.
- "Est." means scaled from the measured 0.04-0.07 ms per million short rays (TRACK_D 8.3) or
  from a cited source.

## 0. The frame budget this audit optimises for

**What the target means.** 2x frame generation locked at 120 Hz gives a 60 fps base. Requiring
a rendered base of at least 65 fps means GPU headroom: the whole frame, including RR and the
DLSS-G cost, must fit in **≤ 15.4 ms**. The 0.8 ms of headroom over 60 fps absorbs spikes.

**Where the frame stands today.** In the foundry, `fg=2` uncapped renders about 103 fps
(TRACK_B), which is about 9.7 ms. That leaves about 5.7 ms of headroom there. The audit's
concern is therefore not the static foundry. It is:
- **(a) Camera motion.** The single-soup BLAS is rebuilt every frame while the culled draw set
  changes: 2.3-3.4 ms, against a 0.39-0.88 ms refit (TRACK_D 6.5, 7.1).
- **(b) Foliage-heavy outdoor scenes.** Alpha-tested traversal is unmeasured there.
- **(c) Act 3.** BG3 is commonly CPU-bound there, and no GPU work recovers that. Check with
  the Reflex/PCL latency stats before attributing a miss of 65 fps to RT.

**Pacing.** VSync with frame generation is not supported on Vulkan (`external/Streamline/docs/ProgrammingGuideDLSS_G.md`
§ known limitations). The supported way to hold exactly 60 to 120 is the **Reflex frame
limiter**, `sl::ReflexOptions::frameLimitUs = 16667`, together with VRR (`ProgrammingGuideReflex.md`
"Frame Rate Limiter"). Today `layer/streamline.cpp:1480-1485` sets only the Reflex mode.

## 1. Feasibility of the Blackwell and Ada features on this machine and in a layer

`vulkaninfo` on the RTX 5090, driver 621.01, Vulkan 1.4.357 runtime:

| Feature | Extension exposed here | How a layer would enable it | Notes |
|---|---|---|---|
| Opacity micromaps | `VK_EXT_opacity_micromap` rev 2 (also `VK_KHR_opacity_micromap` rev 1) | Append the extension and `micromap` feature to the copied device create info, the same scheme as AS/ray query in `layer/rtscene.cpp` | [SDK] Ratified (EXT). Hardware-accelerated on RTX 40/50. OMM SDK GPU baker supports Vulkan ([OMM SDK](https://developer.nvidia.com/rtx/ray-tracing/opacity-micro-map/get-started)) |
| SER | `VK_EXT_ray_tracing_invocation_reorder` rev 2, reorder hint = `REORDER`; `VK_NV_…` rev 1 | Extension + feature at `vkCreateDevice` | [SDK] EXT shipped in Vulkan 1.4.333 ([Khronos blog](https://www.khronos.org/blog/boosting-ray-tracing-performance-with-shader-execution-reordering-introducing-vk-ext-ray-tracing-invocation-reorder)). **Ray generation shaders only**: compute is explicitly "not ideal for reordering"; a ray query can be turned into a hit object (`hitObjectRecordFromQueryEXT`), but only inside a raygen shader ([proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_ray_tracing_invocation_reorder.html)). Our passes are all ray query in compute |
| Cluster AS (Mega Geometry) | `VK_NV_cluster_acceleration_structure` rev 4 | Extension + feature | [NV-ext], not ratified ([proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_NV_cluster_acceleration_structure.html)) |
| Partitioned TLAS | `VK_NV_partitioned_acceleration_structure` rev 1 | Extension + feature | [NV-ext], not ratified ([proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_NV_partitioned_acceleration_structure.html)) |
| Linear swept spheres | `VK_NV_ray_tracing_linear_swept_spheres` rev 1 | | Curve primitives only (hair, fur). Not applicable, see 2.2 |
| Displaced micro-meshes | **not exposed** (no `VK_NV_displacement_micromap`) | | Gone on this GPU and driver. Not applicable |
| Cooperative vectors | `VK_NV_cooperative_vector` rev 4 (+ `VK_KHR_cooperative_matrix`, `VK_NV_cooperative_matrix2`) | Extension + feature | [NV-ext]. RTXNS samples use Slang + Vulkan ([Khronos news](https://www.khronos.org/news/permalink/nvidia-posts-vulkan-rtx-neural-shader-samples)) |
| Position fetch | `VK_KHR_ray_tracing_position_fetch` | Extension + feature, plus `ALLOW_DATA_ACCESS` on the BLAS | [SDK]. Lets a hit read its triangle's vertices from the BLAS, so the 72 B/triangle UV/position soup lookup in `alpha_pass()` could drop its positions |

**Constraints common to all of them in this layer.**
- **Device creation.** The device is created once, at `vkCreateDevice`, with the game's 1.1
  request raised to instance API 1.3 (TRACK_D 6.1). Every extension above is device-level, so
  the existing copy-the-create-info scheme handles it, with the same fallback chain
  (retry without it).
- **Restart.** None of them can be turned on by hot reload. Each one needs a game restart, like
  `rt=1`.
- **Toolchain.** Our shaders are HLSL through DXC. For hit objects (SER) and cooperative
  vectors, DXC's documented support is DXIL (SM 6.9, DXC 1.8.2505,
  [release](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2505)).
  The Vulkan docs say HLSL reaches SER on SPIR-V only through `vk::ext_*` SPIR-V intrinsics.
  I found no documented DXC SPIR-V path for `SPV_EXT_shader_invocation_reorder`. So SER and
  cooperative vectors realistically mean **Slang** or hand-written SPIR-V intrinsics. That is a
  toolchain change (risk: medium).
- **Validation.** The Khronos validation layer is our R5 gate. It may have limited coverage of
  unratified NV extensions (unverified), which weakens the gate for Mega Geometry and
  cooperative vectors.
- **NGX and Streamline.** Both share the device. Adding extensions does not affect them, as
  the AS/ray-query addition showed (TRACK_D 6.1).

## 2. The table

Columns as requested. "Now" cites files. The gains are expected values, not measurements.

| Area | What we do now (files) | State of the art | Gap | Expected quality gain | Expected cost change (ms) | Implementation risk (injected layer) | Verdict |
|---|---|---|---|---|---|---|---|
| **BVH: BLAS strategy** | One BLAS over the whole per-frame XFB soup (2 geometries: opaque, alpha), `PREFER_FAST_BUILD\|ALLOW_UPDATE`; refit when the draw sequence hash matches, else rebuild; forced rebuild every 60 frames (`layer/rtscene.cpp:563-640`, `layer/geocapture.cpp`). Static: XFB 0.60 + convert 0.27-0.40 + refit 0.39-0.88 ms. **Motion: rebuild 2.3-3.4 ms every frame** | Per-mesh BLAS, built once for static content and **compacted** (Indiana Jones: compaction cut BLAS VRAM 1027 → 606 MB, `PREFER_FAST_BUILD\|ALLOW_UPDATE\|ALLOW_COMPACTION` on refitted dynamic BLAS, [NVIDIA blog](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass/)). Refit only deforming meshes. TLAS rebuilt per frame over instances. NVIDIA's long-standing guidance: keep AS work ≤ 2 ms through pruning and selective updates ([RTX best practices](https://developer.nvidia.com/blog/rtx-best-practices/)) [SDK, shipped] | Whole-scene rebuild on any change of the culled set, which happens on every pan. XFB and conversion are paid for static geometry every frame. No compaction | None directly. Indirect: no BVH decay, and it removes the motion spikes that would break the 15.4 ms floor | **Motion: −2.0 to −3.0 ms.** Static: −0.4 to −0.8 ms (skip XFB for cached static draws). Memory: −30-45% with compaction | **Medium-high.** Class A instanced draws (≈600 of 835) are culled (on the CPU, AUDIT_ENGINE_DATA §0), so their instance list changes every frame. A per-draw BLAS of world-space XFB output is therefore *not* stable. The stable unit is the **object-space mesh + per-instance matrix** (see "instancing" row). Skinned (class B) and wind (D) stay XFB/refit. Validate with the existing primary-ray gate (`rt_debug=3`, ≥ 99% match) | **UPGRADE** (top priority) |
| **BVH: instancing and dedup** | None: every instance is triangles in the soup (3.16M tris, 214 MB BLAS) | Instanced BLAS: one BLAS per unique mesh, TLAS instances carry transforms [SDK, universal] | Same mesh repeated N times costs N× build and memory | None directly; it is the enabler for the row above and for OMM caching | Part of the row above | **Medium-high, and feasible.** Class A's position path is exactly `P_object · M_instance` with the matrix rows in vertex attributes 4-6 (TRACK_D 1). So: build the object-space BLAS once from the game's own vertex/index buffers (format from the pipeline's vertex input state, keyed by buffer + offset + count). Write TLAS instance records **on the GPU** from the instance stream after the game's cull compute has filled it. Class C/E (matrix assembled in the shader, terrain) stay XFB | **UPGRADE** (as part of the split) |
| **BVH: opacity micromaps** | Alpha-tested geometry (0.83-0.93M of 3.1M triangles in an interior) is non-opaque. Every candidate runs `alpha_pass()`: a binary search over draw records, UV interpolation, and a Granite VT indirection fetch plus a tile-cache sample (`shaders/rt/rt_common.hlsli:96-180`). Measured cost of the alpha test: GI trace 0.124 → 0.175 ms; sun + AO 0.201 → 0.22 ms (TRACK_D 7.1-7.2) | **OMM** 4-state, baked once per mesh. Indiana Jones on an RTX 5080: TraceMain 7.90 → 3.58 ms (−55%), SHaRC update −55%, sun −14%. Shadow rays use 4-state; indirect rays force 2-state, so no any-hit at all ([NVIDIA blog](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass/)) [SDK, shipped] | Every alpha candidate does a dependent texture chain in the inline loop | None in the result (exact match is kept by "unknown" states → shader test). Allows 2-state OMM for GI rays, a small bias that is acceptable for diffuse | Interior: −0.05 to −0.1 ms. Foliage outdoors: est. −0.3 to −1.0 ms over all effects (unmeasured; scales with the share of foliage rays) | **Medium-high.** (1) Needs the per-mesh BLAS (OMM attaches to a BLAS geometry; rebaking the per-frame soup is not viable). (2) Granite VT alpha is residency-dependent: bake from the coarsest *resident* mip and mark mixed micro-triangles unknown; rebake when the tile set's sentinel changes. (3) OMMs survive **refits**, so wind-animated foliage keeps them. GPU baker: the OMM SDK supports Vulkan | **UPGRADE** (after the split) |
| **BVH: Mega Geometry cluster AS** | n/a | `VK_NV_cluster_acceleration_structure`: CLAS from ≤ 256-triangle clusters, BLAS over clusters. vk_animated_clusters (RTX 6000 Ada, 1440p): AS build 5.22 ms (triangles, 10% rebuilds) / 2.78 ms (refit only) → **0.80 ms** with cluster templates, at +5-10% render time; memory 564 → 271 MB ([sample README](https://github.com/nvpro-samples/vk_animated_clusters)). Alan Wake 2: −1 GB VRAM, +13% on a 4090; older RTX gains more ([Tom's Hardware](https://tomshardware.com/pc-components/gpus/testing-nvidias-rtx-mega-geometry-tech-vram-reducing-tech-a-leap-forward-for-path-traced-rendering)) [NV-ext, shipped in DX12] | For us only the **dynamic** subset matters after the split. The XFB soup is non-indexed, so clusters cap at about 85 triangles (≤ 256 vertices). Templates need stable topology per mesh, which skinned meshes have | Tighter BVH for skinned and wind meshes than decaying refits | Est. −0.1 to −0.4 ms on the dynamic refit, only in foliage/crowd scenes; trace +5-10% | **High.** Unratified NV extension; validation coverage unknown; clusters must be formed per mesh (meshoptimizer offline is impossible, so on the GPU or the CPU at first sight); a second BVH code path | **KEEP** refit now; revisit only if the dynamic refit exceeds 0.5 ms outdoors |
| **BVH: partitioned TLAS** | 1-instance TLAS, 0.024 ms (`rtscene.cpp:287-291`) | `VK_NV_partitioned_acceleration_structure`, for 10⁵+ instances where only some partitions change ([NVIDIA blog](https://developer.nvidia.com/blog/nvidia-rtx-mega-geometry-now-available-with-new-vulkan-samples)) [NV-ext] | After the split: about 10³-10⁴ instances; full TLAS build est. 0.1-0.2 ms (835 BLAS: 0.15 ms, TRACK_D 3) | None | ≤ −0.1 ms at best | Medium; not worth it | **KEEP** a plain TLAS (`PREFER_FAST_TRACE`, rebuilt every frame) |
| **BVH: build flags** | `PREFER_FAST_BUILD`; `rt_fast_trace=1` measured: rebuild 3.3 ms, no trace gain (TRACK_D 6.5) | Fast-trace for static, built once; fast-build + update for dynamic (Indiana Jones measured +5% trace from fast-trace OMM builds) | Only matters once static BLAS are built once | Small | Static BLAS with `PREFER_FAST_TRACE\|ALLOW_COMPACTION`: trace est. −0-5% | Low | **UPGRADE** with the split |
| **Streaming and skinned geometry** | XFB `gl_Position` of every G-buffer (or prepass) draw, solved to camera-relative world (`layer/geocapture.cpp`, `shaders/rt/xfb_to_world.hlsl`) | Engines stream object-space meshes plus skinning output into BLAS refits or CLAS | We cannot see the engine's mesh streaming, but the frame shows every resident mesh's buffers at draw time. Skinned positions already exist in SSBO set0/b13 (the skinning compute output, 20 B stride, TRACK_D 1) | Same geometry; the gain is cost | Skinned BLAS from b13 directly: XFB skipped for class B, est. −0.1 ms | Medium. b13 layout per VS variant must be decoded (12 variants) | **UPGRADE** (with the split; XFB stays the fallback) |
| **Traversal: ray query in compute vs RT pipeline + SER** | Every pass is inline `RayQuery` in compute, 8x8 tiles (`shaders/gi/*.hlsl`) | For short visibility rays and light hit shading, inline ray query is the efficient choice ([Arm](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/ray_tracing/rt03_ray_traversal/); Microsoft DXR notes: "many complex shaders will run better with dynamic-shader-based raytracing"). SER pays off when hit shading is divergent: Khronos reports +47% in the glTF path tracer ([Phoronix](https://www.phoronix.com/news/Vulkan-SER-Performance)) | Our "hit shading" today is a depth-tested screen lookup (`prev_radiance_at`) or an alpha test. There is little to reorder. The alpha-test divergence is better fixed by OMM | None today | 0 today. For a future tier-(b) reflection or multi-bounce pass with material fetches: est. −20-40% of that pass | **Medium.** RT pipeline + SBT inside the core; HLSL → SPIR-V hit objects need Slang or SPIR-V intrinsics (section 1) | **KEEP** ray query. Build the first pass that shades **off-screen materials** (D-RF tier b, D-GI2 update) as a raygen pipeline with SER |
| **Traversal: linear swept spheres** | n/a | LSS for hair/fur strands (Blackwell) | BG3 hair is alpha-tested triangle cards drawn by the G-buffer pass; no curve data exists to capture | — | — | — | **N/A** (not applicable to captured triangles) |
| **Local-light shadows** | Exact unshadowed contribution recomputed per light, then 1 soft ray per unshadowed light (solid-angle sphere sample, `pcg3d`), exact delta `c_i(v_i−1)`; 2.31 lights/px evaluated, 0.345 ms; RIS 1-ray option 0.219 ms (`shaders/gi/rt_local_shadows.hlsl`, `shaders/rt/bg3_lights.hlsli`; TRACK_D 9) | This is structurally the **ratio estimator** of Heitz et al., "Combining Analytic Direct Illumination and Stochastic Shadows" (I3D 2018): analytic unshadowed shading times stochastic visibility, with the noise confined to visibility. For tens to thousands of lights, ReSTIR DI / RTXDI (Bitterli et al., SIGGRAPH 2020; RTXDI 3.1 now adds RR-compatible decorrelation, [changelog](https://raw.githubusercontent.com/NVIDIA-RTX/RTXDI/main/ChangeLog.md)) [SDK, shipped in CP2077/AW2] | At 2-6 lights per pixel, one ray per light is near-optimal: no light-selection variance, unbiased. ReSTIR's advantage appears only with many lights per pixel, or with emissive triangles, which we cannot get (no emissive light list) | Already at the SOTA level for this light count. RIS gives equal expected quality and more noise | Keep 0.35 ms. Add an **adaptive cap**: per-pixel RIS with k = min(lights, 2) rays when lights/px > 4 (est. ≤ 0.25 ms worst case) | Low | **KEEP** (add the adaptive cap; ReSTIR DI only if a scene shows > 8 lights/px) |
| **Many-light sampling (RTXDI) generally** | Planned D-DI (ROADMAP 10) | RTXDI 3.x: ReGIR + ReSTIR DI/GI/PT; RT-pipeline sample on Vulkan (KHR_ray_tracing_pipeline since RTXDI 1.1) | **Infeasible inputs:** emissive triangles and their radiance are unknown (emissive goes to a screen-space FX target, TRACK_D 8.1); the light list is view-culled into froxels (b23/b24), so no global light BVH or ReGIR grid beyond the frustum | — | — | High for little gain | **Defer** (D-DI stays P3) |
| **GI: estimator** | 1 cosine ray/px full-res under RR, radius 1 m, alpha-tested; hit radiance = last frame's lit diffuse + specular at the reprojected pixel (multi-bounce by feedback); hidden or off-screen hits = occlusion only (`shaders/gi/gi_trace_rt.hlsl`, `core/gi.cpp`); 0.175 ms trace | **ReSTIR GI** (Ouyang et al., HPG 2021) + a world-space radiance cache (SHaRC: CP2077 RT Overdrive, Indiana Jones) + RR. Unreal Lumen does the same split: screen traces first, then world-space hits shaded from a surface cache | **Reach**: 1 m makes it an AO with near-field colour bleeding, not room-scale GI. **Off-screen** (2.2%) and **hidden** (11.7%) hits give no bounce. **Variance**: 1 spp with no reuse | **Medium-high**: room-scale bounce and colour bleeding, no darkening bias at off-screen hits | See the next two rows | — | **UPGRADE** |
| **GI: radiance cache (SHaRC / NRC)** | None (`gi_rt_fallback=0`) | **SHaRC** [SDK, shipped]: hash-grid cache, 2²² entries = 160 MiB, update / resolve / query passes; plain HLSL/GLSL helpers, works from compute with ray query (`external/SHARC/docs/Integration.md`). Indiana Jones SharcUpdate 1.99 → 0.89 ms with OMM. **NRC** [SDK experimental / research]: neural cache, trains on path vertices whose inputs include position, direction, normal, roughness and reflectances (Müller et al., "Real-time Neural Radiance Caching for Path Tracing", SIGGRAPH 2021); "experimental mode" at RTXGI 2.0 ([wccftech](https://wccftech.com/nvidia-rtxgi-2-0-available-next-frontier-ray-traced-visuals-neural-radiance-cache-spatial-hash-radiance-cache-dynamic-diffuse-global-illumination/amp/)) | The design in TRACK_D 8.6 (D-GI2, **screen-seeded** SHaRC) is the right adaptation. Standard SHaRC assumes a path tracer that can shade any hit; we can shade exactly only on-screen hits (the game's lit diffuse) and must terminate there | Medium-high: bounce at hidden and off-screen hits; radius 1 m → 8-30 m. Also serves **reflection tier (b) for glossy** (see the reflections row) | **+0.4 to +0.6 ms**, 160 MiB (TRACK_D 8.6 estimate; consistent with Indiana Jones numbers once scaled to our sparse update) | **Medium.** Cut-away roofs make a TLAS miss non-sky indoors; the miss must keep the game's probe (R3). NRC is **infeasible** without off-screen materials, and it needs a training loop (cooperative vectors) on top: too much for too little | **UPGRADE** to SHaRC (screen-seeded); **REJECT** NRC |
| **GI: reuse (ReSTIR GI)** | None | ReSTIR GI with randomised temporal reuse and permutation sampling, which the RR guide explicitly recommends (`external/DLSS/doc/DLSS-RR Integration Guide.pdf` 3.5) | 1 spp without reuse; RR must do all the work | Medium: less noise in dim interiors, so less RR smear | Est. +0.2-0.3 ms | Medium. Must follow RR's decorrelation rules; reservoirs need the same reprojection as Q1 | **UPGRADE, after SHaRC** |
| **Reflections: sampling** | 1 ray/px, bounded GGX VNDF (Eto & Tokuyoshi 2023), mirror below a = 0.1, rough cut-off 0.6, RR only, specular hit distance to RR (`shaders/gi/rt_reflections.hlsl`; TRACK_D 10); 0.33 ms | Bounded VNDF + 1 spp + RR is current practice (DLSS RR reference integrations) [SDK] | None in sampling. ReSTIR for glossy reflections exists in research but is not standard practice | — | — | — | **KEEP** |
| **Reflections: hit shading** | Tier (a) on-screen reprojected radiance; tier (c) the game's own probe for misses, hidden and off-screen hits (`rt_reflections.hlsl`; 42% of rays tier (a) in the foundry, 6% off-screen, 21% hidden, 28% miss) | Full hit shading: material at the hit + RT direct lights + a radiance cache for indirect [shipped everywhere]. For engines without hit materials: a world-space **albedo or radiance cache** filled from on-screen G-buffer data (the "surface cache" idea of Lumen) | **No hit materials** (engine-side). 27% of traced rays fall back to the probe | Medium-high on metal and glossy surfaces near the camera edges and behind the camera | Option 1, a SHaRC radiance lookup at hidden/off-screen hits (glossy, a > ~0.2): est. +0.05 ms on top of D-GI2. Option 2, EN2 VT albedo + EN1 lights + an RT sun ray at the hit: est. +0.2-0.4 ms | Option 1: low once SHaRC exists. Option 2: **high**. Granite VT base colour is feasible for the 252 VT families (`vt_alpha()` already does the lookup). **Plain-texture families, normal maps and roughness at hits are infeasible** without per-material decoding | **UPGRADE**: Option 1 first; Option 2 for mirror-like pixels only |
| **RR placement and colour space** | RR is fed the game's SR input: after god rays, exposure, bloom **and a PQ-space 3D-LUT grade** (AUDIT_RT part 8). Render-resolution DOF also runs before it (FRAME_MAP step 14) | RR guide 3.1: before tone mapping, "as close to the start of post-processing as possible"; "**Depth of Field absolutely needs to be after DLSS-RR**", or use Preset F with the DOF guide | Non-linear grade before RR biases every 1-spp signal (E[f(x)] ≠ f(E[x])). DOF before RR lets RR mistake blur for signal in dialogue close-ups | **High** for every RR-denoised RT effect; medium in DOF scenes | +0.05-0.1 ms (our grade at 3840x1600) | **Medium.** Take i11702 (pre-grade) as RR colour and replicate `0592680b` (HSV + 32³ LUT; its UBO is read live because it varies by area). For DOF: provide `DLSSD.DepthOfFieldGuide`, `luminance(after − before)` from the DOF pass's own input and output (Preset F) | **UPGRADE** (#1) |
| **RR model and preset** | `rr_preset=0` (the snippet's default) for every quality mode (`layer/ngx.cpp:989-1009`). TRACK_A measured `nvngx_dlssd` 310.7.129; with `fg≥2`, NGX loads snippets from Streamline's folder, where `bin/sl` holds **310.9.16** | DLSS 4.5 RR = **Preset F**, "Default model RR2" in the 310.9.16 header (`nvsdk_ngx_defs_dlssd.h:44`). A second-generation transformer, "35% more compute … similar performance" ([hwbusters](https://hwbusters.com/?p=112128)). Guide tables, 5090 at 4K Performance: F 2.12 ms vs D 1.83 ms. Independent reviews: less smearing, some new artifacts ([PurePC](https://www.purepc.pl/test-nvidia-dlss-4-5-ray-reconstruction-recenzja-opinia)) | Which model runs depends on which DLL loads. "Default" also changes with OTA updates (guide 3.13.1) | Medium (less smearing on 1-spp signals; claims are mostly NVIDIA's) | +0.2-0.4 ms if Default is not already F | **Low.** Log the loaded snippet version; A/B with the R6 protocol; then pin `rr_preset=6` | **UPGRADE** (pin after A/B) |
| **RR guides** | Diffuse/specular albedo, world normals + roughness, matrices, specular hit distance (`ngx.cpp:1139-1178`) | The guide also lists ColorBeforeTransparency (E/F), the SSS guide, the DOF guide (E/F), the Responsivity mask (F only), and Transparency overlay. `DLSSD.DiffuseHitDistance` and the `RayDirectionHitDistance` keys exist in the header but are not described in the current guide | Missing: CBT (needs the pre-grade input first, AUDIT_RT), SSS (the noisy GI passes through BG3's SSS blur), DOF | Medium: particles, skin, dialogue DOF | ≈ 0 (images already exist in the frame) | Low-medium | **UPGRADE** (CBT + SSS + DOF; DiffuseHitDistance as an A/B only) |
| **Fallback denoiser (NRD)** | `rr=0`: our half-res EMA accumulator for GI; hard rays for sun, D-LS and mirror D-RF | NRD 4.18 (in `external/NRD`): REBLUR/RELAX for diffuse and specular, SIGMA for shadows [SDK, shipped] | RR runs on every RTX GPU, so "non-RR GPUs" means non-NVIDIA, which DLSS-G does not serve anyway. The real use is the **low tier**, where RR is too expensive: the guide's Performance-mode table gives 5.83 ms at 1080p on an RTX 3060 | Low-end: soft shadows and glossy reflections become possible without RR | NRD est. 0.5-1.5 ms on low-end GPUs (unmeasured) vs RR 4-6 ms | Medium (NRD needs its own guide formats and matrices, which we already compute for RR) | **UPGRADE for the low tier only**; not for the 5090 |
| **DLSS-G and Reflex** | Streamline 2.14.16, DLSS-G 2x/3x/4x, Reflex low latency, PCL markers placed at present (`layer/streamline.cpp:741-745, 1480-1485, 1805`) | Same SDK. Dynamic MFG requires D3D12 ("support is currently limited to D3D12 only", DLSS_G guide 6.3). Reflex is mandatory for DLSS-G (guide 8.0). Frame-rate limiting is supposed to go through `frameLimitUs` | No frame limiter: today the 120 Hz lock relies on DLSS-G's own pacing to refresh. Simulation markers are back-to-back at present, because the game's simulation cannot be instrumented: the latency gain is less than in a native integration (an injection limit) | Steadier 60 → 120 pacing; lower latency under the cap | ≈ 0; `queueParallelismMode = eBlockNoClientQueues` may gain a little on Vulkan (DLSS_G guide) | Low | **KEEP**, add `frameLimitUs` and A/B `queueParallelismMode` |
| **Sun shadows** | 1 ray/px, uniform disc sample, white noise, RR; folded multiplicatively into the game's mask (`shaders/gi/rt_shadow.hlsl`) | Cone-sampled 1 spp + RR (shipped in CP2077 and AW2 RT shadows) | Only the default radius (AUDIT_RT: 0.00459, not 0.0023) and off-screen casters (cascade capture, planned) | — | — | — | **KEEP** (fix the radius per AUDIT_RT; cascade capture as planned) |
| **Contact shadows** | Screen-space contact pass when no TLAS (`gi_contact.hlsl`, `core/gi.cpp:1004`) | With an RT TLAS, RT shadows subsume them. Best-in-class screen-space: Bend Studio's screen-space shadows ([Bend blog](https://www.bendstudio.com/blog/inside-bend-screen-space-shadows/)) | Only relevant to `rt=0` and the low tier, and for geometry missing from the TLAS (the forward pass) | Low | ≈ 0 | Low | **KEEP** as the `rt=0` path only |
| **Compositing and energy** | All terms go into the game's linear b10/b11 before SSS and fog, as fold or exact delta (TRACK_D 8.2) | Pre-tonemap linear compositing [universal] | None. Exact deltas against the decompiled terms are stronger than typical injection (it removes double counting by construction) | — | — | — | **KEEP** |
| **Specular occlusion (untraced pixels)** | Lagarde SO from the AO target for roughness above `rf_max_rough` and models 2-7 (`bg3_ambient.hlsli`) | Cone-cone / bent-normal SO (Jimenez et al., "Practical Real-Time Strategies for Accurate Indirect Occlusion", 2016) or ray-traced specular visibility | Lagarde SO on an AO that is now RT visibility is consistent but view-agnostic | Low-medium on rough metals in creases | +0.02 ms (a bent normal from the GI ray already traced) | Low | **UPGRADE** (small) |
| **Multi-scatter energy compensation** | The game's BRDF has none (single-scatter GGX + DFG LUT) | Kulla-Conty / Fdez-Agüera compensation in modern engines | Adding it would change BG3's material look on rough metals | Art-dependent | ≈ 0 | Changes the art | **KEEP** the game's BRDF (R3) |
| **Evaluation** | Planned: mean luminance, FLIP or PSNR, frame-to-frame difference (QUALITY R6); not run | **HDR-FLIP** on linear HDR against an accumulated reference ([NVlabs/flip](https://github.com/NVlabs/flip), `pip install flip-evaluator`). For temporal artifacts: **CGVQM** (Intel, HPG 2025), a full-reference video metric aimed at denoising, supersampling and frame-interpolation artifacts ([paper](https://arxiv.org/abs/2506.11546)); plus warped frame differences with the game's MVs | No metric has been run yet; the "stability" metric is a raw frame difference, which confuses motion with flicker | Makes every gain claim above testable | Tooling only | Low | **UPGRADE** |
| **Neural shading (cooperative vectors)** | n/a | RTXNS samples on Vulkan; neural materials, NTC, NRC | Every use needs either engine materials (neural materials, NTC) or off-screen material inputs (NRC). RR is already the neural part of the frame | — | — | High (toolchain + unratified extension) | **REJECT** for now |
| **RTX Neural Texture Compression** | n/a | NTC replaces texture sampling in the material shader by small-MLP inference | **Not applicable**: (1) we do not own BG3's assets or its material shaders; NTC needs re-encoding every texture and rewriting every G-buffer fragment shader, and Granite VT streams BC tiles that NTC would have to replace; (2) its benefit is VRAM and disk, and a 32 GB 5090 is not VRAM-bound here (BVH + XFB ≈ 600 MB); (3) "inference on load" transcodes to BC, which the game already ships | — | — | — | **N/A** |
| **Displaced micro-meshes** | n/a | DMM | Not exposed on this GPU and driver; no displacement data in BG3 assets | — | — | — | **N/A** |

**Status of the RR rows (2026-10-26).**
- *RR placement and colour space:* **done** (`rr_post=1`, TRACK_A "RR before
  post"). One correction to the "Now" column: the exposure `2cdd9f08` is only a
  re-exposure (≈1.000-1.005); the lighting is pre-exposed by `s1b13[0] =
  0.8333/2^EV`, confirmed by buffer identity. The grade is pointwise, so it is
  replayed exactly; DoF is replayed from the game's own blend weights.
- *RR model and preset:* **done**: the snippet is logged (310.7.129 with `fg=0`,
  310.9.16 with `fg≥2`); F = default on 310.9.16 (same cost, same image); pinned
  `rr_preset=6`.
- *RR guides:* CBT and the SSS guide **added** (rr_post=1 only); DOF guide not
  needed now that DoF runs after RR; `DiffuseHitDistance` A/B not done.

## 3. What is infeasible because we lack engine access (flags)

| Missing engine data | Blocks | Partial workaround |
|---|---|---|
| **Per-material data at secondary hits** (albedo, normal map, roughness, metal) | Exact off-screen reflections, standard SHaRC/NRC updates, ReSTIR PT, D-PT as a product | Granite VT base colour for 252 shader families via the existing `vt_alpha()` lookup (EN2). Per-draw constants from the G-buffer median. A world-space albedo/radiance cache filled from on-screen pixels (screen-seeded SHaRC) |
| **Emissive light list** (emissive triangles, their radiance) | RTXDI with emissives, ReGIR, emissive NEE | Screen-space only: the pre-lighting FX target into the bounce source (D-EM a) |
| **Global light list** | ReGIR or light BVH beyond the frustum | The view-culled froxel list (b23-b25) is complete for every visible shading point, which is all direct lighting needs |
| **Skinned mesh streaming in object space** | Persistent per-mesh skinned BLAS, CLAS templates without per-frame clustering | The skinning output SSBO (set0/b13) and XFB give per-frame positions; refit or CLAS from them |
| **Off-screen and cut-away geometry** | Closed interiors, reflections behind the camera | Cascade-caster capture + retention (D-EN3, planned) |
| **Engine-side simulation timing** | Full Reflex latency reduction | Markers at present (current approach) |

## 4. Ranked roadmap for the RTX 5090 target

Ranked by quality gained (or frame-time recovered) per unit of effort, with what we know now.
Items already in `AUDIT_RT.md`'s fix list are not repeated, except item 1, which this audit
confirms as the top state-of-the-art gap too.

| Rank | Upgrade | Gain | Cost (ms) | Risk | Also helps low tiers? |
|---|---|---|---|---|---|
<<<<<<< HEAD
| 1 | **DONE (2026-10-26, TRACK_A "RR before post").** **RR fed pre-grade colour, our grade at output; DOF guide (Preset F)**: RR gets the pre-DoF, pre-grade colour and the game's chain (DoF weights, exposure, bloom, grade UBO + LUT) is replayed at output resolution, bit-exact at render resolution; DoF now runs after RR, so no DoF guide; measured +0.15-0.18 ms | High: removes the non-linear bias on every 1-spp signal RR denoises; dialogue DOF stops confusing RR | +0.05-0.1 | Medium (exact grade replication; per-area parameters) | Yes (any RR tier) |
| 2 | **Static/dynamic BVH split with instanced object-space BLAS** (class A from the game's vertex/index buffers + GPU-written TLAS instances; class B from the skinning SSBO; class C/D/E stay XFB), static BLAS `FAST_TRACE\|ALLOW_COMPACTION` built once and compacted, XFB skipped for cached draws, retention (EN3) for free | Removes the 2.3-3.4 ms per-frame rebuild in motion; prerequisite for OMM, EN3 and off-screen reflections | **−2 to −3 in motion**, −0.4 to −0.8 static | Medium-high (keying meshes, GPU-culled instance streams; primary-ray gate ≥ 99%) | **Yes, most of all**: a 2.3 ms rebuild on a 5090 is far more on a low-end GPU |
=======
| 1 | **RR fed pre-grade colour, our grade at output; DOF guide (Preset F)** | High: removes the non-linear bias on every 1-spp signal RR denoises; dialogue DOF stops confusing RR | +0.05-0.1 | Medium (exact grade replication; per-area parameters) | Yes (any RR tier) |
| 2 | **DONE 2026-10-26 (TRACK_D 11): pan 1.37 → 0.45 ms median, 2.91 → 0.83 ms p99; static foundry −0.46 ms; same primary-ray match as the soup. Class A only (UBO-offset variants, skinned via b13 and wind remain per-draw).** **Static/dynamic BVH split with instanced object-space BLAS** (class A from the game's vertex/index buffers + GPU-written TLAS instances; class B from the skinning SSBO; class C/D/E stay XFB), static BLAS `FAST_TRACE\|ALLOW_COMPACTION` built once and compacted, XFB skipped for cached draws, retention (EN3) for free | Removes the 2.3-3.4 ms per-frame rebuild in motion; prerequisite for OMM, EN3 and off-screen reflections | **−2 to −3 in motion**, −0.4 to −0.8 static | Medium-high (keying meshes, GPU-culled instance streams; primary-ray gate ≥ 99%) | **Yes, most of all**: a 2.3 ms rebuild on a 5090 is far more on a low-end GPU |
>>>>>>> main
| 3 | **Screen-seeded SHaRC** (D-GI2) as the cache for GI hidden/off-screen hits **and** glossy reflection tier (b) | Medium-high: room-scale GI and colour bleeding; 27% of reflection rays stop falling back to the probe | +0.4 to +0.6 (+0.05 for reflections) | Medium (roof cut-aways: misses keep the probe) | Yes: a cache is cheaper than more rays |
| 4 | **Opacity micromaps** for alpha-tested meshes (4-state for shadows, forced 2-state for GI), baked once per mesh with the OMM SDK GPU baker | Same image; trace cost in foliage | −0.05 to −0.1 in interiors; est. −0.3 to −1.0 in foliage | Medium-high (VT residency for baking; needs rank 2) | **Yes**: traversal-bound GPUs gain the most |
| 5 | **Partly done (2026-10-26):** Preset F pinned after an A/B on 310.9.16 (same cost and image as the default there); CBT and SSS guides added with rank 1; DOF guide not needed. **Open:** Reflex `frameLimitUs`. Original item: **Pin RR Preset F after an A/B**, then add the CBT (after rank 1), SSS and DOF guides; Reflex `frameLimitUs` for the 60 → 120 lock | Medium: less smear, steadier pacing | +0.2-0.4 | Low | Partly: keep D/E on RTX 20/30 if F's FP cost hurts there |
| 6 | ReSTIR GI on top of rank 3, with RR's randomised and permuted reuse | Medium (dim interiors) | +0.2-0.3 | Medium | Partly |
| 7 | Reflection tier (b) with VT albedo (EN2) for mirror-like pixels, as an RT pipeline pass with SER (Slang or SPIR-V intrinsics) | Medium for mirrors and metal | +0.2-0.4 | High | No |
| 8 | Evaluation tooling: HDR-FLIP against a 256-frame reference, motion-compensated temporal error, CGVQM on `-Move` clips | Makes every decision measurable | 0 | Low | Yes |
| 9 | D-LS adaptive RIS cap (k ≤ 2 rays when lights/px > 4) | Bounded worst case | ≤ 0 | Low | **Yes** |
| — | Defer: Mega Geometry CLAS, partitioned TLAS, ReSTIR DI, NRC, neural shading | | | | Mega Geometry helps older RTX most (Alan Wake 2 data), so revisit it for the low tier after rank 2 |

**Expected 5090 frame, after ranks 1-5.**
- **Static foundry:** about 9.7 ms today, plus about 0.7-1.0 ms (ranks 1, 3, 5) minus about
  0.5 ms (rank 2). That is about 10 ms.
- **In motion:** it drops by about 2-3 ms against today. This is the state that currently
  threatens the 15.4 ms floor.

**For the low-end tier set specifically:**
- Ranks 2, 4 and 9.
- NRD (REBLUR half-res diffuse, SIGMA) instead of RR where RR exceeds about 3 ms.
- The RIS paths for D-LS.
- Half-resolution GI with our accumulator (the `rr=0` path, with QUALITY Q6/Q7).
- Mega Geometry as a later option.

## 5. Things we do now that are obsolete or should be retired

- **Whole-soup BLAS rebuild on a culled-set change** (`rtscene.cpp` refit-or-rebuild policy).
  **Retired 2026-10-26** by the split (`rt_bvh_split=1`, TRACK_D 11). It remains as the
  `rt_bvh_split=0` fallback.
  - This was correct for proving the capture.
  - As the steady-state design it is the single largest avoidable cost (2.3-3.4 ms per frame
    in motion).
  - Replace it with rank 2.
- **Screen-space passes when a TLAS exists.**
  - `gi_trace.hlsl` (screen-space march), `rt_ao.hlsl` (the separate RT AO) and `gi_contact.hlsl`
    are already superseded by `gi_trace_rt` and `rt_shadow`.
  - Keep them only as the `rt=0` / low-tier path. They should not stay selectable as an
    alternative on the RT path, where they only add test surface.
- **The implicit RR preset** (`rr_preset=0`).
  - The default now depends on which `nvngx_dlssd` loads (310.7.129 from the game, or
    310.9.16 from `bin/sl` with `fg≥2`) and on OTA updates.
  - Pin a preset once it is A/B-tested.
- **The inline alpha test as the only alpha mechanism.** It is correct, but it is the slowest
  path on RT hardware since OMMs. Keep it as the "unknown state" resolver once OMMs exist.
- **Nothing else is obsolete.**
  - Sun cone sampling, bounded-VNDF reflections, per-light stochastic shadows with an analytic
    unshadowed term, and white-noise `pcg3d` sampling for RR are all at the state of the art
    for this light count.
  - So are the exact-delta compositing and the DLSS-G/Reflex wiring.
