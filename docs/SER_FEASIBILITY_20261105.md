# SER feasibility for Ultra far GI - 2026-11-05

Decision: NO-GO as an immediate shipping optimization; conditional GO for a bounded offline raygen experiment.
The RTX 5090 and current SDK compiler expose a usable Vulkan route. This is a renderer port, not a compute switch.
No game, queue, Git command, device pipeline execution, or production source change was performed.
Writes: this report, its progress fragment, `.scratch/ser-study/` receipts and shared temporary DXC slot locks.

## 1. What the current measurements establish

Sources: [TRACK_C_GI](TRACK_C_GI.md#far-gi-cost-split-2026-11-05) and [optimization round 6](OPTIMIZATION_20261105.md).
Ultra uses two stochastic rays, with closest-hit search reaching 6 m; hidden shading follows hit qualification.
The attribution run's 8.0 -> 2.1 ms disables sun AND far policy; it is not a measurement of sun alone.
The later 40-pair offline split separates traversal, hidden shading, roof and other policy/context:

| Conditional cost, ms | S8 | Camp | Inn |
|---|---:|---:|---:|
| Extra traversal Q-N | 0.325 | 3.787 | 2.065 |
| Extra hidden shading Fc-L | 1.562 | 2.037 | 0.926 |
| Other policy/context L-Q | 0.562 | 3.217 | 1.711 |

These conditional costs are not additive, and shader context changes matter (near/far registers 168 -> 255).
Do not infer spills from that register count. The qualified early roof reject already removes some hidden work:
S8 live GI trace OFF/ON 7.655/6.275 and 7.937/6.226 ms; camp offline neutral, inn offline improved.
SER must target the remaining production work; the old Fc-L numbers overstate that available cohort.
Software sun wavefront was slower by +0.736/+3.114/+0.737 ms (S8/camp/inn); lane permutation about +3.8 ms live S8.
Those failures reject those implementations, not hardware SER, which can regroup after discovering the hit.

## 2. Vulkan route and this machine

Read-only command: `C:/VulkanSDK/1.4.357.0/Bin/vulkaninfoSDK.exe --text`.
Receipt: `.scratch/ser-study/vulkaninfo.txt`; system vulkaninfo and `--show-promoted-structs` agree.
Device: NVIDIA GeForce RTX 5090, Vulkan 1.4.357, NVIDIA driver 621.14.
- `VK_NV_ray_tracing_invocation_reorder`: revision 1 advertised.
- `VK_EXT_ray_tracing_invocation_reorder`: revision 2 advertised.
- EXT feature `rayTracingInvocationReorder = true`.
- EXT property `rayTracingInvocationReorderReorderingHint = ...MODE_REORDER_EXT`.
- EXT `maxShaderBindingTableRecordIndex = 4294967295`.
- `rayTracingPipeline = true`; SBT handle size/alignment 32/32 bytes, base alignment 64 bytes;
  max stride 4096 bytes, max recursion depth 31. Support is not evidence of enablement on the game's device.
Vulkaninfo prints the EXT structure only, including with promoted structures requested; no separate NV property claim.
`external/` contains neither Vulkan core headers nor NVIDIA SER HLSL headers in this checkout.
CMake uses `%VULKAN_SDK%/Include`; that SDK's `vulkan/vulkan_core.h` defines NV at line 23770 and EXT at 25674,
including both feature/property structures and EXT revision 2. `spirv/unified1/spirv.hpp` has both opcode families.

Both Vulkan extensions depend on `VK_KHR_ray_tracing_pipeline` ([NV][nv], [EXT][ext]).
Both SPIR-V reorder instructions are raygen-only ([NV SPIR-V][spnv], [EXT SPIR-V][spext]).
No compute/ray-query-only SER variant exists in these extensions. A ray query can still run inside raygen.
EXT's `OpHitObjectRecordFromQueryEXT` imports a committed query into a hit object, but is limited to RT stages;
its existence does not enable compute reordering. Hint-only reordering also requires raygen.
A minimal raygen can keep manual query traversal and use hint-only SER without invoking a closest-hit shader.
Thus an RT pipeline and raygen SBT are required; a full material SBT and rewritten traversal are optional later.

## 3. Tiny compiler probe (no GPU execution)

Reproduce: `python .scratch/ser-study/probe.py`. Sources, commands, errors and validation are in `probe.json`.
The probe uses `tools/shader_identity.py` Toolchain arguments and its shared DXC slot at BelowNormal priority;
it overrides only the profile for these scratch library shaders. No production manifest or identity sweep.
Runtime `layer/shaders.cpp:82` tries bin/dxcompiler.dll then the SDK; worktree bin and main bin DXC are absent.
SDK DXC: `1.9.0.5399 (a107ba613)`; external NIS sample DXC: `1.7.2207.3 (e9137cd1d)`.

| Probe | SDK DXC | External DXC |
|---|---|---|
| Plain raygen, lib_6_6 -> SPIR-V | Compile + validate PASS | Compile + validate PASS |
| dx::HitObject / dx::MaybeReorderThread / IsHit, lib_6_9 | Unimplemented intrinsic errors | Unsupported profile |
| Explicit vk::ext_* NV hit object + reorder, lib_6_6 | Compile + validate PASS | Invalid capability 5383 |
| Explicit vk::ext_* EXT hit object + reorder, lib_6_6 | Compile + validate PASS | Invalid capability 5388 |

SDK output disassembly retains OpTypeHitObject, RecordEmpty, ReorderThreadWithHitObject and IsHit for each family;
`spirv-val --target-env vulkan1.3` returns 0. Empty objects test compiler plumbing, not traversal or performance.
This proves an HLSL SPIR-V intrinsic route, not native DXIL/NVAPI portability or a complete SER renderer.
The upstream native SPIR-V feature request remains open ([DXC #8226][dxc]); local results are decisive.

## 4. Renderer changes and material mapping

Current path: `core/gi.cpp:2671` hit_gi_dispatch binds compute, pushes sets 0/1 plus sun/emission/SHaRC,
and dispatches 8x8. `shaders/gi/gi_trace_rt.hlsl:261` calls rt_closest_hit; hidden material starts at line 448.
`shaders/rt/rt_common.hlsli:1433` manually handles candidates, alpha, abort policy and optional procedural paths.
`shaders/rt/native_hit_material.hlsli:240` resolves instance/primitive -> published material row, checks freshness,
then evaluates shared family code, textures and explicit ray-cone gradients. It is not a per-material hit pipeline.
- Layer: enable rayTracingPipeline plus EXT SER features at device creation; resolve CreateRayTracingPipelinesKHR,
  GetRayTracingShaderGroupHandlesKHR and CmdTraceRaysKHR. Hair currently enables the pipeline extension only
  for another dependency, explicitly without the feature (`layer/hair_groom.cpp:132`). A game restart is required.
- Core: add raygen pipeline creation, SBT upload/device address/alignment, retirement on hot reload and timestamps.
  Start with one raygen record; add miss/hit records only if hit-object traversal/shader invocation uses them.
- Replace SV_DispatchThreadID with DispatchRaysIndex, retain pixel-derived RNG and each pixel's ordered two-ray sum.
  Keep existing queries first; after classification use a compact family/material/work hint and shade in raygen.
  Only reorder qualified work worth its cost; keep cheap AO/visibility queries inline initially.
- Bind at RAY_TRACING_KHR. Extend descriptor visibility and push ranges to RAYGEN (and hit/any-hit if used),
  including the layer-owned AlphaDSL (`layer/alphatest.cpp:603`) and sun/emission layouts.
  Extend compute-only producer/consumer barriers to RAY_TRACING_SHADER for AS, material tables, outputs and stats.
- `common/vk_funcs.h` lacks RT pipeline functions; extending its shared table changes ABI layout:
  plan one ABI bump and rebuild both DLLs if using that route. No ABI change was made in this study.
  `layer/cmd.cpp:145` treats every non-compute binding as graphics: add a distinct RT tracking/restoration path.
- A later full hit-object trace could replace the manual query with trace/reorder/invoke and family hit groups.
  SBT geometry/instance offsets must map to existing row/family data (today instance offset is zero-initialized).
  Keep texture rows in existing tables rather than one SBT record per texture/material; a generic hit shader
  plus explicit hints also works. Preserve alpha acceptance, OMM, procedural intersection, candidate-cap aborts,
  emission-before-roof ordering and fail-closed unknown material handling; this is the expensive correctness step.

## 5. Gain hypothesis and cost

NVIDIA reports 20-50% path-tracing throughput gains and 20-30% Lumen reflection speedups ([2022 study][nvperf]);
these correspond to about 17-33% and 17-23% time reductions, not equal percentage ms savings.
A closer Blackwell Vulkan example, RTX 5080 Indiana Jones, saves 11% initially (4.08 -> 3.63 ms), rising to 24%
after live-state optimization. SER raises L2 traffic; live-state spills fell from 222 to 84 bytes/thread ([NVIDIA][indy]).
Our single shared material evaluator, one bounce, existing early roof reject and substantial traversal/policy costs
justify a lower expectation. Planning hypothesis: 0-10% net GI trace reduction, with regressions entirely possible;
10-20% is an optimistic exploratory outcome, not a forecast or gate. No RTX 5090 SER timing was measured here.
Applying the published 17-33% reduction only to historical 0.926-2.037 ms hidden shading gives 0.15-0.68 ms gross,
before migration/reorder overhead and before subtracting work already removed by early roof rejection.
Do not apply those percentages to the entire 8-13 ms trace or promise FPS gains. Traversal may benefit from later
coherence, but this evidence does not quantify it. Live-state save/restore, lost screen locality, texture/L2 traffic,
stack/payload pressure, driver compilation and extra variants can erase savings. SBT storage starts tiny;
transient live-state memory/bandwidth is the larger unknown. Preserve FP32 and energy; no precision shortcuts.

## 6. Focused increments (engineering estimates, not authorization)

1. 0.5-1 day: compile a real query/hit-object raygen fixture with SDK intrinsics; validate SPIR-V and feature gates.
2. 2-4 days: host feature/ABI/SBT/binding/barrier work plus an offline raygen copy with SER disabled;
   compare the same replay against compute, including bit outputs, counters, abort/alpha/OMM cases.
3. 1-2 days: one hint-only reorder boundary before residual hidden shading; same raygen OFF/ON ABBA,
   stats off for cost, OFF repeats for noise, float energy equality and image/counter equivalence.
4. 1-3 days: minimize live state and compare family vs material/work hints; reject if no repeatable net gain.
   Stop before full any-hit/material pipeline migration unless the smaller experiment demonstrates value.
Allow roughly 1-2 focused weeks including debugging; a full traversal/hit-shader conversion is additional work.
Only after offline success, prepare a concrete separately authorized S8/camp/inn in-run A/B and Ultra smoke.
No live authorization is requested here. Current status: FEASIBLE_OFFLINE, not READY_FOR_LIVE or qualified.

[nv]: https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_ray_tracing_invocation_reorder.html
[ext]: https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_ray_tracing_invocation_reorder.html
[spnv]: https://github.khronos.org/SPIRV-Registry/extensions/NV/SPV_NV_shader_invocation_reorder.html
[spext]: https://github.khronos.org/SPIRV-Registry/extensions/EXT/SPV_EXT_shader_invocation_reorder.html
[dxc]: https://github.com/microsoft/DirectXShaderCompiler/issues/8226
[nvperf]: https://developer.nvidia.com/blog/improve-shader-performance-and-in-game-frame-rates-with-shader-execution-reordering/
[indy]: https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/
