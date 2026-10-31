# Architecture

```
bg3.exe ──► Vulkan loader ──► VK_LAYER_BG3RTX_host (bin/VkLayer_bg3rtx.dll)  ──► other layers ──► driver
                                 │  stable; restart the game to change it
                                 ├─ tracker   always on: shader hashes, pipelines, images, views,
                                 │            framebuffers, render passes, descriptor set contents
                                 ├─ cmd       pass boundaries, bound pipeline/sets, draw counts
                                 ├─ tracer    one-frame JSON pass graph on request; SPIR-V dump
                                 ├─ shaders   DXC at runtime: HLSL → SPIR-V, recompiles on save
                                 ├─ hotreload loads bin/hot/<copy of bg3rtx_core.dll>, swaps on rebuild,
                                 │            catches faults, deferred GPU-object destruction
                                 ├─ ngx       hooks _nvngx.dll's Vulkan entry points; game's DLSS SR
                                 │            evaluate → Ray Reconstruction + G-buffer guides (TRACK_A_RR.md)
                                 ├─ streamline: DLSS-G (TRACK_B_FG.md)
                                 ├─ geocapture: G-buffer geometry by transform feedback (TRACK_D_RT.md)
                                 ├─ rtscene: rt=1 enables AS + ray query; per-frame BLAS/TLAS
                                 │            over the captured geometry, handed to the core
                                 └─ assetdb: offline game data (data/assetdb, built by
                                              tools/assets): lights, shader -> material,
                                              rooms/roofs, probes; level detection (ASSET_DB.md)
                                 │
                                 ▼  Bg3HostApi / Bg3CoreApi (common/host_api.h, plain C ABI)
                           bg3rtx_core.dll   hot-reloadable: all lighting/RT/GI logic lives here
```

## Settings

- **One table.** `common/settings.h` (header-only) holds the user-facing knobs, the presets
  and the layering rules. The layer (`layer/util.cpp`), the launcher (`tools/launcher`,
  `bin/BG3RTX.exe`), the unit tests (`tests/settings_test.cpp`) and the phase-2 overlay all
  use it. `data/settings_schema.json` is the same table as JSON; a test keeps it current.
- **Layers.** `bg3rtx.ini` (developer defaults), then `bg3rtx_user.ini` (launcher), then
  `%BG3RTX_INI%` (per-run overrides from `auto-run.ps1`). `preset=<id>` in a layer expands
  first; that layer's own keys override it. `cfg_poll()` re-reads any layer whose mtime
  changed and logs the effective knobs (`[cfg] knobs:` / `[cfg] keys:`).
- **Derived.** `rt_build=auto` (presets) builds the scene only while a ray-traced effect
  uses it.
- **Release profile.** `data/release.ini`, appended by `scripts/package.ps1`, turns the
  developer keys off (`dev=0` disables the trace hotkey, `dump_shaders=0`).
- **Diagnostics build switch.** CMake `BG3RTX_DIAGNOSTICS` (default ON) compiles the
  diagnostic-only host code into the layer: terrain packet dump, pan bursts, `rt_order_trace`,
  the `cpu_timing` hook timers in `cmd.cpp`, `ls_native_direct_validate`. OFF turns their hooks
  (`layer/host.h`) into inline no-ops; same ABI, about 120 KB smaller. `package.ps1` still ships
  the ON layer from `bin/`; a lean layer: `cmake -B build-nodiag -DBG3RTX_DIAGNOSTICS=OFF
  -DBG3RTX_BIN=<dir>` then `cmake --build build-nodiag --target VkLayer_bg3rtx`.
- Player guide: `docs/USER_SETTINGS.md`. Design: `docs/SETTINGS_DESIGN.md`.

## Why a layer of our own and not a ReShade add-on

- **Frame generation.** Streamline DLSS-G on Vulkan has to add extensions and queues
  at `vkCreateDevice`, and it has to own the swapchain and present. Only something at
  the layer level can do both.
- **Ray tracing.** `VK_KHR_acceleration_structure` and `ray_query` have to be enabled
  on the game's own device. That also happens in `vkCreateDevice`.
- **Injection points.** Lighting has to go in before fog/transparency and before
  DLSS. That needs hooks on individual passes: `on_pass_end` hands the core the
  game's command buffer right after a chosen pass.
- **Hooks that never go away.** ReShade's add-on API covers part of this, but it
  can't be reloaded. Splitting a stable host from a reloadable core makes each
  iteration take seconds instead of a game restart.

## Hot reload rules

- The host copies `bin/bg3rtx_core.dll` to `bin/hot/` before loading it, so the
  linker can always overwrite the original.
- **Reload order.** The host waits until the file has been stable for 400 ms. It
  then waits for the present queue to go idle (`QueueWaitIdle`), calls
  `unload()`, then `FreeLibrary` and `LoadLibrary`. This takes about 30 ms.
- **Releasing resources.** The core gives back every GPU object through
  `defer_destroy`. The host destroys them 4 presents later, so command buffers the
  game has already recorded stay valid.
- **State across reloads.** Anything that must survive a reload goes through
  `persist(key, size)`, which hands out host-owned memory.
- **Fault handling.** A crash (SEH exception) inside the core disables the core
  until the next build. The game keeps running.
- **Shaders.** The core asks `get_shader(file, entry, profile)` for a shader and
  rebuilds its pipeline whenever `generation` changes. A shader that fails to
  compile keeps its last good SPIR-V, and the error goes to the log.

## Identifying passes

Command buffers are recorded on several threads, so a pass's position in the
frame isn't a reliable name. Passes are identified by **signature** instead: the
SPIR-V hash of the first fragment shader plus the attachment formats and size.
The game no longer receives patches, so these hashes are stable.

## Known limits of the tracer (v1)

- Command buffers recorded before the trace started and resubmitted unchanged
  show up as `traced:false`.
- Descriptor buffers (`VK_EXT_descriptor_buffer`) are not decoded. If BG3 uses
  them, they need adding.
- Push descriptors are captured only while a trace is recording.
- It doesn't read back image contents yet; that's the next host feature (see
  ROADMAP).
