## Native DLSS5 core route (branch dlss5, API feasibility PASS / qualification BLOCKED, 2026-11-05)

- Supported public SDK/core Init with persistent NR PathListInfo and verbose callback
  logging works; use signed core 32.0.16.2114, driver 621.14 (old attempt chose .2101).
- Vulkan core: Init/capabilities/Allocate/Create/Evaluate/Release all 0x1;
  DLSSNR.Available=1, NeedsUpdatedDriver=0, FeatureInitResult=1. D3D12 core same.
- Verified signed 310.8.0 source + authorized scratch copy have identical SHA-256
  e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e.
  BG3's HashMismatch 310.8.3 is never loaded. No spoof/patch or tracked DLL.
- Vulkan encode -> NR -> decode runs in one command buffer, with no D3D12 device,
  interop or command split. Six 3840 x 1600 frames finite/changed, alpha preserved.
- Validation-off NR post-warmup 46.119/97.012/6.221/97.294 ms (upper median 97.012);
  codec+NR upper median 100.649 ms. Large spread: cost UNQUALIFIED, fastest not a budget.
- Required read/write-without-format feature bits fixed; default validation retains
  internal nv.ngx.dlssnr.resource GENERAL/UNDEFINED VUID-vkCmdDraw-None-09600.
- Both APIs release feature then stall in Shutdown1 after telemetry log; no result
  within 30 s, bounded process termination exits 2. Whole-run qualification BLOCKED.
- D3D12 fallback 128 x 64: four frames finite/changed, ingress exact, NR 2.239 ms.
- Earlier copy 3840 x 1600 exact / validation 0: Vulkan 2.532 + D3D12 copy 0.207 ms;
  this interop transport is not part of the selected Vulkan core path.
- CPU CTest passes; exact DCS Vulkan codec arrays pass spirv-val; MIT notice retained.
- 2b is now direct Vulkan provider + device/search-path/guide/lifetime/settings work,
  roughly 1-2 KLOC plus shader/settings/tests. Drop command redirection/splitting.
- Evidence .scratch/dlss5-core-vulkan-3840.log, -vulkan-validation.log, -d3d12-small.log;
  full logs under each .scratch/dlss5-core-<route>-<pid>/logs/ngx-callback.log.
- No game, queue, git writes, presets, production shaders or ABI changes. NOT READY_FOR_LIVE.
