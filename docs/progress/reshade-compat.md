## ReShade compatibility: Streamline device init after vkCreateDevice (branch reshade-compat, READY_FOR_LIVE, 2026-11-05)
- Symptom: with ReShade 6 installed (implicit layer above ours) and fg=2, every start since 09:56 wrote a
  Streamline mini-dump; the game hid DLSS and our RR never ran.
- Cause: `sl_device_ready` ran `slSetVulkanInfo` at the first `vkGetDeviceQueue`. ReShade calls
  `vkGetDeviceQueue` inside its own `vkCreateDevice`, before the loader fills the device dispatch table.
  NGX (inside slSetVulkanInfo) resolves device functions through vulkan-1's `vkGetDeviceProcAddr`, got null
  for all of them (sl.log: `NGXCubinVulkan::LoadVkEntrypoints: Failed to load VK device function: vkAllocateMemory`
  ... `vkCmdWriteTimestamp`), then called a null `vkGetBufferDeviceAddress` (0xc0000005 at 0, eErrorExceptionHandler).
  Without ReShade (11:28 run) the same build starts RR and FG normally.
- fg=0 is not affected: Streamline is not started and the game's own NGX Init runs after vkCreateDevice returned.
- Fix: `sl_init_after_create` (default 0). With 1 (and sl_init_at=0, the shared-NGX default), slSetVulkanInfo
  runs inside the game's stubbed `NVSDK_NGX_VULKAN_Init*`, which is surely after vkCreateDevice returned and
  before the game uses NGX. First swapchain stays the fallback. Decision logic in `common/sl_init_trigger.h`,
  CTest `sl_init_trigger_test` (12 checks). New log lines: `[compat] ReShade loaded: <path>` / `not loaded`,
  `[sl] slSetVulkanInfo trigger: ...`.
- Live job ready (not run: needs the user's go): `tools/live_batches_staged/reshade_compat/jobs/rs1-reshade-fg2.json`
  (Water Queens House, about 30 s, `live_flow` clean; staged outside tools/live_batches because the CTest lint needs a frozen corpus match for its new log lines, which only a live can give: move it in after the run). It sets `keep_reshade: true`; the queue-tooling branch, which
  disables ReShade in test runs, must honour that field before this job runs there. On main the runner does
  not disable ReShade. Gates: ReShade loaded, trigger = the game's NGX Init, slSetVulkanInfo ok, RR upscaler,
  DLSS-G status 0x0 x2; forbid writeMiniDump and `Failed to load VK device function`.
- If it passes: default 1 (also good without ReShade: the same point is after create there too).
