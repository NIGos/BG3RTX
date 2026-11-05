# DLSS 5 Neural Rendering: feasibility and phase 2a

2026-11-05, branch `dlss5`. Phase 2a offscreen fixture; no game or queue work.
Recommendation: **direct NGX Vulkan core feature 18**, with the DCS Vulkan codec.
Core Init/create/evaluate now work with signed 310.8.0; no D3D12, interop or command
buffer split is needed. Validation and shutdown qualification remain blocked.
No ReShade, separate neural add-on, game integration or live qualification yet.

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

## 2. Bridge transport: historical fallback, not the chosen native path

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

### Phase 2b direct Vulkan design (not implemented)

The core-route fixture supersedes the proposed continuation-buffer split: encode,
feature-18 evaluate and decode now execute in **one original Vulkan command buffer**
(`tools/dlss5_fixture.cpp:574`). No redirection, extra Vulkan submit, external memory,
shared D3D12 fence or worker event park is needed for native NR.
Guard the RR boundary outside a render pass: pass-end clears in_pass (`cmd.cpp:240`)
but Detour_Eval currently has no assertion (`ngx.cpp:1554`). Save/restore the actual
compute state our codec disturbs; no global census of redirected vkCmd calls is needed.

Put verified NR search paths into the **existing Streamline-owned** NGX Init,
not a second Vulkan core instance. Streamline builds FeatureCommonInfo paths from
plugin/config paths (`external/streamline-sdk-v2.14.16/source/plugins/sl.common/commonEntry.cpp:1455`, `:1471`);
our preferences currently supply one path (`layer/streamline.cpp:1337`). Confirm
NR path propagation without replacing the SR/RR/FG snippets or shipping NR.
At device creation query/enable feature-18 requirements: binary_import,
image_view_handle, buffer_device_address, push_descriptor; supported storage-image
read/write-without-format bits are also needed by this runtime's internal shaders.

Port a Vulkan-only provider/codec into layer/dlss5.cpp/.h, call at ngx.cpp:1440,
use g_z or select decoded output explicitly for rrpost, reset on RR/reset/size/mode
changes, and retire all resources on actual submission completion. Touch device
setup/Streamline paths, CMake/settings/INI/generated defaults and focused tests;
keep Surfaces experimental dlss5_nr=0 in all presets, cold enable requires restart.
No common/host_api.h ABI change expected. Revised 2b estimate: roughly 1-2 KLOC
provider/integration plus shader/settings/tests; **remove** the earlier 1-3 KLOC
command-redirection project. Resolve the internal layout finding and shutdown stall
before game integration/live permission; successful API calls are not a quality pass.

## 4. Port surface and quick add-on alternative

Current reusable provider pieces are the public NGX client/core session, DCS
feature-18 parameter block, Vulkan image/codec helpers and exact encode/decode
SPIR-V in tools/dlss5_nr_vulkan_spv.h (from DCS vulkan_shaders.hpp). Keep a full-frame
history, correct guides/energy, failure handling and completion-based lifetimes.
Drop ReShade discovery/proxies/MinHook, SR mirroring, optical flow, synthetic DLAA,
Present/PQ substitution, stereo/gaze/foveation and D3D11/D3D12 transport from the
native game path. Bridge MIT image-sharing helpers remain only in the fallback
fixture; their notice stays. No external add-on callback is required for native NR.

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

If exposing an optional host callback to another consumer later, place it at the
same post-RR point and pass the real g_z image, depth/MV, layouts/subrects and
jitter/scales/reset/colour-domain metadata. A Vulkan-core consumer can append its
commands directly; a D3D12 consumer still requires legal cross-API scheduling.
A renamed NGX export alone does not make the unmodified bridge NR-only or stop
its intentional RR bypass. No external callback was implemented in phase 2a.

## 5. Runtime selection and licences (user decision, 2026-11-05)

Product discovery order is game-folder nvngx_dlssnr.dll first, then the user's
path. Log the actual path, version, SHA-256 and WinVerifyTrust status; **unsigned
or HashMismatch NR runtimes are accepted by the application**, without a signature
option. The fixture's explicit --runtime path selects one candidate at a time for
comparison; it does not silently substitute a different DLL when NGX rejects one.
The installed NGX core remains NVIDIA-signature verified. No runtime is bundled.

This application policy cannot disable NGX core's own secure-loading checks.
The current game-folder 310.8.3 is accepted/logged by the fixture, but the driver
core rejects its signature and cannot create feature 18; see results below.
Do not patch the driver, spoof caller identity or misreport signed-fallback timings
as measurements of the custom DLL. A product fallback must log that rejection
before attempting the next configured path on its existing NGX owner.

DCS/distribution-policy.json:11 marks NR UserSupplied, proprietary, not bundled.
BG/LICENSE-EXCEPTION.md:23 names SR/RR/FG and grants no rights to NVIDIA files
(:31); clarify NR coverage before release. Bridge MIT source notice remains in
docs/release/licences/DLSS5-bridge-MIT.txt and inventory. Reused DCS codec/shader
arrays are the user's code. No ReShade/MinHook or NVIDIA DLL is added to payload.
Runtime copies under ignored .scratch are byte-identical, hash-checked test inputs,
not source or redistributable assets. Never modify shared SDKs or reference trees.

## 6. Fixture gaps: measured results and remaining blockers

Vulkan core Init/capabilities/Allocate/Create/Evaluate/Release all return 0x1 for
signed 310.8.0.0. Core 32.0.16.2114 (driver 621.14); all seven Vulkan images,
feature, pipelines, descriptor sets and direction UBOs persist across frames.
RG32F normalized motion now matches DCS vulkan_nr.cpp:54; interop --copy stays RG16F.
The fixture prefers HOST_CACHED coherent staging, builds/checks data in ordinary
CPU vectors and memcpy's mapped buffers rather than repeatedly scanning mapped
memory. Six timestamps separately bracket NR, encode and decode in the same CB.
Each frame logs evaluate CPU time, submit/fence wait, preparation, NVML SM/memory
clocks/P-state and VK_EXT_memory_budget budget/usage; CSV is flushed per frame.
Timing median now averages the two central samples; p90 uses nearest rank (CPU tested).

**72 frames / 12 warmup / 60 measured, 3840 x 1600, signed 310.8.0:**

| GPU interval | Median ms | p90 ms |
|---|---:|---:|
| NR evaluate alone | 6.519856 | 7.146592 |
| Encode | 0.119776 | 0.174080 |
| Decode | 0.102144 | 0.104160 |
| Encode + NR + decode | 6.785024 | 7.379104 |

These are validation-off diagnostic timings after the competing game exited.
All 60 outputs are finite and changed. NR range 6.235232-7.539744 ms; no ~97 ms
frame recurred. Evaluate CPU median/p90 0.327800/0.342500 ms, maximum 0.406300 ms.
Own usage stays 1,629,663,232 B / budget 32,945,209,344 B: no own oversubscription
or per-frame recreation. Sampled SM 930-1950 MHz, memory 7001 MHz, P3, GPU 10-22%.
These post-submit clock snapshots are not clocks throughout the NR interval.

Two fixture causes of unstable measurements were removed: repeated mapped-memory
scans and full-frame scalar half conversions (millions of ldexp calls). Cached
vectors/staging plus a 65,536-entry lookup generated by the exact reference half
decoder shorten CPU gaps. The preceding run without that lookup still had a
65.918176 ms NR maximum despite a 6.246288 ms median. With the lookup, its repeat
has a 7.539744 ms maximum. This is evidence of improved fixture behavior, not proof
of a single cause for every historical spike; clock policy remains workload-dependent.
A separate contention-affected run measured 25.417056/29.361056 ms median/p90;
Cyberpunk PID 41568 used 93.578% 3D and GPU stayed 98% after fixture exit. It started
14:04:39, after the original 13:38 noisy run, so cannot explain that earlier run.
The original ~97 ms cause cannot be established retrospectively. Current results
exclude recurring long CPU waits in Evaluate, own VRAM exhaustion and recreation.

**Game-folder 310.8.3:** app accepts HashMismatch, version and SHA are logged
(f95feb54137ea11979f9b4ec4f00afd84b5c98a5624d3388fbf6a87714a39fcc).
Core log: nvLoadSignedLibraryW failed, "The digital signature of the object did
not verify"; DLSSNR.Available=0, FeatureInitResult=0xbad00004, Create=0xbad0000b.
Zero evaluate frames: its NR median/p90 are **unavailable**, not 0 and not signed
runtime numbers. Sixty NR samples cannot be collected through this core route
without bypassing NVIDIA's own checks, which was not done. Byte-identical source
and scratch copy have the same result. Signed input SHA remains
e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e.

**Validation remains blocked, not filtered.** Inputs/output GENERAL is valid for
storage-capable ReadWrite resources (SDK vk.h:140; DCS Vulkan does the same).
The seven fixture image handles are 0x30..0x42; VUID-vkCmdDraw-None-09600 instead
names runtime-created image 0xc5, nv.ngx.dlssnr.resource, tracked UNDEFINED while
its descriptor expects GENERAL. Both corrected RG32 guides and controlled
SHADER_READ_ONLY inputs/ReadWrite=false with proper restore reproduce it. Generic
public aliases were also tested and reverted after failing to change it. No
safe transition/ownership contract for internal 0xc5 is exposed; no unknown-image
barrier, validation filter, SDK binary patch or callback bypass was introduced.
A clean signed-runtime NR validation run has **not** been established.

**Shutdown policy gap closed operationally, SDK bug retained as evidence:**
explicit ReleaseFeature=0x1, completed fences and vkDeviceWaitIdle=VK_SUCCESS still
precede a Shutdown1 stall past 30 s. --try-shutdown reproduces once with 5 s
progress. Default finish now releases/idles, logs Shutdown1 as an explicit no-op
and retains modules/session/device/callback until immediate process exit (exit 0
for a successful sequence). This follows TRACK_B_FG.md:348's no-op principle;
unlike the product's Streamline-owned shared session, the standalone fixture has
no surviving owner, so this is process-exit retention, **not clean SDK shutdown**.
No delayed thread is followed by runtime unload/device destruction.

Evidence: .scratch/dlss5-signed-cached72.log and
.scratch/dlss5-core-vulkan-28508/timings.csv; preceding idle72/bench72 logs retain
tail/contention evidence. Latest custom rejection: .scratch/dlss5-custom-final.log
(exit 1), layout: .scratch/dlss5-validation-final.log (exit 1). Earlier experiments:
.scratch/dlss5-rg32-validation.log, dlss5-readonly-validation.log and
dlss5-generic-validation.log. Full NGX logs are in each core-session logs folder.
CPU statistics/fence/LUID tests pass. No game launches, queue access, git writes,
settings/presets, production shaders or host ABI changes.

Native 2b still needs no D3D12 transport or command splitting. It is not ready
for live testing: internal-runtime validation remains unresolved, the current
custom runtime is core-rejected. Timings are now measured without the competing
game, but validation-off timing does not qualify correctness. No job is prepared or queued; future live requires fresh authorization.
