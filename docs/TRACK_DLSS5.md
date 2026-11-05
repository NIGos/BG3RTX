# DLSS 5 Neural Rendering: phase 1 feasibility

2026-11-05, branch `dlss5`. Design only; no game, build, runtime load or queue work.
Status: **conditional feasibility; source and submission blockers remain**.
Recommend **(b) Vulkan to D3D12 interop**, using a verified, user-provided NR runtime.
This is native placement in our layer, not native Vulkan execution of NR.
No phase 2 implementation or quality qualification is included here.

## Evidence and limits

Paths below are relative to these roots; line numbers refer to files read today:

- `BG`: this BG3RTX worktree.
- `DCS`: `C:/Users/quali/dlss5vrframegen` (including its existing `external/cheeky`).
- `BR`: `C:/Users/quali/Documents/ChatGPT/dlss5-bridge`.
- `LOG`: `D:/SteamLibrary/steamapps/common/Baldurs Gate 3/bin/ReShade.log`.

**BR is an empty repository on disk**, containing only `.git`, no source or wiki.
`BR/.git/HEAD:1` names `refs/heads/master`; read-only `git ls-tree ... HEAD`
fails with `Not a valid object name HEAD`. `BR/.git/config:1` contains only core configuration, with no remote. There is no bridge code to cite or inspect here.
Consequently its exact Vulkan transport, handles, synchronization and measured cost are **unverified**, not inferred from a wiki statement or a success log.
An alternate populated source path is needed to complete the three-codebase audit.
All DCS and game-directory reads were read-only; no git writes were used.

## 1. What the working DCS integration actually does

DCS is D3D11, but **NR executes through direct NGX D3D12**, not Streamline.
`DCS/external/cheeky/src/dlss_nr.cpp:292` resolves the snippet's
`NVSDK_NGX_D3D12_Init_Ext`, CreateFeature, EvaluateFeature and ReleaseFeature.
Parameter allocation/destruction can fall back to the installed NGX core (`:315`).
Create uses numeric feature **18**, a D3D12 command list and a private parameter block (`:448`, `:486`); evaluation uses that feature handle (`:1358`).
The integration's string-key contract is version-specific evidence, not a public
NR SDK specification. Our checked-in SDKs do not supply an NR integration contract.

`DCS/external/cheeky/src/d3d11_d3d12_transport.cpp:473` creates a D3D12
shared texture, exports its handle and opens it in D3D11; `:532` implements the reverse allocation route. `:760` creates the private D3D12 device and `:796`
creates the shared fence. After-upscale NR copies DCS colour into shared storage
(`:2445`), signals D3D11 readiness and queues a D3D12 wait (`:2459`).
It evaluates NR (`:2536`), signals completion and queues a D3D11 wait (`:2573`), then copies the result back. This is GPU transport, not CPU pixel readback.
It proves D3D11/D3D12 sharing, **not Vulkan/D3D12 sharing**.

| Input/output | DCS evidence and BG3RTX consequence |
|---|---|
| Colour | `dlss_nr.cpp:1295` encodes a colour proxy; `:1319` binds `DLSSNR.Color`. Do not feed raw linear HDR without this contract. |
| Depth | `:1322` binds prepared depth; `:1350` sets reversed/normal convention. |
| Motion | `:1237` dispatches prepared guides; `:1321` binds MV; `:1339` supplies working-width/height scales. |
| Jitter | `:1215` corrects guide motion for processing order, jitter convention and history. No separate NR jitter parameter is set in this evaluated block. |
| Exposure | No exposure texture or exposure scalar is bound in `:1319` through `:1363`; HDR handling lives in the codec. |
| Other guides | This NR evaluator binds no albedo, normals, roughness, hit distance or RR SSS/CBT guide. Existing RR guides are not automatically NR requirements. |
| Resolution | `:448` sets input/output to working resolution, scale 1 and upscaling off. Cropping/working scale is a wrapper operation. |
| Output | `:1320` binds a separate neural output; decode restores the scene's colour domain and alpha. |
| History | `:1203` tracks geometry/scales; `:1352` supplies reset. Maintain a history per stream, not one global history across unrelated cameras. |

HDR codec evidence: `DCS/external/cheeky/src/nr_codec_shader.hpp:195` divides linear scene colour by PaperWhiteScale and sRGB-encodes the proxy.
`:259` sRGB-decodes proxy and neural outputs, applies `UpgradeToneMap` to the
original HDR colour and scales back; alpha comes from the original (`:267`).
This transfer step can alter tone/energy and needs its own validation.
It is not simply PQ input/output, nor a pure inverse transfer round trip.

**RTX 5090 cost evidence:** `DCS/docs/USER_GUIDE.md:177` reports **10.75 ms
per stereo frame**, whole focus, **1764 x 2480 per eye**; 80% area-axis setting
7.9 ms, 70% 6.35 ms and 50% 4.8 ms. The 90% 9.3 ms value is estimated.
`DCS/docs/PERFORMANCE.md:21` separately documents synthetic offscreen stage sums:
6.13 ms at 1024/2048 and 10.89 ms at 2048/4096, scale 75%, stereo.
Those sums include NR, composition and optical flow; they are not NR-only timings or game FPS. `DCS/docs/VALIDATION.md:69` records signed 310.8.0.0, 12 real NR
evaluations over two histories with finite changed pixels and zero NR failures.
No bridge-only 5090 timing can be established from the supplied empty checkout.

## 2. Vulkan feasibility and chosen route

`BG/external/DLSS/include/nvsdk_ngx_defs.h:111` defines
**0xbad00002 = NVSDK_NGX_Result_FAIL_PlatformError**: an underlying graphics API,
OS or system-library error. FeatureNotSupported is **0xbad00001** (`:104`).
Thus the observed error neither proves Vulkan is unsupported nor identifies which extension, parameter, runtime or dependency failed.

The available code even has a direct Vulkan NR loader:
`DCS/external/cheeky/src/vulkan_nr_runtime.cpp:29` resolves Vulkan Init_Ext/Ext2,
create/evaluate/release; `:57` queries feature-18 instance/device extensions.
Exports, successful init and extension discovery do not prove feature execution.
`LOG:1681` names binary_import, image_view_handle, buffer_device_address and
push_descriptor; `LOG:1685` confirms init on the game's Vulkan device.
`LOG:1689` then fails create at 3840 x 1600. The same create failure repeats.
`LOG:1674` identifies a **custom 310.8.3.0** runtime whose signature is unknown.
Do not generalize that failure to every signed NR build or every Vulkan device.

In contrast, `LOG:1713` records signed 310.8.0 D3D12 init, `:1724` successful feature-18 creation and `:1730` successful inline evaluation, with 3840 x 1600
colour/output and 2560 x 1067 guides. `:1829` reaches evaluation count 600.
These are logged successes, not a certificate of pixels, pre-post placement or the bridge's internal transport. `LOG:1773` explicitly says the active FG
feature prevents the Vulkan Present fallback's pre-FG route.

Choose D3D12 interop because signed D3D12 execution has local positive evidence; direct Vulkan execution does not yet have it. Do not implement two backends now.
Required design, **not a claim about BR's missing implementation**:

1. Match Vulkan physical-device LUID to the DXGI adapter, never just GPU index.
2. Allocate private shareable colour/output and converted depth/MV images.
   Query actual format/handle support first. Never export an arbitrary existing
   BG3 allocation: it was not created with external-memory allocation metadata.
3. Use compatible Win32 D3D12 resource handles with Vulkan external-memory
   imports (`VK_KHR_external_memory_win32`, D3D12_RESOURCE handle type).
   Validate dedicated allocations, tiling, memory types and ownership barriers.
4. Share D3D12 fences as Vulkan external semaphores with D3D12_FENCE handles
   (`VK_KHR_external_semaphore_win32`), supported timeline values and monotonic
   producer/consumer synchronization. Validate import/export support explicitly.
5. Vulkan produces RR and guides, signals ready; D3D12 waits, runs codec/NR,
   signals done; Vulkan waits, consumes result, then runs rrpost. Use GPU waits.
   Keep resources, descriptors and allocators alive through actual completion.

**Critical submission blocker:** the insertion hook records commands; RR has
not executed when `orig_eval()` returns. `BG/layer/layer.cpp:490` submits whole game command buffers (`:519`), not the NR boundary inside one command buffer.
Submitting that whole buffer with a wait for NR would also block its RR producer.
Submitting D3D12 immediately at record time can consume stale or unfinished data.
The layer needs a proven legal split into producer and continuation submissions, preserving game command-buffer/render-pass state, ordering and final fence.
No existing arbitrary mid-buffer split was established in this audit.
This can make production phase 2 substantially larger than an evaluate hook.
Do not move NR to Present or use previous-frame NR to hide this blocker.

**Planning estimate, not measured BG3 cost:** 6.144 MP mono versus 8.749 MP
stereo in the DCS whole-focus measurement gives 10.75 x 6.144 / 8.749 =
**7.55 ms** under a simplistic pixel-linear model. Reserve roughly **8-12 ms**
total incremental GPU time for initial planning, including codec, transfers and queue handoffs. Fixed costs, runtime version, shapes and contention may invalidate
that range. No evidence supports a precise interop overhead today.
Same-frame execution adds its serialized work to the critical path, not a mandatory extra frame; frame pacing may add more latency. Report GPU NR, codec/transport,
CPU waits, VRAM and rendered-frame latency separately before choosing a budget.

## 3. Exact BG3RTX placement and resources

`BG/layer/ngx.cpp:1429` records RR through the hooked NGX Vulkan evaluate.
Insert the logical NR boundary after successful RR and its timestamp (`:1440`), before `rrpost_temporal` (`:1457`) and **before `rrpost_final` (`:1460`)**.
Keep NR timestamps separate from RR. Require `use_post`, successful RR and fresh valid inputs; skip NR on graded-colour fallback, debug views or incomplete chains.

`BG/layer/rrpost.cpp:1082` allocates `g_z` as output-sized RGBA16F;
`:1087` returns it as `post.output`, substituted into NGX at `ngx.cpp:1366`.
`rrpost.cpp:1190` reads that same `g_z`. The NR result must reach `g_z` after
completion, or an explicit selected-input view must be passed to rrpost_final.
The game's original DLSS output pointer is restored at `ngx.cpp:1433`:
it is not the pre-grade RR colour to send to NR.

Use NGX parameter-block depth and MV already extracted at `ngx.cpp:1443`; carry their actual layouts, extents, subrects, reset and jitter/scales (`:1452`).
Convert D24S8 depth to shareable sampled R32F and RG16F MV to the validated NR guide format. Resample render-resolution guides to output-resolution NR working
space with tested sign/units and post-upscale jitter treatment, not raw copies.
`BG/docs/TRACK_B_FG.md:391` derives current-minus-previous
pixel motion with jitter removed; the NGX scale supplies the opposite direction.
For exact source evidence of existing guide generation see `ngx.cpp:1301`:
diffuse/specular albedo, normals/roughness; CBT at `:1371`, SSS at `:1382`.
Keep these for RR; do not add speculative NR bindings.

Colour is **pre-grade, pre-DoF linear HDR, already pre-exposed/eye-adapted**.
`BG/docs/TRACK_A_RR.md:436` describes RGBA16F RR and post replay;
`:529` derives lighting pre-exposure `0.8333 / 2^EV(previous)` and subsequent
re-exposure `2^(EV_prev - EV_new)`. RR's parameter remains 1 (`:543`).
Do not de-expose it twice, use PQ swapchain colour, or treat the delayed CPU
`rrpost_pre_exposure()` probe as a same-frame exposure input.
Preserve the DCS codec's verified contract first; test its HDR energy separately.

Post replay remains `grade(((A * NR + C) * k) + B)` (`TRACK_A_RR.md:451`).
Tonemap/UI follow. Streamline tags HUD-less and UI images at present
(`BG/layer/streamline.cpp:2505`), reached through `layer/layer.cpp:856`.
NR must therefore affect the real frame before these tags/FG processing.
Keep Streamline's shared Vulkan NGX owner and its lifetime rules; avoid a second
Vulkan core init/shutdown (`BG/docs/TRACK_B_FG.md:348`).

## 4. Runtime sourcing and distribution decision

`DCS/distribution-policy.json:11` explicitly marks NR **UserSupplied**, proprietary,
not included. This is a project distribution policy, not an NVIDIA licence grant.
`DCS/README.md:61` requires the user's runtime; `docs/USER_GUIDE.md:170` describes
verification and a local saved copy, not a bundled download.
`DCS/src/DcsVr.Core/NativeBinary.cs:45` documents signature/version checks;
`:106` calls WinVerifyTrust. That project also accepts compatibility-patched
binaries in some paths (`docs/USER_GUIDE.md:172`); BG3RTX must reject those.

BG3RTX's inventory covers SR/RR/FG production binaries, not NR
(`BG/docs/release/THIRD_PARTY.md:24`). `BG/LICENSE-EXCEPTION.md:23` names
SR/RR/FG; the `nvngx_dlss*.dll` wildcard alone does not clearly add NR.
The exception expressly grants no rights to NVIDIA libraries (`:31`).
**Do not ship NR.** Locate an explicitly selected user-provided, x64, valid
NVIDIA-signed runtime; record version, full path and SHA-256 before loading.
An OTA-installed copy is only a candidate after verifying identity, compatibility and applicable access terms; OTA availability or redistribution is not established.
Do not silently download, alter, bundle or accept unsigned/modified NVIDIA DLLs.
Clarify NR coverage in the GPL additional permission and add its exact vendor terms to the inventory before distribution of the integration; current evidence
does not establish an NR redistribution grant.

Loader complication: `DCS/external/cheeky/src/nr_runtime_module.cpp:91` spoofs the caller name as `nvngx.dll`; `:151` patches the runtime's imported
GetModuleFileNameW in memory and pins it (`:167`). The file on disk remains unchanged, but this is a runtime modification, not ordinary export loading.
Do not silently copy that workaround. Establish a supported load route and terms; otherwise report a loading blocker. A valid signature alone does not settle it.

## 5. Setting and later qualification gates

One **Surfaces** setting: **DLSS 5 Neural Rendering (experimental)**,
key `dlss5_nr=0|1`, default 0 in every preset. Require RR and pre-post RR order.
Enabling requires restart for device extensions/interop initialization; disabling
may apply live after safely draining in-flight work. Once initialized, developer
same-run A/B may re-enable it with history reset; do not promise cold live enable.
Display missing-runtime/unsupported status and the actual failure reason.
A runtime path is configuration metadata (`dlss5_nr_runtime`), not another
image-quality knob. No style, intensity, area or working-scale UI in phase 2.

Later live requires a new explicitly authorized batch. Prepare it outside watched
inboxes. Commit predicted observables before it; gate every ON window:

- Actual feature-18 evaluation and finite changed output, never silent passthrough.
- Static pre-post float means and spatial/temporal QA against OFF repeats; derive
  limits from noise/encoding precision. A lower residual with energy loss fails.
  NR changes appearance: report intended relighting separately against PT and
  user review; do not claim physical correctness from neural detail alone.
- Dialogue1 for skin, hair, DoF and UI; Water Queens House for reflections and
  transparency; static and moving windows, resets and resolution/load transitions.
- Separate GPU ms for NR and transport/codec plus total cost and VRAM; no heavy
  diagnostics/builds in cost windows. Cost is reported for user choice, not hidden.
- FG off and 2x/4x: correct real-frame ordering, HUD-less/UI tags, generated-frame
  appearance and pacing; no double application or stale frame across toggles.
- Zero device lost, validation errors, deadlocks, unordered reuse or failed NR
  evaluations in measured windows; clean shutdown and safe fallback on setup failure.
- Real Ultra smoke with other qualified effects, not isolated NR alone.

## 6. Phase 2: smallest useful prototype, not yet implemented

First obtain BR source and close the loader/permission and submission-split blockers.
Start with a standalone offscreen Vulkan/D3D12 fixture, one stream, same adapter,
full-frame scale 1, signed runtime, synthetic HDR/depth/MV and same-frame return.
Prove the exact producer -> NR -> continuation contract before modifying the game
command path. Never call this READY_FOR_LIVE based only on init or create success.

Then touch `layer/dlss5.cpp/.h` (loader, private resources, history, synchronization),
`layer/ngx.cpp` (boundary/input extraction), `layer/rrpost.cpp` only if selecting a
separate result view, `layer/layer.cpp` and `layer/host.h` (device setup/submission
integration), plus CMake wiring. Inspect `tracker.cpp` for resource destruction.
Keep everything host-side: **no `common/host_api.h` ABI change expected** (ABI 26).
Add the central setting to `common/settings.h` and `bg3rtx.ini`, regenerate defaults
and schema with existing scripts, and extend licence/inventory and branch progress.
Any new codec shader uses existing compilation helpers; no on/off define variants.
Planned `shaders/variants.txt` request delta: 0 for a D3D12-only codec prototype;
explicitly count new fixed requests if Vulkan guide conversion is required.

Offline checks: fake-NGX parameter/return-code tests; missing/invalid runtime and
signature rejection; resource state/fence-value/reset lifetime fixtures; CPU tests
for MV/jitter/size conversion; codec identity/pass-through and finite HDR cases;
offscreen GPU validation of sharing, same-frame dependency and readback pixels.
Test dimensions/recreation, disabled bypass, failed-create retention and teardown.
Run settings tests and affected CTests with BelowNormal builds, no direct DXC.
Check key-off shaders through the cached identity helper if shared shaders change.
No live jobs or queue files are needed for these checks.

Size: a few host integration files plus one fixture/settings/tests **if** an existing
legal split can be reused; otherwise a substantial command-recording/submission
change, not a small NGX hook. Estimate effort only after that blocker is resolved.
