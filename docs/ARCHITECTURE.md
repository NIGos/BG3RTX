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
                                 └─ rtscene: rt=1 enables AS + ray query; per-frame BLAS/TLAS
                                              over the captured geometry, handed to the core
                                 │
                                 ▼  Bg3HostApi / Bg3CoreApi (common/host_api.h, plain C ABI)
                           bg3rtx_core.dll   hot-reloadable: all lighting/RT/GI logic lives here
```

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
