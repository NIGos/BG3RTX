# Track B: DLSS Frame Generation through Streamline

`layer/streamline.cpp` runs NVIDIA DLSS-G (including multi frame generation on
RTX 50) in BG3's Vulkan renderer. It uses Streamline 2.14.16 with manual
hooking from inside the host layer. NVIDIA Smooth Motion is not involved:
`launch-bg3.ps1 -Clean` disables `VK_LAYER_NV_present`.

## Setup

1. Download `streamline-sdk-v2.14.16.zip` from
   github.com/NVIDIA-RTX/Streamline/releases and unpack `bin/x64` and `include`
   into `external/streamline-sdk-v2.14.16`. CMake looks there, then in
   `$env:BG3RTX_SL_SDK`, then in the main checkout's `external/` when it
   builds from a worktree.
2. `scripts\build.ps1` copies the signed binaries next to the layer:
   - `bin\sl\` holds the production DLLs;
   - `bin\sl_dev\` holds the development DLLs, which log more and support hotkeys.

   The copied files are `sl.interposer`, `sl.common`, `sl.dlss_g`, `sl.reflex`,
   `sl.pcl`, `nvngx_dlssg` and `NvLowLatencyVk`. Nothing goes into the game
   folder.
3. `bg3rtx.ini`:

| key | default | meaning |
|---|---|---|
| `fg` | 0 | 0 = off (Streamline is not loaded at all), 2 / 3 / 4 = 2x / 3x / 4x. 0 to 2+ needs a restart; 2, 3 and 4 switch live |
| `sl_dev` | 1 | 1 loads `bin\sl_dev`, 0 loads `bin\sl` |
| `sl_verbose` | 0 | Streamline verbose logging, into `bg3rtx.log` and `sl_logs\` |
| `reflex` | 1 | 0 = off, 1 = low latency, 2 = low latency + boost. DLSS-G needs Reflex markers; they are always sent |
| `fg_only_interpolated` | 0 | present only generated frames (`eShowOnlyInterpolatedFrame`), for checking artifacts |
| `fg_menu_detect` | 1 | DLSS-G's full-screen menu detection (needs the UI tag) |
| `fg_ui_recompose` | 0 | user interface recomposition |
| `fg_tag_hudless` / `fg_tag_ui` / `fg_tag_backbuffer` | 1 | which optional tags are set |
| `fg_mvec_scale_x` / `_y` | -1 / -1 | multiplied by 1/render size for `mvecScale` |
| `fg_camera` | 1 | 0 = identity matrices instead of the game's camera |
| `fg_jitter` | 1 | pass the game's jitter as `jitterOffset` |
| `fg_log_camera` | 0 | log the raw camera uniforms every 600 frames |

## How it is wired

```
bg3.exe -> loader -> VK_LAYER_BG3RTX_host -- swapchain/acquire/present/DeviceWaitIdle --> sl.interposer proxies
                          |                                                                    |
                          +------------------------- everything else ----------------------+   v
                                                                                  next layer -> driver
```

### Instance (`vkCreateInstance`)

- The layer loads `sl.interposer.dll` and calls `slInit` with these settings:
  - `eUseManualHooking`, `eDisableCLStateTracking`, `eDisableDebugText`;
  - `renderAPI = eVulkan`;
  - features DLSS_G, Reflex and PCL.
- It reads `slGetFeatureRequirements` for each feature. Their instance extensions are added, and the API version goes from 1.1 to 1.3, as the interposer's own `vkCreateInstance` would do.

### Device (`vkCreateDevice`)

- **Extensions.** The device extensions Streamline reports are merged in:
  - optical flow, external memory and semaphores, maintenance4, low_latency(2);
  - timeline semaphore and push descriptor.

  `VK_KHR_buffer_device_address` is not added, because the game already enables the EXT version and both may not be enabled together (VUID-03328).
- **Features.** Timeline semaphores, synchronization2, buffer device address and NV optical flow are turned on. Where the game's `pNext` chain already has the matching struct, the flag is set there; otherwise a struct is appended.
- **Queues.** Streamline gets one extra graphics queue (family 0, index 1) and two extra compute queues (family 2, index 1). A dedicated optical-flow queue is also created: family 5 (flags 0x10c), index 0, so optical flow runs natively.

### Streamline sits below this layer

- **Device functions.** `sl.interposer` builds the Vulkan table every plugin uses with `GetProcAddress(vulkan-1, "vkGetDeviceProcAddr")`. Left alone, every present Streamline makes would re-enter the top of the chain, reach this layer and recurse. The layer patches the interposer's `GetProcAddress` import so that `vkGetDeviceProcAddr` resolves to `sl_next_gdpa`, which hands out the next layer's functions. Command buffers and queues created that way skip the loader's trampolines, so their dispatch pointer is set with the loader's `SetDeviceLoaderData`. The present that Streamline issues goes through a counting wrapper, which is where the `displayed` count comes from.
- **Instance functions.** These stay on the loader's trampolines. `sl.common` initialises NGX with null proc-addr callbacks, so NGX loads vulkan-1 itself. Streamline is therefore given the application-level `VkPhysicalDevice`, which the loader wraps above the top layer. The layer finds that handle with the loader's `vkEnumeratePhysicalDevices`.
- **When `slSetVulkanInfo` runs.** It is deferred until the game's first `vkGetDeviceQueue` after `vkCreateDevice` has returned. The loader fills the new device's trampoline table only after the layer chain returns. Called any earlier, NGX's DLSS-G snippet resolves every device function to null and throws.
- **Window lookup.** DLSS-G finds the window through the interposer's surface→HWND map, and only its `vkCreateWin32SurfaceKHR` proxy fills that map. The game creates its surface before Streamline has a device, so the layer remembers each surface's HWND. Just before the swapchain is created it calls the proxy once, and the layer answers the nested `vkCreateWin32SurfaceKHR` with the game's existing surface.
- **Re-entrancy.** `vkDeviceWaitIdle` calls that come back down while Streamline is active on that thread go straight to the next layer.

### Per frame (`vkQueuePresentKHR`)

The frame token is created right after the previous present. It is followed by `slReflexSleep` and the PCL markers sim start, sim end and render start. At present the layer does the following, in order:

1. Sets the tags and `sl::Constants`.
2. Calls `slDLSSGSetOptions`, but only when the mode changes.
3. Marks render end and present start.
4. Calls Streamline's `vkQueuePresentKHR` proxy.
5. Marks present end.

## What is tagged

The image ids change between runs, so every input is found by its pass signature (see FRAME_MAP.md). All tags use `eValidUntilPresent` and no command buffer.

| tag | source | format / size |
|---|---|---|
| depth | depth attachment of the velocity pass (first fs `788dfdf60d8ed4cd`). A depth-only view of it is created by the layer | D24S8, render res |
| motion vectors | colour attachment 0 of the same pass | RG16F, render res |
| hudless | the UI composite (fs `046c6949b0e5297b`), set 1 binding 1. Fallback: the tonemap pass `d25b8086de1734d5` | RGB10A2, 3840x1600 |
| UI colour + alpha | the same composite, set 1 binding 2 | RGBA8, 3840x1600 |
| backbuffer | extent only (Streamline owns the swapchain) | |

**Layouts.** Each tag carries the layout its image is in at present, in queue order:

- barriers and render-pass final layouts on the four images are recorded per command buffer;
- secondaries are folded into their primary;
- the recorded layouts are applied at `vkQueueSubmit`.

**Velocity pass.** The pass previously called "velocity" (`f90209c7`) is in fact a copy of the G-buffer's octahedral *normals* for flagged pixels. RT0 of the G-buffer is stereographic normals, not velocity; FRAME_MAP is corrected. The real motion vectors are written in a later pass whose first draw is a full-screen camera pass (`788dfdf6`). That pass reconstructs the world position from linear depth, projects it with the previous view-projection, and writes `(fragCoord + jitter) - (prevPixel + prevJitter)`, followed by draws for dynamic objects.

Its values are in pixels, current minus previous, with jitter removed. DLSS wants previous minus current, so:

- `mvecScale = (-1/2560, -1/1067)`;
- `motionVectorsJittered = false`;
- `depthInverted = true` (reversed Z, D24 values 0.001–0.03);
- `cameraMotionIncluded = true`.

### Camera constants

The velocity pass reads two uniform buffers. They are std140, row-major, and used as `v * M`:

- **set 0 binding 2 (the per-camera UBO):**
  - `m2` (offset 128) is the camera-relative view-projection;
  - `m6` (offset 208) is the camera position.
- **set 0 binding 5:**
  - `m2` is the previous camera-relative view-projection;
  - `m3` (offset 192) is the previous camera position;
  - `m5` and `m6` are the current and previous jitter in pixels.

Both are read on the CPU at the pass's first draw. Readback goes through the game's mapped memory: the layer tracks `vkMapMemory`, `vkBindBufferMemory(2)` and the dynamic offsets of the call that bound the set. The values are promoted at `vkQueueSubmit`. The constants are then built from them:

- `clipToPrevClip = inv(VP) · T(camPos - prevCamPos) · VP_prev`, and `prevClipToClip` is its inverse;
- `cameraViewToClip` is taken from `m1` when that is invertible;
- FOV and the camera basis are derived from that.
