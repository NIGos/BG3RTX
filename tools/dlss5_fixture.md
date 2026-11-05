# DLSS5 offscreen fixture

Build CMake targets dlss5_fixture and dlss5_fixture_logic_test with MSVC,
Vulkan SDK/NGX SDK client and BelowNormal priority (scripts/low-priority.ps1).
The fixture sets itself BelowNormal, disables implicit layers and refuses BG3/DX11
processes. It is not shipped or run automatically by CTest. CPU test:
`ctest -R ^dlss5_fixture_logic_test$ --output-on-failure`.

Recommended Vulkan core probe (check Get-Process before **each** GPU invocation):

```powershell
if (Get-Process bg3 -ErrorAction SilentlyContinue) { throw 'BG3 running' }
./bin/dlss5_fixture.exe --nr-core-vulkan --runtime 'C:/path/nvngx_dlssnr.dll'
```

Fallback and independent transport test:

```powershell
if (Get-Process bg3 -ErrorAction SilentlyContinue) { throw 'BG3 running' }
./bin/dlss5_fixture.exe --nr-core-d3d12 --runtime 'C:/path/nvngx_dlssnr.dll'
if (Get-Process bg3 -ErrorAction SilentlyContinue) { throw 'BG3 running' }
./bin/dlss5_fixture.exe --copy --frames 6
```

Default 3840 x 1600, eight frames, first two excluded from timing. Options:
--width N --height N --frames N (at least three), --ngx-core PATH (otherwise highest
installed numeric core version, verified), --no-validation (explicit timing-only
opt-out). Default requires installed Khronos validation. The legacy --nr mode
retains the direct-snippet attempt for comparison; use the public core modes now.
--verify-runtime --runtime PATH performs only CPU signature/version/hash checking.

Core modes verify x64, valid NVIDIA Corporation signature (WinVerifyTrust with
cached certificates only) and log version/SHA-256 before loading the signed core.
They make the explicitly authorized NR copy in a unique ignored
.scratch/dlss5-core-<route>-<pid>/runtime folder, verify its signature again and
require identical SHA-256. Read-only handles deny mutation throughout the session.
The staged snippet is loaded **by NGX**, never via a direct snippet Init or a
caller-name/import patch. No NVIDIA binary is added to tracked source/player files.

Public SDK Init passes persistent FeatureCommonInfo PathListInfo and verbose
LoggingInfo. Full callback log: .scratch/dlss5-core-<route>-<pid>/logs/ngx-callback.log.
Important results and NR log lines also go to stdout; redirect stdout/stderr to a
.scratch evidence log. Every NGX result is logged. Unknown candidate capability
keys are distinguished from available=false; exact DLSSNR keys exist on this core.

Vulkan core mode owns a Vulkan-only device with discovered feature-18 extensions,
BDA and queried storage-image features. No D3D12 device, external images or shared
fence is created. In one command buffer: upload synthetic HDR/depth/MV, DCS Vulkan
encode, direct core feature-18 evaluate, DCS Vulkan decode, readback. Shader arrays
in dlss5_nr_vulkan_spv.h are exact DCS nr_encode16/nr_decode16 kernels; constants,
separate direction descriptors/UBOs and barriers are adapted from its Vulkan helper.
Input guides are full-resolution normal Z, previous-minus-current UV, no jitter;
only the first frame resets history. This does not qualify BG3 guide resampling.

Vulkan timestamps separately bracket NR and codec+NR; upload/readback and CPU
checking are excluded. The timing helper reports an **upper median** for even
sample counts. D3D12 core mode uses the earlier legal A -> D3D12 -> B timeline
transport with DCS D3DCompile codec; no unrelated API clocks are subtracted.
Copy mode round-trips all four images and checks bit identity and fence values.

Results 2026-11-05: both core APIs Init/capabilities/Create/Evaluate/Release return
0x1, DLSSNR.Available=1. Vulkan 3840 x 1600 produced finite changed decoded HDR
for six frames. Timing-only run (--no-validation): four post-warmup NR samples
46.119, 97.012, 6.221, 97.294 ms, upper median 97.012 ms; unstable, not qualified.
D3D12 fallback 128 x 64 passed four frames, NR upper median 2.239 ms.

Default Vulkan validation still reports an internal nv.ngx.dlssnr.resource layout
error (VUID-vkCmdDraw-None-09600) after required feature bits were enabled.
Both API routes stall on Shutdown1 after the last telemetry log message. The
fixture prints shutdown progress every 5 seconds and terminates at 30 seconds
without unloading a live runtime. Thus the **whole run exits 2/BLOCKED** even when
pixel/timing sequence checks pass. No shutdown result code was returned; do not
label it success or attribute the stall to telemetry alone.

Build/CPU checks, exact shader identity and spirv-val pass. See TRACK_DLSS5.md
for full evidence/provenance and revised 2b: Vulkan core removes interop/splitting,
but validation, lifecycle and stable cost still gate game integration/live tests.
