# DLSS5 offscreen fixture

Build dlss5_fixture and dlss5_fixture_logic_test with MSVC/Vulkan/NGX SDK and
BelowNormal priority (scripts/low-priority.ps1). No implicit layers, no game or
queue integration, no automatic GPU CTest. The executable also checks BG3/DX11.

The current timing-only repro uses --no-validation because the separate validation
gate remains blocked. First close other GPU workloads, then check BG3 immediately
before every GPU invocation:

```powershell
if (Get-Process bg3 -ErrorAction SilentlyContinue) { throw 'BG3 running' }
./bin/dlss5_fixture.exe --nr-core-vulkan --runtime 'C:/path/nvngx_dlssnr.dll' --frames 72 --warmup 12 --no-validation
```

--width/--height default 3840/1600; --frames includes warmup (maximum 600).
--warmup defaults 2; use >=60 remaining measured frames. --no-validation is an
explicit diagnostic timing opt-out, not a validation pass. Omit it and use four
frames to reproduce the validation failure. --readonly-inputs
reproduces the input-layout experiment; GENERAL remains the DCS-compatible default.
--try-shutdown performs a bounded real Shutdown1 probe after feature release and
vkDeviceWaitIdle. Default skips that known-stalling call and retains the owner,
modules, callbacks and VkDevice until immediate process exit; no unsafe unload.
The standalone no-op is not a claim of clean SDK shutdown or another shared owner.

Fixture comparison selects the explicit --runtime input; product discovery will
try the game-folder DLL first, then the configured user path, logging failures and
actual selection. There is **no unsigned-runtime option**: all NR inputs log version,
SHA-256, signature status and are accepted by the app regardless of signature.
Core remains NVIDIA-signature verified. --verify-runtime --runtime PATH is CPU-only
identity logging and succeeds on the current custom HashMismatch DLL. Identity
checks retain read-only handles; core modes use byte-identical ignored .scratch
copies with matching hashes. No NVIDIA binary is modified or bundled.

Public SDK core Init gets persistent FeatureCommonInfo paths and verbose logging.
Full NGX log: .scratch/dlss5-core-vulkan-<pid>/logs/ngx-callback.log. Each session
also writes timings.csv. Every frame includes separate GPU NR/encode/decode,
evaluate-call CPU time, submit/fence wait, preparation, SM/memory MHz, P-state,
GPU utilization and device-local VK_EXT_memory_budget bytes. NVML is loaded from
System32 and matched to the Vulkan adapter PCI address, never adapter index 0.
Median averages central samples; p90 is nearest rank. No API clock subtraction.

All images/feature/codec pipelines/descriptors/UBOs persist. Core mode uses seven
private Vulkan images, including DCS-compatible RG32F normalized motion. HDR encode,
NR and decode record in one command buffer. Warmup only initializes history once.
The fixture uses CPU cached vectors plus memcpy for input and readback checking;
HOST_CACHED coherent staging avoids repeated uncached mapped-memory scans.
Uploads/readbacks and CPU checks are outside GPU shader intervals.
The earlier --copy/D3D12 modes remain available but are not the native game design.

2026-11-05 signed 310.8.0: 72 frames, 12 warmup, 60 measured, finite changed HDR.
3840 x 1600 median/p90: NR 6.519856/7.146592 ms; encode .119776/.174080 ms;
decode .102144/.104160 ms; combined 6.785024/7.379104 ms. Validation was off.
NR range 6.235232-7.539744 ms. Evaluate CPU median/p90 .327800/.342500 ms;
VRAM stable 1.63 GB / budget 32.95 GB, sampled SM 930-1950 MHz, MEM 7001 MHz, P3.
The exact reference half-decoder lookup removes millions of scalar ldexp calls
between submits. Before it, an idle run still reached 65.918176 ms; after it,
no >7.540 ms NR samples in this run. No global driver power/clock settings changed.
Earlier 25.417056/29.361056 ms numbers were contention-affected by Cyberpunk PID
41568 (GPU 98% after fixture exit). That game has exited for the final run.
No recreation, own VRAM exhaustion or recurring long Evaluate CPU wait is seen.
The original ~97 ms cause lacks retrospective clock/budget data and remains unknown.
Final timing CSV: .scratch/dlss5-core-vulkan-28508/timings.csv; log:
.scratch/dlss5-signed-cached72.log. These timings do not qualify validation.

Custom game-folder 310.8.3 is accepted by this app but NGX secure loading rejects
its signature: Available 0, FeatureInitResult 0xbad00004, Create 0xbad0000b; zero NR
frames and no median/p90. Do not silently benchmark a signed fallback under its name.

Validation is still NOT clean. All 7 owned images explicitly transition; the error
is unowned runtime image 0xc5 expected GENERAL / tracked UNDEFINED, VUID 09600. RG32,
GENERAL and controlled read-only input layouts do not remove it. No error filtered,
unknown internal image transitioned or NVIDIA binary patched. Default validation
fails the run; --no-validation timings cannot qualify this gap.

Shutdown probe: ReleaseFeature 0x1 and device-idle 0 precede a 30 s Shutdown1 stall.
Successful no-op-policy sequence exits 0 while retaining resources to process exit;
errors still exit nonzero. See docs/TRACK_DLSS5.md for evidence and remaining blocks.
