# DLSS 5 Neural Rendering: phase 1 feasibility

2026-11-05, branch `dlss5`. Design only: no game, build, runtime load or queue work.
Recommendation: **Vulkan/D3D12 transport + direct DCS NR evaluator inside our layer**.
No ReShade or separate neural add-on. Conditional feasibility: compliant same-frame synchronization and runtime loading remain blockers. No phase 2 code implemented.

## Evidence roots

File:line references are relative to these roots and the source inspected today:

- `BG`: this BG3RTX worktree.
- `DCS`: `C:/Users/quali/dlss5vrframegen` (including `external/cheeky`).
- `BR`: `C:/Users/quali/AppData/Local/Temp/claude/C--Users-quali-Documents-Code-Projects-BG3RTX/f1b834a5-06f4-41fc-8ecf-7a46e660bda3/scratchpad/refs/dlss5-bridge`.
- BR main: `d1cc508a7097534c5c0e01a868ebe3b6657b932b`, read-only reference.
- `LOG`: `D:/SteamLibrary/steamapps/common/Baldurs Gate 3/bin/ReShade.log`.

The earlier empty BR path is superseded by this populated clone. Today's BG3 log had **renodx-dlss5 only, not this bridge** (user evidence). Its D3D12 successes
cannot be attributed to BR. Code comments documenting earlier runs are historical reports, not new measurements or proof that this BG3RTX combination works.

## 1. DCS: NR API, inputs and cost

DCS renders D3D11; NR executes through **direct NGX D3D12**, not Streamline.
`DCS/external/cheeky/src/dlss_nr.cpp:292` resolves snippet Init_Ext, create,
evaluate and release; `:315` falls back to the NGX core for parameter allocation.
`create_feature` (`:375`, call `:486`) uses numeric feature **18** and a private
parameter block; `evaluate_dlss_nr` (`:1000`, call `:1358`) records on a D3D12 list.
This version-specific string-key contract is not a public NR SDK specification.

| Resource/constant | Evidence in DCS `external/cheeky/src/dlss_nr.cpp` |
|---|---|
| Colour/output | Codec dispatch `:1295`; `DLSSNR.Color` and separate `DLSSNR.Output` at `:1319`. |
| Depth/MV | Prepared guides at `:1237`; bindings at `:1321`; convention at `:1350`. |
| Motion scale | UV guide preparation `:1197`; working-width/height NR scales at `:1339`. |
| Jitter/reset | Order/jitter/history correction `:1215`; reset parameter `:1352`. No independent NR jitter parameter in this binding block. |
| Resolution | `:448` sets working input/output dimensions, ratio 1, NR upscaling off. |
| Exposure | No exposure texture/scalar bound at `:1319` through `:1363`; HDR codec handles colour. |
| Other guides | No NR albedo, normals, roughness, hit distance, CBT or SSS binding here. RR guides remain RR inputs. |

`DCS/external/cheeky/src/nr_codec_shader.hpp:195` divides linear HDR colour by
PaperWhiteScale and sRGB-encodes the proxy. `:259` decodes proxy/neural outputs, uses `UpgradeToneMap` on original HDR colour and restores scale/alpha (`:267`).
This is not just an inverse transfer round trip; tone/energy need validation.
`DCS/external/cheeky/src/d3d11_d3d12_transport.cpp:473` and `:532` allocate/open
shared textures; `:2459` signals D3D11 readiness/D3D12 wait, `:2536` evaluates NR,
`:2573` signals completion/D3D11 wait. That transport is D3D11, not Vulkan.

**5090 evidence:** `DCS/docs/USER_GUIDE.md:177` reports 10.75 ms per stereo frame
at 1764 x 2480 per eye, whole focus; 80% setting 7.9 ms, 70% 6.35 ms, 50% 4.8 ms.
`DCS/docs/PERFORMANCE.md:21` separately reports synthetic stereo stage sums
including NR, composition and optical flow, excluding game/DX11 transport costs.
`DCS/docs/VALIDATION.md:69` records 12 real signed-310.8 evaluations, two histories,
finite changed output and zero NR failures. Do not equate stage sums with game FPS.
BR README's CPU example (`BR/README.md:205`) is not a 5090 NR GPU benchmark.

## 2. Bridge transport: what is implemented

BR mirrors a game's **SR contract**, not NR itself (`BR/src/vkmirror.inc:4`).
`Synth12EvaluateShared` calls private D3D12 SR (`BR/src/synth.inc:2721`), where
renodx can intercept and append NR. Our provider must replace this with NR-only.

- `Synth12MakeTexture` (`BR/src/synth.inc:1701`) creates committed default-heap
  textures, SHARED heap, SIMULTANEOUS_ACCESS and output UAV flags, initial COMMON;
  `:1725` exports NT handles with CreateSharedHandle. Existing game memory is not exported.
- `SynthVkImport` (`:3422`, external-image chain `:3458`) creates matching optimal
  VkImages with D3D12_RESOURCE external-memory handle type and transfer usages.
  `:3490` reads image memory requirements; `:3503` intersects image and imported
  handle memory-type bits. `:3555` chains dedicated allocation to Win32 import;
  `:3573` allocates, `:3583` binds. Handle closure remains the exporter's job (`:3563`).
- **No LUID matching:** `Synth12Open` uses the default adapter (`:1519`), because
  ReShade exposes VkDevice but not VkPhysicalDevice (`:1496`). Import can fail on
  a different GPU (`:3576`). BG has the physical device: match deviceLUID to DXGI,
  query external format/handle support and choose suitable memory explicitly.
- **No imported shared semaphore:** `BR/src/synth.inc:2818` says there is no
  shared fence between Vulkan and D3D12. `:2891` describes importing a shared
  D3D12 fence as a future upgrade requiring external_semaphore_win32. The current
  private D3D12 fence is created non-shared (`:1674`). Do not report it as implemented.
- Mirror MV is copied as an image (`BR/src/vkmirror.inc:1641`); scales, jitter and
  subrects come from `VkmReadContract` (`:878`). It does not invent optical-flow MV.
- Mirror depth uses depth-aspect image -> staging buffer -> shared colour image
  (`:1649`, `:1668`, `:1679`). D32 uses R32_FLOAT; BG's D24S8 uses R32_UINT
  (`:1403`), then `Synth12MakeDepthUnpack` (`BR/src/synth.inc:1776`) converts
  `(word & 0x00ffffff) / 16777215.0` (`:1820`) to R32_FLOAT before evaluate (`:2640`).
- `BR/src/depth_convert.comp:4` is a **different, substitute-path** shader: sample
  depth with texelFetch and store normalized depth to R32F (`:12`). No MV conversion,
  linearization, inversion or resolution remap is performed by it. ReShade's depth
  backup lacks transfer-source usage (`BR/src/depth_convert_spv.h:4`), motivating
  this shader. `SynthDepthCsRun` (`BR/src/synth.inc:4147`) belongs to that path.

### The decisive ordering mechanism

**Same frame, same open Vulkan command buffer, event park. No submit split,
no Present hook and no deliberate one-frame delay in the native Vulkan mirror.**
`BR/src/vkmirror.inc:16` explains why inputs have not executed at hook time.
The detour forwards game evaluate (`:1857`), then `VkMirrorFrame` (`:1871`) records copies, `vkCmdSetEvent(ev_in)` (`:1695`), a HOST-stage `vkCmdWaitEvents`
for ev_out (`:1707`), and the output copy back (`:1724`). After return, the game records its later post work and submits normally. No vkQueueSubmit is intercepted (`:31`).
`VkmWorkerProc` polls GPU event status (`:610`), runs D3D12 evaluate (`:631`),
waits for the private D3D12 fence (`BR/src/synth.inc:2830`), then unconditionally host-signals ev_out (`BR/src/vkmirror.inc:703`). The next recording waits for worker
idle (`:1254`); this is a one-frame-in-flight bubble, not an intentional stale-frame result.
`vk_sync=2` adds a frame only to the separate substitute path (`BR/README.md:159`).

**This park is not a compliant solution to our blocker.** BR documents two validation
violations per frame (`BR/src/vkmirror.inc:42`, `BR/README.md:313`):
`VUID-vkSetEvent-event-09543` and `VUID-vkCmdWaitEvents-srcStageMask-01158`.
It reports working NVIDIA drivers, while explicitly acknowledging enforcement risk.
Its 300 ms arrival/900 ms evaluate budgets and unconditional release reduce hangs but do not establish Vulkan legality or safe output after a failed evaluate.
The already-recorded copy can run even on timeout (`BR/src/synth.inc:2830`).
Also replace BR's assumed GENERAL game layouts (`BR/src/vkmirror.inc:1599`) with BG's tracked layouts and actual usage flags. Do not inherit these assumptions.

## 3. Native placement and remaining scheduling blocker

`BG/layer/ngx.cpp:1429` records RR. The NR logical boundary is after successful
RR/timestamp (`:1440`), before `rrpost_temporal` (`:1457`) and `rrpost_final` (`:1460`).
`BG/layer/rrpost.cpp:1082` allocates output-sized RGBA16F `g_z`, returned as
`post.output` (`:1087`), selected for RR at `ngx.cpp:1366` and read by final at
`rrpost.cpp:1190`. Feed **this image**, not the restored game output pointer.
Require use_post, fresh inputs and successful RR; skip on graded fallback/debug views.

Use NGX depth/MV, actual layouts/extents/subrects, reset, jitter and scales already read at `ngx.cpp:1443` and `:1452`. DCS's guide preparation must convert these
render-resolution guides to output-resolution NR coordinates with tested units.
`BG/docs/TRACK_B_FG.md:391` derives current-minus-previous pixel motion, jitter
removed. Keep sign conversion consistent with NGX scales; do not blindly copy
DCS's final working-width scales onto unconverted BG motion.
RR albedo/normals (`ngx.cpp:1301`), CBT (`:1371`) and SSS (`:1382`) stay RR-only.

RR output is pre-DoF/pre-grade **linear HDR, already pre-exposed/eye-adapted**
(`BG/docs/TRACK_A_RR.md:436`, `:529`). RR exposure parameter remains 1 (`:543`).
Keep the NR codec around this domain and validate energy; do not feed PQ or apply exposure twice. Replay remains `grade(((A * NR + C) * k) + B)` (`:451`).
Tonemap/UI follow; Streamline tags at present (`BG/layer/streamline.cpp:2505`, called at `layer/layer.cpp:856`) therefore consume the NR-processed real frame.

**Blocker 1 remains, narrowed by source:** BR solves the boundary using invalid
host-event synchronization, not a legal reusable split. `BG/layer/layer.cpp:490`
submits whole game command buffers (`:519`). A semaphore wait on that whole buffer
would stall the RR producer too. Owning QueueSubmit alone cannot split commands
already recorded inside one buffer. `layer/cmd.cpp:471`/`:526` wrap begin/end but
provide no proven arbitrary continuation/replay mechanism.
Production needs a legal producer/continuation boundary preserving recorded state,
secondary buffers, barriers, query scopes, submit ordering and the game's final fence.
Only then can ready/done D3D12-fence timeline imports order the two APIs legally.
Do not submit D3D12 at CPU record time, use stale output, or move NR to Present.

## 4. Port surface and quick add-on alternative

ReShade-specific pieces to **drop**: effect-runtime/command-list discovery and
lifecycle callbacks (`BR/src/vkmirror.inc:343`, `:380`, `:525`), get_native wrappers
(`BR/src/synth.inc:3303`), proxy unwrapping (`BR/src/bridge.inc:996`), add-on
registration, NGX scanning/MinHook detours in dlss5-bridge.cpp, panels/config/hotkeys,
SR mirroring, synthetic DLAA, substitute Present path, optical flow/private D3D11,
PQ backbuffer handling and the invalid event-park worker. BG already owns the hooks.

| Source to adapt | Native role |
|---|---|
| BR `synth.inc:1701` Synth12MakeTexture; `:3422` SynthVkImport; `:3274` release | Shared images/handles, lifetime and format mapping using actual Vulkan headers. |
| BR `synth.inc:3228` SynthVkBarrier; `:3256` copy; `:1776` depth unpack | Transport helpers after replacing layout assumptions and validating usages. |
| BR `vkmirror.inc:878` VkmReadContract | Reference for scalar/subrect extraction; BG already has NGX inputs. |
| DCS `dlss_nr.cpp:270`, `:375`, `:541`, `:915`, `:1000` | Runtime init, feature create, GPU resources, codec dispatch and direct NR evaluate. |
| DCS `nr_codec_shader.hpp:24` and guide/history/lifetime helpers | HDR encode/decode, converted guides, reset and submission completion tracking. |

DCS helper candidates: `nr_guides.hpp`, `dlss_nr_contract.hpp`, `nr_parameters.hpp`,
`dlss_nr_lifetime.hpp` and `nr_runtime_module.hpp` (`dlss_nr.cpp:1` through `:21`).
Port into `layer/dlss5.cpp/.h` plus a small interop unit; no second SR evaluation,
no renodx interception, no ReShade headers. Trim DCS stereo/gaze/foveation/crops,
model-tuning UI, debug borders and DCS telemetry; retain one full-frame history,
codec/guide correctness, failure retention and real completion tracking.
Estimate **3-5 KLOC adapted provider/transport/helpers**, plus tests; not all of
BR's 2102-line mirror or 8K-line synth.inc. A compliant command-boundary mechanism
is separate, potentially larger, and cannot yet be sized reliably from these sources.
No host/core ABI change expected (`common/host_api.h`, ABI 26).

**Simply installing BR beside renodx is not a proven solution today.** It needs
ReShade's Vulkan layer/full add-on support (`BR/README.md:89`) and compatible hook
ordering. BR explicitly bypasses RR (`BR/src/vkmirror.inc:1835`; denoiser keys in
`BR/src/dlss5-bridge.cpp:2215`), because SR mirroring would overwrite denoised RR.
Our hook substitutes RR and restores parameters after evaluate; observing an outer
NGX return also lands after rrpost, not at the wanted linear-HDR boundary.
BR additionally lists an open BG3 Vulkan FG hook conflict (`BR/README.md:300`).
Today's `LOG:1773` pre-FG-unavailable message is renodx's fallback observation,
not proof that BR itself cannot ever coexist with FG. Conversely, BR's D3D12
mirror route does not rely on that Vulkan Present fallback; its own RR skip,
SR replacement, hook ordering and park still prevent claiming this stack works.

Smallest **exposure hook**, if pursuing compatibility: a versioned optional
post-RR recording callback at `ngx.cpp:1440`, passing cmd/device, post.output,
tracked layouts, depth/MV/subrects, jitter/scales/reset and colour-domain metadata.
The callback records NR transport/return before rrpost; return a selected image or
failure. BR would need an NR-only consumer bypassing SR reconstruction and RR skip.
A renamed NGX entry or notification alone is insufficient: callback recording does
not make RR execute. It still needs the compliant scheduling contract above.
Keep this host-side; do not expand Bg3HostApi just to serve an external add-on.

## 5. Runtime and source licences

`DCS/distribution-policy.json:11` marks NR UserSupplied, proprietary, not bundled.
Use an explicitly selected x64 NVIDIA-signed runtime; verify signature/version/hash
before loading, record provenance, reject unsigned/modified files. DCS's broader
patched-binary allowance (`DCS/docs/USER_GUIDE.md:172`) is not BG policy.
OTA-installed files are candidates only after verification; no NR download or
redistribution grant is established. `BG/docs/release/THIRD_PARTY.md:24` inventories
SR/RR/FG, not NR. The NVIDIA exception names those features (`LICENSE-EXCEPTION.md:23`)
and grants no rights to NVIDIA binaries (`:31`); clarify NR coverage before release.
DCS loader `external/cheeky/src/nr_runtime_module.cpp:91` spoofs caller identity and
`:151` patches an imported function in memory. Establish an acceptable load route;
do not silently port this workaround or ship any altered NVIDIA binary.

BR is MIT (`BR/LICENSE:3`, permission/retention `:5`/`:12`): reused portions may be
combined into GPL-3.0-or-later BG3RTX while retaining the complete copyright,
permission and disclaimer in source/distribution notices. Record commit/files in
`docs/release/THIRD_PARTY.md` and package the exact MIT text under release licences
when code is ported. This grants rights to BR code, not the NVIDIA runtime.
Depth GLSL/SPIR-V has additional Alan Z/NIGos MIT provenance
(`BR/src/depth_convert_spv.h:29`, `BR/README.md:356`); preserve both notices if reused.
Drop ReShade/MinHook rather than importing their dependencies/notices unnecessarily.
No licence inventory change is needed yet: this phase reuses no source code.

## 6. Revised phase 2 and qualification

1. First build an offscreen shared-image fixture from BR allocation/import helpers:
   explicit LUID matching, queried formats, actual layouts, D24 and RG16F cases.
2. Prove a **compliant same-frame producer -> D3D12 -> continuation** with shared
   D3D12 fences imported as timeline semaphores. This fixture alone does not prove
   the game command-buffer split: design/test that boundary before game integration.
   Stop promotion if it requires BR's validation-invalid event park.
3. Add DCS NR-only create/evaluate + codec, full output resolution, one history;
   validate finite changed pixels, bypass, reset/recreate, failure and teardown.
4. Integrate at ngx.cpp boundary, device/submit setup in layer.cpp/host.h and cmd.cpp
   only as needed; rrpost.cpp only for result-view selection; CMake and focused tests.
   Keep Streamline's shared Vulkan NGX lifecycle (`BG/docs/TRACK_B_FG.md:348`).
5. One Surfaces setting: **DLSS 5 Neural Rendering (experimental)**, `dlss5_nr=0|1`,
   off in all presets; metadata `dlss5_nr_runtime` for the verified path. Require RR
   before post. Cold enable needs restart; disable drains safely, initialized A/B
   re-enable resets history. Update settings.h/INI/generated defaults/schema/notices.

Offline first: fake NGX parameter contracts, signature rejection, depth precision,
MV sign/scales/jitter/subrects, HDR codec bypass/energy, completion/failure lifetimes,
submit-split state fixtures, offscreen validation and same-frame readback stamps.
Include failed create/evaluate, timeout without stale copy, resolution changes and
key-off identity. BelowNormal builds; existing shader helpers, no direct DXC.
Count fixed shader requests added; no on/off define multiplication. No queue access.

**One later live qualification gate**, only on fresh explicit user authorization:
one same-run OFF/ON/OFF session at Dialogue1, real Ultra with FG off then 2x/4x.
Require every ON window: actual NR output before post/FG, finite pre-post float
energy and QA within derived OFF-repeat noise/encoding bounds, visible stable
skin/hair/DoF/UI, correct real/generated-frame ordering, no device lost, validation
errors, stale frames, hangs or failed evaluations. NR relighting needs PT/user
review separately; reduced residual with lost energy is not a pass.
Report NR, codec/transport GPU ms, CPU waits, VRAM and rendered latency separately.
Planning only: DCS pixel scaling gives 7.55 ms at 3840 x 1600; reserve roughly
8-12 ms total initially. Not a BG3 measurement, hard limit or promised interop cost.
Same-frame adds critical-path work, not a mandatory extra frame. Scope any later
water/transition coverage to a concrete missing observable, after this gate.
