# Native DLSS 5 Neural Rendering

2026-11-05, branch `dlss5`, phase 2b: experimental host integration, default Off.
Direct NGX Vulkan feature 18 records after RR and before post/FG in the original
command buffer. No D3D12 transport, submission split, ReShade or host ABI change.
The staged live candidate is not queued or run. Runtime-internal validation from
phase 2a remains unresolved; successful API calls are not a quality pass.

## Evidence roots

- BG: this worktree; phase 2b starts at `f8942a23`.
- DCS: `C:/Users/quali/dlss5vrframegen`, read-only, user-owned source.
- BR: `C:/Users/quali/AppData/Local/Temp/claude/C--Users-quali-Documents-Code-Projects-BG3RTX/f1b834a5-06f4-41fc-8ecf-7a46e660bda3/scratchpad/refs/dlss5-bridge`.
  Read-only MIT source, main `d1cc508a7097534c5c0e01a868ebe3b6657b932b`.
- LOG: BG3 `bin/ReShade.log`, read-only phase 1 evidence. Only renodx was
  installed in that observation, not BR. Its failure was not a BR integration test.

## 1. What the reference projects do

DCS is a D3D11 application; NR runs through direct NGX D3D12, not Streamline.
`DCS/external/cheeky/src/dlss_nr.cpp:292` resolves snippet entry points;
`:315` uses the NGX core for parameter allocation. Feature 18 is created at
`:486`, evaluated at `:1358`. Direct snippet Init was unsuitable for our
unmodified signed runtime: the supported public NGX core/client route works.
DCS's caller-name spoof is not ported. No NVIDIA binary is patched or modified.

| NR contract | DCS evidence, `external/cheeky/src/dlss_nr.cpp` |
|---|---|
| Colour and separate output | Codec `:1295`, bindings `:1319` |
| Depth and motion vectors | Guide preparation `:1237`, bindings `:1321` |
| Motion units, scale, jitter/reset | `:1197`, `:1215`, `:1339`, `:1352` |
| Input/output resolution | `:448`, same working size, scaling ratio 1 |
| Exposure | No exposure binding in `:1319-1363`; handled by HDR codec |
| Extra RR guides | No NR normal/albedo/roughness/hit-distance binding there |

`DCS/external/cheeky/src/nr_codec_shader.hpp:195` divides linear HDR by
PaperWhiteScale and sRGB-encodes a proxy. At `:259`, decode uses UpgradeToneMap
with original HDR/proxy/neural output; `:267` restores scale and alpha. This is
not an identity transfer round trip: energy and appearance need live checking.
The native port keeps full-frame HDR mode, scale/strength 1, no foveation masks.

DCS's DX11/DX12 shared transport creates/opens textures at
`d3d11_d3d12_transport.cpp:473/532`, signals readiness at `:2459`, evaluates NR
at `:2536`, then signals completion at `:2573`. It is not Vulkan transport.
Its Vulkan evaluator provides the RG32F normalized-motion convention
(`DCS/external/cheeky/src/vulkan_nr.cpp:54`). Guide/jitter equations come from
`dlss_nr_contract.hpp:91/102` and guide preparation, not guessed optical flow.

DCS reports RTX 5090 cost 10.75 ms per stereo frame at 1764 x 2480 per eye,
whole focus; 80% 7.9 ms, 70% 6.35 ms, 50% 4.8 ms (`docs/USER_GUIDE.md:177`).
`docs/PERFORMANCE.md:21` separately reports synthetic stage sums, not game FPS.
`docs/VALIDATION.md:69` records 12 signed-runtime evaluations with finite changed
output. Our 3840 x 1600 fixture timing is in section 5; in-game cost is unmeasured.

## 2. Why the native provider does not port bridge transport

BR mirrors a Vulkan game's SR contract onto private D3D12 SR so a separate
renodx NR add-on can intercept it (`BR/src/vkmirror.inc:4`, `synth.inc:2721`).
It does not itself supply an NR evaluator. Its current transport has these limits:

- D3D12 shared textures/NT handles: `synth.inc:1701/1725`. Vulkan dedicated Win32
  import: `:3422/3458/3503/3555/3583`; matching format/requirements/memory bits.
- No adapter LUID match: default D3D12 adapter at `:1519`; ReShade does not expose
  the physical Vulkan device (`:1496`). Our fixture independently matches LUIDs.
- No shared semaphore: `:2818` explicitly says so; `:2891` is a future shared-fence
  proposal. The existing private fence at `:1674` is not shared with Vulkan.
- MV image copy: `vkmirror.inc:1641`. Depth D24 staging copy: `:1649/1668/1679`;
  `synth.inc:1776/1820/2640` unpacks low 24 bits to normalized R32F. The distinct
  `depth_convert.comp:4/12` substitute shader samples depth; it does not convert MV.
- `VkmReadContract` (`vkmirror.inc:878`) reads scales/jitter/subrects. Most texture
  allocation/import/math is plain API code; discovery, resources and callbacks
  around it are ReShade-specific. The native provider drops those dependencies.

The decisive BR route is same-frame, same open command buffer, **event park**:
original evaluate at `vkmirror.inc:1857`, mirror at `:1871`, GPU-ready event
`:1695`, host-stage wait `:1707`, output copy `:1724`. Worker `:610/631/703`
polls input readiness, runs D3D12, waits its private fence and host-signals output.
There is no vkQueueSubmit split or Present hook (`:31`), nor deliberate frame lag.
BR acknowledges two validation violations (`:42`, `README.md:313`). That park is
not ported. Phase 2a's legal copy fixture uses imported timeline fences, but the
working Vulkan-core NR route makes all cross-API transport unnecessary.

Installing BR beside renodx is not equivalent to native integration. It requires
ReShade Vulkan/full add-on support (`README.md:89`), intentionally bypasses RR
(`vkmirror.inc:1835`) and lists an open BG3 FG conflict (`README.md:300`).
The old "pre-FG route unavailable" log concerns renodx's fallback, not proof of a
working BR stack. Native placement reaches the actual ungraded RR output.

## 3. Phase 2b frame contract and ownership

`layer/ngx.cpp:1442` constructs the native NR frame after successful RR and its
end timestamp, before rrpost_temporal and rrpost_final. `post.output` is the
RGBA16F g_z image allocated by `layer/rrpost.cpp:1082`, selected for RR there and
read by rrpost final (`:1190`). Decode writes that same image; no stale game-output
pointer, new post route or common/host_api.h field is needed.

Colour is pre-DoF/pre-grade linear HDR, already pre-exposed/eye-adapted
(`TRACK_A_RR.md:529`). RR exposure remains 1 (`:543`). NR never receives PQ,
post-grade colour or a second exposure multiplication. The later Streamline
present tags therefore see NR's processed real frame before DLSS-G.

The boundary reads the current render subrect, guide image extents/origins,
MV low/full-resolution flag, NGX scales, jitter, depth inversion and reset.
The guide shader samples render-resolution depth/MV at output pixel centres,
converts scaled pixel motion to UV, and removes source jitter only when flagged.
NR receives output-resolution R32F depth/RG32F MV, full subrects and output-sized
MV scales. Typical contract: colour 3840 x 1600; source guides 2560 x 1067.
RR's extra albedo/normal/CBT/SSS guides remain RR-only. `common/dlss5_nr_frame.h`
and its CPU tests pin scaling, origins, bounds, reset and boundary decisions.

`layer/dlss5.cpp:239` records the provider; `:216` creates feature 18 lazily.
It owns five private images per retained slot,
fixed codec/guide pipelines, uniforms, descriptors and timestamp queries. Encode,
NGX evaluate and decode record into the same original primary command buffer.
Game compute bindings and source guide layouts are restored; decode is skipped
on failure, retaining RR colour. Unsupported/missing inputs, open render passes,
secondary/simultaneous recordings and debug views are skipped with reason changes
logged once. Creation/evaluation failure disables NR for the session with its code.

Features recreate on render/output size, source guide extents/subrects/scales,
motion convention, HDR/format or depth-convention changes. A changed contract
immediately stops NR recording. The existing per-submission lifetime helper
polls completion, and retained executable recordings must also be discarded
before ReleaseFeature. The replacement gets a fresh parameter block and private
guide images, followed by a history reset; no per-frame device-wide wait.
The boundary accepts only linear HDR RGBA16F pre-post colour and rejects other
colour formats before recording. History resets on RR
reset/camera cut, bypass resume, mode/geometry/scales change, shader reload or
unsubmitted/discontinuous history. Recording resources stay immutable until the
recording is discarded and its actual submitted work is known complete. Eight
slots bound retained recordings; pressure skips NR instead of overwriting live
resources. Failed submissions/discarded first-use recordings do not establish
an image's GENERAL layout. Resource/timestamp completion uses the existing host
lifetime machinery (`layer/dlss5.cpp:146`); no host event waits or extra submit.

Cold enable requests supported NVIDIA binary-import/image-view-handle,
push-descriptor/BDA and storage-image features. Optional setup failure retries the
ordinary device with caller fields restored and leaves NR unavailable on it.
Key Off leaves existing device/shader paths unchanged. No ABI version change.

The provider uses original NGX trampolines on the existing owner: Streamline's
shared owner with FG 2+ (`TRACK_B_FG.md:348`), game's own owner with FG Off.
Forwarded driver Init calls prepend persistent bin/sl search paths, preserve
other common info/logging and initialize exactly once. No private second owner.
Release/drain happens before device destruction; NR never calls Shutdown1.
After the drain, device teardown drops all slot/global feature references before
destroying any bound images, including failed creates retained only by a slot.
The offline teardown fixture checks shared features, failed creates, foreign
devices and repeated teardown; the previous order fails its resource-lifetime check.
The known stall requires explicit process-lifetime core retention/no-op shutdown
when native NR is armed; device recreation fails closed until process restart.
Both Shutdown exports are prepared before either is patched; failure to acquire
writable protection leaves both unmodified, avoiding one retained shutdown path.
The existing inline-hook mechanism forwards Init and stubs Shutdown in memory;
no on-disk DLL, NR snippet or caller identity is patched or spoofed.

GPU logs use `[dlss5] gpu NR: x ms/frame` for codec + guides + NR, with separate
NR evaluate, encode/guide and decode intervals. Completed query samples are
harvested without host waiting; stats reset discards earlier-window samples.
Detailed logging enables these reports; startup/state/failure logs are retained.

## 4. Settings, runtime staging and redistribution

Dedicated **DLSS 5** page (visible without experimental opt-in): **DLSS 5 Neural Rendering**, `dlss5_nr=0|1`, Off in every preset.
Requires RR and pre-post processing, cold enable/runtime changes require restart.
The native and WebView launchers expose Browse... for `dlss5_nr_runtime` beside
that option. Central settings/INI/defaults/schema agree. The experimental description remains.
Ctrl+Shift+F8 (customisable on DLSS 5) toggles the live bypass only if NR was enabled at startup.
The master RT shortcut is customisable on Advanced (default Ctrl+Shift+F9); F10 stays reserved.
Bindings parse once per settings reload; invalid or conflicting INI bindings disable both shortcuts
and log a configuration warning. No shader variants or ABI changes.
Hotkey polling serializes concurrent presents so each press toggles the setting once.
`dlss5_nr_bypass=1` is a hidden live diagnostic bypass, default 0; it resets history
on resume and permits an in-run A/B without pretending device enable is live.

Before a game start, the launcher inspects game-bin nvngx_dlssnr.dll first, then
the configured user file. A compatible NVIDIA-signed candidate wins over unsigned;
ties preserve game-first order. Staged copies are never discovery candidates.
Version, x64 PE, SHA-256 and offline WinVerifyTrust status are logged to
`bg3rtx.dlss5-runtime.log`; both UIs show discovery and unsigned-only warnings.
Unsigned/custom files are accepted without a trust option; NGX can reject them.

`tools/launcher/dlss5_runtime.h:41/84` inspects/stages the selected source.
It is locked and copied to a temporary bin/sl file only if its hash
differs, rehashed and atomically replaced. Bytes must match the chosen source.
No source DLL is changed or executed for inspection. Game-running checks prevent
replacement during play; dry-run/helper launches do not stage a runtime.
Launcher selection logic/path ordering have pure CPU tests.

The current custom 310.8.3 game DLL is HashMismatch and app-accepted, SHA
f95feb54137ea11979f9b4ec4f00afd84b5c98a5624d3388fbf6a87714a39fcc.
NGX itself rejects its signature: Available 0, InitResult 0xbad00004,
Create 0xbad0000b. Preferring a user-provided signed 310.8.0 avoids that known
loader rejection; accepting unsigned at application level cannot override it.

DCS/distribution-policy.json:11 marks NR UserSupplied/proprietary/not bundled.
BG3RTX ships **no NR runtime**, including launcher-staged copies. package.ps1
rejects that basename; package tests reject it in the real payload plan and manifest.
No NR DLL was staged or modified during phase 2b implementation/tests.
The GPL-3.0-or-later NVIDIA exception grants no NVIDIA redistribution rights
(`LICENSE-EXCEPTION.md:31`); its explicit SR/RR/FG wording needs review before NR
release. MIT bridge source notice stays verbatim in the licence inventory; DCS
codec/guide code is user-owned. Reused source rights do not cover the runtime.

## 5. Fixture measurements and known limits

Signed 310.8.0 SHA e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e;
core 32.0.16.2114 / driver 621.14. Vulkan core Init/caps/Create/Evaluate/Release 0x1.
72 frames, 12 warmup, 60 measured, RTX 5090, 3840 x 1600:

| GPU interval | Median ms | p90 ms |
|---|---:|---:|
| NR evaluate | 6.519856 | 7.146592 |
| Encode | 0.119776 | 0.174080 |
| Decode | 0.102144 | 0.104160 |
| Combined | 6.785024 | 7.379104 |

Finite changed HDR; validation-off diagnostic cost, not game/shared-NGX cost.
NR range 6.235232-7.539744 ms, Evaluate CPU median/p90 .327800/.342500 ms.
VRAM usage stable 1.63 GB / budget 32.95 GB; sampled SM 930-1950 MHz, MEM 7001 MHz,
P3. Cached staging/vectors and exact half lookup removed expensive CPU scans and
ldexp gaps. A prior idle run still reached 65.918176 ms before that lookup.
Another 25.417/29.361 ms run was Cyberpunk-contended (98% GPU after fixture exit).
That game started after the original ~97 ms run: its old cause remains unknown.
No recurring long Evaluate CPU wait, own VRAM exhaustion or recreation remains.

Validation is still blocked: VUID-vkCmdDraw-None-09600 identifies runtime-created
image 0xc5 `nv.ngx.dlssnr.resource`, tracked UNDEFINED, descriptor GENERAL.
All owned images transition; GENERAL/read-only inputs and corrected RG32 guides
do not fix it. No unknown-image barrier, callback bypass or validation suppression.
Release 0x1 and device idle 0 still precede a >30 s Shutdown1 stall; fixture default
retains the owner until process exit. Clean SDK shutdown is not claimed.
Custom 310.8.3 has zero evaluates, so its median/p90 remain unavailable.

Evidence: `.scratch/dlss5-signed-cached72.log`, session 28508 timings.csv;
`dlss5-custom-final.log` and `dlss5-validation-final.log` both exit 1.
The old legal interop --copy fixture remains development-only, not the game path.

## 6. Offline checks and staged live decision

Final BelowNormal build.ps1 -Test: 189/189 pass, CTest 265.66 s, build 11 s
(.scratch/dlss5-phase2b-last.log). Details: docs/progress/dlss5.md.
Shader manifest delta **0**; one new bare shader, three fixed entries, no variant
multiplication. All 217 existing requests are byte-identical to f8942a23 by equal
inputs; the three new entries compile and pass spirv-val through shader_identity.
CPU tests cover boundary rejection, layout submission/discard, guide math,
runtime ordering/trust preference, settings, UI persistence and package exclusion.

`tools/live_batches_staged/dlss5/jobs/dlss5-s8-ultra-native.json` is one prepared
READY_FOR_LIVE candidate outside watched inboxes and the active lint corpus.
No live authorization, launch or queue write occurred. The coordinator must first
prepare the chosen user runtime through the normal launcher path; the job does
not stage binaries. Structural/synthetic checks cover all-window acceptance and
reject a bad FG ratio or an NR skip in either ON window. S8 Water Queens House, real Ultra, FG 2, static OFF/ON/OFF/ON:
600 measured rendered frames plus 30 settling each, using the bypass key.
NR ON is warmed before measurement. Separate OFF/ON PNG shots follow cost windows.

Every ON window requires NR GPU ms and no skipped/failed NR; all measured windows
require FG displayed/rendered 2.00, no API error/device loss and capacity/scene
continuity. Final bypass returns Off; the serial runner restores settings.
User PNG review decides visible lighting/detail/water/UI behavior; PNGs do not
prove pre-post linear-HDR energy equivalence. No quality, energy or FG interplay
pass is claimed before an authorized live. See staged predicted.json for prior
cost, limits and gates. Structural checks are offline; full corpus lint is pending
because these new log lines have no historical live evidence.

## 7. Render-resolution change regression (2026-11-05, dlss5-res)

User report: Quality -> Balanced, render 2560 x 1067 -> 2227 x 928, fixed
3840 x 1600 output; process exited inside the driver after the next NR evaluate.
The old feature key covered only output size and inverted depth. The new exact
CPU-tested key covers the complete immutable guide/format contract above.
`[dlss5] NR recreated: render AxB -> CxD` logs successful replacement.

Fixture: `--nr-core-vulkan --guide-switch reuse|recreate --frames 12` exercises
three frames each at 2560 x 1067, 2227 x 928, 1920 x 800, 2560 x 1067 with
fixed 3840 x 1600 colour. By default it directly changes NGX guide extents.
Add `--converted-guides` for the layer-equivalent contract: CPU pixel-centre
sampling converts source guides to fixed output-sized normalized guides.
Both modes replace guide images at transitions; recreate mode uses the shared
decision helper, waits the fixture fence, discards the old recording, releases
the feature/parameters and creates a replacement before evaluate (four creates).
Finite/changed HDR and unchanged alpha are checked on every frame. Every
validation error is printed. Switch sequences continue through only the known
named NGX resource VUID 09600, report its count, and retain exit 1 when present;
other validation errors stop immediately. No validation pass is implied.

Signed 310.8.0 fixture results: reuse and recreate both completed all 12 frames
in direct and converted modes; every Create/Evaluate/Release returned 0x1,
all output finite/changed with alpha preserved. The 48-frame converted recreate
sequence also completed, with four feature creates. The user crash was **not
reproduced** offline, so this is a conservative lifetime fix, not proof of the
driver-exit cause. Validation has exactly one known internal VUID per feature:
1 for reuse, 4 for recreate; zero other errors (validation runs intentionally
exit 1). No game/queue run. Logs: `.scratch/dlss5-res-{reuse,recreate}-{raw,converted}.log`
and `.scratch/dlss5-res-recreate-converted48.log`. NR median 7.118 ms in the
48-frame diagnostic; not an in-game cost measurement. The reported installed
`bg3rtx.prev.log` could not be independently read (access denied).
Full `scripts/build.ps1 -Test`: 189/189 pass (CTest 266.03 s, build 21 s),
including the expanded CPU recreate-decision test. Shader manifest delta 0.
