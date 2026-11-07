## Launcher picks the RTX GPU when an integrated GPU is also present (branch launcher-default-gpu, verified offline, 2026-11-07)

- Bug: with two Vulkan adapters (AMD Radeon iGPU plus RTX 5090, seen on the user's PC after a Windows
  reinstall added the AMD driver) the launcher showed "GPU not chosen" and every option stayed disabled
  until the user picked a GPU under Help & support. The choice was not saved, so it came back on every start.
- Fix: `launcher::default_adapter` (tools/launcher/compatibility.cpp) picks the single best adapter, scored
  like `best_gpu`: discrete, then ray tracing, then NVIDIA. A tie at the top (two equal RTX cards) still asks
  the user. Used by the web bridge and the old product UI. The user can still change it in the GPU menu.
- Tests: `launcher_web_bridge_test` gains the iGPU + RTX, twin RTX, single and empty cases (pass).
  `launcher_diagnostics_fixtures` fails on main too ("stale report is partial"), not caused by this branch.
- No ABI, shader or settings change. Cost: none (one loop over the adapters at startup).
- User check: start BG3RTX.exe on a PC with an iGPU: the Home page shows the RTX name and options are enabled.
