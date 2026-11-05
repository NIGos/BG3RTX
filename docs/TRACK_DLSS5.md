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

## 5. Runtime and source licences

`DCS/distribution-policy.json:11` marks NR UserSupplied, proprietary, not bundled.
Use an explicitly selected x64 NVIDIA-signed runtime; verify signature/version/hash
before loading, record provenance, reject unsigned/modified files. DCS's broader
patched-binary allowance (`DCS/docs/USER_GUIDE.md:172`) is not BG policy.
OTA-installed files are candidates only after verification; no NR download or
redistribution grant is established. `BG/docs/release/THIRD_PARTY.md:41` inventories
SR/RR/FG, not NR. The NVIDIA exception names those features (`LICENSE-EXCEPTION.md:23`)
and grants no rights to NVIDIA binaries (`:31`); clarify NR coverage before release.
DCS loader `external/cheeky/src/nr_runtime_module.cpp:91` spoofs caller identity and
`:151` patches an imported function in memory. Establish an acceptable load route;
the supported core route below avoids this workaround. Never ship an altered NVIDIA binary.

BR is MIT (`BR/LICENSE:3`, permission/retention `:5`/`:12`): reused portions may be
combined into GPL-3.0-or-later BG3RTX while retaining the complete copyright,
permission and disclaimer in source/distribution notices. Record commit/files in
`docs/release/THIRD_PARTY.md` and package the exact MIT text under release licences
when code is ported. This grants rights to BR code, not the NVIDIA runtime.
Depth GLSL/SPIR-V has additional Alan Z/NIGos MIT provenance
(`BR/src/depth_convert_spv.h:29`, `BR/README.md:356`); preserve both notices if reused.
Drop ReShade/MinHook rather than importing their dependencies/notices unnecessarily.
The bridge MIT notice is now retained in `docs/release/licences/DLSS5-bridge-MIT.txt`
and its fixture reuse is recorded in `docs/release/THIRD_PARTY.md`. New core tests
copy only the user-authorized, byte-identical signed NR DLL into ignored .scratch;
no NVIDIA DLL is added to source control or the player payload.

## 6. Core-route results and remaining gates

The earlier direct-snippet --nr Init_Ext PlatformError (0xbad00002) did not test
the supported core path. New modes --nr-core-vulkan and --nr-core-d3d12 use the
public SDK client (nvsdk_ngx_s.lib) to call the verified driver's NGX core.
Core 32.0.16.2114 / driver 621.14 was selected; the original first-directory search
had selected older core 32.0.16.2101. Source and scratch NR copy are NVIDIA-signed
310.8.0.0, both SHA-256 e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e.
The BG3-folder 310.8.3 DLL remains HashMismatch and is not loaded.

CoreSession keeps PathListInfo strings/pointers alive and logs verbosely to
.scratch/dlss5-core-<route>-<pid>/logs/ngx-callback.log. SDK public Init signatures
are used, not a cast of driver/snippet Init_Ext to a different ABI.
Vulkan feature-extension discovery must have a non-null ApplicationDataPath: the
first probe omitted it and faulted; corrected once, discovery returns 0x1 and all
four extensions above. This invalid first probe is not evidence against Vulkan.

| Observable | Vulkan core | D3D12 core |
|---|---|---|
| Init / GetCapabilityParameters / AllocateParameters | all 0x1 | all 0x1 |
| DLSSNR.Available / NeedsUpdatedDriver / FeatureInitResult | 1 / 0 / 0x1 | 1 / 0 / 0x1 |
| Feature 18 Create / Evaluate / Release | all 0x1 | all 0x1 |
| Synthetic output through DCS HDR codec | six 3840 x 1600 frames finite/changed, alpha preserved | four 128 x 64 frames finite/changed, ingress exact |
| Shutdown1 | no return within 30 s; process exits 2 | same |

The core log says denial value 0 and loads the staged signed snippet. It also
reports cache misses for versions/0/files before choosing PathListInfo; a cache
miss alone is **not** a final load failure. Exact capability keys work; speculative
18.Available-style keys return 0xbad00010 (unknown key), not unsupported NR.

**3840 x 1600 Vulkan NR cost, validation explicitly off:** after two warmup frames,
NR samples 46.119, 97.012, 6.221, 97.294 ms; tool's upper median **97.012 ms**,
range **6.221-97.294 ms**, codec+NR upper median 100.649 ms. These timestamps exclude
upload/readback and CPU checks. Large variation makes this **unqualified cost**;
do not replace it with the fastest sample or the earlier 8-12 ms estimate.
No D3D12 device/shared images/timeline semaphore exists on this Vulkan path.

Default validation exposed missing storage-image read/write-without-format device
bits; enabling supported bits removed those feature errors. Retest still reports
VUID-vkCmdDraw-None-09600 on runtime-owned nv.ngx.dlssnr.resource (UNDEFINED versus
GENERAL). Fixture images have explicit transitions. Do not suppress the finding
or invent a barrier for an internal image handle/ownership contract we do not own.
The no-validation run measures API/pixel feasibility, not zero validation errors.

Both core routes stall in Shutdown1 after the last telemetry-shutdown log message.
That message does not establish the cause. Cleanup is bounded, prints every 5 s,
and terminates the standalone process at 30 s without unloading active runtime
code. All measured work/fences and feature release completed first; overall exit 2
remains **BLOCKED**, even when the sequence prints success. No device lost observed.

Evidence: .scratch/dlss5-core-vulkan-3840.log (pid 4972); default-validation retest
.scratch/dlss5-core-vulkan-validation.log (pid 31836); D3D12 fallback
.scratch/dlss5-core-d3d12-small.log (pid 44620). Each has its own full callback log.
DCS Vulkan shader arrays are byte-identical to source and pass spirv-val Vulkan 1.2.
CPU LUID/fence CTest passes. No game, queue, git writes or layer/core ABI changes.

Earlier --copy remains PASS: six 3840 x 1600 frames, exact all four images,
validation 0, Vulkan A+B upper median 2.532 ms and D3D12 copy 0.207 ms. Its transport
cost is **not added** to the direct Vulkan NR path. No new shader request variants:
the fixture uses two exact embedded DCS kernels, not new production shader requests.

NOT READY_FOR_LIVE: resolve validation/shutdown and qualify stable cost, then finish
offline provider/guide/reset/lifetime gates. Only afterward request one explicit
same-run OFF/ON/OFF Dialogue1 Ultra gate with FG off and 2x/4x, float energy/QA,
UI/DoF/skin/hair and no device lost/stale frames. No jobs prepared or queued.
