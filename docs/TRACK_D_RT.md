# Track D: hardware ray tracing

**Phase 2 (2026-10-26): ray-traced sun shadows and AO run on the game's own Vulkan
device**, against acceleration structures rebuilt or refit every frame from the
captured G-buffer geometry. Off by default (`rt=0`). Results, costs and next steps
are in [section 6](#6-phase-2-results-acceleration-structures-and-ray-queries-on-the-games-device).
Sections 1-5 are phase 1 (the capture) and the original phase-2 plan.

Goal of phase 1: prove that the frame's opaque scene geometry can be captured
every frame in camera-relative world space, as the input for BLAS/TLAS builds and
ray queries. **Result: it can, exactly.** All 835 draws (3,163,516 triangles) of
the G-buffer pass are captured; re-projected, they reproduce the frame's depth
buffer on 97.8% of pixels to 0.2%. 0% is missing, and the remaining 2.2% is
alpha-tested geometry that our triangles cover where the game discarded pixels.
Capture costs +0.45 ms GPU per frame. A BLAS refit of the same geometry costs 0.55 ms
and a full rebuild 2.5–3.4 ms on the RTX 5090. Validation-clean.

Captures (Steel Watch Foundry, the auto-run save), each with a `geo/` folder:
`captures/20261026-035645`, `-040924`, `-041511` (under validation) and `-041646`
(final design).

## 1. Survey: how the G-buffer vertex shaders form positions

Source: the decompiled shaders of `captures/20261025-235042` (`shaders/*.glsl`) plus
the draw census of its `frame.json`. The G-buffer pass (5 colour targets + D24S8) has
**835 draws, 36 distinct vertex shaders and 53 VS/FS pairs**, all `vkCmdDrawIndexed`
(no indirect draws). Every one of the 36 ends in the same two lines:

```glsl
precise vec4 W4  = vec4(P_object, 1.0) * M_instance;     // or W = W4.xyz + displacement
precise vec4 C   = vec4(W, 1.0) * cam._m2;                // set 0 / binding 2, ViewProj, row-major
C.y = -C.y;  gl_Position = C;                             // (sky: C.z = 0 as well)
```

Only the source of `M_instance` and the displacement added to `W` differ:

| Class | Position path | VS (draws in the capture) |
|---|---|---|
| A. Instanced static | `mat4(attr4, attr5, attr6, (0,0,0,1))`: the instance matrix rows are vertex attributes (per-instance stream) | 2d2ac3c3 (360), a618ddae (105), 6044c8cf (82), 54df894a (23), 2aff0bdc, c32ce535, 6660248, f474891e, 5575fc19, 646a930e; 6daa7974 = sky dome (forces z = 0) — 11 variants, ~600 draws |
| B. Per-draw UBO matrix, optionally GPU-skinned | `M = set1/b0._m0`; if `_m2 > 0` (or always) the position is read from SSBO `set0/b13` at `(gl_VertexIndex + base - 1) * 20` bytes, the output of the skinning compute pass (`cs 718d8329`) | 3812bc98 (100), f111e292, b1e18895, 1ed985dd, 939d1385 (switchable); 1cf36b97, e9407368, 41a168e2, 61239daf, 17970d7c, 13a9c9f5, 3927925b (always skinned) — 12 variants |
| C. Matrix assembled in the shader | from UBO/attribute data (`_44`, `_75`, `_76`, `_118`) | 5c258c7b, 1e9145e4, 0de1b186 (24), 5b0f1fc1 — 4 variants |
| D. Instanced + vertex animation | class A matrix, then `W += wind / cloth displacement` (noise, `textureLod`, per-material UBO params) | 50fbfceb, 735d5a19, 105e47e4, 80b73bae, 4121c54e, 8e93bb6b, e5dd82b6, 27ef2b49 — 8 variants (foliage, banners, chains) |
| E. Terrain | patch offset from `set?._m0[gl_InstanceIndex].xz`, height from a heightmap `textureLod`, then `* _m0` | 64e25689 (3 draws, 45k triangles) |

So positions come from four different places: vertex attributes, a per-draw UBO, a
skinning SSBO, and textures. Displacement is added after the instance transform. But the
last step is identical everywhere, and the space just before `* _m2` is the one we want.

### The camera UBO (set 0, binding 2, 224 bytes)

Measured in the capture (`report.json` → `camera_ubo.floats`):

| Offset | Member | Content |
|---|---|---|
| 0 | `_m0` mat4 | **View** (absolute world → view; rotation plus translation) |
| 64 | `_m1` mat4 | **Projection**: reversed infinite Z, near 0.1 at `[3][2]`, DLSS jitter in `[2][0..1]` |
| 128 | `_m2` mat4 | **ViewProj of camera-relative world**: its translation row is `(0, 0, 0.1, 0)`, so the camera sits at W = 0 |
| 192 / 196 | `_m3`, `_m4` | near 0.1, far 1500 |
| 208 | `_m6` vec3 | camera position in absolute world (e.g. −320.9, 19.9, −132.5) |

`W` is therefore **camera-relative world space**: world axes (Y up), origin at the eye.
Absolute world = `W + _m6`.

**Correction to FRAME_MAP:** the G-buffer normals (RT0) are *view space*, not
world-aligned. With the view rotation of `_m0` the captured triangle normals agree
with them at |dot| median 0.998 (94% of samples > 0.9). Without it the median is 0.77.
TRACK_C_GI.md (`set1/b1`, "view space") was right.

## 2. Mechanism: transform feedback of `gl_Position`

| Option | Verdict |
|---|---|
| (a) SPIR-V patch: store the pre-projection position to an SSBO | Works, but every variant needs its `W` located (it is a different SSA value in each of the 36 shaders, and classes C and D compute it inline). It also needs `vertexPipelineStoresAndAtomics`, a per-draw base offset (a push constant or a pipeline layout change), and index buffers to rebuild triangles from per-vertex data. |
| **(b) VK_EXT_transform_feedback** | **Chosen.** The patch is three decorations and one execution mode on the `Position` output, the same for every shader. XFB outputs whole primitives (indexed lists and strips are unrolled, instancing expanded), so it delivers a triangle soup with no index buffers. It covers skinning, wind, terrain and anything else by construction. |
| (c) CPU: read vertex and index buffers, rebuild transforms | Would have to reimplement GPU skinning, wind and terrain displacement, and read instance streams per class. Fragile, and needs readbacks. |

The cost of (b): XFB captures clip space, not `W`. Recovering `W` is exact, though.
`clip = [W, 1] · VP`, so the x, y and w rows of `VP` give a 3×3 linear system for `W`
(solved in double). It does not use z, so it is immune to the sky's `z = 0` trick and
to any depth bias. The residual on z then flags such vertices (3,072 sky vertices here).

### Implementation (`layer/geocapture.cpp`, `layer/spirv_xfb.h`)

- **Device.** With `geo_capture=1` (read at `vkCreateDevice`, so restart the game), the
  layer appends `VK_EXT_transform_feedback` and a `VkPhysicalDeviceTransformFeedbackFeaturesEXT`
  to a *copy* of the game's create info. The game's structs are not touched. If the
  driver refuses, the layer creates the device unmodified.
- **Shaders.** Every vertex shader module gets `OpCapability TransformFeedback`,
  `OpExecutionMode Xfb`, and `XfbBuffer 0 / XfbStride 16 / Offset 0` on its Position
  output (a variable or a `gl_PerVertex` member). Modules with GS/tess entry points or existing XFB
  layouts are skipped. Hashes, dumps and pass signatures use the game's original
  SPIR-V. `tools/xfb_patch_test.exe` patches the whole shader dump offline: **2385 of
  2385 vertex shaders patch, and all pass `spirv-val`**. A pipeline is only captured
  if its VS module was patched, the VS is its last pre-raster stage, and its topology is triangles.
- **Capture** (the traced frame's G-buffer pass, found by its signature): every draw
  is self-contained.
  - Before the draw: `vkCmdBindTransformFeedbackBuffersEXT` at a CPU-known offset
    (running sum of `indexCount/3 × instanceCount`; an upper bound for strips, an
    8 MB slab for indirect draws), then `vkCmdBeginTransformFeedbackEXT` with no
    counters, then a `VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT` query.
  - After the draw: end the query, then `vkCmdEndTransformFeedbackEXT`.
  - The query's primitives-written count gives the actual size. Written vs. needed
    detects overflow (bound size too small).
  - The XFB buffer is 768 MB of host-cached memory.
  - Two designs failed first:
    - Resuming XFB through counter buffers garbled the result: 131k of 3.16M triangles,
      wrong ranges. The counter written by End is not visible to the next Begin without
      a barrier, which a render pass cannot hold.
    - Keeping XFB active across the whole pass worked (identical coverage), but the game
      binds pipelines between draws. That is illegal while XFB is active
      (`VUID-vkCmdBindPipeline-None-02323`, found by the validation run).
- **After the pass** (outside it, in the game's command buffer):
  - Copy the depth (D24S8, depth aspect) and RT0 to host buffers.
  - Copy the camera UBO. Its buffer has only `UNIFORM_BUFFER` usage, so `vkCmdCopyBuffer`
    cannot be used. A 14-thread compute shader (`shaders/geo_ubo_copy.hlsl`, push
    descriptors) copies it. The game's compute bindings are then restored with `restore_bindings`.
- **Analysis** (present thread, once, ~0.9 s):
  - Solve `W`. Vertices that are non-finite in clip space are counted and kept out: the
    game parks culled vertices at NaN (73k vertices).
  - Rasterise the clip-space soup on the CPU (16 threads, reversed Z, w-clipping) and
    compare it per pixel with the game's depth.
  - Compare triangle normals with the G-buffer normals.
- **Outputs** in `captures/<ts>/geo/`:
  - `scene.bin`: header, camera UBO, viewport, draw table (range, VS/FS hash, pipeline,
    index count, instances), clip and world positions. Format in `scripts/geo_view.py`.
  - `report.json`: counts, coverage, per-VS triangles and matched pixels, the camera UBO.
  - `coverage.png`: green = match, red = ours in front, blue = ours behind, magenta = missing.
  - `depth_game.png`, `depth_ours.png`, and `normals_sample.bin`.
  - `scripts\geo_view.py <capture> [--obj]` writes `topdown.png` and, on request, `scene.obj`.
    `geo_obj=1` makes the layer write the OBJ directly (~600 MB).

Settings: `geo_capture`, `geo_xfb_mb` (768), `geo_max_draws` (16384), `geo_depth_tol`
(0.002), `geo_obj`, `geo_timing` (G-buffer pass GPU time, logged every
`geo_timing_frames`), `geo_xfb_always` (XFB into device memory every frame, for the cost
measurement), `geo_patch_dump=N`.

## 3. Results

### Coverage (`captures/20261026-040924/geo/report.json`; `-035645` agrees to 0.01%)

| | |
|---|---|
| Draws captured | **835 / 835**, 36 / 36 vertex shaders |
| Triangles | **3,163,516**, exactly Σ indexCount/3 · instances over all draws; 0 dropped, no overflow, 145 MB |
| Depth pixels with geometry | 2,731,520 (100%: the sky dome writes depth too) |
| **Match** (within 0.2% of the game's depth) | **97.82%** |
| Ours in front | 2.18%: alpha-tested grates, railings, chains, cut-out planks (red in `coverage.png`) |
| Ours behind | 0.003% (73 px, on silhouettes) |
| Missing | **0%** |
| Normals vs G-buffer (view rotated) | \|dot\| median 0.998, p10 0.98, 94% > 0.9 |
| Eye position solved from VP | (0, 0, 0): camera-relative confirmed |

The 2.2% "in front" is the any-hit problem for phase 2. Those triangles need their
alpha mask, which means UVs and the material texture. XFB can capture the UV output too
(location 0 in the class A shaders) with one more `Offset` decoration.

### Cost per frame (RTX 5090, 2560x1067 internal, G-buffer pass GPU time, median of 300 frames, `geo_timing=1`)

| | G-buffer pass |
|---|---|
| Unpatched shaders (`geo_capture=0`) | 0.240 ms |
| Patched shaders, XFB idle | 0.242 ms (the patch is free when inactive) |
| XFB active every frame (`geo_xfb_always=1`, 145 MB into device memory), final per-draw design | 0.685 ms (**+0.45 ms**) |
| (same, earlier pass-wide XFB, not spec-valid) | 0.836 ms |

Not yet measured, estimated by bandwidth: clip → world conversion of 9.5M vertices
(16 B in, 12 B out) is about 0.15 ms of compute. The one-off capture frame (host-memory
XFB, readback, CPU raster) costs about 1 s and is diagnostic only.

### BLAS/TLAS over the captured frame (`bin\geo_blas_bench.exe <scene.bin> 7`, own Vulkan 1.3 device, median of 7)

| Variant | Build | Refit (update) | Size |
|---|---|---|---|
| One BLAS, `PREFER_FAST_BUILD` | 2.52 ms | – | 162 MB (+24 MB scratch) |
| One BLAS, `PREFER_FAST_TRACE \| ALLOW_UPDATE` | 3.37 ms | 0.56 ms | 182 MB |
| 835 BLAS (one per draw) in one build call, `FAST_TRACE \| ALLOW_UPDATE` | 2.74 ms | 0.55 ms | 185 MB |
| TLAS over the 835 BLAS | 0.15 ms | | 0.14 MB |

So a naive "capture everything, rebuild everything" frame costs about 0.45 + 0.15 + 2.7
+ 0.15 ≈ **3.5 ms**. Refitting instead of rebuilding brings it to about 1.3 ms. Phase 2
can get well below that, because most of the soup is static (next section). The bench runs on its own
device so that it measures the same hardware without touching the game. Enabling
`VK_KHR_acceleration_structure` on the game's device is the first step of phase 2 (see below).

### What works, what does not (yet)

- Works:
  - Every G-buffer draw and shader variant, skinned characters, wind, terrain and sky,
    with exact triangle counts.
  - The world-space solve, coverage and normal agreement.
  - `geo_view.py` (the top-down render of the soup shows the level layout out to the
    far city geometry).
  - Validation-clean (section 4).
- Limits:
  - The analysis takes ~1 s on the present thread in the capture frame.
  - Only the G-buffer pass is captured. Forward/transparent passes, decals and the
    depth-only prepass are not.
  - No UVs or material IDs yet.
  - The capture runs in traced frames only. `geo_xfb_always` is a cost probe and writes
    to a scratch buffer nobody reads.

## 4. Validation

`auto-run.ps1 -Validation -Set geo_capture=1,geo_xfb_always=1` (Khronos validation
below our layer), capture `20261026-041511`.

- The set of message IDs is identical to the pre-existing baseline
  (`captures/validation_gi1_fixed.log` from the Track C pass). Those messages are all
  the game's own: its shader modules, `StorageImage-FormatMismatch`, layout 09600 and so on.
- No message mentions transform feedback. The capture result under validation is identical.
- One error came from this work and is fixed: `VUID-vkCmdBindPipeline-None-02323`
  (pipeline bound while XFB is active), which led to the per-draw design.
- Rules kept:
  - No game resource is written. XFB goes to our buffers, and the depth/RT0 copies
    transition back to the original layout.
  - The UBO copy dispatch restores the game's compute bindings.
  - Non-finite clip positions (the game's NaN-parked vertices) are filtered before
    anything uses them.

## 5. Phase 2 plan: BLAS/TLAS and ray-query shadows/GI (as planned; section 6 has what was done)

1. **Device.** Bump the instance API version to 1.2 (the game asks for 1.1) or use
   `VK_KHR_buffer_device_address`, then add `VK_KHR_acceleration_structure`,
   `VK_KHR_deferred_host_operations` and `VK_KHR_ray_query` with their features at
   `vkCreateDevice`, using the same copied-create-info scheme. Caveat: the game
   enables `VK_EXT_buffer_device_address`. The KHR and EXT extensions must not be enabled
   together, so the core 1.2 `bufferDeviceAddress` feature is the way. This needs a
   validation run of its own.
2. **Per-frame capture into device memory.**
   - XFB into a device-local ring (double-buffered).
   - Ranges are already known at record time: each draw's offset is the CPU prefix sum
     of `indexCount/3 × instanceCount`. All BG3 G-buffer draws are direct, and the
     queries confirmed the counts exactly. So the BLAS build ranges can be recorded in
     the same command buffer with no readback. The stream queries become a debug option.
   - Then a compute pass: clip → camera-relative world (one 3×3 solve per vertex, the
     matrix comes from the UBO copy), NaN vertices → degenerate.
3. **BLAS policy, keyed by draw identity** (pipeline + index buffer/offset + index
   count + instance stream; all available in the draw hook):
   - Class A/C (static): build once in *absolute* world (`W + _m6`), so camera motion
     does not invalidate it. Reuse while the identity persists; the TLAS instance is
     identity minus the camera offset.
   - Class B (skinned) and D (wind): refit each frame (≈0.55 ms for the whole frame is the
     upper bound; these are a fraction of the 3.16M triangles).
   - Terrain: build on change only.
   - Compaction for static BLAS (182 → ~100 MB expected).
4. **TLAS** every frame (0.15 ms for 835 instances), in camera-relative space for precision.
5. **Alpha-tested geometry** (the 2.2%): capture UV as a second XFB output, keep the
   material's alpha texture index per draw (from the FS descriptor set), and mark those
   geometries non-opaque. Ray queries then run `CandidateTriangle` alpha tests in the
   shader. Foliage-heavy scenes need this most.
6. **First consumers** (core, hot-reloadable; inject where Track C does):
   - Ray-query sun shadows: one ray per pixel from G-buffer position and normal, folded into the
     screen-space sun shadow mask like the current contact shadows.
   - Ray-query AO / one-bounce GI replacing Track C's screen-space tracing. Hits are shaded
     from the screen when on-screen, otherwise from a world-space radiance cache.
   - Denoise with RR (Track A).
7. **Coverage of other passes**: shadow-cascade casters outside the view frustum. The
   G-buffer only holds what the camera sees, plus what BG3 failed to cull (the far city
   here). Off-screen occluders for sun shadows would need XFB on the cascade passes too
   (same mechanism, depth-only VS are patched already).

## 6. Phase 2 results: acceleration structures and ray queries on the game's device

Everything here is behind `rt=1` (read at instance/device creation, so restart the
game) and off by default. Captures: Steel Watch Foundry, the auto-run save, an
interior. Code: `layer/rtscene.cpp` (device, scene build), `layer/geocapture.cpp`
(per-frame capture), `shaders/rt/xfb_to_world.hlsl`, `core/gi.cpp` with
`shaders/gi/rt_shadow.hlsl`, `rt_ao.hlsl` and `rt_debug.hlsl`.

### 6.1 Device

- **Instance API 1.1 → 1.3** (`rt_instance_api`, at `vkCreateInstance`). 1.2 is
  what the extension dependencies need. 1.3 also makes our own shaders valid,
  since DXC targets `vulkan1.3` (SPIR-V 1.6); under 1.1 or 1.2 validation reports
  `VUID-VkShaderModuleCreateInfo-pCode-08737` for every one of them. Streamline
  already raises the version to 1.3 with `fg>=2`.
- **Extensions and features.** `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`
  and `VK_KHR_deferred_host_operations` are added, together with
  `accelerationStructure` and `rayQuery`. They go into a *copy* of the create info,
  after Track D's transform feedback and before Streamline's requirements. The
  game's structs are not touched.
- **The BDA conflict.** The game enables `VK_EXT_buffer_device_address` and chains
  its feature struct (sType 1000244000, with `bufferDeviceAddress = TRUE`).
  - Adding the KHR extension next to it is forbidden (VUID-03328).
  - At API ≥ 1.2 the AS/ray query extensions no longer depend on the KHR
    extension. `vkGetBufferDeviceAddress` accepts the EXT feature
    (VUID-vkGetBufferDeviceAddress-bufferDeviceAddress-03324), so the EXT
    extension stays.
  - In addition the standalone core `VkPhysicalDeviceBufferDeviceAddressFeatures`
    is chained. Only `VkPhysicalDeviceVulkan12Features::bufferDeviceAddress` is
    excluded together with the EXT extension (VUID-04748).
  - Reason: once the device is 1.2+, `nvngx_dlss.dll` (the game's own DLSS, in its
    `bin/` folder) allocates memory with `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT`.
    That needs the core feature (VUID-VkMemoryAllocateInfo-flags-03331, found by
    the first validation run). The layer's `vkAllocateMemory` hook logs who does
    this. Our own BDA buffers then carry the allocation flag too. `rt_bda_core=0`
    switches the core feature off.
  - A game without the EXT extension gets the core feature alone.
- **Fallback.** If `vkCreateDevice` fails, it is retried without RT, then without
  XFB. If any feature is unsupported, or the game already chains AS/ray query
  structs, the device is left alone and the log says why.
- **Verified with RT on:**
  - Ray Reconstruction creates and evaluates (`[ngx] RR create … Success`).
  - The GI runs.
  - DLSS-G initialises with `fg=2` (`-RealPresent`): `slSetVulkanInfo: ok`,
    `DLSS-G state: ok`, `slDLSSGSetOptions(mode on…)`, with RT shadows traced in
    the same run.

### 6.2 Per-frame scene build (host, in the G-buffer pass's command buffer)

1. **Capture.** The phase-1 XFB path runs every frame into a 384 MB device-local
   buffer (`rt_xfb_mb`). That buffer also has storage, BDA and AS-build-input
   usage.
   - Only draws whose output size is exact (direct, triangle lists) are captured.
     That is all 835 here. The soup therefore has no gaps, and its size is known
     while recording: Σ indexCount/3 × instances.
   - The camera UBO (`set0/b2`) of the first captured draw is remembered.
   - A hash of (pipeline, count, instances) per draw identifies the draw sequence.
2. **Convert** (`rt/xfb_to_world.hlsl`, one thread per triangle, in place). Solves
   the x/y/w rows of `_m2` for camera-relative W, as in phase 1, then checks clip z
   against the matrix.
   - Triangles with a vertex whose z was overridden (the sky dome writes z = 0)
     collapse to a point. A shadow ray must never hit the sky.
   - Triangles with a non-finite vertex (culled vertices parked at NaN) collapse
     the same way.
   - Collapsed triangles have zero area: they are never hit but stay "active", so a
     later refit stays valid. NaN positions would make them inactive and break that
     rule.
3. **BLAS**: one geometry, non-indexed, opaque, `PREFER_FAST_BUILD | ALLOW_UPDATE`.
   - It is **refit** when the draw-sequence hash and the triangle count match the
     last build. That is valid by construction: same draws in the same order give
     the same triangles.
   - Otherwise it is **rebuilt**, and at least every `rt_rebuild_frames` (60)
     frames anyway, to bound BVH decay.
   - Storage grows by 25% when exceeded. The old storage goes to `defer_destroy`
     (new kind `BG3_DEFER_ACCELERATION_STRUCTURE`).
4. **TLAS**: one instance, identity transform (the soup is already camera-relative),
   written with `vkCmdUpdateBuffer` each frame. `FORCE_OPAQUE`, no facing cull.
5. **Barriers**:
   - XFB/compute/AS → transfer/compute/AS before the build;
   - compute → AS after the conversion;
   - AS → AS between BLAS and TLAS;
   - AS → compute/fragment after.
   - Before the next frame's G-buffer pass: AS/compute → XFB, because the next
     capture overwrites what this build read.
   - Compute bindings are restored after the build (`restore_bindings`).
6. **Host API v3** `rt_scene(&tlas, &frame)` gives the core the TLAS and the
   frame index it was recorded in.
   - The game records a frame's G-buffer *before* presenting the previous frame,
     and its lighting after. So the lighting nearly always sees the build stamped
     one frame earlier: in a typical 300-frame window, 0–35 "this frame" and the
     rest "previous frame".
   - It is still this frame's geometry. The GPU runs the build first (submission
     order), and the handle is the same object every frame. The primary-ray check
     below passes during a camera pan as well.
   - The core accepts a TLAS stamped this frame or the previous one, and otherwise
     falls back to the contact shadows.

Memory for 3.2M triangles: 384 MB XFB, 214 MB BLAS (capacity 3.6M), 13 MB scratch.

### 6.3 Ray-query passes (core, hot-reloaded; in the GI pre-lighting hook)

- **Sun shadows** (`rt_shadows=1`, `gi/rt_shadow.hlsl`, full resolution, 1 ray per
  pixel).
  - Position: `ray_at(uv) × linear depth`. Normal: G-buffer normal in lighting
    space. Direction: towards the sun (`cam.v0`).
  - The origin is offset along the normal by `rt_bias` × camera distance (0.002).
    `ACCEPT_FIRST_HIT_AND_END_SEARCH`, `rt_tmax` 300.
  - Back-facing pixels (N·L ≤ 0) are shadowed.
  - The result is folded into the game's sun shadow mask (`set1/b7`) before the
    tiled lighting: `mask *= lerp(1, vis, rt_strength)`. It can only darken.
    Off-screen casters, which the G-buffer does not contain, still come from the
    game's cascades.
  - The pass replaces the screen-space contact shadows whenever a TLAS exists.
    Without one (`rt=0`, loading screens) the contact shadows run as before.
  - It also works with `gi=0`.
- **AO** (`rt_ao=1`, `gi/rt_ao.hlsl`, needs `gi=1`). Runs at half resolution on
  exactly the samples `gi_trace` used, with `rt_ao_rays` (2) cosine rays of
  `rt_ao_radius` (1.0 world units).
  - Occlusion falls off smoothly with hit distance, `1 − (t/R)²`.
  - It overwrites the screen-space visibility (`giRaw.a`) before temporal
    accumulation. The existing temporal pass and `gi_ao_apply` fold it into the
    game's AO.
  - The bounce (rgb) stays screen-space.
- **Debug views** (`rt_debug`, written over the lit diffuse at the end of the tiled
  lighting; specular zeroed):
  - 1: sun visibility;
  - 2: game mask in red, traced visibility in green;
  - 3: primary-ray check of the TLAS against the depth buffer;
  - 4: accumulated ambient visibility.
- No non-finite values can reach game resources: visibility is 0/1 or a bounded
  falloff, and the mask is multiplied by a factor in [0, 1].

### 6.4 Results

**The TLAS is in the lighting's space, exactly.** `rt_debug=3` traces from the
camera origin along each pixel's frustum ray and compares the closest hit with the
depth buffer ([docs/img/rt_primary_check.png](img/rt_primary_check.png); scene colour
for reference: [rt_scene_colour.png](img/rt_scene_colour.png)).

| | |
|---|---|
| Hit within 0.5% of the depth | **98.1%** of pixels |
| Hit in front | 1.9%: the alpha-tested geometry of phase 1 (chains, grates, railings), opaque in the BLAS |
| Hit behind, miss | 0.0% |

So the camera-relative W of the G-buffer VS is the same space as the tiled
lighting's `ray(uv) × depth`, and its rotation matches too. During a camera pan
([rt_primary_check_pan.png](img/rt_primary_check_pan.png)) only alpha-tested
geometry is off, so the TLAS is the same frame's.

**Sun shadows** ([rt_sun_visibility.png](img/rt_sun_visibility.png), `rt_debug=1`)
come out as expected from the geometry:
- the character casts a sharp shadow down-left across the walkway;
- pillars, railings and pipes shadow the floors;
- the lower hall lies in the shadow of its walls;
- there is no visible self-shadowing acne at `rt_bias=0.002`.

Comparison with the game's sun mask:
- The test scene is an interior. The game's mask is lit on only 0.01% of pixels
  (mean 1.2e-4, dump of the R8 mask pass). Its cascades contain the building's
  roof, which the G-buffer does not.
- Where the mask is lit, along the top edge of the long diagonal beam at the top
  right, the traced visibility agrees (lit).
- A small cluster at the top-left corner is lit in the game's mask but traced as
  shadowed. It is probably an alpha-tested grate that the BLAS treats as opaque.
- Overall, 27 of 55 lit samples agree (2× downsampled dumps).
- Because the fold only darkens, **RT shadows change nothing visible in this
  scene**.

Outdoors:
- In 5 of 12 `-Move` runs the held pan key carried the camera out of the foundry
  over the docks within one to two minutes. The view there is sunlit, with about
  780 draws and 0.86M triangles.
  - All five were `rt=1` runs, but six other `rt=1` runs stayed inside. With so
    few `rt=0` runs (three), the difference is more likely the timing of the key
    injection than RT.
  - Frame rate was the same either way (about 190 presents/s headless).
- The game's mask is lit on most of that frame (mean 0.61,
  [rt_docks_game_sun_mask.png](img/rt_docks_game_sun_mask.png)).
- The lit result with RT shadows and RT AO folded in
  ([rt_docks_lit.png](img/rt_docks_lit.png)) shows consistent shadows from
  characters, ships and rigging, with no artifacts. (The black area is the sea:
  water is drawn later, by a forward pass.)
- A direct comparison there, traced vs mask with `rt_debug=2`, has not been
  captured yet: the pan did not reach the docks in the later runs. For that run,
  set `auto_min_draws=600 auto_max_draws=2000 auto_stable_s=20
  auto_stable_frames=1000000`. `auto_max_draws` is new in the tracer: a trace
  then waits for a light (outdoor) view.

**AO** ([rt_ao_visibility.png](img/rt_ao_visibility.png) vs screen-space
[ss_ao_visibility.png](img/ss_ao_visibility.png), `rt_debug=4`): contact
occlusion is crisper under pipes, railings and the character, and occluders that
are off-screen or behind a surface now count.

### 6.5 Cost (RTX 5090, 2560x1067, GPU timestamps, medians of 300 frames, 2.8–3.2M triangles)

| Step | Static camera | Every frame rebuilt (`rt_refit=0`) |
|---|---|---|
| XFB capture (G-buffer pass 0.31 → 0.91 ms, `geo_timing=1`) | +0.60 ms | +0.60 ms |
| Clip → world conversion | 0.27 ms | 0.27 ms |
| BLAS | **refit 0.39 ms** (95–98% of frames) | rebuild 2.3–2.5 ms |
| TLAS | 0.024 ms | 0.024 ms |
| Sun shadows, 1 ray/pixel full res | 0.08–0.11 ms | |
| + AO, 2 rays/pixel half res | +0.13 ms (0.24 ms together) | |
| **Total** | **≈ 1.5 ms** | ≈ 3.7 ms |

- `rt_fast_trace=1` (`PREFER_FAST_TRACE`): rebuild 3.3 ms, refit 0.56 ms, with no
  measurable gain in trace time (0.225–0.235 vs 0.235–0.241 ms). Fast build stays
  the default.
- The phase-1 bench (0.55 ms refit, 2.5–3.4 ms rebuild on its own device) is
  reproduced on the game's device.
- With `rt_debug=3` the extra primary ray brings the shadow pass to 0.25–0.36 ms.
- In a static view the draw sequence is identical for most frames, so most frames
  refit. A camera pan changes the culled draw set, and then frames rebuild.

### 6.6 Validation

`auto-run.ps1 -Validation -Set rt=1,rt_shadows=1,rt_ao=1` against the same run with
`rt=0`:
- the message IDs are identical: the game's own `09600`, `10788`,
  `StorageImage-FormatMismatch`, `OutputNotWritten`, its BufferBlock-layout
  `08737`, one `vkCmdCopyBuffer-00118`, and `non-acquired-swapchain-image-used`
  from headless presentation;
- none mentions acceleration structures, ray queries, our buffers or the XFB.

Found and fixed along the way:
- `VUID-VkMemoryAllocateInfo-flags-03331`, caused by NGX's allocations once the
  device was 1.2. Fixed by enabling the core BDA feature, see 6.1.
- `VUID-VkPipelineShaderStageCreateInfo-stage-08771`, an old one: the core created
  its compute pipelines with a null module and a chained `VkShaderModuleCreateInfo`,
  which needs maintenance5. It now creates real modules.
- `08737` for our SPIR-V 1.6 shaders, gone with `rt=1` (API 1.3). With `rt=0` it
  remains, as before (instance API 1.1).

### 6.7 What does not work yet / next steps

1. **Alpha-tested geometry is opaque** (1.9% of pixels, and false shadows under
   grates and chains). Capture UV as a second XFB output and keep each draw's
   alpha texture. Mark those geometries non-opaque and alpha-test in the ray query
   (`CandidateTriangle`).
2. **Only what the camera sees casts shadows.** That means no off-screen casters
   and no roofs the game hides. The fold keeps the game's cascades for those, but
   then RT can only add detail where the cascades already say "lit". Next: XFB on
   the cascade passes (their depth-only VS are patched already) to capture the
   sun's view of the casters.
3. **Static/dynamic split** (section 5, step 3). Today the whole soup is one BLAS:
   every camera move that changes the culled set costs a 2.3 ms rebuild. Per-draw
   BLAS keyed by draw identity, static ones in absolute world space and reused, a
   refit only for skinned/wind draws, and a TLAS with the camera offset. That
   brings motion down to roughly the static cost.
4. **Conversion bandwidth** (0.27 ms): write a separate float3 buffer, or convert
   inside the BLAS build's vertex fetch by giving the BLAS clip positions plus a
   transform per geometry. That works once the split exists.
5. **Outdoor validation and soft shadows.**
   - Capture `rt_debug=2` (and `3`) at the docks (see 6.4) and measure agreement
     with the game's mask where it is nonzero.
   - Then the sun's angular size: cone-sampled rays, then RR or a small denoiser.
6. **RT GI**: replace the screen-space bounce with ray-query hits shaded from the
   screen when on-screen, otherwise from a radiance cache. Denoise with RR (Track A).

Settings (`bg3rtx.ini`):
- `rt`, `rt_build`, `rt_refit`, `rt_rebuild_frames`, `rt_fast_trace`, `rt_xfb_mb`,
  `rt_bda_core`;
- `rt_shadows`, `rt_strength`, `rt_bias`, `rt_tmax`;
- `rt_ao`, `rt_ao_rays`, `rt_ao_radius`;
- `rt_debug`, `rt_timing_frames`.

The log reports `[rt] scene build GPU …` (convert, BLAS, TLAS per rebuild/refit),
`[rt] last N frames: … rebuilds, … refits` and `[core] rt passes: GPU cost …`.
