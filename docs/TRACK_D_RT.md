# Track D: hardware ray tracing

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
3. **Static/dynamic split** (section 5, step 3). **Done, see [section 11](#11-static-dynamic-bvh-split-instanced-object-space-blas-rt_bvh_split1).**
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
- `rt`, `rt_build`, `rt_refit`, `rt_rebuild_frames`, `rt_fast_trace`, `rt_xfb_mb`,
  `rt_bda_core`;
- `rt_shadows`, `rt_strength`, `rt_bias`, `rt_tmax`;
- `rt_ao`, `rt_ao_rays`, `rt_ao_radius`;
- `rt_debug`, `rt_timing_frames`.

The log reports `[rt] scene build GPU …` (convert, BLAS, TLAS per rebuild/refit),
`[rt] last N frames: … rebuilds, … refits` and `[core] rt passes: GPU cost …`.

## 7. Phase 3 (2026-10-26): alpha-tested geometry and hybrid RT GI — PAUSED, WIP

Status: both parts work in game behind switches that are **off by default**
(`rt_alpha=0`, `gi_rt=0`). Paused on request before the final write-up; the
numbers below are from the runs done (Steel Watch Foundry, static camera,
RTX 5090, 2560x1067, GPU timestamps, 300-frame medians/averages).

### 7.1 Alpha test (`rt_alpha=1`, restart; `rt_alpha_test` is a live A/B switch)

- **Where the alpha test lives.** BG3's G-buffer FS never discard; the depth
  prepass does (G-buffer then depth-tests against it). So with `rt_alpha=1` the
  scene is captured from the **depth prepass** (depth-only D24S8 at render
  resolution; its small 5-draw sibling pass is learned and skipped), XFB stride
  24: `gl_Position` + the VS's lowest-location vec2 output (the UV in every
  prepass VS). `tools/xfb_patch_test <dump> <out> 24`: 2385/2385 VS patch and
  pass `spirv-val`.
- **Which draws alpha test, and with what.** `scripts/alpha_table.py` decompiles
  every depth-only discarding FS in the shader dump (1243) and writes
  `shaders/rt/alpha_fs.txt` (372 shaders):
  - `vt` (252): BG3's virtual texturing (Granite). Alpha = `.w` of the albedo
    tile, through an indirection texture (`texelFetch`, page bits in `.x`) into
    the tile cache array; per-draw UV transform + tile-set index in the material
    UBO (set 1 b0), tile-set constants (2 row-major mat4) in SSBO set 0 b12.
    `rt_common.hlsli` `vt_alpha()` replicates the lookup; mip from a ray cone.
  - `tex` (120): a plain texture's `.w`/`.x` (optional UBO uv scale). Only
    where our test is conservative: condition reads the sample directly; blends
    (`mix`) and branch-selected alpha are rejected (they let rays through solid
    pixels: 0.30% "behind" before this rule).
- **Per draw** (`layer/alphatest.cpp`): UBO/SSBO constants read from the game's
  mapped memory at the draw; views recorded; after the pass the records go to a
  buffer and the views into one of 8 ring descriptor sets (set 1 of the core's
  RT passes: records, UV soup, Texture2D[256], Texture2DArray[4], 2 samplers).
  Only views bound that frame. Alpha draws go to BLAS geometry 1 (not opaque);
  ray queries loop `Proceed()` and commit non-opaque candidates that pass.
- **Primary-ray check** (`rt_debug=3/5`, `rt_stats=1`, same view, live A/B):

  | | alpha test off | on |
  |---|---|---|
  | match | 98.12% | **99.09%** |
  | in front | 1.88% (vt cutouts 1.13, tex 0.01, opaque 0.73) | **0.91%** (vt 0.17, opaque 0.73) |
  | behind | 0.002% | 0.007% |

  The remaining 0.73% "opaque" is screen-door dithered geometry (camera
  cut-away / LOD fades, [rt_alpha_primary_check.png](img/rt_alpha_primary_check.png),
  purple) — correctly solid for secondary rays — plus unrecognised cutouts.
- **Cost**: shadows + AO 0.201 → 0.22 ms with the alpha test; BLAS: 2.23M opaque
  + 0.83–0.93M alpha triangles, refit 0.88 ms (convert 0.40 at stride 24),
  rebuild 3.4 ms; XFB buffer 384 MB, alpha region 64 MB.

### 7.1b Alpha test at secondary hits: grass fix (2026-10-26)

**Symptom.** Campsite: black, sharp-edged polygons on the ground under grass
(GI visibility 0; gone only with `gi=0`), the same under a twig in reflections,
and sun RT shadows of grass shaped like the card, not the blades.

**Root cause.** The grass and ground-cover family (`73849471`, 148 prepass draws
in Campsite; also `2f1b0ffd`, `b2d72ea7` and 21 other shaders, 24 in all) was
**not in `alpha_fs.txt`**, so its draws went into the TLAS as opaque geometry.
Its Granite alpha test keeps the selected sample in a `vec4` and reads `.w` from
it in the next statement (`_225 = mix(textureGrad(...), vec4(1,0,0,1), bvec4(... != 2202.0)); _226 = _225.w`);
`alpha_table.py` only recognised the inline `...2202.0)).w`. Flat cards lying
on the ground then blocked every GI ray from the pixels under them at t ~ 0.
Evidence (same view, `rt_debug=5`): the patches are "in front, opaque
geometry" (purple), not alpha-tested hits; `rt_debug=4` shows visibility 0 in
the same polygons; with `rt_stats=1` the alpha path itself returned solid only
by evaluation (no record 0%, no UV 0%, prim out of range 0% in steady state,
tile set not resident 0%, page from a coarser level ~1%), so none of the
fallbacks was the cause. Alpha-tested draws per frame went 108 → 412 once the
family was recognised. This is in the TLAS build, so it fixed every ray type
(GI, AO, sun and local shadows, reflections) at once.

**Second table bug (fixed, not the Campsite cause).** 76 vt shaders discard
twice: an early-out `alpha * mask < 0.001` (or 0.01), then
`fade * alpha * mask < 0.5`. The table took the first test's cut-off, so every
texel with any coverage was solid (whole cards solid at a filtered mip).
`effective_cut()` now takes the strictest constant cut-off of the tests that
keep the alpha as a factor (through `mix` only when both operands carry it,
`bvec` selects of the Granite sample allowed; UBO cut-offs and
`gl_FragCoord` screen-door dithers skipped). Line format: `cut=<effective>
cut0=<first>`; `rt_alpha_cut=0` (live) restores the first.

[img/alpha_campsite_crop0.png](img/alpha_campsite_crop0.png) (before | after | diff x4, render-res compose),
[img/alpha_campsite_crop1.png](img/alpha_campsite_crop1.png) (sun shadow of a twig clump: a rectangle before, the twigs after),
[img/alpha_campsite_vis_crop0.png](img/alpha_campsite_vis_crop0.png) (`rt_debug=4`),
[img/alpha_campsite_primary_crop0.png](img/alpha_campsite_primary_crop0.png) (`rt_debug=5`: purple opaque-in-front gone).

**Evaluation changes in `rt_common.hlsli`** (live switches, all on by default):
- `rt_alpha_stochastic=1`: the mip comes from the ray cone with the game's own
  Granite formula; the indirection entry already points at the finest resident
  ancestor of a missing page (its tile scale `e & 15` is smaller), and the read
  reports that level. Where the read is blurred (level > 1 above mip 0), the
  hard cut-off blends over one level into a hashed threshold (pcg3d of the hit's
  uv bits, the triangle and the frame), so P(solid) tends to the filtered alpha,
  i.e. the coverage: partial coverage of distant grass is unbiased under RR
  instead of all-solid or all-clear. Levels 0-1 keep the game's exact cut-off.
- `rt_alpha_unknown=1`: a candidate whose alpha cannot be read (no record, no
  UV, prim out of range, non-finite, non-resident tile set) lets visibility rays
  (GI, AO, reflections) through and stops shadow rays. Evidence: these cases
  are 0% in steady state and 0.6-2% of visibility candidates for a frame after
  a capture change (prim >= tris: an InstInfo/BLAS mismatch, not
  investigated further); a false "solid" there gives a hard black polygon in
  GI (the artefact class above), a false "clear" in a shadow gives a light
  leak, which is the worse error for shadows.
- `rt_stats=1` logs per ray class (`alpha candidates of visibility/shadow
  rays`): each fallback, vt/tex solid/clear, records with a loose first
  cut-off, blur level >= 2 / >= 4, stochastic share, pages from a coarser level.

**Cost** (Campsite, 2560x1067, 300-frame averages, shared GPU, noise ~0.2 ms):
4 x more alpha-tested draws (1.85M alpha triangles), visibility candidates
2.0M → 7.1M/frame; GI 2.95-3.0 → 3.0-3.4 ms, RT passes 1.35-1.40 → 1.40-1.53 ms.

**Not done.** 20 vt shaders keep a loose first cut-off (their 0.5 test reads
the alpha through `mix(1.0, alpha, ubo)` or a UBO cut-off; reading the UBO at
the draw would settle them). Blended-texture `tex` shaders (`42507cb4`) stay opaque.

### 7.2 Hybrid RT GI (`gi_rt=1`, live; see TRACK_C_GI.md)

`shaders/gi/gi_trace_rt.hlsl` replaces `gi_trace` (+`rt_ao`): same samples and
outputs, `gi_rays` alpha-tested ray queries to `gi_rt_radius` (1 m). Hit on
screen with matching depth: last frame's radiance; otherwise occlusion only
(`gi_rt_fallback=0`). Hits: 18% of 2.73M rays/frame; 86% shaded from the
screen, 11.7% hidden, 2.2% off screen.

| trace pass, 4 rays/sample, half res | GPU |
|---|---|
| screen-space (16 steps) | 0.248 ms |
| ray-traced, alpha test on / off | **0.175** / 0.124 ms |
| GI total (pre + post) ss + rt_ao → gi_rt | 0.44 → 0.35 ms |

Images: [gi_ss_visibility.png](img/gi_ss_visibility.png) vs
[gi_rt_visibility.png](img/gi_rt_visibility.png) (`rt_debug=4`; mean
visibility 0.74 vs 0.80: no screen-space halos at depth edges, crisper contact
occlusion), irradiance [gi_ss_irradiance.png](img/gi_ss_irradiance.png) vs
[gi_rt_irradiance.png](img/gi_rt_irradiance.png) (`gi_debug=1`).

### 7.3 Validation

`-Validation` with `rt=1 rt_alpha=0 gi_rt=0` vs `rt=1 rt_alpha=1 gi_rt=1`
(toggling rt_ao, rt_debug=5, rt_stats, rt_alpha_test live): identical message
IDs (game's own 09600, 10788, StorageImage-FormatMismatch, OutputNotWritten,
CopyBuffer-00118, 08737, headless swapchain); none from our work.

### 7.4 Not done / next steps

- Not verified: camera motion (`-Move`) with `rt_alpha=1` (rebuild path, prepass
  ordinal relearning), outdoor/foliage scenes, `rt_alpha` + `fg>=2`.
- `rt_stats=1` costs ~2 ms (atomics); measurement only.
- 781cd8d1-style cutouts (alpha chosen by a vertex-colour branch) and blended
  `tex` shaders stay opaque; would need a second captured varying.
- Off-screen hit radiance: a world-space radiance cache instead of fallback 0.
- Opacity micromaps would remove most candidate shader work.
- Decide default-on for `rt_alpha`/`gi_rt` after the motion run; update ROADMAP.

## 8. Effect catalogue and roadmap (analysis, 2026-10-26)

A feasibility study of further ray/path-traced effects. It was done without running
the game, from the decompiled shaders of `captures/20261025-235042` (mainly the
tiled lighting `f469db78`), the report of that capture, and the code of
Tracks A, C and D. The plans in sections 5-7 stay as they are. This section only adds
effects that were not already planned, plus the enablers they share. The quality
audit of the existing effects, and the rules every new effect must follow, are in
[QUALITY.md](QUALITY.md).

### 8.1 What the game gives us (data inventory)

Everything below is in the **lighting space**, which is camera-relative world
(`P = ray(uv) × linear depth`, the same space as the TLAS; see 6.4). Absolute world =
`P + _m6` of the camera UBO (set 0 b2). Light positions in the light array are in
this space too: the lighting computes `light.m0.xyz − P`.

| Data | Where | Have it? | Notes |
|---|---|---|---|
| Position, view-space normal, base colour, roughness, F0 (`0.16·spec²`/metal), metalness, light-channel bits (RT2.w bits 4-6) | G-buffer RT0-RT2, linear depth; bound to the tiled lighting at set 1 b1/b2/b3/b5 | **yes** (core snapshot) | Primary hit only. RT3 is almost empty. |
| **Local lights** | lighting set 1: b23 cluster headers (ivec4: x offset; y bits 0-11 points, 12-23 spots, 24-31 env probes; z bits 0-9 projector lights), b24 index list, **b25 light array** (struct `_50`) | **readable** (same `query_binding` route as the camera UBO; not done yet) | `_50`: m0.xyz position, m0.w = 1/radius; m1.xyz colour, m1.w spot cos-outer; m2.xyz spot direction, m2.w cos-inner; m3 < 0 → × global scale `b13[0]`; m4 falloff exponent; m5 falloff model/bias (sign selects the model); m6 light-channel mask; m7 bit 0 = casts a shadow, m7>>2 = shadow-atlas slot; m8 shadow strength. Clusters are froxels (log depth), so they only index lights for points inside the view frustum. |
| Projector (textured) lights | b29 (struct `_53`, row-major mat4x3 projection, cookie index into b45 `texture2D[]`) | readable | Third light loop in `f469db78`. |
| Local-light shadows | b40 (per-face mat4 + atlas rect + bias), b41 atlas | readable | Point lights: 4-face **tetrahedral** maps. 12-tap Vogel-disc PCF rotated by interleaved gradient noise (`fract(52.98·fract(0.0671·x + 0.00584·y))`), faded by camera distance (`b39 _m34`) and by m8. Lights without m7 bit 0 have **no shadow at all**. |
| Sun | camera CB set 0 b1: `_m0` direction (= `cam.v0`), `_m2` colour, or a transmittance-LUT colour when the atmosphere is on; × `b13[0]` | **yes** | **Angular radius:** the sun specular uses a representative-point disc with `sin θ = 0.0045901`, `cos θ = 0.9999894` (θ = 0.263°, the real sun). Shadow = mask b7 (ours to fold into) × two cloud-shadow textures. |
| Ambient | global sky cubemaps b34-b37 (irradiance + prefiltered spec, two sets blended by `b39 _m29`), local parallax-corrected box/sphere env probes (b33 list + b45 cube array), DFG LUT b38, × `b13[0]·100` | readable | Diffuse ambient = irradiance · diffuse colour · DFG.z · AO. Specular ambient = prefiltered probe · (F0·A + B) · Lagarde specular occlusion(AO, N·V, roughness). **There is no SSR in BG3.** All indirect specular comes from probes. |
| AO | b6 RGB10A2 `.x` = the game's **half-res SSAO** (units 88-91: `0effa9ff` → `af81b62d` deinterleave → `a6bb35c6` blur) | **yes** (we fold into it) | The lighting also multiplies **direct** light (local lights and sun) by `mix(1, AO, b0._m0.y)` (diffuse) and `mix(1, specOcc, b0._m0.y)` (specular). That value has not been read yet. |
| BRDF | `f469db78` | **known exactly** | GGX D, height-correlated Smith (the approximate form), Schlick F; renormalised Burley diffuse × `mix(1, 0.662, r)`; roughness ≥ 0.09, plus specular AA from the quad normal variance (`sqrt(r² + min(var, 0.18))`). |
| Atmosphere | inside the lighting: aerial-perspective transmittance multiplies both outputs, and the in-scatter is added to diffuse (b10) | known | So the lit diffuse buffer contains in-scatter. Negligible at gameplay distances (the planet is scaled, R ≈ 636 km), but not zero. |
| Emissive / sky / FX | a B10G11R11 render-resolution target written **before** the lighting (units 78-85: sky `c9f013fc`, half-res clouds, projected FX) and added in compose `5c3fdabc`: `(diffuse_after_SSS + specular + this) · fogT + fogInscatter` | not captured | Not in our `prevRadiance` (QUALITY Q8). Where emissive *materials* go is unverified: RT3 is near zero, so either this target or the forward pass. |
| SSS | units 121/122 (`6f535075`, `d1669194`), profiles in an SSBO (set 2 b15); they read our diffuse buffer | yes (we inject before it) | Screen-space blur, two passes. |
| Fog / volumetrics | froxel grid 285x119xZ: `c3e212ad` density, `ca8adce7` scattering (sun cascades via samplerShadow + local lights), `8a4e0720` integration; tricubic lookup in compose | known | Volumetric sun shafts already exist (from the cascades). |
| Transparents | forward pass unit 125 (46 draws, water among them), after a colour copy for refraction (unit 124) | **not in the TLAS** | Would need XFB on the forward pass. |
| Off-screen geometry | — | **no** | The TLAS holds what the camera's G-buffer (or prepass) drew: frustum-culled, **roofs cut away indoors**, no forward geometry. The shadow cascades contain more (roofs, casters around the view): 6.7 item 2. |
| Materials at secondary hits | — | **partly feasible** | `vt_alpha()` already samples the Granite tile set for alpha. The same lookup in the albedo layer gives the hit's **base colour** for the 252 VT shader families. Normal/physical layers and plain-texture materials would need more decoding. |

### 8.2 Composition: how a new term enters without double counting

Every effect must name the game term it replaces and use one of three patterns:

1. **Fold** through a game input whose meaning is verified. Example: visibility into AO
   `.x`, or into the sun mask b7. Use *replace* semantics when the new signal contains
   the old one (RT AO contains SSAO's occluders). Use *multiply* only when the two are
   independent (RT on-screen casters vs off-screen cascade casters).
2. **Exact delta.** Re-evaluate the game's term `T_game` from the decompiled code with
   the same bound resources, then write `out += T_ours − T_game` into b10/b11 at
   the post-lighting hook (where `gi_composite` runs, before SSS). Since `T_game` is
   computed exactly, the result is `out − T_game + T_ours ≥ 0` whenever
   `T_ours ≥ 0`, with no clamping bias. This works for per-light shadow terms, the
   specular probe term and the diffuse ambient term.
   - Limitation: the aerial-perspective transmittance is not replicated, so
     deltas fade back to the game's term beyond about 100 m.
3. **Lighting-shader patch** (enabler D-EN5): a SPIR-V patch of the `f469db78` family
   that multiplies or replaces specific terms by values from our images. It is the
   cheapest at run time, but the most fragile.

| Game term | Replaced by | Pattern |
|---|---|---|
| SSAO (b6 `.x`) | RT ambient visibility | fold, **replace** (today: multiply, see QUALITY Q2) |
| Sun mask (b7) | RT sun visibility (hard, then soft) | fold; multiply until cascade casters are captured, then replace within TLAS coverage |
| Per-light atlas shadow, or no shadow | RT per-light visibility (D-LS) | exact delta (v1), patch (v2) |
| Specular ambient (probes) | RT reflection (D-RF) | exact delta (v1), patch (v2) |
| Diffuse ambient (probes · AO) | RT/cached irradiance (D-GI2) | fold (today's hybrid) → exact delta |
| Emissive/FX target | (source of bounce only) | read-only (D-EM) |

### 8.3 Feasibility of the candidates (summary)

Costs are for the RTX 5090 at 2560x1067 (2.73 M pixels), scaled from what was measured:
- sun shadows: 2.73 M rays, 0.08-0.11 ms;
- hybrid GI: 2.73 M alpha-tested rays of 1 m, 0.175 ms.

That works out to about **0.04-0.07 ms per million short rays**. For long closest-hit
rays, assume 2-3× that.

| Candidate | Data needed → have? | Injection | Gain | Cost | Risk | Verdict |
|---|---|---|---|---|---|---|
| Soft sun shadows | sun dir + **angular radius 0.00459 rad** (found) → yes | sun mask b7 (as now) | medium: penumbrae grow with caster distance (~0.9% of the distance), mostly for buildings and trees | 0.10-0.15 ms + denoise (RR: 0) | the fold only darkens, so penumbrae appear only where the cascades are lit | **already planned** (6.7 item 5); refined in 8.5 |
| RT shadows for local point/spot lights | light array b25 (pos, 1/r, colour, cone, flags), clusters b23/b24, atlas b40/b41 → **readable** | exact delta at post-lighting | **high**: BG3 has many unshadowed fill and torch lights that leak through walls and floors, and its shadowed lights use a low-res tetrahedral atlas | 0.3-0.6 ms (stochastic 1-2 rays/px + RR) | art intent: some fill lights are meant to leak, so shadowing is per-light and opt-in | **new: D-LS** |
| ReSTIR DI / RTXDI | as D-LS; RTXDI SDK **not** in `external/` | sampler inside D-LS | medium: BG3 has tens of lights per view, not thousands. Worth it only with emissive triangles. | +0.2 ms over D-LS | RR wants permuted/randomised reuse | **new: D-DI** (after D-LS) |
| RT reflections (glossy and mirror) | roughness/F0/metal decode → yes; hit radiance: on-screen → yes (reprojected); off-screen → needs EN1 + EN2 (+EN3) | exact delta on the specular ambient (probes) | **very high**: there is no SSR in BG3; wet stone, polished floors and metal show only probe reflections | 0.5-1.0 ms | the TLAS lacks geometry behind the camera and above the screen (culled), so the probe is the fallback; RR needs specular hit distance and matrices | **new: D-RF** |
| Multi-bounce GI + radiance cache | TLAS, hit albedo (EN2), lights at hits (EN1), sky (b34) → mostly | today's hybrid (fold) → delta on the diffuse ambient | medium-high: the GI reach grows from 1 m to the room scale, with colour bleeding | 0.4-0.6 ms, 160 MiB (SHaRC) | **roofs are cut away indoors**: a TLAS miss is not sky, so misses must keep the game's probe; that needs cascade casters for closed rooms | radiance cache **planned** (C/D "next"); the multi-bounce, screen-seeded SHaRC design is **new: D-GI2** |
| Emissive surfaces as light | on-screen: the emissive/FX target (8.1) → capturable; as RTXDI lights: emissive triangles + radiance → **no** | bounce source (`prevRadiance`) | low-medium (glyphs, lava, braziers; flames are particles with their own point lights) | ~0.02 ms | where emissive materials go is unverified | **new: D-EM** (part a now, b deferred) |
| Translucency / SSS | SSS pixel mask (stencil/flags, unverified), TLAS for thickness → partly | delta into diffuse before the SSS blur | low-medium (back-lit ears, leaves, cloth) | 0.05-0.1 ms | finding the SSS/foliage classification | **new: D-TR** (low) |
| RT refraction (glass, water) | refractive surfaces are forward-drawn and not in the TLAS; the game's refraction samples the unit-124 copy | patch the forward FS families | low (BG3's water is stylised; screen-space refraction rarely fails at the isometric angle) | 0.3 ms + XFB on the forward pass | high: shader patching of forward materials; RR resampling caveats | **new: D-RFR**, deferred |
| RT for transparents/particles | forward pass not captured | — | low | — | — | **not recommended**. Do RR transparency guides instead (QUALITY Q12) |
| Volumetric light shafts with RT visibility | froxel grid known; sun-view geometry needs cascade capture | patch `ca8adce7`'s cascade lookup, or replicate it | low-medium: the game's froxels already sample the cascades; RT adds only on-screen detail | ~0.1 ms (1 ray per froxel) | patching a 1227-line compute shader | **new: D-VOL**, low |
| Full path tracing replacing the lighting | geometry incomplete (culled, roofs cut away, no forward), secondary materials approximate, light list view-culled, baked probe look, unshadowed art lights | replace b10/b11 | — | 5-8 ms | **infeasible as a product**: it cannot reproduce what the game does not draw | **new: D-PT**, a reference/debug mode only |

### 8.4 Enablers (shared by several effects)

**D-EN1 Light-list snapshot.**
- Add set 1 b13, b23, b24, b25, b29, b39 (the lighting constants), b40 and b41 to the
  core's `Snapshot` at the tiled-lighting dispatch, and bind them to our passes
  read-only.
- Write `shaders/rt/bg3_lights.hlsli`: the struct decode, the cluster walk (same
  froxel formula as `f469db78`: `log2(depth·far)·_m21 + _m22`), the exact
  unshadowed contribution (falloff models, spot cone, channel mask, ×`b13[0]`, AO
  factor `mix(1, AO, b0._m0.y)`), and the atlas PCF including its IGN rotation.
- Validation: `light_debug=1` recomputes Σ of all lights and compares it with the
  game's b10/b11 minus ambient and sun (by toggling the ambient terms via debug
  modes), so the mean error must be < 1%.
- Log statistics: number of lights, number shadowed, lights per pixel.
- Also read `b0._m0.y` (the AO-on-direct factor) and log it.
- Cost: 0.

**D-EN2 Hit material.**
- Capture the UV for **all** draws (stride 24, as `rt_alpha=1` does for the prepass)
  and keep a per-draw material record for every VT draw, not only alpha-tested
  ones: tile-set constants, UV transform, albedo layer.
- `hit_base_colour(prim, bary, cone)` = the `vt_alpha()` lookup returning `.rgb`.
- Plain-texture draws, and draws not recognised, get a per-draw average colour
  (sampled once from the G-buffer where the draw is visible) or 0.5 grey.
- Roughness/metal at hits: per-draw constants from the G-buffer median.
- Normal: geometric, from the triangle.

**D-EN3 Geometry retention and closure.**
- The static/dynamic split (6.7 item 3) plus **retention**: a static instance
  stays in the TLAS for `rt_retain_frames` (e.g. 300) after the game stops
  drawing it (culled, behind the camera).
- Together with cascade-caster capture (6.7 item 2, which also brings the roofs the
  camera cut-away hides), this gives off-screen geometry for reflections and GI,
  and closed rooms.
- Instance masks: bit 0 G-buffer, 1 cascade-only, 2 retained, 3 dynamic. Each
  effect chooses its masks. For example, the sun uses all of them; reflections
  exclude cascade-only geometry if it is missing faces that face the camera.

**D-EN4 Exact-delta library.**
- Replicate the game's specular ambient (global and local probe loop, DFG,
  Lagarde specular occlusion) and diffuse ambient from `f469db78` lines
  ~1210-1450, in `shaders/rt/bg3_ambient.hlsli`.
- Validate it against b10/b11 the same way as EN1.

**D-EN5 Lighting-shader patch hooks (v2 of the deltas).**
- A SPIR-V pass over the ~15 `f469db78` permutations that adds
  `ourFactor(pixel, term)` reads at three sites:
  - the per-light shadow factor (`_802`/`_1087`/`_1427`);
  - the specular ambient `_1684`;
  - the diffuse ambient `_1687`.
- This removes the ALU of re-evaluating the lights and makes the composition exact,
  including the transmittance.
- Do it only after the deltas are validated, as an optimisation.

### 8.5 Refinements to already-planned items

- **Soft sun shadows (6.7 item 5).**
  - Sample a cone of half-angle `rt_sun_radius` (default **0.00459 rad**, the game's
    own value) with uniform disc samples, 1 ray per pixel at full resolution.
  - With `rr=1`: per-pixel white noise from a proper hash (no checkerboard), fed to
    the mask raw; RR denoises.
  - With `rr=0`: NRD SIGMA (in `external/NRD`) from the occluder distance, or a
    penumbra-sized bilateral blur.
  - Keep the multiply fold until cascade casters exist, then replace within TLAS
    coverage (`mask = vis_rt` where the cascades' casters are all in the TLAS).
  - An artistic `rt_sun_scale` multiplier, since BG3's PCF cascades are softer than
    the real sun.
  - Cost 0.10-0.15 ms.
- **Cascade-caster capture (6.7 item 2).**
  - It is now also the prerequisite for closed interiors in GI and for volumetric
    shafts.
  - Capture the largest cascade first: one more XFB pass of about +0.3 ms, and
    duplicates with the G-buffer soup are harmless for any-hit rays.
- **Radiance cache (TRACK_C next, 7.4).** D-GI2 below fixes the design.

### 8.6 New effects

Each entry gives the ID, the data needed, the injection point, the cost, the priority
and the steps. Priority: P1 next, P2 after P1, P3 later, P4 not recommended.

#### D-LS: ray-traced local-light shadows (P1)
- **Status (2026-10-26): implemented, on by default (`rt_local_shadows=1`).** Results, choices
  and numbers in [section 9](#9-d-ls-ray-traced-local-light-shadows-implemented). The plan below is kept as written.
- **Data:** EN1 (lights, clusters, atlas), the G-buffer, the TLAS (+ alpha).
- **Injection:** the post-lighting hook. Per pixel, the exact delta
  `Σ_i c_i·(v_i − s_i)`:
  - `c_i` is the unshadowed contribution (diffuse to b10, specular to b11);
  - `s_i` is the game's own shadow term (atlas PCF × fade × m8, or 1 when the
    light casts no shadow);
  - `v_i` is the ray-traced visibility.
- **Which lights:** `ls_mode=1` shadowed lights only (the game's intent, better
  shadows); `ls_mode=2` also unshadowed lights with radius > `ls_min_radius`
  (stops leaks); `ls_exclude_channel` for art lights.
- **Sampling:** v1 is deterministic, 1 ray to the light centre per light (hard).
  v2 samples the sphere by solid angle, using the radius from `_m5` if it encodes a
  source size (to verify), with 1-2 rays per pixel chosen by RIS over the cluster's
  lights weighted by `c_i`. The estimate is `Σ c_i s_i` (exact, subtracted) plus
  `c_j v_j / p_j` (stochastic, ≥ 0). RR denoises; with `rr=0`, SIGMA.
- **Cost:** v1 about 0.05 ms per light-per-pixel on average (measure the light
  count first); v2 0.25-0.4 ms.
- **Steps:**
  1. EN1, with its validation (Σ of lights matches the game's buffers).
  2. Debug view: per-pixel light count, and shadowed vs unshadowed lights.
  3. v1 deterministic, for shadowed lights only. A/B against the atlas in a
     torch-lit interior.
  4. `ls_mode=2`. A/B for light leaks under floors and through walls.
  5. v2 stochastic plus RR.
  6. Validation, motion run, cost table. Then default-on decision.

#### D-DI: ReSTIR DI over the local lights (P3)
- **Data:** as D-LS, plus emissive triangles (D-EM b) if they ever exist.
- **Injection:** the sampler inside D-LS v2; the composition is unchanged.
- **Cost:** +0.15-0.25 ms (temporal and spatial reuse, 1 shadow ray).
- **Steps:**
  1. Fetch RTXDI (not in `external/`).
  2. Initial RIS over the cluster lights.
  3. Temporal reuse with randomised neighbours, then spatial reuse with permutation
     sampling (the RR guide's rule, QUALITY R1).
  4. Visibility reuse.
  5. Compare its variance with D-LS v2 at equal cost. Keep it only if it wins in
     scenes with more than 8 lights per pixel.

#### D-RF: ray-traced reflections (P1)
- **Status (2026-10-26): tiers (a) + (c) implemented with D-EN4 and the RR hit distance.**
  Results in [section 10](#10-d-rf-ray-traced-reflections-implemented-tiers-a-and-c). The plan below is kept as written.
- **Data:**
  - G-buffer roughness (after the game's specular AA: `sqrt(r² + min(var, 0.18))`,
    then `r ≥ 0.09`), F0 and metalness.
  - Hit radiance in three tiers:
    - (a) on screen with matching depth: `prevRadiance`, reprojected (QUALITY Q1);
    - (b) off screen but in the TLAS: EN2 albedo × (sun with an RT shadow ray +
      EN1 lights + sky irradiance b34) + emissive 0;
    - (c) miss, or a cascade-only hit: the game's probe.
- **Injection:** exact delta on the specular ambient (EN4):
  `b11 += (L_rt · DFG(F0, NoV, r) · SO − probeSpec_game)`.
  It is blended toward the probe with a confidence weight: tier (a) 1, tier (b)
  `rf_offscreen_w`, tier (c) 0. The weight also fades to 0 at roughness
  `rf_max_rough` (0.6).
- **Sampling:**
  - 1 ray per pixel, GGX bounded-VNDF sampling, for pixels with r < `rf_max_rough`;
  - mirror (r < 0.1) gets a single deterministic ray;
  - RR gets `DLSSD.SpecularHitDistance` (FP16_MAX for misses) plus the
    world-to-view and view-to-clip matrices (QUALITY Q11).
  - With `rr=0`: NRD REBLUR specular.
- **Cost:**
  - about 1.4 M rays (≈ 50% of pixels) at 0.15-0.3 ms;
  - tier (b) shading 0.2-0.4 ms;
  - EN4 0.1 ms;
  - total **0.5-1.0 ms**.
- **Steps:**
  1. QUALITY Q11, the RR world-space normals and matrices.
  2. EN4, with its validation.
  3. Tiers (a) and (c) only: on-screen reflections with probe fallback. This is
     already "SSR quality with RT accuracy". Debug view: tier per pixel.
  4. Hit-distance output to RR. A/B on a wet or polished floor scene and on metal.
  5. EN2 + EN1, then tier (b). Measure the tier fractions with `rt_stats`.
  6. EN3 retention, which shrinks tier (c).
  7. Then specular occlusion from the same rays replaces Lagarde's for those pixels.

#### D-GI2: multi-bounce GI with a screen-seeded SHaRC cache (P2)
- **Data:** TLAS, EN2, EN1, the sky b34, absolute world (`+ _m6`) for hashing.
  `external/SHARC` (needs `-enable-16bit-types` for DXC).
- **Design (screen-seeded):**
  - **Update** pass: about 1/16 of pixels trace paths with 2-3 bounces. At each hit:
    if it is on screen with matching depth, its radiance is the game's own lit
    diffuse (exact, with all the game's lights and baked probes) and the path
    terminates; otherwise it is shaded as D-RF tier (b) and the path continues.
  - **Miss** rule: a miss *indoors* (or anywhere until EN3 closure) returns the
    game's diffuse probe irradiance at the last vertex, never the open sky. Roofs
    are cut away.
  - **Resolve:** the SHaRC accumulation, with a 32-64 frame cap and stale eviction.
  - **Query:** the existing GI rays (radius raised from 1 m to `gi_rt_radius` 8-30 m)
    read the cache at off-screen hits instead of fallback 0.
- **Injection:** unchanged (AO fold + bounce add), until EN4 allows an exact delta on
  the diffuse ambient.
- **Cost:** update 0.1-0.2 ms, resolve 0.05 ms, longer query rays +0.2 ms;
  **0.4-0.6 ms**, 160 MiB.
- **Steps:**
  1. EN2, EN1.
  2. SHaRC update/resolve on our own passes; hash-grid debug view.
  3. Query in `gi_trace_rt` for hidden and off-screen hits (replaces
     `gi_rt_fallback`).
  4. Raise the radius; A/B for colour bleeding and interior leaks.
  5. EN3 closure, then misses use the sky irradiance outdoors only.

#### D-EM: emissive surfaces (a: P1-small, b: P4)
- **(a) Emissive in the bounce.**
  - Find the pre-lighting additive target: B10G11R11 + D24S8 at render resolution,
    the passes before the tiled lighting (units 78-85), and the third colour input
    of compose `5c3fdabc`.
  - Add it to `prevRadiance` (it is available during the lighting).
  - Verify which materials write there (glowing glyphs? lava? or only sky/FX), and
    exclude the sky (depth = 0).
  - Cost 0.02 ms.
- **(b) Emissive triangles as lights** (RTXDI). This needs per-draw emissive
  detection and radiance. Not recommended until (a) shows that emissive
  surfaces are common.

#### D-TR: ray-traced thickness for translucency (P3)
- **Data:** a mask of SSS/foliage pixels (to find: G-buffer flag bits in RT2.w, the
  stencil, or the SSS profile index); TLAS.
- **Injection:** a delta into b10 before the SSS passes. A sun and light
  transmission term `c·albedo·exp(−σ·thickness)`, with the thickness from one
  ray along −N, inside the object.
- **Cost:** 0.05-0.1 ms.
- **Steps:**
  1. Find the mask.
  2. Thickness debug view.
  3. Back-lit leaves and ears A/B.
  4. Set the RR SSS guides (QUALITY Q12) first.

#### D-RFR: ray-traced refraction for glass and water (P4)
- **Data:** XFB on the forward pass (unit 125) for the refractive surfaces; their
  IOR and tint from the forward FS.
- **Injection:** patch the forward FS families' refraction sample (the unit-124
  copy).
- Deferred. RR's guide warns that refraction resampling breaks its sample
  independence.

#### D-VOL: RT sun visibility for the volumetrics (P3)
- **Data:** cascade-caster capture (6.7 item 2) for the sun-view geometry; the
  froxel grid of `ca8adce7`.
- **Injection:** a per-froxel sun visibility volume (1 ray per froxel), read by a
  patched `ca8adce7` in place of its cascade compare.
- **Cost:** about 0.1 ms.
- Worth it only if cascade-resolution aliasing in the shafts is visible in A/B.

#### D-PT: path-traced reference mode (P4, a tool, not an effect)
- A debug mode accumulating 1 path per pixel per frame over a static camera.
  - The primary hit comes from the G-buffer; secondary hits use EN2 materials,
    EN1 lights with RT shadows, and the sky.
- It writes to `rt_debug` only and replaces nothing.
- Purpose: a "how wrong is the hybrid" reference *within the captured geometry*,
  for the A/B metrics of QUALITY R6.
- Infeasible as a product for these reasons:
  - **Geometry:** culled and cut-away surfaces are not in any capture (even with
    EN3, interiors are closed only where cascades reach).
  - **Materials:** only the primary hit's material is exact.
  - **Lights:** the list is view-culled; emissive materials are unknown.
  - **Look:** the game's look depends on baked probes and deliberately unshadowed
    fill lights.
  - **Cost:** 5-8 ms with RR.

### 8.7 Ranking by value/effort

| Rank | Item | Value | Effort | Why |
|---|---|---|---|---|
| 0 | QUALITY P1 fixes (Q1, Q2, Q4, Q11) | high | low | they make every later effect correct and cheaper to denoise |
| 1 | Soft sun shadows (planned, 8.5) | medium | **low** | data complete (angular radius known), 0.1 ms, RR denoises |
| 2 | D-LS local-light shadows | **high** | medium | fixes BG3's most visible lighting flaw (leaks from unshadowed lights); all data readable |
| 3 | D-RF reflections, tiers (a)+(c) | **very high** | medium | BG3 has no SSR at all; the on-screen tier needs no new data |
| 4 | D-EM (a) emissive in the bounce | low-medium | **very low** | one target capture |
| 5 | Cascade-caster capture (planned) + EN3 retention | enabler | medium | closes interiors for GI, adds off-screen casters, shrinks D-RF tier (c) |
| 6 | D-RF tier (b) + EN2 | high | high | off-screen reflections |
| 7 | D-GI2 screen-seeded SHaRC | medium-high | high | room-scale GI; needs EN2 and closure to avoid leaks |
| 8 | D-DI ReSTIR DI | medium | medium | only after D-LS, and only with many lights |
| 9 | D-TR, D-VOL | low-medium | medium | niche |
| 10 | D-RFR, transparents/particles | low | high | forward-pass capture and shader patching |
| — | D-PT | reference only | medium | not a product |

## 9. D-LS: ray-traced local-light shadows (implemented)

Status (2026-10-26): **on by default** (`rt_local_shadows=1`, live; needs `rt=1`).
Code: `shaders/gi/rt_local_shadows.hlsl`, `shaders/rt/bg3_lights.hlsli` (the light list
and the game's per-light shading, D-EN1), `shaders/gi/rt_local_shadows_prep.hlsl`
(validation only), `core/gi.cpp` (snapshot, bindings, pass, timing, statistics).
Tools: `scripts/live_phases.py` (live setting phases during one auto-run),
`scripts/ab_compare.py` (A/B stills and crops from two captures).
Measured in the Steel Watch Foundry (the auto-run save), RTX 5090, 2560x1067.

### 9.1 The light list (D-EN1)

At the first tiled-lighting dispatch the core snapshots, with `query_binding`, the
lighting's own set 1 b0 (flags), b13 (global light scale), b23 (froxel headers),
b24 (index list), b25 (light array), b39 (lighting constants), b40 (shadow views),
b41 (shadow atlas) and set 0 b2 (the froxel depth scale), and re-binds them read-only
to the D-LS pass (push-descriptor bindings 21-29). Every offset comes from the SPIR-V
`Offset`/`ArrayStride` decorations of `f469db78` (listed in `bg3_lights.hlsli`).
If any of them is missing, the pass is skipped and the log says so once (R8).

| What | Value in the foundry |
|---|---|
| Light array b25 | 4096 slots of 80 bytes (327680 B); shadow views b40: 4096 x 96 B |
| Froxels b23 / index list b24 | 397440 B (24840 froxels) / 6.36 MB |
| Shadow atlas b41 | 8192x8192 R16_UNORM, SHADER_READ_ONLY |
| Lights listed in the pixel's froxel (points + spots) | **5.61 per pixel** |
| Lights the game actually evaluates (channel, range, cone, N·L > 0) | **2.31 per pixel**; 89% of pixels get at least one |
| ... of which have a game shadow map (`m7` bit 0) | **0.00**: in this scene no point/spot light casts a shadow |
| Projector lights listed (b29, third loop of `f469db78`) | 1.02 per pixel (not handled, see 9.7) |
| `set1/b0 m0.y`, the AO factor on direct light (QUALITY Q3) | **0.5** in gameplay (1.0 on the loading screen); `m0.x = m0.w = 1` |
| `b13[0]`, the global light scale (lights with `m3 < 0`) | 0.000407 |

So Q3 is answered: BG3 multiplies direct local and sun light by `mix(1, AO, 0.5)`.
Our AO fold (RT AO / RT GI visibility into `b6.x`) therefore also darkens direct light
by half its occlusion. That is the game's own formula, and it is kept.

### 9.2 What the pass does

Full resolution, after the lighting and **before** the GI composite (so next frame's
bounce source, `prevRadiance`, already contains the shadows), SSS and fog.

1. It rebuilds the surface exactly as `f469db78` does: `P = ray(uv)·depth`, the view
   normal rotated by `_m6` (not renormalised), F0 `mix(0.16·spec², base, metal)`, the
   diffuse colour, the light-channel bits, roughness `max(0.09, r)` with the game's
   specular AA (`sqrt(r² + min(var, 0.18))`; the 2x2 quad's normal differences are read
   from the neighbours instead of quad shuffles), N·V and the IGN of the atlas PCF.
2. It walks the pixel's froxel (`log2(depth·_m4)·_m21 + _m22`, tiles of `_m25`) through
   the point lights, then the spot lights, and recomputes each one's **unshadowed**
   diffuse and specular `c_i` with the game's terms: the range test, the
   `pow(sat(1 − x²), m4)` falloff with both bias models, the spot cone², `1/4π` (points)
   and `1/π` (spots), `b13[0]` for `m3 < 0`, GGX D, the approximate height-correlated
   Smith visibility, Schlick F, and renormalised Burley × `mix(1, 0.662, a)`.
3. For each light the game left **unshadowed** (`m7` bit 0 clear), it traces one ray
   (alpha-tested TLAS, `rt_occluded`) and adds the exact delta
   `out += c_i·(v_i − 1)·mix(1, AO, m0.y)` into b10 (diffuse) and b11 (specular, with the
   game's Lagarde specular occlusion in place of AO). Since `v_i ≤ 1`, a light can only
   lose what the game gave it. The result is clamped at 0 only against the
   transmittance, which the delta does not replicate (it is 1 indoors, see 9.3).
4. Non-finite guard on both writes (R5).

**Lights that have a game shadow map** (`ls_shadowed`, default 0 = *skip*): their
contribution is left exactly as the game computed it. Reason: the atlas is rendered
from the light and contains casters the TLAS does not have (culled walls, cut-away
roofs, everything off screen). Replacing it with the traced visibility could
*un*-shadow those, which is worse than the low-resolution penumbra it would fix.
- `ls_shadowed=1` replaces the atlas by the traced visibility (`c·(v − s)`, with `s`
  the game's own PCF term replicated exactly: tetrahedral face, 12-tap Vogel disc
  rotated by IGN, `textureGather` emulated with loads, camera-distance fade × `m8`).
- `ls_shadowed=2` takes `min(v, s)`.

Both are A/B options only. Neither could be judged here, because no light in the
foundry has a shadow map.

**Sampling (R1).**
- With RR (`rr=1`; `ls_soft=-1` follows it) each traced light gets one ray to a
  uniform point of its sphere by solid angle (radius `ls_source_radius`, 0.05 world
  units: BG3's lights have no source size, so this is an artistic value). White noise
  from `pcg3d(pixel, frame·1024 + light index)`. RR denoises the penumbra; nothing of
  ours filters it (R2).
- Without RR there is no denoiser, so the ray goes to the light centre (hard shadows,
  no noise).
- `ls_samples=1|2` instead traces 1-2 rays per pixel to lights chosen by RIS over the
  unshadowed luminance (unbiased; cheaper, noisier). It is an option, not the default,
  because 2.31 rays per pixel are affordable.

Rays start at `P + N·dist·rt_bias` and stop `ls_clear` (0.2) short of the light, so a
lamp's own housing does not shadow it completely:

| `ls_clear` | occluded light evaluations per pixel (of 2.31) |
|---|---|
| 0.05 | 1.00 |
| **0.2** (default) | **0.80** |
| 0.5 | 0.77 |

Most of the step from 0.05 to 0.2 is the fixture geometry around the light. Beyond
0.2 almost nothing changes, so the occlusion that remains is the scene's.

### 9.3 Validation of the recompute

`ls_debug=4` runs `rt_local_shadows_prep.hlsl` before the lighting. It sets the game's
AO to 0 (no ambient; direct light keeps `1 − m0.y` of itself, which the recompute
applies too) and its sun mask to 0. The game's diffuse + specular then hold only the
direct local lights, plus the terms the recompute does not model (projector lights,
the atmosphere's in-scatter). With `ls_stats=1` the core fits
`game = k·recomputed + r` per pixel, over the pixels whose froxel lists no projector
light (counters in `rtStats`, 120-frame windows):

| | AO 0 (`ls_debug=4`) |
|---|---|
| Fit over 1.20M lit pixels | **game = 1.07 × recomputed + 0.0134** |
| Mean recomputed / game | 0.0093 / 0.0234 |
| Game where no local light reaches (0.15M px) | 0.0181 |
| Recompute above the game (luminance) | 0.004% of the game's; 0.02% of pixels |

- The residual is an additive term of about the same size where no local light reaches
  at all (0.018) as under the lights (intercept 0.013).
- The recompute is never above the game's output. An error in a factor, the falloff or
  the BRDF would produce that somewhere.
- The slope of 1.07 is within what that spatially varying additive term can bias a
  least-squares fit by.
- The exact < 1% check of D-EN1 needs that residual removed too (the in-scatter
  replicated, or the projector lights added to the recompute). That is not done.
- `ls_debug=5` shows the recomputed light; `ls_debug=6` is the same check with AO = 1.

### 9.4 Results (A/B, same view, one run, live toggle)

`auto-run.ps1 -Traces 3` with `scripts/live_phases.py`: trace 1 on, trace 2 off (set live
right after trace 1), trace 3 `ls_debug=2` (the light removed, ×4); then `scripts/ab_compare.py`.

- [ls_foundry_off.png](img/ls_foundry_off.png) / [ls_foundry_on.png](img/ls_foundry_on.png):
  the presented frames (after RR, post and exposure), downscaled.
- [ls_foundry_removed.png](img/ls_foundry_removed.png): the light D-LS removes (×4,
  written over the lit diffuse, so it also passes through SSS and fog). This is where
  light previously leaked through geometry:
  - the floor behind the curved beam (right);
  - the beam's back face and the area above its lamp;
  - the walkway under the pipes and railings;
  - the machinery at the left;
  - the character's shadow wedge across the walkway, from the light above it.
- [ls_foundry_crops.png](img/ls_foundry_crops.png): three 1:1 crops of the 3840x1600
  frame, rows off / on / removed.

| Metric | Value |
|---|---|
| Light removed (unshadowed lights only) | **7.2% of the lit diffuse + specular luminance** (mean 0.0046 of 0.063) |
| Occluded evaluations | 0.80 of 2.31 per pixel (35%) |
| Mean luminance of the presented frame | −2.6% (exposure adapts partly); HDR compose output −2.3 to −2.7% |
| Non-finite values (every dumped image of all three traces) | **0** |

The render-resolution HDR images (compose, before RR) are not usable for a diff: the
per-frame jitter and the 1-spp noise of RR mode dominate it. The presented frames and
the debug view are the evidence.

Not done from R6:
- the docks, foliage and night-interior scenes. The `-Move` pan reached the docks once;
  there 0.07 lights are listed per pixel and none is evaluated, so the pass costs
  0.068 ms and changes nothing;
- FLIP against an accumulated reference;
- the temporal-stability metric.

### 9.5 Cost (GPU timestamps around the pass, medians of 300 frames)

| Configuration | Foundry, static |
|---|---|
| **Default: every unshadowed light, 1 ray each, soft (RR)** | **0.345 ms** (p10 0.342, p90 0.348) |
| Same, hard (`ls_soft=0`, the `rr=0` path) | 0.337 ms |
| RIS, 1 ray per pixel (`ls_samples=1`) | 0.219 ms |
| `-Validation`, default | 0.325-0.329 ms |
| Docks (no lights evaluated) / loading screen | 0.068 / 0.12 ms |
| `ls_stats=1` (wave-reduced atomics; measurement only) | 4-5 ms |

The budget (QUALITY R7) is 0.5 ms for local-light shadows: met. All rt passes together
(sun shadows + RT GI 0.26-0.28 ms, D-LS 0.35 ms) stay well inside the 2.5 ms Track D total.

### 9.6 Validation layer and robustness (R5, R8)

- `auto-run.ps1 -Validation`, `rt_local_shadows=0` against `1` with every mode toggled
  live in the lit foundry (`ls_stats`, debug views 1-6 including the validation
  pre-pass, `ls_samples=2` with `ls_shadowed=1`, `ls_shadowed=2`, live off and on):
  **identical message IDs**, all the game's own (`09600`, `10788`,
  `StorageImage-FormatMismatch`, `OutputNotWritten`, `CopyBuffer-00118`, `08737`, the
  headless `non-acquired-swapchain-image-used`). No core faults.

- The first on-run showed one new ID, `VUID-vkCmdDispatch-imageLayout-00344`. The
  validation pre-pass lived in the main shader, so the sun mask (binding 16) was
  statically used by the post pass while it was in SHADER_READ_ONLY. Fixed by moving
  the pre-pass into `rt_local_shadows_prep.hlsl`.
- `-Validation -Move`: the pan left the foundry for the docks (a large change of
  geometry and an outdoor view). No faults, no non-finite values, IDs as above.
- Missing light-list bindings, no TLAS or a stale one (more than one frame old): the
  pass does not run, and the game's lighting is untouched. It only ever removes light
  the game added this frame.
- `fg=2` (`-RealPresent`, DLSS-G 2x) with D-LS on: DLSS-G status 0x0, 2 presents per
  rendered frame, 0 Streamline API errors; the pass costs the same (0.331 ms).

### 9.7 What does not work / next steps

- **Projector lights** (b29, about 1 per pixel here) are not shadowed. Their loop has
  its own falloff and cookie terms; adding them follows the same pattern.
- **Shadowed lights** are skipped by default. `ls_shadowed=1/2` are untested in a scene
  that has them (a torch-lit interior with shadow-casting lights is needed, R6).
- **Art intent.** Every unshadowed light is shadowed, including fill lights that may be
  meant to leak and the light that follows the active character (its wedge-shaped
  shadow on the floor). `ls_min_radius` and `ls_strength` exist as controls. A
  per-channel exclusion was not needed here.
- **Off-screen casters** are missing from the TLAS, so leaks through geometry the camera
  does not see remain (cascade-caster capture, 6.7 item 2, and D-EN3 retention).
- **Exactness.** The transmittance and in-scatter are not replicated (they are why 9.3
  stops at a fit), and the delta is clamped at 0 for that reason.
- **Light source radius.** BG3 gives none; 0.05 is a guess, to tune by eye.
- **`rr=0`**: hard shadows only (no SIGMA).

Settings (`bg3rtx.ini`, all live): `rt_local_shadows`, `ls_shadowed`, `ls_source_radius`,
`ls_soft`, `ls_clear`, `ls_min_radius`, `ls_samples`, `ls_strength`, `ls_debug`
(1 lights per pixel, 2 light removed ×4, 3 traced visibility, 4/5/6 validation),
`ls_stats`. The log reports `ls: first frame ...` (the bindings),
`ls: local-light shadow pass GPU median ...` and, with `ls_stats=1`, `ls stats` and
`ls validation fit`.

## 10. D-RF: ray-traced reflections (implemented, tiers (a) and (c))

Status (2026-10-26): **implemented, on by default** (`rt_reflections=1`, live; needs `rt=1`
and `gi=1`). Code: `shaders/gi/rt_reflections.hlsl` (the pass), `shaders/rt/bg3_ambient.hlsli`
(the game's specular ambient, D-EN4), `core/gi.cpp` (bindings, pass, timing, statistics),
`layer/ngx.cpp` (the RR hit distance), `layer/cmd.cpp` (`query_set`, host ABI 5).
Measured in the Steel Watch Foundry (the auto-run save) and at the docks (end of the `-Move`
pan), RTX 5090, 2560x1067.

### 10.1 The game's specular ambient (D-EN4)

BG3 has no SSR: its only reflections are cube-map probes, applied inside the tiled lighting
`f469db78` (FRAME_MAP "Specular ambient"). The term in b11 is `S · k · SO`:
- `S` (`_1684`): the global probe b35 (and b37) at `R = reflect(−V, N)`, mip `a·8 + _m30 − 8`,
  times the split-sum env BRDF `E = F0·DFG.x + DFG.y` (LUT b38 at `(max(N·V, 1/512), a)`); then
  every local probe listed in the pixel's froxel (b23 `y >> 24`, records b33, cubes in the
  bindless array b45: box/sphere parallax, edge fade, its own roughness and DFG) lerped over it;
- `k = b13[0] · 100`;
- `SO`: Lagarde's specular occlusion from the AO target;
- then the aerial-perspective transmittance (1 indoors; not replicated).

**How the pass sees the probes.** The local probes need the bindless cube array, which
cannot be pushed (push descriptors are capped at 32 and D-LS already uses 30). The host now
exposes `query_set` (ABI 5): the descriptor set the lighting dispatch has bound at set 1, its
`VkDescriptorSetLayout` and its dynamic offsets (2 here). D-RF binds **that set itself** as its
set 2, through a pipeline layout built on the game's own set layout (rebuilt if the layout
changes; it never did). Every probe resource is the game's exact descriptor. The froxel walk
and the lighting constants come from the D-LS push bindings 21-29; the hit distance image is
push binding 30 (31 push descriptors).

**Validation** (`rf_validate=1`, `rf_stats=1`): the D-LS prep pass sets the AO to 1 (so
`SO = 1`) and the sun mask to 0 before the lighting, and D-LS is skipped. b11 then holds only
the local lights (recomputed with the game's own atlas shadows, as D-LS does), projector
lights and `S·k`. Over the pixels without projector lights:

| Foundry, gameplay (same result in two runs) | Value |
|---|---|
| pixels compared | 1.34M per frame |
| game specular (luminance) | 0.01813 |
| local lights + probe term recomputed | 0.01825 |
| **mean \|residual\|** | **0.00011 = 0.58% of the game's** |
| recompute above the game | 0.58% (almost all of the residual) |

The replication meets the < 1% gate of D-EN4. The remaining +0.7% is an excess of the
recompute; its cause (LUT/cube filtering, the transmittance) is not identified. The uber
permutation `34a007cc` handles shading models 2-7 (`RT2.w & 7`) differently (two-sided
foliage blends a back-face probe, model 7 takes F0 from RT3); D-RF only touches models 0 and 1
(`rf_models=3`): 99.8% of the foundry's pixels, 98.5% at the docks. (Validation numbers logged
on loading-screen frames are meaningless: that G-buffer is not the scene's.)

### 10.2 What the pass does

Full resolution, after the lighting and D-LS, **before** the GI composite (which stores this
frame's lit radiance, reflections included, as next frame's `prevRadiance`), SSS and fog.

1. The surface exactly as `f469db78` sets it up (shared with D-LS): `P`, `N` (not
   renormalised), `V`, F0, roughness `a` with the game's specular AA, the shading model.
2. `Tp = S·k·SO` (10.1) and the pixel's `E`.
3. One ray (`rt_closest`, alpha-tested TLAS) from `P + N·dist·rt_bias`, up to `rf_tmax` (100):
   - with RR (`rf_rr=-1` follows `rr`): GGX **bounded VNDF** (Eto & Tokuyoshi 2023), α = a²,
     white noise from `pcg3d(pixel, frame)` on its own stream; a mirror ray for
     `a < rf_mirror_rough` (0.1);
   - without RR: mirror rays only, up to `rf_max_rough_norr` (0.3). There is no denoiser for
     GGX noise there, so the fallback is deterministic and nothing of ours filters (R2).
4. Tiers:
   - **(a)** the hit projects on screen, the depth there is the hit itself (`rf_depth_tol`
     3%) and that surface faces the ray: `L = prev_radiance_at(hit pixel)` (last frame's lit
     diffuse + specular, reprojected with the MVs, QUALITY Q1), weight `w = 1`, faded within
     `rf_edge` of the screen border and between 0.75 and 1 × `rf_max_rough` (0.6);
   - **(c)** a miss, a hit hidden on screen or off screen, or a sample below the horizon:
     `w = 0`, the game's own probe term stays exactly as it was. A TLAS miss is never sky
     (QUALITY R3; roofs are cut away).
   - Tier (b), shading off-screen hits, needs the hit material (D-EN2) and is not done.
5. The exact delta `b11 += w·(L·E − Tp)`, clamped at 0 only against the transmittance it does
   not replicate. The game's `SO` is not applied to traced hits (`rf_so=0`): the ray found the
   occluder and returns its radiance; applying `SO` as well would double-occlude.
6. RR's `DLSSD.SpecularHitDistance` (R16F): the hit distance for tier (a), 65504 elsewhere
   (the probe behaves as an environment at infinity). Non-finite guard on every write (R5).

Per pixel the estimate is stochastic between (a) and (c); in expectation it is
`P(a)·L·E + (1 − P(a))·Tp`, and RR denoises it (R2: no history or filter of ours).

### 10.3 RR guides: the specular hit distance

`layer/ngx.cpp` sets `DLSSD.SpecularHitDistance` (and its subrect base) on every RR evaluate
while the image is fresh (written this frame or the previous one) and at the render
resolution; otherwise the key is cleared, since the game reuses its parameter block. It goes
with the v2 world normals and camera matrices (TRACK_A "Guides v2"). The log line
`[ngx] RR gets the specular hit distance (D-RF)` confirms it, and RR reported no evaluate
error in any run. `rr_spec_hit_distance=0` withholds it (an A/B of its effect was not made).

### 10.4 Results

Foundry, one run, live toggle (`auto-run.ps1 -Traces 4` with `scripts/live_phases.py`,
`dump_png_width=3840`; `scripts/ab_compare.py`):

- [rf_foundry_off.png](img/rf_foundry_off.png) / [rf_foundry_on.png](img/rf_foundry_on.png) /
  [rf_foundry_diff.png](img/rf_foundry_diff.png) (×4): the presented frames, downscaled.
- [rf_foundry_crops.png](img/rf_foundry_crops.png): three 1:1 crops of the 3840x1600 output,
  off | on | diff ×4. Top: the metal pipes along the walkway. With the probe they and the wall
  behind them carry a uniform sheen; with D-RF they reflect the dark wall and walkway they
  face, and the sheen is gone. Middle and bottom: the chimney pipes and the curved beam,
  rough; almost no change (the moving NPC is most of the diff).
- [rf_foundry_probe_term.png](img/rf_foundry_probe_term.png) (`rf_debug=3`): the game's probe
  term as recomputed, i.e. everything D-RF can replace.
- [rf_foundry_tiers.png](img/rf_foundry_tiers.png) (`rf_debug=1`): green tier (a), red hidden,
  orange off screen, blue miss (the walkway's rays leave through the cut-away roof), grey not
  traced. [rf_docks_tiers.png](img/rf_docks_tiers.png): the same at the docks.

| Statistics (`rf_stats=1`, 120-frame windows) | Foundry | Docks (end of the `-Move` pan) |
|---|---|---|
| traced (models 0/1, `a` below the fade) | 54.1% of px (1.48M rays) | 23.3% (0.64M) |
| of the traced: tier (a) on-screen hit | 42.0% | 4.8% |
| hidden on screen / off screen / miss | 20.9 / 6.1 / 28.4% | 1.5 / 3.0 / 5.7% |
| below the horizon | 2.7% | 85.0% (G-buffer under the forward-rendered water) |
| local probes per pixel | 1.29 | 0.01 |
| mean luminance: game specular / probe term / probe term after D-RF | 0.01425 / 0.01093 / 0.01098 | 0.03700 / 0.01714 / 0.01717 |
| presented frame, mean luminance | −0.78%; 10.5% of pixels change by > 2/255 | water animation dominates the diff |
| non-finite values (every dump of every trace, all runs) | **0** | **0** |

The probe term is 77% of the foundry's specular, yet its mean moves by +0.5% only: the
foundry is mostly rough painted metal and stone, and the change is local (concave areas lose
the probe's sheen, faces opposite lit surfaces take their colour). The docks view the pan
ends at is mostly water, which is forward-rendered over the G-buffer; there D-RF changes only
the ship's hull and the quay. **No scene with strongly reflective G-buffer surfaces (wet stone,
polished floors) was reached by the automated runs**, so that A/B (R6) is still owed.

Moving camera (`-Move`, RR on): the presented frame during the pan
([rf_docks_moving.png](img/rf_docks_moving.png)) shows no noise, smearing or black holes on
the reflecting surfaces; 0 non-finite values. The dark dissolve pattern at its left edge looks
like the game's see-through cut-away of the wall in front of the camera, but no D-RF-off frame
of that exact view was captured to prove it. Not measured: a temporal-stability metric and
FLIP against a reference.

### 10.5 Cost (GPU timestamps around the pass, medians of 300 frames)

| Configuration | Foundry | Docks |
|---|---|---|
| **Default (RR, GGX VNDF, 1 ray per px below a = 0.6)** | **0.33 ms** (p10 0.31, p90 0.36) | 0.13 ms |
| `-Validation` | 0.31 ms | — |
| `rf_stats=1` (wave-reduced atomics; measurement only) | 4.1-4.4 ms | — |

`rr=0` (mirror rays up to a = 0.3, 2% of pixels traced) was only timed with `rf_stats=1` on,
so it has no clean number; the probe recompute runs on every pixel either way. The default is
within the 0.5-1.0 ms budget (QUALITY R7). Track D in the foundry: sun + RT GI 0.26-0.28 ms,
D-LS 0.35 ms, D-RF 0.33 ms, about 0.95 ms of the 2.5 ms.

### 10.6 Validation layer and robustness (R5, R8)

- `auto-run.ps1 -Validation`, `rt_reflections=0` against `1` with every mode toggled live in
  the lit foundry (`rf_stats`, `rf_validate` with debug 5, debug 1, 2, 3, 4, 6, `rr=0` (the
  mirror path), off, on): **identical message IDs**, all the game's own (`09600`, `10788`,
  `StorageImage-FormatMismatch`, `OutputNotWritten`, `CopyBuffer-00118`, `08737`, the headless
  `non-acquired-swapchain-image-used`). Binding the game's own descriptor set into our
  pipeline raised nothing.
- `fg=2` (`-RealPresent`, DLSS-G 2x) with D-RF on and RR getting the hit distance: DLSS-G
  status 0x0 on every logged frame (26 reports), 2 presents per rendered frame, 0 Streamline
  API errors; the pass costs the same (0.313 ms).
- Missing inputs (no TLAS or a stale one, a missing light-list binding, the lighting's set 1
  being a push set, `gi=0`): the pass does not run and logs why once; the game's probe term
  is untouched, and RR stops getting the hit distance after 2 frames.

### 10.7 What does not work / next steps

- **Tier (b)**, off-screen hits shaded with the hit material (D-EN2 + EN1 lights): 6% of the
  foundry's rays (and the 21% hidden on screen) fall back to the probe.
- **Off-screen and culled geometry** is missing from the TLAS (D-EN3), so hits behind the
  camera are probe-shaded, and misses through cut-away roofs keep the probe (by design).
- **A reflective-scene A/B** (wet stone, polished floors) and the temporal-stability metric.
- **Shading models 2-7** are left to the game (`rf_models`).
- **The transmittance** is not replicated (delta clamped at 0), as for D-LS.
- **`rr=0`**: mirror rays only, up to `a = 0.3`; NRD REBLUR is not integrated.
- **Bounce source.** `prevRadiance` holds view-dependent specular (QUALITY Q8). For reflections
  that is the usual SSR approximation (the hit's highlight as the camera sees it).
- `stats_begin` adds 3.8-4 ms when `rf_stats=1`, as for D-LS: measurement only.

Settings (`bg3rtx.ini`, all live): `rt_reflections`, `rf_max_rough`, `rf_max_rough_norr`,
`rf_mirror_rough`, `rf_rr`, `rf_tmax`, `rf_depth_tol`, `rf_edge`, `rf_strength`, `rf_so`,
`rf_models`, `rf_debug` (1 tiers, 2 change ×4, 3 probe term, 4 probe term after D-RF,
5 validation residual, 6 hit distance), `rf_stats`, `rf_validate`, `rr_spec_hit_distance`.
The log reports `rf: first frame ...` (the lighting's set and its layout),
`rf: reflection pass GPU median ...`, `[ngx] RR gets the specular hit distance` and, with
`rf_stats=1`, `rf stats` and `rf validation`.

Tooling added with D-RF: `auto-run.ps1 -MoveSec N` (release the pan key after N s), and the
`trace.request` file trigger now works (its path had a `\t`, a tab, so it never fired).

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
