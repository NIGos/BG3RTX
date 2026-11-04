# Track D: geometry capture, BVH and scene build

Part of Track D (hardware ray tracing); index and old section map: [TRACK_D_RT.md](TRACK_D_RT.md).
Sections 1-6 and 11 keep their original numbers.

**Phase 2 (2026-10-26): ray-traced sun shadows and AO run on the game's own Vulkan
device**, against acceleration structures rebuilt or refit every frame from the
captured G-buffer geometry. Off by default (`rt=0`). Results, costs and next steps
are in [section 6](#6-phase-2-results-acceleration-structures-and-ray-queries-on-the-games-device).
Sections 1-5 are phase 1 (the capture) and the original phase-2 plan. Section 7 is
phase 3 (alpha test, hybrid GI). Section 8 is the effect catalogue and roadmap for
further RT effects (see also [QUALITY.md](QUALITY.md)).

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
`geo_timing_frames`), `geo_patch_dump=N`. `geo_xfb_always` (the cost probe below): removed
2026-10-31, see CONSOLIDATION.md C1.

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
  - The capture runs in traced frames only. (The `geo_xfb_always` cost probe was removed
    2026-10-31.)

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
- **AO** (`rt_ao=1`, `gi/rt_ao.hlsl`, needs `gi=1`; superseded by `gi_rt`: removed
  2026-10-31, see CONSOLIDATION.md C1). Runs at half resolution on
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
  the default. `rt_fast_trace`: removed 2026-10-31, see CONSOLIDATION.md C1.
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
   sun's view of the casters. **Implemented, off by default (`rt_cascade_casters`,
   section 8.5 "Cascade casters as implemented"); not gated yet.**
3. **Static/dynamic split** (section 5, step 3). **Done, see [section 11](#11-staticdynamic-bvh-split-instanced-object-space-blas-rt_bvh_split1).**
   The design differs from this plan: object-space BLAS per unique mesh with the
   game's per-instance matrices, not world-space per-draw BLAS.
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
- `rt`, `rt_build`, `rt_refit`, `rt_rebuild_frames`, `rt_xfb_mb`,
  `rt_bda_core`;
- `rt_shadows`, `rt_strength`, `rt_bias`, `rt_tmax`;
- `rt_debug`, `rt_timing_frames`.

The log reports `[rt] scene build GPU …` (convert, BLAS, TLAS per rebuild/refit),
`[rt] last N frames: … rebuilds, … refits` and `[core] rt passes: GPU cost …`.

## 11. Static/dynamic BVH split: instanced object-space BLAS (`rt_bvh_split=1`)

AUDIT_SOTA rank 2, implemented 2026-10-26 (`layer/bvhsplit.cpp`, `shaders/rt/mesh_extract.hlsl`,
`shaders/rt/rt_common.hlsli`). On by default and live: `rt_bvh_split=0` switches back to the
single soup BLAS of sections 6-7 on the next frame (that path is unchanged).

### 11.1 What was wrong

The scene was one BLAS over the whole per-frame XFB soup: refit while the draw sequence held,
rebuilt whenever it changed. BG3 culls on the CPU (AUDIT_ENGINE_DATA §0), so every camera
pan changes the drawn set, and the BLAS was rebuilt every frame of a pan (2.3-3.4 ms). Static
geometry was also captured, converted and refit every frame.

### 11.2 Design

**Which draws are instanced.** Class A (section 1) places a mesh with an affine
per-instance matrix whose three rows are vertex inputs. `analyze_vs_instancing`
(`layer/spirv_xfb.h`) proves this from the game's SPIR-V. It looks for
`W = vec4(P, 1) * mat4(R0, R1, R2, (0,0,0,1))` with P and the rows plain input loads, and W
reaching `gl_Position` only through `vec4(W.xyz, 1) * matrix`. Then W is exactly the 3x4
row-major matrix `[R0; R1; R2]` applied to P, which is the `VkTransformMatrixKHR` layout.
- Over the 2,418 dumped vertex shaders, 187 match. The foundry prepass ones are `e6fc6bf6`,
  `689836de`, `31c4659f`, `646a930e` and `f4b35620`.
- These are rejected: wind and cloth (`5f757332`: W gets a displacement), the variants that
  add a per-draw UBO translation to the rows (`2ad8656b`, `106a0550`, `05d39f75`), and the
  skinned and per-draw-matrix variants.
- `make_inst_pipe` (`layer/geocapture.cpp`) checks the pipeline's vertex input. The rows must
  be `R32G32B32A32_SFLOAT` attributes of instance-rate bindings with divisor 1, and the
  position must come from a per-vertex binding. For alpha-tested draws, the captured UV must
  be a copy of a per-vertex input.

**Mesh key.** The VS hash and per-vertex input layout, the per-vertex vertex buffers and
offsets, the index buffer, offset and type, the draw's count, first index and vertex offset,
and the alpha test. New hooks provide the buffers: `vkCmdBindVertexBuffers(2)` and
`vkCmdBindIndexBuffer`.

**Building a mesh, once.** The first time a key is seen, its draw is captured as before.
`mesh_extract.hlsl` maps the first instance's triangles back to object space with the
inverse of that instance's matrix, into a mesh pool (with the UV, stride 24).
- It checks the result against the draw's last instance (`M_last · P` against its captured
  triangles).
- A single-instance draw is checked again in a later frame instead: `M_now · P` against that
  frame's capture.
- Collapsed triangles (sky z override, NaN-parked vertices; `xfb_to_world` now marks them
  `w = 0`) and singular or non-finite matrices reject the mesh.
- Rejections back off and are retried later: 600 frames, doubling.
- The mesh's BLAS is built from the pool (`PREFER_FAST_TRACE | ALLOW_COMPACTION`) and
  compacted once its size is back.
- Results come back through a host-visible status buffer that a transfer marks done after
  the batch's jobs. A query's availability can still be a previous use's until the
  GPU-side reset executes. The first version read stale availability and lost results.
- Until a mesh is verified, its draws stay on the per-draw path below. New meshes are
  budgeted at `rt_mesh_new_tris` triangles per build.

**Every frame.** A draw of a verified mesh is not captured at all. Each of its instances
becomes a TLAS instance whose matrix comes from the game's own instance stream, which is
host-mapped.
- The matrices are copied when the game **submits** the command buffer (`bvh_on_submit`),
  which is exactly what the GPU will read.
- Non-finite or singular matrices get mask 0.
- The log counts draws whose instance data changed between record and submit. It was 0 in
  every run.

**Everything else** (skinned, wind/cloth, per-draw matrices, terrain, sky, meshes in
probation) is captured and converted as before. Each draw gets its own BLAS, keyed by the
draw's identity (pipeline, binding 0, index range, n-th occurrence in the frame).
- It is refit while its triangle count and build flags hold.
- It is rebuilt at least every `rt_rebuild_frames`, staggered per draw.
- So a draw entering or leaving the view costs only its own build.

**TLAS** over all instances (`PREFER_FAST_TRACE | ALLOW_UPDATE`).
- Instance records live in a host-visible ring (8 regions).
- The TLAS is refit while the instance list (count, BLAS and flags in order) holds, at
  least every `rt_rebuild_frames`.

**Eviction.**
- A mesh is evicted when one of its game buffers is destroyed (the one `vkDestroyBuffer`
  hook, in `tracker.cpp`; geocapture's duplicate hook used to shadow the tracker's), or
  when a transfer writes its index or vertex range (`vkCmdCopyBuffer(2)`,
  `vkCmdUpdateBuffer`, `vkCmdFillBuffer` hooks).
- It is also evicted by LRU over `rt_mesh_budget_mb` and after `rt_mesh_lru_frames` unused.
- A periodic **audit** catches any rewrite the hooks cannot see (host writes to mapped
  vertex memory). Up to `rt_mesh_audits` cached draws per frame, each mesh at most every
  `rt_mesh_audit_frames`, are captured once more and compared. A failure evicts the mesh
  with a warning.

**Ray queries** (`rt_common.hlsli`). One `InstInfo` per TLAS instance (set 1 binding 7)
gives: where its vertices are (the mesh pool, binding 8, object space; or the soup, binding
6), the stride, the alpha record, and the triangle count.
- Alpha-tested candidates read their UVs and corners through it and transform them by
  `CandidateObjectToWorld3x4` for the footprint.
- `rt_origin`'s geometric normal uses `rt_hit_normal`, which applies the committed object
  to world transform.
- The legacy TLAS has one instance marked `kInfoLegacy` and keeps the old lookups (geometry
  0/1, records by triangle index).
- Opaque draws' instances are `FORCE_OPAQUE`. `rt_alpha_test=0` forces all instances
  opaque, as before.

### Invisible helper draws (`rt_capture_discard`, 2026-10-30)

The inn's two dark floor squares come from one captured 24-triangle helper draw,
VS `ca869467434cd2db`, FS `dc180592a8fc6087`. In capture 890 both floor regions hit
TLAS instance 1834 in frame 3992, on the per-draw path. This FS has one empty
entry block ending in `OpKill` and no EarlyFragmentTests mode. It never produces
raster depth or color, regardless of its pipeline's depth-write flag.

Transform feedback runs before fragment discard. `rt_capture_discard` filters
this proved always-discard case before either BVH classification or XFB allocation.
`common/spirv_visibility.h` recognizes only that exact control-flow shape. It
rejects conditional alpha tests, normal returns, multiple entry points and early
fragment tests (which can write depth/stencil before the kill). Raster commands
still run. The check is cached per module/pipeline and its live key once per capture.
No host ABI or lighting shader change is needed.

The split-off oracle was incomplete: the legacy capture overflowed about 168
opaque draws/frame and omitted this late helper. A corrected same-run refit sweep
leaves 30,343-30,362 opaque-front pixels for TLAS refit, dynamic refit and in-place
rebuild toggles. Capture-all/audit-all also overflow; their lower residual is not
parity. The cause is capture eligibility, rather than firstInstance, instance count,
vertex offset or stale transforms.

Gate: `sup-hidden-quads-discard-gate`, valid FG 2 run, 0/1 twice in debug 8.
Opaque-front pixels 29,411 / 29,429 -> 132 / 127; the two square regions together
29,288 / 29,303 -> 0 / 1 pixels. Primary-ray match 96.095% / 96.092% ->
96.951% / 97.190%. Normal-image mean +0.28%. BVH build GPU median, with all
statistics and primary diagnostics off: 1.387 -> 1.368 ms (14 timing windows each;
median reported p99 1.791 -> 1.686 ms). Zero VK errors, NVIDIA events or dumps.
Kept, `rt_capture_discard=1` by default. Check The Blushing Mermaid - 96h 39m near
the crates and stage edge after a layer restart. Diagnostic keys remain default 0.

### Static-camera LOD retention (`rt_dyn_hold`, 2026-10-29)

BG3 alternates captured LOD draws every two frames even with the camera still
(sup-static-flicker3-fg2: 380 / 764 and 310 / 154 primitives). `rt_dyn_hold=N`
keeps a missing captured draw for N frames since its last actual capture (max 120).
Movement clears all holds. The last submitted BLAS is reused; a GPU copy of all
positions and UVs lives in the persistent mesh pool, with an identity transform
and the existing pool InstInfo flag. No shader or host/core ABI change is needed.
Alpha records retain actual views and material constants, remapped to the current
scene's descriptor indices. Views and images are protected through the hold window.
Dropped command buffers cannot supply holds. Pool ranges retire through the existing
GPU/scene lifetime guard. Held draws never refresh their own expiry.
Gate pending: QuickSave_3784, FG 2, static camera, live hold 0 / 4 for 40 seconds each;
alternations within 8 frames, primary match, RT GPU cost. Default remains 0 pending gate.

### 11.3 Results (RTX 5090, 2560x1067, Steel Watch Foundry, `-Background`)

**Geometry parity.** The same view, live A/B in one run, `rt_debug=3 rt_stats=1`:

| | legacy soup | split |
|---|---|---|
| primary-ray match | 98.976-98.984% | 98.974-98.980% |
| in front (opaque / vt cutouts) | 1.007-1.015% (0.95 / 0.060) | 1.011-1.017% (0.95 / 0.060) |
| behind / miss | 0.009% / 0% | 0.009% / 0% |
| triangles in the TLAS | 2.59M (0.68M alpha-tested) | 2.060M static (2,070 instances of 507 meshes) + 0.535M dynamic (182 draws) = 2.595M |
| ray origins: TLAS surface found under | 98.3-99.3% | 98.3-99.3% |

The 0.95% "opaque in front" is the screen-door dithered geometry of 7.1, present in both.
- In the 3D main menu, before the save loads, the primary check reads 67-69% with the legacy
  soup and 68-72% with the split (separate runs, same scene). That low figure belongs to the
  menu scene, not to the split.
- The static instance count follows the view. During a pan the cache grew to 627 meshes
  (1.81M unique triangles) with 0 rejections for mismatch.

**Cost.** GPU timestamps around the scene build (convert + extraction, BLAS builds/refits,
TLAS). The medians are the medians of 300-frame windows; p99 is the worst window's p99.
The depth prepass time with XFB active is separate (`geo_timing=1`).

| | legacy | split |
|---|---|---|
| **camera panning** (`-Move`), scene build | median **1.37 ms**, p99 **2.91 ms** (every frame a rebuild: BLAS 1.47-2.55 ms) | median **0.45 ms**, p99 **0.83 ms** |
| panning, depth prepass with XFB | 0.22 ms (0.16-0.37) | 0.17 ms (0.14-0.20) |
| static foundry, scene build | 0.71-0.72 ms (p99 0.73-0.92); forced rebuild every 60 frames 2.8-2.9 ms | 0.49 ms (p99 0.75) before the TLAS refit; **0.29-0.35 ms (p99 0.57-0.74)** with it |
| static foundry, depth prepass with XFB | 0.47-0.49 ms | 0.23-0.24 ms |
| static, light view after the pan | 0.21 ms (refit of a small soup) | 0.34-0.37 ms (TLAS 0.14 ms, 60 small per-draw refits 0.17 ms) before the TLAS refit; TLAS 0.020 ms once refit (98% of static frames), total 0.25 ms in a comparable light view |
| level load / first frames | rebuild 2.9 ms | up to 2.1 ms p99 while ~500 meshes are built once |

- **Motion: −0.9 ms median and −2.1 ms p99** per frame on this machine.
- **Static foundry: about −0.65 ms** (build −0.40, XFB −0.25), and no 2.8 ms rebuild every 60
  frames.
- In the light outdoor view after the pan, the split was 0.15 ms slower than a legacy refit:
  the fixed TLAS cost plus many small refits. The TLAS refit (11.2) was added for this.
- The forced legacy rebuild every 60 frames (2.9 ms, the 1% lows) is gone for static
  geometry.

**Memory** (foundry): mesh pool 87-124 MB (object-space position + UV, 72 B/triangle).
Mesh BLAS after compaction 78-90 MB for 1.3-1.8M unique triangles. Compaction saved 22-44 MB
per 100-500 meshes (about 25-35%).

### 11.4 Validation layer and robustness

- `auto-run.ps1 -Validation -Background` (Khronos validation below the layer): a baseline
  was taken with the unmodified main first, then the split was run in gameplay with a 15 s
  pan (8 minutes). **The message IDs are identical**: the game's own `09600`, `10788`,
  `08737`, `CopyBuffer-00118`, and the headless `non-acquired-swapchain-image-used`.
  - The first split run added two IDs of ours: `vkCmdFillBuffer-dstBuffer-00029` and
    `vkCmdCopyQueryPoolResults-dstBuffer-00825`. The status buffer lacked
    `TRANSFER_DST` usage; that is fixed.
  - A third run covered the main menu only, also clean.
- Non-finite values: 0 in every image dump of the 15 traces taken during this work (legacy
  and split, static and panning).

- Per run: 0 non-finite or singular instance matrices (about 600k read per 300 frames), 0
  late instance writes, 0 commands recorded but never submitted, 0 validation results lost
  after the fix, 0 audit failures over thousands of audits, and a maximum accepted error of
  0.012 × tolerance. The one recurring "invalid" rejection is the sky dome (z = 0).
- `-Move` runs: no device loss, no GPU page fault, no crash.
- The alpha region "full" warning in the main menu (64 of 77 MB) predates this work (the
  legacy capture shows it too).

### 11.5 What does not work / next steps

- **Not compared:** Campsite, Lower City and Water Queen's House (foliage, view distance) were not
  run with the split; `fg>=2` with the split.
- **UBO-offset class-A variants** (`2ad8656b`, `106a0550`, `05d39f75`; 45+ draws in the
  foundry prepass) stay per-draw: their rows plus a per-draw translation from a UBO. The
  matrix could take the UBO offset (read like the alpha constants).
- **Skinned meshes** use XFB per draw (refit each frame), not the skinning SSBO b13 directly
  (AUDIT_ENGINE_DATA 2.4); that would save their XFB.
- **Wind foliage** (class D) is per-draw and rebuilds when its instance count changes;
  outdoors it is the remaining motion cost. A rest-pose instanced BLAS plus a refit, or
  CLAS, are the options.
- **Opacity micromaps** (AUDIT_SOTA rank 4) can now attach to the cached mesh BLAS: the pool
  holds the UVs, and OMMs survive compaction.
- **Retention (EN3)**: meshes stay cached after leaving the view; keeping their instances in
  the TLAS needs the last matrices and a policy.
- `-Move -Background` posted one key-down every 2 s, which the game reads as taps, so the
  pans of earlier docs were mostly static. `auto-run.ps1` now posts key-downs at about 30 Hz
  with the repeat bit (to the game window only).

Settings (`bg3rtx.ini`, all live):
- `rt_bvh_split`; `rt_tlas_refit`; `rt_rebuild_frames`, which now paces the per-draw BLAS
  and the TLAS;
- `rt_mesh_compact`, `rt_mesh_compact_per_frame`, `rt_mesh_new_tris`;
- `rt_mesh_pool_mb`, `rt_mesh_pool_max_mb`, `rt_mesh_chunk_mb`, `rt_mesh_budget_mb`,
  `rt_mesh_lru_frames`;
- `rt_mesh_audits`, `rt_mesh_audit_frames`, `rt_mesh_tol_abs`, `rt_mesh_tol_rel`.

The log reports `[bvh] last N builds ...` (draws, instances, triangles, refits),
`[bvh] mesh cache ...` (meshes, memory, verification, rejections, evictions, compaction),
`[bvh] instance matrices read at submit ...`, and
`[rt] scene build GPU (<mode>, static|camera moving, N samples): ... total median, p99`.
`[geo] depth prepass pass GPU time (...)` now gives p99 and splits by motion and mode.

## Split capture partition spill (2026-10-31, offline candidate)

Key rt_alpha_spill=0 is Experimental and live; OFF in defaults/presets. Existing
alpha overflow captured a draw as opaque even when the opaque storage range had
room. Exact triangle-list split captures with a direct record now may use that
space without losing alpha semantics. The actual absolute offset, AlphaRec,
Hair procedural bounds and alpha instance flags survive. Only the storage cursor
changes; no geometry, coverage, memory limit, shader or public ABI changes.

105 CPU budget checks and 335 settings checks pass; six targeted CTests and the
host/launcher build pass. Independent conversion/binding/Hair/lifetime review is
clear. No GPU/game participation, image quality or cost measured. Log rescued
spills separately from residual opaque substitution; remove their bytes from the
next intrinsic opaque reserve. Record/material failures and both ranges full
retain their existing behavior; UNKNOWN policies are not universally native fallback.

User check after a qualified automatic comparison: camp rug fringes and planks,
no new dark card rectangles or flicker; actual save identity still unspecified.
Require nonzero rescued draws, zero residual substitutions, RT active and stable
G-buffer/primary matching in one OFF/ON run. Unaffected regions preserve energy
within OFF-repeat noise; removal of false occlusion may legitimately brighten
the affected region. Screenshots alone do not identify the
pictured mesh or prove allocation overflow caused those particular rectangles.

## P1 rt_scene_wait modes — 2026-10-31

`rt_scene_wait=1` (default) fixed the rotation detachment (primary match 33% -> 97%): the first RT query
of frame N blocks the lighting recording thread (at most `rt_scene_wait_ms=12`, mean 4.6-4.9 ms) until the
G-buffer thread commits frame N's scene slot. GPU order is already right (one vkQueueSubmit, G-buffer first);
the wait orders CPU recording: the RT passes bind the slot's TLAS and set 1 when recorded, and frame N's slot,
TLAS and set exist only after the build is recorded. A semaphore cannot rebind recorded handles; a full GPU-side
fix needs descriptor indirection (update-after-bind sets or a proxy TLAS), not attempted.
`rt_scene_wait` is 0 (no wait, traces the previous frame's scene) or 1 (default).
Mode 2 removed 2026-10-31: stale scene after rotation; see live batch consolidation-20261031d job cb2.

### Hit-material CPU on the scene commit path (2026-11-01, `rt_hit_material_recycle`, default 0, READY_FOR_LIVE)

User report: with native hit materials on, RT shadows detach during rotation/pans and flicker, the stale-scene
symptom `rt_scene_wait` had fixed. `[rt] scene wait` per 600 frames: without `rt_hit_material` 0 timeouts in steady
lines (lr1 camp 08:40, mean 4.8-5.0 ms); with it 15-278 timeouts, mean 5.9-11.8 ms (m4i 111403), 3-232 (m4p4 113146),
and 5-456, mean 9.8-12.5 ms in the user's own run (17:26-17:35, every experimental key on). Water-only runs ~0. A mean
at 12 ms is the `rt_scene_wait_ms` cap: the commit came after the lighting thread gave up, so it traced frame N-1.

Cause (code read; no CPU timer existed for it): the provider allocated and freed GPU memory every frame on the
recording threads, under locks the commit path takes. Per frame: a deferred packet (1 MB rows + 1-4 x 32 MB XFB chunks
+ 2 MB staged) in the G-buffer material hooks, a query (rows, up to 256 MB of copies, pool UBO, remap counters) on the
lighting thread, up to 8 UV/basis jobs (XFB + host status), the slot's source stream when its need moves past the 2x
hysteresis; and `collect()` freed the retired ones of 2-3 frames earlier, from whichever hook ran first: the prepass
begin, `scene_prepare` (under the scene lock, before the BVH build), or `deferred_begin`, which also ran `collect()`
(a std::set rebuild over every owner) on every G-buffer draw under the provider mutex that the prepass hooks
(`note_prepass`, `note_opaque_prepass`) need per draw. The lighting thread's query also held the scene lock `g_mu`
for its whole recording (allocations, set clone, descriptor writes), the lock the next capture end needs to commit.
Pans move the copied sizes every frame (more reallocations), which matches the worse moving windows.

Fix, `rt_hit_material_recycle=1` (live A/B, layer only): retired buffers go to a pool (common/native_hit_recycle.h:
exact key = rounded size, usage, memory kind; sizes rounded to 8 per octave, at most 12.5% over; cap 512 MB = one
frame's largest transient set 384 MB + rounding; idle 120 frames freed; all freed when the key goes off). A buffer
is given back exactly where it used to be freed (no GPU user left), its contents are undefined as a fresh
allocation's, and every user writes what it reads (rows cleared + frame header each use), so the frame-pairing checks
(slot frame, generation, row header, publication word) are unchanged and rows of another frame are still rejected.
`collect()` runs once per new packet / new query instead of per G-buffer draw / RT pass. The query finds and pins
its slot under `g_mu` (last_use = now, so no build can take it), then records without it. The commit order and
the GPU work are unchanged (scene_prepare's copy must stay before the in-place convert).

Measure (cpu_timing=1): `[cpu] hit material over N frames` (prepass hooks, scene_prepare, G-buffer material hooks,
query; collect, allocations n/frame and MB/frame, frees, reused, retained) and the `[cpu] scene build recording`
line's new tail "capture end to commit, of which scene lock wait, hit material scene_prepare".
`rt_scene_wait_log_frames` (default 600) sets the scene-wait line cadence; lean jobs use 120.

Gate: every lean job gates `[rt] scene wait` with 0 timeouts in every traced cost-* window
(`py -3 scripts/queue_gates.py --lint-scene-wait`, a CTest via tests/queue_gates_test.py and a lean_dryrun check;
`queue_gates.SCENE_WAIT_GATE`). Live A/B prepared: `tools/live_batches/scene_wait` (S8 + camp, not authorized).
Expected with 1: 0 timeouts, wait mean back to the no-hit-material 2-5 ms; allocations ~0/frame, reused 5-20/frame.
User check after it passes: FG on, S8 or camp with the hit-material bundle, rotate with Q/E or the mouse: shadows
stay on their casters.

## Base frame CPU cost - 2026-10-30, default-off candidate

`rt_alpha_stream=0/1` selects ordinary copies or aligned streaming loads for alpha
material constants from mapped game uploads. The split instance path already uses
streaming loads. Prefixes/tails stay inside the requested range; shader and ABI stay
unchanged. Capture once per pass reads the key, avoiding per-draw config overhead.

Static inn, Ultra 3840x1600, FG 0: capture CPU 4.224 -> 2.973 ms/frame, of which
pre-draw 2.989 -> 1.730. Build recording (included) ~0.97 and submit copies ~0.48 ms.
GPU chain / scene / RR: 2.54 / 1.67 / 2.36 -> 2.55 / 1.63 / 2.36 ms. Primary match
98.969 -> 98.968% median, overlapping ranges; exact-copy alignment/boundary CTest
passes. No final-image energy comparison. No VK errors, driver events or dumps.

NOT QUALIFIED for promotion: all stats-off A/B windows stay at a 30 fps ceiling.
WDDM GPU busy ~10.6 ms vs 33.3 ms presentation; no uncapped CPU/GPU bound claim.
Keep default 0 and branch unmerged until an authorized foreground speed gate passes.
Full rt_stats parity windows raise RT GPU cost from 1.03 to ~19.4 ms: never include
them in speed gates. Check: inn save, static view, toggle rt_alpha_stream live, compare
[cpu] pre-draw/capture and rendered fps with all stats off. See PROGRESS_CODEX and
docs/evidence/perf-base-20261030.json; job sup-perf-game-uncapped.

### BLAS chunk reclamation (2026-11-02, codex-as-compact, offline candidate)

- Each chunk tracks live bytes separately from occupied bytes. Occupied bytes include
  pending frees until at least 6 presents, the frame + 2 GPU fence, and retirement of
  every old scene slot. The 240-present timeout no longer releases these ranges.
- Every wholly empty chunk goes through the existing deferred buffer/memory destruction
  queue on present, regardless of `rt_vram_guard` or its low/cap state. Slots stay in the
  vector and are reused in place; chunk indices never move. Diagnostic kept AS handles
  pin their own chunk, including after `rt_as_keep` is switched off, until destroyed.
- New BLAS ranges use first-fit within chunks tried from fullest to emptiest (occupied /
  capacity; stable index tie break). Pending bytes count as occupied, but cannot be reused.
  This concentrates future allocations and lets draining chunks empty without GPU copies.
- General relocation is deferred: the existing COMPACT copy/swap completion path owns
  cached meshes only. Dynamic refit BLAS, cascade and sun clone owners share this allocator;
  moving them needs ownership and recorded-copy lifetime handling beyond the small fix.
  No reserved/live trigger ratio is introduced for this permitted fallback.
- TLAS instance records take current mesh/dynamic/cascade addresses each build. The BLAS
  address enters the TLAS signature; a changed signature forces BUILD instead of UPDATE.
  Older TLAS slots still need the retirement checks above. Existing one-time mesh
  compaction remains unchanged and benefits from the new placement policy.
- `[bvh] mesh cache` reports active chunk count, reserved/live/pending MB and window freed
  chunk count/MB, plus `relocated 0` (general relocation). Existing compactions stay separate.
  Freed means handed to deferred destruction; physical heap release follows that queue.
  `[mem] ours` live now excludes pending ranges; diagnostic-pinned chunks are not yieldable.
- No key, ABI, shader or variant changes (request delta 0). No added GPU commands. GPU ms
  and actual reclaimed VRAM remain unmeasured: no game or GPU test was run. Restart the
  game for the layer change, then compare these counters in a separately authorized run
  of the S8 low long view, including a scene transition and a GPU backlog window.

### Cached mesh VRAM compaction (2026-11-02, codex-mesh-pool, offline candidate)

- `rt_vram_compact=0` by default, including every preset. Experimental GUI entry
  "Compact ray-tracing caches". Host-layer change needs a restart to load the code;
  the key itself is live. No host/core ABI or shader change; variant request delta 0.
- Pool growth doubles capacity. Shrink qualification is therefore used <= reserved / 4,
  with target max(start capacity, 2 x used), aligned for pool copies. This frees at
  least half while retaining one doubling step of headroom. Demand must remain low
  for 240 + 8 scene slots + 6 retirement presents = 254 frames, beyond the existing
  build-result watchdog and retirement interval. A failed allocation resets the window.
- Repack requires ready meshes, no outstanding BVH batch/pending pool range, no dynamic
  hold/hair pool range, and no native material basis jobs. Native hair capability
  disables pool repacking, because newly visible procedural bounds require pool writes.
  These restrictions leave BLAS chunk relocation available independently.
- GPU copies include every live vertex and material-basis range. While copying,
  allocations and basis captures pause; existing cached geometry remains usable, and
  new geometry keeps the ordinary per-draw soup path. Confirmation is a dedicated
  private status word, separate from the ordinary status ring. Dropped recordings discard the candidate; stalled recordings
  never publish on a timeout. The old allocator remains active until confirmation.
  Timed-out BVH/native-basis pool users conservatively block repacking until their
  command resets; another command's registered unsubmitted BVH work also blocks it.
- Commit rebases surviving mesh generations, material rows (including offscreen ones),
  and the pool epoch. Each scene keeps its own material offsets, so an old scene's
  pool descriptor and rows agree. Evicted snapshot ranges are released in the new
  allocator; old pending frees stay with the retained original buffer. Old/cancelled
  buffers wait for the GPU frame + 2 fence, oldest live scene, and six presents before
  entering deferred destruction. A key toggle cancels publication, not retirement.
- BLAS relocation plans complete evacuation of the emptiest eligible chunk into
  fuller existing chunks, largest mesh first. No new chunk or increased reservation
  is allowed. Exact aligned sizes and allocator holes are used, not aggregate free
  bytes. Only verified cached meshes participate, after ordinary compaction resolves;
  the existing per-frame compaction count bounds a complete evacuation. CLONE copies
  use the existing completion/swap path; future TLAS builds read the new addresses.
- The source stays excluded from allocation until emptied ranges retire. Its AS
  ranges remain pinned if meshes are evicted before the copy completes. Relocation
  batches wait for completion or explicit recording drop without a 240-frame timeout.
  Dynamic, cascade, current-sun and OMM-owned BLAS are not relocated; diagnostic kept
  handles pin their chunks. A chunk with any such live owner is ineligible.
- `[mem] ours` reports active pool used/capacity, candidate/retained buffer bytes,
  repack count and successful cached BLAS moves. The existing mesh-cache capacity
  line reports relocation MB, freed chunks and pool savings. Handed-off deferred
  buffers are excluded once their explicit scene retirement has completed.
- Supplied vg1 S8 evidence: pool 467 / 2047 MB predicts 934 MB target, 1113 MB less
  active reservation after retirement. BLAS 211 / 2560 MB does not predict exact
  savings without the cached/dynamic owner distribution. CPU sizing/planning tests
  cover hysteresis, watchdog window, fragmentation, pinned ranges and no-growth moves.
  Validation: 94 / 94 permitted CTests passed (21.44 s); final layer rebuild and
  5 / 5 targeted CTests passed after lifetime safeguards. CPU compaction test: 68 checks.
  GPU cost and visual/energy invariance remain unmeasured; no game was launched.

### BLAS motion fragmentation (2026-11-02, codex-vram-motion, offline candidate)

- vg2 at 21:55:12: 62 chunks, 4077.7 MB reserved, 867.3 MB live and 588.3 MB
  pending. The other 2622.1 MB is free space inside pinned chunks, not pending
  retirement. Existing freed-range suballocation and fence retirement already work.
- The fullest-chunk policy mixes short-lived rebuilds with cached compacted meshes.
  A small mesh placed in a rebuild chunk's tail keeps the entire chunk alive after
  the large range retires. A slightly larger next rebuild cannot fit its old hole.
  Per-draw, cascade and sun rebuilds all used that same allocation domain. The log
  does not identify every chunk owner; the exact contribution awaits vg3.
- With rt_vram_compact on, allocations now use three chunk arenas: cached mesh and
  compaction, per-draw, cascade/sun. Cached relocation destinations also stay in the
  cached arena. Sizes, chunk granularity and all retirement fences stay unchanged.
  There is no extra GPU command and no unsafe in-place full BLAS rebuild.
- Turning the key on does not move existing mixed allocations. They drain through
  ordinary ownership/retirement or verified cached relocation. Turning it off restores
  mixed placement; a chunk's arena records its original allocation class.
- CPU regression: 96 frames of increasing rebuild demand plus small persistent
  compaction destinations, with six-present/GPU/scene retirement. Segregated reserve
  stays at most 448 MB; mixed reserve reaches 1344 MB. Both finish with 354.8 MB
  occupied (including retirement) and 24.0 MB live. This reproduces fragmentation,
  not the exact game workload or an expected 3x runtime gain.
  Tests also preserve pending fences and exclude dynamic/cascade relocation targets.
- No shader/ABI change, variants delta 0. Keep the key off by default. Actual motion
  coverage, physical reserve and GPU ms require the separately authorized vg3 batch
  at S8 low long view; no game was launched for this change.

### VRAM defaults qualified (2026-11-02, vram-default-on)

vg3 at the S8 port passed real-budget OFF/ON/OFF motion, energy and budget gates.
`rt_vram_guard`, `rt_vram_compact` and `rt_capture_elastic` now default to 1, automatic
without GUI entries; the test-only budget override stays 0. This supersedes the
default-off candidate status above. All presets inherit the automatic defaults.

Opaque drop peaks right 1.78 / 0.39 / 1.20, left 1.10 / 0.64 / 1.77 draws/frame;
ON energy +0.1836% within +/-0.92%; ON usage at most 21,860 / 31,419 MB.
Complete-turn RT-pass GPU intervals OFF-a / ON / OFF-b: right
5.363 / 5.464 / 5.657 ms, left 5.321 / 5.269 / 5.287 ms (whole-pass costs,
no isolated compaction timer). smoke_ultra retains opaque drops 0.00 and adds
every-window `[mem] ours` within budget. Check a long low-camera view and turns at
S8 Water Queens House port: `[mem] ours` within budget, no new stutter/flicker.
Full gate numbers, GI costs and source log: [progress/vram-default-on.md](progress/vram-default-on.md).

### Elastic primary capture drop regression (2026-11-02, fix-elastic-drops, offline candidate)

su1-ultra-s8-fg2.1.223616 is INVALID: pan-e and rotate-r violate the every-window
zero opaque-drop gate. At 22:38:10-12 primary capture grows 544 -> 720 -> 912 MiB;
the following 120-frame summary reports opaque peak 394.2 MiB, alpha demand
118.5 MiB, final alpha region 364 MiB and final capacity 912 MiB. Peak demand
already includes rejected opaque bytes. Comparing it to final capacity hides
the smaller allocations/partitions earlier in the window. Growth after a
completed capture cannot recover draws already rejected during that capture.

The primary capture now retains at least effective rt_xfb_mb (1024 MiB for
Ultra's 64-byte layout). Predicting an arbitrary next draw from past frames
cannot safely permit a smaller buffer; replacing it mid-recording would need
to preserve existing Vulkan commands, geometry and published offsets. This
conservative floor gives up primary shrink savings below the fixed baseline.
Cascade shrinking remains unchanged. Primary growth restores the 25% reserve
at the next safe producer boundary, before observed demand exhausts capacity.
Demand includes the actual alpha reservation and physical opaque placement.
The rolling maximum plus margin persists for 120 captures, across replacement;
a lower card cap cannot force a shrink below reserve already allocated.

A separate fixed-capacity alpha hysteresis state bounds the elastic split so
both streams retain at least their fixed-path room. This also covers the inn
proportional-split threshold: 1024 -> 1040 MiB could otherwise reduce opaque
room from 483 -> 413 MiB. Lifetime fences and allocation guards are unchanged.
Defaults remain on. No shader/ABI changes; variant request delta 0. No added
GPU commands; cost and live coverage remain unmeasured. Layer restart required.
S8 smoke_ultra must pass buffer-full 0.00 in every pan/rotation window before
qualification. No game launched. Unapplied, independently tested default-OFF
alternative: [patch](alternatives/fix-elastic-drops-default-off.patch).

### Elastic demand headroom (2026-11-03, cascade-elastic-headroom, offline candidate)

su1-ultra-s8-fg2.1.205937 at 21:02:20 grew 272 -> 368 MiB only after
casters had been dropped (pan-q buffer full 0.1; demand max 301.71 MiB).
The cascade window now restores reserve before overflow, like the RT window.
Both retain the largest positive observed demand step across quiet windows.
Reserve is max(demand / 4, preset capacity / 4, step * delay), rounded with
demand to 16 MiB. The preset quarter reserve bootstraps the first pan without
hardcoding S8 numbers. Delay covers four fence frames plus the next producer:
ceil((4 + 3) / 3) = 3 cascade-slot observations; 5 primary observations.
Cascade inactivity is not a growth observation. The existing 120-sample rolling
target peak, shrink hysteresis, initial preset allocation, RT fixed floor,
partition safeguards and retirement fences remain. A falling growth cap at
replacement cannot turn a growth request into a shrink.

The synthetic summary-envelope trace 206.18 -> 301.71 MiB (not exact frame
telemetry) settles at 320 MiB static, grows to 592 MiB after the step, then
shrinks to 320 MiB after 120 quiet samples while retaining learned headroom.
For three slots: 960 MiB static vs old 816 (+144), vs fixed 1281 (-321);
1776 MiB after that conservative step vs old 1104 (+672). Detached allocations
overlap temporarily: 3 * (320 + 592) = 2736 MiB if all three overlap.
Actual per-slot demand steps and fence latency determine runtime allocation.
No GPU commands, shader requests (delta 0), ABI or settings changes; GPU cost
unmeasured. Layer restart required. READY_FOR_LIVE: expect cascade buffer full
0.0 and RT opaque buffer full 0.00 in EVERY su1 S8 window; inspect sun casters
during Q/E pans. No game launched. Past observations cannot bound an unseen
larger discontinuity, unbounded fence stalls or allocation/VRAM refusal; this
candidate is not a universal zero-drop guarantee or a live quality pass.

### Elastic cascade inactivity regression (2026-11-03, promote-omm, offline fix)

ds3 at 07:57:46-07:58:06 shrank cascade capture 320 -> 16 MiB after 24
zero-demand samples. Returning casters then lost about 230 draws over 2-3
frames before replacement. Already-recorded commands and offsets prevent a
simple buffer replacement from recovering those draws in the same frame.

Empty demand now cancels the shrink window and retains the current allocation.
No finite inactivity timeout can guarantee first-return capacity: casters can
remain disabled indefinitely. A shrink therefore needs 24 consecutive nonempty
samples proving a smaller working set; inactivity never supplies that evidence.
Demand still includes rejected bytes. Card/storage caps, allocation refusals,
detachment and GPU retirement fences are unchanged. This retains more memory
while inactive and adds no GPU commands, shader variants or ABI changes.
CPU regression covers all three slots, 240 empty samples per slot, first-return
fit, smaller active-set shrink, interrupted observations and tighter budget caps.
Layer restart required; live confirmation has not been run for this fix.

### Elastic cascade turn peak (fixes4, 2026-11-03)

The 10:00-10:17 analysis found a 256 MiB cascade buffer facing 271 MiB demand
for one frame after the 24-sample window forgot a turn's peak. Cascade shrink now
uses the same 120-sample window as primary RT capture. Zero demand still resets
the observation and retains capacity; shrink needs 120 nonempty samples. CPU
regressions cover the turn peak, every slot, inactivity and interrupted shrink.
No shader/ABI/variant change or added GPU work; live overflow confirmation pending.

### CPU hit-material lists (historical implementation, 2026-11-02)

`perf_hitmat_lists` retired 2026-11-04: no saving (G-buffer hooks 1.16 ms both); hidden developer INI only, default 0; shared material snapshot/list paths retained.

`perf_hitmat_lists=0` preserves the existing path. On: prepass writers use a
command-recording-owned candidate list with a recording-local leaf mutex. Other
recordings no longer share the pending mutex on each candidate. Scene preparation
seals an immutable list and releases the pending registry mutex before allocation
and copy recording. The mode is latched at prepass begin, so a live toggle cannot
split one recording between registries. Duplicate keys and opaque limits are kept.

Queries pin finalized packet metadata and an immutable source snapshot under the
provider mutex, add the query recording to source owners, and do CPU pairing without
the provider mutex. Relocking revalidates command/source recording, frame,
generation, descriptor set, attached/prepared state, packet uniqueness/validity,
query capacity, and material resource currency before GPU recording. Allocation,
mutable counters, remap and submit-order publication remain under their old locks.
The existing CPU hit-material window line includes mutex wait ms and query record
ms, both per frame across the reported window. Query record starts at row upload
and includes join, remap and pool gather recording. See
[progress/codex-perf-hitmat.md](progress/codex-perf-hitmat.md) for the unqueued camp
historical A/B plan. The later no-gain verdict supersedes that plan.
### CPU hook state and submit matrix reuse (historical implementation, 2026-11-02)

`perf_hook_state` retired 2026-11-04: submit CPU 20.2 -> 26.8 ms (+6.6 ms); hidden developer INI only, default 0; shared matrix helpers serve the default path.
- `perf_hook_state=0` controls CPU audit ranks 2 and 3 together; hidden developer INI key; absent from the GUI and preset expansion.
  Internal queries accept the resolved recording state but always re-read current descriptors/offsets and
  retain lifetime validation. No command pointer cache, host ABI change, shader edit or request delta.
- Immutable pipeline snapshots/eligibility are cached per thread by handle and registry generation;
  create/destroy invalidates all entries, including recycled handles. Classification also keys on shader
  hashes, capture stride and tileset capture mode. Runtime material enable and diagnostics stay live.
- Submit-local matrix keys are the three exact 16-byte source row addresses after stride/index resolution.
  Load and validate each triple once, then scatter raw rows to instances (invalid mask 0) and jobs (invalid
  first float NaN). Preserve per-destination late-write logging and invalid counts. Never reuse next submit.
- Existing hook CPU line reports draw bookkeeping and key state over 300 frames; matrix line reports
  ms/build, matrices, unique sources, builds and submits over 300 submit samples. Toggle changes discard
  partial samples. A nonempty submitted fixup list is one build, matching its replacement at record time.
- Explicit C++ build passed; 100/100 permitted CTests passed. No live performance, GPU cost or visual gate
  measured. Conditional estimates only: 0.1-0.3 ms/frame hooks, 0.1-0.3 ms/build with repeated matrices.
  A/B: S3 camp and S8 Water Queens House, OFF/ON/OFF/ON in one session per save, qualified Ultra keys on.
  Historical plan: [codex-perf-hooks](progress/codex-perf-hooks.md); rejected, do not queue.

### Native hair load admission (hairline-band, 2026-11-03)

The retained hq4 Dialogue3 log (`captures/queue/logs/hq4-dialogue3-fg2.1.140457.log`
in the main checkout) hits pool full at 14:06:33.182. The next sampled line is
1857.8 / 2047 MiB, with 466 new meshes / 3.98 M triangles, 264 LRU evictions
and 1017 MiB AS ownership in that window. It still reports 60 / 60 split builds;
this event alone does not establish a dropped native-hair scene. A sampled
post-eviction count cannot identify the instantaneous allocation or largest hole.

Native capture uses 88-byte vertices. Optional mesh extraction previously ran
before mandatory procedural AABBs, and the combined mesh/AS budget was enforced
only by later LRU eviction; pending GPU ranges cannot be reclaimed immediately.
Bounds are now allocated first. Native-layout cache extraction admits its
vertices plus uncompressed BLAS only within the existing combined mesh budget,
including pending ownership; rejected cache work retains the captured dynamic
BLAS. No pool cap, GPU retirement fence or material coverage changes. Synthetic
admission tests cover the recorded load scale, exact budget, signed pool cap and
overflow rejection. Actual Dialogue3 peak, scene continuity and GPU cost need a
live confirmation; no game launch was performed for this repair.
